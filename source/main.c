/*
 * Switch 7zip - Nintendo Switch homebrew file manager and archive utility.
 * Author: Xhulio
 * Status: pre-1.0 preview; not fully tested across every firmware, CFW,
 * archive format, multipart set, or very large SD-card workload.
 */


#include <switch.h>
#include <SDL.h>
#include <SDL_image.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "app_config.h"
#include "archive_extract.h"
#include "archive_compress.h"
#include "archive_browse.h"
#include "archive_test.h"
#include "fs_utils.h"
#include "logging.h"
#include "alert_audio.h"
#include "file_ops.h"
#include "diagnostics.h"
#include "benchmark.h"

#define SCREEN_W 1280
#define SCREEN_H 720
#define MAX_BROWSER_ITEMS 512
#define ROW_HEIGHT 44
#define LIST_X 34
#define LIST_Y 190
#define LIST_W 770
#define LIST_H 392
#define DETAILS_X 832
#define DETAILS_Y 190
#define DETAILS_W 414
#define DETAILS_H 392
#define TOPBAR_H 92
#define ALERT_Y 104
#define BREADCRUMB_Y 146
#define FOOTER_Y 632
#define STATUS_Y 586
#define LOG_VIEW_LINES 12
#define LOG_VIEW_LINE_LEN 160
#define JOB_HISTORY_COUNT 8
#define FILE_SELECTION_MAX 64
#define RECENT_PATHS_MAX 8

typedef enum BrowserItemType {
    ITEM_PARENT = 0,
    ITEM_DIRECTORY,
    ITEM_ARCHIVE,
    ITEM_FILE
} BrowserItemType;

typedef enum OperationMode {
    OP_IDLE = 0,
    OP_EXTRACT,
    OP_COMPRESS,
    OP_TEST,
    OP_COPY,
    OP_MOVE,
    OP_TRASH,
    OP_CLEANUP,
    OP_BENCHMARK
} OperationMode;

typedef enum JobStatus {
    JOB_STATUS_DONE = 0,
    JOB_STATUS_FAILED,
    JOB_STATUS_CANCELLED
} JobStatus;

typedef enum OverlayMode {
    OVERLAY_NONE = 0,
    OVERLAY_CONTEXT_MENU,
    OVERLAY_SETTINGS,
    OVERLAY_JOBS,
    OVERLAY_PROPERTIES,
    OVERLAY_BOOKMARKS
} OverlayMode;

typedef enum ContextAction {
    CTX_OPEN = 0,
    CTX_MARK,
    CTX_SELECT_ALL,
    CTX_CLEAR_SELECTION,
    CTX_EXTRACT,
    CTX_TEST,
    CTX_COMPRESS,
    CTX_COPY,
    CTX_MOVE,
    CTX_PASTE,
    CTX_TRASH,
    CTX_NEW_FOLDER,
    CTX_NEW_FILE,
    CTX_RENAME,
    CTX_PROPERTIES,
    CTX_HOMEBREW_INFO,
    CTX_COMPARE_TARGET,
    CTX_VIEW_TEXT,
    CTX_EDIT_TEXT,
    CTX_HEX_VIEW,
    CTX_VIEW_IMAGE,
    CTX_FIND,
    CTX_SORT_NEXT,
    CTX_FILTER_NEXT,
    CTX_TOGGLE_HIDDEN,
    CTX_ARCHIVE_BIT,
    CTX_TOGGLE_DUAL_PANE,
    CTX_SWAP_PANES,
    CTX_SET_OTHER_PANE,
    CTX_PASTE_OTHER_PANE,
    CTX_RESTORE_TRASH,
    CTX_EMPTY_TRASH,
    CTX_CLEAN_PARTIALS,
    CTX_EXPORT_DIAGNOSTICS,
    CTX_BOOKMARKS,
    CTX_SD_BENCHMARK,
    CTX_REFRESH,
    CTX_JOBS,
    CTX_LOGS,
    CTX_SETTINGS,
    CTX_EXIT,
    CTX_COUNT
} ContextAction;

typedef enum SortMode {
    SORT_NAME = 0,
    SORT_DATE,
    SORT_SIZE,
    SORT_TYPE,
    SORT_COUNT
} SortMode;

typedef enum FilterMode {
    FILTER_ALL = 0,
    FILTER_ARCHIVES,
    FILTER_IMAGES,
    FILTER_TEXT,
    FILTER_NRO,
    FILTER_FOLDERS,
    FILTER_COUNT
} FilterMode;

typedef enum SettingRow {
    SETTING_EXTRACT_FOLDER = 0,
    SETTING_OVERWRITE,
    SETTING_SOUNDS,
    SETTING_APPLET_WARN,
    SETTING_TRASH_DELETE,
    SETTING_FAT32_MODE,
    SETTING_COUNT
} SettingRow;

typedef struct UserSettings {
    bool extract_to_folder;
    bool overwrite_existing;
    bool sounds_enabled;
    bool applet_warning_enabled;
    bool trash_delete_enabled;
    bool fat32_guard_enabled;
    Fat32OversizeMode fat32_oversize_mode;
} UserSettings;

typedef struct BrowserItem {
    char name[256];
    char path[SWITCH7ZIP_MAX_PATH];
    BrowserItemType type;
    uint64_t size;
    uint64_t mtime;
} BrowserItem;

typedef struct JobRecord {
    uint64_t id;
    OperationMode kind;
    JobStatus status;
    char title[96];
    char source[SWITCH7ZIP_MAX_PATH];
    char destination[SWITCH7ZIP_MAX_PATH];
    uint64_t bytes_done;
    uint64_t bytes_total;
    uint64_t elapsed_ms;
    uint64_t files_done;
} JobRecord;

typedef struct AppState {
    SDL_Window *window;
    SDL_Renderer *renderer;
    PadState pad;
    BrowserItem items[MAX_BROWSER_ITEMS];
    size_t count;
    size_t cursor;
    size_t scroll;
    char current_dir[SWITCH7ZIP_MAX_PATH];
    char status[SWITCH7ZIP_STATUS_LEN];
    char last_output_dir[SWITCH7ZIP_MAX_PATH];
    ExtractStats live_stats;
    CompressStats live_compress;
    FileOpStats live_fileop;
    BenchmarkStats live_benchmark;
    OperationMode operation;
    bool extracting;
    bool compressing;
    bool file_operating;
    bool benchmarking;
    bool applet_mode_alert;
    bool renderer_accelerated;
    bool cancel_requested;
    JobRecord job_history[JOB_HISTORY_COUNT];
    size_t job_history_count;
    uint64_t next_job_id;
    uint64_t last_logged_progress;
    uint64_t operation_start_ticks;
    bool log_view_open;
    bool image_view_open;
    SDL_Texture *image_texture;
    int image_w;
    int image_h;
    float image_zoom;
    int image_pan_x;
    int image_pan_y;
    int image_decode_flags;
    char image_path[SWITCH7ZIP_MAX_PATH];
    char image_title[128];
    char image_error[SWITCH7ZIP_STATUS_LEN];
    char log_lines[LOG_VIEW_LINES][LOG_VIEW_LINE_LEN];
    size_t log_line_count;
    char text_view_path[SWITCH7ZIP_MAX_PATH];
    char text_view_title[128];
    char property_lines[14][LOG_VIEW_LINE_LEN];
    size_t property_line_count;
    size_t bookmark_cursor;
    AppletType applet_type;
    UserSettings settings;
    OverlayMode overlay;
    size_t context_cursor;
    size_t settings_cursor;
    bool exit_requested;
    bool archive_view;
    bool marked[MAX_BROWSER_ITEMS];
    size_t marked_count;
    char clipboard_paths[FILE_SELECTION_MAX][SWITCH7ZIP_MAX_PATH];
    size_t clipboard_count;
    bool clipboard_move;
    char archive_path[SWITCH7ZIP_MAX_PATH];
    char archive_prefix[SWITCH7ZIP_MAX_PATH];
    char archive_title[256];
    SortMode sort_mode;
    FilterMode filter_mode;
    bool show_hidden;
    bool dual_pane_enabled;
    char other_dir[SWITCH7ZIP_MAX_PATH];
    char restore_dir[SWITCH7ZIP_MAX_PATH];
    char recent_dirs[RECENT_PATHS_MAX][SWITCH7ZIP_MAX_PATH];
    size_t recent_dir_count;
} AppState;

static void extract_selected(AppState *app);
static void test_selected(AppState *app);
static void compress_selected(AppState *app);
static void copy_selection_to_clipboard(AppState *app, bool move);
static void paste_clipboard(AppState *app);
static void trash_selection(AppState *app);
static void create_folder_action(AppState *app);
static void create_file_action(AppState *app);
static void rename_selected(AppState *app);
static void edit_text_selected(AppState *app);
static void open_hex_selected(AppState *app);
static void cycle_sort_mode(AppState *app);
static void cycle_filter_mode(AppState *app);
static void toggle_show_hidden(AppState *app);
static void set_archive_bit_selected(AppState *app);
static void toggle_mark_current(AppState *app);
static void select_all_items(AppState *app);
static void clear_marks(AppState *app);
static void refresh_log_view(AppState *app);
static void render_app(AppState *app);
static void scan_current_dir(AppState *app);
static const char *fat32_mode_label(Fat32OversizeMode mode);
static bool operation_cancel_requested(void *user_data);
static void fileop_progress(const FileOpStats *stats, const char *entry_name, void *user_data);
static size_t gather_selected_paths(AppState *app, char out[FILE_SELECTION_MAX][SWITCH7ZIP_MAX_PATH]);
static size_t bookmark_count(void);
static const char *type_label(BrowserItemType type);
static const char *sort_mode_label(SortMode mode);
static const char *filter_mode_label(FilterMode mode);
static const char *bookmark_paths[];
static void toggle_dual_pane(AppState *app);
static void swap_panes(AppState *app);
static void set_other_pane_to_current(AppState *app);
static void paste_clipboard_other_pane(AppState *app);
static void restore_trash_selection(AppState *app);
static void empty_trash_action(AppState *app);
static void cleanup_partials_action(AppState *app);
static void export_diagnostics_bundle(AppState *app);

static void open_properties(AppState *app);
static void open_homebrew_info(AppState *app);
static void compare_with_target_pane(AppState *app);
static void open_text_selected(AppState *app);
static void open_image_selected(AppState *app);
static void find_in_current_folder(AppState *app);
static void open_bookmarks(AppState *app);
static void run_sd_benchmark(AppState *app);
static void close_image_view(AppState *app);

static const uint8_t FONT5X7[95][7] = {
    [0] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    [1] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04},
    [2] = {0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00},
    [3] = {0x0a, 0x1f, 0x0a, 0x0a, 0x1f, 0x0a, 0x00},
    [4] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04},
    [5] = {0x19, 0x1a, 0x04, 0x08, 0x16, 0x06, 0x00},
    [6] = {0x0c, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0d},
    [7] = {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00},
    [8] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02},
    [9] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08},
    [10] = {0x00, 0x15, 0x0e, 0x1f, 0x0e, 0x15, 0x00},
    [11] = {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00},
    [12] = {0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08},
    [13] = {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00},
    [14] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c},
    [15] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00},
    [16] = {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
    [17] = {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
    [18] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
    [19] = {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e},
    [20] = {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
    [21] = {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e},
    [22] = {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e},
    [23] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    [24] = {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
    [25] = {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x1c},
    [26] = {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00},
    [27] = {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x04, 0x08},
    [28] = {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02},
    [29] = {0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00},
    [30] = {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08},
    [31] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04},
    [32] = {0x0e, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0e},
    [33] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    [34] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
    [35] = {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e},
    [36] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e},
    [37] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
    [38] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
    [39] = {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f},
    [40] = {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    [41] = {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},
    [42] = {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e},
    [43] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    [44] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
    [45] = {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11},
    [46] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    [47] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    [48] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
    [49] = {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},
    [50] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
    [51] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
    [52] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    [53] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    [54] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04},
    [55] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a},
    [56] = {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
    [57] = {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04},
    [58] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},
    [59] = {0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e},
    [60] = {0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00},
    [61] = {0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e},
    [62] = {0x04, 0x0a, 0x11, 0x00, 0x00, 0x00, 0x00},
    [63] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f},
    [64] = {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00},
    [65] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    [66] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e},
    [67] = {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e},
    [68] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e},
    [69] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f},
    [70] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10},
    [71] = {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f},
    [72] = {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11},
    [73] = {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e},
    [74] = {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e},
    [75] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    [76] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f},
    [77] = {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11},
    [78] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    [79] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    [80] = {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10},
    [81] = {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d},
    [82] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11},
    [83] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e},
    [84] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    [85] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e},
    [86] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04},
    [87] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a},
    [88] = {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11},
    [89] = {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04},
    [90] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f},
    [91] = {0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02},
    [92] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    [93] = {0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08},
    [94] = {0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00},
};


static SDL_Color C_BG       = { 9, 13, 22, 255 };
static SDL_Color C_BG_2     = { 13, 18, 30, 255 };
static SDL_Color C_PANEL    = { 24, 31, 46, 255 };
static SDL_Color C_PANEL_2  = { 34, 44, 64, 255 };
static SDL_Color C_PANEL_3  = { 47, 61, 86, 255 };
static SDL_Color C_LINE     = { 70, 88, 116, 255 };
static SDL_Color C_TEXT     = { 242, 247, 255, 255 };
static SDL_Color C_MUTED    = { 151, 164, 190, 255 };
static SDL_Color C_ACCENT   = { 64, 178, 255, 255 };
static SDL_Color C_ACCENT_2 = { 136, 94, 255, 255 };
static SDL_Color C_GREEN    = { 71, 221, 152, 255 };
static SDL_Color C_AMBER    = { 255, 191, 87, 255 };
static SDL_Color C_RED      = { 255, 99, 112, 255 };
static SDL_Color C_BLACK_A  = { 0, 0, 0, 92 };

static void set_draw_color(SDL_Renderer *r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c) {
    SDL_Rect rect = {x, y, w, h};
    set_draw_color(r, c);
    SDL_RenderFillRect(r, &rect);
}

static void stroke_rect(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c) {
    SDL_Rect rect = {x, y, w, h};
    set_draw_color(r, c);
    SDL_RenderDrawRect(r, &rect);
}

static void fill_card(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c) {
    fill_rect(r, x + 4, y + 5, w, h, C_BLACK_A);
    fill_rect(r, x, y, w, h, c);
    stroke_rect(r, x, y, w, h, C_LINE);
}

static void draw_accent_bar(SDL_Renderer *r, int x, int y, int w, SDL_Color c) {
    fill_rect(r, x, y, w, 4, c);
}


static char safe_display_char(char ch) {
    unsigned char c = (unsigned char)ch;
    if (c < 32 || c > 126) return '?';
    return (char)toupper(c);
}

static int text_width_px(const char *text, int scale) {
    if (!text) return 0;
    return (int)strlen(text) * 6 * scale;
}

static void copy_cstr(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    size_t n = 0;
    while (n + 1 < dst_size && src[n]) n++;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

static void append_cstr(char *dst, size_t dst_size, const char *suffix) {
    if (!dst || dst_size == 0 || !suffix) return;
    size_t len = 0;
    while (len < dst_size && dst[len]) len++;
    if (len >= dst_size) {
        dst[dst_size - 1] = '\0';
        return;
    }
    size_t i = 0;
    while (len + 1 < dst_size && suffix[i]) dst[len++] = suffix[i++];
    dst[len] = '\0';
}

static void draw_char(SDL_Renderer *r, int x, int y, char ch, int scale, SDL_Color color) {
    ch = safe_display_char(ch);
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *rows = FONT5X7[(int)ch - 32];
    set_draw_color(r, color);
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if (rows[row] & (1u << (4 - col))) {
                SDL_Rect px = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(r, &px);
            }
        }
    }
}

static void draw_text(SDL_Renderer *r, int x, int y, const char *text, int scale, SDL_Color color) {
    if (!text) return;
    for (size_t i = 0; text[i]; ++i) {
        draw_char(r, x + (int)i * 6 * scale, y, text[i], scale, color);
    }
}

static void draw_text_clipped(SDL_Renderer *r, int x, int y, const char *text, int scale, SDL_Color color, int max_w) {
    if (!text || max_w <= 0) return;
    int max_chars = max_w / (6 * scale);
    if (max_chars <= 0) return;
    char buf[512];
    size_t len = strlen(text);
    if ((int)len <= max_chars) {
        draw_text(r, x, y, text, scale, color);
        return;
    }
    if (max_chars < 4) return;
    size_t take = (size_t)(max_chars - 3);
    if (take >= sizeof(buf) - 4) take = sizeof(buf) - 4;
    memcpy(buf, text, take);
    memcpy(buf + take, "...", 4);
    draw_text(r, x, y, buf, scale, color);
}

static void format_bytes(uint64_t bytes, char *out, size_t out_size) {
    const char *units[] = {"B", "KB", "MB", "GB"};
    double value = (double)bytes;
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        unit++;
    }
    if (unit == 0) snprintf(out, out_size, "%llu %s", (unsigned long long)bytes, units[unit]);
    else snprintf(out, out_size, "%.1f %s", value, units[unit]);
}


static void format_duration(uint64_t seconds, char *out, size_t out_size) {
    uint64_t h = seconds / 3600ULL;
    uint64_t m = (seconds / 60ULL) % 60ULL;
    uint64_t s = seconds % 60ULL;
    if (h > 0) snprintf(out, out_size, "%lluh %02llum", (unsigned long long)h, (unsigned long long)m);
    else snprintf(out, out_size, "%llum %02llus", (unsigned long long)m, (unsigned long long)s);
}

static void format_rate(uint64_t bytes_per_second, char *out, size_t out_size) {
    char bytes[64];
    format_bytes(bytes_per_second, bytes, sizeof(bytes));
    copy_cstr(out, out_size, bytes);
    append_cstr(out, out_size, "/s");
}

static uint64_t operation_done_bytes(const AppState *app) {
    if (!app) return 0;
    if (app->operation == OP_EXTRACT || app->operation == OP_TEST) {
        if (app->live_stats.archive_bytes_read > 0) return app->live_stats.archive_bytes_read;
        return app->live_stats.bytes_written;
    }
    if (app->operation == OP_COMPRESS) return app->live_compress.bytes_read;
    if (app->operation == OP_COPY || app->operation == OP_MOVE || app->operation == OP_TRASH) return app->live_fileop.bytes_done;
    if (app->operation == OP_BENCHMARK) return app->live_benchmark.bytes_done;
    return 0;
}

static uint64_t operation_expected_bytes(const AppState *app) {
    if (!app) return 0;
    if (app->operation == OP_EXTRACT || app->operation == OP_TEST) {
        if (app->live_stats.archive_bytes_total > 0) return app->live_stats.archive_bytes_total;
        return app->live_stats.bytes_expected;
    }
    if (app->operation == OP_COMPRESS) return app->live_compress.bytes_expected;
    if (app->operation == OP_COPY || app->operation == OP_MOVE || app->operation == OP_TRASH) return app->live_fileop.bytes_expected;
    if (app->operation == OP_BENCHMARK) return app->live_benchmark.bytes_expected;
    return 0;
}

static uint64_t current_file_done_bytes(const AppState *app) {
    if (!app) return 0;
    if (app->operation == OP_EXTRACT || app->operation == OP_TEST) return app->live_stats.current_entry_written;
    if (app->operation == OP_COMPRESS) return app->live_compress.current_file_read;
    if (app->operation == OP_COPY || app->operation == OP_MOVE || app->operation == OP_TRASH) return app->live_fileop.bytes_done;
    if (app->operation == OP_BENCHMARK) return app->live_benchmark.bytes_done;
    return 0;
}

static uint64_t current_file_expected_bytes(const AppState *app) {
    if (!app) return 0;
    if (app->operation == OP_EXTRACT || app->operation == OP_TEST) return app->live_stats.current_entry_size;
    if (app->operation == OP_COMPRESS) return app->live_compress.current_file_size;
    if (app->operation == OP_COPY || app->operation == OP_MOVE || app->operation == OP_TRASH) return app->live_fileop.bytes_expected;
    if (app->operation == OP_BENCHMARK) return app->live_benchmark.bytes_expected;
    return 0;
}

static uint64_t operation_elapsed_ms(const AppState *app) {
    if (!app || app->operation_start_ticks == 0) return 0;
    uint64_t now = SDL_GetTicks64();
    return now >= app->operation_start_ticks ? now - app->operation_start_ticks : 0;
}

static int percent_from_parts(uint64_t done, uint64_t expected) {
    if (expected == 0) return -1;
    if (done >= expected) return 100;
    return (int)((done * 100ULL) / expected);
}


static const char *operation_label(OperationMode mode) {
    switch (mode) {
        case OP_EXTRACT: return "Extract";
        case OP_COMPRESS: return "Compress";
        case OP_TEST: return "Test";
        case OP_COPY: return "Copy";
        case OP_MOVE: return "Move";
        case OP_TRASH: return "Trash";
        case OP_CLEANUP: return "Cleanup";
        case OP_BENCHMARK: return "Benchmark";
        default: return "Idle";
    }
}

