/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#include "benchmark.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t now_ms(void) {
    clock_t c = clock();
    if (c == (clock_t)-1) return 0;
    return (uint64_t)((double)c * 1000.0 / (double)CLOCKS_PER_SEC);
}

static int join_path(char *out, size_t out_size, const char *base, const char *name) {
    if (!out || !base || !name) return -1;
    const char *sep = (base[0] && base[strlen(base) - 1] == '/') ? "" : "/";
    int n = snprintf(out, out_size, "%s%s%s", base, sep, name);
    return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
}

static bool cancelled(const BenchmarkOptions *options) {
    return options && options->cancel_cb && options->cancel_cb(options->cancel_user_data);
}

static void emit(BenchmarkStats *stats, BenchmarkProgressCb cb, void *user_data) {
    if (cb) cb(stats, user_data);
}

static void fill_pattern(unsigned char *buf, size_t n, uint64_t offset) {
    for (size_t i = 0; i < n; ++i) buf[i] = (unsigned char)((offset + i) * 131u + 17u);
}

int nxcmd_run_sd_benchmark(const char *target_dir,
                           uint64_t test_bytes,
                           BenchmarkStats *stats,
                           BenchmarkProgressCb progress_cb,
                           void *progress_user_data,
                           const BenchmarkOptions *options) {
    if (!target_dir || !stats || test_bytes == 0) return -1;
    memset(stats, 0, sizeof(*stats));
    stats->bytes_expected = test_bytes * 2ULL;
    snprintf(stats->phase, sizeof(stats->phase), "Preparing");

    char path[SWITCH7ZIP_MAX_PATH];
    if (join_path(path, sizeof(path), target_dir, ".nxcommander_benchmark.tmp") != 0) {
        snprintf(stats->error_message, sizeof(stats->error_message), "Benchmark path is too long");
        return -1;
    }

    const size_t chunk_size = 1024 * 1024;
    unsigned char *buffer = (unsigned char *)malloc(chunk_size);
    if (!buffer) {
        snprintf(stats->error_message, sizeof(stats->error_message), "Could not allocate benchmark buffer");
        return -1;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        snprintf(stats->error_message, sizeof(stats->error_message), "Could not create temp file: %s", strerror(errno));
        free(buffer);
        return -1;
    }

    snprintf(stats->phase, sizeof(stats->phase), "Writing");
    uint64_t write_start = now_ms();
    uint64_t offset = 0;
    while (offset < test_bytes) {
        if (cancelled(options)) {
            snprintf(stats->error_message, sizeof(stats->error_message), "Benchmark cancelled");
            fclose(f);
            remove(path);
            free(buffer);
            return -2;
        }
        size_t want = chunk_size;
        if (test_bytes - offset < (uint64_t)want) want = (size_t)(test_bytes - offset);
        fill_pattern(buffer, want, offset);
        size_t written = fwrite(buffer, 1, want, f);
        if (written != want) {
            snprintf(stats->error_message, sizeof(stats->error_message), "Write failed after %llu bytes", (unsigned long long)offset);
            fclose(f);
            remove(path);
            free(buffer);
            return -1;
        }
        offset += (uint64_t)written;
        stats->write_bytes = offset;
        stats->bytes_done = offset;
        uint64_t now = now_ms();
        stats->write_elapsed_ms = now > write_start ? now - write_start : 0;
        if (stats->write_elapsed_ms > 0) stats->write_bytes_per_second = (stats->write_bytes * 1000ULL) / stats->write_elapsed_ms;
        emit(stats, progress_cb, progress_user_data);
    }
    fflush(f);
    fclose(f);

    f = fopen(path, "rb");
    if (!f) {
        snprintf(stats->error_message, sizeof(stats->error_message), "Could not reopen temp file: %s", strerror(errno));
        remove(path);
        free(buffer);
        return -1;
    }

    snprintf(stats->phase, sizeof(stats->phase), "Reading");
    uint64_t read_start = now_ms();
    offset = 0;
    while (offset < test_bytes) {
        if (cancelled(options)) {
            snprintf(stats->error_message, sizeof(stats->error_message), "Benchmark cancelled");
            fclose(f);
            remove(path);
            free(buffer);
            return -2;
        }
        size_t want = chunk_size;
        if (test_bytes - offset < (uint64_t)want) want = (size_t)(test_bytes - offset);
        size_t got = fread(buffer, 1, want, f);
        if (got != want) {
            snprintf(stats->error_message, sizeof(stats->error_message), "Read failed after %llu bytes", (unsigned long long)offset);
            fclose(f);
            remove(path);
            free(buffer);
            return -1;
        }
        offset += (uint64_t)got;
        stats->read_bytes = offset;
        stats->bytes_done = test_bytes + offset;
        uint64_t now = now_ms();
        stats->read_elapsed_ms = now > read_start ? now - read_start : 0;
        if (stats->read_elapsed_ms > 0) stats->read_bytes_per_second = (stats->read_bytes * 1000ULL) / stats->read_elapsed_ms;
        emit(stats, progress_cb, progress_user_data);
    }
    fclose(f);
    remove(path);
    snprintf(stats->phase, sizeof(stats->phase), "Done");
    emit(stats, progress_cb, progress_user_data);
    free(buffer);
    return 0;
}
