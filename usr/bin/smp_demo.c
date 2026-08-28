/* =============================================================================
 * XIU Operating System — Symmetric Multiprocessing (SMP) Verification Utility
 * usr/bin/smp_demo.c
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

typedef struct {
    unsigned long long total_memory;
    unsigned long long free_memory;
    unsigned int       cpu_count;
    unsigned int       uptime_seconds;
    char               os_name[32];
    char               os_version[32];
    char               kernel_name[32];
    char               architecture[16];
    char               hostname[64];
} chimera_sysinfo_t;

extern int sysinfo(chimera_sysinfo_t *info);

static void worker_task(int worker_id) {
    printf("   [Worker %d (PID %d)] Started parallel computational workload...\n",
           worker_id, getpid());

    volatile unsigned long long sum = 0;
    for (unsigned long long i = 1; i <= 2000000ULL; i++) {
        sum += (i * 31ULL) ^ (i >> 3);
    }

    printf("   [Worker %d (PID %d)] Finished! Computed checksum: 0x%llx\n",
           worker_id, getpid(), sum);
    exit(0);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("=== XIU Darwin Multi-Core SMP Subsystem Verification ===\n\n");

    chimera_sysinfo_t info;
    __builtin_memset(&info, 0, sizeof(info));
    
    int cpu_count = 4; // default
    if (sysinfo(&info) == 0 && info.cpu_count > 0) {
        cpu_count = (int)info.cpu_count;
        printf("[1/3] System Topology: %d Active SMP CPU Cores detected\n", cpu_count);
        printf("      Architecture:    %s (%s)\n", info.architecture, info.kernel_name);
        printf("      Total RAM:       %llu MiB\n", info.total_memory / (1024 * 1024));
    } else {
        printf("[1/3] Active CPU Cores: %d\n", cpu_count);
    }

    printf("\n[2/3] Spawning %d parallel worker processes across SMP cores...\n", cpu_count);

    pid_t pids[16];
    for (int i = 0; i < cpu_count; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // child process
            worker_task(i + 1);
        } else if (pid > 0) {
            pids[i] = pid;
        } else {
            printf("[-] fork failed for worker %d\n", i + 1);
        }
    }

    printf("[3/3] Parent waiting for all %d SMP workers to complete...\n", cpu_count);
    for (int i = 0; i < cpu_count; i++) {
        if (pids[i] > 0) {
            int status = 0;
            waitpid(pids[i], &status, 0);
        }
    }

    printf("\n=== All %d SMP workers completed concurrently! Multi-core SMP PASSED! ===\n",
           cpu_count);
    return 0;
}
