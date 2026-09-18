# Production Readiness Audit Report

**Проект:** Chimera OS (xiu) — гибридное Mach/BSD-ядро x86_64, Mach-O kernel (`mach_kernel`), кастомный EFI-загрузчик
**Дата аудита:** 2026-08-29
**Область аудита:** всё, что реально собирается (`kernel/CMakeLists.txt` + `Makefile.darwin`, ~20 300 LOC: `arch/x86_64`, `mm`, `mach`, `bsd`, `vfs`, `drivers`, `net`, `chimerakit`), загрузчик `boot/efi/`, libsystem. Вендорная копия XNU (`kernel/osfmk`, `kernel/bsd/kern`, `iokit`, `libkern`, ~1.1M LOC) **не входит в сборку** и рассматривалась только как источник ложных заявлений README.

---

## 1. Executive Summary

- **Production Readiness Index: 19 / 100** *(на момент аудита; после цикла исправлений, см. «Журнал исправлений», — ~38/100, переоценка запланирована после закрытия сетевых и SMP-блокеров)*
- **Краткое резюме:** Ядро является качественным **функциональным прототипом уровня "Stage-1"**, но не боевой ОС. Фундамент частично честный (buddy-аллокатор PMM, ticket-спинлоки, copyin/copyout с проверкой границ, PIO-драйверы пишут в реальные регистры), однако в каждой подсистеме найдены дефекты, которые в реальной среде дают либо **user-triggerable kernel panic**, либо **молчаливую порчу данных**, либо являются **фасадом** (SMP, сигналы, launchd, TCP-сервер, `ftruncate`). Критические блокеры: лимит 64 потока с паникой ядра по действию пользователя; `copyout` пишет через HHDM мимо COW (порча памяти родителя после `fork`); `munmap` разделяемых shm-страниц освобождает физические страницы, всё ещё смаппленные другим процессам (cross-process use-after-free); EFER.NXE не включён при том, что `mprotect` выставляет NX-бит (reserved-bit fault); sysret без проверки каноничности RCX (ring-0 #GP); AP-ядра никогда не стартуют (нет SIPI). Полнота/зрелость: подсистемы связаны в работающий демонстрируемый конвейер (boot → zsh → GUI → сеть → FAT32), но ни одна не прошла уровень отказоустойчивости и защиты "боевого" ядра.

**Ключевые числа:**

| Метрика | Значение |
|---|---|
| Критических дефектов (Blockers) | 21 |
| User-triggerable kernel panic | 4 независимых пути |
| Молчаливая порча данных (data corruption) | 7 путей |
| Фасадные/заглушенные подсистемы | 8 |
| Макс. процессов / потоков / портов | 64 / 64 / 4096 (паника при переполнении портов) |
| IRQ-драйверов (настоящих, прерывание-driven) | 0 из 5 (ATA, e1000, xHCI, PS/2, PIT — всё polling) |

---

## 1.1 Журнал исправлений (обновляется по мере работы)

> Статусы по дефектам из раздела 2: ✅ исправлено · 🔶 исправлено частично · ⬜ не трогали.

**Стадия 1 — Модель памяти и вход в ядро (P0)** — ✅
1. `EFER.NXE` включён (`kernel_stubs.c`, `smp.c`) — NX-бит enforced, `mprotect` без EXEC работает.
2. **W^X в Mach-O loader**: `__DATA`/BSS/стек мапятся с NX, исполняем только `__TEXT` (`mach_loader.c`, `idt.c`, `copy.c`); флаг `pmap_map_user_page` переведён на `u64` во всех 7 файлах.
3. **OOM в `sys_execve`**: `-ENOMEM` вместо зануления физической страницы 0 и `cr3=0`.
4. **Перезапись PTE** (`pmap_map_user_page_unlocked`): старая страница освобождается (fixed-mmap больше не течёт), с guard-ом на remap той же страницы.
5. **COW-aware `copyout`**: запись в RO+COW ломает COW, в честно RO — `EFAULT`. Порча памяти родителя после `fork` устранена.
6. **sysret hardening**: каноничность user RIP/RSP проверяется по стек-слотам без порчи регистров (`syscall_entry.S`); неканоничный возврат → SIGSEGV вместо ring-0 #GP.

**Стадия 2 — Пулы вместо паник + честная паника (P0)** — ✅
1. Динамический run queue (`scheduler.c`): лимит 64 снят, `scheduler_add_thread()` возвращает код; `fork/spawn` при отказе корректно откатываются через `proc_mark_exited`.
2. Пулы proc/task/spawn/fork-threads расширены 64 → 256.
3. Арена Mach-портов: freelist-рециклинг + `ENOMEM` вместо panic; счётчик ipc_space защищён от wraparound.
4. `space_alloc_name` без паники; все вызывающие проверяют результат (закрыт OOB в `sys_mach_lookup_service`).
5. `thread_init_stack` — ошибка вместо panic; утечка 16 КиБ kernel-стека на повторный `spawn` устранена.
6. **Честная `chimera_panic`**: CPU/uptime/CR0–CR4, best-effort backtrace по RBP с валидацией фреймов; ядро собирается с `-fno-omit-frame-pointer`.

**Стадия 3 — Блочный слой и FAT32 (P0)** — ✅ (частично, см. остатки в 2.11)
1. **ATA LBA48**: `READ/WRITE SECTORS EXT` при детектированной поддержке, отказ вместо усечения LBA>2²⁸, `FLUSH CACHE EXT`; flush один на вызов.
2. **Time-based таймауты** через TSC (`timer_tsc_hz()`), лог Error-регистра при отказе.
3. **`fat32_resize_node()`**: рост с zero-fill, усечение с освобождением цепочки + EOC + запись dir entry; подключены `sys_truncate`/`sys_ftruncate`/`O_TRUNC` (раньше фальшивые), `-EFBIG` >4 ГиБ.

**Стадия 4 — Ownership разделяемых страниц (P0)** — ✅
1. Таблицы shm/поверхностей владеют ссылкой; каждый маппер добавляет свою (`pmm_retain_page`) — cross-process UAF через `munmap` устранён.
2. `pmap_unmap_user_range_ex(..., release)` — framebuffer освобождать нельзя (раньше страницы видеоуходили в buddy, в т.ч. при выходе WindowServer'а).
3. `chimera_mmap_record_t` в задаче: `munmap`/exit снимают map_count, страницы возвращаются при уходе последнего маппера; OOM в mmap → `-ENOMEM`.

**Стадия 5 — Sleep-очереди (P1)** — ✅
1. Глобальный sleep-список + `thread_sleep_until()` + пробуждение из PIT-прерывания (`timer_wake_sleepers`); поле дедлайна в конце `chimera_thread_t` (вне asm-смещений).
2. `nanosleep` (с EINTR), `wait4` (сон 200 мс + ранний wake по SIGCHLD), `poll`/`select` (рескан 20 мс) — busy-poll в ядре больше не жжёт кванты.

**Стадия 6 — Корректность и безопасность syscall (P1)** — ✅
1. VFS: tombstone-пробинг вместо лимита 128 слотов (фантомные ENOENT устранены).
2. `fchdir` — настоящий обработчик (fd → VDIR → CWD).
3. `readv`/`writev` — батчи по 16 (все iovcnt до 1024 honoring); `poll` — весь набор fd через kalloc (был молчаливый кламп до 32).
4. `sendto`/`recvfrom` — стриминг чанками для TCP-потоков (протест >1500 байт больше не теряется), `-EMSGSIZE` для UDP.
5. `chmod`/`fchmod` — владелец/root; `chown`/`fchown` — root.
6. `kfree` — sanity large-заголовка + громкий лог чужих указателей (произвольная порча PMM через double-free невозможна).

**Стадия 7 — Bounded blocking mach_msg (P1)** — ✅
1. `ipc_mqueue_send`: полная очередь + timeout>0 → блокировка с дедлайном на `imq_send_waiters`; timeout==0 — прежнее мгновенное `PORT_FULL`.
2. `ipc_mqueue_receive`: ожидание ограничено дедлайном (было «навсегда»); dequeue будит отправителя; `ipc_port_destroy` будит и отправителей (PORT_DEAD).
3. Новый примитив `wait_queue_sleep_until_irqrestore()` (wait-queue + timer-список).

**Стадия 8 — Настоящая доставка сигналов (P1)** — ✅
1. Доставка на syscall-return: `sigctx` (9 регистров + rax + маска + magic) и вызов handler(sig) через **RX-трамплин «mini-vdso»** на фиксированном VA 0x70000000 (стек NX, исполняемый код на стеке невозможен).
2. `SYS_sigreturn` (184): валидация magic, восстановление регистров/маски, возврат оригинального rax. Прерванный syscall для пользователя выглядит завершённым.
3. BSD-семантика SIGCHLD: успешный `wait` поглощает pending-уведомление (нет гонки reap-main vs reap-handler).
4. Подтверждено логами: полный цикл EXIT → proc_signal → deliver → handler → sigreturn → wait4 reap, повторно.

**Стадия 9 — TCP LISTEN/accept + ARP (P1)** — ✅ (частично, см. остатки)
1. **TCP-сервер**: `tcp_listen()` (LISTEN на bound-сокете), полный three-way handshake — SYN → child-PCB (SYN_RECEIVED) + SYN|ACK → ACK → ESTABLISHED → перемещение из `so_q0` в accept-очередь `so_q`; backlog-лимит (SYN при полной очереди молча отбрасывается).
2. **Двухпроходный demux** в `tcp_input`: точное совпадение 4-tuple приоритетнее LISTEN-fallback (раньше листенер перехватывал чужие сегменты).
3. **`sys_accept`** — настоящий: блокирующий (сон 20 мс + поллинг RX), O_NONBLOCK → `-EWOULDBLOCK`, создаёт fd для принятого соединения, копирует peer-адрес; `soaccept`/`so_new_child`/`so_q0_to_q`/`so_free` на слое сокетов (BSD-схема so_head/so_q0/so_q).
4. Закрытие листенера освобождает очереди и PCB детей.
5. **seq-валидация** в ESTABLISHED: сегмент не по `rcv_nxt` отбрасывается (in-order only, OOO/SACK — открыто).
6. **ARP**: при исчерпании ретраев — `CHIMERA_ERR_TIMEOUT` вместо фабрикации MAC и тихой потери кадров.

**Стадия 10 — MSI-прерывания NIC (P1, 2.13 частично)** — ✅
1. PCI: `configRead16/Write16/Read8/Write8`, `pci_enable_msi()` — обход capability-списка, программирование MSI-адреса (0xFEE00000, physical dest APIC 0) и data (vector, edge/fixed), enable + INTx-disable в command.
2. e1000: MSI включается при инициализации (vector 0x50), `IMS = RXT0|TXDW`; `e1000_isr()` читает ICR, осушает RX-кольцо, write-back очистка; `[e1000] interrupt path active (MSI)` — одноразовое подтверждение.
3. `interrupt_handler`: vector 0x50 → `e1000_isr()` + LAPIC EOI. Поллинг-пути оставлены как страховка.
4. **Барьер готовности**: APs spin-wait на `g_smp_ready` (kernel_main ставит после ВСЕХ фаз) — APs не трогают подсистемы до полной инициализации. `gdt_init_ap` больше не вызывает `cpu_enable_features()` (BSP-only — фикс гонки на глобальных FP-глобалах).
5. Откаты: hid_poll в ISR, IOAPIC-роутинг — открыто.

**Стадия 11 — Права Mach-портов + блокировки FAT32 (P1)** — ✅ *(подтверждено: полный boot, zsh/fastfetch на ген-именах портов, MSI-сеть)*
1. **2.8 права**: `ipc_port_lookup` теперь требует запрошенное право (`required_right & ie_bits`) — SEND-имя больше не может принимать из чужого порта.
2. **2.8 имена**: генерационные имена `(gen << 6) | idx` — `ie_gen` в `ipc_entry_t`, инкремент при free; устаревшее/подделанное имя не резолвится (`lookup`/`deallocate`/`type` проверяют gen). Все `is_table[name]`-обращения декодируют индекс (8 точек).
3. **2.11.6 локи каталогов**: `fat32_create_file/create_dir/unlink_file` выполняются под `s_fat_lock` через `_locked`-тела с lock-free хелперами (иначе self-deadlock); `fat32_update_dir_entry` — без внутреннего лока (вызывается только под ним).
4. **Латентный deadlock** в `fat32_resize_node` (вызов locked `fat32_free_cluster_chain` под локом) устранён через `_unlocked`-вариант — ftruncate реального файла больше не вешает машину.

**Стадия 12 — XSAVE/AVX (P1, 2.17)** — ✅ *(подтверждено: `xsave mask=0x7 area=832 (AVX)`, полный цикл переключений, zsh/fastfetch; попутно найдены и закрыты 3 каскадных дефекта — см. ниже)*
   - Дефект A: `thread_init_stack` занулял xsave-заголовок → `XSTATE_BV[0]=0` при `XCR0[0]=1` → #GP на первом xrstor (SDM-требование); фикс — заголовок заполняется маской XCR0.
   - Дефект B: kalloc возвращал 8/16-байтно выровненные указатели → xsave-область внутри kalloc'd потока не 64-выровнена → #GP; фикс — 64-байтное выравнивание всех kalloc/zalloc аллокаций (заголовок в 64-байтном префиксе).
   - Дефект C (бутлуп): `mov edx, mask_hi` в switch.S затирал `rdx` = новый CR3 → `mov cr3, 0` → silent triple fault; зафиксирован hardware-логом QEMU (`-d int`); фикс — new_cr3 сохраняется в r12. Диагностика снята.
1. `cpu_enable_features`: OSXSAVE (CR4.18), программирование `XCR0 = x87|SSE|AVX` (бит AVX — при наличии), верификация через `xgetbv`; экспорт маски и размера области (`g_fpu_mask_lo/hi`, `g_fpu_area_size`).
2. `switch.S`: `fxsave64/fxrstor64` → `xsave64/xrstor64` с runtime-маской — YMM-половины AVX-регистров больше не теряются на каждом переключении.
3. `th_fp_state` 512 → 2696 байт (максимум x87+sse+avx+opmask), 64-байтное выравнивание сохранено; `thread_init_stack` клампит размер области.
4. Деградация: без XSAVE/AVX — корректный fxsave-фоллбэк (маска 0x3, 512 байт).

**Стадия 14 — Free-cluster bitmap FAT32 (P1, 2.11.3)** — ✅
1. При монтировании — один проход по FAT-копии №1: битмапа свободных кластеров (бит на кластер, `bit = c-2`), счётчик свободных, running-hint; лог `Free-cluster bitmap: N/M clusters free`.
2. `fat32_alloc_cluster()`: поиск первого свободного по битмапе от хинта (word-scan, один wraparound) вместо полного O(FAT) скана через PIO; классический скан оставлен как fallback (bitmap > 8 МиБ или не построилась).
3. Синхронизация битмапы — в единственной точке изменения FAT-записей `fat32_set_fat_entry` (val==0 → free, иначе → used) + хинт опускается при освобождении. Всё под `s_fat_lock`.

**Стадия 15 — Настоящий launchd (P1, 2.18)** — ✅ *(подтверждено: proclist показывает PID 1 = /bin/launchd, PID 2 = zsh (ребёнок), launchd reaper активен)*
1. `/bin/launchd` — userland init-reaper: вечный `waitpid(-1)` — репание сирот, репарентируемых ядром на PID 1 (пул процессов больше не заполняется зомби).
2. `launchd_chimera_start()` грузит бинарник в PID 1 (vfs_lookup → FAT32 → mach_load_args → thread → scheduler); при ошибке — деградация в kernel-stub.
3. kernel_main спавнит zsh напрямую (без launchd-посредника — убирает гонку двух шеллов за консоль); launchd работает параллельно как reaper.
4. PID 1 защищён от kill -9/15/19 (уже было), zsh — ребёнок PID 1 (spawn-путь использует proc_launchd как parent).

**Стадия 16 — SMP bring-up (P1, 2.6/2.7)** — ✅ (код готов, требует boot-тест)
1. **MADT-парсер** (`madt.c`): RSDP → XSDT/RSDT → MADT ("APIC") → LAPIC IDs + IOAPIC bases + GSI.
2. **AP-trampoline** (`ap_trampoline.S`): 16-bit real → 32-bit protected (PAE+LME+PG) → 64-bit long mode; копируется в 0x7000 (SIPI vector 0x07); identity page tables (PML4+PDPT, 1GB pages) на 0x8000/0x9000.
3. **SIPI последовательность**: INIT → wait → SIPI → wait → SIPI (backup) → alive flag polling.
   - Fix: identity page tables не мапят kernel higher-half → AP переключается на kernel CR3 (записан BSP'ом в trampoline data) **перед** прыжком в C entry.
4. **Per-CPU init** (`smp_ap_entry_64`): per-AP GDT/TSS/GS-base/syscall/LAPIC/timer → scheduler_ap_run.
5. **LAPIC timer**: только на BSP (PIT делает preemption; APs — IPI-driven только, иначе 5×100Hz lock contention на глобальном run queue при TCG = фриз).
6. TLB-shootdown: IPI fire-and-forget (существующий код) — работает при g_active_cpus > 1.
7. **BSP-only планировщик**: scheduler_yield возвращается немедленно на non-BSP CPU — глобальный run queue + g_current_thread не SMP-safe; per-CPU run queues — отдельная большая задача.
8. **SMP-safe архитектура (стадия 17)**:
   - BSP-only барьер снят — scheduler_yield работает на всех CPU
   - `g_current_thread` глобал удалён (per-CPU `gs:[0]` — единственный источник)
   - Консольный spinlock (`s_console_lock`) на `kvprintf`
   - **`sti` после `popfq` УДАЛЁН** — ставил IF=1 внутри критических секций → другой CPU входил в тот же спинлок → порча. Корректный механизм: popfq восстанавливает IF=0 (ISR), iretq в конце ISR восстанавливает IF=1 (user mode). Каждый поток сам восстанавливает свой IF через iretq.
   - LAPIC timer на всех AP (100Hz per-AP preemption)
   - `-smp 4` включён

**Стадия 17 — Полный SMP-safe аудит + локи (P0)** — ✅
Систематический аудит всех разделяемых глобалов. Добавлены спинлоки:
1. **Сетевой стек**: `s_net_input_lock` — сериализация всего `if_input` (диспетчер IP/TCP/UDP/ARP/loopback), иначе 2+ CPU обрабатывают пакеты одновременно.
2. **HID/PS-2**: `s_hid_lock` (XIUKit::s_hid_lock) — сериализация `chimerakit_hid_poll` (один PS/2-контроллер, 2+ CPU читают порт 0x60 — разные байты).
3. **Mach-сервисы**: `s_services_lock` на register/lookup.
4. **g_fpu_area_size** глобальная мутация из thread_init_stack удалена (race: multiple CPUs writing).
Уже имевшие локи (подтверждено аудитом): PMM ✓, zones ✓, pmap ✓, run_queue ✓, FAT32 ✓, e1000 ✓, IPC spaces/ports ✓, fileproc pool ✓, proc pool ✓, task pool ✓, console output ✓.

**Стадия 17c — Контекст-свитч мьютекс + снижение AP-таймера** — ✅
1. `s_cs_lock` (глобальный мьютекс context_switch) — только один CPU выполняет context_switch одновременно; лок освобождается при следующем scheduler_yield на том же CPU (после возобновления потока).
2. LAPIC timer на APs снижен до 25Hz (BSP PIT 100Hz) → суммарно 200 таймерных IRQ/сек вместо 500.
3. Причина фриза: 4 LAPIC timer × 100Hz + PIT = 500 lock acquisitions/sec на s_runq_lock + s_cs_lock при TCG (последовательная эмуляция) — CPU тратили всё время на lock contention.

### Остатки (открытые пункты)
⬜ 2.6/2.7 SMP-фасад и TLB-shootdown · ⬜ 2.8 права портов (required_right всё ещё игнорируется) и предсказуемые имена · ⬜ 2.11.3 free-cluster bitmap · ⬜ 2.11.5 кластеры >4 КиБ · ⬜ 2.11.6 блокировки каталогов · ⬜ 2.12 TCP (LISTEN/accept, ретрансмиссия, seq-валидация), ARP-таймауты, IRQ-driven e1000 · ⬜ 2.13 IOAPIC/MSI + LAPIC timer, hid_poll в ISR · ⬜ 2.15 boot-memmap enum'ы · ⬜ 2.17 XSAVE/AVX · ⬜ 2.18 launchd · ⬜ 2.19 компонентный lookup · ⬜ 2.20 malloc · ⬜ 2.21 гигиена репозитория.

---

## 2. Критические дефекты (Blockers / Toy-code)

> Шаблон: **Файл:строка → функция** · Категория · Почему это toy-code (сценарий отказа) · Как должно быть · План рефакторинга.

### 2.1 Планировщик: жёсткий лимит 64 потоков с паникой ядра
- **Статус:** ✅ ИСПРАВЛЕНО (стадия 2)
- **Файл и строка / Функция:** `kernel/arch/x86_64/scheduler.c:11,120-126` → `scheduler_add_thread()`
- **Категория:** Наивный алгоритм / Уязвимость (user-triggerable panic)
- **Что найдено:** `#define SCHED_MAX_THREADS 64`; глобальный массив `g_run_queue[64]`, в котором живут **все** потоки системы (состояние фильтруется флагом). При попытке добавить 65-й поток — `chimera_panic("run queue full")`. Потоки создаются через `SYS_fork`/`SYS_posix_spawn`, т.е. **любой непривилегированный процесс крашит всю ОС одним циклом `for(;;) fork();`**. Пул `s_proc_pool[64]` (`bsd/proc.c:17`), `s_task_pool[64]` (`proc.c:442`), отдельные статические пулы потоков `s_spawn_threads[64]` (`syscall.c:839`) и `s_fork_threads[64]` (`syscall.c:934`) — четыре несогласованных "лимита по 64", суммарно не бьются с капом планировщика.
- **Как должно быть:** Динамический per-CPU run queue (XNU: `runq` в `percpu` + `rt_queues`), потоки в зоне `threads` (zalloc, растёт), лимиты через `RLIMIT_NPROC`/`kern.maxproc`, при исчерпании — `EAGAIN` пользователю, никогда не panic.
- **План рефакторинга:**
  1. Заменить статический массив на связный список/очередь с `zalloc`-выделением узлов; убрать panic → вернуть `-EAGAIN` из `fork/spawn`.
  2. Единый пул `chimera_thread_t` в зоне (сейчас их 4), refcount на дескриптор потока.
  3. Ввести `sys_setrlimit` и проверку `p_task->ta_thread_count` против лимита до создания потока.

### 2.2 VM/copy: `copyout` обходит COW — порча памяти родителя после fork
- **Статус:** ✅ ИСПРАВЛЕНО (стадия 1)
- **Файл и строка / Функция:** `kernel/mm/copy.c:80-105` → `copyout()`; `kernel/mm/pmap.c:207-272` → `pmap_clone_user_space()`
- **Категория:** Уязвимость / Архитектурный дефект
- **Что найдено:** После `fork` страницы родителя и ребёнка переведены в RO+PAGE_COW с refcount>1. `copyout` **не читает флаги PTE** — только `pmap_extract()` (которому достаточно `PAGE_USER`), после чего пишет в физическую страницу **напрямую через physmap (HHDM)**: `__builtin_memcpy(hhdm_ptr, kaddr, chunk)`. Запись в COW-страницу не триггерит fault → `write()` в область, попавшую под COW (например, буфер в heap при стеке-пейджинге `copyout` в окне `USER_STACK_MIN..MAX`, или OOL-данные), **тихо изменяет память родительского процесса**. Это межпроцессная порча данных, а не EFAULT.
- **Как должно быть:** `copyout` обязан проверять PTE на RW/COW и либо провоцировать COW-обработку (`pmap_handle_cow_fault`-логика), либо выполнять запись по реальному пользовательскому адресу под `stac/clac` с fault-tolerant copy (как XNU `copyout` через `fleur64`/`movsb` + PCID-fault fixup).
- **План рефакторинга (diff по сути):**
  ```c
  u64 pte = pmap_get_pte_val(pml4_phys, curr_uaddr);      // читать сам PTE
  if (!(pte & PRESENT) || !(pte & USER)) return EFAULT;
  if (!(pte & WRITE)) {
      if (pte & PAGE_COW) {
          if (!pmap_handle_cow_fault(pml4_phys, curr_uaddr & ~0xFFFULL))
              return EFAULT;                                 // COW-break, затем рестарт итерации
      } else return EFAULT;                                  // RO-страница — писать нельзя
  }
  ```
  Долгосрочно: копирование через user VA с обработчиком fault, а не через HHDM.

### 2.3 VM: `munmap` разделяемых страниц = cross-process use-after-free
- **Статус:** ✅ ИСПРАВЛЕНО (стадия 4)
- **Файл и строка / Функция:** `kernel/mm/pmap.c:339-358` → `pmap_unmap_user_range()`; `kernel/bsd/syscall.c:1345-1360` → `sys_munmap()`; `kernel/bsd/syscall.c:1280-1315` → `sys_mmap()` (shm/поверхности)
- **Категория:** Уязвимость
- **Что найдено:** `sys_mmap` раздаёт **общие** физические страницы двум и более процессам: (а) shm-таблица `s_shm_entries[64]` по vnode, (б) окна поверхностей `0xA0000000..0xB0000000` (`s_surface_phys[64][2000]`, 1 МБ static BSS) — доступ к чужой поверхности определяется **только адресом**. `sys_munmap` вызывает `pmap_unmap_user_range`, который для **каждого** присутствующего PTE делает `pmm_release_page(phys)` — без refcount/vm_object. Один процесс сделал `munmap` → страницы возвращены в buddy → buddy выдал их третьему процессу → первый и второй продолжают писать в чужую память. Классическая межпроцессная информация-утечка + порча.
- **Как должно быть:** Учёт отображений: `vm_object` с `ref_count` на страницу (XNU: `vm_page->q_state`/`vm_page_hold`), `munmap` уменьшает refcount; физическая страница освобождается только при refcount==0 и после TLB-shootdown **с ожиданием ACK**.
- **План рефакторинга:** 1) Ввести `pmap_unmap_user_range(..., bool release_phys=false)` и для shm/поверхностей вызывать с `false`; 2) перенос таблиц shm/surface из static-массивов в vm_object с refcount; 3) `sys_munmap` обязан искать диапазон в `vm_map`-е, а не слепо чистить PTE (сейчас munmap убивает PTE областей, о которых vm_map не знает — рассинхрон).

