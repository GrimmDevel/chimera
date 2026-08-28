# Project memory

Durable notes the agent keeps across sessions.

- (2026-08-27) Проект xiu = гибридная ОС Chimera (Mach/BSD, CMake, x86_64). 2026-08-27 проведён аудит безопасности, отчёт в SECURITY_AUDIT.md (5 критических: pmap без проверки vaddr, Mach-O loader без валидации, mach_msg send_sz/msgh_size несоответствие, несмежный стек в execve, TCP mbuf overflow). SMEP/SMAP выключены, нет stack protector/KASLR. Вендорные папки (ravynos, OpenCorePkg, tinycc, dash, fastfetch, zsh, kilo) не аудировались.
- (2026-08-27) (2026-08-27) Том 2 аудита — SECURITY_AUDIT_2.md (4 крит: pipe-гонка OOB, паника через user RSP в #PF, COW без лока, setuid без проверок; 8 высоких: teardown-гонка, TLB-ghost, паника 64 потоков, безлимитный стек, g_syscall_frame SMP, kill без прав, двойной fp_retain, утечка паддинга событий). Том 1 — SECURITY_AUDIT.md. Вендор не аудировался.
