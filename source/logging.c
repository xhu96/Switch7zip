/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#include "logging.h"
#include "app_config.h"
#include "fs_utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

const char *switch7zip_log_path(void) {
    return SWITCH7ZIP_LATEST_LOG;
}

static void write_prefix(FILE *f) {
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (tmv) {
        fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d ",
                tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
                tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
    } else {
        fputs("0000-00-00 00:00:00 ", f);
    }
}

void switch7zip_log_reset(void) {
    mkdir_p(SWITCH7ZIP_LOG_DIR);
    FILE *f = fopen(SWITCH7ZIP_LATEST_LOG, "wb");
    if (!f) return;
    write_prefix(f);
    fprintf(f, "Switch 7zip %s log started\n", APP_VERSION_STRING);
    fclose(f);
}

void switch7zip_log_line(const char *fmt, ...) {
    if (!fmt) return;
    mkdir_p(SWITCH7ZIP_LOG_DIR);
    FILE *f = fopen(SWITCH7ZIP_LATEST_LOG, "ab");
    if (!f) return;
    write_prefix(f);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}
