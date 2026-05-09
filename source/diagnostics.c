/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#include "diagnostics.h"

#include <archive.h>
#include <archive_entry.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

static int format_part_name(char *out, size_t out_size, const char *stem, const char *suffix_kind, unsigned index) {
    if (!out || out_size == 0 || !stem) return -1;
    int n;
    if (strcmp(suffix_kind, "part-rar") == 0) n = snprintf(out, out_size, "%s.part%u.rar", stem, index);
    else n = snprintf(out, out_size, "%s.%03u", stem, index);
    return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
}

static int format_split_zip_name(char *out, size_t out_size, const char *stem, unsigned index) {
    if (!out || out_size == 0 || !stem) return -1;
    int n = snprintf(out, out_size, "%s.z%02u", stem, index);
    return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
}

static int format_first_part_name(char *out, size_t out_size, const char *stem, const char *kind) {
    if (!out || out_size == 0 || !stem || !kind) return -1;
    int n;
    if (strcmp(kind, "part-rar") == 0) n = snprintf(out, out_size, "%s.part1.rar", stem);
    else n = snprintf(out, out_size, "%s.001", stem);
    return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
}


static int ends_with_ci(const char *value, const char *suffix) {
    if (!value || !suffix) return 0;
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > value_len) return 0;
    const char *start = value + value_len - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        if (tolower((unsigned char)start[i]) != tolower((unsigned char)suffix[i])) return 0;
    }
    return 1;
}

static const char *base_name(const char *path) {
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int dir_name(const char *path, char *out, size_t out_size) {
    if (!path || !out || out_size == 0) return -1;
    int n = snprintf(out, out_size, "%s", path);
    if (n < 0 || (size_t)n >= out_size) return -1;
    char *slash = strrchr(out, '/');
    if (!slash) return -1;
    if (slash <= out + strlen("sdmc:")) slash[1] = '\0';
    else *slash = '\0';
    return 0;
}

static int join_path(char *out, size_t out_size, const char *base, const char *name) {
    if (!out || !base || !name) return -1;
    const char *sep = (base[0] && base[strlen(base) - 1] == '/') ? "" : "/";
    int n = snprintf(out, out_size, "%s%s%s", base, sep, name);
    return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
}

uint64_t nxcmd_free_space_for_path(const char *path) {
    char probe[SWITCH7ZIP_MAX_PATH];
    if (!path || !*path) snprintf(probe, sizeof(probe), "sdmc:/");
    else snprintf(probe, sizeof(probe), "%s", path);

    struct stat st;
    while (stat(probe, &st) != 0) {
        char *slash = strrchr(probe, '/');
        if (!slash || slash <= probe + strlen("sdmc:")) {
            snprintf(probe, sizeof(probe), "sdmc:/");
            break;
        }
        *slash = '\0';
    }

    struct statvfs vfs;
    if (statvfs(probe, &vfs) != 0) return 0;
    return (uint64_t)vfs.f_bavail * (uint64_t)vfs.f_frsize;
}

static int measure_recursive(const char *path, PathSizeInfo *info) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (info) info->failures++;
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        if (info) info->dirs++;
        DIR *dir = opendir(path);
        if (!dir) {
            if (info) info->failures++;
            return -1;
        }
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char child[SWITCH7ZIP_MAX_PATH];
            if (join_path(child, sizeof(child), path, ent->d_name) != 0) {
                if (info) info->failures++;
                continue;
            }
            measure_recursive(child, info);
        }
        closedir(dir);
    } else if (S_ISREG(st.st_mode)) {
        if (info) {
            info->files++;
            if (st.st_size > 0) info->bytes += (uint64_t)st.st_size;
        }
    }
    return 0;
}

int nxcmd_measure_path_tree(const char *path, PathSizeInfo *info) {
    if (!path || !info) return -1;
    memset(info, 0, sizeof(*info));
    return measure_recursive(path, info);
}

static const char *entry_path_utf8_or_native(struct archive_entry *entry) {
    const char *path = archive_entry_pathname_utf8(entry);
    if (path && *path) return path;
    return archive_entry_pathname(entry);
}

