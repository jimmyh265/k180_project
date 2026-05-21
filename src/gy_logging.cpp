#include "gy_logging.h"

void init_logging(const char *ident) {
    fprintf(stdout, "[%s] Logging started\n", ident ? ident : "grand_yeah");
    fflush(stdout);
}

void close_logging() {
    fprintf(stdout, "Logging closed\n");
    fflush(stdout);
}

void log_info_fmt(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fprintf(stdout, "INFO: %s\n", buf);
    fflush(stdout);
}

void log_error_fmt(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fprintf(stderr, "ERROR: %s\n", buf);
    fflush(stderr);
}

void log_error_errno_fmt(const char *fmt, ...) {
    int saved_errno = errno;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fprintf(stderr, "ERROR: %s: %s\n", buf, strerror(saved_errno));
    fflush(stderr);
}
