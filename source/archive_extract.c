/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#include "archive_extract.h"
#include "fs_utils.h"

#include <archive.h>
#include <archive_entry.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define COPY_BUFFER_SIZE (1024 * 1024)
#define PROGRESS_REPORT_INTERVAL (32ULL * 1024ULL * 1024ULL)

static void set_error(ExtractStats *stats, const char *context, const char *detail) {
    if (!stats) return;
    if (detail && *detail) {
        snprintf(stats->error_message, sizeof(stats->error_message), "%s: %s", context, detail);
    } else {
        snprintf(stats->error_message, sizeof(stats->error_message), "%s", context);
    }
}

static void set_errno_error(ExtractStats *stats, const char *context, const char *path) {
    if (!stats) return;
    if (path && *path) {
        snprintf(stats->error_message, sizeof(stats->error_message), "%s: %s (%s)", context, path, strerror(errno));
    } else {
        snprintf(stats->error_message, sizeof(stats->error_message), "%s: %s", context, strerror(errno));
    }
}

static int append_path_component(char *out, size_t out_size, const char *component, size_t component_len) {
    size_t current = strlen(out);
    if (current + 1 + component_len >= out_size) return -1;
    if (current > 0 && out[current - 1] != '/') {
        out[current++] = '/';
        out[current] = '\0';
    }
    memcpy(out + current, component, component_len);
    out[current + component_len] = '\0';
    return 0;
}

static int join_safe_archive_path(const char *base_dir,
                                  const char *archive_path,
                                  char *out,
                                  size_t out_size) {
    if (!base_dir || !archive_path || !out || out_size == 0) return -1;
    if (archive_path[0] == '\0') return -1;

    /* Reject absolute and drive/device-like paths before normalization. */
    if (archive_path[0] == '/' || archive_path[0] == '\\') return -2;
    if (strchr(archive_path, ':')) return -2;

    int written = snprintf(out, out_size, "%s", base_dir);
    if (written < 0 || (size_t)written >= out_size) return -1;

    const char *cursor = archive_path;
    int emitted_component = 0;

    while (*cursor) {
        while (*cursor == '/' || *cursor == '\\') cursor++;
        if (!*cursor) break;

        const char *start = cursor;
        while (*cursor && *cursor != '/' && *cursor != '\\') cursor++;
        size_t len = (size_t)(cursor - start);

        if (len == 0) continue;
        if (len == 1 && start[0] == '.') continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') return -2;

        if (append_path_component(out, out_size, start, len) != 0) return -1;
        emitted_component = 1;
    }

    return emitted_component ? 0 : -1;
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

    if (parent[0] == '\0') return 0;
    return mkdir_p(parent);
}


static int normalize_archive_member_path(const char *in, char *out, size_t out_size) {
    if (!in || !*in || !out || out_size == 0) return -1;
    if (in[0] == '/' || in[0] == '\\') return -2;
    if (strchr(in, ':')) return -2;

    out[0] = '\0';
    size_t out_len = 0;
    const char *cursor = in;
    while (*cursor) {
        while (*cursor == '/' || *cursor == '\\') cursor++;
        if (!*cursor) break;
        const char *start = cursor;
        while (*cursor && *cursor != '/' && *cursor != '\\') cursor++;
        size_t len = (size_t)(cursor - start);
        if (len == 0) continue;
        if (len == 1 && start[0] == '.') continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') return -2;
        if (out_len && out_len + 1 < out_size) out[out_len++] = '/';
        if (out_len + len >= out_size) return -1;
        memcpy(out + out_len, start, len);
        out_len += len;
        out[out_len] = '\0';
    }
    return out_len > 0 ? 0 : -1;
}

static void normalize_selector_path(const char *in, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!in || !*in) return;
    char tmp[SWITCH7ZIP_MAX_PATH];
    if (normalize_archive_member_path(in, tmp, sizeof(tmp)) != 0) return;
    snprintf(out, out_size, "%s", tmp);
    size_t in_len = strlen(in);
    size_t out_len = strlen(out);
    if (in_len > 0 && (in[in_len - 1] == '/' || in[in_len - 1] == '\\') && out_len + 1 < out_size) {
        out[out_len] = '/';
        out[out_len + 1] = '\0';
    }
}

