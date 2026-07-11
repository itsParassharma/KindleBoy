/*
 * system_kindle.c — the boring-but-necessary bits on the Kindle: a clock, a
 * sleep, and a log file.
 */
#include "../platform.h"

#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define LOG_PATH   "/mnt/us/kindleboy.log"
#define LOG_CAP    (1024 * 1024)   /* truncate the log past 1 MiB */

uint64_t plat_now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

void plat_sleep_us(uint64_t us)
{
	struct timespec ts;
	ts.tv_sec  = (time_t)(us / 1000000ull);
	ts.tv_nsec = (long)((us % 1000000ull) * 1000ull);
	nanosleep(&ts, NULL);
}

void plat_log(const char *fmt, ...)
{
	static FILE *f;
	static long  written;

	if (!f) {
		f = fopen(LOG_PATH, "a");
		if (!f) return;
		written = ftell(f);
	}
	/* Roll over if the log grew too large. */
	if (written > LOG_CAP) {
		freopen(LOG_PATH, "w", f);
		written = 0;
	}

	va_list ap; va_start(ap, fmt);
	int n = vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fflush(f);
	if (n > 0) written += n + 1;
}
