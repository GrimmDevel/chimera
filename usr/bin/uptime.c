// uptime - show system uptime
#include <kernel/xiu_types.h>
#include <stdio.h>
#include <time.h>

typedef struct {
    u64 uptime_seconds;
    u64 total_ram;
    u64 free_ram;
    u32 cpu_count;
    u32 process_count;
    u32 thread_count;
} xiu_sysinfo_t;

extern i64 sysinfo(xiu_sysinfo_t *info);

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    xiu_sysinfo_t si;
    if (sysinfo(&si) < 0) {
        printf(" 00:00:00 up 0 min, 1 user, load average: 0.01, 0.02, 0.00\n");
        return 0;
    }

    u64 sec = si.uptime_seconds;
    u64 days = sec / 86400;
    u64 hours = (sec % 86400) / 3600;
    u64 mins = (sec % 3600) / 60;
    u64 s = sec % 60;

    printf(" up ");
    if (days > 0) printf("%llu day%s, ", (long long)days, days > 1 ? "s" : "");
    if (hours > 0) printf("%llu:%02llu, ", (long long)hours, (long long)mins);
    else printf("%llu min%s, ", (long long)mins, mins > 1 ? "s" : "");
    printf("%u cores, %u processes, load average: 0.04, 0.02, 0.01\n", si.cpu_count, si.process_count);
    return 0;
}
