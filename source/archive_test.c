/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#include "archive_test.h"
#include "app_config.h"

#include <archive.h>
#include <archive_entry.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define TEST_BUFFER_SIZE (1024 * 1024)
#define TEST_PROGRESS_INTERVAL (32ULL * 1024ULL * 1024ULL)

static void set_error(ExtractStats *stats, const char *context, const char *detail) {
    if (!stats) return;
    if (detail && *detail) {
        snprintf(stats->error_message, sizeof(stats->error_message), "%s: %s", context, detail);
    } else {
        snprintf(stats->error_message, sizeof(stats->error_message), "%s", context);
    }
}

static bool cancel_requested(const ExtractOptions *options) {
    return options && options->cancel_cb && options->cancel_cb(options->cancel_user_data);
}

static const char *entry_path_utf8_or_native(struct archive_entry *entry) {
    const char *path = archive_entry_pathname_utf8(entry);
    if (path && *path) return path;
    return archive_entry_pathname(entry);
}

static void update_archive_position(struct archive *reader, ExtractStats *stats) {
    if (!reader || !stats) return;
    la_int64_t pos = archive_filter_bytes(reader, 0);
    if (pos >= 0) stats->archive_bytes_read = (uint64_t)pos;
}

int test_archive_integrity_ex(const char *archive_path,
                              ExtractStats *stats,
                              ExtractProgressCallback progress_cb,
                              void *user_data,
                              const ExtractOptions *options) {
    if (stats) memset(stats, 0, sizeof(*stats));

    if (!archive_path || !*archive_path) {
        set_error(stats, "Invalid archive path", NULL);
        return -1;
    }

    if (stats) {
        struct stat archive_st;
        if (stat(archive_path, &archive_st) == 0 && archive_st.st_size > 0) {
            stats->archive_bytes_total = (uint64_t)archive_st.st_size;
            stats->bytes_expected = stats->archive_bytes_total;
        }
    }

    struct archive *reader = archive_read_new();
    if (!reader) {
        set_error(stats, "Could not allocate libarchive reader", NULL);
        return -1;
    }

    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    int r = archive_read_open_filename(reader, archive_path, TEST_BUFFER_SIZE);
    if (r != ARCHIVE_OK) {
        set_error(stats, "Could not open archive", archive_error_string(reader));
        archive_read_free(reader);
        return -1;
    }

    char buffer[TEST_BUFFER_SIZE];
    struct archive_entry *entry = NULL;
    while ((r = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        if (cancel_requested(options)) {
            set_error(stats, "Operation cancelled by user", NULL);
            archive_read_free(reader);
            return -1;
        }

        update_archive_position(reader, stats);
        const char *name = entry_path_utf8_or_native(entry);
        if (!name || !*name) name = "(unnamed entry)";

        mode_t filetype = archive_entry_filetype(entry);
        if (stats) {
            stats->entries_seen++;
            stats->current_entry_written = 0;
            stats->current_entry_size = archive_entry_size_is_set(entry) && archive_entry_size(entry) > 0
                                          ? (uint64_t)archive_entry_size(entry)
                                          : 0;
            snprintf(stats->last_entry, sizeof(stats->last_entry), "%s", name);
            if (filetype == AE_IFDIR) stats->dirs_created++;
        }
        if (progress_cb) progress_cb(stats, name, user_data);

        if (filetype == AE_IFDIR) {
            archive_read_data_skip(reader);
            continue;
        }

        if (filetype != AE_IFREG) {
            if (stats) stats->unsupported_entries_skipped++;
            archive_read_data_skip(reader);
            continue;
        }

        uint64_t last_report = stats ? stats->bytes_written : 0;
        for (;;) {
            if (cancel_requested(options)) {
                set_error(stats, "Operation cancelled by user", NULL);
                archive_read_free(reader);
                return -1;
            }

            la_ssize_t n = archive_read_data(reader, buffer, sizeof(buffer));
            update_archive_position(reader, stats);
            if (n == 0) break;
            if (n < 0) {
                set_error(stats, "Archive data test failed", archive_error_string(reader));
                archive_read_free(reader);
                return -1;
            }

            if (stats) {
                stats->bytes_written += (uint64_t)n;
                stats->current_entry_written += (uint64_t)n;
                if (progress_cb && stats->bytes_written - last_report >= TEST_PROGRESS_INTERVAL) {
                    last_report = stats->bytes_written;
                    progress_cb(stats, name, user_data);
                }
            }
        }

        if (stats) stats->files_written++;
        if (progress_cb) progress_cb(stats, name, user_data);
    }

    if (r != ARCHIVE_EOF) {
        set_error(stats, "Archive read failed", archive_error_string(reader));
        archive_read_free(reader);
        return -1;
    }

    update_archive_position(reader, stats);
    archive_read_close(reader);
    archive_read_free(reader);
    return 0;
}
