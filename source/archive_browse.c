/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#include "archive_browse.h"

#include <archive.h>
#include <archive_entry.h>

#include <stdio.h>
#include <string.h>

static void copy_truncated(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    size_t n = strnlen(src, dst_size - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int append_char(char *dst, size_t dst_size, char ch) {
    if (!dst || dst_size == 0) return -1;
    size_t n = strnlen(dst, dst_size);
    if (n + 1 >= dst_size) return -1;
    dst[n] = ch;
    dst[n + 1] = '\0';
    return 0;
}


static void set_error(char *error, size_t error_size, const char *context, const char *detail) {
    if (!error || error_size == 0) return;
    if (detail && *detail) snprintf(error, error_size, "%s: %s", context, detail);
    else snprintf(error, error_size, "%s", context ? context : "unknown error");
}

bool archive_browse_path_is_dir(const char *path) {
    size_t len = path ? strlen(path) : 0;
    return len > 0 && path[len - 1] == '/';
}

static const char *entry_path_utf8_or_native(struct archive_entry *entry) {
    const char *path = archive_entry_pathname_utf8(entry);
    if (path && *path) return path;
    return archive_entry_pathname(entry);
}

static int normalize_archive_path(const char *in, char *out, size_t out_size) {
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

static bool starts_with_prefix(const char *path, const char *prefix) {
    if (!prefix || !*prefix) return true;
    size_t n = strlen(prefix);
    return strncmp(path, prefix, n) == 0;
}

static int find_existing(const ArchiveBrowseItem *items, size_t count, const char *name) {
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(items[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int add_or_update_item(ArchiveBrowseItem *items,
                              size_t max_items,
                              size_t *count,
                              const char *prefix,
                              const char *name,
                              ArchiveBrowseType type,
                              uint64_t size) {
    if (!items || !count || !name || !*name) return 0;
    int existing = find_existing(items, *count, name);
    if (existing >= 0) {
        if (items[existing].type == ARCHIVE_BROWSE_FILE && type == ARCHIVE_BROWSE_DIR) {
            items[existing].type = ARCHIVE_BROWSE_DIR;
        }
        if (size > items[existing].size) items[existing].size = size;
        return 0;
    }
    if (*count >= max_items) return 1;
    ArchiveBrowseItem *item = &items[(*count)++];
    memset(item, 0, sizeof(*item));
    copy_truncated(item->name, sizeof(item->name), name);
    if (type == ARCHIVE_BROWSE_DIR) {
        const char *pfx = prefix && *prefix ? prefix : "";
        int n = snprintf(item->path, sizeof(item->path), "%s%s/", pfx, item->name);
        if (n < 0 || (size_t)n >= sizeof(item->path)) return 1;
    } else {
        const char *pfx = prefix && *prefix ? prefix : "";
        int n = snprintf(item->path, sizeof(item->path), "%s%s", pfx, item->name);
        if (n < 0 || (size_t)n >= sizeof(item->path)) return 1;
    }
    item->type = type;
    item->size = size;
    return 0;
}

int archive_browse_list_dir(const char *archive_path,
                            const char *prefix,
                            ArchiveBrowseItem *items,
                            size_t max_items,
                            size_t *out_count,
                            char *error,
                            size_t error_size) {
    if (out_count) *out_count = 0;
    if (!archive_path || !*archive_path || !items || !out_count) {
        set_error(error, error_size, "Invalid archive preview request", NULL);
        return -1;
    }

    char clean_prefix[SWITCH7ZIP_MAX_PATH];
    clean_prefix[0] = '\0';
    if (prefix && *prefix) {
        char tmp[SWITCH7ZIP_MAX_PATH];
        int rc = normalize_archive_path(prefix, tmp, sizeof(tmp));
        if (rc != 0) {
            set_error(error, error_size, "Invalid archive folder path", prefix);
            return -1;
        }
        copy_truncated(clean_prefix, sizeof(clean_prefix), tmp);
        if (!archive_browse_path_is_dir(clean_prefix) && append_char(clean_prefix, sizeof(clean_prefix), '/') != 0) {
            set_error(error, error_size, "Archive folder path is too long", prefix);
            return -1;
        }
    }

    struct archive *reader = archive_read_new();
    if (!reader) {
        set_error(error, error_size, "Could not allocate archive reader", NULL);
        return -1;
    }
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    int r = archive_read_open_filename(reader, archive_path, 1024 * 1024);
    if (r != ARCHIVE_OK) {
        set_error(error, error_size, "Could not open archive", archive_error_string(reader));
        archive_read_free(reader);
        return -1;
    }

    struct archive_entry *entry = NULL;
    size_t count = 0;
    while ((r = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        const char *raw = entry_path_utf8_or_native(entry);
        char clean[SWITCH7ZIP_MAX_PATH];
        if (normalize_archive_path(raw, clean, sizeof(clean)) != 0) {
            archive_read_data_skip(reader);
            continue;
        }
        bool raw_dir = archive_entry_filetype(entry) == AE_IFDIR || archive_browse_path_is_dir(raw);
        if (raw_dir && !archive_browse_path_is_dir(clean)) {
            size_t len = strlen(clean);
            if (len + 1 < sizeof(clean)) {
                clean[len] = '/';
                clean[len + 1] = '\0';
            }
        }
        if (!starts_with_prefix(clean, clean_prefix)) {
            archive_read_data_skip(reader);
            continue;
        }
        const char *rest = clean + strlen(clean_prefix);
        if (!*rest) {
            archive_read_data_skip(reader);
            continue;
        }
        char name[256];
        const char *slash = strchr(rest, '/');
        ArchiveBrowseType type = slash ? ARCHIVE_BROWSE_DIR : ARCHIVE_BROWSE_FILE;
        size_t name_len = slash ? (size_t)(slash - rest) : strlen(rest);
        if (name_len == 0) {
            archive_read_data_skip(reader);
            continue;
        }
        if (name_len >= sizeof(name)) name_len = sizeof(name) - 1;
        memcpy(name, rest, name_len);
        name[name_len] = '\0';
        uint64_t size = 0;
        if (type == ARCHIVE_BROWSE_FILE && archive_entry_size_is_set(entry) && archive_entry_size(entry) > 0) {
            size = (uint64_t)archive_entry_size(entry);
        }
        add_or_update_item(items, max_items, &count, clean_prefix, name, type, size);
        archive_read_data_skip(reader);
    }

    if (r != ARCHIVE_EOF) {
        set_error(error, error_size, "Archive preview failed", archive_error_string(reader));
        archive_read_free(reader);
        return -1;
    }
    archive_read_close(reader);
    archive_read_free(reader);
    *out_count = count;
    return 0;
}
