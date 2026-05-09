/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "app_config.h"

typedef struct CompressStats {
    uint64_t entries_seen;
    uint64_t files_added;
    uint64_t dirs_added;
    uint64_t bytes_read;
    uint64_t bytes_expected;
    uint64_t current_file_read;
    uint64_t current_file_size;
    uint64_t partial_files_left;
    char partial_path[SWITCH7ZIP_MAX_PATH];
    char last_entry[SWITCH7ZIP_MAX_PATH];
    char error_message[SWITCH7ZIP_STATUS_LEN];
} CompressStats;

typedef void (*CompressProgressCallback)(const CompressStats *stats, const char *entry_name, void *user_data);
typedef bool (*CompressCancelCallback)(void *user_data);

typedef struct CompressOptions {
    CompressCancelCallback cancel_cb;
    void *cancel_user_data;
} CompressOptions;

int compress_path_to_zip_ex(const char *source_path,
                            const char *zip_path,
                            CompressStats *stats,
                            CompressProgressCallback progress_cb,
                            void *user_data,
                            const CompressOptions *options);

int compress_path_to_zip(const char *source_path,
                         const char *zip_path,
                         CompressStats *stats,
                         CompressProgressCallback progress_cb,
                         void *user_data);