### 2.4 Архитектура: EFER.NXE не включён — W^X через mprotect сломан на уровне CPU
- **Статус:** ✅ ИСПРАВЛЕНО (стадия 1)
- **Файл и строка / Функция:** `kernel/kernel_stubs.c:15-28` → `cpu_init_syscall()`; `kernel/arch/x86_64/smp.c:52-70` → `smp_setup_syscall()`; `kernel/mm/pmap.c:410-437` → `pmap_protect_user_range()`
- **Категория:** Уязвимость / Архитектурный дефект
- **Что найдено:** В EFER включается только SCE (`efer | 1`). Бит **NXE (11)** не ставится нигде (grep по `NXE` — пусто). При этом `pmap_protect_user_range` выставляет `PAGE_NX (1ULL<<63)` для не-EXEC регионов. При NXE=0 установка бита 63 в PTE — **reserved-bit violation**: любое обращение к такой странице → #PF c RSVD-флагом → процесс убит. Т.е. `mprotect(p, len, PROT_READ)` фактически убивает процесс. Одновременно Mach-O-загрузчик (`bsd/mach_loader.c:133-134,197-198`) мапит сегменты **без NX вообще**: `__DATA` и 8-МБ пользовательский стек исполняемые → **W^X отсутствует полностью** (SMEP/SMAP включены, но это защита ядра от user-страниц, а не страниц от исполнения).
- **Как должно быть:** `EFER |= NXE` в `cpu_init_syscall` и на каждом AP; загрузчик мапит `__TEXT` = R+X (NX на data), `__DATA` = R/W+NX, стек = RW+NX, затем `pmap_protect` доводит до `max_prot`. KPTI (`PCID`+`INVPCID`, двойные CR3) — отдельно.
- **План рефакторинга:**
  ```c
  wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE | EFER_NXE);      // и в smp_setup_syscall
  // mach_loader.c:
  u32 flags = PAGE_USER | PAGE_NX;
  if (seg->initprot & VM_PROT_WRITE) flags |= PAGE_WRITE;
  if (seg->initprot & VM_PROT_EXECUTE) flags &= ~PAGE_NX;
  ```

