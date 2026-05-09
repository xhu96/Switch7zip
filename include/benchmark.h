/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"

typedef struct BenchmarkStats {
    uint64_t bytes_done;
    uint64_t bytes_expected;
    uint64_t write_bytes;
    uint64_t read_bytes;
    uint64_t write_elapsed_ms;
    uint64_t read_elapsed_ms;
    uint64_t write_bytes_per_second;
    uint64_t read_bytes_per_second;
    char phase[32];
    char error_message[SWITCH7ZIP_STATUS_LEN];
} BenchmarkStats;

typedef bool (*BenchmarkCancelCb)(void *user_data);
typedef void (*BenchmarkProgressCb)(const BenchmarkStats *stats, void *user_data);

typedef struct BenchmarkOptions {
    BenchmarkCancelCb cancel_cb;
    void *cancel_user_data;
} BenchmarkOptions;

int nxcmd_run_sd_benchmark(const char *target_dir,
                           uint64_t test_bytes,
                           BenchmarkStats *stats,
                           BenchmarkProgressCb progress_cb,
                           void *progress_user_data,
                           const BenchmarkOptions *options);