static const char *job_status_label(JobStatus status) {
    switch (status) {
        case JOB_STATUS_DONE: return "DONE";
        case JOB_STATUS_FAILED: return "FAILED";
        case JOB_STATUS_CANCELLED: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

static SDL_Color job_status_color(JobStatus status) {
    switch (status) {
        case JOB_STATUS_DONE: return C_GREEN;
        case JOB_STATUS_FAILED: return C_RED;
        case JOB_STATUS_CANCELLED: return C_AMBER;
        default: return C_MUTED;
    }
}

static void add_job_history(AppState *app,
                            OperationMode kind,
                            JobStatus status,
                            const char *title,
                            const char *source,
                            const char *destination,
                            uint64_t bytes_done,
                            uint64_t bytes_total,
                            uint64_t files_done,
                            uint64_t elapsed_ms) {
    if (!app) return;
    if (app->next_job_id == 0) app->next_job_id = 1;
    if (app->job_history_count == JOB_HISTORY_COUNT) {
        memmove(&app->job_history[0], &app->job_history[1], (JOB_HISTORY_COUNT - 1) * sizeof(app->job_history[0]));
        app->job_history_count--;
    }
    JobRecord *job = &app->job_history[app->job_history_count++];
    memset(job, 0, sizeof(*job));
    job->id = app->next_job_id++;
    job->kind = kind;
    job->status = status;
    snprintf(job->title, sizeof(job->title), "%s", title && *title ? title : operation_label(kind));
    snprintf(job->source, sizeof(job->source), "%s", source ? source : "");
    snprintf(job->destination, sizeof(job->destination), "%s", destination ? destination : "");
    job->bytes_done = bytes_done;
    job->bytes_total = bytes_total;
    job->elapsed_ms = elapsed_ms;
    job->files_done = files_done;
}

static void set_status(AppState *app, const char *message) {
    if (!app) return;
    snprintf(app->status, sizeof(app->status), "%s", message ? message : "");
}

static void set_statusf(AppState *app, const char *fmt, ...) {
    if (!app || !fmt) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, args);
    va_end(args);
}



static void ensure_app_dirs(void);

static void write_failed_operation_report(AppState *app,
                                          const char *kind,
                                          JobStatus status,
                                          const char *source,
                                          const char *destination,
                                          const char *message,
                                          const char *last_entry,
                                          const char *partial_path,
                                          uint64_t bytes_done,
                                          uint64_t bytes_total,
                                          uint64_t elapsed_ms) {
    ensure_app_dirs();
    FILE *f = fopen(SWITCH7ZIP_FAILED_REPORT, "wb");
    if (!f) return;
    fprintf(f, "Switch 7zip failed-operation report\n");
    fprintf(f, "version=%s\n", APP_VERSION_STRING);
    fprintf(f, "kind=%s\n", kind ? kind : "operation");
    fprintf(f, "status=%s\n", job_status_label(status));
    fprintf(f, "source=%s\n", source ? source : "");
    fprintf(f, "destination=%s\n", destination ? destination : "");
    fprintf(f, "message=%s\n", message ? message : "");
    fprintf(f, "last_entry=%s\n", last_entry ? last_entry : "");
    fprintf(f, "partial_path=%s\n", partial_path ? partial_path : "");
    fprintf(f, "bytes_done=%llu\n", (unsigned long long)bytes_done);
    fprintf(f, "bytes_total=%llu\n", (unsigned long long)bytes_total);
    fprintf(f, "elapsed_ms=%llu\n", (unsigned long long)elapsed_ms);
    if (app) {
        fprintf(f, "current_dir=%s\n", app->current_dir);
        fprintf(f, "archive_view=%d\n", app->archive_view ? 1 : 0);
        fprintf(f, "applet_type=%d\n", (int)app->applet_type);
        fprintf(f, "last_status=%s\n", app->status);
    }
    fclose(f);
    switch7zip_log_line("failed-operation report written: %s", SWITCH7ZIP_FAILED_REPORT);
}

static void ensure_app_dirs(void);

static bool path_has_prefix_local(const char *path, const char *prefix) {
    if (!path || !prefix) return false;
    size_t n = strlen(prefix);
    return strncmp(path, prefix, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

static void remember_recent_dir(AppState *app, const char *path) {
    if (!app || !path || !*path || app->archive_view) return;
    if (path_has_prefix_local(path, SWITCH7ZIP_TRASH_DIR)) return;
    for (size_t i = 0; i < app->recent_dir_count; ++i) {
        if (strcmp(app->recent_dirs[i], path) == 0) {
            if (i > 0) {
                char tmp[SWITCH7ZIP_MAX_PATH];
                snprintf(tmp, sizeof(tmp), "%s", app->recent_dirs[i]);
                memmove(&app->recent_dirs[1], &app->recent_dirs[0], i * sizeof(app->recent_dirs[0]));
                snprintf(app->recent_dirs[0], sizeof(app->recent_dirs[0]), "%s", tmp);
            }
            snprintf(app->restore_dir, sizeof(app->restore_dir), "%s", path);
            return;
        }
    }
    size_t max = RECENT_PATHS_MAX;
    if (app->recent_dir_count < max) app->recent_dir_count++;
    if (app->recent_dir_count > 1) memmove(&app->recent_dirs[1], &app->recent_dirs[0], (app->recent_dir_count - 1) * sizeof(app->recent_dirs[0]));
    snprintf(app->recent_dirs[0], sizeof(app->recent_dirs[0]), "%s", path);
    snprintf(app->restore_dir, sizeof(app->restore_dir), "%s", path);
}

static size_t bookmark_total_count(const AppState *app) {
    return bookmark_count() + (app ? app->recent_dir_count : 0);
}

static bool get_bookmark_or_recent_path(const AppState *app, size_t index, const char **path, bool *recent) {
    if (!path) return false;
    if (index < bookmark_count()) {
        *path = bookmark_paths[index];
        if (recent) *recent = false;
        return true;
    }
    size_t ri = index - bookmark_count();
    if (app && ri < app->recent_dir_count) {
        *path = app->recent_dirs[ri];
        if (recent) *recent = true;
        return true;
    }
    return false;
}

static void toggle_dual_pane(AppState *app) {
    if (!app) return;
    app->dual_pane_enabled = !app->dual_pane_enabled;
    if (!app->other_dir[0]) snprintf(app->other_dir, sizeof(app->other_dir), "sdmc:/switch");
    set_statusf(app, "Dual-pane workspace %s. Target: %s", app->dual_pane_enabled ? "enabled" : "hidden", app->other_dir);
    switch7zip_log_line("dual_pane=%d target=%s", app->dual_pane_enabled ? 1 : 0, app->other_dir);
}

static void set_other_pane_to_current(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "Target pane uses SD-card folders, not archive preview paths.");
        return;
    }
    snprintf(app->other_dir, sizeof(app->other_dir), "%s", app->current_dir);
    app->dual_pane_enabled = true;
    set_statusf(app, "Target pane set to %s", app->other_dir);
    switch7zip_log_line("target pane set: %s", app->other_dir);
}

static void swap_panes(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "Close archive preview before swapping panes.");
        return;
    }
    DIR *dir = opendir(app->other_dir[0] ? app->other_dir : "sdmc:/");
    if (!dir) {
        set_statusf(app, "Target pane unavailable: %s", app->other_dir);
        return;
    }
    closedir(dir);
    char tmp[SWITCH7ZIP_MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", app->current_dir);
    snprintf(app->current_dir, sizeof(app->current_dir), "%s", app->other_dir);
    snprintf(app->other_dir, sizeof(app->other_dir), "%s", tmp);
    app->dual_pane_enabled = true;
    app->cursor = 0;
    app->scroll = 0;
    scan_current_dir(app);
    set_statusf(app, "Swapped panes. Target is now %s", app->other_dir);
    switch7zip_log_line("panes swapped: active=%s target=%s", app->current_dir, app->other_dir);
}

static void paste_clipboard_other_pane(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "Paste to target pane is available in the SD-card browser.");
        return;
    }
    if (!app->other_dir[0]) {
        set_status(app, "Target pane is not set.");
        return;
    }
    DIR *dir = opendir(app->other_dir);
    if (!dir) {
        set_statusf(app, "Target pane unavailable: %s", app->other_dir);
        return;
    }
    closedir(dir);
    char active[SWITCH7ZIP_MAX_PATH];
    snprintf(active, sizeof(active), "%s", app->current_dir);
    snprintf(app->current_dir, sizeof(app->current_dir), "%s", app->other_dir);
    switch7zip_log_line("paste to target pane: %s", app->other_dir);
    paste_clipboard(app);
    snprintf(app->current_dir, sizeof(app->current_dir), "%s", active);
    app->cursor = 0;
    app->scroll = 0;
    scan_current_dir(app);
}

static void restore_trash_selection(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "Trash restore is available in the SD-card browser.");
        return;
    }
    if (!path_has_prefix_local(app->current_dir, SWITCH7ZIP_TRASH_DIR)) {
        set_status(app, "Open /switch/Switch7zip/.trash first, then select items to restore.");
        return;
    }
    if (!app->restore_dir[0]) snprintf(app->restore_dir, sizeof(app->restore_dir), "sdmc:/");
    DIR *dir = opendir(app->restore_dir);
    if (!dir) {
        mkdir_p(app->restore_dir);
    } else {
        closedir(dir);
    }
    char saved[SWITCH7ZIP_MAX_PATH];
    snprintf(saved, sizeof(saved), "%s", app->current_dir);
    app->clipboard_count = 0;
    char paths[FILE_SELECTION_MAX][SWITCH7ZIP_MAX_PATH];
    size_t n = gather_selected_paths(app, paths);
    if (n == 0) {
        set_status(app, "Select at least one Trash item to restore.");
        return;
    }
    app->clipboard_count = n;
    app->clipboard_move = true;
    for (size_t i = 0; i < n; ++i) copy_cstr(app->clipboard_paths[i], sizeof(app->clipboard_paths[i]), paths[i]);
    snprintf(app->current_dir, sizeof(app->current_dir), "%s", app->restore_dir);
    switch7zip_log_line("restore trash start: count=%llu restore_dir=%s", (unsigned long long)n, app->restore_dir);
    paste_clipboard(app);
    snprintf(app->current_dir, sizeof(app->current_dir), "%s", saved);
    app->cursor = 0;
    app->scroll = 0;
    scan_current_dir(app);
}

static void empty_trash_action(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "Empty Trash is available in the SD-card browser.");
        return;
    }
    app->operation = OP_CLEANUP;
    app->file_operating = true;
    app->cancel_requested = false;
    app->operation_start_ticks = SDL_GetTicks64();
    app->last_logged_progress = 0;
    memset(&app->live_fileop, 0, sizeof(app->live_fileop));
    switch7zip_log_reset();
    switch7zip_log_line("empty trash start: %s", SWITCH7ZIP_TRASH_DIR);
    render_app(app);

    FileOpOptions options;
    memset(&options, 0, sizeof(options));
    options.cancel_cb = operation_cancel_requested;
    options.cancel_user_data = app;

    FileOpStats stats;
    int rc = fileop_delete_path_ex(SWITCH7ZIP_TRASH_DIR, &stats, fileop_progress, app, &options);
    uint64_t elapsed_ms = operation_elapsed_ms(app);
    app->live_fileop = stats;
    app->file_operating = false;
    app->operation = OP_IDLE;
    mkdir_p(SWITCH7ZIP_TRASH_DIR);
    JobStatus job_status = rc == 0 ? JOB_STATUS_DONE : (app->cancel_requested ? JOB_STATUS_CANCELLED : JOB_STATUS_FAILED);
    add_job_history(app, OP_CLEANUP, job_status, "Empty Trash", SWITCH7ZIP_TRASH_DIR, "permanent delete", stats.bytes_done, stats.bytes_expected, stats.files_done, elapsed_ms);
    if (rc == 0) {
        set_statusf(app, "Trash emptied: %llu file(s) removed.", (unsigned long long)stats.files_done);
        switch7zip_log_line("empty trash done: files=%llu bytes=%llu elapsed_ms=%llu", (unsigned long long)stats.files_done, (unsigned long long)stats.bytes_done, (unsigned long long)elapsed_ms);
        if (path_has_prefix_local(app->current_dir, SWITCH7ZIP_TRASH_DIR)) snprintf(app->current_dir, sizeof(app->current_dir), "%s", SWITCH7ZIP_BASE_DIR);
        if (app->settings.sounds_enabled) alert_audio_play(true);
    } else if (job_status == JOB_STATUS_CANCELLED) {
        set_status(app, "Empty Trash cancelled.");
        switch7zip_log_line("empty trash cancelled: bytes=%llu elapsed_ms=%llu", (unsigned long long)stats.bytes_done, (unsigned long long)elapsed_ms);
        write_failed_operation_report(app, "empty-trash", job_status, SWITCH7ZIP_TRASH_DIR, "permanent delete", "cancelled", stats.last_entry, NULL, stats.bytes_done, stats.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    } else {
        const char *msg = stats.error_message[0] ? stats.error_message : "unknown error";
        set_statusf(app, "Empty Trash failed: %s", msg);
        switch7zip_log_line("empty trash failed: %s", msg);
        write_failed_operation_report(app, "empty-trash", job_status, SWITCH7ZIP_TRASH_DIR, "permanent delete", msg, stats.last_entry, NULL, stats.bytes_done, stats.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    }
    app->operation_start_ticks = 0;
    scan_current_dir(app);
}

static void cleanup_partials_action(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "Partial cleanup is available in the SD-card browser.");
        return;
    }
    app->operation = OP_CLEANUP;
    app->file_operating = true;
    app->cancel_requested = false;
    app->operation_start_ticks = SDL_GetTicks64();
    app->last_logged_progress = 0;
    memset(&app->live_fileop, 0, sizeof(app->live_fileop));
    switch7zip_log_reset();
    switch7zip_log_line("partial cleanup start: root=%s", app->current_dir);
    render_app(app);

    FileOpOptions options;
    memset(&options, 0, sizeof(options));
    options.cancel_cb = operation_cancel_requested;
    options.cancel_user_data = app;

    FileOpStats stats;
    int rc = fileop_cleanup_partials_ex(app->current_dir, &stats, fileop_progress, app, &options);
    uint64_t elapsed_ms = operation_elapsed_ms(app);
    app->live_fileop = stats;
    app->file_operating = false;
    app->operation = OP_IDLE;
    JobStatus job_status = rc == 0 ? JOB_STATUS_DONE : (app->cancel_requested ? JOB_STATUS_CANCELLED : JOB_STATUS_FAILED);
    add_job_history(app, OP_CLEANUP, job_status, "Clean partial files", app->current_dir, "remove *.partial", stats.bytes_done, stats.bytes_expected, stats.files_done, elapsed_ms);
    if (rc == 0) {
        char bytes[48];
        format_bytes(stats.bytes_done, bytes, sizeof(bytes));
        set_statusf(app, "Removed %llu .partial file(s), %s freed.", (unsigned long long)stats.files_done, bytes);
        switch7zip_log_line("partial cleanup done: files=%llu bytes=%llu elapsed_ms=%llu", (unsigned long long)stats.files_done, (unsigned long long)stats.bytes_done, (unsigned long long)elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(true);
    } else if (job_status == JOB_STATUS_CANCELLED) {
        set_status(app, "Partial cleanup cancelled.");
        switch7zip_log_line("partial cleanup cancelled: files=%llu bytes=%llu elapsed_ms=%llu", (unsigned long long)stats.files_done, (unsigned long long)stats.bytes_done, (unsigned long long)elapsed_ms);
        write_failed_operation_report(app, "partial-cleanup", job_status, app->current_dir, "remove *.partial", "cancelled", stats.last_entry, NULL, stats.bytes_done, stats.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    } else {
        const char *msg = stats.error_message[0] ? stats.error_message : "unknown error";
        set_statusf(app, "Partial cleanup failed: %s", msg);
        switch7zip_log_line("partial cleanup failed: %s", msg);
        write_failed_operation_report(app, "partial-cleanup", job_status, app->current_dir, "remove *.partial", msg, stats.last_entry, NULL, stats.bytes_done, stats.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    }
    app->operation_start_ticks = 0;
    scan_current_dir(app);
}

static void export_diagnostics_bundle(AppState *app) {
    if (!app) return;
    ensure_app_dirs();
    FILE *f = fopen(SWITCH7ZIP_DIAGNOSTIC_BUNDLE, "wb");
    if (!f) {
        set_status(app, "Could not write diagnostic bundle.");
        return;
    }
    fprintf(f, "Switch 7zip diagnostic bundle\n");
    fprintf(f, "version=%s\n", APP_VERSION_STRING);
    fprintf(f, "applet_type=%d\n", (int)app->applet_type);
    fprintf(f, "renderer=%s\n", app->renderer_accelerated ? "accelerated" : "software");
    fprintf(f, "current_dir=%s\n", app->current_dir);
    fprintf(f, "target_dir=%s\n", app->other_dir);
    fprintf(f, "restore_dir=%s\n", app->restore_dir);
    fprintf(f, "archive_view=%d\n", app->archive_view ? 1 : 0);
    fprintf(f, "sort=%s filter=%s hidden=%d fat32_mode=%s\n", sort_mode_label(app->sort_mode), filter_mode_label(app->filter_mode), app->show_hidden ? 1 : 0, fat32_mode_label(app->settings.fat32_oversize_mode));
    fprintf(f, "marked=%llu clipboard=%llu clipboard_mode=%s\n", (unsigned long long)app->marked_count, (unsigned long long)app->clipboard_count, app->clipboard_move ? "move" : "copy");
    fprintf(f, "last_status=%s\n", app->status);
    if (app->count > 0 && app->cursor < app->count) {
        BrowserItem *item = &app->items[app->cursor];
        fprintf(f, "selected_name=%s\nselected_path=%s\nselected_type=%s\nselected_size=%llu\n", item->name, item->path, type_label(item->type), (unsigned long long)item->size);
    }
    uint64_t free_current = nxcmd_free_space_for_path(app->current_dir);
    uint64_t free_target = nxcmd_free_space_for_path(app->other_dir);
    fprintf(f, "free_current=%llu\nfree_target=%llu\n", (unsigned long long)free_current, (unsigned long long)free_target);
    fprintf(f, "\nrecent_dirs:\n");
    for (size_t i = 0; i < app->recent_dir_count; ++i) fprintf(f, "  %s\n", app->recent_dirs[i]);
    fprintf(f, "\nlatest_log_path=%s\n", switch7zip_log_path());
    fprintf(f, "failed_report_path=%s\n", SWITCH7ZIP_FAILED_REPORT);
    fclose(f);
    switch7zip_log_line("diagnostic bundle exported: %s", SWITCH7ZIP_DIAGNOSTIC_BUNDLE);
    set_statusf(app, "Diagnostic bundle written: %s", SWITCH7ZIP_DIAGNOSTIC_BUNDLE);
}

static void settings_defaults(UserSettings *settings) {
    if (!settings) return;
    settings->extract_to_folder = true;
    settings->overwrite_existing = false;
    settings->sounds_enabled = true;
    settings->applet_warning_enabled = true;
    settings->trash_delete_enabled = true;
    settings->fat32_guard_enabled = true;
    settings->fat32_oversize_mode = FAT32_OVERSIZE_BLOCK;
}

static bool parse_bool_value(const char *value, bool fallback) {
    if (!value) return fallback;
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "on") == 0 || strcmp(value, "yes") == 0) return true;
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 || strcmp(value, "off") == 0 || strcmp(value, "no") == 0) return false;
    return fallback;
}

static void load_settings(AppState *app) {
    if (!app) return;
    settings_defaults(&app->settings);
    FILE *f = fopen(SWITCH7ZIP_CONFIG_PATH, "rb");
    if (!f) return;

    char line[160];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        for (char *p = value; *p; ++p) {
            if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
            *p = (char)tolower((unsigned char)*p);
        }
        if (strcmp(key, "extract_to_folder") == 0) app->settings.extract_to_folder = parse_bool_value(value, app->settings.extract_to_folder);
        else if (strcmp(key, "overwrite_existing") == 0) app->settings.overwrite_existing = parse_bool_value(value, app->settings.overwrite_existing);
        else if (strcmp(key, "sounds_enabled") == 0) app->settings.sounds_enabled = parse_bool_value(value, app->settings.sounds_enabled);
        else if (strcmp(key, "applet_warning_enabled") == 0) app->settings.applet_warning_enabled = parse_bool_value(value, app->settings.applet_warning_enabled);
        else if (strcmp(key, "trash_delete_enabled") == 0) app->settings.trash_delete_enabled = parse_bool_value(value, app->settings.trash_delete_enabled);
        else if (strcmp(key, "fat32_guard_enabled") == 0) app->settings.fat32_guard_enabled = parse_bool_value(value, app->settings.fat32_guard_enabled);
        else if (strcmp(key, "fat32_oversize_mode") == 0) {
            int mode = atoi(value);
            if (mode >= 0 && mode <= 2) app->settings.fat32_oversize_mode = (Fat32OversizeMode)mode;
        }
    }
    fclose(f);
}

static int save_settings(const AppState *app) {
    if (!app) return -1;
    ensure_app_dirs();
    FILE *f = fopen(SWITCH7ZIP_CONFIG_PATH, "wb");
    if (!f) return -1;
    fprintf(f, "extract_to_folder=%d\n", app->settings.extract_to_folder ? 1 : 0);
    fprintf(f, "overwrite_existing=%d\n", app->settings.overwrite_existing ? 1 : 0);
    fprintf(f, "sounds_enabled=%d\n", app->settings.sounds_enabled ? 1 : 0);
    fprintf(f, "applet_warning_enabled=%d\n", app->settings.applet_warning_enabled ? 1 : 0);
    fprintf(f, "trash_delete_enabled=%d\n", app->settings.trash_delete_enabled ? 1 : 0);
    fprintf(f, "fat32_guard_enabled=%d\n", app->settings.fat32_guard_enabled ? 1 : 0);
    fprintf(f, "fat32_oversize_mode=%d\n", (int)app->settings.fat32_oversize_mode);
    fclose(f);
    return 0;
}

static bool is_root_dir(const char *path) {
    return path && strcmp(path, "sdmc:/") == 0;
}

static int path_join(char *out, size_t out_size, const char *base, const char *name) {
    if (!out || !base || !name) return -1;
    const char *sep = (base[0] && base[strlen(base) - 1] == '/') ? "" : "/";
    int written = snprintf(out, out_size, "%s%s%s", base, sep, name);
    return (written < 0 || (size_t)written >= out_size) ? -1 : 0;
}

static void parent_dir_of(char *path) {
    if (!path || is_root_dir(path)) return;
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] == '/') path[--len] = '\0';
    char *slash = strrchr(path, '/');
    if (!slash) {
        snprintf(path, SWITCH7ZIP_MAX_PATH, "sdmc:/");
        return;
    }
    if (slash <= path + strlen("sdmc:")) {
        snprintf(path, SWITCH7ZIP_MAX_PATH, "sdmc:/");
        return;
    }
    *slash = '\0';
}

static const char *sort_mode_label(SortMode mode) {
    switch (mode) {
        case SORT_NAME: return "NAME";
        case SORT_DATE: return "DATE";
        case SORT_SIZE: return "SIZE";
        case SORT_TYPE: return "TYPE";
        default: return "NAME";
    }
}

static const char *filter_mode_label(FilterMode mode) {
    switch (mode) {
        case FILTER_ALL: return "ALL";
        case FILTER_ARCHIVES: return "ARCHIVES";
        case FILTER_IMAGES: return "IMAGES";
        case FILTER_TEXT: return "TEXT";
        case FILTER_NRO: return "NRO";
        case FILTER_FOLDERS: return "FOLDERS";
        default: return "ALL";
    }
}

static bool ends_with_local_ci(const char *value, const char *suffix) {
    if (!value || !suffix) return false;
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > value_len) return false;
    const char *start = value + value_len - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        if (tolower((unsigned char)start[i]) != tolower((unsigned char)suffix[i])) return false;
    }
    return true;
}

static bool item_matches_filter(FilterMode mode, const BrowserItem *item) {
    if (!item || item->type == ITEM_PARENT) return true;
    switch (mode) {
        case FILTER_ALL: return true;
        case FILTER_ARCHIVES: return item->type == ITEM_ARCHIVE;
        case FILTER_IMAGES: return item->type == ITEM_FILE && nxcmd_file_is_image_like(item->name);
        case FILTER_TEXT: return item->type == ITEM_FILE && nxcmd_file_is_text_like(item->name);
        case FILTER_NRO: return item->type == ITEM_FILE && ends_with_local_ci(item->name, ".nro");
        case FILTER_FOLDERS: return item->type == ITEM_DIRECTORY;
        default: return true;
    }
}

static SortMode g_sort_mode = SORT_NAME;

static int ci_compare(const char *a, const char *b) {
    for (;;) {
        unsigned char ca = (unsigned char)tolower((unsigned char)*a++);
        unsigned char cb = (unsigned char)tolower((unsigned char)*b++);
        if (ca != cb || ca == 0 || cb == 0) return (int)ca - (int)cb;
    }
}

