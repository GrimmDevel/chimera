/* =============================================================================
 * Chimera Operating System — User Space C Library
 * usr/libsystem/stdtime/localtime.c
 *
 * Real POSIX epoch calendar time calculation (leap years, weekdays, months).
 * =============================================================================
 */

#include <time.h>
#include <string.h>
#include <stdlib.h>

static const int s_days_in_month[2][12] = {
    { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
    { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

static inline int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static struct tm s_localtime_buf;
static struct tm s_gmtime_buf;

char *tzname[2] = { (char *)"UTC", (char *)"UTC" };
long timezone = 0;
int daylight = 0;

void tzset(void) {
    const char *tz = getenv("TZ");
    if (!tz || !*tz || strcmp(tz, "UTC") == 0 || strcmp(tz, "GMT") == 0) {
        tzname[0] = (char *)"UTC";
        tzname[1] = (char *)"UTC";
        timezone = 0;
        daylight = 0;
    } else if (strcmp(tz, "MSK") == 0 || strcmp(tz, "Europe/Moscow") == 0 || strcmp(tz, "UTC+3") == 0) {
        tzname[0] = (char *)"MSK";
        tzname[1] = (char *)"MSK";
        timezone = -10800;
        daylight = 0;
    } else {
        tzname[0] = (char *)"UTC";
        tzname[1] = (char *)"UTC";
        timezone = 0;
        daylight = 0;
    }
}

static struct tm *secs_to_tm(time_t epoch, long offset_sec, struct tm *res, const char *tz_abbr) {
    if (!res) return NULL;
    memset(res, 0, sizeof(*res));

    time_t t = epoch + offset_sec;
    long long days = t / 86400;
    long long rem = t % 86400;
    if (rem < 0) {
        rem += 86400;
        days--;
    }

    res->tm_hour = (int)(rem / 3600);
    res->tm_min = (int)((rem % 3600) / 60);
    res->tm_sec = (int)(rem % 60);

    // 1970-01-01 was Thursday (tm_wday = 4)
    res->tm_wday = (int)((days + 4) % 7);
    if (res->tm_wday < 0) res->tm_wday += 7;

    // calculate year from days
    long long y = 1970;
    while (1) {
        int leap = is_leap_year((int)y);
        int d_in_y = leap ? 366 : 365;
        if (days >= 0 && days < d_in_y) break;
        if (days >= d_in_y) {
            days -= d_in_y;
            y++;
        } else {
            y--;
            int prev_leap = is_leap_year((int)y);
            days += prev_leap ? 366 : 365;
        }
    }

    res->tm_year = (int)(y - 1900);
    res->tm_yday = (int)days;

    int leap = is_leap_year((int)y);
    int m = 0;
    while (m < 12 && days >= s_days_in_month[leap][m]) {
        days -= s_days_in_month[leap][m];
        m++;
    }
    res->tm_mon = m;
    res->tm_mday = (int)(days + 1);
    res->tm_isdst = 0;
    res->tm_gmtoff = offset_sec;
    res->tm_zone = (char *)tz_abbr;

    return res;
}

struct tm *gmtime_r(const time_t *timer, struct tm *result) {
    if (!timer || !result) return NULL;
    return secs_to_tm(*timer, 0, result, "UTC");
}

struct tm *gmtime(const time_t *timer) {
    if (!timer) return NULL;
    return gmtime_r(timer, &s_gmtime_buf);
}

struct tm *localtime_r(const time_t *timer, struct tm *result) {
    if (!timer || !result) return NULL;
    tzset();
    return secs_to_tm(*timer, -timezone, result, tzname[0]);
}

struct tm *localtime(const time_t *timer) {
    if (!timer) return NULL;
    return localtime_r(timer, &s_localtime_buf);
}

time_t timegm(struct tm *tm) {
    if (!tm) return (time_t)-1;
    int year = tm->tm_year + 1900;
    int mon = tm->tm_mon;
    int mday = tm->tm_mday;

    long long days = 0;
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    for (int y = 1970; y > year; y--) {
        days -= is_leap_year(y - 1) ? 366 : 365;
    }
    int leap = is_leap_year(year);
    for (int m = 0; m < mon && m < 12; m++) {
        days += s_days_in_month[leap][m];
    }
    days += (mday - 1);

    time_t res = (time_t)(days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec);
    return res;
}

time_t mktime(struct tm *tm) {
    if (!tm) return (time_t)-1;
    tzset();
    time_t t = timegm(tm);
    if (t == (time_t)-1) return (time_t)-1;
    t += timezone;
    localtime_r(&t, tm);
    return t;
}

__attribute__((weak)) double difftime(time_t time1, time_t time0) {
    return (double)(time1 - time0);
}


char *asctime_r(const struct tm *tm, char *buf) {
    if (!tm || !buf) return NULL;
    static const char *wday_name[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *mon_name[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    
    int w = (tm->tm_wday >= 0 && tm->tm_wday < 7) ? tm->tm_wday : 0;
    int m = (tm->tm_mon >= 0 && tm->tm_mon < 12) ? tm->tm_mon : 0;
    
    int year = tm->tm_year + 1900;
    char year_str[16];
    char num_buf[32];
    
    // snprintf format
    const char *wn = wday_name[w];
    const char *mn = mon_name[m];
    
    buf[0] = wn[0]; buf[1] = wn[1]; buf[2] = wn[2]; buf[3] = ' ';
    buf[4] = mn[0]; buf[5] = mn[1]; buf[6] = mn[2]; buf[7] = ' ';
    
    int day = tm->tm_mday;
    if (day < 10) { buf[8] = ' '; buf[9] = '0' + day; }
    else { buf[8] = '0' + (day / 10); buf[9] = '0' + (day % 10); }
    buf[10] = ' ';
    
    int hour = tm->tm_hour;
    buf[11] = '0' + (hour / 10); buf[12] = '0' + (hour % 10); buf[13] = ':';
    
    int min = tm->tm_min;
    buf[14] = '0' + (min / 10); buf[15] = '0' + (min % 10); buf[16] = ':';
    
    int sec = tm->tm_sec;
    buf[17] = '0' + (sec / 10); buf[18] = '0' + (sec % 10); buf[19] = ' ';
    
    // year
    if (year < 0) year = 0;
    buf[20] = '0' + ((year / 1000) % 10);
    buf[21] = '0' + ((year / 100) % 10);
    buf[22] = '0' + ((year / 10) % 10);
    buf[23] = '0' + (year % 10);
    buf[24] = '\n';
    buf[25] = '\0';
    return buf;
}

char *asctime(const struct tm *tm) {
    static char buf[32];
    return asctime_r(tm, buf);
}

char *ctime_r(const time_t *timep, char *buf) {
    struct tm tm_buf;
    if (!localtime_r(timep, &tm_buf)) return NULL;
    return asctime_r(&tm_buf, buf);
}

char *ctime(const time_t *timep) {
    static char buf[32];
    return ctime_r(timep, buf);
}
