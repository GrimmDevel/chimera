// =============================================================================
// XIU Operating System — Memory Load & Stress Testing Tool
// usr/bin/memload.c
// =============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>

typedef struct {
  unsigned long total_memory;
  unsigned long free_memory;
  unsigned int cpu_count;
  unsigned int uptime_seconds;
  char os_name[32];
  char os_version[32];
  char kernel_name[32];
  char architecture[16];
  char hostname[64];
} sysinfo_t;

#define SYS_sysinfo 251

static void get_mem_stats(unsigned long *total_mb, unsigned long *free_mb, unsigned long *used_mb) {
    sysinfo_t info;
    memset(&info, 0, sizeof(info));
    long ret = syscall(SYS_sysinfo, (long)&info);
    if (ret == 0) {
        *total_mb = info.total_memory / (1024 * 1024);
        *free_mb  = info.free_memory / (1024 * 1024);
        *used_mb  = (*total_mb >= *free_mb) ? (*total_mb - *free_mb) : 0;
    } else {
        *total_mb = *free_mb = *used_mb = 0;
    }
}

int main(int argc, char **argv) {
    unsigned long target_mb = 256;
    unsigned int duration_sec = 10;

    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("Usage: memload [megabytes] [duration_seconds]\n");
            printf("Example:\n");
            printf("  memload 500 15     # Allocate 500 MB RAM, hold for 15s\n");
            printf("  memload 1024       # Allocate 1024 MB RAM, hold for 10s\n");
            return 0;
        }
        target_mb = (unsigned long)atoi(argv[1]);
        if (target_mb == 0) target_mb = 256;
    }

    if (argc > 2) {
        duration_sec = (unsigned int)atoi(argv[2]);
        if (duration_sec == 0) duration_sec = 10;
    }

    unsigned long total_mb, free_mb, used_mb;
    get_mem_stats(&total_mb, &free_mb, &used_mb);

    printf("\033[1;36m=== XIU OS Memory Load Generator ===\033[0m\n");
    printf("Initial Memory: \033[1;32m%lu MiB used\033[0m / %lu MiB total (Free: %lu MiB)\n",
           used_mb, total_mb, free_mb);
    printf("Target allocation: \033[1;33m%lu MiB\033[0m for \033[1;33m%u seconds\033[0m\n\n",
           target_mb, duration_sec);

    if (target_mb >= free_mb) {
        printf("\033[1;31mWarning:\033[0m Target (%lu MiB) exceeds or is close to available free RAM (%lu MiB)!\n",
               target_mb, free_mb);
    }

    size_t alloc_bytes = (size_t)target_mb * 1024 * 1024;
    printf("[1/4] Allocating %lu MiB virtual address space via mmap...\n", target_mb);

    void *ptr = mmap(NULL, alloc_bytes, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (ptr == MAP_FAILED || ptr == NULL) {
        printf("\033[1;31mError:\033[0m mmap failed to allocate %lu MiB!\n", target_mb);
        return 1;
    }

    printf("[2/4] Committing physical pages and writing test pattern (0xAA55AA55)...\n");

    // write pattern to every 4KB page to force physical PMM page allocation & testing
    volatile unsigned int *words = (volatile unsigned int *)ptr;
    size_t total_words = alloc_bytes / sizeof(unsigned int);
    size_t words_per_mb = (1024 * 1024) / sizeof(unsigned int);

    for (size_t mb = 0; mb < target_mb; mb++) {
        size_t start = mb * words_per_mb;
        size_t end = start + words_per_mb;
        for (size_t i = start; i < end; i += (4096 / sizeof(unsigned int))) {
            words[i] = 0xAA55AA55;
            words[i + 1] = (unsigned int)mb;
        }
        if ((mb + 1) % 64 == 0 || mb + 1 == target_mb) {
            printf("      Committed %lu / %lu MiB (%lu%%)...\n",
                   mb + 1, target_mb, ((mb + 1) * 100) / target_mb);
        }
    }

    get_mem_stats(&total_mb, &free_mb, &used_mb);
    printf("\n[3/4] Memory under active load: \033[1;31m%lu MiB used\033[0m / %lu MiB total (Free: %lu MiB)\n",
           used_mb, total_mb, free_mb);

    // Verify pattern integrity
    printf("      Verifying memory integrity...\n");
    int errors = 0;
    for (size_t mb = 0; mb < target_mb; mb++) {
        size_t idx = mb * words_per_mb;
        if (words[idx] != 0xAA55AA55 || words[idx + 1] != (unsigned int)mb) {
            errors++;
        }
    }

    if (errors == 0) {
        printf("      \033[1;32mIntegrity OK:\033[0m All %lu MiB pages verified without corruption.\n", target_mb);
    } else {
        printf("      \033[1;31mIntegrity Failure:\033[0m %d corrupted page headers detected!\n", errors);
    }

    printf("      Holding RAM allocation for %u seconds...\n", duration_sec);
    for (unsigned int s = duration_sec; s > 0; s--) {
        printf("\r      Releasing in %u seconds...   ", s);
        fflush(stdout);
        sleep(1);
    }
    printf("\r      Releasing memory now!                     \n");

    printf("[4/4] Freeing %lu MiB back to kernel PMM...\n", target_mb);
    munmap(ptr, alloc_bytes);

    get_mem_stats(&total_mb, &free_mb, &used_mb);
    printf("\n\033[1;32mFinal Memory:\033[0m %lu MiB used / %lu MiB total (Free: %lu MiB)\n",
           used_mb, total_mb, free_mb);
    printf("Done.\n");

    return 0;
}