static int type_rank(BrowserItemType type) {
    switch (type) {
        case ITEM_PARENT: return 0;
        case ITEM_DIRECTORY: return 1;
        case ITEM_ARCHIVE: return 2;
        case ITEM_FILE: return 3;
        default: return 4;
    }
}

static int compare_items(const void *pa, const void *pb) {
    const BrowserItem *a = (const BrowserItem *)pa;
    const BrowserItem *b = (const BrowserItem *)pb;
    int ra = type_rank(a->type);
    int rb = type_rank(b->type);
    if (ra != rb) return ra - rb;
    switch (g_sort_mode) {
        case SORT_DATE:
            if (a->mtime != b->mtime) return a->mtime < b->mtime ? 1 : -1;
            break;
        case SORT_SIZE:
            if (a->size != b->size) return a->size < b->size ? 1 : -1;
            break;
        case SORT_TYPE:
            break;
        case SORT_NAME:
        default:
            break;
    }
    return ci_compare(a->name, b->name);
}

static void ensure_app_dirs(void) {
    mkdir_p(SWITCH7ZIP_BASE_DIR);
    mkdir_p(SWITCH7ZIP_INPUT_DIR);
    mkdir_p(SWITCH7ZIP_OUTPUT_DIR);
    mkdir_p(SWITCH7ZIP_COMPRESS_DIR);
    mkdir_p(SWITCH7ZIP_LOG_DIR);
    mkdir_p(SWITCH7ZIP_TRASH_DIR);
}

static bool is_busy(const AppState *app) {
    return app && (app->extracting || app->compressing || app->file_operating || app->benchmarking);
}

static void clamp_cursor(AppState *app) {
    if (app->count == 0) {
        app->cursor = 0;
        app->scroll = 0;
        return;
    }
    if (app->cursor >= app->count) app->cursor = app->count - 1;
    const size_t visible = (LIST_H - 58) / ROW_HEIGHT;
    if (app->cursor < app->scroll) app->scroll = app->cursor;
    if (app->cursor >= app->scroll + visible) app->scroll = app->cursor - visible + 1;
}


static void clear_marks(AppState *app) {
    if (!app) return;
    memset(app->marked, 0, sizeof(app->marked));
    app->marked_count = 0;
}

static bool item_can_mark(const AppState *app, size_t index) {
    if (!app || app->archive_view || index >= app->count) return false;
    return app->items[index].type != ITEM_PARENT;
}

static void toggle_mark_current(AppState *app) {
    if (!app || app->count == 0) return;
    if (!item_can_mark(app, app->cursor)) {
        set_status(app, app->archive_view ? "Multi-select file ops are for SD-card browser items." : "Cannot mark this item.");
        return;
    }
    app->marked[app->cursor] = !app->marked[app->cursor];
    if (app->marked[app->cursor]) app->marked_count++;
    else if (app->marked_count > 0) app->marked_count--;
    set_statusf(app, app->marked[app->cursor] ? "Marked: %s" : "Unmarked: %s", app->items[app->cursor].name);
}

static void select_all_items(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "Select-all is available in the SD-card browser.");
        return;
    }
    clear_marks(app);
    for (size_t i = 0; i < app->count; ++i) {
        if (item_can_mark(app, i)) {
            app->marked[i] = true;
            app->marked_count++;
        }
    }
    set_statusf(app, "Marked %llu item(s).", (unsigned long long)app->marked_count);
}

static size_t gather_selected_paths(AppState *app, char out[FILE_SELECTION_MAX][SWITCH7ZIP_MAX_PATH]) {
    if (!app || app->archive_view) return 0;
    size_t n = 0;
    if (app->marked_count > 0) {
        for (size_t i = 0; i < app->count && n < FILE_SELECTION_MAX; ++i) {
            if (app->marked[i] && item_can_mark(app, i)) {
                copy_cstr(out[n], SWITCH7ZIP_MAX_PATH, app->items[i].path);
                n++;
            }
        }
    } else if (app->count > 0 && item_can_mark(app, app->cursor)) {
        copy_cstr(out[n], SWITCH7ZIP_MAX_PATH, app->items[app->cursor].path);
        n++;
    }
    return n;
}

static bool prompt_text_input(const char *title, const char *initial, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
#ifdef __SWITCH__
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) return false;
    swkbdConfigMakePresetDefault(&kbd);
    if (title) swkbdConfigSetHeaderText(&kbd, title);
    if (initial) swkbdConfigSetInitialText(&kbd, initial);
    rc = swkbdShow(&kbd, out, out_size);
    swkbdClose(&kbd);
    return R_SUCCEEDED(rc) && out[0] != '\0';
#else
    (void)title;
    copy_cstr(out, out_size, initial && *initial ? initial : "New Folder");
    return true;
#endif
}


static void set_archive_title_from_path(AppState *app, const char *path) {
    if (!app || !path) return;
    const char *slash = strrchr(path, '/');
    copy_cstr(app->archive_title, sizeof(app->archive_title), slash ? slash + 1 : path);
}

static void archive_prefix_parent(char *prefix) {
    if (!prefix || !*prefix) return;
    size_t len = strlen(prefix);
    while (len > 0 && prefix[len - 1] == '/') prefix[--len] = '\0';
    char *slash = strrchr(prefix, '/');
    if (!slash) {
        prefix[0] = '\0';
        return;
    }
    slash[1] = '\0';
}

static void scan_archive_dir(AppState *app) {
    if (!app) return;
    clear_marks(app);
    app->count = 0;

    if (app->count < MAX_BROWSER_ITEMS) {
        BrowserItem *up = &app->items[app->count++];
        memset(up, 0, sizeof(*up));
        snprintf(up->name, sizeof(up->name), "..");
        snprintf(up->path, sizeof(up->path), "%s", app->archive_prefix);
        up->type = ITEM_PARENT;
    }

    ArchiveBrowseItem archive_items[MAX_BROWSER_ITEMS];
    size_t archive_count = 0;
    char error[SWITCH7ZIP_STATUS_LEN];
    error[0] = '\0';
    int rc = archive_browse_list_dir(app->archive_path,
                                     app->archive_prefix,
                                     archive_items,
                                     MAX_BROWSER_ITEMS - app->count,
                                     &archive_count,
                                     error,
                                     sizeof(error));
    if (rc != 0) {
        set_statusf(app, "Archive preview failed: %s", error[0] ? error : "unknown error");
        return;
    }

    for (size_t i = 0; i < archive_count && app->count < MAX_BROWSER_ITEMS; ++i) {
        BrowserItem *item = &app->items[app->count++];
        memset(item, 0, sizeof(*item));
        copy_cstr(item->name, sizeof(item->name), archive_items[i].name);
        copy_cstr(item->path, sizeof(item->path), archive_items[i].path);
        item->type = archive_items[i].type == ARCHIVE_BROWSE_DIR ? ITEM_DIRECTORY : ITEM_FILE;
        item->size = archive_items[i].size;
    }

    if (app->count > 1) {
        g_sort_mode = app->sort_mode;
        qsort(app->items + 1, app->count - 1, sizeof(BrowserItem), compare_items);
    }
    clamp_cursor(app);
    if (!is_busy(app)) {
        set_statusf(app, "Previewing %s%s%s", app->archive_title, app->archive_prefix[0] ? ":/" : "", app->archive_prefix);
    }
}

static void scan_current_dir(AppState *app) {
    if (!app->archive_view) clear_marks(app);
    if (app->archive_view) {
        scan_archive_dir(app);
        return;
    }
    app->count = 0;
    ensure_app_dirs();

    DIR *dir = opendir(app->current_dir);
    if (!dir) {
        set_statusf(app, "Cannot open %s: %s", app->current_dir, strerror(errno));
        snprintf(app->current_dir, sizeof(app->current_dir), "sdmc:/");
        dir = opendir(app->current_dir);
        if (!dir) return;
    }

    if (!is_root_dir(app->current_dir) && app->count < MAX_BROWSER_ITEMS) {
        BrowserItem *up = &app->items[app->count++];
        snprintf(up->name, sizeof(up->name), "..");
        snprintf(up->path, sizeof(up->path), "%s", app->current_dir);
        up->type = ITEM_PARENT;
        up->size = 0;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (app->count >= MAX_BROWSER_ITEMS) break;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        BrowserItem item;
        memset(&item, 0, sizeof(item));
        snprintf(item.name, sizeof(item.name), "%s", ent->d_name);
        if (!app->show_hidden && item.name[0] == '.') continue;
        if (path_join(item.path, sizeof(item.path), app->current_dir, ent->d_name) != 0) continue;

        struct stat st;
        if (stat(item.path, &st) == 0) {
            item.size = (uint64_t)st.st_size;
            item.mtime = (uint64_t)st.st_mtime;
            if (S_ISDIR(st.st_mode)) item.type = ITEM_DIRECTORY;
            else if (has_supported_archive_extension(item.name)) item.type = ITEM_ARCHIVE;
            else item.type = ITEM_FILE;
        } else {
            item.type = has_supported_archive_extension(item.name) ? ITEM_ARCHIVE : ITEM_FILE;
        }
        if (!item_matches_filter(app->filter_mode, &item)) continue;
        app->items[app->count++] = item;
    }
    closedir(dir);

    size_t sort_start = (!is_root_dir(app->current_dir) && app->count > 0) ? 1 : 0;
    if (app->count > sort_start) {
        g_sort_mode = app->sort_mode;
        qsort(app->items + sort_start, app->count - sort_start, sizeof(BrowserItem), compare_items);
    }
    clamp_cursor(app);
    remember_recent_dir(app, app->current_dir);
    if (!is_busy(app)) {
        set_statusf(app, "Browsing %s  | Sort %s | Filter %s%s", app->current_dir, sort_mode_label(app->sort_mode), filter_mode_label(app->filter_mode), app->show_hidden ? " | Hidden ON" : "");
    }
}

static const char *type_label(BrowserItemType type) {
    switch (type) {
        case ITEM_PARENT: return "UP";
        case ITEM_DIRECTORY: return "DIR";
        case ITEM_ARCHIVE: return "ARCHIVE";
        case ITEM_FILE: return "FILE";
        default: return "ITEM";
    }
}

static SDL_Color type_color(BrowserItemType type) {
    switch (type) {
        case ITEM_PARENT: return C_AMBER;
        case ITEM_DIRECTORY: return C_ACCENT;
        case ITEM_ARCHIVE: return C_GREEN;
        case ITEM_FILE: return C_MUTED;
        default: return C_TEXT;
    }
}


static void draw_pill(SDL_Renderer *r, int x, int y, const char *text, SDL_Color color) {
    int w = text_width_px(text, 2) + 22;
    fill_rect(r, x + 2, y + 2, w, 26, C_BLACK_A);
    fill_rect(r, x, y, w, 26, C_PANEL_2);
    stroke_rect(r, x, y, w, 26, color);
    draw_text(r, x + 11, y + 6, text, 2, color);
}

static void draw_button_hint(SDL_Renderer *r, int x, int y, const char *button, const char *label, SDL_Color color) {
    int bw = 28;
    fill_rect(r, x, y, bw, 26, color);
    draw_text(r, x + 8, y + 6, button, 2, C_BG);
    draw_text(r, x + bw + 8, y + 6, label, 2, C_TEXT);
}

static void draw_item_icon(SDL_Renderer *r, int x, int y, BrowserItemType type, bool selected) {
    SDL_Color c = type_color(type);
    SDL_Color bg = selected ? C_ACCENT : C_PANEL_3;
    fill_rect(r, x, y, 30, 30, bg);
    stroke_rect(r, x, y, 30, 30, c);
    if (type == ITEM_DIRECTORY) {
        fill_rect(r, x + 5, y + 9, 20, 15, c);
        fill_rect(r, x + 7, y + 6, 9, 5, c);
    } else if (type == ITEM_ARCHIVE) {
        stroke_rect(r, x + 7, y + 6, 16, 18, c);
        fill_rect(r, x + 10, y + 9, 10, 3, c);
        fill_rect(r, x + 10, y + 15, 10, 3, c);
    } else if (type == ITEM_PARENT) {
        draw_text(r, x + 8, y + 7, "UP", 2, c);
    } else {
        stroke_rect(r, x + 8, y + 5, 14, 20, c);
        fill_rect(r, x + 11, y + 11, 8, 2, c);
        fill_rect(r, x + 11, y + 16, 8, 2, c);
    }
}

static void draw_header(AppState *app) {
    SDL_Renderer *r = app->renderer;
    fill_rect(r, 0, 0, SCREEN_W, SCREEN_H, C_BG);
    fill_rect(r, 0, 0, SCREEN_W, TOPBAR_H, C_BG_2);
    draw_accent_bar(r, 0, 0, SCREEN_W, C_ACCENT);

    draw_text(r, 34, 20, "SWITCH7ZIP", 4, C_TEXT);
    draw_text(r, 36, 62, "MODERN ARCHIVE + FILE WORKSPACE", 2, C_MUTED);

    draw_pill(r, 812, 24, APP_VERSION_STRING, C_ACCENT);
    draw_pill(r, 912, 24, app->renderer_accelerated ? "GPU UI" : "SDL UI", app->renderer_accelerated ? C_GREEN : C_AMBER);
    draw_pill(r, 1018, 24, app->applet_mode_alert ? "APPLET" : "FULL RAM", app->applet_mode_alert ? C_AMBER : C_GREEN);
    draw_pill(r, 1138, 24, app->dual_pane_enabled ? "DUAL" : "LOGS", app->dual_pane_enabled ? C_GREEN : C_ACCENT_2);

    /*
     * Keep a visible pre-1.0 warning in the main UI. This project performs
     * destructive file operations, so the app should never look more mature
     * than it currently is. Applet Mode is promoted to the stronger warning
     * because large archives are more likely to fail with reduced memory.
     */
    const char *safety_banner = app->applet_mode_alert
        ? "APPLET MODE DETECTED - LARGE ARCHIVES MAY STOP EARLY. USE TITLE OVERRIDE FOR FULL MEMORY."
        : APP_SAFETY_ALERT;
    fill_card(r, 34, ALERT_Y, SCREEN_W - 68, 32, C_AMBER);
    draw_text_clipped(r, 52, ALERT_Y + 9, safety_banner, 2, C_BG, SCREEN_W - 104);

    fill_card(r, 34, BREADCRUMB_Y, SCREEN_W - 68, 34, C_PANEL);
    draw_text(r, 52, BREADCRUMB_Y + 10, app->archive_view ? "ARCHIVE" : "LOCATION", 2, C_ACCENT);
    char location[SWITCH7ZIP_MAX_PATH + 320];
    if (app->archive_view) {
        snprintf(location, sizeof(location), "%s:/%s", app->archive_title, app->archive_prefix);
    } else {
        snprintf(location, sizeof(location), "%s", app->current_dir);
    }
    draw_text_clipped(r, 176, BREADCRUMB_Y + 10, location, 2, C_TEXT, SCREEN_W - 230);
}

static void draw_browser(AppState *app) {
    SDL_Renderer *r = app->renderer;
    fill_card(r, LIST_X, LIST_Y, LIST_W, LIST_H, C_PANEL);
    draw_accent_bar(r, LIST_X, LIST_Y, LIST_W, C_ACCENT);
    draw_text(r, LIST_X + 18, LIST_Y + 16, app->archive_view ? "ARCHIVE CONTENTS" : "FILES", 3, C_TEXT);
    char view_chip[160];
    snprintf(view_chip, sizeof(view_chip), "SORT:%s  FILTER:%s%s", sort_mode_label(app->sort_mode), filter_mode_label(app->filter_mode), app->show_hidden ? "  HIDDEN" : "");
    draw_text_clipped(r, LIST_X + 382, LIST_Y + 20, view_chip, 2, C_MUTED, 360);

    const size_t visible = (LIST_H - 58) / ROW_HEIGHT;
    size_t end = app->scroll + visible;
    if (end > app->count) end = app->count;

    if (app->count == 0) {
        draw_text(r, LIST_X + 28, LIST_Y + 96, "EMPTY FOLDER", 4, C_MUTED);
        draw_text(r, LIST_X + 30, LIST_Y + 144, "B BACK    - INPUT FOLDER    ZR REFRESH", 2, C_MUTED);
        return;
    }

    int list_top = LIST_Y + 58;
    for (size_t i = app->scroll; i < end; ++i) {
        int row = list_top + (int)(i - app->scroll) * ROW_HEIGHT;
        BrowserItem *item = &app->items[i];
        bool selected = i == app->cursor;
        SDL_Color item_color = type_color(item->type);

        if (selected) {
            fill_rect(r, LIST_X + 10, row + 2, LIST_W - 20, ROW_HEIGHT - 4, C_PANEL_3);
            fill_rect(r, LIST_X + 10, row + 2, 5, ROW_HEIGHT - 4, C_ACCENT);
        }
        if (!app->archive_view && app->marked[i]) {
            fill_rect(r, LIST_X + 18, row + 14, 16, 16, C_GREEN);
            draw_text(r, LIST_X + 21, row + 15, "X", 2, C_BG);
        }
        draw_item_icon(r, LIST_X + 42, row + 7, item->type, selected);
        draw_text_clipped(r, LIST_X + 84, row + 9, item->name, 2, item->type == ITEM_FILE ? C_MUTED : C_TEXT, LIST_W - 273);
        draw_text(r, LIST_X + LIST_W - 184, row + 9, type_label(item->type), 2, item_color);
        if (item->type == ITEM_ARCHIVE || item->type == ITEM_FILE) {
            char bytes[32];
            format_bytes(item->size, bytes, sizeof(bytes));
            draw_text_clipped(r, LIST_X + LIST_W - 96, row + 9, bytes, 2, C_MUTED, 86);
        }
    }

    char count_buf[80];
    if (app->marked_count > 0) snprintf(count_buf, sizeof(count_buf), "%llu ITEMS  /  %llu MARKED", (unsigned long long)app->count, (unsigned long long)app->marked_count);
    else snprintf(count_buf, sizeof(count_buf), "%llu ITEMS", (unsigned long long)app->count);
    draw_text(r, LIST_X + 18, LIST_Y + LIST_H - 28, count_buf, 2, C_MUTED);
    draw_text(r, LIST_X + LIST_W - 248, LIST_Y + LIST_H - 28, "L/R PAGE", 2, C_MUTED);
}

static int current_progress_percent(const AppState *app) {
    if (!app) return -1;
    return percent_from_parts(operation_done_bytes(app), operation_expected_bytes(app));
}

static void draw_progress_bar(SDL_Renderer *r, int x, int y, int w, int h, int percent) {
    fill_rect(r, x, y, w, h, C_BG_2);
    stroke_rect(r, x, y, w, h, C_LINE);
    if (percent >= 0) {
        if (percent > 100) percent = 100;
        int fill_w = (w - 6) * percent / 100;
        if (fill_w > 0) {
            fill_rect(r, x + 3, y + 3, fill_w, h - 6, C_GREEN);
            if (fill_w > 28) fill_rect(r, x + 3, y + 3, fill_w, 4, C_ACCENT);
        }
    } else {
        fill_rect(r, x + 3, y + 3, (w - 6) / 3, h - 6, C_AMBER);
    }
}

static void draw_metric_chip(SDL_Renderer *r, int x, int y, const char *label, const char *value, SDL_Color accent) {
    fill_rect(r, x, y, 168, 42, C_PANEL_2);
    stroke_rect(r, x, y, 168, 42, accent);
    draw_text(r, x + 10, y + 7, label, 2, C_MUTED);
    draw_text_clipped(r, x + 10, y + 24, value, 2, C_TEXT, 148);
}

static void draw_operation_progress(AppState *app, int x, int y, int w) {
    SDL_Renderer *r = app->renderer;
    int percent = current_progress_percent(app);
    int current_percent = percent_from_parts(current_file_done_bytes(app), current_file_expected_bytes(app));
    char line[256];
    char done[48], total[48], current_done[48], current_total[48];
    char rate[48], elapsed[48], eta[48];

    uint64_t done_bytes = operation_done_bytes(app);
    uint64_t total_bytes = operation_expected_bytes(app);
    uint64_t elapsed_ms = operation_elapsed_ms(app);
    uint64_t elapsed_sec = elapsed_ms / 1000ULL;
    uint64_t bytes_per_sec = elapsed_ms > 0 ? (done_bytes * 1000ULL) / elapsed_ms : 0;
    uint64_t remaining_sec = (bytes_per_sec > 0 && total_bytes > done_bytes) ? ((total_bytes - done_bytes) / bytes_per_sec) : 0;

    format_bytes(done_bytes, done, sizeof(done));
    format_bytes(total_bytes, total, sizeof(total));
    format_bytes(current_file_done_bytes(app), current_done, sizeof(current_done));
    format_bytes(current_file_expected_bytes(app), current_total, sizeof(current_total));
    format_rate(bytes_per_sec, rate, sizeof(rate));
    format_duration(elapsed_sec, elapsed, sizeof(elapsed));
    if (remaining_sec > 0 && percent >= 0) format_duration(remaining_sec, eta, sizeof(eta));
    else snprintf(eta, sizeof(eta), "--");

    char op_title[64];
    snprintf(op_title, sizeof(op_title), "%s ACTIVE", operation_label(app->operation));
    draw_text(r, x, y, op_title, 2, C_AMBER);
    y += 28;

    draw_progress_bar(r, x, y, w, 30, percent);
    if (percent >= 0) {
        snprintf(line, sizeof(line), "%d%% OVERALL", percent);
        draw_text(r, x + w - 160, y + 8, line, 2, C_TEXT);
    } else {
        draw_text(r, x + w - 190, y + 8, "SCANNING STREAM", 2, C_AMBER);
    }
    y += 42;

    if (total_bytes > 0) snprintf(line, sizeof(line), "%s / %s processed", done, total);
    else snprintf(line, sizeof(line), "%s processed", done);
    draw_text_clipped(r, x, y, line, 2, C_TEXT, w);
    y += 30;

    draw_metric_chip(r, x, y, "SPEED", rate, C_GREEN);
    draw_metric_chip(r, x + 184, y, "ELAPSED", elapsed, C_ACCENT);
    draw_metric_chip(r, x + 368, y, "ETA", eta, C_AMBER);
    char files[48];
    const char *metric_label = "FILES";
    if (app->operation == OP_EXTRACT || app->operation == OP_TEST) snprintf(files, sizeof(files), "%llu", (unsigned long long)app->live_stats.files_written);
    else if (app->operation == OP_COMPRESS) snprintf(files, sizeof(files), "%llu", (unsigned long long)app->live_compress.files_added);
    else if (app->operation == OP_BENCHMARK) { snprintf(files, sizeof(files), "%s", app->live_benchmark.phase); metric_label = "PHASE"; }
    else snprintf(files, sizeof(files), "%llu", (unsigned long long)app->live_fileop.files_done);
    draw_metric_chip(r, x + 552, y, metric_label, files, C_ACCENT_2);
    y += 58;

    draw_text(r, x, y, "CURRENT FILE", 2, C_MUTED);
    y += 24;
    draw_progress_bar(r, x, y, w, 20, current_percent);
    y += 30;
    if (current_file_expected_bytes(app) > 0) snprintf(line, sizeof(line), "%s / %s", current_done, current_total);
    else snprintf(line, sizeof(line), "%s", current_done);
    draw_text_clipped(r, x, y, line, 2, C_MUTED, w);
    y += 26;

    const char *entry = (app->operation == OP_EXTRACT || app->operation == OP_TEST) ? app->live_stats.last_entry : (app->operation == OP_COMPRESS ? app->live_compress.last_entry : (app->operation == OP_BENCHMARK ? app->live_benchmark.phase : app->live_fileop.last_entry));
    draw_text_clipped(r, x, y, entry, 2, C_MUTED, w);
}


