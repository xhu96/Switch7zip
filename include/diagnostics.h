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

typedef struct MultipartInfo {
    bool is_multipart;
    bool selected_first_part;
    bool missing_parts;
    unsigned selected_index;
    unsigned highest_index;
    unsigned part_count;
    unsigned first_missing_index;
    char first_part_path[SWITCH7ZIP_MAX_PATH];
    char message[SWITCH7ZIP_STATUS_LEN];
} MultipartInfo;

typedef struct PathSizeInfo {
    uint64_t bytes;
    uint64_t files;
    uint64_t dirs;
    uint64_t failures;
} PathSizeInfo;

typedef struct ArchiveEstimate {
    uint64_t bytes;
    uint64_t files;
    uint64_t dirs;
    uint64_t unsafe_paths;
    uint64_t unsupported_entries;
    uint64_t largest_file_bytes;
    uint64_t files_over_fat32_limit;
    char largest_file_path[SWITCH7ZIP_MAX_PATH];
    char first_file_over_fat32_limit[SWITCH7ZIP_MAX_PATH];
} ArchiveEstimate;

uint64_t nxcmd_free_space_for_path(const char *path);
int nxcmd_measure_path_tree(const char *path, PathSizeInfo *info);
int nxcmd_estimate_archive_unpacked_size(const char *archive_path,
                                         const char *selected_archive_path,
                                         ArchiveEstimate *estimate,
                                         char *error,
                                         size_t error_size);
int nxcmd_check_multipart_archive(const char *archive_path, MultipartInfo *info);
bool nxcmd_file_is_text_like(const char *name);
bool nxcmd_file_is_image_like(const char *name);
