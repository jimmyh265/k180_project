#ifndef GY_LOGGING_H
#define GY_LOGGING_H

#include <cstdarg>
#include <cstdio>
#include <cerrno>
#include <cstring>

#ifdef USE_SYSLOG
#include <syslog.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 初始化/關閉 logging
void init_logging(const char *ident);
void close_logging(void);

// printf-style logging
void log_info_fmt(const char *fmt, ...);
void log_error_fmt(const char *fmt, ...);            // 不含 errno
void log_error_errno_fmt(const char *fmt, ...);     // 含 errno

#ifdef __cplusplus
}
#endif

#endif // GY_LOGGING_H
