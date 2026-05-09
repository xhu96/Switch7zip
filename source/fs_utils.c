/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#include "fs_utils.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int ends_with_case_insensitive(const char *value, const char *suffix) {
    if (!value || !suffix) return 0;

    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > value_len) return 0;

    const char *start = value + value_len - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        unsigned char a = (unsigned char)start[i];
        unsigned char b = (unsigned char)suffix[i];
        if (tolower(a) != tolower(b)) return 0;
    }

    return 1;
}

int has_supported_archive_extension(const char *name) {
    if (!name || !*name) return 0;

    /* Formats are limited by the linked libarchive build. */
    static const char *const exts[] = {
        ".7z", ".7z.001",
        ".zip", ".zipx", ".zip.001", ".jar", ".apk", ".cbz",
        ".rar", ".cbr",
        ".tar", ".tgz", ".tar.gz", ".gz",
        ".txz", ".tar.xz", ".xz", ".tbz", ".tbz2", ".tar.bz2",
        ".bz2", ".tzst", ".tar.zst", ".zst"
    };

    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
        if (ends_with_case_insensitive(name, exts[i])) return 1;
    }

    return 0;
}

int mkdir_p(const char *path) {
    if (!path || !*path) return -1;

    char tmp[SWITCH7ZIP_MAX_PATH];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;

    memcpy(tmp, path, len + 1);

    if (len > 1 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    char *start = tmp;
    char *colon = strchr(tmp, ':');
    if (colon && colon[1] == '/') {
        start = colon + 2;
    } else if (tmp[0] == '/') {
        start = tmp + 1;
    }

    for (char *p = start; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (tmp[0] && mkdir(tmp, 0777) != 0 && errno != EEXIST) {
                *p = '/';
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

int scan_archives(const char *directory, ArchiveEntry *entries, size_t capacity, size_t *count) {
    if (!directory || !entries || !count) return -1;

    *count = 0;

    DIR *dir = opendir(directory);
    if (!dir) return -1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (*count >= capacity) break;
        if (ent->d_name[0] == '.') continue;
        if (!has_supported_archive_extension(ent->d_name)) continue;

        ArchiveEntry *entry = &entries[*count];
        snprintf(entry->name, sizeof(entry->name), "%s", ent->d_name);
        snprintf(entry->path, sizeof(entry->path), "%s/%s", directory, ent->d_name);
        (*count)++;
    }

    closedir(dir);
    return 0;
}

int make_archive_output_dir(const char *base_output_dir, const char *archive_name, char *out, size_t out_size) {
    if (!base_output_dir || !archive_name || !out || out_size == 0) return -1;

    char clean[256];
    size_t n = 0;

    for (size_t i = 0; archive_name[i] && n + 1 < sizeof(clean); ++i) {
        unsigned char c = (unsigned char)archive_name[i];
        if (c == '/' || c == '\\' || c == ':') {
            clean[n++] = '_';
        } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == ' ') {
            clean[n++] = (char)c;
        } else {
            clean[n++] = '_';
        }
    }
    clean[n] = '\0';

    static const char *const suffixes[] = {
        ".tar.gz", ".tar.xz", ".tar.bz2", ".tar.zst",
        ".tgz", ".txz", ".tbz2", ".tbz", ".tzst",
        ".7z.001", ".zip.001", ".rar", ".cbr", ".7z", ".zip", ".zipx", ".tar", ".gz", ".xz", ".bz2", ".zst"
    };

    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        if (ends_with_case_insensitive(clean, suffixes[i])) {
            size_t clean_len = strlen(clean);
            size_t suffix_len = strlen(suffixes[i]);
            clean[clean_len - suffix_len] = '\0';
            break;
        }
    }

    if (clean[0] == '\0') {
        snprintf(clean, sizeof(clean), "archive");
    }

    int written = snprintf(out, out_size, "%s/%s", base_output_dir, clean);
    if (written < 0 || (size_t)written >= out_size) return -1;

    return 0;
}


static int dirname_of_path(const char *path, char *out, size_t out_size) {
    if (!path || !out || out_size == 0) return -1;
    int written = snprintf(out, out_size, "%s", path);
    if (written < 0 || (size_t)written >= out_size) return -1;
    char *slash = strrchr(out, '/');
    if (!slash) return -1;
    if (slash <= out + strlen("sdmc:")) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    return 0;
}

static int basename_of_path(const char *path, char *out, size_t out_size) {
    if (!path || !out || out_size == 0) return -1;
    const char *end = path + strlen(path);
    while (end > path && end[-1] == '/') end--;
    const char *slash = end;
    while (slash > path && slash[-1] != '/') slash--;
    size_t len = (size_t)(end - slash);
    if (len == 0 || len >= out_size) return -1;
    memcpy(out, slash, len);
    out[len] = '\0';
    return 0;
}

static void strip_archive_suffix(char *name) {
    if (!name || !*name) return;
    static const char *const suffixes[] = {
        ".tar.gz", ".tar.xz", ".tar.bz2", ".tar.zst",
        ".tgz", ".txz", ".tbz2", ".tbz", ".tzst",
        ".7z.001", ".zip.001", ".rar", ".cbr", ".7z", ".zip", ".zipx", ".tar", ".gz", ".xz", ".bz2", ".zst"
    };
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        if (ends_with_case_insensitive(name, suffixes[i])) {
            size_t clean_len = strlen(name);
            size_t suffix_len = strlen(suffixes[i]);
            if (clean_len > suffix_len) name[clean_len - suffix_len] = '\0';
            return;
        }
    }
}

