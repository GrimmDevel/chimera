# План перехода Chimera OS на Multicore

## Цель

Перевести ядро с устойчивого однопроцессорного режима на корректную работу с
несколькими CPU без порчи памяти, зависаний и потери интерактивности.

До завершения стадий 1–5 запуск по умолчанию остаётся с `-smp 1`.

## Референсная база: ravynOS

В дереве [`ravynos`](ravynos/) можно сверяться с зрелыми реализациями и
документацией FreeBSD/XNU-подсистем. Это источник архитектурных идей,
инвариантов и тестовых сценариев, а не код для механического копирования.

- Для VM и `pmap`: `ravynos/BSD/share/man/man9/pmap*.9`.
- Для планировщика: `ravynos/BSD/share/man/man9/scheduler.9`.
- Для модели обработки прерываний: `ravynos/BSD/share/man/man9/intr_event.9`.
- Для реализации сначала искать аналог в `ravynos/Kernel/xnu/`, затем
  адаптировать минимальный интерфейс к структурам и ABI Chimera.

Перед переносом любой идеи нужно зафиксировать: какой инвариант решается,
какие lock'и и контексты выполнения участвуют, а также какой regression-тест
подтверждает результат в Chimera.

## Принципы

- Каждая стадия имеет проверяемый результат до включения следующей.
- Нельзя удерживать spinlock через `context_switch`, I/O или ожидание события.
- Обработчик прерывания выполняет минимум работы; тяжёлая работа переносится в worker/softirq.
- Освобождать страницу после изменения отображений можно только после TLB ACK всех затронутых CPU.
- Сначала проверка на 2 CPU, затем на 4 CPU.

## Стадия 0 — Стабильный baseline

**Статус:** завершена.

**Цель:** сохранить воспроизводимую однопроцессорную загрузку.

- Оставить `-smp 1` в скриптах запуска.
- Ввести `CHIMERA_VCPUS` для контролируемых QEMU-тестов; значение по умолчанию — `1`, допустимый диапазон — `1..16`.
- Зафиксировать, что kernel-параметры `smp=off|bringup|on` будут добавлены в стадии 1 после прокладки cmdline через EFI loader.
- Зафиксировать smoke-тест: boot → zsh → ввод → fork/exec → FAT32 → сеть → USB.

**Готово, когда:** 100 последовательных запусков с `CHIMERA_VCPUS=1` проходят без panic и зависаний.

Базовые команды:

```sh
make run                         # стабильный baseline, 1 vCPU
CHIMERA_VCPUS=2 make run         # только диагностический SMP-тест
CHIMERA_VCPUS=4 make qemu        # только после стадии 1
```

## Стадия 1 — Надёжный запуск AP

**Статус:** завершена — подтверждены SIPI и состояние `ONLINE_IDLE` для 2 и 4 vCPU.
Все AP корректно выходят в long mode с отдельными GS base, CR3 и kernel stack,
после чего безопасно переходят в `hlt`-цикл ожидания. BSP стабильно загружается
до интерактивного `zsh` на CPU 0; планировщик на AP отключён.

**Цель:** AP корректно выходит из INIT/SIPI и безопасно простаивает.

- Исправить handshake AP: одинаковое значение `ap_alive` у trampoline и BSP.
- Ввести состояния CPU: `OFFLINE`, `STARTING`, `ONLINE_IDLE`, `SCHEDULABLE`, `FAILED`.
- Ограничивать число CPU размером `g_cpu_data` до любого доступа к массиву.
- Копировать trampoline и таблицы только через HHDM.
- Заменить `volatile`-ожидания на acquire/release атомики.
- Логировать APIC ID, GS base, CR3, RSP и причину таймаута.

**Готово, когда:** `-smp 2` и `-smp 4` 100 раз доходят до `ONLINE_IDLE`; пользовательские потоки всё ещё выполняются только на BSP.

## Стадия 2 — IRQ и время

**Статус:** завершена — LAPIC timer получил отдельный vector `0xE0`, калиброван через TSC, подтверждается строго через LAPIC EOI. Монотонное время single-writer на BSP; broadcast IPI storm устранён благодаря per-CPU `cpu_need_resched`.

**Цель:** каждый источник прерываний имеет ясный vector и правильный EOI.

- Выделить отдельные vectors для PIT, LAPIC timer, reschedule IPI, TLB IPI и device MSI.
- LAPIC timer не должен подтверждаться через PIC EOI.
- Сделать системное время single-writer: BSP/tick source обновляет monotonic clock; AP только читают его.
- Ввести per-CPU `need_resched`; убрать broadcast-reschedule на каждом таймерном interrupt.
- Настроить локальный tick AP только для локального timeslice.

**Готово, когда:** счётчики IRQ соответствуют частоте таймеров, нет IPI storm, система остаётся отзывчивой на 2 и 4 CPU.

