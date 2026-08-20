// neofetch - System information tool for XIU OS
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/syscall.h>

extern int sprintf(char *buf, const char *fmt, ...);
extern long syscall(long number, ...);

// sysinfo structure - must match kernel definition
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

// ascii art logo for XIU OS
static const char *logo[] = {
    "        ___    ___       ",
    "       \\  \\  /  /       ",
    "        \\  \\/  /        ",
    "    __   \\    /   __    ",
    "    \\ \\  /    \\  / /   ",
    "     \\ \\/  /\\  \\/ /    ",
    "      \\__/  \\__\\/      ",
    "                         ",
    NULL
};

static void get_cpu_model(char *brand, size_t maxlen) {
    unsigned int eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000));
    if (eax >= 0x80000004) {
        unsigned int *p = (unsigned int *)brand;
        for (unsigned int i = 0x80000002; i <= 0x80000004; i++) {
            __asm__ volatile("cpuid" : "=a"(p[0]), "=b"(p[1]), "=c"(p[2]), "=d"(p[3]) : "a"(i));
            p += 4;
        }
        brand[48] = '\0';
        // trim leading whitespace
        char *src = brand;
        while (*src == ' ') src++;
        if (src != brand) {
            memmove(brand, src, strlen(src) + 1);
        }
    } else {
        strncpy(brand, "x86_64 Processor", maxlen);
    }
}

static void print_info(void) {
    char buf[512];
    char cpu_brand[64];
    
    // get system info via syscall
    sysinfo_t info;
    memset(&info, 0, sizeof(info));
    long ret = syscall(SYS_sysinfo, (long)&info);
    
    if (ret != 0) {
        write(1, "Error: sysinfo syscall failed\n", 30);
        return;
    }
    
    get_cpu_model(cpu_brand, sizeof(cpu_brand));

    const char *user = getenv("USER");
    if (!user || user[0] == '\0') {
        user = (getuid() == 0) ? "root" : "user";
    }

    const char *shell = getenv("SHELL");
    if (!shell || shell[0] == '\0') {
        shell = "xsh";
    }

    const char *term = getenv("TERM");
    if (!term || term[0] == '\0') {
        term = "fbcon (1280x800)";
    }

    unsigned long total_mb = info.total_memory / (1024 * 1024);
    unsigned long free_mb = info.free_memory / (1024 * 1024);
    unsigned long used_mb = (total_mb >= free_mb) ? (total_mb - free_mb) : 0;
    
    int line = 0;
    
    // line 0: Logo + User@Host
    if (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m  \033[1;32m%s\033[0m@\033[1;32m%s\033[0m\n", 
                logo[line], user, info.hostname);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 1: Logo + Separator
    if (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m  ", logo[line]);
        write(1, buf, strlen(buf));
        int sep_len = strlen(user) + 1 + strlen(info.hostname);
        for (int i = 0; i < sep_len; i++) {
            write(1, "-", 1);
        }
        write(1, "\n", 1);
        line++;
    }
    
    // line 2: Logo + OS
    if (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mOS:\033[0m %s %s\n", 
                logo[line], info.os_name, info.architecture);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 3: Logo + Host
    if (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mHost:\033[0m %s\n", 
                logo[line], info.hostname);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 4: Logo + Kernel
    if (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mKernel:\033[0m %s %s\n", 
                logo[line], info.kernel_name, info.os_version);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 5: Logo + Uptime
    if (logo[line]) {
        unsigned int days = info.uptime_seconds / 86400;
        unsigned int hours = (info.uptime_seconds % 86400) / 3600;
        unsigned int minutes = (info.uptime_seconds % 3600) / 60;
        unsigned int secs = info.uptime_seconds % 60;
        if (days > 0) {
            sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mUptime:\033[0m %ud %uh %um\n", 
                    logo[line], days, hours, minutes);
        } else if (hours > 0) {
            sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mUptime:\033[0m %u hours, %u mins\n", 
                    logo[line], hours, minutes);
        } else if (minutes > 0) {
            sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mUptime:\033[0m %u mins, %u secs\n", 
                    logo[line], minutes, secs);
        } else {
            sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mUptime:\033[0m %u secs\n", 
                    logo[line], secs);
        }
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 6: Logo + Shell
    if (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mShell:\033[0m %s\n", logo[line], shell);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 7: Logo + Terminal
    if (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mTerminal:\033[0m %s\n", logo[line], term);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // print remaining logo lines if any
    while (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m\n", logo[line]);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // CPU details with hardware CPUID string
    sprintf(buf, "                          \033[1;37mCPU:\033[0m %s (%u cores)\n", 
            cpu_brand, info.cpu_count);
    write(1, buf, strlen(buf));
    
    // Memory details with calculation
    unsigned int pct = (total_mb > 0) ? (unsigned int)((used_mb * 100) / total_mb) : 0;
    sprintf(buf, "                          \033[1;37mMemory:\033[0m %lu MiB / %lu MiB (%u%%)\n", 
            used_mb, total_mb, pct);
    write(1, buf, strlen(buf));
    
    // Color palette
    write(1, "\n                          ", 27);
    for (int i = 0; i < 8; i++) {
        sprintf(buf, "\033[4%dm   \033[0m", i);
        write(1, buf, strlen(buf));
    }
    write(1, "\n                          ", 27);
    for (int i = 0; i < 8; i++) {
        sprintf(buf, "\033[10%dm   \033[0m", i);
        write(1, buf, strlen(buf));
    }
    write(1, "\n\n", 2);
}

int main(void) {
    print_info();
    return 0;
}
