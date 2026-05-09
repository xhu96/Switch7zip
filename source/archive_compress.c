/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#include "archive_compress.h"
#include "fs_utils.h"

#include <archive.h>
#include <archive_entry.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#define COPY_BUFFER_SIZE (1024 * 1024)
#define PROGRESS_REPORT_INTERVAL (32ULL * 1024ULL * 1024ULL)

static void set_error(CompressStats *stats, const char *context, const char *detail) {
    if (!stats) return;
    if (detail && *detail) {
        snprintf(stats->error_message, sizeof(stats->error_message), "%s: %s", context, detail);
    } else {
        snprintf(stats->error_message, sizeof(stats->error_message), "%s", context);
    }
}

static void set_errno_error(CompressStats *stats, const char *context, const char *path) {
    if (!stats) return;
    if (path && *path) {
        snprintf(stats->error_message, sizeof(stats->error_message), "%s: %s (%s)", context, path, strerror(errno));
    } else {
        snprintf(stats->error_message, sizeof(stats->error_message), "%s: %s", context, strerror(errno));
    }
}

static const char *base_name(const char *path) {
    if (!path || !*path) return "archive";
    const char *end = path + strlen(path);
    while (end > path && end[-1] == '/') end--;
    const char *slash = end;
    while (slash > path && slash[-1] != '/') slash--;
    return *slash ? slash : "archive";
}

static int path_join_local(char *out, size_t out_size, const char *base, const char *name) {
    if (!out || !base || !name) return -1;
    const char *sep = (base[0] && base[strlen(base) - 1] == '/') ? "" : "/";
    int written = snprintf(out, out_size, "%s%s%s", base, sep, name);
    return (written < 0 || (size_t)written >= out_size) ? -1 : 0;
}

static int entry_join(char *out, size_t out_size, const char *base, const char *name) {
    if (!out || !base || !name) return -1;
    if (!base[0]) {
        int written = snprintf(out, out_size, "%s", name);
        return (written < 0 || (size_t)written >= out_size) ? -1 : 0;
    }
    int written = snprintf(out, out_size, "%s/%s", base, name);
    return (written < 0 || (size_t)written >= out_size) ? -1 : 0;
}

static int ensure_parent_dir(const char *path) {
    if (!path || !*path) return -1;
    char parent[SWITCH7ZIP_MAX_PATH];
    size_t len = strlen(path);
    if (len >= sizeof(parent)) return -1;
    memcpy(parent, path, len + 1);
    char *slash = strrchr(parent, '/');
    if (!slash) return 0;
    *slash = '\0';
    if (!parent[0]) return 0;
    return mkdir_p(parent);
}

static bool cancel_requested(const CompressOptions *options) {
    return options && options->cancel_cb && options->cancel_cb(options->cancel_user_data);
}

static uint64_t accumulate_regular_file_sizes(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (S_ISREG(st.st_mode)) return (uint64_t)st.st_size;
    if (!S_ISDIR(st.st_mode)) return 0;

    uint64_t total = 0;
    DIR *dir = opendir(path);
    if (!dir) return 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char child[SWITCH7ZIP_MAX_PATH];
        if (path_join_local(child, sizeof(child), path, ent->d_name) == 0) {
            total += accumulate_regular_file_sizes(child);
        }
    }
    closedir(dir);
    return total;
}

static int add_directory_entry(struct archive *writer,
                               const char *entry_name,
                               CompressStats *stats,
                               CompressProgressCallback progress_cb,
                               void *user_data,
                               const CompressOptions *options) {
    char dir_name[SWITCH7ZIP_MAX_PATH];
    int written = snprintf(dir_name, sizeof(dir_name), "%s/", entry_name);
    if (written < 0 || (size_t)written >= sizeof(dir_name)) return -1;

    if (cancel_requested(options)) {
        set_error(stats, "Operation cancelled by user", NULL);
        return ARCHIVE_FATAL;
    }

    struct archive_entry *entry = archive_entry_new();
    if (!entry) return -1;
    archive_entry_set_pathname(entry, dir_name);
    archive_entry_set_filetype(entry, AE_IFDIR);
    archive_entry_set_perm(entry, 0755);
    archive_entry_set_size(entry, 0);
    archive_entry_set_mtime(entry, time(NULL), 0);

    int r = archive_write_header(writer, entry);
    archive_entry_free(entry);
    if (r != ARCHIVE_OK) return r;

    if (stats) {
        stats->entries_seen++;
        stats->dirs_added++;
        snprintf(stats->last_entry, sizeof(stats->last_entry), "%s", dir_name);
    }
    if (progress_cb) progress_cb(stats, dir_name, user_data);
    return ARCHIVE_OK;
}