## Стадия 3 — Per-CPU scheduler

**Статус:** завершена — глобальная run queue заменена на per-CPU очереди, развёрнуты per-CPU idle threads, `current_thread` и stack перенесены в `cpu_local_t`, реализованы таргетированные IPI пробуждения, work-stealing load balancing и защита от одновременного запуска стека на двух ядрах. Проверено одновременным исполнением 4 воркеров smpdemo на 4 ядрах.

**Цель:** задачи могут безопасно выполняться параллельно.

- Заменить глобальную run queue на per-CPU run queues.
- Создать idle thread для каждого CPU.
- Хранить `current_thread`, kernel stack и `need_resched` только в CPU-local данных.
- Реализовать remote enqueue и wakeup IPI только при пробуждении idle CPU.
- Добавить редкую load balancing процедуру, а не глобальный lock на каждом tick.
- Формализовать переходы состояний thread и запретить одновременный запуск одного thread на двух CPU.

**Готово, когда:** два CPU-bound процесса исполняются одновременно; stress `fork`, `wait`, `sleep`, wakeup не даёт hang или двойного запуска thread.

## Стадия 4 — SMP-безопасный pmap и TLB shootdown

**Статус:** завершена — реализовано отслеживание активного CR3 на каждом CPU (`cpu_active_cr3`), синхронный межъядерный TLB shootdown по протоколу с маской подтверждений (`s_tlb_ack_mask`) и таймаутом, вынос инвалидации TLB за пределы `s_pmap_lock` для исключения IPI deadlock, освобождение физических страниц только после завершения shootdown, а также корректная инициализация CR0/CR4/XCR0 на AP ядрах (исключающая `#GP` при `XRSTOR`). Проверено сериями тестов с fork/execve/exit и параллельным 4-ядерным `smpdemo`.

**Цель:** исключить stale TLB и освобождение используемой физической памяти.

- Вести `active_cpu_mask` для каждого address space.
- Ввести request/epoch для TLB shootdown и ACK от каждого target CPU.
- Освобождать PTE, page table и physical page только после всех ACK.
- Обработчик TLB IPI должен получать адрес/диапазон, а не всегда сбрасывать весь CR3.
- Разделить глобальный pmap lock на map/page-table уровни после появления корректного протокола.
- Отдельно закрыть COW и shared-memory lifetime ошибки.

**Готово, когда:** длительный тест `fork + mmap + munmap + COW write` на 2/4 CPU проходит без page fault, use-after-free и memory corruption.

## Стадия 5 — Locking и жизненный цикл объектов

**Цель:** убрать гонки и взаимные блокировки между подсистемами.

- Описать lock order и закрепить его в документации.
- Запретить sleep, disk I/O, `kprintf` и scheduler switch под spinlock.
- Защитить данные, одновременно используемые ISR и потоками, через `irqsave` или lock-free queue.
- Добавить debug-инструменты: owner CPU, время удержания, assert на recursive lock и lock-order проверки.
- Проверить proc/task/IPC/VFS/FAT32/network/console на глобальные mutable state без синхронизации.

**Готово, когда:** parallel filesystem, IPC и socket stress-tests не показывают deadlock и нарушения инвариантов.

## Стадия 6 — Drivers: ISR и workers

**Цель:** убрать polling и тяжёлую работу из timer interrupt.

- Перенести HID/xHCI обработку из timer ISR в input worker.
- Для e1000: ISR подтверждает причину, RX/TX разбирает net worker.
- Добавить IRQ/MSI routing через IOAPIC/MSI abstraction.
- Заменить busy-wait задержки и polling loops на timer/callout/wait queue.
- Добавить affinity для высокочастотных IRQ и очередь передачи между CPU.

**Готово, когда:** USB-ввод, сеть и storage остаются отзывчивыми под CPU stress без работы драйверов в timer ISR.

## Стадия 7 — Поэтапное включение

1. `-smp 2`, AP в idle.
2. `-smp 2`, kernel workers на AP.
3. `-smp 2`, user threads на обоих CPU.
4. `-smp 4`, тот же набор тестов.
5. Сделать `-smp 4` default только после длительного soak-теста.

Для каждого шага запускать:

- 100 boot cycles;
- scheduler stress (`fork`, `exec`, `wait`, sleep/wakeup);
- VM stress (`mmap`, `munmap`, COW, shared memory);
- parallel FAT32 create/write/unlink;
- network RX/TX под нагрузкой;
- USB/console interactive test.

## Критерий завершения

Multicore считается готовым, когда `-smp 4` стабильно проходит все regression и
stress-тесты, не содержит fire-and-forget TLB invalidation, не выполняет polling
драйверов в timer ISR и не требует глобального context-switch mutex.
