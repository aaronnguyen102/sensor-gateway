/**
 * @file    log_process.c
 * @brief   Thread-safe logging implementation.
 * @author  Nguyen Si Phu
 */

#include "log_process.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

/* ── Private state ────────────────────────────────────────── */

static FILE            *g_log_fp    = NULL;
static pthread_mutex_t  g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Padded to 7 chars ("WARNING") for aligned log output. */
static const char *level_str[] = {
    "DEBUG  ",
    "INFO   ",
    "WARNING",
    "ERROR  "
};

/* ── Public API ───────────────────────────────────────────── */

int log_init(void)
{
    /* Append mode: preserve logs across restarts for post-crash analysis. */
    g_log_fp = fopen(LOG_FILE, "a");
    if (!g_log_fp) {
        perror("[LogProcess] fopen(gateway.log)");
        return -1;
    }

    log_write(LOG_INFO, "LogProcess", "Log system initialized → %s", LOG_FILE);
    return 0;
}

void log_write(LogLevel level, const char *module, const char *fmt, ...)
{
    /* ── Build timestamp (outside mutex to minimize critical section) ── */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);   /* Thread-safe variant of localtime() */

    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    int ms = (int)(ts.tv_nsec / 1000000);

    /* ── Format the caller's message ──────────────────────────────────── */
    char msg_buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    /* Clamp invalid level to INFO to prevent out-of-bounds access. */
    if (level < LOG_DEBUG || level > LOG_ERROR) {
        level = LOG_INFO;
    }

    /* ── Write to file and stdout (critical section) ──────────────────── */
    pthread_mutex_lock(&g_log_mutex);

    if (g_log_fp) {
        fprintf(g_log_fp, "[%s.%03d] [%s] [%s] %s\n",
                time_buf, ms, level_str[level], module, msg_buf);
        fflush(g_log_fp);   /* Flush every line — essential for crash debugging */
    }

    printf("[%s.%03d] [%s] [%s] %s\n",
           time_buf, ms, level_str[level], module, msg_buf);

    pthread_mutex_unlock(&g_log_mutex);
}

void log_close(void)
{
    /* Write final message BEFORE acquiring the mutex.
     * log_write() locks internally — calling it while holding the
     * (non-recursive) mutex would deadlock. */
    log_write(LOG_INFO, "LogProcess", "Log system shutting down.");

    pthread_mutex_lock(&g_log_mutex);

    if (g_log_fp) {
        fflush(g_log_fp);
        fclose(g_log_fp);
        g_log_fp = NULL;
    }

    pthread_mutex_unlock(&g_log_mutex);
    pthread_mutex_destroy(&g_log_mutex);
}
