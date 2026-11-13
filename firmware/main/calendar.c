#include "calendar.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

static bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
static int days_in(int y, int m)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[m-1] + (m == 2 && leap(y));
}

int date_parse(const char *s)
{
    if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return 0;
    for (int i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return 0;
    int y, m, d;
    if (sscanf(s, "%4d-%2d-%2d", &y, &m, &d) != 3 ||
        y < 1 || m < 1 || m > 12 || d < 1 || d > days_in(y,m)) return 0;
    return y*10000 + m*100 + d;
}

void date_format(int date, char out[11])
{
    if (!date) { *out = 0; return; }
    unsigned y = (unsigned)date / 10000 % 10000;
    unsigned m = (unsigned)date / 100 % 100;
    unsigned d = (unsigned)date % 100;
    snprintf(out, 11, "%04u-%02u-%02u", y, m, d);
}

int date_day(int date)
{
    if (!date) return 0;
    int y = date/10000, m = date/100%100;
    int prev = y-1;
    int result = prev*365 + prev/4 - prev/100 + prev/400 + date%100;
    for (int i = 1; i < m; ++i) result += days_in(y,i);
    return result;
}

int date_age(int birth, int date)
{
    if (!birth || date < birth) return -1;
    return date/10000 - birth/10000 - (date%10000 < birth%10000);
}

int date_today(void)
{
    time_t now = time(NULL);
    struct tm tm;
    if (now < 1704067200 || !localtime_r(&now, &tm)) return 0;
    return (tm.tm_year+1900)*10000 + (tm.tm_mon+1)*100 + tm.tm_mday;
}

bool date_sync(long long epoch)
{
    if (epoch < 1704067200LL || epoch >= 4133980800LL) return false;
    if (date_today()) return true;
    struct timeval now = { .tv_sec = (time_t)epoch };
    return settimeofday(&now, NULL) == 0;
}