static int make_extract_output_for_item(const AppState *app, const BrowserItem *item, char *out, size_t out_size) {
    if (!app || !item || !out || out_size == 0) return -1;
    const char *source_path = app->archive_view ? app->archive_path : item->path;
    if (app->settings.extract_to_folder) {
        return make_sibling_extract_dir(source_path, out, out_size);
    }
    int written = snprintf(out, out_size, "%s", source_path);
    if (written < 0 || (size_t)written >= out_size) return -1;
    parent_dir_of(out);
    return 0;
}

static void log_multipart_diagnostics(const MultipartInfo *mi) {
    if (!mi || !mi->is_multipart) return;
    switch7zip_log_line("multipart: selected_index=%u first=%d missing=%d count=%u highest=%u first_missing=%u first_part=%s message=%s",
                        mi->selected_index,
                        mi->selected_first_part ? 1 : 0,
                        mi->missing_parts ? 1 : 0,
                        mi->part_count,
                        mi->highest_index,
                        mi->first_missing_index,
                        mi->first_part_path,
                        mi->message);
}

static bool preflight_archive_for_extract(AppState *app,
                                          const char *source_archive,
                                          const char *selected_member,
                                          const char *output_dir) {
    if (!app || !source_archive || !output_dir) return false;

    MultipartInfo mi;
    if (nxcmd_check_multipart_archive(source_archive, &mi) == 0 && mi.is_multipart) {
        log_multipart_diagnostics(&mi);
        if (!mi.selected_first_part) {
            set_statusf(app, "%s", mi.message);
            return false;
        }
        if (mi.missing_parts) {
            set_statusf(app, "%s", mi.message);
            return false;
        }
    }

    ArchiveEstimate estimate;
    char error[SWITCH7ZIP_STATUS_LEN];
    error[0] = '\0';
    if (nxcmd_estimate_archive_unpacked_size(source_archive, selected_member, &estimate, error, sizeof(error)) == 0) {
        uint64_t free_bytes = nxcmd_free_space_for_path(output_dir);
        char needed[48], free_s[48];
        format_bytes(estimate.bytes, needed, sizeof(needed));
        format_bytes(free_bytes, free_s, sizeof(free_s));
        switch7zip_log_line("preflight extract: estimated_unpacked=%llu files=%llu dirs=%llu free=%llu largest_file=%llu fat32_over_limit=%llu destination=%s",
                            (unsigned long long)estimate.bytes,
                            (unsigned long long)estimate.files,
                            (unsigned long long)estimate.dirs,
                            (unsigned long long)free_bytes,
                            (unsigned long long)estimate.largest_file_bytes,
                            (unsigned long long)estimate.files_over_fat32_limit,
                            output_dir);
        if (estimate.files_over_fat32_limit > 0) {
            char largest[48];
            format_bytes(estimate.largest_file_bytes, largest, sizeof(largest));
            const char *first_big = estimate.first_file_over_fat32_limit[0] ? estimate.first_file_over_fat32_limit : estimate.largest_file_path;
            switch7zip_log_line("FAT32 oversized files detected: mode=%s oversized_files=%llu largest=%s first=%s",
                            fat32_mode_label(app->settings.fat32_oversize_mode),
                            (unsigned long long)estimate.files_over_fat32_limit,
                            largest,
                            first_big);
            if (app->settings.fat32_oversize_mode == FAT32_OVERSIZE_BLOCK) {
                set_statusf(app, "FAT32 limit: %llu file(s) exceed 4 GiB. First: %s",
                            (unsigned long long)estimate.files_over_fat32_limit,
                            first_big);
                return false;
            }
            set_statusf(app, "FAT32 mode %s will handle %llu oversized file(s).",
                        fat32_mode_label(app->settings.fat32_oversize_mode),
                        (unsigned long long)estimate.files_over_fat32_limit);
        }
        if (free_bytes > 0 && estimate.bytes > 0 && estimate.bytes + (64ULL * 1024ULL * 1024ULL) > free_bytes) {
            set_statusf(app, "Not enough free space: need about %s, free %s.", needed, free_s);
            return false;
        }
        set_statusf(app, "Preflight OK: about %s to extract, free %s.", needed, free_s);
    } else {
        switch7zip_log_line("preflight estimate failed: %s", error[0] ? error : "unknown error");
        set_status(app, "Preflight size estimate unavailable; continuing with logs enabled.");
    }
    return true;
}

static bool preflight_source_to_destination(AppState *app,
                                            const char *source_path,
                                            const char *destination_path,
                                            const char *label) {
    PathSizeInfo info;
    if (nxcmd_measure_path_tree(source_path, &info) != 0 && info.bytes == 0 && info.files == 0) {
        switch7zip_log_line("preflight %s: could not fully measure %s", label ? label : "operation", source_path);
        return true;
    }
    uint64_t free_bytes = nxcmd_free_space_for_path(destination_path);
    char needed[48], free_s[48];
    format_bytes(info.bytes, needed, sizeof(needed));
    format_bytes(free_bytes, free_s, sizeof(free_s));
    switch7zip_log_line("preflight %s: source=%s bytes=%llu files=%llu dirs=%llu free=%llu dest=%s",
                        label ? label : "operation", source_path,
                        (unsigned long long)info.bytes,
                        (unsigned long long)info.files,
                        (unsigned long long)info.dirs,
                        (unsigned long long)free_bytes,
                        destination_path ? destination_path : "");
    if (free_bytes > 0 && info.bytes > 0 && info.bytes + (32ULL * 1024ULL * 1024ULL) > free_bytes) {
        set_statusf(app, "Not enough free space for %s: need %s, free %s.", label ? label : "operation", needed, free_s);
        return false;
    }
    return true;
}


static void draw_details(AppState *app) {
    SDL_Renderer *r = app->renderer;
    fill_card(r, DETAILS_X, DETAILS_Y, DETAILS_W, DETAILS_H, C_PANEL);
    draw_accent_bar(r, DETAILS_X, DETAILS_Y, DETAILS_W, C_ACCENT_2);
    draw_text(r, DETAILS_X + 18, DETAILS_Y + 16, "INSPECTOR", 3, C_TEXT);

    int y = DETAILS_Y + 62;
    if (app->count == 0) {
        draw_text(r, DETAILS_X + 22, y, "NO SELECTION", 3, C_MUTED);
        return;
    }

    BrowserItem *item = &app->items[app->cursor];
    if (!app->archive_view && app->marked_count > 0) {
        char sel[96];
        snprintf(sel, sizeof(sel), "%llu MARKED  |  CLIPBOARD %llu %s",
                 (unsigned long long)app->marked_count,
                 (unsigned long long)app->clipboard_count,
                 app->clipboard_move ? "MOVE" : "COPY");
        draw_text_clipped(r, DETAILS_X + 22, y, sel, 2, C_GREEN, DETAILS_W - 44);
        y += 32;
    }
    draw_item_icon(r, DETAILS_X + 22, y, item->type, true);
    draw_text(r, DETAILS_X + 64, y + 2, type_label(item->type), 2, type_color(item->type));
    draw_text_clipped(r, DETAILS_X + 64, y + 24, item->name, 2, C_TEXT, DETAILS_W - 90);
    y += 64;

    if (item->type == ITEM_ARCHIVE || item->type == ITEM_FILE) {
        char bytes[48];
        format_bytes(item->size, bytes, sizeof(bytes));
        fill_rect(r, DETAILS_X + 22, y, DETAILS_W - 44, 42, C_PANEL_2);
        draw_text(r, DETAILS_X + 34, y + 12, "SIZE", 2, C_MUTED);
        draw_text_clipped(r, DETAILS_X + 126, y + 12, bytes, 2, C_TEXT, DETAILS_W - 160);
        y += 54;
    }

    draw_text(r, DETAILS_X + 22, y, app->archive_view ? "INTERNAL PATH" : "PATH", 2, C_MUTED);
    y += 24;
    draw_text_clipped(r, DETAILS_X + 22, y, item->path, 2, C_TEXT, DETAILS_W - 44);
    y += 44;

    if (item->type == ITEM_ARCHIVE || item->type == ITEM_FILE || (app->archive_view && item->type == ITEM_DIRECTORY)) {
        char out[SWITCH7ZIP_MAX_PATH];
        if (item->type != ITEM_PARENT && make_extract_output_for_item(app, item, out, sizeof(out)) == 0) {
            draw_text(r, DETAILS_X + 22, y, app->settings.extract_to_folder ? "EXTRACT DESTINATION" : "EXTRACT HERE", 2, C_MUTED);
            y += 24;
            draw_text_clipped(r, DETAILS_X + 22, y, out, 2, app->archive_view || item->type == ITEM_ARCHIVE ? C_GREEN : C_AMBER, DETAILS_W - 44);
            y += 40;
        }
        draw_button_hint(r, DETAILS_X + 22, y, "X", app->archive_view ? "EXTRACT SELECTED" : "EXTRACT", C_AMBER);
        if (!app->archive_view) draw_button_hint(r, DETAILS_X + 252, y, "+", "TEST/MENU", C_GREEN);
    } else if (item->type == ITEM_DIRECTORY || item->type == ITEM_PARENT) {
        draw_button_hint(r, DETAILS_X + 22, y, "A", "OPEN", C_ACCENT);
        if (!app->archive_view) draw_button_hint(r, DETAILS_X + 174, y, "Y", "MARK", C_GREEN);
    }

    y = DETAILS_Y + DETAILS_H - 132;
    draw_text(r, DETAILS_X + 22, y, app->dual_pane_enabled ? "TARGET PANE" : "TARGET PANE (HIDDEN)", 2, app->dual_pane_enabled ? C_GREEN : C_MUTED);
    y += 24;
    draw_text_clipped(r, DETAILS_X + 22, y, app->other_dir[0] ? app->other_dir : "not set", 2, app->dual_pane_enabled ? C_TEXT : C_MUTED, DETAILS_W - 44);

    y = DETAILS_Y + DETAILS_H - 72;
    draw_text(r, DETAILS_X + 22, y, "LATEST LOG", 2, C_MUTED);
    y += 24;
    draw_text_clipped(r, DETAILS_X + 22, y, switch7zip_log_path(), 2, C_MUTED, DETAILS_W - 44);
}

static void draw_status_drawer(AppState *app) {
    SDL_Renderer *r = app->renderer;
    fill_card(r, LIST_X, STATUS_Y, SCREEN_W - 68, 38, is_busy(app) ? C_PANEL_2 : C_PANEL);
    draw_text(r, LIST_X + 18, STATUS_Y + 12, is_busy(app) ? "ACTIVE" : "STATUS", 2, is_busy(app) ? C_AMBER : C_ACCENT);
    draw_text_clipped(r, LIST_X + 130, STATUS_Y + 12, app->status, 2, is_busy(app) ? C_AMBER : C_TEXT, SCREEN_W - 220);
}

static void refresh_text_view_from_path(AppState *app, const char *path, const char *title) {
    if (!app) return;
    app->log_line_count = 0;
    for (size_t i = 0; i < LOG_VIEW_LINES; ++i) app->log_lines[i][0] = '\0';
    copy_cstr(app->text_view_path, sizeof(app->text_view_path), path ? path : switch7zip_log_path());
    copy_cstr(app->text_view_title, sizeof(app->text_view_title), title ? title : "TEXT VIEWER");

    FILE *f = fopen(app->text_view_path, "rb");
    if (!f) {
        copy_cstr(app->log_lines[0], sizeof(app->log_lines[0]), "Could not open file.");
        copy_cstr(app->log_lines[1], sizeof(app->log_lines[1]), app->text_view_path);
        app->log_line_count = 2;
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (app->log_line_count < LOG_VIEW_LINES) {
            copy_cstr(app->log_lines[app->log_line_count], sizeof(app->log_lines[app->log_line_count]), line);
            app->log_line_count++;
        } else {
            memmove(app->log_lines[0], app->log_lines[1], (LOG_VIEW_LINES - 1) * sizeof(app->log_lines[0]));
            copy_cstr(app->log_lines[LOG_VIEW_LINES - 1], sizeof(app->log_lines[LOG_VIEW_LINES - 1]), line);
        }
    }
    fclose(f);

    if (app->log_line_count == 0) {
        snprintf(app->log_lines[0], sizeof(app->log_lines[0]), "File exists but is empty.");
        app->log_line_count = 1;
    }
}

static void refresh_log_view(AppState *app) {
    refresh_text_view_from_path(app, switch7zip_log_path(), "LATEST OPERATION LOG");
}

static void draw_log_overlay(AppState *app) {
    if (!app || !app->log_view_open) return;
    SDL_Renderer *r = app->renderer;
    fill_rect(r, 0, 0, SCREEN_W, SCREEN_H, C_BLACK_A);
    fill_card(r, 86, 84, 1108, 548, C_PANEL);
    draw_accent_bar(r, 86, 84, 1108, C_ACCENT_2);
    draw_text(r, 118, 112, app->text_view_title[0] ? app->text_view_title : "TEXT VIEWER", 3, C_TEXT);
    draw_text_clipped(r, 118, 154, app->text_view_path[0] ? app->text_view_path : switch7zip_log_path(), 2, C_MUTED, 1030);

    int y = 198;
    for (size_t i = 0; i < app->log_line_count && i < LOG_VIEW_LINES; ++i) {
        draw_text_clipped(r, 118, y, app->log_lines[i], 2, C_TEXT, 1030);
        y += 28;
    }

    fill_rect(r, 118, 576, 1020, 34, C_PANEL_2);
    draw_button_hint(r, 136, 580, "B", "CLOSE", C_ACCENT_2);
    draw_button_hint(r, 294, 580, "ZL", "CLOSE", C_ACCENT_2);
    draw_button_hint(r, 474, 580, "Y", "REFRESH", C_GREEN);
}

static void draw_image_overlay(AppState *app) {
    if (!app || !app->image_view_open) return;
    SDL_Renderer *r = app->renderer;
    fill_rect(r, 0, 0, SCREEN_W, SCREEN_H, C_BLACK_A);
    fill_card(r, 74, 50, 1132, 620, C_PANEL);
    draw_accent_bar(r, 74, 50, 1132, C_GREEN);
    draw_text(r, 108, 78, app->image_title[0] ? app->image_title : "IMAGE VIEWER", 3, C_TEXT);
    draw_text_clipped(r, 108, 118, app->image_path, 2, C_MUTED, 1038);

    int box_x = 108;
    int box_y = 152;
    int box_w = 1064;
    int box_h = 418;
    fill_rect(r, box_x, box_y, box_w, box_h, C_BG_2);
    stroke_rect(r, box_x, box_y, box_w, box_h, C_LINE);

    if (app->image_texture) {
        float sx = (float)box_w / (float)app->image_w;
        float sy = (float)box_h / (float)app->image_h;
        float base = sx < sy ? sx : sy;
        if (base > 1.0f) base = 1.0f;
        float zoom = app->image_zoom <= 0.1f ? 0.1f : app->image_zoom;
        float sc = base * zoom;
        int dw = (int)((float)app->image_w * sc);
        int dh = (int)((float)app->image_h * sc);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;

        int dst_x = box_x + (box_w - dw) / 2 + app->image_pan_x;
        int dst_y = box_y + (box_h - dh) / 2 + app->image_pan_y;
        SDL_Rect clip = { box_x + 1, box_y + 1, box_w - 2, box_h - 2 };
        SDL_Rect dst = { dst_x, dst_y, dw, dh };
        SDL_RenderSetClipRect(r, &clip);
        SDL_RenderCopy(r, app->image_texture, NULL, &dst);
        SDL_RenderSetClipRect(r, NULL);

        char meta[160];
        snprintf(meta, sizeof(meta), "%d x %d   zoom %.1fx   pan %+d,%+d", app->image_w, app->image_h, zoom, app->image_pan_x, app->image_pan_y);
        draw_text(r, 108, 584, meta, 2, C_MUTED);
    } else {
        draw_text(r, 132, 184, "IMAGE NOT RENDERED", 4, C_AMBER);
        draw_text_clipped(r, 132, 242, app->image_error[0] ? app->image_error : "Unsupported or damaged image file.", 2, C_TEXT, 1000);
        draw_text(r, 132, 288, "Image viewer uses SDL2_image for BMP, PNG, JPG/JPEG and other enabled formats.", 2, C_MUTED);
        draw_text(r, 132, 320, "Install switch-sdl2_image if the build cannot find SDL_image.h or -lSDL2_image.", 2, C_MUTED);
    }

    fill_rect(r, 108, 612, 1040, 34, C_PANEL_2);
    draw_button_hint(r, 126, 616, "B", "CLOSE", C_ACCENT_2);
    draw_button_hint(r, 278, 616, "L/R", "ZOOM", C_GREEN);
    draw_button_hint(r, 460, 616, "D-PAD", "PAN", C_ACCENT);
    draw_button_hint(r, 682, 616, "X", "RESET", C_AMBER);
    draw_button_hint(r, 840, 616, "Y", "RELOAD", C_GREEN);
}





static const char *bookmark_paths[] = {
    "sdmc:/",
    "sdmc:/switch",
    "sdmc:/atmosphere",
    "sdmc:/config",
    "sdmc:/downloads",
    "sdmc:/Nintendo",
    SWITCH7ZIP_BASE_DIR,
    SWITCH7ZIP_LOG_DIR,
    SWITCH7ZIP_TRASH_DIR
};

static size_t bookmark_count(void) {
    return sizeof(bookmark_paths) / sizeof(bookmark_paths[0]);
}

static void draw_properties_overlay(AppState *app) {
    if (!app || app->overlay != OVERLAY_PROPERTIES) return;
    SDL_Renderer *r = app->renderer;
    fill_rect(r, 0, 0, SCREEN_W, SCREEN_H, C_BLACK_A);
    fill_card(r, 150, 82, 980, 556, C_PANEL);
    draw_accent_bar(r, 150, 82, 980, C_GREEN);
    draw_text(r, 184, 112, "PROPERTIES + DIAGNOSTICS", 3, C_TEXT);
    int y = 166;
    for (size_t i = 0; i < app->property_line_count && i < 14; ++i) {
        SDL_Color c = (strstr(app->property_lines[i], "WARNING") || strstr(app->property_lines[i], "Not enough")) ? C_AMBER : C_TEXT;
        draw_text_clipped(r, 184, y, app->property_lines[i], 2, c, 910);
        y += 30;
    }
    fill_rect(r, 184, 584, 900, 34, C_PANEL_2);
    draw_button_hint(r, 202, 588, "B", "BACK", C_ACCENT_2);
    draw_button_hint(r, 348, 588, "+", "MENU", C_ACCENT);
}

static void draw_bookmarks_overlay(AppState *app) {
    if (!app || app->overlay != OVERLAY_BOOKMARKS) return;
    SDL_Renderer *r = app->renderer;
    fill_rect(r, 0, 0, SCREEN_W, SCREEN_H, C_BLACK_A);
    fill_card(r, 294, 82, 692, 556, C_PANEL);
    draw_accent_bar(r, 294, 82, 692, C_ACCENT);
    draw_text(r, 326, 112, "BOOKMARKS + RECENT PATHS", 3, C_TEXT);
    draw_text(r, 326, 146, "Static shortcuts first; recently visited folders below.", 2, C_MUTED);
    int y = 182;
    size_t total = bookmark_total_count(app);
    if (total == 0) total = bookmark_count();
    for (size_t i = 0; i < total; ++i) {
        const char *path = NULL;
        bool recent = false;
        if (!get_bookmark_or_recent_path(app, i, &path, &recent)) continue;
        bool selected = i == app->bookmark_cursor;
        fill_rect(r, 326, y, 628, 36, selected ? C_PANEL_3 : C_PANEL_2);
        if (selected) fill_rect(r, 326, y, 6, 36, recent ? C_GREEN : C_ACCENT);
        draw_text(r, 346, y + 10, recent ? "REC" : "PIN", 2, recent ? C_GREEN : C_ACCENT);
        draw_text_clipped(r, 400, y + 10, path, 2, selected ? C_TEXT : C_MUTED, 540);
        y += 42;
        if (y > 556) break;
    }
    fill_rect(r, 326, 584, 628, 34, C_PANEL_2);
    draw_button_hint(r, 344, 588, "A", "GO", C_ACCENT);
    draw_button_hint(r, 478, 588, "B", "BACK", C_ACCENT_2);
}

static const char *context_action_label(ContextAction action) {
    switch (action) {
        case CTX_OPEN: return "Open / Select";
        case CTX_MARK: return "Mark / unmark item";
        case CTX_SELECT_ALL: return "Select all in folder";
        case CTX_CLEAR_SELECTION: return "Clear selection";
        case CTX_EXTRACT: return "Extract archive";
        case CTX_TEST: return "Test archive integrity";
        case CTX_COMPRESS: return "Compress to ZIP";
        case CTX_COPY: return "Copy selection";
        case CTX_MOVE: return "Move selection";
        case CTX_PASTE: return "Paste here";
        case CTX_TRASH: return "Move to Trash";
        case CTX_NEW_FOLDER: return "New folder";
        case CTX_NEW_FILE: return "New file";
        case CTX_RENAME: return "Rename selected";
        case CTX_PROPERTIES: return "Properties / diagnostics";
        case CTX_HOMEBREW_INFO: return "Homebrew/NACP info";
        case CTX_COMPARE_TARGET: return "Compare with target pane";
        case CTX_VIEW_TEXT: return "View text/log file";
        case CTX_EDIT_TEXT: return "Edit small text file";
        case CTX_HEX_VIEW: return "Hex viewer";
        case CTX_VIEW_IMAGE: return "View image file";
        case CTX_FIND: return "Find in current folder";
        case CTX_SORT_NEXT: return "Cycle sorting";
        case CTX_FILTER_NEXT: return "Cycle filter";
        case CTX_TOGGLE_HIDDEN: return "Toggle hidden files";
        case CTX_ARCHIVE_BIT: return "Set archive bit";
        case CTX_TOGGLE_DUAL_PANE: return "Toggle dual-pane workspace";
        case CTX_SWAP_PANES: return "Swap active/target pane";
        case CTX_SET_OTHER_PANE: return "Set target pane here";
        case CTX_PASTE_OTHER_PANE: return "Paste to target pane";
        case CTX_RESTORE_TRASH: return "Restore from Trash";
        case CTX_EMPTY_TRASH: return "Empty Trash";
        case CTX_CLEAN_PARTIALS: return "Clean .partial files";
        case CTX_EXPORT_DIAGNOSTICS: return "Export diagnostic bundle";
        case CTX_BOOKMARKS: return "Bookmarks / quick paths";
        case CTX_SD_BENCHMARK: return "SD-card benchmark";
        case CTX_REFRESH: return "Refresh folder";
        case CTX_JOBS: return "Job center";
        case CTX_LOGS: return "Open latest log";
        case CTX_SETTINGS: return "Settings";
        case CTX_EXIT: return "Exit app";
        default: return "Action";
    }
}

