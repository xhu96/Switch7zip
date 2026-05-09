/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#include "file_ops.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fs_utils.h"

#define FILEOP_BUFFER_SIZE (1024u * 1024u)

static const char *path_basename(const char *path) {
    if (!path || !*path) return "item";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int join_path(char *out, size_t out_size, const char *base, const char *name) {
    if (!out || !base || !name) return -1;
    const char *sep = (base[0] && base[strlen(base) - 1] == '/') ? "" : "/";
    int n = snprintf(out, out_size, "%s%s%s", base, sep, name);
    return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
}

static bool path_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

static bool is_dir_path(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void stats_error(FileOpStats *stats, const char *fmt, const char *path) {
    if (!stats) return;
    if (path) snprintf(stats->error_message, sizeof(stats->error_message), fmt, path);
    else snprintf(stats->error_message, sizeof(stats->error_message), "%s", fmt ? fmt : "file operation failed");
}

static bool should_cancel(const FileOpOptions *options) {
    return options && options->cancel_cb && options->cancel_cb(options->cancel_user_data);
}

static void progress(FileOpProgressCb cb, void *user, const FileOpStats *stats, const char *entry) {
    if (cb) cb(stats, entry, user);
}

static int make_unique_child(const char *parent, const char *name, char *out, size_t out_size) {
    if (join_path(out, out_size, parent, name) != 0) return -1;
    if (!path_exists(out)) return 0;

    char stem[256];
    char ext[96];
    const char *dot = strrchr(name, '.');
    if (dot && dot != name) {
        size_t stem_len = (size_t)(dot - name);
        if (stem_len >= sizeof(stem)) stem_len = sizeof(stem) - 1;
        memcpy(stem, name, stem_len);
        stem[stem_len] = '\0';
        snprintf(ext, sizeof(ext), "%s", dot);
    } else {
        snprintf(stem, sizeof(stem), "%s", name);
        ext[0] = '\0';
    }

    for (unsigned i = 1; i < 10000; ++i) {
        char candidate[384];
        snprintf(candidate, sizeof(candidate), "%s_%u%s", stem, i, ext);
        if (join_path(out, out_size, parent, candidate) != 0) return -1;
        if (!path_exists(out)) return 0;
    }
    return -1;
}

static int count_path(const char *path, FileOpStats *stats, const FileOpOptions *options) {
    if (should_cancel(options)) return 1;
    struct stat st;
    if (stat(path, &st) != 0) {
        stats_error(stats, "stat failed: %s", path);
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            stats_error(stats, "opendir failed: %s", path);
            return -1;
        }
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child[SWITCH7ZIP_MAX_PATH];
            if (join_path(child, sizeof(child), path, ent->d_name) != 0) continue;
            int rc = count_path(child, stats, options);
            if (rc != 0) { closedir(dir); return rc; }
        }
        closedir(dir);
    } else if (S_ISREG(st.st_mode)) {
        stats->bytes_expected += (uint64_t)st.st_size;
    }
    return 0;
}

static int copy_file(const char *src, const char *dst, FileOpStats *stats, FileOpProgressCb cb, void *user, const FileOpOptions *options) {
    FILE *in = fopen(src, "rb");
    if (!in) { stats_error(stats, "open source failed: %s", src); return -1; }

    char partial_dst[SWITCH7ZIP_MAX_PATH];
    int written = snprintf(partial_dst, sizeof(partial_dst), "%s%s", dst, SWITCH7ZIP_PARTIAL_SUFFIX);
    if (written < 0 || (size_t)written >= sizeof(partial_dst)) {
        fclose(in);
        stats_error(stats, "partial destination path too long: %s", dst);
        return -1;
    }
    remove(partial_dst);

    FILE *out = fopen(partial_dst, "wb");
    if (!out) { fclose(in); stats_error(stats, "open destination failed: %s", partial_dst); return -1; }

    void *buf = malloc(FILEOP_BUFFER_SIZE);
    if (!buf) { fclose(in); fclose(out); stats_error(stats, "out of memory", NULL); return -1; }

    int rc = 0;
    for (;;) {
        if (should_cancel(options)) { rc = 1; break; }
        size_t n = fread(buf, 1, FILEOP_BUFFER_SIZE, in);
        if (n > 0) {
            if (fwrite(buf, 1, n, out) != n) { stats_error(stats, "write failed: %s", partial_dst); rc = -1; break; }
            stats->bytes_done += (uint64_t)n;
            snprintf(stats->last_entry, sizeof(stats->last_entry), "%s", src);
            progress(cb, user, stats, src);
        }
        if (n < FILEOP_BUFFER_SIZE) {
            if (ferror(in)) { stats_error(stats, "read failed: %s", src); rc = -1; }
            break;
        }
    }

    free(buf);
    if (fclose(out) != 0 && rc == 0) { stats_error(stats, "close failed: %s", partial_dst); rc = -1; }
    fclose(in);

    if (rc == 0) {
        remove(dst);
        if (rename(partial_dst, dst) != 0) {
            snprintf(stats->partial_path, sizeof(stats->partial_path), "%s", partial_dst);
            stats->partial_files_left++;
            stats_error(stats, "rename partial failed: %s", partial_dst);
            return -1;
        }
        stats->files_done++;
        stats->entries_done++;
        progress(cb, user, stats, src);
    } else {
        snprintf(stats->partial_path, sizeof(stats->partial_path), "%s", partial_dst);
        stats->partial_files_left++;
    }
    return rc;
}