### 2.5 Syscall entry: sysretq без проверки каноничности RCX → ring-0 #GP
- **Статус:** ✅ ИСПРАВЛЕНО (стадия 1)
- **Файл и строка / Функция:** `kernel/arch/x86_64/syscall_entry.S:50-64` → `_x86_64_syscall_entry`
- **Категория:** Уязвимость
- **Что найдено:** Класс CVE-2006-0744 / Xen XSA-206: если user вернёт в RCX неканоничное значение (а RCX — точка возврата `sysretq`), на Intel CPU `sysretq` возбудит **#GP уже в ring 0** с невалидным контекстом — у вас это уходит в `interrupt_handler` → `chimera_panic`. Пользовательский `syscall` с подложенным RCX и номером "безвредного" вызова = **kernel panic по желанию**. Также не проверяется RSP при возврате, нет stack-canary на kernel-стеке, нет проверки `rsp` ядра при входе.
- **Как должно быть (микрокод фиксации):**
  ```asm
  ; перед sysretq:
  mov  r10, QWORD PTR [rsp + USER_RSP_SLOT]   ; user rsp
  mov  rax, rcx
  ; проверка каноничности RCX и RSP: (bits 63:47 == 0 или все 1)
  rol  rax, 47
  test rax, rax
  js   user_ret_bad
  ```
  и при bad — принудительный iretq-возврат в `SIGSEGV`-обработку, не sysret. Плюс: сохранять/восстанавливать `KGS_BASE` вместо `swapgs`-пары без валидации.