static int add_file_entry(struct archive *writer,
                          const char *fs_path,
                          const char *entry_name,
                          const struct stat *st,
                          CompressStats *stats,
                          CompressProgressCallback progress_cb,
                          void *user_data,
                          const CompressOptions *options) {
    if (cancel_requested(options)) {
        set_error(stats, "Operation cancelled by user", NULL);
        return ARCHIVE_FATAL;
    }

    FILE *in = fopen(fs_path, "rb");
    if (!in) {
        set_errno_error(stats, "Could not open file for compression", fs_path);
        return ARCHIVE_FATAL;
    }

    struct archive_entry *entry = archive_entry_new();
    if (!entry) {
        fclose(in);
        set_error(stats, "Could not allocate archive entry", NULL);
        return ARCHIVE_FATAL;
    }
    archive_entry_set_pathname(entry, entry_name);
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, st->st_size);
    archive_entry_set_mtime(entry, st->st_mtime, 0);

    int r = archive_write_header(writer, entry);
    archive_entry_free(entry);
    if (r != ARCHIVE_OK) {
        fclose(in);
        return r;
    }

    if (stats) {
        stats->entries_seen++;
        stats->current_file_read = 0;
        stats->current_file_size = st->st_size > 0 ? (uint64_t)st->st_size : 0;
        snprintf(stats->last_entry, sizeof(stats->last_entry), "%s", entry_name);
    }
    if (progress_cb) progress_cb(stats, entry_name, user_data);

    char *buffer = (char *)malloc(COPY_BUFFER_SIZE);
    if (!buffer) {
        fclose(in);
        set_error(stats, "Out of memory allocating compression buffer", NULL);
        return ARCHIVE_FATAL;
    }

    uint64_t last_report = stats ? stats->bytes_read : 0;
    int rc = ARCHIVE_OK;
    for (;;) {
        if (cancel_requested(options)) {
            set_error(stats, "Operation cancelled by user", NULL);
            rc = ARCHIVE_FATAL;
            break;
        }
        size_t n = fread(buffer, 1, COPY_BUFFER_SIZE, in);
        if (n > 0) {
            la_ssize_t wr = archive_write_data(writer, buffer, n);
            if (wr < 0 || (size_t)wr != n) {
                rc = ARCHIVE_FATAL;
                break;
            }
            if (stats) {
                stats->bytes_read += (uint64_t)n;
                stats->current_file_read += (uint64_t)n;
                if (progress_cb && stats->bytes_read - last_report >= PROGRESS_REPORT_INTERVAL) {
                    last_report = stats->bytes_read;
                    progress_cb(stats, entry_name, user_data);
                    if (cancel_requested(options)) {
                        set_error(stats, "Operation cancelled by user", NULL);
                        rc = ARCHIVE_FATAL;
                        break;
                    }
                }
            }
        }
        if (n < COPY_BUFFER_SIZE) {
            if (ferror(in)) {
                set_errno_error(stats, "Could not read file during compression", fs_path);
                rc = ARCHIVE_FATAL;
            }
            break;
        }
    }

    free(buffer);
    fclose(in);
    if (rc != ARCHIVE_OK) return rc;

    if (stats) {
        stats->files_added++;
        if (stats->current_file_size > 0 && stats->current_file_read > stats->current_file_size) {
            stats->current_file_read = stats->current_file_size;
        }
    }
    if (progress_cb) progress_cb(stats, entry_name, user_data);
    return ARCHIVE_OK;
}

static int add_path_recursive(struct archive *writer,
                              const char *fs_path,
                              const char *entry_name,
                              CompressStats *stats,
                              CompressProgressCallback progress_cb,
                              void *user_data,
                              const CompressOptions *options) {
    if (cancel_requested(options)) {
        set_error(stats, "Operation cancelled by user", NULL);
        return ARCHIVE_FATAL;
    }

    struct stat st;
    if (stat(fs_path, &st) != 0) {
        set_errno_error(stats, "Could not stat path", fs_path);
        return ARCHIVE_FATAL;
    }

    if (S_ISDIR(st.st_mode)) {
        int r = add_directory_entry(writer, entry_name, stats, progress_cb, user_data, options);
        if (r != ARCHIVE_OK) return r;

        DIR *dir = opendir(fs_path);
        if (!dir) {
            set_errno_error(stats, "Could not open directory for compression", fs_path);
            return ARCHIVE_FATAL;
        }

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child_path[SWITCH7ZIP_MAX_PATH];
            char child_entry[SWITCH7ZIP_MAX_PATH];
            if (path_join_local(child_path, sizeof(child_path), fs_path, ent->d_name) != 0 ||
                entry_join(child_entry, sizeof(child_entry), entry_name, ent->d_name) != 0) {
                closedir(dir);
                set_error(stats, "Path too long while compressing", ent->d_name);
                return ARCHIVE_FATAL;
            }
            r = add_path_recursive(writer, child_path, child_entry, stats, progress_cb, user_data, options);
            if (r != ARCHIVE_OK) {
                closedir(dir);
                return r;
            }
        }
        closedir(dir);
        return ARCHIVE_OK;
    }

    if (S_ISREG(st.st_mode)) {
        return add_file_entry(writer, fs_path, entry_name, &st, stats, progress_cb, user_data, options);
    }

    /* Skip device/special entries. */
    return ARCHIVE_OK;
}

