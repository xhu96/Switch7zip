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

typedef struct FileOpStats {
    uint64_t bytes_done;
    uint64_t bytes_expected;
    uint64_t files_done;
    uint64_t dirs_done;
    uint64_t entries_done;
    uint64_t failures;
    uint64_t partial_files_left;
    char partial_path[SWITCH7ZIP_MAX_PATH];
    char last_entry[SWITCH7ZIP_MAX_PATH];
    char error_message[SWITCH7ZIP_STATUS_LEN];
} FileOpStats;

typedef bool (*FileOpCancelCb)(void *user_data);
typedef void (*FileOpProgressCb)(const FileOpStats *stats, const char *entry_name, void *user_data);

typedef struct FileOpOptions {
    FileOpCancelCb cancel_cb;
    void *cancel_user_data;
} FileOpOptions;

int fileop_copy_path_ex(const char *source_path,
                        const char *destination_dir,
                        char *actual_destination,
                        size_t actual_destination_size,
                        FileOpStats *stats,
                        FileOpProgressCb progress_cb,
                        void *progress_user_data,
                        const FileOpOptions *options);

int fileop_move_path_ex(const char *source_path,
                        const char *destination_dir,
                        char *actual_destination,
                        size_t actual_destination_size,
                        FileOpStats *stats,
                        FileOpProgressCb progress_cb,
                        void *progress_user_data,
                        const FileOpOptions *options);

int fileop_trash_path_ex(const char *source_path,
                         const char *trash_dir,
                         char *actual_destination,
                         size_t actual_destination_size,
                         FileOpStats *stats,
                         FileOpProgressCb progress_cb,
                         void *progress_user_data,
                         const FileOpOptions *options);


int fileop_delete_path_ex(const char *source_path,
                          FileOpStats *stats,
                          FileOpProgressCb progress_cb,
                          void *progress_user_data,
                          const FileOpOptions *options);

int fileop_cleanup_partials_ex(const char *root_path,
                               FileOpStats *stats,
                               FileOpProgressCb progress_cb,
                               void *progress_user_data,
                               const FileOpOptions *options);

int fileop_create_unique_folder(const char *parent_dir,
                                const char *preferred_name,
                                char *actual_path,
                                size_t actual_path_size);

int fileop_rename_path(const char *source_path,
                       const char *new_name,
                       char *actual_path,
                       size_t actual_path_size);
