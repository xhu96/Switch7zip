/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#pragma once

#include <stddef.h>

#include "app_config.h"

typedef struct ArchiveEntry {
    char name[256];
    char path[SWITCH7ZIP_MAX_PATH];
} ArchiveEntry;

int mkdir_p(const char *path);
int has_supported_archive_extension(const char *name);
int scan_archives(const char *directory, ArchiveEntry *entries, size_t capacity, size_t *count);
int make_archive_output_dir(const char *base_output_dir, const char *archive_name, char *out, size_t out_size);
int make_sibling_extract_dir(const char *archive_path, char *out, size_t out_size);
int make_sibling_zip_path(const char *source_path, char *out, size_t out_size);