static int normalize_member_path(const char *in, char *out, size_t out_size) {
    if (!in || !*in || !out || out_size == 0) return -1;
    if (in[0] == '/' || in[0] == '\\' || strchr(in, ':')) return -2;
    out[0] = '\0';
    size_t out_len = 0;
    const char *cur = in;
    while (*cur) {
        while (*cur == '/' || *cur == '\\') cur++;
        if (!*cur) break;
        const char *start = cur;
        while (*cur && *cur != '/' && *cur != '\\') cur++;
        size_t len = (size_t)(cur - start);
        if (len == 1 && start[0] == '.') continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') return -2;
        if (out_len && out_len + 1 < out_size) out[out_len++] = '/';
        if (out_len + len >= out_size) return -1;
        memcpy(out + out_len, start, len);
        out_len += len;
        out[out_len] = '\0';
    }
    return out_len ? 0 : -1;
}

static void normalize_selector(const char *in, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!in || !*in) return;
    char tmp[SWITCH7ZIP_MAX_PATH];
    if (normalize_member_path(in, tmp, sizeof(tmp)) != 0) return;
    snprintf(out, out_size, "%s", tmp);
    size_t in_len = strlen(in);
    size_t out_len = strlen(out);
    if (in_len && (in[in_len - 1] == '/' || in[in_len - 1] == '\\') && out_len + 1 < out_size) {
        out[out_len] = '/';
        out[out_len + 1] = '\0';
    }
}

static bool selector_matches(const char *selector, const char *path, bool is_dir) {
    if (!selector || !*selector) return true;
    size_t n = strlen(selector);
    bool selector_dir = n && selector[n - 1] == '/';
    if (selector_dir) return strncmp(path, selector, n) == 0 || (is_dir && strncmp(path, selector, n - 1) == 0 && path[n - 1] == '\0');
    return strcmp(path, selector) == 0;
}

int nxcmd_estimate_archive_unpacked_size(const char *archive_path,
                                         const char *selected_archive_path,
                                         ArchiveEstimate *estimate,
                                         char *error,
                                         size_t error_size) {
    if (!archive_path || !estimate) return -1;
    memset(estimate, 0, sizeof(*estimate));
    if (error && error_size) error[0] = '\0';
    char selector[SWITCH7ZIP_MAX_PATH];
    normalize_selector(selected_archive_path, selector, sizeof(selector));

    struct archive *reader = archive_read_new();
    if (!reader) {
        if (error && error_size) snprintf(error, error_size, "Could not allocate archive reader");
        return -1;
    }
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);
    int r = archive_read_open_filename(reader, archive_path, 1024 * 1024);
    if (r != ARCHIVE_OK) {
        if (error && error_size) snprintf(error, error_size, "Could not open archive: %s", archive_error_string(reader));
        archive_read_free(reader);
        return -1;
    }
    struct archive_entry *entry = NULL;
    while ((r = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        const char *raw = entry_path_utf8_or_native(entry);
        char clean[SWITCH7ZIP_MAX_PATH];
        if (normalize_member_path(raw, clean, sizeof(clean)) != 0) {
            estimate->unsafe_paths++;
            archive_read_data_skip(reader);
            continue;
        }
        bool is_dir = archive_entry_filetype(entry) == AE_IFDIR;
        if (!selector_matches(selector, clean, is_dir)) {
            archive_read_data_skip(reader);
            continue;
        }
        if (is_dir) estimate->dirs++;
        else if (archive_entry_filetype(entry) == AE_IFREG) {
            estimate->files++;
            if (archive_entry_size_is_set(entry) && archive_entry_size(entry) > 0) {
                uint64_t entry_size = (uint64_t)archive_entry_size(entry);
                estimate->bytes += entry_size;
                if (entry_size > estimate->largest_file_bytes) {
                    estimate->largest_file_bytes = entry_size;
                    snprintf(estimate->largest_file_path, sizeof(estimate->largest_file_path), "%s", clean);
                }
                if (entry_size > SWITCH7ZIP_FAT32_MAX_FILE_BYTES) {
                    estimate->files_over_fat32_limit++;
                    if (estimate->first_file_over_fat32_limit[0] == '\0') {
                        snprintf(estimate->first_file_over_fat32_limit, sizeof(estimate->first_file_over_fat32_limit), "%s", clean);
                    }
                }
            }
        } else {
            estimate->unsupported_entries++;
        }
        archive_read_data_skip(reader);
    }
    if (r != ARCHIVE_EOF) {
        if (error && error_size) snprintf(error, error_size, "Archive size estimate failed: %s", archive_error_string(reader));
        archive_read_free(reader);
        return -1;
    }
    archive_read_close(reader);
    archive_read_free(reader);
    return 0;
}