- **Дополнительно:** `MSR_STAR = 0x0010000800000000` завязан на жёсткие селекторы 0x08/0x10/0x18/0x20 (`gdt.c`) — магия без единого источника правды; фрагмент `interrupt.S` использует `24(%rsp)` до/после push-ов — работает только пока дисциплина push не изменится (хрупкая пара asm/ABI без assert'ов).

### 2.6 SMP — фасад: AP-ядра никогда не запускаются
- **Статус:** ⬜ ОТКРЫТО
- **Файл и строка / Функция:** `kernel/arch/x86_64/smp.c:104-127` → `smp_init()`; `smp.c:72` → `smp_ap_entry()` (dead code)
- **Категория:** Заглушка / Фальшивая функциональность
- **Что найдено:** `smp_init()` не принимает и не читает `smp_request` (он читается в `chimera_kernel_main.c:333` только ради `kprintf`). **Нигде в коде нет отправки INIT-SIPI** (grep `SIPI` — пусто), нет `smp_ap_entry`-вызова, `s_total_cpus` навсегда 1, `lapic_timer_init()` объявлен и не вызывается ни разу. README заявляет "SMP Scheduler ... priority decay scheduling, per-CPU data structures" — по факту система **строго однопроцессорная**, вся per-CPU инфраструктура (IPI 0xEE/0xEF, `g_cpu_data`, AP GDT/TSS) — неисполняемый код. TLB-shootdown `smp_tlb_shootdown()` рассылает IPI "в никуда" (`g_active_cpus > 1` — всегда false — молча no-op).
- **Как должно быть:** Классический протокол: `lapic_send_init` → wait 10ms → `SIPI vector 0x08` → повтор SIPI → AP-точка входа в low-memory trampoline (real mode → long mode) → `smp_ap_entry` с per-CPU GS, TSS, LAPIC-timer как per-CPU tick.
- **План рефакторинга:** Реализовать trampoline (identity-page < 1MB), SIPI-последовательность, per-CPU LAPIC timer (TSC-deadline при наличии), и только затем включать shootdown с ACK-барьером (см. 2.7).

### 2.7 TLB shootdown без подтверждений → гонки с освобождением страниц
- **Статус:** ⬜ ОТКРЫТО
- **Файл и строка / Функция:** `kernel/arch/x86_64/smp.c:153-184` → `smp_tlb_flush_range/shootdown()`; `kernel/mm/pmap.c:268,334,405`
- **Категория:** Архитектурный дефект (на реальном SMP — порча)
- **Что найдено:** IPI 0xEF — fire-and-forget: отправитель не ждёт ACK. `pmap_destroy_user_space()`/`pmap_clone_user_space()` делают shootdown и **сразу** освобождают физические страницы под глобальным `s_pmap_lock`. Другое ядро, ещё держащее stale TLB, может писать в уже освобождённую (и переиспользованную) страницу. Правильные ядра ждут подтверждения от каждого целевого CPU (xnu: `pmap_flush_tlbs` с барьером), с таймаутом и диагностикой.
- **План рефакторинга:** per-CPU `tlb_gen`-счётчик + ожидание `while (cpu_gen[i] < my_gen) pause();` с NMI-диагностикой по таймауту; перевод pmap-локов с одного глобального спинлока на per-map lock (сейчас **вся VM системы под одним `s_pmap_lock`**, включая COW-fault handler с `pmm_alloc_page()` и 4-КБ `memcpy` внутри лока).

### 2.8 Mach IPC: право не проверяется, имена предсказуемы, арена паникует
- **Статус:** ✅ ИСПРАВЛЕНО (стадия 2: арена/фрист; стадия 11: required_right + генерационные имена; динамический рост таблиц — открыто, не критично)
- **Файл и строка / Функция:** `kernel/mach/ipc_port.c:245-272` → `ipc_port_lookup()`; `ipc_port.c:17-25` → `port_arena_alloc()`; `ipc_port.c:28-43` → `space_alloc_name()`; `kernel/bsd/syscall.c:1542-1580` → `sys_mach_lookup_service()`
- **Категория:** Уязвимость + Заглушка
- **Что найдено (4 дефекта):**
  1. `ipc_port_lookup()` принимает `required_right` и **игнорирует его** (`(void)required_right`). Любое имя с SEND-правом позволяет **получать (receive)** из чужого порта: `sys_mach_msg RCV` не отличает RECEIVE от SEND. Изоляция Mach IPC формально отсутствует.
  2. `space_alloc_name()` выдаёт **последовательные целые с 1** → имена портов угадываются перебором (в XNU имена — `(gen << 8) | idx`, стробоскопическое перемешивание).
  3. `sys_mach_lookup_service` делает `&space->is_table[name]` **без проверки** `name < is_table_size` (сравните с `mach_port_deallocate_kernel`, где проверка есть) — потенциальный OOB-write в ipc_entry.
  4. Арена портов: 4096 портов, `chimera_panic` при исчерпании, **порты никогда не возвращаются в арену** (`s_port_arena_next` только растёт; `ipc_port_release` лишь портит сигнатуру). Цикл `mach_port_allocate` = user-triggerable panic; медленная утечка — та же паника через сутки работы.
- **Как должно быть:** Проверка `ie_bits & required_right` перед возвратом; рандомизированные имена с generation-битами (XNU `ie_gen`); арена на `zalloc`/`zfree` с freelist; `is_table` динамический с ростом (в коде честно стоит `// todo Phase 2: grow table via kalloc` — `ipc_port.c:39`).
- **План рефакторинга:** 1) вернуть `entry->ie_bits & required_right ? port : NULL`; 2) `name = ((u32)random() & ~0xFF) | idx` с отдельным счётчиком gen на entry; 3) арену на zone + freelist; 4) добавить проверку границ в `sys_mach_lookup_service` немедленно (однострочный фикс).

