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

typedef enum Fat32OversizeMode {
    FAT32_OVERSIZE_BLOCK = 0,
    FAT32_OVERSIZE_SPLIT_PARTS = 1,
    FAT32_OVERSIZE_SWITCH_CONCAT = 2
} Fat32OversizeMode;

typedef struct ExtractStats {
    uint64_t entries_seen;
    uint64_t files_written;
    uint64_t dirs_created;
    uint64_t bytes_written;
    uint64_t bytes_expected;
    uint64_t archive_bytes_read;
    uint64_t archive_bytes_total;
    uint64_t current_entry_written;
    uint64_t current_entry_size;
    uint64_t unsafe_paths_skipped;
    uint64_t unsupported_entries_skipped;
    uint64_t existing_files_skipped;
    uint64_t partial_files_left;
    uint64_t fat32_oversize_blocked;
    uint64_t fat32_oversize_split;
    uint64_t fat32_oversize_concat;
    uint64_t split_parts_written;
    uint64_t concat_attribute_failures;
    char partial_path[SWITCH7ZIP_MAX_PATH];
    char last_entry[SWITCH7ZIP_MAX_PATH];
    char error_message[SWITCH7ZIP_STATUS_LEN];
} ExtractStats;

typedef void (*ExtractProgressCallback)(const ExtractStats *stats, const char *entry_name, void *user_data);
typedef bool (*ExtractCancelCallback)(void *user_data);

typedef struct ExtractOptions {
    bool overwrite_existing;
    bool fat32_guard_enabled;
    Fat32OversizeMode fat32_oversize_mode;
    ExtractCancelCallback cancel_cb;
    void *cancel_user_data;
} ExtractOptions;


int extract_archive_selection_to_dir_ex(const char *archive_path,
                                        const char *selected_archive_path,
                                        const char *output_dir,
                                        ExtractStats *stats,
                                        ExtractProgressCallback progress_cb,
                                        void *user_data,
                                        const ExtractOptions *options);

int extract_archive_to_dir_ex(const char *archive_path,
                           const char *output_dir,
                           ExtractStats *stats,
                           ExtractProgressCallback progress_cb,
                           void *user_data,
                           const ExtractOptions *options);

int extract_archive_to_dir(const char *archive_path,
                           const char *output_dir,
                           ExtractStats *stats,
                           ExtractProgressCallback progress_cb,
                           void *user_data);