static int copy_recursive(const char *src, const char *dst, FileOpStats *stats, FileOpProgressCb cb, void *user, const FileOpOptions *options) {
    if (should_cancel(options)) return 1;
    struct stat st;
    if (stat(src, &st) != 0) { stats_error(stats, "stat failed: %s", src); return -1; }

    if (S_ISDIR(st.st_mode)) {
        if (mkdir_p(dst) != 0) { stats_error(stats, "mkdir failed: %s", dst); return -1; }
        stats->dirs_done++;
        stats->entries_done++;
        snprintf(stats->last_entry, sizeof(stats->last_entry), "%s", src);
        progress(cb, user, stats, src);

        DIR *dir = opendir(src);
        if (!dir) { stats_error(stats, "opendir failed: %s", src); return -1; }
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child_src[SWITCH7ZIP_MAX_PATH];
            char child_dst[SWITCH7ZIP_MAX_PATH];
            if (join_path(child_src, sizeof(child_src), src, ent->d_name) != 0 ||
                join_path(child_dst, sizeof(child_dst), dst, ent->d_name) != 0) {
                continue;
            }
            int rc = copy_recursive(child_src, child_dst, stats, cb, user, options);
            if (rc != 0) { closedir(dir); return rc; }
        }
        closedir(dir);
        return 0;
    }

    if (S_ISREG(st.st_mode)) return copy_file(src, dst, stats, cb, user, options);
    stats->failures++;
    return 0;
}

static int remove_recursive(const char *path, FileOpStats *stats, const FileOpOptions *options) {
    if (should_cancel(options)) return 1;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) { stats_error(stats, "opendir failed: %s", path); return -1; }
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child[SWITCH7ZIP_MAX_PATH];
            if (join_path(child, sizeof(child), path, ent->d_name) != 0) continue;
            int rc = remove_recursive(child, stats, options);
            if (rc != 0) { closedir(dir); return rc; }
        }
        closedir(dir);
        if (rmdir(path) != 0) { stats_error(stats, "rmdir failed: %s", path); return -1; }
    } else {
        if (remove(path) != 0) { stats_error(stats, "remove failed: %s", path); return -1; }
    }
    return 0;
}

int fileop_copy_path_ex(const char *source_path,
                        const char *destination_dir,
                        char *actual_destination,
                        size_t actual_destination_size,
                        FileOpStats *stats,
                        FileOpProgressCb progress_cb,
                        void *progress_user_data,
                        const FileOpOptions *options) {
    if (!source_path || !destination_dir || !stats) return -1;
    memset(stats, 0, sizeof(*stats));
    if (mkdir_p(destination_dir) != 0) { stats_error(stats, "mkdir failed: %s", destination_dir); return -1; }
    if (make_unique_child(destination_dir, path_basename(source_path), actual_destination, actual_destination_size) != 0) {
        stats_error(stats, "could not create destination", NULL);
        return -1;
    }
    int rc = count_path(source_path, stats, options);
    if (rc != 0) return rc;
    return copy_recursive(source_path, actual_destination, stats, progress_cb, progress_user_data, options);
}

int fileop_move_path_ex(const char *source_path,
                        const char *destination_dir,
                        char *actual_destination,
                        size_t actual_destination_size,
                        FileOpStats *stats,
                        FileOpProgressCb progress_cb,
                        void *progress_user_data,
                        const FileOpOptions *options) {
    if (!source_path || !destination_dir || !stats) return -1;
    memset(stats, 0, sizeof(*stats));
    if (mkdir_p(destination_dir) != 0) { stats_error(stats, "mkdir failed: %s", destination_dir); return -1; }
    if (make_unique_child(destination_dir, path_basename(source_path), actual_destination, actual_destination_size) != 0) {
        stats_error(stats, "could not create destination", NULL);
        return -1;
    }
    if (rename(source_path, actual_destination) == 0) {
        stats->entries_done = 1;
        if (is_dir_path(actual_destination)) stats->dirs_done = 1;
        else {
            struct stat st;
            if (stat(actual_destination, &st) == 0) stats->bytes_done = stats->bytes_expected = (uint64_t)st.st_size;
            stats->files_done = 1;
        }
        snprintf(stats->last_entry, sizeof(stats->last_entry), "%s", source_path);
        progress(progress_cb, progress_user_data, stats, source_path);
        return 0;
    }

    int rc = count_path(source_path, stats, options);
    if (rc != 0) return rc;
    rc = copy_recursive(source_path, actual_destination, stats, progress_cb, progress_user_data, options);
    if (rc != 0) return rc;
    return remove_recursive(source_path, stats, options);
}