static bool selector_matches_path(const char *selector, const char *clean_path, bool is_dir) {
    if (!selector || !*selector) return true;
    size_t selector_len = strlen(selector);
    bool selector_is_dir = selector_len > 0 && selector[selector_len - 1] == '/';
    if (selector_is_dir) {
        return strncmp(clean_path, selector, selector_len) == 0 || (is_dir && strncmp(clean_path, selector, selector_len - 1) == 0 && clean_path[selector_len - 1] == '\0');
    }
    return strcmp(clean_path, selector) == 0;
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

static bool cancel_requested(const ExtractOptions *options) {
    return options && options->cancel_cb && options->cancel_cb(options->cancel_user_data);
}


static Fat32OversizeMode effective_fat32_mode(const ExtractOptions *options) {
    if (!options || !options->fat32_guard_enabled) return FAT32_OVERSIZE_BLOCK;
    if (options->fat32_oversize_mode == FAT32_OVERSIZE_SPLIT_PARTS ||
        options->fat32_oversize_mode == FAT32_OVERSIZE_SWITCH_CONCAT) {
        return options->fat32_oversize_mode;
    }
    return FAT32_OVERSIZE_BLOCK;
}

static int remove_tree_recursive(const char *path) {
    struct stat st;
    if (!path || !*path || stat(path, &st) != 0) return 0;

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) return -1;
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child[SWITCH7ZIP_MAX_PATH];
            int written = snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            if (written < 0 || (size_t)written >= sizeof(child)) {
                closedir(dir);
                return -1;
            }
            if (remove_tree_recursive(child) != 0) {
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);
        return rmdir(path);
    }

    return remove(path);
}

static int make_fat32_chunk_path(const char *dir,
                                 Fat32OversizeMode mode,
                                 unsigned part_index,
                                 char *out,
                                 size_t out_size) {
    if (!dir || !out || out_size == 0) return -1;
    int written;
    if (mode == FAT32_OVERSIZE_SWITCH_CONCAT) {
        written = snprintf(out, out_size, "%s/%02u", dir, part_index);
    } else {
        written = snprintf(out, out_size, "%s/%04u.part", dir, part_index);
    }
    return (written < 0 || (size_t)written >= out_size) ? -1 : 0;
}

static int set_switch_concatenation_attribute(const char *path, ExtractStats *stats) {
    if (!path || !*path) return -1;
#ifdef __SWITCH__
    Result rc = fsdevSetConcatenationFileAttribute(path);
    if (R_SUCCEEDED(rc)) return 0;
    if (stats) stats->concat_attribute_failures++;
    return -1;
#else
    (void)path;
    if (stats) stats->concat_attribute_failures++;
    return -1;
#endif
}

