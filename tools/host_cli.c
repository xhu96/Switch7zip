#include "archive_extract.h"

#include <stdio.h>

static void progress(const ExtractStats *stats, const char *entry_name, void *user_data) {
    (void)user_data;
    printf("[%llu] %s\n", stats ? (unsigned long long)stats->entries_seen : 0ULL, entry_name ? entry_name : "");
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <archive> <output-dir>\n", argv[0]);
        return 2;
    }

    ExtractStats stats;
    int rc = extract_archive_to_dir(argv[1], argv[2], &stats, progress, NULL);
    if (rc != 0) {
        fprintf(stderr, "Extraction failed: %s\n", stats.error_message[0] ? stats.error_message : "unknown error");
        return 1;
    }

    printf("Done: entries=%llu files=%llu dirs=%llu bytes=%llu unsafe_skipped=%llu unsupported_skipped=%llu\n",
           (unsigned long long)stats.entries_seen,
           (unsigned long long)stats.files_written,
           (unsigned long long)stats.dirs_created,
           (unsigned long long)stats.bytes_written,
           (unsigned long long)stats.unsafe_paths_skipped,
           (unsigned long long)stats.unsupported_entries_skipped);
    return 0;
}
