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

#ifndef G510S_LOG_H
#define G510S_LOG_H

/* ── Log levels ──────────────────────────────────────────────────────────── */

typedef enum {
    G510S_LOG_FATAL = 0,   /* program cannot continue */
    G510S_LOG_ERROR = 1,   /* operation failed */
    G510S_LOG_WARN  = 2,   /* degraded operation */
    G510S_LOG_INFO  = 3,   /* significant lifecycle events (default) */
    G510S_LOG_DEBUG = 4,   /* development details */
    G510S_LOG_TRACE = 5,   /* ultra-verbose: switch timing, hot paths */
} g510s_log_level_t;

/* ── API ─────────────────────────────────────────────────────────────────── */

/* Call once after argument parsing.  enable_file=1 → also write ~/.g510s/g510s.log */
void g510s_log_init(g510s_log_level_t level, int enable_file);

/* Open (or reopen) the log file.  Safe to call after check_dir() creates ~/.g510s */
void g510s_log_init_file(void);

/* Flush and close the log file.  Call at program exit. */
void g510s_log_close(void);

/* Core log function — use the macros below instead of calling this directly. */
void g510s_log(g510s_log_level_t level, const char *module, const char *func,
               const char *fmt, ...) __attribute__((format(printf, 4, 5)));

/* Parse a level name ("fatal","error","warn","info","debug","trace").
 * Returns G510S_LOG_INFO for unknown strings. */
g510s_log_level_t g510s_log_level_from_str(const char *s);

/* ── Per-file module tag ─────────────────────────────────────────────────── */

/* Each .c file defines LOG_MODULE before its first #include to give its log
 * lines a module tag.  Falls back to "?" if the file forgets. */
#ifndef LOG_MODULE
#define LOG_MODULE "?"
#endif

/* ── Convenience macros ──────────────────────────────────────────────────── */

#define LFATAL(fmt, ...) g510s_log(G510S_LOG_FATAL, LOG_MODULE, __func__, fmt, ##__VA_ARGS__)
#define LERROR(fmt, ...) g510s_log(G510S_LOG_ERROR, LOG_MODULE, __func__, fmt, ##__VA_ARGS__)
#define LWARN(fmt, ...)  g510s_log(G510S_LOG_WARN,  LOG_MODULE, __func__, fmt, ##__VA_ARGS__)
#define LINFO(fmt, ...)  g510s_log(G510S_LOG_INFO,  LOG_MODULE, __func__, fmt, ##__VA_ARGS__)
#define LDEBUG(fmt, ...) g510s_log(G510S_LOG_DEBUG, LOG_MODULE, __func__, fmt, ##__VA_ARGS__)
#define LTRACE(fmt, ...) g510s_log(G510S_LOG_TRACE, LOG_MODULE, __func__, fmt, ##__VA_ARGS__)

#endif /* G510S_LOG_H */
