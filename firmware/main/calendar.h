#pragma once
#include <stdbool.h>
int date_parse(const char *iso);
void date_format(int date, char out[11]);
int date_day(int date);
int date_age(int birth, int date);
int date_today(void);
bool date_sync(long long epoch);