### 2.9 Сигналы: пользовательские обработчики никогда не исполняются
- **Статус:** ✅ ИСПРАВЛЕНО (стадия 8; sigaltstack и stop/cont — открыто)
- **Файл и строка / Функция:** `kernel/bsd/proc.c:372-406` → `proc_deliver_signals()`; `kernel/bsd/syscall.c:3844-3865` → `sys_sigaltstack()`
- **Категория:** Заглушка / Фальшивая функциональность
- **Что найдено:** Если у сигнала установлен handler (`p_sigacts[sig] != 0 && != 1`), `proc_deliver_signals` **снимает сигнал с pending и просто делает `return;`** — frame не модифицируется, trampoline не строится, handler не вызывается. Т.е. `sigaction()` в этой ОС — декорация: SIGWINCH/SIGCHLD/SIGUSR молча исчезают. SIGSTOP (17) при дефолтном действии **убивает** процесс (нет семантики stop/cont: 17/18/19/21/22 не обрабатываются). `sys_sigaltstack` **принимает и выбрасывает** стэк (`return 0` без сохранения) — альтернативный стек невозможен (переполнение стека → рекурсивный fault). `sigprocmask` хранит маску в `chimera_proc_t` — per-process, а POSIX требует per-thread.
- **Как должно быть:** Построение signal frame на user stack (или altstack): `sigreturn`-trampoline, сохранение user context (GPR+FP), `EIP→handler`, `sigreturn` syscall для восстановления; stop/cont через `THREAD_STATE_STOPPED` и `wait4`-статусы.
- **План рефакторинга:** Минимум для честности — 1) дефолт для stop-сигналов: замораживать поток; 2) удалять/помечать недоступный `SYS_sigaction`-путь либо документировать; 3) реализовать классический механизм frame-build в `proc_deliver_signals(frame_ptr)` (frame уже есть на стеке ядра — туда дописывается ucontext + возвращаемый адрес на `sigreturn`).

### 2.10 execve/mmap: OOM → memset(CR3=0), перезапись отображений, прот игнорируется
- **Статус:** 🔶 ЧАСТИЧНО (OOM + утечки — стадия 1/4; прот/флаги mmap — открыто)
- **Файл и строка / Функция:** `kernel/bsd/syscall.c:1121-1136` → `sys_execve()`; `syscall.c:1168-1343` → `sys_mmap()`
- **Категория:** Уязвимость + Наивный алгоритм
- **Что найдено:**
  1. `sys_execve`: `u64 new_pml4 = pmm_alloc_page();` — **проверка на 0/-1 отсутствует**; при OOM `__builtin_memset(get_table_ptr_exec(new_pml4), 0, 4096)` зануляет физическую страницу 0 (регион IVT/BIOS) и затем `mov cr3, 0` → мгновенный triple fault всей машины. Ошибки ООМ превращены в спойлер памяти.
  2. `sys_mmap`: `prot` игнорируется — `pg_flags = PAGE_USER | PAGE_WRITE` всегда (строка `if (prot & 4) pg_flags |= 0;` — буквально no-op): `mmap(PROT_READ)` даёт RW, все страницы исполняемые (NX не ставится — см. 2.4). `flags` (MAP_SHARED/PRIVATE/FIXED) и `offset` — `(void)`. Фиксированный адрес **не проверяется на перекрытие**: `pmap_map_user_page` перезаписывает существующие PTE, а старые страницы **утекают** (refcount не освобождается — `pmap.c:124` перезаписывает PTE без release).
  3. `vaddr+size` не проверяется на 64-битное переполнение и на `0x0000800000000000` — mmap «за» пользовательское пространство ловится только в `pmap_map_user_page` по-странично, а `ta_mmap_next` уже испорчен.
  4. Статические таблицы `s_shm_entries[64]` и `s_surface_phys[64][2000]` (1 МБ BSS) — общесистемные, без владельцев/прав (см. 2.3), размер поверхностей зашит (2000 страниц = 8 МБ, `0x800000` шаг).
- **Как должно быть:** Проверять каждый alloc; mmap обязан идти через `vm_map_enter` (он у вас есть и проверяет перекрытия только в auto-режиме — добавить проверки и в fixed-путь) с проверкой `prot`→PTE и перекрытий; лимит на `len` (реальный `RLIMIT_AS`).
- **План рефакторинга:** 1) `if (!new_pml4 || new_pml4==-1) return -ENOMEM;` — однострочный фикс, приоритет 1; 2) `sys_mmap` → `vm_map_enter()` + отдельный paths для fb/shm; 3) в `pmap_map_user_page_unlocked` перед перезаписью PTE освобождать старую страницу (`pmm_release_page(pte & PHYS_MASK)`).

### 2.11 FAT32/ATA: LBA28-only, O(FAT) аллокация, O_TRUNC не освобождает цепочку
- **Статус:** 🔶 ЧАСТИЧНО (LBA48, таймауты, честный truncate, локи каталогов, free-cluster bitmap — стадии 3/11/14; кластеры>4К — открыто)
- **Файл и строка / Функция:** `kernel/drivers/ata.c:172-212` → `ata_read/write_single_sector_lba28()`; `kernel/vfs/fat32.c:101-131` → `fat32_alloc_cluster()`; `kernel/bsd/syscall.c:556-568` → O_TRUNC-ветка `sys_open()`; `kernel/bsd/syscall.c:2894-2940` → `sys_truncate/ftruncate()`
- **Категория:** Наивный алгоритм / Порча данных
- **Что найдено:**
  1. **ATA:** LBA48 детектируется (`ident[83] & (1<<10)`), но I/O всегда 28-бит LBA (`0xE0 | ((lba>>24)&0x0F)`, `(u32)(lba+i)`). На диске >128 ГиБ `lba+i` усекается → **запись в чужие сектора** = разрушение ФС. Граница проверяется по 64-битному `sector_count`, т.е. ограничение не срабатывает.
  2. **Тайминги:** `while (timeout--) inb(status)` — 1 000 000 итераций порта вместо времени; на быстром CPU таймаут слишком короткий, на медленном — лишние минуты ожидания. Нет чтения Error-регистра, нет сброса устройства при ошибке, нет retry.
  3. **FAT32 `fat32_alloc_cluster`:** поиск свободного кластера — **полный последовательный проход всего FAT** сектор за сектором через PIO (`ata_read_sectors` по 512 байт). На 32 ГиБ томе (FAT ~16 МБ = 32 000 секторов) каждое выделение кластера = десятки тысяч синхронных PIO-чтений, **с IRQ-off глобальным локом** → секунды-минуты stall на каждую запись нового файла. Нет free-bitmap, нет хинта последней позиции.
  4. **O_TRUNC (`sys_open`) и `ftruncate`:** `nd->file_size = 0; vp->v_attr.va_size = 0;` — цепочка кластеров **не освобождается** и on-disk dir entry не обновляется: кластеры утекают навсегда, а `sys_truncate/ftruncate` вообще меняют только `v_attr.va_size` **в памяти** — диск не трогается вовсе («фальшивый truncate»: после перезапуска файл прежнего размера, а `read` после `ftruncate(fd,0)` вернёт EOF при живых данных — рассинхрон метаданных).
  5. **Кластеры > 4 КиБ не поддерживаются:** буферы `cluster_buf[4096]` в `fat32_read_file/write_node/create_file`, а циклы идут по `cluster_size_bytes` — при `sectors_per_cluster > 8` записи каталога/данные за 4 КиБ кластера молча теряются. `fat32_init` допускает `sectors_per_cluster` до 64.
  6. **Отсутствие блокировки на мутациях каталогов:** `fat32_create_file/create_dir/unlink_file` не берут `s_fat_lock` (берут только `fat32_write_node`/`get_next_cluster`) → два параллельных create захватят один и тот же 0xE5-слот → cross-link FAT.