static const char *context_action_hint(ContextAction action, const AppState *app) {
    switch (action) {
        case CTX_OPEN: return "Open folders, select files, or preview archives.";
        case CTX_MARK: return app && app->marked_count ? "Toggle the highlighted item in the multi-selection." : "Mark the highlighted item for batch file operations.";
        case CTX_SELECT_ALL: return "Mark all normal items in the current SD-card folder.";
        case CTX_CLEAR_SELECTION: return "Unmark all selected items.";
        case CTX_EXTRACT: return "Extract selected archive-like file using current settings.";
        case CTX_TEST: return "Read the whole archive without writing files to detect CRC/read errors first.";
        case CTX_COMPRESS: return "Create a ZIP beside the selected file or folder.";
        case CTX_COPY: return "Copy selected/current SD-card item(s) to the clipboard.";
        case CTX_MOVE: return "Move selected/current SD-card item(s) after Paste here.";
        case CTX_PASTE: return "Paste clipboard item(s) into the current folder.";
        case CTX_TRASH: return "Move selected/current item(s) to /switch/Switch7zip/.trash.";
        case CTX_NEW_FOLDER: return "Create a folder here using the Switch keyboard.";
        case CTX_NEW_FILE: return "Create an empty file here using the Switch keyboard.";
        case CTX_RENAME: return "Rename the highlighted item using the Switch keyboard.";
        case CTX_PROPERTIES: return "Show size, free space, archive estimate, and multipart diagnostics.";
        case CTX_HOMEBREW_INFO: return "Inspect selected .nro/.nacp metadata, magic bytes, title, author, and version where available.";
        case CTX_COMPARE_TARGET: return "Compare the current folder against the target pane and show top-level differences.";
        case CTX_VIEW_TEXT: return "Open readable text/log/config files in the built-in viewer.";
        case CTX_EDIT_TEXT: return "Edit small text/config files and write a .bak backup before saving.";
        case CTX_HEX_VIEW: return "Open the selected file in a read-only hex/ASCII preview.";
        case CTX_VIEW_IMAGE: return "Open BMP/PNG/JPG images in the built-in viewer, with zoom and pan controls.";
        case CTX_FIND: return "Search visible items in the current folder by name.";
        case CTX_SORT_NEXT: return "Switch between name, date, size, and type sorting.";
        case CTX_FILTER_NEXT: return "Filter by all, archives, images, text files, NROs, or folders.";
        case CTX_TOGGLE_HIDDEN: return "Show or hide dotfiles such as .trash.";
        case CTX_ARCHIVE_BIT: return "Set the Switch concatenation/archive bit on selected folder(s).";
        case CTX_TOGGLE_DUAL_PANE: return "Show or hide the second workspace pane path.";
        case CTX_SWAP_PANES: return "Make the target pane active and keep the old path as the target.";
        case CTX_SET_OTHER_PANE: return "Use the current folder as the target pane for copy/move/paste.";
        case CTX_PASTE_OTHER_PANE: return "Paste clipboard items directly into the target pane.";
        case CTX_RESTORE_TRASH: return "Move selected Trash item(s) back to the last normal folder.";
        case CTX_EMPTY_TRASH: return "Permanently remove all items currently inside /switch/Switch7zip/.trash.";
        case CTX_CLEAN_PARTIALS: return "Remove .partial files left by cancelled or failed large operations in this folder tree.";
        case CTX_EXPORT_DIAGNOSTICS: return "Write version, paths, mode, selected item, latest status, and failed-operation report path.";
        case CTX_BOOKMARKS: return "Jump to common Switch folders such as /switch and /atmosphere.";
        case CTX_SD_BENCHMARK: return "Write and read a temporary file to estimate SD-card throughput.";
        case CTX_REFRESH: return "Rescan the current SD-card folder.";
        case CTX_JOBS: return "View active job state and recent operation history.";
        case CTX_LOGS: return "View /switch/Switch7zip/logs/latest.log.";
        case CTX_SETTINGS: return "Change extraction, overwrite, alert, and warning behavior.";
        case CTX_EXIT: return "Close Switch 7zip.";
        default: return "";
    }
}

static const char *fat32_mode_label(Fat32OversizeMode mode) {
    switch (mode) {
        case FAT32_OVERSIZE_SPLIT_PARTS: return "SPLIT";
        case FAT32_OVERSIZE_SWITCH_CONCAT: return "CONCAT";
        case FAT32_OVERSIZE_BLOCK:
        default: return "BLOCK";
    }
}

static const char *setting_label(SettingRow row) {
    switch (row) {
        case SETTING_EXTRACT_FOLDER: return "Extract into archive-named folder";
        case SETTING_OVERWRITE: return "Overwrite existing files";
        case SETTING_SOUNDS: return "Done / failed sounds";
        case SETTING_APPLET_WARN: return "Applet Mode warning";
        case SETTING_TRASH_DELETE: return "Delete uses Trash folder";
        case SETTING_FAT32_MODE: return "FAT32 >4 GiB handling";
        default: return "Setting";
    }
}

static bool setting_value(const AppState *app, SettingRow row) {
    if (!app) return false;
    switch (row) {
        case SETTING_EXTRACT_FOLDER: return app->settings.extract_to_folder;
        case SETTING_OVERWRITE: return app->settings.overwrite_existing;
        case SETTING_SOUNDS: return app->settings.sounds_enabled;
        case SETTING_APPLET_WARN: return app->settings.applet_warning_enabled;
        case SETTING_TRASH_DELETE: return app->settings.trash_delete_enabled;
        case SETTING_FAT32_MODE: return true;
        default: return false;
    }
}

static void toggle_setting(AppState *app, SettingRow row) {
    if (!app) return;
    switch (row) {
        case SETTING_EXTRACT_FOLDER: app->settings.extract_to_folder = !app->settings.extract_to_folder; break;
        case SETTING_OVERWRITE: app->settings.overwrite_existing = !app->settings.overwrite_existing; break;
        case SETTING_SOUNDS: app->settings.sounds_enabled = !app->settings.sounds_enabled; break;
        case SETTING_APPLET_WARN: app->settings.applet_warning_enabled = !app->settings.applet_warning_enabled; break;
        case SETTING_TRASH_DELETE: app->settings.trash_delete_enabled = !app->settings.trash_delete_enabled; break;
        case SETTING_FAT32_MODE:
            app->settings.fat32_oversize_mode = (Fat32OversizeMode)(((int)app->settings.fat32_oversize_mode + 1) % 3);
            app->settings.fat32_guard_enabled = true;
            break;
        default: break;
    }
    if (save_settings(app) == 0) set_status(app, "Settings saved.");
    else set_status(app, "Settings changed, but config save failed.");
}


static void draw_jobs_overlay(AppState *app) {
    if (!app || app->overlay != OVERLAY_JOBS) return;
    SDL_Renderer *r = app->renderer;
    fill_rect(r, 0, 0, SCREEN_W, SCREEN_H, C_BLACK_A);
    fill_card(r, 118, 76, 1044, 566, C_PANEL);
    draw_accent_bar(r, 118, 76, 1044, C_AMBER);
    draw_text(r, 150, 106, "JOB CENTER", 3, C_TEXT);
    draw_text(r, 150, 146, "Recent extract/compress operations. B closes. During jobs, press B or + to request cancel.", 2, C_MUTED);

    int y = 194;
    if (is_busy(app)) {
        fill_rect(r, 150, y, 980, 92, C_PANEL_3);
        fill_rect(r, 150, y, 6, 92, C_AMBER);
        char active_title[96];
        snprintf(active_title, sizeof(active_title), "ACTIVE %s", operation_label(app->operation));
        draw_text(r, 170, y + 12, active_title, 2, C_AMBER);
        int pct = current_progress_percent(app);
        draw_progress_bar(r, 170, y + 40, 740, 24, pct);
        char meta[160];
        char done[48], total[48];
        format_bytes(operation_done_bytes(app), done, sizeof(done));
        format_bytes(operation_expected_bytes(app), total, sizeof(total));
        if (pct >= 0) snprintf(meta, sizeof(meta), "%d%%  %s / %s", pct, done, total);
        else snprintf(meta, sizeof(meta), "%s processed", done);
        draw_text_clipped(r, 926, y + 44, meta, 2, C_TEXT, 188);
        const char *active_entry = (app->operation == OP_EXTRACT || app->operation == OP_TEST) ? app->live_stats.last_entry : (app->operation == OP_COMPRESS ? app->live_compress.last_entry : app->live_fileop.last_entry);
        draw_text_clipped(r, 170, y + 68, active_entry, 2, C_MUTED, 920);
        y += 114;
    }

    draw_text(r, 150, y, "HISTORY", 2, C_ACCENT);
    y += 28;
    if (app->job_history_count == 0) {
        draw_text(r, 150, y + 20, "No completed jobs yet.", 3, C_MUTED);
    } else {
        for (size_t i = 0; i < app->job_history_count; ++i) {
            const JobRecord *job = &app->job_history[app->job_history_count - 1 - i];
            SDL_Color c = job_status_color(job->status);
            fill_rect(r, 150, y, 980, 52, C_PANEL_2);
            fill_rect(r, 150, y, 5, 52, c);
            char title[180];
            snprintf(title, sizeof(title), "#%llu %s - %s",
                     (unsigned long long)job->id, operation_label(job->kind), job_status_label(job->status));
            draw_text_clipped(r, 168, y + 8, title, 2, c, 300);
            draw_text_clipped(r, 470, y + 8, job->title, 2, C_TEXT, 248);
            char bytes[64], elapsed[48], files[64];
            format_bytes(job->bytes_done, bytes, sizeof(bytes));
            format_duration(job->elapsed_ms / 1000ULL, elapsed, sizeof(elapsed));
            snprintf(files, sizeof(files), "%llu files", (unsigned long long)job->files_done);
            draw_text_clipped(r, 730, y + 8, bytes, 2, C_MUTED, 124);
            draw_text_clipped(r, 864, y + 8, files, 2, C_MUTED, 112);
            draw_text_clipped(r, 990, y + 8, elapsed, 2, C_MUTED, 110);
            draw_text_clipped(r, 168, y + 30, job->source, 2, C_MUTED, 930);
            y += 60;
            if (y > 556) break;
        }
    }

    fill_rect(r, 150, 594, 980, 34, C_PANEL_2);
    draw_button_hint(r, 168, 598, "B", "CLOSE", C_ACCENT_2);
    draw_button_hint(r, 330, 598, "ZL", "LATEST LOG", C_ACCENT);
}

static void draw_context_menu_overlay(AppState *app) {
    if (!app || app->overlay != OVERLAY_CONTEXT_MENU) return;
    SDL_Renderer *r = app->renderer;
    fill_rect(r, 0, 0, SCREEN_W, SCREEN_H, C_BLACK_A);
    fill_card(r, 300, 38, 680, 642, C_PANEL);
    draw_accent_bar(r, 300, 38, 680, C_ACCENT);
    draw_text(r, 332, 68, "ACTION MENU", 3, C_TEXT);
    if (app->count > 0) {
        BrowserItem *item = &app->items[app->cursor];
        draw_text_clipped(r, 332, 110, item->name, 2, C_MUTED, 616);
    } else {
        draw_text(r, 332, 110, "No item selected", 2, C_MUTED);
    }

    int y = 144;
    const size_t visible = 16;
    size_t start = 0;
    if (CTX_COUNT > visible) {
        start = app->context_cursor > visible / 2 ? app->context_cursor - visible / 2 : 0;
        if (start + visible > CTX_COUNT) start = CTX_COUNT - visible;
    }
    for (size_t row_i = 0; row_i < visible && start + row_i < CTX_COUNT; ++row_i) {
        size_t i = start + row_i;
        bool selected = i == app->context_cursor;
        SDL_Color row = selected ? C_PANEL_3 : C_PANEL_2;
        fill_rect(r, 332, y, 616, 25, row);
        if (selected) fill_rect(r, 332, y, 6, 25, C_ACCENT);
        draw_text(r, 350, y + 6, context_action_label((ContextAction)i), 2, selected ? C_TEXT : C_MUTED);
        y += 28;
    }
    char page[64];
    snprintf(page, sizeof(page), "%u / %u", (unsigned)(app->context_cursor + 1), (unsigned)CTX_COUNT);
    draw_text(r, 846, 116, page, 2, C_MUTED);

    fill_rect(r, 332, 610, 616, 36, C_PANEL_2);
    draw_text_clipped(r, 348, 622, context_action_hint((ContextAction)app->context_cursor, app), 2, C_MUTED, 584);
    draw_button_hint(r, 332, 652, "A", "SELECT", C_ACCENT);
    draw_button_hint(r, 508, 652, "B", "CLOSE", C_ACCENT_2);
}

static void draw_settings_overlay(AppState *app) {
    if (!app || app->overlay != OVERLAY_SETTINGS) return;
    SDL_Renderer *r = app->renderer;
    fill_rect(r, 0, 0, SCREEN_W, SCREEN_H, C_BLACK_A);
    fill_card(r, 252, 92, 776, 512, C_PANEL);
    draw_accent_bar(r, 252, 92, 776, C_GREEN);
    draw_text(r, 284, 124, "SETTINGS", 3, C_TEXT);
    draw_text_clipped(r, 284, 164, SWITCH7ZIP_CONFIG_PATH, 2, C_MUTED, 720);

    int y = 218;
    for (size_t i = 0; i < SETTING_COUNT; ++i) {
        bool selected = i == app->settings_cursor;
        bool enabled = setting_value(app, (SettingRow)i);
        const char *value = (i == SETTING_FAT32_MODE) ? fat32_mode_label(app->settings.fat32_oversize_mode) : (enabled ? "ON" : "OFF");
        fill_rect(r, 284, y, 708, 52, selected ? C_PANEL_3 : C_PANEL_2);
        if (selected) fill_rect(r, 284, y, 6, 52, C_GREEN);
        draw_text(r, 306, y + 18, setting_label((SettingRow)i), 2, selected ? C_TEXT : C_MUTED);
        fill_rect(r, 824, y + 11, 138, 30, enabled ? C_GREEN : C_RED);
        draw_text(r, 846, y + 18, value, 2, C_BG);
        y += 62;
    }

    draw_text(r, 284, 486, "Recommended for 12GB+ archives: full RAM launch, extract-to-folder ON, overwrite OFF.", 2, C_MUTED);
    draw_button_hint(r, 284, 548, "A", "CHANGE", C_GREEN);
    draw_button_hint(r, 462, 548, "B", "BACK", C_ACCENT_2);
    draw_button_hint(r, 610, 548, "X", "SAVE", C_ACCENT);
}

static void draw_operation_overlay(AppState *app) {
    if (!is_busy(app)) return;
    SDL_Renderer *r = app->renderer;
    fill_card(r, 210, 184, 860, 390, C_PANEL);
    draw_accent_bar(r, 210, 184, 860, C_GREEN);
    char title[80];
    snprintf(title, sizeof(title), "%s IN PROGRESS", operation_label(app->operation));
    draw_text(r, 240, 210, title, 3, C_TEXT);
    draw_operation_progress(app, 240, 264, 800);
    draw_text(r, 240, 540, "B OR + REQUESTS CANCEL. LOGS: /SWITCH/SWITCH7ZIP/LOGS/LATEST.LOG", 2, C_MUTED);
}

static void draw_footer(AppState *app) {
    SDL_Renderer *r = app->renderer;
    fill_rect(r, 0, FOOTER_Y, SCREEN_W, SCREEN_H - FOOTER_Y, C_BG_2);
    stroke_rect(r, 0, FOOTER_Y, SCREEN_W, 1, C_LINE);
    draw_button_hint(r, 34, 656, "A", app->archive_view ? "OPEN" : "OPEN/PREVIEW", C_ACCENT);
    draw_button_hint(r, 164, 656, "B", "BACK", C_ACCENT_2);
    draw_button_hint(r, 296, 656, "X", "EXTRACT", C_AMBER);
    draw_button_hint(r, 472, 656, "Y", "MARK", C_GREEN);
    draw_button_hint(r, 596, 656, "ZR", "REFRESH", C_ACCENT);
    draw_button_hint(r, 792, 656, "ZL", "LOG", C_ACCENT_2);
    draw_button_hint(r, 936, 656, "-", "INPUT", C_MUTED);
    draw_button_hint(r, 1074, 656, "+", "MENU", C_RED);
}

static void render_app(AppState *app) {
    SDL_Renderer *r = app->renderer;
    set_draw_color(r, C_BG);
    SDL_RenderClear(r);
    draw_header(app);
    draw_browser(app);
    draw_details(app);
    draw_status_drawer(app);
    draw_operation_overlay(app);
    draw_footer(app);
    draw_context_menu_overlay(app);
    draw_settings_overlay(app);
    draw_jobs_overlay(app);
    draw_properties_overlay(app);
    draw_bookmarks_overlay(app);
    draw_log_overlay(app);
    draw_image_overlay(app);
    SDL_RenderPresent(r);
}

static void go_parent(AppState *app) {
    if (app->archive_view) {
        if (app->archive_prefix[0]) {
            archive_prefix_parent(app->archive_prefix);
            app->cursor = 0;
            app->scroll = 0;
            scan_current_dir(app);
        } else {
            app->archive_view = false;
            app->archive_path[0] = '\0';
            app->archive_title[0] = '\0';
            app->cursor = 0;
            app->scroll = 0;
            scan_current_dir(app);
            set_status(app, "Closed archive preview.");
        }
        return;
    }
    if (is_root_dir(app->current_dir)) {
        set_status(app, "Already at sdmc:/");
        return;
    }
    parent_dir_of(app->current_dir);
    app->cursor = 0;
    app->scroll = 0;
    scan_current_dir(app);
}

static void enter_dir(AppState *app, const char *path) {
    snprintf(app->current_dir, sizeof(app->current_dir), "%s", path);
    app->cursor = 0;
    app->scroll = 0;
    scan_current_dir(app);
}

static void enter_archive(AppState *app, const BrowserItem *item) {
    if (!app || !item) return;
    snprintf(app->archive_path, sizeof(app->archive_path), "%s", item->path);
    app->archive_prefix[0] = '\0';
    set_archive_title_from_path(app, item->path);
    app->archive_view = true;
    app->cursor = 0;
    app->scroll = 0;
    switch7zip_log_line("archive preview start: %s", app->archive_path);
    scan_current_dir(app);
}

static void open_selected(AppState *app) {
    if (!app || app->count == 0) return;
    BrowserItem selected = app->items[app->cursor];
    if (selected.type == ITEM_PARENT) {
        go_parent(app);
    } else if (selected.type == ITEM_DIRECTORY) {
        if (app->archive_view) {
            snprintf(app->archive_prefix, sizeof(app->archive_prefix), "%s", selected.path);
            app->cursor = 0;
            app->scroll = 0;
            scan_current_dir(app);
        } else {
            enter_dir(app, selected.path);
        }
    } else if (selected.type == ITEM_ARCHIVE && !app->archive_view) {
        enter_archive(app, &selected);
    } else if (selected.type == ITEM_FILE) {
        if (app->archive_view) set_status(app, "Archive member selected. Press X to extract this file, B to go back.");
        else set_status(app, "File selected. Press Y to ZIP, + for actions, or X to try extraction.");
    }
}

static void perform_context_action(AppState *app, ContextAction action) {
    if (!app) return;
    app->overlay = OVERLAY_NONE;
    switch (action) {
        case CTX_OPEN:
            open_selected(app);
            break;
        case CTX_MARK:
            toggle_mark_current(app);
            break;
        case CTX_SELECT_ALL:
            select_all_items(app);
            break;
        case CTX_CLEAR_SELECTION:
            clear_marks(app);
            set_status(app, "Selection cleared.");
            break;
        case CTX_EXTRACT:
            extract_selected(app);
            break;
        case CTX_TEST:
            test_selected(app);
            break;
        case CTX_COMPRESS:
            compress_selected(app);
            break;
        case CTX_COPY:
            copy_selection_to_clipboard(app, false);
            break;
        case CTX_MOVE:
            copy_selection_to_clipboard(app, true);
            break;
        case CTX_PASTE:
            paste_clipboard(app);
            break;
        case CTX_TRASH:
            trash_selection(app);
            break;
        case CTX_NEW_FOLDER:
            create_folder_action(app);
            break;
        case CTX_NEW_FILE:
            create_file_action(app);
            break;
        case CTX_RENAME:
            rename_selected(app);
            break;
        case CTX_PROPERTIES:
            open_properties(app);
            break;
        case CTX_HOMEBREW_INFO:
            open_homebrew_info(app);
            break;
        case CTX_COMPARE_TARGET:
            compare_with_target_pane(app);
            break;
        case CTX_VIEW_TEXT:
            open_text_selected(app);
            break;
        case CTX_EDIT_TEXT:
            edit_text_selected(app);
            break;
        case CTX_HEX_VIEW:
            open_hex_selected(app);
            break;
        case CTX_VIEW_IMAGE:
            open_image_selected(app);
            break;
        case CTX_FIND:
            find_in_current_folder(app);
            break;
        case CTX_SORT_NEXT:
            cycle_sort_mode(app);
            break;
        case CTX_FILTER_NEXT:
            cycle_filter_mode(app);
            break;
        case CTX_TOGGLE_HIDDEN:
            toggle_show_hidden(app);
            break;
        case CTX_ARCHIVE_BIT:
            set_archive_bit_selected(app);
            break;
        case CTX_TOGGLE_DUAL_PANE:
            toggle_dual_pane(app);
            break;
        case CTX_SWAP_PANES:
            swap_panes(app);
            break;
        case CTX_SET_OTHER_PANE:
            set_other_pane_to_current(app);
            break;
        case CTX_PASTE_OTHER_PANE:
            paste_clipboard_other_pane(app);
            break;
        case CTX_RESTORE_TRASH:
            restore_trash_selection(app);
            break;
        case CTX_EMPTY_TRASH:
            empty_trash_action(app);
            break;
        case CTX_CLEAN_PARTIALS:
            cleanup_partials_action(app);
            break;
        case CTX_EXPORT_DIAGNOSTICS:
            export_diagnostics_bundle(app);
            break;
        case CTX_BOOKMARKS:
            open_bookmarks(app);
            break;
        case CTX_SD_BENCHMARK:
            run_sd_benchmark(app);
            break;
        case CTX_REFRESH:
            scan_current_dir(app);
            set_status(app, "Folder refreshed.");
            break;
        case CTX_JOBS:
            app->overlay = OVERLAY_JOBS;
            set_status(app, "Job center opened.");
            break;
        case CTX_LOGS:
            refresh_log_view(app);
            app->log_view_open = true;
            set_status(app, "Opened latest log.");
            break;
        case CTX_SETTINGS:
            app->overlay = OVERLAY_SETTINGS;
            set_status(app, "Settings opened.");
            break;
        case CTX_EXIT:
            app->exit_requested = true;
            break;
        default:
            break;
    }
}