int fileop_trash_path_ex(const char *source_path,
                         const char *trash_dir,
                         char *actual_destination,
                         size_t actual_destination_size,
                         FileOpStats *stats,
                         FileOpProgressCb progress_cb,
                         void *progress_user_data,
                         const FileOpOptions *options) {
    return fileop_move_path_ex(source_path, trash_dir, actual_destination, actual_destination_size, stats, progress_cb, progress_user_data, options);
}

int fileop_delete_path_ex(const char *source_path,
                          FileOpStats *stats,
                          FileOpProgressCb progress_cb,
                          void *progress_user_data,
                          const FileOpOptions *options) {
    if (!source_path || !stats) return -1;
    memset(stats, 0, sizeof(*stats));
    int rc = count_path(source_path, stats, options);
    if (rc != 0) return rc;
    snprintf(stats->last_entry, sizeof(stats->last_entry), "%s", source_path);
    progress(progress_cb, progress_user_data, stats, source_path);
    rc = remove_recursive(source_path, stats, options);
    if (rc == 0) {
        stats->entries_done++;
        progress(progress_cb, progress_user_data, stats, source_path);
    }
    return rc;
}

static bool name_has_suffix(const char *name, const char *suffix) {
    if (!name || !suffix) return false;
    size_t n = strlen(name), s = strlen(suffix);
    return n >= s && strcmp(name + n - s, suffix) == 0;
}

static int cleanup_partials_recursive(const char *path, FileOpStats *stats, FileOpProgressCb cb, void *user, const FileOpOptions *options) {
    if (should_cancel(options)) return 1;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) { stats_error(stats, "opendir failed: %s", path); return -1; }
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child[SWITCH7ZIP_MAX_PATH];
            if (join_path(child, sizeof(child), path, ent->d_name) != 0) continue;
            int rc = cleanup_partials_recursive(child, stats, cb, user, options);
            if (rc != 0) { closedir(dir); return rc; }
        }
        closedir(dir);
    } else if (S_ISREG(st.st_mode) && name_has_suffix(path, SWITCH7ZIP_PARTIAL_SUFFIX)) {
        stats->bytes_done += (uint64_t)st.st_size;
        stats->files_done++;
        stats->entries_done++;
        snprintf(stats->last_entry, sizeof(stats->last_entry), "%s", path);
        progress(cb, user, stats, path);
        if (remove(path) != 0) { stats_error(stats, "remove partial failed: %s", path); return -1; }
    }
    return 0;
}

int fileop_cleanup_partials_ex(const char *root_path,
                               FileOpStats *stats,
                               FileOpProgressCb progress_cb,
                               void *progress_user_data,
                               const FileOpOptions *options) {
    if (!root_path || !stats) return -1;
    memset(stats, 0, sizeof(*stats));
    return cleanup_partials_recursive(root_path, stats, progress_cb, progress_user_data, options);
}

int fileop_create_unique_folder(const char *parent_dir,
                                const char *preferred_name,
                                char *actual_path,
                                size_t actual_path_size) {
    if (!parent_dir || !preferred_name || !actual_path) return -1;
    char sanitized[256];
    size_t j = 0;
    for (size_t i = 0; preferred_name[i] && j + 1 < sizeof(sanitized); ++i) {
        char c = preferred_name[i];
        if (c == '/' || c == '\\' || c == ':' || (unsigned char)c < 32) c = '_';
        sanitized[j++] = c;
    }
    sanitized[j] = '\0';
    if (!sanitized[0]) snprintf(sanitized, sizeof(sanitized), "New Folder");
    if (make_unique_child(parent_dir, sanitized, actual_path, actual_path_size) != 0) return -1;
    return mkdir_p(actual_path);
}

int fileop_rename_path(const char *source_path,
                       const char *new_name,
                       char *actual_path,
                       size_t actual_path_size) {
    if (!source_path || !new_name || !actual_path) return -1;
    char parent[SWITCH7ZIP_MAX_PATH];
    snprintf(parent, sizeof(parent), "%s", source_path);
    char *slash = strrchr(parent, '/');
    if (!slash) return -1;
    *slash = '\0';
    if (strchr(new_name, '/') || strchr(new_name, '\\') || strchr(new_name, ':')) return -1;
    if (join_path(actual_path, actual_path_size, parent, new_name) != 0) return -1;
    if (path_exists(actual_path)) return -2;
    return rename(source_path, actual_path);
}
