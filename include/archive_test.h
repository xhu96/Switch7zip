/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#pragma once

#include <stdbool.h>

#include "archive_extract.h"

/*
 * Reads an archive fully without writing extracted files. This forces libarchive
 * to parse headers and stream file payloads so CRC/data errors surface before a
 * large extraction job modifies the SD card.
 */
int test_archive_integrity_ex(const char *archive_path,
                              ExtractStats *stats,
                              ExtractProgressCallback progress_cb,
                              void *user_data,
                              const ExtractOptions *options);