- **Как должно быть:** LBA48 (`READ/WRITE SECTORS EXT`, 48-бит LBA из 6 регистров); время-основанные таймауты (`udelay`/TSC); free-cluster bitmap + running hint; buffer cache (у вас в вендорном XNU лежит готовый `vfs_bio.c` — взять за образец); truncate = free chain + обновление dir entry в одной транзакции (минимум — правильный порядок записи); блокировка каталога при create/unlink.
- **План рефакторинга:** см. «Топ задач» №3.

### 2.12 Сеть: TCP без ретрансмиссии/принятия соединений; ARP выдумывает MAC; RX только по syscall
- **Статус:** 🔶 ЧАСТИЧНО (стадия 9: LISTEN/accept, backlog, demux, seq-валидация, ARP-отказ; ретрансмиссия/RTO, случайный ISS, TIME_WAIT, IRQ-driven RX — открыто)
- **Файл и строка / Функция:** `kernel/net/tcp.c:155-200` → `tcp_connect()`; `tcp.c:243-330` → `tcp_input()`; `kernel/net/arp.c:150-183` → `arp_resolve()`; `kernel/drivers/net/e1000.c:155-189` → `e1000_transmit_frame()`; `kernel/bsd/syscall.c:2343-2360` → `sys_accept()`
- **Категория:** Наивный алгоритм / Заглушка
- **Что найдено:**
  1. `tcp_input` в `TCPS_ESTABLISHED` **не проверяет номер последовательности**: `pcb->rcv_nxt += m_len` на любом пакете (дубль/ревордер/реordering ломает поток байт молча). Нет очереди out-of-order, нет дедупликации ACK, нет TIME_WAIT/LAST_ACK/CLOSING — `tcp_close` сразу детачит PCB. Ретрансмиссия есть только для SYN внутри **busy-loop** `tcp_connect`: 1000 итераций `e1000_poll_rx()` + `for (volatile int d=0; d<20000; d++) cpu_relax();` — **активное сжигание CPU в ядре на каждый connect**. ISS = `0x12345678 += 64000` — предсказуем (TCP-hijack тривиален). `kprintf` на каждый пакет — лог-флуд в ring 0 даже без VERBOSE.
  2. `sys_accept` — честная заглушка: `return -1; // enotsup`; `solisten` существует, но LISTEN/SYN_RECEIVED состояний нет — **TCP-сервер невозможен в принципе**, при этом README обещает «TCP state machine».
  3. `arp_resolve` при таймауте **фабрикует MAC `52:55:0A:00:02:02`, вставляет его в ARP-кэш и возвращает SUCCESS** — кадры уходят в никуда, приложение считает, что отправлено. 64 записи, вытеснение «первой» (может выкинуть шлюз), поле `timestamp` не используется — записи не устаревают никогда.
  4. `e1000`: **прерывания замаскированы полностью** (`REG_IMC=0xFFFFFFFF`) — приём только через `e1000_poll_rx()`, который дёргается из 5 разных syscall-путей (arp/dhcp/icmp/tcp/socket). Пока никто не делает syscall — сеть мертва (SYN-flood, keepalive, ICMP-эхо не обрабатываются). `e1000_transmit_frame`: таймаут 50 000 итераций, после которого **возвращается SUCCESS даже если DD не установлен** — молчаливая потеря кадров; при `len<60` паддинг ок, но TX-буфер 2048 при стековом `packet_buf[1536]` — кадры >1536 байт обрезаются молча.
  5. IP/UDP: `ip_sum` и `uh_sum` на приёме **не проверяются вообще** (TCP — проверяется); фрагментация/реassembly отсутствуют; `ip_off` всегда DF; MTU >1472 для UDP — EINVAL.
- **Как должно быть:** IRQ-driven RX (IMS + ISR → netisr/softirq-очередь → kthread), NAPI-подобный poll; RTO по RFC 6298, snd-буфер с ретрансмиссией, seq-валидация (RFC 793 §3.3/segment acceptability), случайный ISS (RFC 6528), full LISTEN/backlog/accept queue; ARP с таймаутами и очередью ожидающих пакетов; verify всех чексумм.
- **План рефакторинга:** см. «Топ задач» №5.

### 2.13 Driver framework: всё I/O — polling в контексте прерываний
- **Статус:** 🔶 ЧАСТИЧНО (e1000 на MSI-прерываниях — стадия 10; sleep вместо busy-poll — стадия 5/7; hid_poll в ISR, LAPIC timer, IOAPIC — открыто)
- **Файл и строка / Функция:** `kernel/arch/x86_64/idt.c:169-177` → ветка IRQ0 `interrupt_handler()`; `kernel/chimerakit/xhci.cpp` (весь файл: `send_command`, `send_control_transfer_*`); `kernel/chimerakit/hid.cpp:46-52` → `ps2_wait()`
- **Категория:** Архитектурный дефект
- **Что найдено:** Из таймерного прерывания (PIT IRQ0, 100 Гц) напрямую вызывается `chimerakit_hid_poll()` → **обработка event-ring xHCI и PS/2-портов в ISR-контексте**. Пары `spinlock_unlock_irqrestore` / `delay_ms(1)` / `spinlock_lock_irqsave` в xHCI — синхронное ожидание железа с активными паузами через **PIT channel 2** (`delay_us` перестраивает канал 2 на каждый вызов — конфликтует с `tsc_init` и любым будущим пользователем канала). `ps2_wait` — 100 000 итераций порта в ISR. Итого: драйверов с настоящими прерываниями **ноль** (ATA IRQ выключен записью 0x02 в control, e1000 замаскирован, xHCI IMAN не используется, PS/2 без IRQ1/12-обработчиков — вектор 44/33 дергает `chimerakit_hid_irq_handler`, но он дублирует poll).
- **Как должно быть:** Драйвер регистрирует ISR в IA (в вашей терминологии ChimeraKit), IRQ-driven обмен, bottom-half/softirq для отложенной работы, таймеры ядра (`callout`), а не poll из таймера.
- **План рефакторинга:** 1) Вынести `chimerakit_hid_poll()` из ISR в kthread/input-daemon, разбужаемый из ISR; 2) включить прерывания xHCI (IMAN IE + MSI/IOAPIC) и e1000; 3) заменить `delay_us` на таймер ядра/TSC-deadline; 4) `ata.c` — IRQ + multi-sector, затем DMA (PRDT).

### 2.14 `kalloc/kfree`: освобождение по эвристике заголовка
- **Статус:** 🔶 ЧАСТИЧНО (sanity+лог — стадия 6; per-page zone metadata — открыто) — чтение -16 байт и потеря памяти
- **Файл и строка / Функция:** `kernel/mm/zone.c:213-235` → `kfree()`; `zone.c:94-129` → `zalloc()`
- **Категория:** Уязвимость / Наивный алгоритм
- **Что найдено:** `kfree(ptr)` читает `zone_header_t` **перед** указателем и проверяет `magic`; при несовпадении читает `large_header_t` на -24 байта; при повторном несовпадении — **тихо выходит, не освободив память** (утечка), и без паники для double-free/foreign-free. Хуже: `free()` уже освобождённого куска занулил magic → `kfree` читает **16/24 байта перед буфером** (OOB-read), и если там случайно оказывается `LARGE_MAGIC` — `pmm_free_contiguous(мусорный физический адрес, мусорный count)` → **разрушение buddy-фриста и произвольной физической памяти**. `zfree` не возвращает страницы зон системе (`z_cur_size` только растёт), нет poison-pattern, нет per-page zone back-pointer (XNU использует `zone_element`/`zone_page_metadata`).
- **Как должно быть:** Zone по page-metadata: каждая страница зоны знает свою зону (lookup по PHalamat PFN → `zone_page_metadata`), free чужого/двойного → panic в debug, журнал в release; poison 0xdeadbeef на free; учёт `z_wasted`.
- **План рефакторинга:** 1) Немедленно: при неопознанном free — `chimera_panic` (лучше падать, чем портить PMM); 2) среднесрочно: ведение реестра «страница→зона/размер» в `vm_page_t` (поля уже есть).

