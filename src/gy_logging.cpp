#include "gy_logging.h"

#ifdef USE_SYSLOG

void init_logging(const char *ident) {
    openlog(ident, LOG_PID | LOG_CONS, LOG_USER);
}

void close_logging() {
    closelog();
}

void log_info_fmt(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    syslog(LOG_INFO, "%s", buf);
}

void log_error_fmt(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    syslog(LOG_ERR, "%s", buf);
}

void log_error_errno_fmt(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    syslog(LOG_ERR, "%s: %s", buf, strerror(errno));
}

#else  // USE_SYSLOG not defined → printf 模式

void init_logging(const char *ident) {
    printf("[%s] Logging started\n", ident);
}

void close_logging() {
    printf("Logging closed\n");
}

void log_info_fmt(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("INFO: %s\n", buf);
}

void log_error_fmt(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("ERROR: %s\n", buf);
}

void log_error_errno_fmt(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("ERROR: %s: %s\n", buf, strerror(errno));
}

#endif