static void sanitize_component_in_place(char *name) {
    if (!name) return;
    for (size_t i = 0; name[i]; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '-' || c == '_' || c == '.' || c == ' ')) name[i] = '_';
    }
    if (!name[0]) snprintf(name, 8, "archive");
}

static int unique_path(char *path, size_t path_size, int is_zip) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;

    char original[SWITCH7ZIP_MAX_PATH];
    int written = snprintf(original, sizeof(original), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(original)) return -1;

    char base[SWITCH7ZIP_MAX_PATH];
    char suffix[16] = "";
    if (is_zip) {
        size_t len = strlen(original);
        if (len > 4 && ends_with_case_insensitive(original, ".zip")) {
            memcpy(base, original, len - 4);
            base[len - 4] = '\0';
            snprintf(suffix, sizeof(suffix), ".zip");
        } else {
            snprintf(base, sizeof(base), "%s", original);
        }
    } else {
        snprintf(base, sizeof(base), "%s", original);
    }

    for (int i = 1; i < 1000; ++i) {
        written = snprintf(path, path_size, "%s_%d%s", base, i, suffix);
        if (written < 0 || (size_t)written >= path_size) return -1;
        if (stat(path, &st) != 0) return 0;
    }
    return -1;
}

int make_sibling_extract_dir(const char *archive_path, char *out, size_t out_size) {
    if (!archive_path || !out || out_size == 0) return -1;
    char dir[SWITCH7ZIP_MAX_PATH];
    char name[256];
    if (dirname_of_path(archive_path, dir, sizeof(dir)) != 0) return -1;
    if (basename_of_path(archive_path, name, sizeof(name)) != 0) return -1;
    strip_archive_suffix(name);
    sanitize_component_in_place(name);
    int written = snprintf(out, out_size, "%s/%s", dir, name[0] ? name : "archive");
    if (written < 0 || (size_t)written >= out_size) return -1;
    return unique_path(out, out_size, 0);
}

int make_sibling_zip_path(const char *source_path, char *out, size_t out_size) {
    if (!source_path || !out || out_size == 0) return -1;
    char dir[SWITCH7ZIP_MAX_PATH];
    char name[256];
    if (dirname_of_path(source_path, dir, sizeof(dir)) != 0) return -1;
    if (basename_of_path(source_path, name, sizeof(name)) != 0) return -1;
    sanitize_component_in_place(name);
    int written = snprintf(out, out_size, "%s/%s.zip", dir, name[0] ? name : "selection");
    if (written < 0 || (size_t)written >= out_size) return -1;
    return unique_path(out, out_size, 1);
}