static int parse_trailing_part_number(const char *name, char *stem, size_t stem_size, char *ext, size_t ext_size, unsigned *index) {
    if (!name || !index) return 0;
    const char *dot = strrchr(name, '.');
    if (!dot || strlen(dot + 1) != 3 || !isdigit((unsigned char)dot[1]) || !isdigit((unsigned char)dot[2]) || !isdigit((unsigned char)dot[3])) return 0;
    *index = (unsigned)atoi(dot + 1);
    size_t stem_len = (size_t)(dot - name);
    if (stem_len == 0 || stem_len >= stem_size) return 0;
    memcpy(stem, name, stem_len);
    stem[stem_len] = '\0';
    snprintf(ext, ext_size, ".%03u", *index);
    return 1;
}

static int parse_partn_rar(const char *name, char *stem, size_t stem_size, unsigned *index) {
    if (!name || !ends_with_ci(name, ".rar")) return 0;
    const char *p = name;
    const char *last = NULL;
    while ((p = strstr(p, ".part")) != NULL) {
        last = p;
        p += 5;
    }
    if (!last) return 0;
    const char *digits = last + 5;
    if (!isdigit((unsigned char)*digits)) return 0;
    char *endp = NULL;
    unsigned long v = strtoul(digits, &endp, 10);
    if (!endp || strcasecmp(endp, ".rar") != 0 || v == 0 || v > 9999) return 0;
    size_t len = (size_t)(last - name);
    if (len == 0 || len >= stem_size) return 0;
    memcpy(stem, name, len);
    stem[len] = '\0';
    *index = (unsigned)v;
    return 1;
}

static bool file_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0;
}

static unsigned scan_numeric_parts(const char *dir, const char *stem, const char *suffix_kind, unsigned *highest, unsigned *first_missing) {
    unsigned count = 0;
    *highest = 0;
    *first_missing = 0;
    for (unsigned i = 1; i <= 999; ++i) {
        char name[SWITCH7ZIP_MAX_PATH];
        if (format_part_name(name, sizeof(name), stem, suffix_kind, i) != 0) continue;
        char path[SWITCH7ZIP_MAX_PATH];
        if (join_path(path, sizeof(path), dir, name) != 0) continue;
        if (file_exists(path)) {
            count++;
            *highest = i;
        }
    }
    if (*highest > 0) {
        for (unsigned i = 1; i <= *highest; ++i) {
            char name[SWITCH7ZIP_MAX_PATH];
            if (format_part_name(name, sizeof(name), stem, suffix_kind, i) != 0) continue;
            char path[SWITCH7ZIP_MAX_PATH];
            if (join_path(path, sizeof(path), dir, name) != 0) continue;
            if (!file_exists(path)) {
                *first_missing = i;
                break;
            }
        }
    }
    return count;
}