static bool operation_cancel_requested(void *user_data) {
    AppState *app = (AppState *)user_data;
    if (!app) return false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) app->cancel_requested = true;
    }

    padUpdate(&app->pad);
    u64 down = padGetButtonsDown(&app->pad);
    if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
        app->cancel_requested = true;
        set_status(app, "Cancelling after current archive block...");
        switch7zip_log_line("cancel requested by user");
    }
    return app->cancel_requested;
}

static void benchmark_progress(const BenchmarkStats *stats, void *user_data) {
    AppState *app = (AppState *)user_data;
    if (!app || !stats) return;
    app->live_benchmark = *stats;
    int pct = current_progress_percent(app);
    if (pct >= 0) set_statusf(app, "SD benchmark %d%%: %s", pct, stats->phase);
    else set_statusf(app, "SD benchmark: %s", stats->phase);
    uint64_t marker = stats->bytes_done;
    if (marker == 0 || marker - app->last_logged_progress >= 16ULL * 1024ULL * 1024ULL) {
        app->last_logged_progress = marker;
        switch7zip_log_line("benchmark progress: phase=%s bytes=%llu/%llu write_bps=%llu read_bps=%llu",
                            stats->phase,
                            (unsigned long long)stats->bytes_done,
                            (unsigned long long)stats->bytes_expected,
                            (unsigned long long)stats->write_bytes_per_second,
                            (unsigned long long)stats->read_bytes_per_second);
    }
    appletReportUserIsActive();
    render_app(app);
    SDL_PumpEvents();
}

static void run_sd_benchmark(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "SD benchmark runs from the SD-card browser.");
        return;
    }
    const uint64_t test_bytes = 64ULL * 1024ULL * 1024ULL;
    uint64_t free_space = nxcmd_free_space_for_path(app->current_dir);
    if (free_space && free_space < test_bytes + 128ULL * 1024ULL * 1024ULL) {
        char free_s[48];
        format_bytes(free_space, free_s, sizeof(free_s));
        set_statusf(app, "Not enough free space for benchmark: %s available.", free_s);
        return;
    }

    switch7zip_log_reset();
    switch7zip_log_line("sd benchmark requested: dir=%s test_bytes=%llu", app->current_dir, (unsigned long long)test_bytes);
    app->operation = OP_BENCHMARK;
    app->benchmarking = true;
    app->cancel_requested = false;
    app->operation_start_ticks = SDL_GetTicks64();
    app->last_logged_progress = 0;
    memset(&app->live_benchmark, 0, sizeof(app->live_benchmark));
    app->live_benchmark.bytes_expected = test_bytes * 2ULL;
    snprintf(app->live_benchmark.phase, sizeof(app->live_benchmark.phase), "Starting");
    set_status(app, "Running SD-card benchmark...");
    render_app(app);

    BenchmarkOptions options;
    memset(&options, 0, sizeof(options));
    options.cancel_cb = operation_cancel_requested;
    options.cancel_user_data = app;

    BenchmarkStats stats;
    int rc = nxcmd_run_sd_benchmark(app->current_dir, test_bytes, &stats, benchmark_progress, app, &options);
    app->live_benchmark = stats;
    uint64_t elapsed_ms = operation_elapsed_ms(app);
    app->benchmarking = false;
    app->operation = OP_IDLE;

    JobStatus job_status = rc == 0 ? JOB_STATUS_DONE : (app->cancel_requested || rc == -2 ? JOB_STATUS_CANCELLED : JOB_STATUS_FAILED);
    add_job_history(app, OP_BENCHMARK, job_status, "SD benchmark", app->current_dir, app->current_dir, stats.bytes_done, stats.bytes_expected, 0, elapsed_ms);

    char wr[48], rd[48];
    format_rate(stats.write_bytes_per_second, wr, sizeof(wr));
    format_rate(stats.read_bytes_per_second, rd, sizeof(rd));
    if (rc == 0) {
        set_statusf(app, "SD benchmark done. Write %s, read %s.", wr, rd);
        switch7zip_log_line("benchmark done: write_bps=%llu read_bps=%llu elapsed_ms=%llu",
                            (unsigned long long)stats.write_bytes_per_second,
                            (unsigned long long)stats.read_bytes_per_second,
                            (unsigned long long)elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(true);
    } else if (job_status == JOB_STATUS_CANCELLED) {
        set_status(app, "SD benchmark cancelled.");
        switch7zip_log_line("benchmark cancelled: %s", stats.error_message);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    } else {
        set_statusf(app, "SD benchmark failed: %s", stats.error_message[0] ? stats.error_message : "unknown error");
        switch7zip_log_line("benchmark failed: %s", stats.error_message);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    }
    app->operation_start_ticks = 0;
}



static void extraction_progress(const ExtractStats *stats, const char *entry_name, void *user_data) {
    AppState *app = (AppState *)user_data;
    if (!app || !stats) return;
    app->live_stats = *stats;
    if (entry_name && *entry_name) {
        int pct = current_progress_percent(app);
        if (pct >= 0) set_statusf(app, "Extracting %d%%: %s", pct, entry_name);
        else set_statusf(app, "Extracting: %s", entry_name);
    } else {
        set_status(app, "Extracting...");
    }
    uint64_t marker = stats->archive_bytes_read ? stats->archive_bytes_read : stats->bytes_written;
    if (marker == 0 || marker - app->last_logged_progress >= 256ULL * 1024ULL * 1024ULL) {
        app->last_logged_progress = marker;
        switch7zip_log_line("extract progress: src=%llu/%llu written=%llu entries=%llu files=%llu current=%s",
                            (unsigned long long)stats->archive_bytes_read,
                            (unsigned long long)stats->archive_bytes_total,
                            (unsigned long long)stats->bytes_written,
                            (unsigned long long)stats->entries_seen,
                            (unsigned long long)stats->files_written,
                            entry_name ? entry_name : "");
    }
    appletReportUserIsActive();
    render_app(app);
    SDL_PumpEvents();
}

static void compression_progress(const CompressStats *stats, const char *entry_name, void *user_data) {
    AppState *app = (AppState *)user_data;
    if (!app || !stats) return;
    app->live_compress = *stats;
    if (entry_name && *entry_name) {
        int pct = current_progress_percent(app);
        if (pct >= 0) set_statusf(app, "Compressing %d%%: %s", pct, entry_name);
        else set_statusf(app, "Compressing: %s", entry_name);
    } else {
        set_status(app, "Compressing...");
    }
    if (stats->bytes_read == 0 || stats->bytes_read - app->last_logged_progress >= 256ULL * 1024ULL * 1024ULL) {
        app->last_logged_progress = stats->bytes_read;
        switch7zip_log_line("compress progress: read=%llu/%llu entries=%llu files=%llu current=%s",
                            (unsigned long long)stats->bytes_read,
                            (unsigned long long)stats->bytes_expected,
                            (unsigned long long)stats->entries_seen,
                            (unsigned long long)stats->files_added,
                            entry_name ? entry_name : "");
    }
    appletReportUserIsActive();
    render_app(app);
    SDL_PumpEvents();
}


static void test_progress(const ExtractStats *stats, const char *entry_name, void *user_data) {
    AppState *app = (AppState *)user_data;
    if (!app || !stats) return;
    app->live_stats = *stats;
    if (entry_name && *entry_name) {
        int pct = current_progress_percent(app);
        if (pct >= 0) set_statusf(app, "Testing %d%%: %s", pct, entry_name);
        else set_statusf(app, "Testing: %s", entry_name);
    } else {
        set_status(app, "Testing archive...");
    }
    uint64_t marker = stats->archive_bytes_read ? stats->archive_bytes_read : stats->bytes_written;
    if (marker == 0 || marker - app->last_logged_progress >= 256ULL * 1024ULL * 1024ULL) {
        app->last_logged_progress = marker;
        switch7zip_log_line("test progress: src=%llu/%llu read=%llu entries=%llu files=%llu current=%s",
                            (unsigned long long)stats->archive_bytes_read,
                            (unsigned long long)stats->archive_bytes_total,
                            (unsigned long long)stats->bytes_written,
                            (unsigned long long)stats->entries_seen,
                            (unsigned long long)stats->files_written,
                            entry_name ? entry_name : "");
    }
    appletReportUserIsActive();
    render_app(app);
    SDL_PumpEvents();
}

