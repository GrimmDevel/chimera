// neofetch - System information tool for XIU OS
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <plist.h>

extern int sprintf(char *buf, const char *fmt, ...);

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

// Native XIU OS ASCII Logo
static const char *logo[] = {
    "        \033[1;36m___    ___\033[0m       ",
    "       \033[1;36m\\  \\  /  /\033[0m       ",
    "        \033[1;34m\\  \\/  /\033[0m        ",
    "    \033[1;34m__   \\    /   __\033[0m    ",
    "    \033[1;35m\\ \\  /    \\  / /\033[0m    ",
    "     \033[1;35m\\ \\/  /\\  \\/ /\033[0m     ",
    "      \033[1;31m\\__/  \\__\\/\033[0m       ",
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
        char *src = brand;
        while (*src == ' ') src++;
        if (src != brand) {
            memmove(brand, src, strlen(src) + 1);
        }
    } else {
        strncpy(brand, "Intel/AMD x86_64 Processor", maxlen);
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
    gethostname(info.hostname, sizeof(info.hostname));

    // Read SystemVersion.plist
    char os_full_name[128];
    plist_t *sys_ver_plist = plist_read_file("/System/Library/CoreServices/SystemVersion.plist");
    if (sys_ver_plist) {
        const char *prod_name = plist_dict_get_string(sys_ver_plist, "ProductName");
        const char *prod_ver = plist_dict_get_string(sys_ver_plist, "ProductVersion");
        const char *prod_build = plist_dict_get_string(sys_ver_plist, "ProductBuildVersion");
        sprintf(os_full_name, "%s %s (%s) %s",
                prod_name ? prod_name : "XIU OS",
                prod_ver ? prod_ver : "1.0.0",
                prod_build ? prod_build : "24A348",
                info.architecture);
        plist_free(sys_ver_plist);
    } else {
        sprintf(os_full_name, "%s %s", info.os_name, info.architecture);
    }

    const char *user = getenv("USER");
    if (!user || user[0] == '\0') {
        user = "fvr";
    }

    const char *shell = getenv("SHELL");
    if (!shell || shell[0] == '\0') {
        shell = "zsh";
    }

    const char *term = getenv("TERM");
    if (!term || term[0] == '\0') {
        term = "xterm-256color";
    }

    unsigned long total_mb = info.total_memory / (1024 * 1024);
    unsigned long free_mb = info.free_memory / (1024 * 1024);
    unsigned long used_mb = (total_mb >= free_mb) ? (total_mb - free_mb) : 0;
    
    int line = 0;
    
    // line 0: Logo + User@Host
    if (logo[line]) {
        sprintf(buf, "%s  \033[1;32m%s\033[0m@\033[1;32m%s\033[0m\n", 
                logo[line], user, info.hostname);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 1: Logo + Separator
    if (logo[line]) {
        sprintf(buf, "%s  ", logo[line]);
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
        sprintf(buf, "%s  \033[1;37mOS:\033[0m %s\n", 
                logo[line], os_full_name);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 3: Logo + Host
    if (logo[line]) {
        sprintf(buf, "%s  \033[1;37mHost:\033[0m %s (x86_64 Machine)\n", 
                logo[line], info.hostname);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 4: Logo + Kernel
    if (logo[line]) {
        sprintf(buf, "%s  \033[1;37mKernel:\033[0m Darwin 24.0.0 (XIU Mach/BSD Hybrid)\n", 
                logo[line]);
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
            sprintf(buf, "%s  \033[1;37mUptime:\033[0m %ud %uh %um\n", 
                    logo[line], days, hours, minutes);
        } else if (hours > 0) {
            sprintf(buf, "%s  \033[1;37mUptime:\033[0m %u hours, %u mins\n", 
                    logo[line], hours, minutes);
        } else if (minutes > 0) {
            sprintf(buf, "%s  \033[1;37mUptime:\033[0m %u mins, %u secs\n", 
                    logo[line], minutes, secs);
        } else {
            sprintf(buf, "%s  \033[1;37mUptime:\033[0m %u secs\n", 
                    logo[line], secs);
        }
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 6: Logo + Shell
    if (logo[line]) {
        sprintf(buf, "%s  \033[1;37mShell:\033[0m %s\n", 
                logo[line], shell);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 7: Logo + Resolution
    if (logo[line]) {
        sprintf(buf, "%s  \033[1;37mResolution:\033[0m 1280x800 @ 32bpp (Framebuffer)\n", 
                logo[line]);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 8: Logo + Terminal
    if (logo[line]) {
        sprintf(buf, "%s  \033[1;37mTerminal:\033[0m %s\n", 
                logo[line], term);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 9: Logo + CPU
    if (logo[line]) {
        sprintf(buf, "%s  \033[1;37mCPU:\033[0m %s (%u SMP Cores)\n", 
                logo[line], cpu_brand, info.cpu_count);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 10: Logo + Memory
    if (logo[line]) {
        sprintf(buf, "%s  \033[1;37mMemory:\033[0m %luMiB / %luMiB (%lu%%)\n", 
                logo[line], used_mb, total_mb, 
                total_mb > 0 ? (used_mb * 100 / total_mb) : 0);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 11: Logo + Blank
    if (logo[line]) {
        sprintf(buf, "%s  \n", logo[line]);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 12: Logo + Color Palette (Standard)
    if (logo[line]) {
        sprintf(buf, "%s  \033[40m   \033[41m   \033[42m   \033[43m   \033[44m   \033[45m   \033[46m   \033[47m   \033[0m\n", 
                logo[line]);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // line 13: Logo + Color Palette (Bright)
    if (logo[line]) {
        sprintf(buf, "%s  \033[100m   \033[101m   \033[102m   \033[103m   \033[104m   \033[105m   \033[106m   \033[107m   \033[0m\n", 
                logo[line]);
        write(1, buf, strlen(buf));
        line++;
    }
    
    // print remaining logo lines if any
    while (logo[line]) {
        sprintf(buf, "%s\n", logo[line]);
        write(1, buf, strlen(buf));
        line++;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print_info();
    return 0;
}
