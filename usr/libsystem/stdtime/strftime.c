/* =============================================================================
 * XIU Operating System — User Space C Library
 * usr/libsystem/stdtime/strftime.c
 *
 * Full POSIX strftime format string generator.
 * =============================================================================
 */

#include <time.h>
#include <stdio.h>
#include <string.h>

static const char *s_days_abbrev[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *s_days_full[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *s_months_abbrev[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *s_months_full[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    if (!s || max == 0 || !format || !tm) return 0;

    char *dst = s;
    char *end = s + max - 1; // leave room for null terminator

    while (*format && dst < end) {
        if (*format != '%') {
            *dst++ = *format++;
            continue;
        }

        format++; // skip '%'
        if (*format == '\0') break;

        char temp[64];
        temp[0] = '\0';
        int temp_len = 0;

        switch (*format) {
            case '%':
                temp[0] = '%'; temp[1] = '\0';
                temp_len = 1;
                break;
            case 'a': // Abbreviated weekday
                if (tm->tm_wday >= 0 && tm->tm_wday < 7) {
                    temp_len = snprintf(temp, sizeof(temp), "%s", s_days_abbrev[tm->tm_wday]);
                }
                break;
            case 'A': // Full weekday
                if (tm->tm_wday >= 0 && tm->tm_wday < 7) {
                    temp_len = snprintf(temp, sizeof(temp), "%s", s_days_full[tm->tm_wday]);
                }
                break;
            case 'b':
            case 'h': // Abbreviated month
                if (tm->tm_mon >= 0 && tm->tm_mon < 12) {
                    temp_len = snprintf(temp, sizeof(temp), "%s", s_months_abbrev[tm->tm_mon]);
                }
                break;
            case 'B': // Full month
                if (tm->tm_mon >= 0 && tm->tm_mon < 12) {
                    temp_len = snprintf(temp, sizeof(temp), "%s", s_months_full[tm->tm_mon]);
                }
                break;
            case 'c': // Date and time representation
                temp_len = snprintf(temp, sizeof(temp), "%s %s %2d %02d:%02d:%02d %d",
                                    (tm->tm_wday >= 0 && tm->tm_wday < 7) ? s_days_abbrev[tm->tm_wday] : "",
                                    (tm->tm_mon >= 0 && tm->tm_mon < 12) ? s_months_abbrev[tm->tm_mon] : "",
                                    tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
                break;
            case 'C': // Century (00-99)
                temp_len = snprintf(temp, sizeof(temp), "%02d", (tm->tm_year + 1900) / 100);
                break;
            case 'd': // Day of month (01-31)
                temp_len = snprintf(temp, sizeof(temp), "%02d", tm->tm_mday);
                break;
            case 'D': // %m/%d/%y
                temp_len = snprintf(temp, sizeof(temp), "%02d/%02d/%02d", tm->tm_mon + 1, tm->tm_mday, (tm->tm_year + 1900) % 100);
                break;
            case 'e': // Day of month, space padded ( 1-31)
                temp_len = snprintf(temp, sizeof(temp), "%2d", tm->tm_mday);
                break;
            case 'F': // %Y-%m-%d
                temp_len = snprintf(temp, sizeof(temp), "%04d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
                break;
            case 'H': // Hour (00-23)
                temp_len = snprintf(temp, sizeof(temp), "%02d", tm->tm_hour);
                break;
            case 'I': // Hour (01-12)
                {
                    int h = tm->tm_hour % 12;
                    if (h == 0) h = 12;
                    temp_len = snprintf(temp, sizeof(temp), "%02d", h);
                }
                break;
            case 'j': // Day of year (001-366)
                temp_len = snprintf(temp, sizeof(temp), "%03d", tm->tm_yday + 1);
                break;
            case 'm': // Month (01-12)
                temp_len = snprintf(temp, sizeof(temp), "%02d", tm->tm_mon + 1);
                break;
            case 'M': // Minute (00-59)
                temp_len = snprintf(temp, sizeof(temp), "%02d", tm->tm_min);
                break;
            case 'n':
                temp[0] = '\n'; temp[1] = '\0'; temp_len = 1;
                break;
            case 'p': // AM/PM
                temp_len = snprintf(temp, sizeof(temp), "%s", (tm->tm_hour >= 12) ? "PM" : "AM");
                break;
            case 'r': // 12-hour time (%I:%M:%S %p)
                {
                    int h = tm->tm_hour % 12;
                    if (h == 0) h = 12;
                    temp_len = snprintf(temp, sizeof(temp), "%02d:%02d:%02d %s", h, tm->tm_min, tm->tm_sec, (tm->tm_hour >= 12) ? "PM" : "AM");
                }
                break;
            case 'R': // %H:%M
                temp_len = snprintf(temp, sizeof(temp), "%02d:%02d", tm->tm_hour, tm->tm_min);
                break;
            case 'S': // Second (00-60)
                temp_len = snprintf(temp, sizeof(temp), "%02d", tm->tm_sec);
                break;
            case 't':
                temp[0] = '\t'; temp[1] = '\0'; temp_len = 1;
                break;
            case 'T': // %H:%M:%S
                temp_len = snprintf(temp, sizeof(temp), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
                break;
            case 'u': // Weekday (1-7, 1=Monday)
                temp_len = snprintf(temp, sizeof(temp), "%d", (tm->tm_wday == 0) ? 7 : tm->tm_wday);
                break;
            case 'w': // Weekday (0-6, 0=Sunday)
                temp_len = snprintf(temp, sizeof(temp), "%d", tm->tm_wday);
                break;
            case 'x': // Date (%m/%d/%y)
                temp_len = snprintf(temp, sizeof(temp), "%02d/%02d/%02d", tm->tm_mon + 1, tm->tm_mday, (tm->tm_year + 1900) % 100);
                break;
            case 'X': // Time (%H:%M:%S)
                temp_len = snprintf(temp, sizeof(temp), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
                break;
            case 'y': // Year 2-digit (00-99)
                temp_len = snprintf(temp, sizeof(temp), "%02d", (tm->tm_year + 1900) % 100);
                break;
            case 'Y': // Year 4-digit
                temp_len = snprintf(temp, sizeof(temp), "%04d", tm->tm_year + 1900);
                break;
            case 'z': // Timezone offset (+HHMM or -HHMM)
                {
                    long off = tm->tm_gmtoff;
                    char sign = '+';
                    if (off < 0) { sign = '-'; off = -off; }
                    int off_h = (int)(off / 3600);
                    int off_m = (int)((off % 3600) / 60);
                    temp_len = snprintf(temp, sizeof(temp), "%c%02d%02d", sign, off_h, off_m);
                }
                break;
            case 'Z': // Timezone name
                temp_len = snprintf(temp, sizeof(temp), "%s", tm->tm_zone ? tm->tm_zone : "UTC");
                break;
            default:
                temp[0] = '%'; temp[1] = *format; temp[2] = '\0';
                temp_len = 2;
                break;
        }

        for (int i = 0; i < temp_len && dst < end; i++) {
            *dst++ = temp[i];
        }
        format++;
    }

    *dst = '\0';
    return (size_t)(dst - s);
}

size_t strftime_l(char *s, size_t max, const char *format, const struct tm *tm, void *loc) {
    (void)loc;
    return strftime(s, max, format, tm);
}