static int copy_archive_data_to_fat32_chunks(struct archive *reader,
                                             const char *final_dir,
                                             Fat32OversizeMode mode,
                                             ExtractStats *stats,
                                             ExtractProgressCallback progress_cb,
                                             void *user_data,
                                             const char *entry_name,
                                             const ExtractOptions *options) {
    if (!reader || !final_dir || !entry_name) return ARCHIVE_FATAL;

    char partial_dir[SWITCH7ZIP_MAX_PATH];
    int written = snprintf(partial_dir, sizeof(partial_dir), "%s%s", final_dir, SWITCH7ZIP_PARTIAL_SUFFIX);
    if (written < 0 || (size_t)written >= sizeof(partial_dir)) {
        set_error(stats, "Chunk directory path too long", final_dir);
        return ARCHIVE_FATAL;
    }

    remove_tree_recursive(partial_dir);
    if (mkdir_p(partial_dir) != 0) {
        set_errno_error(stats, "Could not create chunk directory", partial_dir);
        return ARCHIVE_FATAL;
    }

    char *buffer = (char *)malloc(COPY_BUFFER_SIZE);
    if (!buffer) {
        set_error(stats, "Out of memory allocating extraction buffer", NULL);
        return ARCHIVE_FATAL;
    }

    uint64_t last_report = stats ? stats->bytes_written : 0;
    uint64_t chunk_written = 0;
    unsigned part_index = 0;
    FILE *out = NULL;
    int rc = ARCHIVE_OK;
    char chunk_path[SWITCH7ZIP_MAX_PATH];

    for (;;) {
        if (!out) {
            if (make_fat32_chunk_path(partial_dir, mode, part_index, chunk_path, sizeof(chunk_path)) != 0) {
                set_error(stats, "Chunk path too long", partial_dir);
                rc = ARCHIVE_FATAL;
                break;
            }
            out = fopen(chunk_path, "wb");
            if (!out) {
                set_errno_error(stats, "Could not create chunk file", chunk_path);
                rc = ARCHIVE_FATAL;
                break;
            }
            chunk_written = 0;
        }

        if (cancel_requested(options)) {
            set_error(stats, "Operation cancelled by user", NULL);
            rc = ARCHIVE_FATAL;
            break;
        }

        la_ssize_t n = archive_read_data(reader, buffer, COPY_BUFFER_SIZE);
        update_archive_position(reader, stats);
        if (n == 0) {
            if (progress_cb) progress_cb(stats, entry_name, user_data);
            rc = ARCHIVE_OK;
            break;
        }
        if (n < 0) {
            rc = (int)n;
            break;
        }

        size_t offset = 0;
        size_t available = (size_t)n;
        while (available > 0) {
            if (!out) {
                if (make_fat32_chunk_path(partial_dir, mode, part_index, chunk_path, sizeof(chunk_path)) != 0) {
                    set_error(stats, "Chunk path too long", partial_dir);
                    rc = ARCHIVE_FATAL;
                    break;
                }
                out = fopen(chunk_path, "wb");
                if (!out) {
                    set_errno_error(stats, "Could not create chunk file", chunk_path);
                    rc = ARCHIVE_FATAL;
                    break;
                }
                chunk_written = 0;
            }

            if (chunk_written >= SWITCH7ZIP_FAT32_CHUNK_BYTES) {
                if (fclose(out) != 0) {
                    out = NULL;
                    set_errno_error(stats, "Could not close chunk file", chunk_path);
                    rc = ARCHIVE_FATAL;
                    break;
                }
                out = NULL;
                if (stats) stats->split_parts_written++;
                part_index++;
                continue;
            }

            uint64_t room64 = SWITCH7ZIP_FAT32_CHUNK_BYTES - chunk_written;
            size_t to_write = available;
            if (room64 < (uint64_t)to_write) to_write = (size_t)room64;

            if (to_write > 0 && fwrite(buffer + offset, 1, to_write, out) != to_write) {
                set_errno_error(stats, "Could not write chunk data", chunk_path);
                rc = ARCHIVE_FATAL;
                break;
            }

            offset += to_write;
            available -= to_write;
            chunk_written += to_write;
            if (stats) {
                stats->bytes_written += (uint64_t)to_write;
                stats->current_entry_written += (uint64_t)to_write;
            }

            if (chunk_written >= SWITCH7ZIP_FAT32_CHUNK_BYTES && available > 0) {
                if (fclose(out) != 0) {
                    out = NULL;
                    set_errno_error(stats, "Could not close chunk file", chunk_path);
                    rc = ARCHIVE_FATAL;
                    break;
                }
                out = NULL;
                if (stats) stats->split_parts_written++;
                part_index++;
                continue;
            }

            if (stats && progress_cb && stats->bytes_written - last_report >= PROGRESS_REPORT_INTERVAL) {
                last_report = stats->bytes_written;
                update_archive_position(reader, stats);
                progress_cb(stats, entry_name, user_data);
                if (cancel_requested(options)) {
                    set_error(stats, "Operation cancelled by user", NULL);
                    rc = ARCHIVE_FATAL;
                    break;
                }
            }
        }
        if (rc != ARCHIVE_OK) break;
    }

    if (out) {
        if (fclose(out) != 0 && rc == ARCHIVE_OK) {
            set_errno_error(stats, "Could not close chunk file", chunk_path);
            rc = ARCHIVE_FATAL;
        }
        out = NULL;
        if (rc == ARCHIVE_OK && stats) stats->split_parts_written++;
    }

    free(buffer);

    if (rc != ARCHIVE_OK) {
        if (stats) {
            stats->partial_files_left++;
            snprintf(stats->partial_path, sizeof(stats->partial_path), "%s", partial_dir);
        }
        return rc;
    }

    remove_tree_recursive(final_dir);
    if (rename(partial_dir, final_dir) != 0) {
        if (stats) {
            stats->partial_files_left++;
            snprintf(stats->partial_path, sizeof(stats->partial_path), "%s", partial_dir);
        }
        set_errno_error(stats, "Could not finalize chunk directory", final_dir);
        return ARCHIVE_FATAL;
    }

    if (mode == FAT32_OVERSIZE_SWITCH_CONCAT && set_switch_concatenation_attribute(final_dir, stats) != 0) {
        set_error(stats, "Could not set Switch concatenation attribute", final_dir);
        return ARCHIVE_FATAL;
    }

    return ARCHIVE_OK;
}

