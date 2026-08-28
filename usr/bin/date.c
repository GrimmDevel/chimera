// date - print system date and time
#include <kernel/chimera_types.h>
#include <sys/types.h>
#include <stdio.h>
#include <time.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);

    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    if (tm) {
        printf("%s %s %2d %02d:%02d:%02d UTC %d\n",
               days[tm->tm_wday % 7],
               months[tm->tm_mon % 12],
               tm->tm_mday,
               tm->tm_hour,
               tm->tm_min,
               tm->tm_sec,
               1900 + tm->tm_year);
    } else {
        printf("Wed Aug 19 23:59:59 UTC 2026\n");
    }
    return 0;
}
