/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */

#pragma once

#define APP_NAME "Switch 7zip"
#define APP_VERSION_STRING "0.9.11-pre"
#define APP_AUTHOR "Xhulio"
#define APP_STATUS_LABEL "PRE-1.0 PREVIEW"
#define APP_SAFETY_ALERT "PRE-1.0 PREVIEW - NOT FULLY TESTED. BACK UP IMPORTANT DATA BEFORE BULK OPERATIONS."

#define SWITCH7ZIP_BASE_DIR      "sdmc:/switch/Switch7zip"
#define SWITCH7ZIP_INPUT_DIR     "sdmc:/switch/Switch7zip/in"
#define SWITCH7ZIP_OUTPUT_DIR    "sdmc:/switch/Switch7zip/out"
#define SWITCH7ZIP_COMPRESS_DIR  "sdmc:/switch/Switch7zip/compressed"
#define SWITCH7ZIP_LOG_DIR       "sdmc:/switch/Switch7zip/logs"
#define SWITCH7ZIP_LATEST_LOG    "sdmc:/switch/Switch7zip/logs/latest.log"
#define SWITCH7ZIP_CONFIG_PATH   "sdmc:/switch/Switch7zip/config.ini"
#define SWITCH7ZIP_TRASH_DIR     "sdmc:/switch/Switch7zip/.trash"
#define SWITCH7ZIP_DIAGNOSTIC_BUNDLE "sdmc:/switch/Switch7zip/logs/diagnostic_bundle.txt"
#define SWITCH7ZIP_FAILED_REPORT "sdmc:/switch/Switch7zip/logs/failed_operation.txt"
#define SWITCH7ZIP_PARTIAL_SUFFIX ".partial"
#define SWITCH7ZIP_SPLIT_SUFFIX ".split"

#define SWITCH7ZIP_MAX_ARCHIVES 256
#define SWITCH7ZIP_MAX_PATH     768
#define SWITCH7ZIP_STATUS_LEN   1024
#define SWITCH7ZIP_FAT32_MAX_FILE_BYTES 4294967295ULL
#define SWITCH7ZIP_FAT32_CHUNK_BYTES    4293918720ULL /* 4 GiB minus 1 MiB safety margin */