int nxcmd_check_multipart_archive(const char *archive_path, MultipartInfo *info) {
    if (!info) return -1;
    memset(info, 0, sizeof(*info));
    info->selected_first_part = true;
    if (!archive_path || !*archive_path) return -1;

    char dir[SWITCH7ZIP_MAX_PATH];
    if (dir_name(archive_path, dir, sizeof(dir)) != 0) return 0;
    const char *name = base_name(archive_path);
    char stem[512];
    char dummy[32];
    unsigned index = 0;
    const char *kind = NULL;

    if (parse_partn_rar(name, stem, sizeof(stem), &index)) {
        kind = "part-rar";
    } else if (parse_trailing_part_number(name, stem, sizeof(stem), dummy, sizeof(dummy), &index) && (ends_with_ci(stem, ".zip") || ends_with_ci(stem, ".7z") || ends_with_ci(stem, ".rar"))) {
        kind = "numeric";
    } else if (ends_with_ci(name, ".zip")) {
        size_t stem_len = strlen(name) - 4;
        if (stem_len > 0 && stem_len < sizeof(stem)) {
            memcpy(stem, name, stem_len);
            stem[stem_len] = '\0';
            char z01_name[SWITCH7ZIP_MAX_PATH];
            if (format_split_zip_name(z01_name, sizeof(z01_name), stem, 1) != 0) return 0;
            char z01_path[SWITCH7ZIP_MAX_PATH];
            if (join_path(z01_path, sizeof(z01_path), dir, z01_name) == 0 && file_exists(z01_path)) {
                info->is_multipart = true;
                info->selected_index = 0;
                info->selected_first_part = true;
                for (unsigned i = 1; i <= 99; ++i) {
                    char zn[SWITCH7ZIP_MAX_PATH];
                    if (format_split_zip_name(zn, sizeof(zn), stem, i) != 0) break;
                    char zp[SWITCH7ZIP_MAX_PATH];
                    if (join_path(zp, sizeof(zp), dir, zn) != 0) continue;
                    if (file_exists(zp)) {
                        info->part_count++;
                        info->highest_index = i;
                    }
                }
                if (info->highest_index) {
                    for (unsigned i = 1; i <= info->highest_index; ++i) {
                        char zn[SWITCH7ZIP_MAX_PATH];
                        if (format_split_zip_name(zn, sizeof(zn), stem, i) != 0) break;
                        char zp[SWITCH7ZIP_MAX_PATH];
                        if (join_path(zp, sizeof(zp), dir, zn) != 0) continue;
                        if (!file_exists(zp)) { info->first_missing_index = i; break; }
                    }
                }
                info->missing_parts = info->first_missing_index != 0;
                snprintf(info->first_part_path, sizeof(info->first_part_path), "%s", z01_path);
                if (info->missing_parts) snprintf(info->message, sizeof(info->message), "Split ZIP appears incomplete: missing .z%02u", info->first_missing_index);
                else snprintf(info->message, sizeof(info->message), "Split ZIP detected: %u .zNN part(s) plus final .zip.", info->part_count);
                return 0;
            }
        }
        return 0;
    } else {
        return 0;
    }

    info->is_multipart = true;
    info->selected_index = index;
    info->selected_first_part = index == 1;
    info->part_count = scan_numeric_parts(dir, stem, kind, &info->highest_index, &info->first_missing_index);
    info->missing_parts = info->first_missing_index != 0;

    char first_name[SWITCH7ZIP_MAX_PATH];
    if (format_first_part_name(first_name, sizeof(first_name), stem, kind) != 0) {
        snprintf(info->message, sizeof(info->message), "Multipart archive name is too long to validate safely.");
        return 0;
    }
    join_path(info->first_part_path, sizeof(info->first_part_path), dir, first_name);

    if (!info->selected_first_part) {
        snprintf(info->message, sizeof(info->message), "Multipart archive: select first part instead: %s", first_name);
    } else if (info->missing_parts) {
        snprintf(info->message, sizeof(info->message), "Multipart archive appears incomplete: missing part %u", info->first_missing_index);
    } else {
        snprintf(info->message, sizeof(info->message), "Multipart archive detected: %u part(s), first part selected.", info->part_count);
    }
    return 0;
}

bool nxcmd_file_is_text_like(const char *name) {
    if (!name) return false;
    static const char *const exts[] = {
        ".txt", ".log", ".ini", ".cfg", ".json", ".xml", ".md", ".nfo", ".csv", ".yaml", ".yml"
    };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
        if (ends_with_ci(name, exts[i])) return true;
    }
    return false;
}


bool nxcmd_file_is_image_like(const char *name) {
    if (!name) return false;
    static const char *const exts[] = {
        ".bmp", ".ppm", ".pgm", ".jpg", ".jpeg", ".png", ".webp", ".tif", ".tiff"
    };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
        if (ends_with_ci(name, exts[i])) return true;
    }
    return false;
}