int compress_path_to_zip_ex(const char *source_path,
                            const char *zip_path,
                            CompressStats *stats,
                            CompressProgressCallback progress_cb,
                            void *user_data,
                            const CompressOptions *options) {
    if (stats) memset(stats, 0, sizeof(*stats));

    if (!source_path || !*source_path || !zip_path || !*zip_path) {
        set_error(stats, "Invalid source or output path", NULL);
        return -1;
    }

    if (stats) {
        stats->bytes_expected = accumulate_regular_file_sizes(source_path);
        if (progress_cb) progress_cb(stats, "Scanning complete", user_data);
        if (cancel_requested(options)) {
            set_error(stats, "Operation cancelled by user", NULL);
            return -1;
        }
    }

    if (ensure_parent_dir(zip_path) != 0) {
        set_errno_error(stats, "Could not create compression output folder", zip_path);
        return -1;
    }

    struct archive *writer = archive_write_new();
    if (!writer) {
        set_error(stats, "Could not allocate libarchive writer", NULL);
        return -1;
    }

    archive_write_set_format_zip(writer);
    /* Deflate is the useful default for a 7Zip-like tool; if the option is unsupported, continue with libarchive's ZIP default. */
    archive_write_set_options(writer, "zip:compression=deflate");

    char partial_zip_path[SWITCH7ZIP_MAX_PATH];
    int partial_written = snprintf(partial_zip_path, sizeof(partial_zip_path), "%s%s", zip_path, SWITCH7ZIP_PARTIAL_SUFFIX);
    if (partial_written < 0 || (size_t)partial_written >= sizeof(partial_zip_path)) {
        set_error(stats, "Partial ZIP path too long", zip_path);
        archive_write_free(writer);
        return -1;
    }
    remove(partial_zip_path);

    int r = archive_write_open_filename(writer, partial_zip_path);
    if (r != ARCHIVE_OK) {
        set_error(stats, "Could not create partial ZIP file", archive_error_string(writer));
        archive_write_free(writer);
        return -1;
    }

    char entry_root[SWITCH7ZIP_MAX_PATH];
    snprintf(entry_root, sizeof(entry_root), "%s", base_name(source_path));
    if (!entry_root[0]) snprintf(entry_root, sizeof(entry_root), "archive");

    r = add_path_recursive(writer, source_path, entry_root, stats, progress_cb, user_data, options);
    if (r != ARCHIVE_OK) {
        if (!stats || stats->error_message[0] == 0) {
            const char *err = archive_error_string(writer);
            if (err && *err) set_error(stats, "Compression failed", err);
            else set_error(stats, "Compression failed", NULL);
        }
        if (stats) {
            stats->partial_files_left++;
            snprintf(stats->partial_path, sizeof(stats->partial_path), "%s", partial_zip_path);
        }
        archive_write_close(writer);
        archive_write_free(writer);
        return -1;
    }

    r = archive_write_close(writer);
    archive_write_free(writer);
    if (r != ARCHIVE_OK) {
        if (stats) {
            stats->partial_files_left++;
            snprintf(stats->partial_path, sizeof(stats->partial_path), "%s", partial_zip_path);
        }
        set_error(stats, "Could not finalize partial ZIP file", NULL);
        return -1;
    }

    remove(zip_path);
    if (rename(partial_zip_path, zip_path) != 0) {
        if (stats) {
            stats->partial_files_left++;
            snprintf(stats->partial_path, sizeof(stats->partial_path), "%s", partial_zip_path);
        }
        set_errno_error(stats, "Could not publish completed ZIP", zip_path);
        return -1;
    }

    return 0;
}


int compress_path_to_zip(const char *source_path,
                         const char *zip_path,
                         CompressStats *stats,
                         CompressProgressCallback progress_cb,
                         void *user_data) {
    CompressOptions options;
    memset(&options, 0, sizeof(options));
    return compress_path_to_zip_ex(source_path, zip_path, stats, progress_cb, user_data, &options);
}