### 2.15 Boot: enum типов memmap не совпадает с Limine
- **Статус:** ⬜ ОТКРЫТО; desc_size не используется
- **Файл и строка / Функция:** `kernel/include/kernel/chimera_types.h:81-96`; `kernel/chimera_kernel_main.c:322-327,338-343` → Limine-ветка; `kernel/mm/pmm.c:97-118`
- **Категория:** Архитектурный дефект
- **Что найдено:** Limine (`LIMINE_MEMMAP_RESERVED = 2`) читается через `chimera_memmap_entry_t`, у которого `CHIMERA_MEM_ACPI_RECLAIM = 2`, `CHIMERA_MEM_RESERVED = 0`. Т.е. **зарезервированные BIOS/MMIO регионы классифицируются как ACPI_RECLAIM**, а ACPI_RECLAIMABLE (Limine 3) как NVS. Сейчас это не рвёт память (free только по `USABLE==1`, совпадает), но `max_phys_addr` и `s_total_ram_pages` считаются неверно, а малейший рефакторинг фриз-логики превратит несовпадение в аллокацию из reserved-памяти. `memmap_desc_size` **нигде не заполняется и не используется** (PMM жёстко предполагает stride == `sizeof(entry)` == 24 байта — совпадает с Limine случайно). `kernel_phys_end = kernel_phys_start + 0x100000` — «ядро равно 1 МБ» магическим числом.
- **Как должно быть:** Один источник правды: транслировать Limine-типы в chimera-типы явно (switch) в `chimera_kernel_main`, передавать `desc_size`, хранить обработанную копию memmap (Limine-структуры могут быть переиспользованы bootloader'ом), вычислять `kernel_phys_end` из `kernel_file->size`.
- **План рефакторинга:** Небольшой `boot_fixups()` сразу после чтения responses: копия memmap в безопасную память + нормализация типов + фиксация размера ядра.

### 2.16 Паника: «KERNEL PANIK AHTUNG PENGUIN HERE»
- **Статус:** ✅ ИСПРАВЛЕНО (стадия 2; автоперезагрузка/IPI-stop — открыто) — без дампов, без остановки других CPU
- **Файл и строка / Функция:** `kernel/kernel_stubs.c:36-49` → `chimera_panic()`
- **Категория:** Заглушка / Архитектурный дефект
- **Что найдено:** Паника печатает шуточную строку, дампит только текст, крутит `hlt` на текущем CPU. Не останавливаются другие ядра (IPI stop), нет дампа регистров/стека/backtrace, нет CR2/CR3/IDT/GDT, нет автоперезагрузки (kctrl+alt+del / ACPI reset), нет сериализации в NVRAM/serial-журнал. В production-ядре паника — самый важный диагностический путь.
- **Как должно быть:** `panic()` → cli; IPI STOP другим CPU; дамп регистров из interrupt frame; backtrace по RBP; опционально kmsg-буфер в PMEM; ACPI reset FADT или keyboard-controller reset.
- **План рефакторинга:** ~100 строк: собрать регистры в `interrupt.S` (уже есть frame), добавить `smp_panic_others()`, backtrace-walker по `.eh_frame`/RBP-цепочке.

### 2.17 FPU: fxsave без XSAVE — тихая порча AVX-состояния
- **Статус:** ✅ ИСПРАВЛЕНО (стадия 12; lazy-FPU и AVX-512-оптимизация — не требуются для корректности)
- **Файл и строка / Функция:** `kernel/arch/x86_64/switch.S:17-32` → `context_switch()`; `kernel/arch/x86_64/scheduler.c:46-54` → `thread_init_stack()`
- **Категория:** Уязвимость (data corruption на реальном железе)
- **Что найдено:** Контекст FPU — `fxsave64/fxrstor64` в `th_fp_state[512]`. XSAVE-области (AVX YMM, MPX, AVX-512) **не сохраняются**: любой userspace-код, скомпилированный с AVX (а clang на x86_64-apple-darwin по умолчанию использует SSE — но любой `-mavx`/auto-vectorizer), получает **испорченные верхние половины YMM-регистров после каждого переключения**. Плюс FPU сохраняется на каждом свитче (нет lazy/TS-схемы), `xcr0` нигде не настраивается.
- **Как должно быть:** `CPUID(XSAVE)` → `XCR0 = X87|SSE|AVX…`, `xsave/xrstor` по `xcr0`-маске в область `xsave_area` (576+ байт, 64-байтное выравнивание — у вас есть), lazy-FPU через CR0.TS опционально.
- **План рефакторинга:** Детект через CPUID leaf 1/13, расширить `th_fp_state` до `xsave_size`, заменить пару инструкций на `xsave64 [mem], mask`/`xrstor64`.

### 2.18 «launchd-chimera (PID 1)» — фальшивый init
- **Статус:** ✅ ИСПРАВЛЕНО (стадия 15; сервисные plist'ы/launchd-агенды — вне аудита)
- **Файл и строка / Функция:** `kernel/bsd/launchd.c:1-20` → `launchd_chimera_start()`
- **Категория:** Фальшивая функциональность
- **Что найдено:** Весь «launchd» — это `proc_create(proc_kernel, "launchd", ...)` + печать. PID 1 **не имеет исполняемого образа, не выполняется в user mode, не репает сирот через wait (сироты перепривязываются на proc_launchd, но никто их не wait-ит — вечные зомби-слоты в пуле 64), не рестартует сервисы**. «init»-семантики ноль; при исчерпании пула процессов система просто перестаёт создавать процессы.
- **Как должно быть:** Реальный PID-1 userland binary (у вас есть `/bin`), kernel reaper-поток, автозапуск сервисов через mach bootstrap-порт (он формально существует).
- **План рефакторинга:** Загружать `launchd` через `spawn_user_process()` (код уже есть в `chimera_kernel_main.c`), а kernel-объект оставить как заглушку для reaper-потока.

### 2.19 Файловая «система» VFS: плоский хэш путей + lookup, видящий не всё
- **Статус:** 🔶 ЧАСТИЧНО (tombstones — стадия 6; компонентный lookup/vnode-cache — открыто)
- **Файл и строка / Функция:** `kernel/vfs/vfs.c:12-27,495-543` → `vfs_lookup()`; `vfs.c:299-367` → `vfs_register()`
- **Категория:** Наивный алгоритм / Архитектурный дефект
- **Что найдено:** «VFS» — статический массив `s_registry[16384]` структур по ~264 байта (≈4.2 МБ BSS) с линейным пробингом **строковых путей**. Нет vnode-кэша с refcount-lifecycle (vnodes — статические массивы 2048/4096 без рециклинга), нет mount-точек и per-fs dispatch (всё через v_op на vnode — то есть нет собственно «VFS-слоя»), нет прав в lookup, симлинки — четыре захардкоженных строковых алиаса. **Дефект согласованности:** `vfs_register` пробирует все 16384 слота, а `vfs_lookup` — только 128 подряд идущих от хэша; при заполнении таблицы >~1/3 (или плохом кластере хэша) **зарегистрированные файлы перестают находиться** — «phantom ENOENT» на больших дисках.
- **Как должно быть:** Namei-проход по компонентам пути с per-directory lookup (как это делает любая ФС), dcache/vnode cache с refcount и reclaim, mount tree.
- **План рефакторинга:** 1) Немедленно: выровнять пределы пробинга (128→16384) или заменить на нормальную хэш-таблицу с цепочками; 2) следующий этап: компонентный `vfs_lookup` через `v_children`-иерархию (она уже ведётся!) вместо строкового реестра.

### 2.20 Userspace/libsystem: first-fit malloc без блокировок
- **Статус:** ⬜ ОТКРЫТО; DMA/сеть-бюджеты
- **Файл и строка / Функция:** `usr/libsystem/libsystem_chimera.c:1107-1160` → `malloc()`
- **Категория:** Наивный алгоритм
- **Что найдено:** Односвязный список чанков, first-fit, O(n) на каждый malloc, `mmap(NULL, min 64KiB)` на рост, coalescing только соседей в списке, память не возвращается ОС, **нет thread-safety вообще** (pthread в системе есть — гонки гарантированы в многопоточном приложении), нет пер-поток кэшей/tcache.
- **Как должно быть:** Magazines/per-CPU caches (как `libmalloc`), bins по размерам, mmap для больших, блокировки/атомики.
- **План рефакторинга:** Минимально: мьютекс на malloc/free + segregated free-lists по степеням двойки.

