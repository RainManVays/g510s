/*
 *  This file is part of g510s.
 *
 *  g510s is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  Copyright © 2015 John Augustine
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

#include "g510s-log.h"

/* ── Module state ────────────────────────────────────────────────────────── */

static g510s_log_level_t  s_level = G510S_LOG_INFO;
static FILE              *s_file  = NULL;
static pthread_mutex_t    s_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct timespec    s_t0;   /* monotonic origin for elapsed display */

/* ── Internal helpers ────────────────────────────────────────────────────── */

static const char *level_str(g510s_log_level_t level)
{
    switch (level) {
        case G510S_LOG_FATAL: return "FATAL";
        case G510S_LOG_ERROR: return "ERROR";
        case G510S_LOG_WARN:  return "WARN ";
        case G510S_LOG_INFO:  return "INFO ";
        case G510S_LOG_DEBUG: return "DEBUG";
        case G510S_LOG_TRACE: return "TRACE";
        default:              return "?????";
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void g510s_log_init(g510s_log_level_t level, int enable_file)
{
    s_level = level;
    clock_gettime(CLOCK_MONOTONIC, &s_t0);

    if (enable_file)
        g510s_log_init_file();
}

void g510s_log_init_file(void)
{
    if (s_file) return;   /* already open */

    const char *home = getenv("HOME");
    if (!home) return;

    char path[512];
    snprintf(path, sizeof(path), "%s/.g510s/g510s.log", home);
    s_file = fopen(path, "a");   /* NULL if dir doesn't exist yet */
}

void g510s_log_close(void)
{
    pthread_mutex_lock(&s_mutex);
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
    pthread_mutex_unlock(&s_mutex);
}

g510s_log_level_t g510s_log_level_from_str(const char *s)
{
    if (!s)                        return G510S_LOG_INFO;
    if (strcasecmp(s, "fatal") == 0) return G510S_LOG_FATAL;
    if (strcasecmp(s, "error") == 0) return G510S_LOG_ERROR;
    if (strcasecmp(s, "warn")  == 0) return G510S_LOG_WARN;
    if (strcasecmp(s, "info")  == 0) return G510S_LOG_INFO;
    if (strcasecmp(s, "debug") == 0) return G510S_LOG_DEBUG;
    if (strcasecmp(s, "trace") == 0) return G510S_LOG_TRACE;
    return G510S_LOG_INFO;
}

/*
 * Output format:
 *   2026-06-07 14:23:45.123 (+   234ms) [INFO ] [threads ] key_function: message
 */
void g510s_log(g510s_log_level_t level, const char *module, const char *func,
               const char *fmt, ...)
{
    if (level > s_level) return;

    /* Wall-clock timestamp */
    struct timespec wall, mono;
    clock_gettime(CLOCK_REALTIME,  &wall);
    clock_gettime(CLOCK_MONOTONIC, &mono);

    long elapsed_ms = (mono.tv_sec  - s_t0.tv_sec)  * 1000L
                    + (mono.tv_nsec - s_t0.tv_nsec) / 1000000L;

    struct tm tm_info;
    localtime_r(&wall.tv_sec, &tm_info);
    char ts[24];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_info);
    long wall_ms = wall.tv_nsec / 1000000L;

    /* Format caller's message */
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* Assemble full log line */
    char line[2176];
    int n = snprintf(line, sizeof(line),
                     "%s.%03ld (+%6ldms) [%s] [%-8s] %s: %s\n",
                     ts, wall_ms, elapsed_ms,
                     level_str(level),
                     module ? module : "?",
                     func   ? func   : "?",
                     msg);
    if (n <= 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;

    pthread_mutex_lock(&s_mutex);
    fwrite(line, 1, (size_t)n, stdout);
    fflush(stdout);
    if (s_file) {
        fwrite(line, 1, (size_t)n, s_file);
        fflush(s_file);
    }
    pthread_mutex_unlock(&s_mutex);
}