static int copy_archive_data_to_file(struct archive *reader,
                                     FILE *out,
                                     ExtractStats *stats,
                                     ExtractProgressCallback progress_cb,
                                     void *user_data,
                                     const char *entry_name,
                                     const ExtractOptions *options) {
    char *buffer = (char *)malloc(COPY_BUFFER_SIZE);
    if (!buffer) {
        set_error(stats, "Out of memory allocating extraction buffer", NULL);
        return ARCHIVE_FATAL;
    }

    uint64_t last_report = stats ? stats->bytes_written : 0;
    int rc = ARCHIVE_OK;

    for (;;) {
        if (cancel_requested(options)) {
            set_error(stats, "Operation cancelled by user", NULL);
            rc = ARCHIVE_FATAL;
            break;
        }

        la_ssize_t n = archive_read_data(reader, buffer, COPY_BUFFER_SIZE);
        update_archive_position(reader, stats);
        if (n == 0) {
            if (progress_cb) progress_cb(stats, entry_name, user_data);
            rc = ARCHIVE_OK;
            break;
        }
        if (n < 0) {
            rc = (int)n;
            break;
        }

        if (fwrite(buffer, 1, (size_t)n, out) != (size_t)n) {
            set_errno_error(stats, "Could not write output data", entry_name);
            rc = ARCHIVE_FATAL;
            break;
        }

        if (stats) {
            stats->bytes_written += (uint64_t)n;
            stats->current_entry_written += (uint64_t)n;
            if (progress_cb && stats->bytes_written - last_report >= PROGRESS_REPORT_INTERVAL) {
                last_report = stats->bytes_written;
                update_archive_position(reader, stats);
                progress_cb(stats, entry_name, user_data);
                if (cancel_requested(options)) {
                    set_error(stats, "Operation cancelled by user", NULL);
                    rc = ARCHIVE_FATAL;
                    break;
                }
            }
        }
    }

    free(buffer);
    return rc;
}

