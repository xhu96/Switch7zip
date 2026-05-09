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

typedef enum ArchiveBrowseType {
    ARCHIVE_BROWSE_DIR = 0,
    ARCHIVE_BROWSE_FILE
} ArchiveBrowseType;

typedef struct ArchiveBrowseItem {
    char name[256];
    char path[SWITCH7ZIP_MAX_PATH];
    ArchiveBrowseType type;
    uint64_t size;
} ArchiveBrowseItem;

int archive_browse_list_dir(const char *archive_path,
                            const char *prefix,
                            ArchiveBrowseItem *items,
                            size_t max_items,
                            size_t *out_count,
                            char *error,
                            size_t error_size);

bool archive_browse_path_is_dir(const char *path);