static void test_selected(AppState *app) {
    if (!app || app->count == 0) return;

    const BrowserItem *item = &app->items[app->cursor];
    const char *archive_path = NULL;
    const char *display_name = NULL;

    if (app->archive_view) {
        archive_path = app->archive_path;
        display_name = app->archive_title[0] ? app->archive_title : "archive";
    } else {
        if (item->type != ITEM_ARCHIVE && item->type != ITEM_FILE) {
            set_status(app, "Select an archive-like file to test.");
            return;
        }
        archive_path = item->path;
        display_name = item->name;
    }

    app->operation = OP_TEST;
    app->extracting = true;
    app->cancel_requested = false;
    app->operation_start_ticks = SDL_GetTicks64();
    memset(&app->live_stats, 0, sizeof(app->live_stats));
    app->last_logged_progress = 0;
    switch7zip_log_reset();
    switch7zip_log_line("test start: source=%s", archive_path);
    set_statusf(app, "Testing archive integrity: %s", display_name);
    render_app(app);

    ExtractStats stats;
    ExtractOptions options;
    memset(&options, 0, sizeof(options));
    options.cancel_cb = operation_cancel_requested;
    options.cancel_user_data = app;

    int rc = test_archive_integrity_ex(archive_path, &stats, test_progress, app, &options);
    app->live_stats = stats;
    uint64_t elapsed_ms = operation_elapsed_ms(app);
    app->extracting = false;
    app->operation = OP_IDLE;

    JobStatus job_status = rc == 0 ? JOB_STATUS_DONE : (app->cancel_requested ? JOB_STATUS_CANCELLED : JOB_STATUS_FAILED);
    add_job_history(app, OP_TEST, job_status, "Archive integrity test", archive_path, "read-only test", stats.bytes_written, stats.archive_bytes_total ? stats.archive_bytes_total : stats.bytes_expected, stats.files_written, elapsed_ms);

    if (rc == 0) {
        char bytes[48];
        format_bytes(stats.bytes_written, bytes, sizeof(bytes));
        set_statusf(app, "Archive test passed: %llu files, %s read.",
                    (unsigned long long)stats.files_written,
                    bytes);
        switch7zip_log_line("test done: entries=%llu files=%llu dirs=%llu read=%llu unsupported_skipped=%llu elapsed_ms=%llu",
                            (unsigned long long)stats.entries_seen,
                            (unsigned long long)stats.files_written,
                            (unsigned long long)stats.dirs_created,
                            (unsigned long long)stats.bytes_written,
                            (unsigned long long)stats.unsupported_entries_skipped,
                            (unsigned long long)elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(true);
    } else if (job_status == JOB_STATUS_CANCELLED) {
        set_status(app, "Archive test cancelled.");
        switch7zip_log_line("test cancelled: entries=%llu files=%llu read=%llu elapsed_ms=%llu",
                            (unsigned long long)stats.entries_seen,
                            (unsigned long long)stats.files_written,
                            (unsigned long long)stats.bytes_written,
                            (unsigned long long)elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    } else {
        const char *msg = stats.error_message[0] ? stats.error_message : "unknown error";
        set_statusf(app, "Archive test failed: %s", msg);
        switch7zip_log_line("test failed: %s", msg);
        switch7zip_log_line("last entry: %s", stats.last_entry);
        switch7zip_log_line("elapsed_ms=%llu", (unsigned long long)elapsed_ms);
        write_failed_operation_report(app, "test", job_status, archive_path, "read-only test", msg, stats.last_entry, NULL, stats.bytes_written, stats.archive_bytes_total ? stats.archive_bytes_total : stats.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    }
    app->operation_start_ticks = 0;
}

static void extract_selected(AppState *app) {
    if (app->count == 0) return;
    BrowserItem *item = &app->items[app->cursor];

    if (app->archive_view) {
        if (item->type == ITEM_PARENT) {
            set_status(app, "Select an internal file or folder to extract, or B to leave preview.");
            return;
        }
    } else if (item->type != ITEM_ARCHIVE && item->type != ITEM_FILE) {
        set_status(app, "Select an archive-like file to extract.");
        return;
    }

    char output_dir[SWITCH7ZIP_MAX_PATH];
    if (make_extract_output_for_item(app, item, output_dir, sizeof(output_dir)) != 0) {
        set_status(app, "Could not build output folder path.");
        return;
    }

    const char *source_archive = app->archive_view ? app->archive_path : item->path;
    const char *selected_member = app->archive_view ? item->path : NULL;

    switch7zip_log_reset();
    switch7zip_log_line("extract requested: source=%s destination=%s selection=%s", source_archive, output_dir, selected_member ? selected_member : "");
    if (!preflight_archive_for_extract(app, source_archive, selected_member, output_dir)) {
        switch7zip_log_line("extract preflight blocked operation: %s", app->status);
        return;
    }

    app->operation = OP_EXTRACT;
    app->extracting = true;
    app->cancel_requested = false;
    app->operation_start_ticks = SDL_GetTicks64();
    memset(&app->live_stats, 0, sizeof(app->live_stats));
    app->last_logged_progress = 0;
    snprintf(app->last_output_dir, sizeof(app->last_output_dir), "%s", output_dir);
    switch7zip_log_line("extract start: source=%s", source_archive);
    if (selected_member && *selected_member) switch7zip_log_line("extract selection=%s", selected_member);
    switch7zip_log_line("extract destination=%s", output_dir);
    switch7zip_log_line("extract settings: extract_to_folder=%d overwrite_existing=%d fat32_mode=%s", app->settings.extract_to_folder ? 1 : 0, app->settings.overwrite_existing ? 1 : 0, fat32_mode_label(app->settings.fat32_oversize_mode));
    set_statusf(app, app->archive_view ? "Extracting selected archive member: %s" : "Extracting %s", item->name);
    render_app(app);

    ExtractStats stats;
    ExtractOptions options;
    memset(&options, 0, sizeof(options));
    options.overwrite_existing = app->settings.overwrite_existing;
    options.fat32_guard_enabled = true;
    options.fat32_oversize_mode = app->settings.fat32_oversize_mode;
    options.cancel_cb = operation_cancel_requested;
    options.cancel_user_data = app;
    int rc = extract_archive_selection_to_dir_ex(source_archive, selected_member, output_dir, &stats, extraction_progress, app, &options);
    app->live_stats = stats;
    uint64_t elapsed_ms = operation_elapsed_ms(app);
    app->extracting = false;
    app->operation = OP_IDLE;

    JobStatus job_status = rc == 0 ? JOB_STATUS_DONE : (app->cancel_requested ? JOB_STATUS_CANCELLED : JOB_STATUS_FAILED);
    add_job_history(app, OP_EXTRACT, job_status, app->archive_view ? "Selective extraction" : "Archive extraction", source_archive, output_dir, stats.bytes_written, stats.archive_bytes_total ? stats.archive_bytes_total : stats.bytes_expected, stats.files_written, elapsed_ms);


    if (rc == 0) {
        char bytes[48];
        format_bytes(stats.bytes_written, bytes, sizeof(bytes));
        set_statusf(app, "Done: %llu files, %llu dirs, %s written.",
                    (unsigned long long)stats.files_written,
                    (unsigned long long)stats.dirs_created,
                    bytes);
        switch7zip_log_line("extract done: files=%llu dirs=%llu written=%llu unsafe_skipped=%llu unsupported_skipped=%llu existing_skipped=%llu split_outputs=%llu concat_outputs=%llu parts=%llu concat_attr_failures=%llu elapsed_ms=%llu",
                            (unsigned long long)stats.files_written,
                            (unsigned long long)stats.dirs_created,
                            (unsigned long long)stats.bytes_written,
                            (unsigned long long)stats.unsafe_paths_skipped,
                            (unsigned long long)stats.unsupported_entries_skipped,
                            (unsigned long long)stats.existing_files_skipped,
                            (unsigned long long)stats.fat32_oversize_split,
                            (unsigned long long)stats.fat32_oversize_concat,
                            (unsigned long long)stats.split_parts_written,
                            (unsigned long long)stats.concat_attribute_failures,
                            (unsigned long long)elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(true);
    } else if (job_status == JOB_STATUS_CANCELLED) {
        set_status(app, "Extraction cancelled. Partial files may remain in destination.");
        switch7zip_log_line("extract cancelled: files=%llu dirs=%llu written=%llu partials=%llu elapsed_ms=%llu",
                            (unsigned long long)stats.files_written,
                            (unsigned long long)stats.dirs_created,
                            (unsigned long long)stats.bytes_written,
                            (unsigned long long)stats.partial_files_left,
                            (unsigned long long)elapsed_ms);
        write_failed_operation_report(app, "extract", job_status, source_archive, output_dir, "cancelled", stats.last_entry, stats.partial_path, stats.bytes_written, stats.archive_bytes_total ? stats.archive_bytes_total : stats.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    } else {
        const char *msg = stats.error_message[0] ? stats.error_message : "unknown error";
        set_statusf(app, "Extraction failed: %s", msg);
        switch7zip_log_line("extract failed: %s", msg);
        switch7zip_log_line("last entry: %s", stats.last_entry);
        if (stats.partial_path[0]) switch7zip_log_line("partial left: %s", stats.partial_path);
        switch7zip_log_line("elapsed_ms=%llu", (unsigned long long)elapsed_ms);
        write_failed_operation_report(app, "extract", job_status, source_archive, output_dir, msg, stats.last_entry, stats.partial_path, stats.bytes_written, stats.archive_bytes_total ? stats.archive_bytes_total : stats.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    }
    app->operation_start_ticks = 0;
}

static void compress_selected(AppState *app) {
    if (app->archive_view) {
        set_status(app, "Compression is available from the SD-card browser, not inside archive preview.");
        return;
    }
    if (app->count == 0) return;
    BrowserItem *item = &app->items[app->cursor];
    if (item->type == ITEM_PARENT) {
        set_status(app, "Select a file or folder to compress.");
        return;
    }

    char zip_path[SWITCH7ZIP_MAX_PATH];
    if (make_sibling_zip_path(item->path, zip_path, sizeof(zip_path)) != 0) {
        set_status(app, "Could not build sibling ZIP output path.");
        return;
    }

    switch7zip_log_reset();
    switch7zip_log_line("compress requested: source=%s destination=%s", item->path, zip_path);
    if (!preflight_source_to_destination(app, item->path, zip_path, "compression")) {
        switch7zip_log_line("compress preflight blocked operation: %s", app->status);
        return;
    }

    app->operation = OP_COMPRESS;
    app->compressing = true;
    app->cancel_requested = false;
    app->operation_start_ticks = SDL_GetTicks64();
    memset(&app->live_compress, 0, sizeof(app->live_compress));
    app->last_logged_progress = 0;
    snprintf(app->last_output_dir, sizeof(app->last_output_dir), "%s", zip_path);
    switch7zip_log_line("compress start: source=%s", item->path);
    switch7zip_log_line("compress destination=%s", zip_path);
    set_statusf(app, "Compressing %s to ZIP", item->name);
    render_app(app);

    CompressStats stats;
    CompressOptions options;
    memset(&options, 0, sizeof(options));
    options.cancel_cb = operation_cancel_requested;
    options.cancel_user_data = app;
    int rc = compress_path_to_zip_ex(item->path, zip_path, &stats, compression_progress, app, &options);
    app->live_compress = stats;
    uint64_t elapsed_ms = operation_elapsed_ms(app);
    app->compressing = false;
    app->operation = OP_IDLE;

    JobStatus job_status = rc == 0 ? JOB_STATUS_DONE : (app->cancel_requested ? JOB_STATUS_CANCELLED : JOB_STATUS_FAILED);
    add_job_history(app, OP_COMPRESS, job_status, "ZIP compression", item->path, zip_path, stats.bytes_read, stats.bytes_expected, stats.files_added, elapsed_ms);

    if (rc == 0) {
        char bytes[48];
        format_bytes(stats.bytes_read, bytes, sizeof(bytes));
        set_statusf(app, "ZIP created: %llu files, %s read.",
                    (unsigned long long)stats.files_added,
                    bytes);
        switch7zip_log_line("compress done: files=%llu dirs=%llu read=%llu elapsed_ms=%llu",
                            (unsigned long long)stats.files_added,
                            (unsigned long long)stats.dirs_added,
                            (unsigned long long)stats.bytes_read,
                            (unsigned long long)elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(true);
    } else if (job_status == JOB_STATUS_CANCELLED) {
        set_status(app, "Compression cancelled. Partial ZIP may remain beside the source.");
        switch7zip_log_line("compress cancelled: files=%llu dirs=%llu read=%llu partials=%llu elapsed_ms=%llu",
                            (unsigned long long)stats.files_added,
                            (unsigned long long)stats.dirs_added,
                            (unsigned long long)stats.bytes_read,
                            (unsigned long long)stats.partial_files_left,
                            (unsigned long long)elapsed_ms);
        write_failed_operation_report(app, "compress", job_status, item->path, zip_path, "cancelled", stats.last_entry, stats.partial_path, stats.bytes_read, stats.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    } else {
        const char *msg = stats.error_message[0] ? stats.error_message : "unknown error";
        set_statusf(app, "Compression failed: %s", msg);
        switch7zip_log_line("compress failed: %s", msg);
        switch7zip_log_line("last entry: %s", stats.last_entry);
        if (stats.partial_path[0]) switch7zip_log_line("partial left: %s", stats.partial_path);
        switch7zip_log_line("elapsed_ms=%llu", (unsigned long long)elapsed_ms);
        write_failed_operation_report(app, "compress", job_status, item->path, zip_path, msg, stats.last_entry, stats.partial_path, stats.bytes_read, stats.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    }
    app->operation_start_ticks = 0;
}


static void fileop_progress(const FileOpStats *stats, const char *entry_name, void *user_data) {
    AppState *app = (AppState *)user_data;
    if (!app || !stats) return;
    app->live_fileop = *stats;
    int pct = current_progress_percent(app);
    const char *verb = operation_label(app->operation);
    if (entry_name && *entry_name) {
        if (pct >= 0) set_statusf(app, "%s %d%%: %s", verb, pct, entry_name);
        else set_statusf(app, "%s: %s", verb, entry_name);
    } else {
        set_statusf(app, "%s...", verb);
    }
    uint64_t marker = stats->bytes_done;
    if (marker == 0 || marker - app->last_logged_progress >= 256ULL * 1024ULL * 1024ULL) {
        app->last_logged_progress = marker;
        switch7zip_log_line("fileop progress: op=%s bytes=%llu/%llu files=%llu dirs=%llu current=%s",
                            verb,
                            (unsigned long long)stats->bytes_done,
                            (unsigned long long)stats->bytes_expected,
                            (unsigned long long)stats->files_done,
                            (unsigned long long)stats->dirs_done,
                            entry_name ? entry_name : "");
    }
    appletReportUserIsActive();
    render_app(app);
    SDL_PumpEvents();
}

static void copy_selection_to_clipboard(AppState *app, bool move) {
    if (!app || app->archive_view) {
        set_status(app, "Copy/move clipboard is available in the SD-card browser.");
        return;
    }
    char paths[FILE_SELECTION_MAX][SWITCH7ZIP_MAX_PATH];
    size_t n = gather_selected_paths(app, paths);
    if (n == 0) {
        set_status(app, "Select or mark at least one SD-card item first.");
        return;
    }
    app->clipboard_count = n;
    app->clipboard_move = move;
    for (size_t i = 0; i < n; ++i) copy_cstr(app->clipboard_paths[i], sizeof(app->clipboard_paths[i]), paths[i]);
    set_statusf(app, "%s %llu item(s) to clipboard. Navigate and choose Paste here.", move ? "Move" : "Copied", (unsigned long long)n);
    switch7zip_log_line("clipboard %s count=%llu", move ? "move" : "copy", (unsigned long long)n);
}

static void paste_clipboard(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "Paste is available in the SD-card browser.");
        return;
    }
    if (app->clipboard_count == 0) {
        set_status(app, "Clipboard is empty.");
        return;
    }

    if (!app->clipboard_move) {
        switch7zip_log_reset();
        switch7zip_log_line("copy preflight requested: destination=%s count=%llu", app->current_dir, (unsigned long long)app->clipboard_count);
        for (size_t i = 0; i < app->clipboard_count; ++i) {
            if (!preflight_source_to_destination(app, app->clipboard_paths[i], app->current_dir, "copy")) {
                switch7zip_log_line("copy preflight blocked operation: %s", app->status);
                return;
            }
        }
    }

    app->operation = app->clipboard_move ? OP_MOVE : OP_COPY;
    app->file_operating = true;
    app->cancel_requested = false;
    app->operation_start_ticks = SDL_GetTicks64();
    app->last_logged_progress = 0;
    memset(&app->live_fileop, 0, sizeof(app->live_fileop));
    if (app->clipboard_move) switch7zip_log_reset();
    switch7zip_log_line("paste start: mode=%s destination=%s count=%llu", app->clipboard_move ? "move" : "copy", app->current_dir, (unsigned long long)app->clipboard_count);
    render_app(app);

    FileOpOptions options;
    memset(&options, 0, sizeof(options));
    options.cancel_cb = operation_cancel_requested;
    options.cancel_user_data = app;

    FileOpStats total;
    memset(&total, 0, sizeof(total));
    int rc = 0;
    char last_dest[SWITCH7ZIP_MAX_PATH] = {0};
    for (size_t i = 0; i < app->clipboard_count; ++i) {
        FileOpStats stats;
        char dest[SWITCH7ZIP_MAX_PATH] = {0};
        if (app->clipboard_move) rc = fileop_move_path_ex(app->clipboard_paths[i], app->current_dir, dest, sizeof(dest), &stats, fileop_progress, app, &options);
        else rc = fileop_copy_path_ex(app->clipboard_paths[i], app->current_dir, dest, sizeof(dest), &stats, fileop_progress, app, &options);
        total.bytes_done += stats.bytes_done;
        total.bytes_expected += stats.bytes_expected;
        total.files_done += stats.files_done;
        total.dirs_done += stats.dirs_done;
        total.entries_done += stats.entries_done;
        total.failures += stats.failures;
        total.partial_files_left += stats.partial_files_left;
        if (stats.partial_path[0]) snprintf(total.partial_path, sizeof(total.partial_path), "%s", stats.partial_path);
        if (stats.last_entry[0]) snprintf(total.last_entry, sizeof(total.last_entry), "%s", stats.last_entry);
        snprintf(last_dest, sizeof(last_dest), "%s", dest);
        if (rc != 0) {
            snprintf(total.error_message, sizeof(total.error_message), "%s", stats.error_message[0] ? stats.error_message : (app->cancel_requested ? "cancelled" : "file operation failed"));
            break;
        }
    }

    uint64_t elapsed_ms = operation_elapsed_ms(app);
    app->live_fileop = total;
    app->file_operating = false;
    OperationMode kind = app->operation;
    app->operation = OP_IDLE;
    JobStatus job_status = rc == 0 ? JOB_STATUS_DONE : (app->cancel_requested ? JOB_STATUS_CANCELLED : JOB_STATUS_FAILED);
    add_job_history(app, kind, job_status, app->clipboard_move ? "Move items" : "Copy items", app->clipboard_paths[0], app->current_dir, total.bytes_done, total.bytes_expected, total.files_done, elapsed_ms);

    if (rc == 0) {
        char bytes[48];
        format_bytes(total.bytes_done, bytes, sizeof(bytes));
        set_statusf(app, "%s complete: %llu files, %llu dirs, %s.", app->clipboard_move ? "Move" : "Copy", (unsigned long long)total.files_done, (unsigned long long)total.dirs_done, bytes);
        switch7zip_log_line("paste done: files=%llu dirs=%llu bytes=%llu elapsed_ms=%llu last_dest=%s", (unsigned long long)total.files_done, (unsigned long long)total.dirs_done, (unsigned long long)total.bytes_done, (unsigned long long)elapsed_ms, last_dest);
        if (app->clipboard_move) app->clipboard_count = 0;
        if (app->settings.sounds_enabled) alert_audio_play(true);
    } else if (job_status == JOB_STATUS_CANCELLED) {
        set_status(app, "File operation cancelled. .partial output may remain and can be cleaned from the menu.");
        switch7zip_log_line("paste cancelled: bytes=%llu partials=%llu elapsed_ms=%llu", (unsigned long long)total.bytes_done, (unsigned long long)total.partial_files_left, (unsigned long long)elapsed_ms);
        write_failed_operation_report(app, app->clipboard_move ? "move" : "copy", job_status, app->clipboard_paths[0], app->current_dir, "cancelled", total.last_entry, total.partial_path, total.bytes_done, total.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    } else {
        const char *msg = total.error_message[0] ? total.error_message : "unknown error";
        set_statusf(app, "File operation failed: %s", msg);
        switch7zip_log_line("paste failed: %s", msg);
        if (total.partial_path[0]) switch7zip_log_line("partial left: %s", total.partial_path);
        write_failed_operation_report(app, app->clipboard_move ? "move" : "copy", job_status, app->clipboard_paths[0], app->current_dir, msg, total.last_entry, total.partial_path, total.bytes_done, total.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    }
    app->operation_start_ticks = 0;
    scan_current_dir(app);
}

static void trash_selection(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "Trash is available in the SD-card browser.");
        return;
    }
    if (!app->settings.trash_delete_enabled) {
        set_status(app, "Trash is disabled in Settings.");
        return;
    }
    char paths[FILE_SELECTION_MAX][SWITCH7ZIP_MAX_PATH];
    size_t n = gather_selected_paths(app, paths);
    if (n == 0) {
        set_status(app, "Select or mark at least one item to trash.");
        return;
    }

    app->operation = OP_TRASH;
    app->file_operating = true;
    app->cancel_requested = false;
    app->operation_start_ticks = SDL_GetTicks64();
    app->last_logged_progress = 0;
    memset(&app->live_fileop, 0, sizeof(app->live_fileop));
    switch7zip_log_reset();
    switch7zip_log_line("trash start: count=%llu dest=%s", (unsigned long long)n, SWITCH7ZIP_TRASH_DIR);
    render_app(app);

    FileOpOptions options;
    memset(&options, 0, sizeof(options));
    options.cancel_cb = operation_cancel_requested;
    options.cancel_user_data = app;

    FileOpStats total;
    memset(&total, 0, sizeof(total));
    int rc = 0;
    for (size_t i = 0; i < n; ++i) {
        FileOpStats stats;
        char dest[SWITCH7ZIP_MAX_PATH] = {0};
        rc = fileop_trash_path_ex(paths[i], SWITCH7ZIP_TRASH_DIR, dest, sizeof(dest), &stats, fileop_progress, app, &options);
        total.bytes_done += stats.bytes_done;
        total.bytes_expected += stats.bytes_expected;
        total.files_done += stats.files_done;
        total.dirs_done += stats.dirs_done;
        total.entries_done += stats.entries_done;
        total.failures += stats.failures;
        total.partial_files_left += stats.partial_files_left;
        if (stats.partial_path[0]) snprintf(total.partial_path, sizeof(total.partial_path), "%s", stats.partial_path);
        if (stats.last_entry[0]) snprintf(total.last_entry, sizeof(total.last_entry), "%s", stats.last_entry);
        if (rc != 0) {
            snprintf(total.error_message, sizeof(total.error_message), "%s", stats.error_message[0] ? stats.error_message : (app->cancel_requested ? "cancelled" : "trash failed"));
            break;
        }
    }

    uint64_t elapsed_ms = operation_elapsed_ms(app);
    app->live_fileop = total;
    app->file_operating = false;
    app->operation = OP_IDLE;
    JobStatus job_status = rc == 0 ? JOB_STATUS_DONE : (app->cancel_requested ? JOB_STATUS_CANCELLED : JOB_STATUS_FAILED);
    add_job_history(app, OP_TRASH, job_status, "Move to Trash", paths[0], SWITCH7ZIP_TRASH_DIR, total.bytes_done, total.bytes_expected, total.files_done, elapsed_ms);

    if (rc == 0) {
        set_statusf(app, "Moved %llu item(s) to Trash.", (unsigned long long)n);
        switch7zip_log_line("trash done: items=%llu files=%llu dirs=%llu bytes=%llu elapsed_ms=%llu", (unsigned long long)n, (unsigned long long)total.files_done, (unsigned long long)total.dirs_done, (unsigned long long)total.bytes_done, (unsigned long long)elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(true);
    } else if (job_status == JOB_STATUS_CANCELLED) {
        set_status(app, "Trash operation cancelled.");
        switch7zip_log_line("trash cancelled: elapsed_ms=%llu", (unsigned long long)elapsed_ms);
        write_failed_operation_report(app, "trash", job_status, paths[0], SWITCH7ZIP_TRASH_DIR, "cancelled", total.last_entry, NULL, total.bytes_done, total.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    } else {
        const char *msg = total.error_message[0] ? total.error_message : "unknown error";
        set_statusf(app, "Trash failed: %s", msg);
        switch7zip_log_line("trash failed: %s", msg);
        write_failed_operation_report(app, "trash", job_status, paths[0], SWITCH7ZIP_TRASH_DIR, msg, total.last_entry, NULL, total.bytes_done, total.bytes_expected, elapsed_ms);
        if (app->settings.sounds_enabled) alert_audio_play(false);
    }
    app->operation_start_ticks = 0;
    scan_current_dir(app);
}

static void create_folder_action(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "New folder is available in the SD-card browser.");
        return;
    }
    char name[128];
    if (!prompt_text_input("New folder", "New Folder", name, sizeof(name))) {
        set_status(app, "New folder cancelled.");
        return;
    }
    char actual[SWITCH7ZIP_MAX_PATH];
    if (fileop_create_unique_folder(app->current_dir, name, actual, sizeof(actual)) == 0) {
        set_statusf(app, "Folder created: %s", actual);
        switch7zip_log_line("mkdir: %s", actual);
        scan_current_dir(app);
    } else {
        set_status(app, "Could not create folder.");
    }
}

static void rename_selected(AppState *app) {
    if (!app || app->archive_view || app->count == 0 || !item_can_mark(app, app->cursor)) {
        set_status(app, "Rename is available for one SD-card item at a time.");
        return;
    }
    BrowserItem *item = &app->items[app->cursor];
    char name[256];
    if (!prompt_text_input("Rename", item->name, name, sizeof(name))) {
        set_status(app, "Rename cancelled.");
        return;
    }
    char actual[SWITCH7ZIP_MAX_PATH];
    int rc = fileop_rename_path(item->path, name, actual, sizeof(actual));
    if (rc == 0) {
        set_statusf(app, "Renamed to: %s", name);
        switch7zip_log_line("rename: %s -> %s", item->path, actual);
        scan_current_dir(app);
    } else if (rc == -2) {
        set_status(app, "Rename failed: destination already exists.");
    } else {
        set_status(app, "Rename failed.");
    }
}


static void prop_line(AppState *app, const char *fmt, ...) {
    if (!app || app->property_line_count >= 14) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(app->property_lines[app->property_line_count], sizeof(app->property_lines[app->property_line_count]), fmt, ap);
    va_end(ap);
    app->property_line_count++;
}


static bool valid_new_name(const char *name) {
    if (!name || !*name) return false;
    if (strstr(name, "..")) return false;
    for (const char *p = name; *p; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':') return false;
    }
    return true;
}

static void create_file_action(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "New file is available in the SD-card browser.");
        return;
    }
    char name[256];
    if (!prompt_text_input("New file name", "new.txt", name, sizeof(name))) {
        set_status(app, "New file cancelled.");
        return;
    }
    if (!valid_new_name(name)) {
        set_status(app, "Invalid file name.");
        return;
    }
    char path[SWITCH7ZIP_MAX_PATH];
    if (path_join(path, sizeof(path), app->current_dir, name) != 0) {
        set_status(app, "New file path is too long.");
        return;
    }
    struct stat st;
    if (stat(path, &st) == 0) {
        set_status(app, "A file or folder with that name already exists.");
        return;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        set_statusf(app, "Could not create file: %s", strerror(errno));
        return;
    }
    fclose(f);
    switch7zip_log_line("new file: %s", path);
    scan_current_dir(app);
    set_statusf(app, "Created file: %s", name);
}

static int copy_file_for_backup(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[64 * 1024];
    int rc = 0;
    for (;;) {
        size_t n = fread(buf, 1, sizeof(buf), in);
        if (n > 0 && fwrite(buf, 1, n, out) != n) { rc = -1; break; }
        if (n < sizeof(buf)) {
            if (ferror(in)) rc = -1;
            break;
        }
    }
    fclose(out);
    fclose(in);
    return rc;
}

static void edit_text_selected(AppState *app) {
    if (!app || app->count == 0 || app->archive_view) {
        set_status(app, "Text editor opens small SD-card text/config files.");
        return;
    }
    BrowserItem *item = &app->items[app->cursor];
    if (item->type == ITEM_PARENT || item->type == ITEM_DIRECTORY || !nxcmd_file_is_text_like(item->name)) {
        set_status(app, "Select a small text/log/config file first.");
        return;
    }
    if (item->size > 12000) {
        set_status(app, "Text editor is limited to small files; use viewer for large logs.");
        return;
    }
    char content[16384];
    content[0] = '\0';
    FILE *f = fopen(item->path, "rb");
    if (f) {
        size_t n = fread(content, 1, sizeof(content) - 1, f);
        content[n] = '\0';
        fclose(f);
    }
    char edited[16384];
    if (!prompt_text_input("Edit text file", content, edited, sizeof(edited))) {
        set_status(app, "Edit cancelled.");
        return;
    }
    char backup[SWITCH7ZIP_MAX_PATH];
    snprintf(backup, sizeof(backup), "%s.bak", item->path);
    copy_file_for_backup(item->path, backup);
    f = fopen(item->path, "wb");
    if (!f) {
        set_statusf(app, "Could not save file: %s", strerror(errno));
        return;
    }
    fwrite(edited, 1, strlen(edited), f);
    fclose(f);
    switch7zip_log_line("edited text file: %s backup=%s", item->path, backup);
    scan_current_dir(app);
    set_statusf(app, "Saved text file; backup: %s.bak", item->name);
}

static void open_hex_selected(AppState *app) {
    if (!app || app->count == 0 || app->archive_view) {
        set_status(app, "Hex viewer opens SD-card files.");
        return;
    }
    BrowserItem *item = &app->items[app->cursor];
    if (item->type == ITEM_PARENT || item->type == ITEM_DIRECTORY) {
        set_status(app, "Select a file first.");
        return;
    }
    FILE *f = fopen(item->path, "rb");
    app->log_line_count = 0;
    copy_cstr(app->text_view_path, sizeof(app->text_view_path), item->path);
    copy_cstr(app->text_view_title, sizeof(app->text_view_title), "HEX VIEWER");
    if (!f) {
        copy_cstr(app->log_lines[0], sizeof(app->log_lines[0]), "Could not open file.");
        copy_cstr(app->log_lines[1], sizeof(app->log_lines[1]), item->path);
        app->log_line_count = 2;
        app->log_view_open = true;
        return;
    }
    unsigned char buf[16];
    uint64_t off = 0;
    while (app->log_line_count < LOG_VIEW_LINES) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n == 0) break;
        char hex[64] = {0};
        char ascii[20] = {0};
        size_t pos = 0;
        for (size_t i = 0; i < 16; ++i) {
            if (i < n) pos += (size_t)snprintf(hex + pos, sizeof(hex) - pos, "%02X ", buf[i]);
            else pos += (size_t)snprintf(hex + pos, sizeof(hex) - pos, "   ");
            ascii[i] = (i < n && buf[i] >= 32 && buf[i] <= 126) ? (char)buf[i] : '.';
        }
        ascii[16] = '\0';
        snprintf(app->log_lines[app->log_line_count], sizeof(app->log_lines[app->log_line_count]), "%08llX  %s |%s|", (unsigned long long)off, hex, ascii);
        app->log_line_count++;
        off += (uint64_t)n;
    }
    fclose(f);
    if (app->log_line_count == 0) {
        snprintf(app->log_lines[0], sizeof(app->log_lines[0]), "Empty file.");
        app->log_line_count = 1;
    }
    app->log_view_open = true;
    set_status(app, "Opened read-only hex viewer.");
}

static void cycle_sort_mode(AppState *app) {
    if (!app) return;
    app->sort_mode = (SortMode)(((int)app->sort_mode + 1) % SORT_COUNT);
    app->cursor = 0;
    app->scroll = 0;
    scan_current_dir(app);
    set_statusf(app, "Sort mode: %s", sort_mode_label(app->sort_mode));
}

static void cycle_filter_mode(AppState *app) {
    if (!app) return;
    app->filter_mode = (FilterMode)(((int)app->filter_mode + 1) % FILTER_COUNT);
    app->cursor = 0;
    app->scroll = 0;
    scan_current_dir(app);
    set_statusf(app, "Filter: %s", filter_mode_label(app->filter_mode));
}

static void toggle_show_hidden(AppState *app) {
    if (!app) return;
    app->show_hidden = !app->show_hidden;
    app->cursor = 0;
    app->scroll = 0;
    scan_current_dir(app);
    set_statusf(app, "Hidden files: %s", app->show_hidden ? "shown" : "hidden");
}

static void set_archive_bit_selected(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "Archive bit is available in the SD-card browser.");
        return;
    }
    char paths[FILE_SELECTION_MAX][SWITCH7ZIP_MAX_PATH];
    size_t n = gather_selected_paths(app, paths);
    if (n == 0) {
        set_status(app, "Select a folder first.");
        return;
    }
    unsigned ok = 0, skipped = 0, failed = 0;
    for (size_t i = 0; i < n; ++i) {
        struct stat st;
        if (stat(paths[i], &st) != 0 || !S_ISDIR(st.st_mode)) {
            skipped++;
            continue;
        }
#ifdef __SWITCH__
        Result rc = fsdevSetConcatenationFileAttribute(paths[i]);
        if (R_SUCCEEDED(rc)) {
            ok++;
            switch7zip_log_line("archive bit set: %s", paths[i]);
        } else {
            failed++;
            switch7zip_log_line("archive bit failed: %s rc=0x%x", paths[i], (unsigned)rc);
        }
#else
        failed++;
#endif
    }
    set_statusf(app, "Archive bit: %u set, %u skipped, %u failed.", ok, skipped, failed);
}


static uint32_t read_le32_local(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool printable_ascii_or_utf8_prefix(const char *s) {
    if (!s || !*s) return false;
    for (size_t i = 0; s[i] && i < 32; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 && c != '\t') return false;
    }
    return true;
}

static void sanitize_inline_text(char *s) {
    if (!s) return;
    for (size_t i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f) s[i] = ' ';
    }
}

static void open_homebrew_info(AppState *app) {
    if (!app || app->count == 0 || app->archive_view) {
        set_status(app, "Homebrew info is available for SD-card .nro/.nacp files.");
        return;
    }
    BrowserItem *item = &app->items[app->cursor];
    app->property_line_count = 0;
    for (size_t i = 0; i < 14; ++i) app->property_lines[i][0] = '\0';

    if (item->type == ITEM_PARENT || item->type == ITEM_DIRECTORY) {
        set_status(app, "Select an .nro or .nacp file first.");
        return;
    }
    bool is_nro = ends_with_local_ci(item->name, ".nro");
    bool is_nacp = ends_with_local_ci(item->name, ".nacp");
    if (!is_nro && !is_nacp) {
        set_status(app, "Homebrew info currently supports .nro and .nacp files.");
        return;
    }

    FILE *f = fopen(item->path, "rb");
    if (!f) {
        set_statusf(app, "Could not open metadata file: %s", strerror(errno));
        return;
    }

    char size_s[48];
    format_bytes(item->size, size_s, sizeof(size_s));
    prop_line(app, "Homebrew metadata viewer");
    prop_line(app, "File: %s", item->name);
    prop_line(app, "Size: %s", size_s);
    prop_line(app, "Path: %s", item->path);

    if (is_nro) {
        unsigned char hdr[0x80];
        size_t n = fread(hdr, 1, sizeof(hdr), f);
        if (n >= 0x20 && memcmp(hdr + 0x10, "NRO0", 4) == 0) {
            uint32_t version = read_le32_local(hdr + 0x14);
            uint32_t nro_size = read_le32_local(hdr + 0x18);
            uint32_t flags = read_le32_local(hdr + 0x1c);
            char nro_size_s[48];
            format_bytes(nro_size, nro_size_s, sizeof(nro_size_s));
            prop_line(app, "Format: NRO0 executable");
            prop_line(app, "Header version: %u", (unsigned)version);
            prop_line(app, "Header image size: %s", nro_size_s);
            prop_line(app, "Flags: 0x%08x", (unsigned)flags);
            if (n >= 0x50) {
                uint32_t text_off = read_le32_local(hdr + 0x20);
                uint32_t text_size = read_le32_local(hdr + 0x24);
                uint32_t ro_off = read_le32_local(hdr + 0x28);
                uint32_t ro_size = read_le32_local(hdr + 0x2c);
                prop_line(app, "Text: off 0x%x size 0x%x", (unsigned)text_off, (unsigned)text_size);
                prop_line(app, "ROData: off 0x%x size 0x%x", (unsigned)ro_off, (unsigned)ro_size);
            }
            prop_line(app, "Note: NACP/icon assets may be embedded; use .nacp viewer when separate.");
        } else {
            prop_line(app, "WARNING: NRO0 magic not found at expected offset 0x10.");
            if (n >= 4) prop_line(app, "First bytes: %02X %02X %02X %02X", hdr[0], hdr[1], hdr[2], hdr[3]);
        }
    } else if (is_nacp) {
        unsigned char title[0x300];
        size_t n = fread(title, 1, sizeof(title), f);
        if (n >= sizeof(title)) {
            char name[0x201];
            char author[0x101];
            memcpy(name, title, 0x200); name[0x200] = '\0';
            memcpy(author, title + 0x200, 0x100); author[0x100] = '\0';
            sanitize_inline_text(name);
            sanitize_inline_text(author);
            prop_line(app, "Format: NACP metadata");
            prop_line(app, "Title: %s", printable_ascii_or_utf8_prefix(name) ? name : "<empty/unknown>");
            prop_line(app, "Author: %s", printable_ascii_or_utf8_prefix(author) ? author : "<empty/unknown>");

            if (fseek(f, 0x3060, SEEK_SET) == 0) {
                char version[0x11];
                memset(version, 0, sizeof(version));
                if (fread(version, 1, 0x10, f) > 0) {
                    sanitize_inline_text(version);
                    if (printable_ascii_or_utf8_prefix(version)) prop_line(app, "Display version: %s", version);
                }
            }
            prop_line(app, "Locale parsed: first title entry only.");
        } else {
            prop_line(app, "WARNING: file is too small to contain a normal NACP title entry.");
        }
    }
    fclose(f);
    app->overlay = OVERLAY_PROPERTIES;
    set_status(app, "Homebrew metadata opened.");
}

