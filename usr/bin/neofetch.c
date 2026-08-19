// neofetch - System information tool for XIU OS
#include <stdio.h>
#include <unistd.h>
#include <string.h>
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

static void print_info(void) {
    char buf[512];
    
    // get system info via syscall
    sysinfo_t info;
    long ret = syscall(SYS_sysinfo, (long)&info);
    
    if (ret != 0) {
        write(1, "Error: sysinfo syscall failed\n", 30);
        return;
    }
    
    unsigned long total_mb = info.total_memory / (1024 * 1024);
    unsigned long free_mb = info.free_memory / (1024 * 1024);
    unsigned long used_mb = total_mb - free_mb;
    
    // print logo and system info side by side
    int line = 0;
    
    // line 0: Logo + User@Host
    if (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m  \033[1;32muser\033[0m@\033[1;32m%s\033[0m\n", 
                logo[line], info.hostname);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 1: Logo + Separator
    if (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m  ", logo[line]);
        write(1, buf, strlen(buf));
        // print separator matching username@hostname length
        int sep_len = 5 + strlen(info.hostname); // "user@" + hostname
        for (int i = 0; i < sep_len; i++) {
            write(1, "-", 1);
        }
        write(1, "\n", 1);
        line++;
    }
    
    // line 2: Logo + OS
    if (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mOS:\033[0m %s\n", 
                logo[line], info.os_name);
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
        unsigned int hours = info.uptime_seconds / 3600;
        unsigned int minutes = (info.uptime_seconds % 3600) / 60;
        if (hours > 0) {
            sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mUptime:\033[0m %u hours, %u mins\n", 
                    logo[line], hours, minutes);
        } else {
            sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mUptime:\033[0m %u mins\n", 
                    logo[line], minutes);
        }
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 6: Logo + Shell
    if (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mShell:\033[0m xsh / dash\n", logo[line]);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 7: Logo + Terminal
    if (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m  \033[1;37mTerminal:\033[0m Framebuffer Console (Direct)\n", logo[line]);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // print remaining logo lines if any
    while (logo[line]) {
        sprintf(buf, "\033[1;36m%s\033[0m\n", logo[line]);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // print system info without logo
    sprintf(buf, "                          \033[1;37mCPU:\033[0m %s (%u cores)\n", 
            info.architecture, info.cpu_count);
    write(1, buf, strlen(buf));
    
    sprintf(buf, "                          \033[1;37mMemory:\033[0m %lu MiB / %lu MiB\n", 
            used_mb, total_mb);
    write(1, buf, strlen(buf));
    
    sprintf(buf, "                          \033[1;37mEnvironment:\033[0m Pure Console\n");
    write(1, buf, strlen(buf));
    
    // print color palette
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