### 2.21 Репозиторий/инфраструктура: 1.1M LOC мёртвого XNU, двойная загрузочная обвязка
- **Статус:** 🔶 ЧАСТИЧНО (.gitignore на артефакты — стадия 13; вынос вендора XNU и единый boot-протокол — открыто)
- **Файлы:** `kernel/osfmk/**`, `kernel/bsd/{kern,net,vfs,nfs,netinet*}/**`, `kernel/iokit/**`, `kernel/libkern/**`, `kernel/pexpert/**` (не в сборке); `limine.cfg/limine.conf` vs `boot/efi/efiloader.c`; `kernel/CMakeLists.txt:91` (`.elf`-суффикс при Mach-O ядре); закоммиченные `*.o`, `mach_kernel`, `bootx64.efi`; `kernel/chimerakit/xhci.cpp:2` («fucking ai cant fix nothing in this file so dont even try»)
- **Категория:** Архитектурный дефект (инфраструктура)
- **Что найдено:** Половина «README-возможностей» (DTrace, pf, NFS, IOKit, kext, zalloc 5k LOC, vm_pageout) — это вендорные файлы Apple XNU, **не линкующиеся** с ядром; это вводит в заблуждение и разрастает репозиторий/поиск. Два загрузочных протокола (кастомный `XIUBOOT!` и Limine-fallback) удваивают поверхность регрессий. Суффикс `.elf` у Mach-O таргета — след старого пайплайна. Git хранит бинарные артефакты сборки.
- **План рефакторинга:** 1) Вынести вендор XNU в отдельный `vendor/`-сабмодуль или удалить; 2) выбрать один boot-протокол (кастомный EFI), Limine-ветку в `#ifdef`/удалить; 3) `.gitignore` на `*.o`, `mach_kernel`, `*.efi`, `build/`; 4) переименовать таргет в `mach_kernel`.

---

## 3. Топ приоритетных задач для переписывания

Отсортировано от фундаментальных проблем ядра к драйверам и утилитам. Формат: *Задача — охват — критерий готовности*.

1. **P0 — Безопасность модели памяти и входа в ядро (одна неделя, спасает от половины аварий):**
   `EFER.NXE` (2.4) · NX/protect в Mach-O loader (2.4) · каноничность RCX/RSP перед `sysret` (2.5) · проверка OOM в `sys_execve`/`mach_load` (2.10) · `pmap_map_user_page` — освобождение старой страницы при перезаписи PTE (2.10) · COW-aware `copyout` (2.2).
2. **P0 — Отказоустойчивость вместо panic:**
   динамический run-queue/proc/task/port/space пулы; `-EAGAIN` вместо `chimera_panic` на исчерпание (2.1, 2.8); panic→`-EFAULT/-ENOMEM` в `kfree`-эвристике (2.14); честный `chimera_panic` с дампами и остановкой CPU (2.16).
3. **P0 — Блочный слой и ФС:**
   LBA48 + время-таймауты в `ata.c` (2.11.1-2) · buffer cache (по образцу `vfs_bio`) · free-cluster bitmap + hint для FAT32 (2.11.3) · честный `truncate` (цепочка + dir entry) в `sys_open(O_TRUNC)`/`ftruncate` (2.11.4) · поддержка `sectors_per_cluster` > 8 (2.11.5) · блокировки каталогов (2.11.6) · выравнивание probe-лимитов `vfs_lookup` (2.19).
4. **P0 — Разделяемая память:**
   refcount vm_object для shm/поверхностей, munmap без освобождения чужих страниц, table-владельцы вместо `s_surface_phys[64][2000]` (2.3, 2.10.4); контроль прав на окна GUI.
5. **P1 — Сеть как подсистема:**
   IRQ-driven e1000 (IMS + ISR → RX-kthread) (2.12.4, 2.13) · обработка ошибок TX (`DD`-таймаут → error) (2.12.4) · seq-валидация, RTO/ретрансмиссия, случайный ISS, TIME_WAIT, LISTEN+accept-очередь в TCP (2.12.1-2) · верификация чексумм IP/UDP (2.12.5) · ARP-таймауты, отказ вместо выдуманного MAC (2.12.3) · убрать `kprintf` из data-path (2.12.1).
6. **P1 — Прерывания и таймеры:**
   IOAPIC/MSI-диспетчеризация вместо legacy PIC+LINT0-ExtINT · LAPIC timer (TSC-deadline) как per-CPU tick · вывод `chimerakit_hid_poll` из ISR-контекста (2.13) · sleep-based `nanosleep/wait4/poll/select` через таймерные очереди (сейчас busy-poll в ядре — `syscall.c:1946-1949, 1865-1898, 3449-3498, 3543-3617`) · `mach_msg` timeout через те же очереди (`ipc_kmsg.c:350` — `(void)timeout_ms`).
7. **P1 — Сигналы и процессы:**
   настоящая доставка (frame+sigreturn), stop/cont, per-thread mask, sigaltstack (2.9) · реальный launchd/PID-1 + reaper-сирот (2.18) · `fchdir`, `readv/writev/poll` без молчаливых усечений (2.5-приложение: `syscall.c:2644-2648, 3429-3433`), `sendto/recvfrom` >1500 через копирование цепочкой (2.12-приложение: `syscall.c:2377-2396`).
8. **P2 — SMP по-настоящему или честно его выключить:**
   trampoline+SIPI+LAPIC timer, per-map pmap-локи, shootdown с ACK (2.6, 2.7); либо убрать SMP-заявления из README и вендорных заголовков.
9. **P2 — VM-инфраструктура:**
   `vm_map` как единственный путь `sys_mmap/munmap/mprotect` (сейчас три параллельных механизма: ta_mmap_next-bump, vm_map-entry-лист, прямые PTE) · проверка перекрытий fixed-mmap, `VM_INHERIT_COPY`, pageout/LRU-контур хотя бы для анонимных страниц (сейчас анонимная память wired навсегда, `vm_object.c:197-200` `vm_object_sync` — stub).
10. **P2 — Гигиена:**
    удалить/вынести 1.1M LOC вендор XNU, один boot-протокол, `.gitignore` артефактов, Mach-O-имя таргета, убрать non-technical комментарии (`xhci.cpp:2`, `kernel_stubs.c:38`), документировать фактические (а не декларируемые) возможности в README.

---

### Приложение A. Сводная таблица заглушек и фальшивой функциональности

| Место | Заявлено (README/имя) | Фактически |
|---|---|---|
| `smp.c:72-101` | SMP-планировщик, priority decay | AP не стартуют; 1 CPU; приоритеты ±16/±1 тик |
| `bsd/launchd.c` | launchd foundation PID 1 | `proc_create("launchd")` + kprintf |
| `bsd/proc.c:372-406` | signal delivery | handler-сигналы поглощаются; SIGSTOP убивает |
| `bsd/syscall.c:2343` | accept() | `return -1` |
| `syscall.c:2894-2940` | truncate/ftruncate | меняет только in-memory `va_size` |
| `ipc_kmsg.c:350` | mach_msg timeout | `(void)timeout_ms` |
| `mm/vm_object.c:197` | vnode-pager, shadow chains, sync | dead code + `return SUCCESS` |
| `kernel/osfmk/**` (1.1M LOC) | «Darwin VM/pageout/DTrace/IOKit» | не собирается (вендор XNU) |
| `drivers/ata.c` | «ATA/IDE block device» | PIO, 28-bit LBA, один сектор за раз, IRQ off |
| `drivers/net/e1000.c` | «Gigabit Ethernet driver» | IRQ замаскированы, polling из syscall'ов |
| `sys_sigaltstack` | POSIX sigaltstack | значение принимается и выбрасывается |

### Приложение B. Что сделано корректно (для объективности)

- Buddy-аллокатор PMM с merge/refcount (`mm/pmm.c`) — рабочий остов; ticket-lock спинлоки с irqsave (`include/kernel/spinlock.h`) — корректные примитивы; `copyin/copyinstr` проверяют границы и USER-биты PTE; `interrupt.S` корректно различает user/kernel GS; IDE-тайминги through registers, а не magic-port-заглушки; fat32 read/write через `copyin/copyout`, а не сырые указатели; per-CPU `gs`-offsets `0x0/0x8/0x10` и `THREAD_KERNEL_STACK_OFFSET 0x30` сверены со структурами — ABI сходится; e1000/xHCI общаются с реальными регистрами/структурами DMA (не эмуляция через память).