static int compare_entry_against(const char *base, const char *name, struct stat *out) {
    char path[SWITCH7ZIP_MAX_PATH];
    if (path_join(path, sizeof(path), base, name) != 0) return -1;
    return stat(path, out);
}

static void compare_with_target_pane(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "Folder compare is available in the SD-card browser.");
        return;
    }
    if (!app->other_dir[0]) {
        set_status(app, "Set a target pane first from the action menu.");
        return;
    }
    DIR *left = opendir(app->current_dir);
    DIR *right = opendir(app->other_dir);
    app->property_line_count = 0;
    for (size_t i = 0; i < 14; ++i) app->property_lines[i][0] = '\0';
    if (!left || !right) {
        if (left) closedir(left);
        if (right) closedir(right);
        set_status(app, "Could not open one of the folders for compare.");
        return;
    }

    unsigned left_only = 0, right_only = 0, changed = 0, same = 0;
    char examples[6][96];
    unsigned example_count = 0;
    struct dirent *de;
    while ((de = readdir(left)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (!app->show_hidden && de->d_name[0] == '.') continue;
        struct stat ls, rs;
        if (compare_entry_against(app->current_dir, de->d_name, &ls) != 0) continue;
        if (compare_entry_against(app->other_dir, de->d_name, &rs) != 0) {
            left_only++;
            if (example_count < 6) snprintf(examples[example_count++], sizeof(examples[0]), "Left only: %.80s", de->d_name);
            continue;
        }
        bool ldir = S_ISDIR(ls.st_mode);
        bool rdir = S_ISDIR(rs.st_mode);
        if (ldir != rdir || (!ldir && (uint64_t)ls.st_size != (uint64_t)rs.st_size)) {
            changed++;
            if (example_count < 6) snprintf(examples[example_count++], sizeof(examples[0]), "Different: %.78s", de->d_name);
        } else {
            same++;
        }
    }
    rewinddir(right);
    while ((de = readdir(right)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (!app->show_hidden && de->d_name[0] == '.') continue;
        struct stat rs, ls;
        if (compare_entry_against(app->other_dir, de->d_name, &rs) != 0) continue;
        if (compare_entry_against(app->current_dir, de->d_name, &ls) != 0) {
            right_only++;
            if (example_count < 6) snprintf(examples[example_count++], sizeof(examples[0]), "Right only: %.79s", de->d_name);
        }
    }
    closedir(left);
    closedir(right);

    prop_line(app, "Top-level folder compare");
    prop_line(app, "Active: %s", app->current_dir);
    prop_line(app, "Target: %s", app->other_dir);
    prop_line(app, "Same names/type/size: %u", same);
    prop_line(app, "Only in active: %u", left_only);
    prop_line(app, "Only in target: %u", right_only);
    prop_line(app, "Different type/size: %u", changed);
    if (example_count == 0) prop_line(app, "No top-level differences found.");
    else {
        prop_line(app, "Examples:");
        for (unsigned i = 0; i < example_count; ++i) prop_line(app, "%s", examples[i]);
    }
    prop_line(app, "Note: compare is top-level only in this pre-1.0 build.");
    app->overlay = OVERLAY_PROPERTIES;
    set_status(app, "Folder compare complete.");
}

static void open_properties(AppState *app) {
    if (!app || app->count == 0) return;
    BrowserItem *item = &app->items[app->cursor];
    app->property_line_count = 0;
    for (size_t i = 0; i < 14; ++i) app->property_lines[i][0] = '\0';

    prop_line(app, "Name: %s", item->name);
    prop_line(app, "Type: %s", type_label(item->type));
    prop_line(app, "Path: %s", app->archive_view ? item->path : item->path);

    if (app->archive_view) {
        prop_line(app, "Archive: %s", app->archive_path);
        prop_line(app, "Internal folder: %s", app->archive_prefix[0] ? app->archive_prefix : "/");
        if (item->type == ITEM_FILE) {
            char size[48]; format_bytes(item->size, size, sizeof(size));
            prop_line(app, "Internal size: %s", size);
        }
    } else if (item->type == ITEM_DIRECTORY || item->type == ITEM_FILE || item->type == ITEM_ARCHIVE) {
        PathSizeInfo psi;
        if (nxcmd_measure_path_tree(item->path, &psi) == 0 || psi.files || psi.dirs || psi.bytes) {
            char size[48]; format_bytes(psi.bytes, size, sizeof(size));
            prop_line(app, "Measured size: %s", size);
            prop_line(app, "Contained files: %llu", (unsigned long long)psi.files);
            prop_line(app, "Contained folders: %llu", (unsigned long long)psi.dirs);
            if (psi.failures) prop_line(app, "WARNING: %llu item(s) could not be measured", (unsigned long long)psi.failures);
        }
        uint64_t free_bytes = nxcmd_free_space_for_path(item->path);
        char free_s[48]; format_bytes(free_bytes, free_s, sizeof(free_s));
        prop_line(app, "Free space here: %s", free_s);
    }

    if (!app->archive_view && item->type == ITEM_ARCHIVE) {
        MultipartInfo mi;
        if (nxcmd_check_multipart_archive(item->path, &mi) == 0 && mi.is_multipart) {
            prop_line(app, "%s", mi.message);
            prop_line(app, "First part: %s", mi.first_part_path);
        }
        ArchiveEstimate ae;
        char error[SWITCH7ZIP_STATUS_LEN]; error[0] = '\0';
        if (nxcmd_estimate_archive_unpacked_size(item->path, NULL, &ae, error, sizeof(error)) == 0) {
            char unpacked[48]; format_bytes(ae.bytes, unpacked, sizeof(unpacked));
            prop_line(app, "Estimated unpacked: %s", unpacked);
            prop_line(app, "Archive entries: %llu files, %llu dirs", (unsigned long long)ae.files, (unsigned long long)ae.dirs);
            if (ae.largest_file_bytes > 0) {
                char largest[48]; format_bytes(ae.largest_file_bytes, largest, sizeof(largest));
                prop_line(app, "Largest file: %s", largest);
            }
            if (ae.files_over_fat32_limit > 0) {
                prop_line(app, "FAT32 WARNING: %llu file(s) > 4 GiB", (unsigned long long)ae.files_over_fat32_limit);
                prop_line(app, "FAT32 mode: %s", fat32_mode_label(app->settings.fat32_oversize_mode));
                prop_line(app, "First oversized: %s", ae.first_file_over_fat32_limit);
            }
            if (ae.unsafe_paths || ae.unsupported_entries) {
                prop_line(app, "WARNING: skipped unsafe=%llu unsupported=%llu", (unsigned long long)ae.unsafe_paths, (unsigned long long)ae.unsupported_entries);
            }
        } else {
            prop_line(app, "Archive estimate: %s", error[0] ? error : "unavailable");
        }
    }

    app->overlay = OVERLAY_PROPERTIES;
    set_status(app, "Properties opened.");
}

static void open_text_selected(AppState *app) {
    if (!app || app->count == 0 || app->archive_view) {
        set_status(app, "Text viewer opens SD-card text/log/config files.");
        return;
    }
    BrowserItem *item = &app->items[app->cursor];
    if (item->type == ITEM_PARENT || item->type == ITEM_DIRECTORY) {
        set_status(app, "Select a text, log, config, or markdown file first.");
        return;
    }
    if (!nxcmd_file_is_text_like(item->name)) {
        set_status(app, "This does not look like a text/log/config file.");
        return;
    }
    refresh_text_view_from_path(app, item->path, "TEXT / LOG VIEWER");
    app->log_view_open = true;
    set_status(app, "Opened text viewer.");
}


static void close_image_view(AppState *app) {
    if (!app) return;
    if (app->image_texture) {
        SDL_DestroyTexture(app->image_texture);
        app->image_texture = NULL;
    }
    app->image_view_open = false;
    app->image_w = 0;
    app->image_h = 0;
    app->image_zoom = 1.0f;
    app->image_pan_x = 0;
    app->image_pan_y = 0;
}

static bool load_image_for_view(AppState *app, const char *path, const char *title) {
    if (!app || !path) return false;
    close_image_view(app);
    snprintf(app->image_path, sizeof(app->image_path), "%s", path);
    snprintf(app->image_title, sizeof(app->image_title), "%s", title ? title : "IMAGE VIEWER");
    app->image_error[0] = '\0';
    app->image_zoom = 1.0f;
    app->image_pan_x = 0;
    app->image_pan_y = 0;

    SDL_Surface *surface = IMG_Load(path);
    if (!surface) {
        snprintf(app->image_error, sizeof(app->image_error),
                 "Could not decode image with SDL2_image: %s", IMG_GetError());
        app->image_view_open = true;
        return false;
    }

    SDL_Surface *converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surface);
    if (!converted) {
        snprintf(app->image_error, sizeof(app->image_error),
                 "Could not convert image surface: %s", SDL_GetError());
        app->image_view_open = true;
        return false;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(app->renderer, converted);
    app->image_w = converted->w;
    app->image_h = converted->h;
    SDL_FreeSurface(converted);
    if (!texture) {
        snprintf(app->image_error, sizeof(app->image_error),
                 "Could not create image texture: %s", SDL_GetError());
        app->image_view_open = true;
        return false;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    app->image_texture = texture;
    app->image_view_open = true;
    switch7zip_log_line("image view: %s (%dx%d)", path, app->image_w, app->image_h);
    return true;
}

static void open_image_selected(AppState *app) {
    if (!app || app->count == 0 || app->archive_view) {
        set_status(app, "Image viewer opens SD-card image files.");
        return;
    }
    BrowserItem *item = &app->items[app->cursor];
    if (item->type == ITEM_PARENT || item->type == ITEM_DIRECTORY) {
        set_status(app, "Select an image file first.");
        return;
    }
    if (!nxcmd_file_is_image_like(item->name)) {
        set_status(app, "This does not look like a supported image filename.");
        return;
    }
    load_image_for_view(app, item->path, "IMAGE VIEWER");
    if (app->image_texture) set_status(app, "Opened image viewer.");
    else set_status(app, "Image viewer opened with diagnostics.");
}

static int contains_ci(const char *text, const char *needle) {
    if (!text || !needle || !*needle) return 0;
    size_t n = strlen(needle);
    for (const char *p = text; *p; ++p) {
        size_t i = 0;
        for (; i < n; ++i) {
            if (!p[i]) return 0;
            if (tolower((unsigned char)p[i]) != tolower((unsigned char)needle[i])) break;
        }
        if (i == n) return 1;
    }
    return 0;
}

static void find_in_current_folder(AppState *app) {
    if (!app || app->archive_view) {
        set_status(app, "Find is available in the SD-card browser for now.");
        return;
    }
    char query[128];
    if (!prompt_text_input("Find in folder", "", query, sizeof(query))) {
        set_status(app, "Find cancelled.");
        return;
    }
    if (!query[0]) {
        set_status(app, "Find query was empty.");
        return;
    }
    for (size_t i = 0; i < app->count; ++i) {
        if (app->items[i].type == ITEM_PARENT) continue;
        if (contains_ci(app->items[i].name, query)) {
            app->cursor = i;
            clamp_cursor(app);
            set_statusf(app, "Found: %s", app->items[i].name);
            return;
        }
    }
    set_statusf(app, "No visible item matches: %s", query);
}

static void open_bookmarks(AppState *app) {
    if (!app) return;
    app->bookmark_cursor = 0;
    app->overlay = OVERLAY_BOOKMARKS;
    set_status(app, "Bookmarks opened.");
}

static void go_bookmark(AppState *app) {
    if (!app) return;
    size_t count = bookmark_total_count(app);
    if (count == 0) count = bookmark_count();
    if (app->bookmark_cursor >= count) app->bookmark_cursor = 0;
    const char *path = NULL;
    bool recent = false;
    if (!get_bookmark_or_recent_path(app, app->bookmark_cursor, &path, &recent) || !path) {
        set_status(app, "Bookmark unavailable.");
        return;
    }
    DIR *dir = opendir(path);
    if (!dir) {
        set_statusf(app, "%s unavailable: %s", recent ? "Recent path" : "Bookmark", path);
        return;
    }
    closedir(dir);
    app->archive_view = false;
    app->archive_path[0] = '\0';
    app->archive_prefix[0] = '\0';
    app->archive_title[0] = '\0';
    snprintf(app->current_dir, sizeof(app->current_dir), "%s", path);
    app->cursor = 0;
    app->scroll = 0;
    app->overlay = OVERLAY_NONE;
    scan_current_dir(app);
    set_statusf(app, "Jumped to %s", path);
}

static bool init_sdl(AppState *app) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        return false;
    }
    app->window = SDL_CreateWindow(APP_NAME, 0, 0, SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    if (!app->window) return false;
    app->renderer = SDL_CreateRenderer(app->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    app->renderer_accelerated = app->renderer != NULL;
    if (!app->renderer) {
        app->renderer = SDL_CreateRenderer(app->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!app->renderer) return false;
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(app->renderer, SCREEN_W, SCREEN_H);
    app->image_decode_flags = IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    switch7zip_log_line("sdl2_image flags=0x%x", app->image_decode_flags);
    alert_audio_init();
    return true;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    static AppState app;
    memset(&app, 0, sizeof(app));
    settings_defaults(&app.settings);
    snprintf(app.current_dir, sizeof(app.current_dir), "sdmc:/");
    snprintf(app.other_dir, sizeof(app.other_dir), "sdmc:/switch");
    snprintf(app.restore_dir, sizeof(app.restore_dir), "sdmc:/");
    set_status(&app, "Starting...");

    ensure_app_dirs();
    load_settings(&app);
    switch7zip_log_reset();
    switch7zip_log_line("app start");
    if (!init_sdl(&app)) {
        SDL_Quit();
        return 1;
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&app.pad);

    app.sort_mode = SORT_NAME;
    app.filter_mode = FILTER_ALL;
    app.show_hidden = false;

    app.applet_type = appletGetAppletType();
    app.applet_mode_alert = app.settings.applet_warning_enabled && app.applet_type != AppletType_Application;
    switch7zip_log_line("applet_type=%d renderer=%s", (int)app.applet_type, app.renderer_accelerated ? "accelerated" : "software");
    scan_current_dir(&app);
    if (app.applet_mode_alert) {
        set_status(&app, "Applet mode detected: use full RAM launch for large archives.");
    } else {
        set_status(&app, APP_SAFETY_ALERT);
    }

    while (appletMainLoop()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) goto done;
        }

        padUpdate(&app.pad);
        u64 down = padGetButtonsDown(&app.pad);

        if (app.image_view_open) {
            if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
                close_image_view(&app);
                set_status(&app, "Closed image viewer.");
            }
            if (down & HidNpadButton_L) {
                app.image_zoom *= 0.8f;
                if (app.image_zoom < 0.25f) app.image_zoom = 0.25f;
                set_statusf(&app, "Image zoom %.1fx", app.image_zoom);
            }
            if (down & HidNpadButton_R) {
                app.image_zoom *= 1.25f;
                if (app.image_zoom > 8.0f) app.image_zoom = 8.0f;
                set_statusf(&app, "Image zoom %.1fx", app.image_zoom);
            }
            if (down & HidNpadButton_Left) app.image_pan_x += 40;
            if (down & HidNpadButton_Right) app.image_pan_x -= 40;
            if (down & HidNpadButton_Up) app.image_pan_y += 40;
            if (down & HidNpadButton_Down) app.image_pan_y -= 40;
            if (down & HidNpadButton_X) {
                app.image_zoom = 1.0f;
                app.image_pan_x = 0;
                app.image_pan_y = 0;
                set_status(&app, "Image view reset.");
            }
            if (down & (HidNpadButton_Y | HidNpadButton_ZR)) {
                char path[SWITCH7ZIP_MAX_PATH];
                char title[128];
                snprintf(path, sizeof(path), "%s", app.image_path);
                snprintf(title, sizeof(title), "%s", app.image_title[0] ? app.image_title : "IMAGE VIEWER");
                load_image_for_view(&app, path, title);
                set_status(&app, "Image viewer reloaded.");
            }
            render_app(&app);
            continue;
        }

        if (app.log_view_open) {
            if (down & (HidNpadButton_B | HidNpadButton_ZL | HidNpadButton_Plus)) {
                app.log_view_open = false;
                set_status(&app, "Closed log viewer.");
            }
            if (down & (HidNpadButton_Y | HidNpadButton_ZR)) {
                refresh_text_view_from_path(&app, app.text_view_path[0] ? app.text_view_path : switch7zip_log_path(), app.text_view_title[0] ? app.text_view_title : "TEXT VIEWER");
                set_status(&app, "Viewer refreshed.");
            }
            render_app(&app);
            continue;
        }

        if (app.overlay == OVERLAY_CONTEXT_MENU) {
            if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
                app.overlay = OVERLAY_NONE;
                set_status(&app, "Menu closed.");
            }
            if (down & HidNpadButton_Up) {
                app.context_cursor = app.context_cursor > 0 ? app.context_cursor - 1 : CTX_COUNT - 1;
            }
            if (down & HidNpadButton_Down) {
                app.context_cursor = (app.context_cursor + 1) % CTX_COUNT;
            }
            if (down & HidNpadButton_A) {
                perform_context_action(&app, (ContextAction)app.context_cursor);
            }
            if (app.exit_requested) break;
            render_app(&app);
            continue;
        }

        if (app.overlay == OVERLAY_SETTINGS) {
            if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
                app.overlay = OVERLAY_CONTEXT_MENU;
                set_status(&app, "Settings closed.");
            }
            if (down & HidNpadButton_Up) {
                app.settings_cursor = app.settings_cursor > 0 ? app.settings_cursor - 1 : SETTING_COUNT - 1;
            }
            if (down & HidNpadButton_Down) {
                app.settings_cursor = (app.settings_cursor + 1) % SETTING_COUNT;
            }
            if (down & HidNpadButton_A) {
                toggle_setting(&app, (SettingRow)app.settings_cursor);
            }
            if (down & HidNpadButton_X) {
                if (save_settings(&app) == 0) set_status(&app, "Settings saved.");
                else set_status(&app, "Could not save settings.");
            }
            render_app(&app);
            continue;
        }

        if (app.overlay == OVERLAY_JOBS) {
            if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
                app.overlay = OVERLAY_CONTEXT_MENU;
                set_status(&app, "Job center closed.");
            }
            if (down & HidNpadButton_ZL) {
                refresh_log_view(&app);
                app.log_view_open = true;
                app.overlay = OVERLAY_NONE;
                set_status(&app, "Opened latest log.");
            }
            render_app(&app);
            continue;
        }

        if (app.overlay == OVERLAY_PROPERTIES) {
            if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
                app.overlay = OVERLAY_CONTEXT_MENU;
                set_status(&app, "Properties closed.");
            }
            render_app(&app);
            continue;
        }

        if (app.overlay == OVERLAY_BOOKMARKS) {
            if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
                app.overlay = OVERLAY_CONTEXT_MENU;
                set_status(&app, "Bookmarks closed.");
            }
            if (down & HidNpadButton_Up) {
                size_t bcount = bookmark_total_count(&app);
                if (bcount == 0) bcount = bookmark_count();
                app.bookmark_cursor = app.bookmark_cursor > 0 ? app.bookmark_cursor - 1 : bcount - 1;
            }
            if (down & HidNpadButton_Down) {
                size_t bcount = bookmark_total_count(&app);
                if (bcount == 0) bcount = bookmark_count();
                app.bookmark_cursor = (app.bookmark_cursor + 1) % bcount;
            }
            if (down & HidNpadButton_A) {
                go_bookmark(&app);
            }
            render_app(&app);
            continue;
        }

        if (!is_busy(&app)) {
            if (down & HidNpadButton_Plus) {
                app.context_cursor = 0;
                app.overlay = OVERLAY_CONTEXT_MENU;
                set_status(&app, "Action menu opened.");
            }
            if (down & HidNpadButton_ZL) {
                refresh_log_view(&app);
                app.log_view_open = true;
                set_status(&app, "Opened latest log.");
            }
            if (down & HidNpadButton_Up) {
                if (app.cursor > 0) app.cursor--;
                clamp_cursor(&app);
            }
            if (down & HidNpadButton_Down) {
                if (app.cursor + 1 < app.count) app.cursor++;
                clamp_cursor(&app);
            }
            if (down & HidNpadButton_L) {
                size_t page = (LIST_H - 58) / ROW_HEIGHT;
                app.cursor = app.cursor > page ? app.cursor - page : 0;
                clamp_cursor(&app);
            }
            if (down & HidNpadButton_R) {
                size_t page = (LIST_H - 58) / ROW_HEIGHT;
                if (app.count > 0) {
                    app.cursor += page;
                    if (app.cursor >= app.count) app.cursor = app.count - 1;
                    clamp_cursor(&app);
                }
            }
            if (down & HidNpadButton_B) {
                go_parent(&app);
            }
            if (down & HidNpadButton_X) {
                extract_selected(&app);
            }
            if (down & HidNpadButton_Minus) {
                app.archive_view = false;
                app.archive_path[0] = '\0';
                app.archive_prefix[0] = '\0';
                app.archive_title[0] = '\0';
                snprintf(app.current_dir, sizeof(app.current_dir), "%s", SWITCH7ZIP_INPUT_DIR);
                app.cursor = 0;
                app.scroll = 0;
                scan_current_dir(&app);
                set_status(&app, "Jumped to input folder.");
            }
            if (down & HidNpadButton_Y) {
                toggle_mark_current(&app);
            }
            if (down & HidNpadButton_ZR) {
                scan_current_dir(&app);
                set_status(&app, "Folder refreshed.");
            }
            if (down & HidNpadButton_A) {
                open_selected(&app);
            }
        }

        if (app.exit_requested) break;
        render_app(&app);
    }

done:
    switch7zip_log_line("app exit");
    alert_audio_quit();
    close_image_view(&app);
    if (app.renderer) SDL_DestroyRenderer(app.renderer);
    if (app.window) SDL_DestroyWindow(app.window);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