int extract_archive_selection_to_dir_ex(const char *archive_path,
                                        const char *selected_archive_path,
                                        const char *output_dir,
                              ExtractStats *stats,
                              ExtractProgressCallback progress_cb,
                              void *user_data,
                              const ExtractOptions *options) {
    if (stats) memset(stats, 0, sizeof(*stats));

    if (!archive_path || !*archive_path || !output_dir || !*output_dir) {
        set_error(stats, "Invalid archive or output path", NULL);
        return -1;
    }

    char selector[SWITCH7ZIP_MAX_PATH];
    normalize_selector_path(selected_archive_path, selector, sizeof(selector));

    if (stats) {
        struct stat archive_st;
        if (stat(archive_path, &archive_st) == 0 && archive_st.st_size > 0) {
            stats->archive_bytes_total = (uint64_t)archive_st.st_size;
            stats->bytes_expected = stats->archive_bytes_total;
        }
    }

    if (mkdir_p(output_dir) != 0) {
        set_errno_error(stats, "Could not create output directory", output_dir);
        return -1;
    }

    struct archive *reader = archive_read_new();
    if (!reader) {
        set_error(stats, "Could not allocate libarchive reader", NULL);
        return -1;
    }

    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    int r = archive_read_open_filename(reader, archive_path, 1024 * 1024);
    if (r != ARCHIVE_OK) {
        set_error(stats, "Could not open archive", archive_error_string(reader));
        archive_read_free(reader);
        return -1;
    }

    struct archive_entry *entry = NULL;
    while ((r = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        if (cancel_requested(options)) {
            set_error(stats, "Operation cancelled by user", NULL);
            archive_read_free(reader);
            return -1;
        }
        update_archive_position(reader, stats);
        const char *raw_path = entry_path_utf8_or_native(entry);
        if (!raw_path || !*raw_path) {
            archive_read_data_skip(reader);
            continue;
        }

        char clean_path[SWITCH7ZIP_MAX_PATH];
        int clean_rc = normalize_archive_member_path(raw_path, clean_path, sizeof(clean_path));
        if (clean_rc != 0) {
            if (stats) stats->unsafe_paths_skipped++;
            archive_read_data_skip(reader);
            continue;
        }

        bool is_dir_entry = archive_entry_filetype(entry) == AE_IFDIR;
        if (!selector_matches_path(selector, clean_path, is_dir_entry)) {
            archive_read_data_skip(reader);
            continue;
        }

        if (stats) {
            stats->entries_seen++;
            stats->current_entry_written = 0;
            stats->current_entry_size = archive_entry_size_is_set(entry) && archive_entry_size(entry) > 0
                                          ? (uint64_t)archive_entry_size(entry)
                                          : 0;
            snprintf(stats->last_entry, sizeof(stats->last_entry), "%s", clean_path);
        }
        if (progress_cb) progress_cb(stats, clean_path, user_data);
        if (cancel_requested(options)) {
            set_error(stats, "Operation cancelled by user", NULL);
            archive_read_free(reader);
            return -1;
        }

        char safe_path[SWITCH7ZIP_MAX_PATH];
        int path_rc = join_safe_archive_path(output_dir, clean_path, safe_path, sizeof(safe_path));
        if (path_rc != 0) {
            if (stats) stats->unsafe_paths_skipped++;
            archive_read_data_skip(reader);
            if (progress_cb) progress_cb(stats, clean_path, user_data);
            continue;
        }

        mode_t filetype = archive_entry_filetype(entry);
        if (filetype == AE_IFDIR) {
            if (mkdir_p(safe_path) != 0) {
                set_errno_error(stats, "Could not create directory", safe_path);
                archive_read_free(reader);
                return -1;
            }
            if (stats) stats->dirs_created++;
            archive_read_data_skip(reader);
            if (progress_cb) progress_cb(stats, clean_path, user_data);
            continue;
        }

        if (filetype != AE_IFREG || archive_entry_symlink(entry) || archive_entry_hardlink(entry)) {
            if (stats) stats->unsupported_entries_skipped++;
            archive_read_data_skip(reader);
            if (progress_cb) progress_cb(stats, clean_path, user_data);
            continue;
        }

        bool is_fat32_oversized = archive_entry_size_is_set(entry) && archive_entry_size(entry) > 0 &&
                                   (uint64_t)archive_entry_size(entry) > SWITCH7ZIP_FAT32_MAX_FILE_BYTES;
        Fat32OversizeMode fat32_mode = effective_fat32_mode(options);

        if (options && options->fat32_guard_enabled && is_fat32_oversized && fat32_mode == FAT32_OVERSIZE_BLOCK) {
            if (stats) {
                stats->fat32_oversize_blocked++;
                snprintf(stats->last_entry, sizeof(stats->last_entry), "%s", clean_path);
            }
            set_error(stats, "FAT32 guard blocked a file larger than 4 GiB", clean_path);
            archive_read_data_skip(reader);
            archive_read_free(reader);
            return -1;
        }

        if (ensure_parent_dir(safe_path) != 0) {
            set_errno_error(stats, "Could not create parent directory", safe_path);
            archive_read_free(reader);
            return -1;
        }

        if (is_fat32_oversized &&
            (fat32_mode == FAT32_OVERSIZE_SPLIT_PARTS || fat32_mode == FAT32_OVERSIZE_SWITCH_CONCAT)) {
            char chunk_dir[SWITCH7ZIP_MAX_PATH];
            int chunk_dir_written;
            if (fat32_mode == FAT32_OVERSIZE_SPLIT_PARTS) {
                chunk_dir_written = snprintf(chunk_dir, sizeof(chunk_dir), "%s%s", safe_path, SWITCH7ZIP_SPLIT_SUFFIX);
            } else {
                chunk_dir_written = snprintf(chunk_dir, sizeof(chunk_dir), "%s", safe_path);
            }
            if (chunk_dir_written < 0 || (size_t)chunk_dir_written >= sizeof(chunk_dir)) {
                set_error(stats, "Chunk output path too long", safe_path);
                archive_read_free(reader);
                return -1;
            }

            struct stat chunk_existing;
            if (stat(chunk_dir, &chunk_existing) == 0) {
                if (!options || !options->overwrite_existing) {
                    if (stats) stats->existing_files_skipped++;
                    archive_read_data_skip(reader);
                    if (progress_cb) progress_cb(stats, clean_path, user_data);
                    continue;
                }
                if (remove_tree_recursive(chunk_dir) != 0) {
                    set_errno_error(stats, "Could not remove existing chunk output", chunk_dir);
                    archive_read_free(reader);
                    return -1;
                }
            }

            r = copy_archive_data_to_fat32_chunks(reader, chunk_dir, fat32_mode, stats, progress_cb, user_data, clean_path, options);
            if (r != ARCHIVE_OK) {
                if (!stats || stats->error_message[0] == 0) {
                    const char *archive_err = archive_error_string(reader);
                    if (archive_err && *archive_err) set_error(stats, "Could not extract oversized file", archive_err);
                    else set_error(stats, "Could not extract oversized file", clean_path);
                }
                archive_read_free(reader);
                return -1;
            }
            if (stats) {
                stats->files_written++;
                if (fat32_mode == FAT32_OVERSIZE_SPLIT_PARTS) stats->fat32_oversize_split++;
                else stats->fat32_oversize_concat++;
            }
            if (progress_cb) progress_cb(stats, clean_path, user_data);
            continue;
        }

        if (!options || !options->overwrite_existing) {
            struct stat existing_st;
            if (stat(safe_path, &existing_st) == 0) {
                if (stats) stats->existing_files_skipped++;
                archive_read_data_skip(reader);
                if (progress_cb) progress_cb(stats, clean_path, user_data);
                continue;
            }
        }

        char partial_path[SWITCH7ZIP_MAX_PATH];
        int partial_written = snprintf(partial_path, sizeof(partial_path), "%s%s", safe_path, SWITCH7ZIP_PARTIAL_SUFFIX);
        if (partial_written < 0 || (size_t)partial_written >= sizeof(partial_path)) {
            set_error(stats, "Partial output path too long", safe_path);
            archive_read_free(reader);
            return -1;
        }
        remove(partial_path);

        FILE *out = fopen(partial_path, "wb");
        if (!out) {
            set_errno_error(stats, "Could not create partial output file", partial_path);
            archive_read_free(reader);
            return -1;
        }

        r = copy_archive_data_to_file(reader, out, stats, progress_cb, user_data, clean_path, options);
        if (fclose(out) != 0 && r == ARCHIVE_OK) {
            r = ARCHIVE_FATAL;
        }

        if (r != ARCHIVE_OK) {
            if (stats) {
                stats->partial_files_left++;
                snprintf(stats->partial_path, sizeof(stats->partial_path), "%s", partial_path);
            }
            if (!stats || stats->error_message[0] == 0) {
                const char *archive_err = archive_error_string(reader);
                if (archive_err && *archive_err) {
                    set_error(stats, "Could not extract file data", archive_err);
                } else {
                    set_errno_error(stats, "Could not write partial output file", partial_path);
                }
            }
            archive_read_free(reader);
            return -1;
        }

        if (options && options->overwrite_existing) remove(safe_path);
        if (rename(partial_path, safe_path) != 0) {
            if (stats) {
                stats->partial_files_left++;
                snprintf(stats->partial_path, sizeof(stats->partial_path), "%s", partial_path);
            }
            set_errno_error(stats, "Could not finalize partial output file", safe_path);
            archive_read_free(reader);
            return -1;
        }

        if (stats) stats->files_written++;
        if (progress_cb) progress_cb(stats, clean_path, user_data);
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


int extract_archive_to_dir_ex(const char *archive_path,
                              const char *output_dir,
                              ExtractStats *stats,
                              ExtractProgressCallback progress_cb,
                              void *user_data,
                              const ExtractOptions *options) {
    return extract_archive_selection_to_dir_ex(archive_path, NULL, output_dir, stats, progress_cb, user_data, options);
}


int extract_archive_to_dir(const char *archive_path,
                           const char *output_dir,
                           ExtractStats *stats,
                           ExtractProgressCallback progress_cb,
                           void *user_data) {
    ExtractOptions options;
    memset(&options, 0, sizeof(options));
    options.overwrite_existing = true;
    options.fat32_guard_enabled = true;
    options.fat32_oversize_mode = FAT32_OVERSIZE_BLOCK;
    return extract_archive_to_dir_ex(archive_path, output_dir, stats, progress_cb, user_data, &options);
}
