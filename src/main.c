#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "font.h"
#include "video_player.h"
#include "audio_engine.h"
#include "daw_types.h"
#include "daw_tabs.h"
#include "project_io.h"

#ifndef OFN_ALLOWMULTISELECT
#define OFN_ALLOWMULTISELECT 0x00000200
#endif
#ifndef OFN_EXPLORER
#define OFN_EXPLORER 0x00080000
#endif

DawEngine g_daw;

static SDL_Cursor *g_cursor_arrow = NULL;
static SDL_Cursor *g_cursor_size_we = NULL;
static SDL_Cursor *g_cursor_size_ns = NULL;
static SDL_Cursor *g_cursor_ibeam = NULL;

// Per-tab Crossfade drag mode state:
// true  = Crossfade ON  (clips can be moved/trimmed to overlap and crossfade)
// false = Crossfade OFF (existing overlapped clips stay as-is; newly moved/trimmed clips stop edge-to-edge)
static bool g_tab_crossfade[MAX_TABS] = { true, true, true, true, true, true, true, true };

// Per-tab Logic Pro style Cycle Range selection state
static bool   g_tab_range_active[MAX_TABS] = { false };
static double g_tab_range_start[MAX_TABS]  = { 0.0 };
static double g_tab_range_end[MAX_TABS]    = { 0.0 };

// Per-tab, per-item manual Clip Fade-In and Fade-Out durations (seconds)
static double g_item_fade_in[MAX_TABS][MAX_ITEMS];
static double g_item_fade_out[MAX_TABS][MAX_ITEMS];

// Per-tab, per-item Volume Offset (dB), Mute, Custom Color Index, and Group ID
static float  g_item_vol_db[MAX_TABS][MAX_ITEMS];
static bool   g_item_muted[MAX_TABS][MAX_ITEMS];
static int    g_item_color_idx[MAX_TABS][MAX_ITEMS];
static int    g_item_group_id[MAX_TABS][MAX_ITEMS];
static int    g_next_item_group_id = 1;
static double g_drag_group_orig_start[MAX_ITEMS];
static bool   g_show_transport_bar = true;
static int    g_saved_alt_tcp_width = 200;
static bool     g_video_fullscreen[MAX_TABS] = { false };
static SDL_Rect g_video_saved_rect[MAX_TABS];

typedef enum {
    VID_RESIZE_NONE = 0,
    VID_RESIZE_LEFT = 1,
    VID_RESIZE_RIGHT = 2,
    VID_RESIZE_BOTTOM = 3,
    VID_RESIZE_BOTTOM_LEFT = 4,
    VID_RESIZE_BOTTOM_RIGHT = 5
} VideoResizeEdge;

static VideoResizeEdge g_video_resize_edge = VID_RESIZE_NONE;
static int             g_video_resize_start_mx = 0;
static int             g_video_resize_start_my = 0;
static SDL_Rect        g_video_resize_orig_rect = { 680, 110, 480, 294 };

// Detects if mouse (mx, my) is on any side edge (left/right), bottom edge, or bottom corners of the Video Monitor
static VideoResizeEdge hit_test_video_resize_edge(const SDL_Rect *vr, int mx, int my) {
    if (!vr) return VID_RESIZE_NONE;
    const int tol = 8;
    const int c_tol = 12;
    if (mx < vr->x - tol || mx > vr->x + vr->w + tol ||
        my < vr->y || my > vr->y + vr->h + tol) {
        return VID_RESIZE_NONE;
    }
    bool near_left   = (abs(mx - vr->x) <= tol);
    bool near_right  = (abs(mx - (vr->x + vr->w)) <= tol);
    bool near_bottom = (abs(my - (vr->y + vr->h)) <= tol);

    if (abs(mx - vr->x) <= c_tol && abs(my - (vr->y + vr->h)) <= c_tol) {
        return VID_RESIZE_BOTTOM_LEFT;
    }
    if (abs(mx - (vr->x + vr->w)) <= c_tol && abs(my - (vr->y + vr->h)) <= c_tol) {
        return VID_RESIZE_BOTTOM_RIGHT;
    }
    // Exclude top title bar (vr->y .. vr->y + 24) from left/right edge resize so title bar drag & [X] buttons work cleanly
    if (my > vr->y + 24) {
        if (near_left)  return VID_RESIZE_LEFT;
        if (near_right) return VID_RESIZE_RIGHT;
    }
    if (near_bottom) return VID_RESIZE_BOTTOM;
    return VID_RESIZE_NONE;
}

typedef struct {
    AudioItem item;
    double fade_in;
    double fade_out;
    float vol_db;
    bool muted;
    int color_idx;
    int group_id;
} ClipboardItemEntry;

static ClipboardItemEntry g_clipboard[MAX_ITEMS];
static int g_clipboard_count = 0;

// Per-tab Track Hide state & Hide Tracks Popup Window state (with scrolling)
static bool     g_track_hidden[MAX_TABS][MAX_TRACKS];
static bool     g_hide_popup_open = false;
static SDL_Rect g_hide_win_rect = { 450, 110, 380, 340 };
static bool     g_hide_win_dragging = false;
static int      g_hide_drag_off_x = 0;
static int      g_hide_drag_off_y = 0;
static int      g_hide_scroll_y = 0;
static bool     g_hide_sb_dragging = false;
static int      g_hide_sb_start_my = 0;
static int      g_hide_sb_orig_scroll = 0;

// Tools Menu, 100-Entry Project History Popup Window & Tab Bar Change Notification State
#define MAX_HISTORY_ENTRIES 100
#define HISTORY_ENTRY_LEN   128
static bool     g_menu_tools_open = false;
static bool     g_history_popup_open = false;
static SDL_Rect g_history_win_rect = { 380, 96, 520, 400 };
static bool     g_history_win_dragging = false;
static int      g_history_drag_off_x = 0;
static int      g_history_drag_off_y = 0;
static int      g_history_scroll_y = 0;
static bool     g_history_sb_dragging = false;
static int      g_history_sb_start_my = 0;
static int      g_history_sb_orig_scroll = 0;

static char     g_tab_history[MAX_TABS][MAX_HISTORY_ENTRIES][HISTORY_ENTRY_LEN];
static int      g_tab_history_count[MAX_TABS] = { 0 };
static char     g_tab_notification[MAX_TABS][128];

// ================= INSERT MULTIPLE MEDIA ITEMS POPUP STATE =================
#define MAX_MULTI_IMPORT_FILES 64
typedef enum {
    MULTI_IMPORT_SEPARATE_TRACKS = 0,   // Same time position on separate tracks
    MULTI_IMPORT_SEQUENTIAL_SINGLE = 1  // Sequential time positions on a single track
} MultiImportMode;

typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;
    MultiImportMode mode;
    int base_track;
    double base_time;
    int num_files;
    char files[MAX_MULTI_IMPORT_FILES][512];
} MultiImportDialogState;

static MultiImportDialogState g_multi_import = {
    false,
    { 410, 220, 460, 210 },
    false, 0, 0,
    MULTI_IMPORT_SEPARATE_TRACKS,
    0,
    0.0,
    0,
    {{0}}
};

// Temporary batch collector for drag-and-drop (SDL_DROPFILE)
static int    g_drop_batch_count = 0;
static int    g_drop_batch_track = 0;
static double g_drop_batch_time  = 0.0;
static char   g_drop_batch_files[MAX_MULTI_IMPORT_FILES][512];

// Extended marker text storage (allows typing long marker text beyond 63 chars)
#define MARKER_TEXT_MAX 256
static char g_marker_full_text[MAX_TABS][MAX_MARKERS][MARKER_TEXT_MAX];
static char g_editing_marker_full_buf[MARKER_TEXT_MAX];

// Marker selection, typing cursor, & right-click context menu state
static int g_selected_marker_idx = -1;
static int g_marker_cursor_pos = 0;
static bool g_marker_context_menu_open = false;
static int g_marker_context_target_idx = -1;
static int g_marker_context_x = 0;
static int g_marker_context_y = 0;

// ================= UNIFIED EXPORT MEDIA POPUP WINDOW STATE =================
typedef enum {
    EXPORT_SRC_MASTER = 0,          // Master mix
    EXPORT_SRC_STEMS  = 1,          // Selected tracks (stems)
    EXPORT_SRC_ITEMS  = 2           // Selected media items
} ExportSourceType;

typedef enum {
    EXPORT_FMT_WAV = 0,             // WAV (16 / 24 / 32 bit PCM)
    EXPORT_FMT_MP3 = 1,             // MP3 (128 / 192 / 320 kbps)
    EXPORT_FMT_MP4 = 2              // MP4 (Resolution, Framerate, Video kbps, Audio codec + kbps)
} ExportFormatType;

typedef enum {
    EXPORT_MP4_AUDIO_AAC = 0,
    EXPORT_MP4_AUDIO_WAV = 1,
    EXPORT_MP4_AUDIO_MP3 = 2
} ExportMp4AudioCodec;

typedef enum {
    EXP_FIELD_NONE = 0,
    EXP_FIELD_DIR,
    EXP_FIELD_FILENAME,
    EXP_FIELD_MP4_WIDTH,
    EXP_FIELD_MP4_HEIGHT,
    EXP_FIELD_MP4_FPS,
    EXP_FIELD_MP4_V_KBPS,
    EXP_FIELD_MP4_A_KBPS
} ExportInputField;

typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;

    ExportSourceType source;        // Master mix / Selected tracks (stems) / Selected media items
    char directory[512];            // Output directory
    char filename[128];             // Output base file name

    int sample_rate;                // 48000 or 44100
    int channels;                   // 1 = Mono, 2 = Stereo

    ExportFormatType format;        // WAV, MP3, MP4
    int wav_bit_depth;              // 16, 24, or 32
    int mp3_bitrate;                // 128, 192, or 320

    // MP4 specific settings (matching REAPER/studio layout)
    char mp4_width_str[16];         // e.g. "1920"
    char mp4_height_str[16];        // e.g. "1080"
    char mp4_fps_str[16];           // e.g. "25.00"
    char mp4_video_kbps_str[16];    // e.g. "2048"
    ExportMp4AudioCodec mp4_audio_codec; // AAC
    char mp4_audio_kbps_str[16];    // "120", "192", or "320"
    bool mp4_aac_dropdown_open;     // Dropdown for 120 / 192 / 320 kbps

    bool two_pass_render;           // Option: two pass render

    // Export processing progress & timing state
    bool is_rendering;
    int progress_pct;               // 0 .. 100%
    double elapsed_sec;             // Processing elapsed time in seconds
    double est_remaining_sec;       // Estimated remaining time in seconds
    Uint32 render_start_ticks;      // SDL_GetTicks() when export started
    Uint32 last_ui_present_ticks;   // Last UI refresh timestamp

    ExportInputField active_field;  // Active text input field inside Export Popup
    char status_msg[256];           // Render status message
} ExportDialogState;

static ExportDialogState g_export = {
    false,
    { 300, 72, 680, 580 },
    false, 0, 0,
    EXPORT_SRC_MASTER,
    ".",
    "output",
    48000,
    2,
    EXPORT_FMT_WAV,
    24,
    192,
    "1920",
    "1080",
    "25.00",
    "2048",
    EXPORT_MP4_AUDIO_AAC,
    "192",
    false,
    false,
    false,
    0,
    0.0,
    0.0,
    0,
    0,
    EXP_FIELD_NONE,
    ""
};

static char* get_active_export_field_buf(size_t *out_max_len) {
    switch (g_export.active_field) {
        case EXP_FIELD_DIR:        if (out_max_len) *out_max_len = sizeof(g_export.directory) - 1; return g_export.directory;
        case EXP_FIELD_FILENAME:   if (out_max_len) *out_max_len = sizeof(g_export.filename) - 1; return g_export.filename;
        case EXP_FIELD_MP4_WIDTH:  if (out_max_len) *out_max_len = sizeof(g_export.mp4_width_str) - 1; return g_export.mp4_width_str;
        case EXP_FIELD_MP4_HEIGHT: if (out_max_len) *out_max_len = sizeof(g_export.mp4_height_str) - 1; return g_export.mp4_height_str;
        case EXP_FIELD_MP4_FPS:    if (out_max_len) *out_max_len = sizeof(g_export.mp4_fps_str) - 1; return g_export.mp4_fps_str;
        case EXP_FIELD_MP4_V_KBPS: if (out_max_len) *out_max_len = sizeof(g_export.mp4_video_kbps_str) - 1; return g_export.mp4_video_kbps_str;
        case EXP_FIELD_MP4_A_KBPS: if (out_max_len) *out_max_len = sizeof(g_export.mp4_audio_kbps_str) - 1; return g_export.mp4_audio_kbps_str;
        default:                   if (out_max_len) *out_max_len = 0; return NULL;
    }
}

static inline int get_valid_tab_idx(void) {
    int idx = g_daw.active_tab;
    if (idx < 0 || idx >= MAX_TABS) idx = 0;
    return idx;
}

// Records a timestamped project change ("yy:mm:dd::hh:mm:ss: <action>") in the 100-item History list
// and updates the top Tab Bar notification banner.
static void record_project_change(const char *action_name, bool mark_dirty) {
    int tab = get_valid_tab_idx();
    ProjectTab *p = get_cur_tab();
    if (p && mark_dirty) {
        p->is_dirty = true;
    }

    time_t now_t = time(NULL);
    struct tm *lt = localtime(&now_t);
    char ts_buf[32] = "26:10:20::00:00:00:";
    if (lt) {
        strftime(ts_buf, sizeof(ts_buf), "%y:%m:%d::%H:%M:%S:", lt);
    }

    const char *act = (action_name && action_name[0]) ? action_name : "Project Updated";

    char entry[HISTORY_ENTRY_LEN];
    snprintf(entry, sizeof(entry), "%s  %s", ts_buf, act);

    int cnt = g_tab_history_count[tab];
    if (cnt < MAX_HISTORY_ENTRIES) {
        strncpy(g_tab_history[tab][cnt], entry, HISTORY_ENTRY_LEN - 1);
        g_tab_history[tab][cnt][HISTORY_ENTRY_LEN - 1] = '\0';
        g_tab_history_count[tab] = cnt + 1;
    } else {
        for (int i = 0; i < MAX_HISTORY_ENTRIES - 1; i++) {
            memcpy(g_tab_history[tab][i], g_tab_history[tab][i + 1], HISTORY_ENTRY_LEN);
        }
        strncpy(g_tab_history[tab][MAX_HISTORY_ENTRIES - 1], entry, HISTORY_ENTRY_LEN - 1);
        g_tab_history[tab][MAX_HISTORY_ENTRIES - 1][HISTORY_ENTRY_LEN - 1] = '\0';
    }

    snprintf(g_tab_notification[tab], sizeof(g_tab_notification[0]), "%s", act);
}

static inline bool is_crossfade_active(void) {
    return g_tab_crossfade[get_valid_tab_idx()];
}

static inline void set_crossfade_active(bool active) {
    g_tab_crossfade[get_valid_tab_idx()] = active;
}

static inline bool is_range_active(void) {
    int idx = get_valid_tab_idx();
    return g_tab_range_active[idx] && (g_tab_range_end[idx] > g_tab_range_start[idx] + 0.01);
}

static inline double get_range_start(void) {
    return g_tab_range_start[get_valid_tab_idx()];
}

static inline double get_range_end(void) {
    return g_tab_range_end[get_valid_tab_idx()];
}

static inline void set_tab_range(double start_sec, double end_sec, bool active) {
    int idx = get_valid_tab_idx();
    if (start_sec < 0.0) start_sec = 0.0;
    if (end_sec < start_sec + 0.05) end_sec = start_sec + 0.05;
    g_tab_range_start[idx]  = start_sec;
    g_tab_range_end[idx]    = end_sec;
    g_tab_range_active[idx] = active;
}

static inline bool is_track_hidden(int trk_idx) {
    if (trk_idx < 0 || trk_idx >= MAX_TRACKS) return false;
    return g_track_hidden[get_valid_tab_idx()][trk_idx];
}

static inline void set_track_hidden(int trk_idx, bool hidden) {
    if (trk_idx < 0 || trk_idx >= MAX_TRACKS) return;
    g_track_hidden[get_valid_tab_idx()][trk_idx] = hidden;
}

static inline bool has_any_hidden_track(ProjectTab *p) {
    if (!p) return false;
    int tab = get_valid_tab_idx();
    for (int t = 0; t < p->num_tracks; t++) {
        if (g_track_hidden[tab][t]) return true;
    }
    return false;
}

static inline double get_item_fade_in(ProjectTab *p, int item_idx) {
    if (!p || item_idx < 0 || item_idx >= p->num_items) return 0.0;
    double fin = g_item_fade_in[get_valid_tab_idx()][item_idx];
    if (fin < 0.0) fin = 0.0;
    if (fin > p->items[item_idx].duration) fin = p->items[item_idx].duration;
    return fin;
}

static inline double get_item_fade_out(ProjectTab *p, int item_idx) {
    if (!p || item_idx < 0 || item_idx >= p->num_items) return 0.0;
    double fout = g_item_fade_out[get_valid_tab_idx()][item_idx];
    if (fout < 0.0) fout = 0.0;
    if (fout > p->items[item_idx].duration) fout = p->items[item_idx].duration;
    return fout;
}

static inline void set_item_fade_in(ProjectTab *p, int item_idx, double fin) {
    if (!p || item_idx < 0 || item_idx >= p->num_items) return;
    int tab = get_valid_tab_idx();
    double dur = p->items[item_idx].duration;
    if (fin < 0.0) fin = 0.0;
    if (fin > dur) fin = dur;
    g_item_fade_in[tab][item_idx] = fin;
    if (g_item_fade_in[tab][item_idx] + g_item_fade_out[tab][item_idx] > dur) {
        g_item_fade_out[tab][item_idx] = fmax(0.0, dur - g_item_fade_in[tab][item_idx]);
    }
}

static inline void set_item_fade_out(ProjectTab *p, int item_idx, double fout) {
    if (!p || item_idx < 0 || item_idx >= p->num_items) return;
    int tab = get_valid_tab_idx();
    double dur = p->items[item_idx].duration;
    if (fout < 0.0) fout = 0.0;
    if (fout > dur) fout = dur;
    g_item_fade_out[tab][item_idx] = fout;
    if (g_item_fade_in[tab][item_idx] + g_item_fade_out[tab][item_idx] > dur) {
        g_item_fade_in[tab][item_idx] = fmax(0.0, dur - g_item_fade_out[tab][item_idx]);
    }
}

// Activated when clicking the top-bar "Range" button:
// If any clip(s) are selected, sets the Logic Pro style yellow cycle range to the selected clip(s) bounds.
// Clicking "Range" again when already matching (or when no clip is selected) toggles the cycle range on/off.
static void handle_top_menu_range_button(ProjectTab *p) {
    if (!p) return;
    int idx = get_valid_tab_idx();

    bool has_sel_clip = false;
    double min_s = 1e18;
    double max_e = 0.0;

    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].is_selected) {
            has_sel_clip = true;
            if (p->items[i].start_time < min_s) min_s = p->items[i].start_time;
            double ie = p->items[i].start_time + p->items[i].duration;
            if (ie > max_e) max_e = ie;
        }
    }

    if (has_sel_clip && max_e > min_s + 0.01) {
        if (g_tab_range_active[idx] &&
            fabs(g_tab_range_start[idx] - min_s) < 1e-3 &&
            fabs(g_tab_range_end[idx] - max_e) < 1e-3) {
            g_tab_range_active[idx] = false;
        } else {
            set_tab_range(min_s, max_e, true);
        }
    } else {
        if (g_tab_range_active[idx]) {
            g_tab_range_active[idx] = false;
        } else {
            if (g_tab_range_end[idx] > g_tab_range_start[idx] + 0.05) {
                g_tab_range_active[idx] = true;
            } else {
                double st = p->playhead_seconds;
                set_tab_range(st, st + 5.0, true);
            }
        }
    }
}

static const char* get_marker_full_str(ProjectTab *p, int m_idx) {
    int tab_idx = get_valid_tab_idx();
    if (!p || m_idx < 0 || m_idx >= p->num_markers) return "";
    if (strncmp(g_marker_full_text[tab_idx][m_idx], p->markers[m_idx].text, 63) != 0) {
        strncpy(g_marker_full_text[tab_idx][m_idx], p->markers[m_idx].text, 63);
        g_marker_full_text[tab_idx][m_idx][63] = '\0';
    }
    return g_marker_full_text[tab_idx][m_idx];
}

static void set_marker_full_str(ProjectTab *p, int m_idx, const char *str) {
    int tab_idx = get_valid_tab_idx();
    if (!p || m_idx < 0 || m_idx >= MAX_MARKERS || !str) return;
    strncpy(g_marker_full_text[tab_idx][m_idx], str, MARKER_TEXT_MAX - 1);
    g_marker_full_text[tab_idx][m_idx][MARKER_TEXT_MAX - 1] = '\0';
    strncpy(p->markers[m_idx].text, str, 63);
    p->markers[m_idx].text[63] = '\0';
}

// Marker tag width expands strictly according to text length
static inline int get_marker_tag_width(const char *text) {
    int len = text ? (int)strlen(text) : 0;
    int w = len * 8 + 14;
    if (w < 24) w = 24;
    return w;
}

// Finds the screen X of the next marker on the timeline after m_idx (so marker m_idx never crosses it)
static int get_next_marker_screen_x(ProjectTab *p, int m_idx) {
    if (!p || m_idx < 0 || m_idx >= p->num_markers) return 999999;
    double my_t = p->markers[m_idx].time_sec;
    double best_next_t = 1e18;
    bool found = false;
    for (int k = 0; k < p->num_markers; k++) {
        if (k == m_idx) continue;
        double kt = p->markers[k].time_sec;
        if (kt > my_t + 1e-6 || (fabs(kt - my_t) <= 1e-6 && k > m_idx)) {
            if (kt < best_next_t) {
                best_next_t = kt;
                found = true;
            }
        }
    }
    if (!found) return 999999;
    return g_daw.tcp_width + (int)((best_next_t - p->scroll_x_sec) * p->pixels_per_second);
}

// Computes the visible (clamped) width of marker m_idx when not being edited
static int get_marker_clamped_width(ProjectTab *p, int m_idx, int mk_x) {
    const char *txt = get_marker_full_str(p, m_idx);
    int full_w = get_marker_tag_width(txt);
    int next_x = get_next_marker_screen_x(p, m_idx);
    int max_w = next_x - mk_x;
    if (max_w < 6) max_w = 6;
    return (full_w > max_w) ? max_w : full_w;
}

// Deletes marker at idx in the current tab
static void delete_marker_at(ProjectTab *p, int idx) {
    if (!p || idx < 0 || idx >= p->num_markers) return;
    int tab_idx = get_valid_tab_idx();

    push_undo_state();
    for (int m = idx; m < p->num_markers - 1; m++) {
        p->markers[m] = p->markers[m + 1];
        memcpy(g_marker_full_text[tab_idx][m], g_marker_full_text[tab_idx][m + 1], MARKER_TEXT_MAX);
    }
    p->num_markers--;
    if (g_selected_marker_idx == idx) g_selected_marker_idx = -1;
    else if (g_selected_marker_idx > idx) g_selected_marker_idx--;

    if (g_daw.input_focus == INPUT_FOCUS_MARKER) {
        if (g_daw.editing_marker_idx == idx) {
            g_daw.input_focus = INPUT_FOCUS_NONE;
            g_daw.editing_marker_idx = -1;
            SDL_StopTextInput();
        } else if (g_daw.editing_marker_idx > idx) {
            g_daw.editing_marker_idx--;
        }
    }
    g_marker_context_menu_open = false;
    g_marker_context_target_idx = -1;
    record_project_change("Deleted Marker", true);
}

// Swaps two tracks (t1 and t2) and updates all AudioItem track_idx references
static void swap_tracks(ProjectTab *p, int t1, int t2) {
    if (!p || t1 == t2 || t1 < 0 || t2 < 0 || t1 >= p->num_tracks || t2 >= p->num_tracks) return;

    Track tmp_trk = p->tracks[t1];
    p->tracks[t1] = p->tracks[t2];
    p->tracks[t2] = tmp_trk;
    p->tracks[t1].id = t1 + 1;
    p->tracks[t2].id = t2 + 1;

    char tmp_name[64];
    memcpy(tmp_name, p->track_names[t1], sizeof(tmp_name));
    memcpy(p->track_names[t1], p->track_names[t2], sizeof(tmp_name));
    memcpy(p->track_names[t2], tmp_name, sizeof(tmp_name));

    float tmp_db = p->track_db[t1];
    p->track_db[t1] = p->track_db[t2];
    p->track_db[t2] = tmp_db;

    int tmp_h = p->track_heights[t1];
    p->track_heights[t1] = p->track_heights[t2];
    p->track_heights[t2] = tmp_h;

    bool tmp_sel = p->track_selected[t1];
    p->track_selected[t1] = p->track_selected[t2];
    p->track_selected[t2] = tmp_sel;

    int tab_idx = get_valid_tab_idx();
    bool tmp_hid = g_track_hidden[tab_idx][t1];
    g_track_hidden[tab_idx][t1] = g_track_hidden[tab_idx][t2];
    g_track_hidden[tab_idx][t2] = tmp_hid;

    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].track_idx == t1) p->items[i].track_idx = t2;
        else if (p->items[i].track_idx == t2) p->items[i].track_idx = t1;
    }

    if (g_daw.editing_track_name_idx == t1) g_daw.editing_track_name_idx = t2;
    else if (g_daw.editing_track_name_idx == t2) g_daw.editing_track_name_idx = t1;

    record_project_change("Reordered Track", true);
}

#ifndef DAW_SHARED_STABILITY_VARS_DEFINED
#define DAW_SHARED_STABILITY_VARS_DEFINED
static int g_last_clicked_track_idx = -1;
static int g_last_clicked_item_idx = -1;
// ================= APP STABILITY & PROCESSING WAIT MODAL STATE =================
static volatile bool g_engine_processing = false;
static bool   g_processing_popup_open = false;
static char   g_processing_title[128] = "Processing, please wait...";
static char   g_processing_detail[160] = "Stabilizing audio timeline...";
static int    g_processing_pct = 100;
static Uint32 g_processing_close_at_ticks = 0;

// Track Delete Confirmation Modal State (when deleting a track that contains clips)
static bool g_track_delete_prompt_open = false;

// Open Project on Dirty Tab Save Confirmation Flag
static bool g_save_prompt_open_project_after = false;

// Tools -> Record Submenu State (Rec | Browse [with folder symbol])
static bool g_submenu_tools_record_open = false;

// Track vs Clip Copy/Paste Mode Flag
static bool g_last_copied_is_track = false;
#endif

// ================= BULLETPROOF DEEP UNDO / REDO ENGINE (100% AUDIO & PEAK RECOVERY) =================
#define MAX_DEEP_UNDO 16

typedef struct {
    int num_items;
    int num_tracks;
    AudioItem items[MAX_ITEMS];
    double fade_in[MAX_ITEMS];
    double fade_out[MAX_ITEMS];
    float  vol_db[MAX_ITEMS];
    bool   muted[MAX_ITEMS];
    int    color_idx[MAX_ITEMS];
    int    group_id[MAX_ITEMS];

    Track  tracks[MAX_TRACKS];
    char   track_names[MAX_TRACKS][64];
    float  track_db[MAX_TRACKS];
    int    track_heights[MAX_TRACKS];
    bool   track_selected[MAX_TRACKS];
    bool   track_hidden[MAX_TRACKS];

    int       num_markers;
    DawMarker markers[MAX_MARKERS];
} DeepUndoEntry;

static DeepUndoEntry *g_deep_undo_stack[MAX_TABS] = { NULL };
static int g_deep_undo_head[MAX_TABS]  = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int g_deep_undo_count[MAX_TABS] = {  0,  0,  0,  0,  0,  0,  0,  0 };
static bool g_deep_undo_has_redo_tip[MAX_TABS] = { false };

static void free_deep_undo_entry_peaks(DeepUndoEntry *ent) {
    if (!ent) return;
    for (int i = 0; i < ent->num_items; i++) {
        if (ent->items[i].peaks) {
            free(ent->items[i].peaks);
            ent->items[i].peaks = NULL;
        }
        ent->items[i].raw_file_handle = NULL;
    }
    ent->num_items = 0;
}

static void capture_tab_into_deep_entry(ProjectTab *p, int tab, DeepUndoEntry *ent) {
    if (!p || !ent) return;
    free_deep_undo_entry_peaks(ent);
    memset(ent, 0, sizeof(DeepUndoEntry));

    ent->num_items  = p->num_items;
    ent->num_tracks = p->num_tracks;
    for (int i = 0; i < p->num_items; i++) {
        ent->items[i] = p->items[i];
        ent->items[i].raw_file_handle = NULL;
        ent->items[i].peaks = NULL;
        if (p->items[i].peak_count > 0 && p->items[i].peaks) {
            ent->items[i].peaks = (PeakBin *)calloc((size_t)p->items[i].peak_count, sizeof(PeakBin));
            if (ent->items[i].peaks) {
                memcpy(ent->items[i].peaks, p->items[i].peaks, (size_t)p->items[i].peak_count * sizeof(PeakBin));
            }
        }
        ent->fade_in[i]   = g_item_fade_in[tab][i];
        ent->fade_out[i]  = g_item_fade_out[tab][i];
        ent->vol_db[i]    = g_item_vol_db[tab][i];
        ent->muted[i]     = g_item_muted[tab][i];
        ent->color_idx[i] = g_item_color_idx[tab][i];
        ent->group_id[i]  = g_item_group_id[tab][i];
    }

    memcpy(ent->tracks, p->tracks, sizeof(p->tracks));
    memcpy(ent->track_names, p->track_names, sizeof(p->track_names));
    memcpy(ent->track_db, p->track_db, sizeof(p->track_db));
    memcpy(ent->track_heights, p->track_heights, sizeof(p->track_heights));
    memcpy(ent->track_selected, p->track_selected, sizeof(p->track_selected));
    memcpy(ent->track_hidden, g_track_hidden[tab], sizeof(ent->track_hidden));

    ent->num_markers = p->num_markers;
    memcpy(ent->markers, p->markers, sizeof(DawMarker) * (size_t)p->num_markers);
}

static void restore_tab_from_deep_entry(ProjectTab *p, int tab, const DeepUndoEntry *ent) {
    if (!p || !ent) return;

    g_engine_processing = true;
    if (g_daw.audio_device > 0) SDL_LockAudioDevice(g_daw.audio_device);

    // 1. Safely close & free current items on the tab
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].raw_file_handle) {
            fclose(p->items[i].raw_file_handle);
            p->items[i].raw_file_handle = NULL;
        }
        if (p->items[i].peaks) {
            free(p->items[i].peaks);
            p->items[i].peaks = NULL;
        }
    }

    // 2. Restore track & marker states
    p->num_tracks = ent->num_tracks;
    memcpy(p->tracks, ent->tracks, sizeof(p->tracks));
    memcpy(p->track_names, ent->track_names, sizeof(p->track_names));
    memcpy(p->track_db, ent->track_db, sizeof(p->track_db));
    memcpy(p->track_heights, ent->track_heights, sizeof(p->track_heights));
    memcpy(p->track_selected, ent->track_selected, sizeof(p->track_selected));
    memcpy(g_track_hidden[tab], ent->track_hidden, sizeof(ent->track_hidden));

    p->num_markers = ent->num_markers;
    memcpy(p->markers, ent->markers, sizeof(DawMarker) * (size_t)ent->num_markers);

    // 3. Restore all clips with deep-cloned peaks and brand-new fopen handles for 100% audio recovery
    p->num_items = ent->num_items;
    for (int i = 0; i < ent->num_items; i++) {
        p->items[i] = ent->items[i];
        p->items[i].peaks = NULL;
        p->items[i].raw_file_handle = NULL;

        if (ent->items[i].peak_count > 0 && ent->items[i].peaks) {
            p->items[i].peaks = (PeakBin *)calloc((size_t)ent->items[i].peak_count, sizeof(PeakBin));
            if (p->items[i].peaks) {
                memcpy(p->items[i].peaks, ent->items[i].peaks, (size_t)ent->items[i].peak_count * sizeof(PeakBin));
            }
        }
        if (p->items[i].raw_audio_path[0] != '\0') {
            p->items[i].raw_file_handle = fopen(p->items[i].raw_audio_path, "rb");
        }
        // Fallback: if raw cache file was missing, regenerate cache from source media file while preserving edits
        if (!p->items[i].raw_file_handle && p->items[i].file_path[0] != '\0') {
            double sv_st  = p->items[i].start_time;
            double sv_off = p->items[i].offset_in_file;
            double sv_dur = p->items[i].duration;
            int    sv_trk = p->items[i].track_idx;
            bool   sv_sel = p->items[i].is_selected;
            process_media_cache(p->items[i].file_path, &p->items[i]);
            p->items[i].start_time     = sv_st;
            p->items[i].offset_in_file = sv_off;
            p->items[i].duration       = sv_dur;
            p->items[i].track_idx      = sv_trk;
            p->items[i].is_selected    = sv_sel;
        }

        g_item_fade_in[tab][i]   = ent->fade_in[i];
        g_item_fade_out[tab][i]  = ent->fade_out[i];
        g_item_vol_db[tab][i]    = ent->vol_db[i];
        g_item_muted[tab][i]     = ent->muted[i];
        g_item_color_idx[tab][i] = ent->color_idx[i];
        g_item_group_id[tab][i]  = ent->group_id[i];
    }

    if (g_daw.audio_device > 0) SDL_UnlockAudioDevice(g_daw.audio_device);
    g_engine_processing = false;
    p->is_dirty = true;
}

static void daw_push_undo(void) {
    ProjectTab *p = get_cur_tab();
    if (!p) return;
    int tab = get_valid_tab_idx();
    if (!g_deep_undo_stack[tab]) {
        g_deep_undo_stack[tab] = (DeepUndoEntry *)calloc(MAX_DEEP_UNDO, sizeof(DeepUndoEntry));
        if (!g_deep_undo_stack[tab]) return;
        g_deep_undo_head[tab] = -1;
        g_deep_undo_count[tab] = 0;
    }

    // Reserve 1 slot at the top for the Redo tip snapshot
    int max_push_head = MAX_DEEP_UNDO - 2;
    if (g_deep_undo_head[tab] < max_push_head) {
        g_deep_undo_head[tab]++;
    } else {
        free_deep_undo_entry_peaks(&g_deep_undo_stack[tab][0]);
        for (int i = 0; i < max_push_head; i++) {
            g_deep_undo_stack[tab][i] = g_deep_undo_stack[tab][i + 1];
        }
        memset(&g_deep_undo_stack[tab][max_push_head], 0, sizeof(DeepUndoEntry));
    }

    // Clear any invalidated future redo entries beyond head
    for (int k = g_deep_undo_head[tab] + 1; k < MAX_DEEP_UNDO; k++) {
        free_deep_undo_entry_peaks(&g_deep_undo_stack[tab][k]);
    }

    capture_tab_into_deep_entry(p, tab, &g_deep_undo_stack[tab][g_deep_undo_head[tab]]);
    g_deep_undo_count[tab] = g_deep_undo_head[tab] + 1;
    g_deep_undo_has_redo_tip[tab] = false;
    p->is_dirty = true;
}

static void daw_perform_undo(void) {
    ProjectTab *p = get_cur_tab();
    if (!p) return;
    int tab = get_valid_tab_idx();
    if (!g_deep_undo_stack[tab] || g_deep_undo_head[tab] < 0) return;

    // If we are undoing from the latest live edit state, first save the live post-edit state at head + 1 for Redo!
    if (!g_deep_undo_has_redo_tip[tab] && g_deep_undo_head[tab] + 1 < MAX_DEEP_UNDO) {
        int redo_slot = g_deep_undo_head[tab] + 1;
        capture_tab_into_deep_entry(p, tab, &g_deep_undo_stack[tab][redo_slot]);
        g_deep_undo_count[tab] = redo_slot + 1;
        g_deep_undo_has_redo_tip[tab] = true;
        // Restore the pre-edit snapshot at g_deep_undo_head[tab]
        restore_tab_from_deep_entry(p, tab, &g_deep_undo_stack[tab][g_deep_undo_head[tab]]);
        return;
    }

    if (g_deep_undo_head[tab] > 0) {
        g_deep_undo_head[tab]--;
        restore_tab_from_deep_entry(p, tab, &g_deep_undo_stack[tab][g_deep_undo_head[tab]]);
    }
}

static void daw_perform_redo(void) {
    ProjectTab *p = get_cur_tab();
    if (!p) return;
    int tab = get_valid_tab_idx();
    if (!g_deep_undo_stack[tab]) return;

    if (g_deep_undo_head[tab] + 1 < g_deep_undo_count[tab]) {
        g_deep_undo_head[tab]++;
        restore_tab_from_deep_entry(p, tab, &g_deep_undo_stack[tab][g_deep_undo_head[tab]]);
    }
}

#define push_undo_state daw_push_undo
#define perform_undo    daw_perform_undo
#define perform_redo    daw_perform_redo

// Automatically scrolls p->scroll_x_sec so the playhead is always visible on screen wherever it moves
static void ensure_playhead_visible_in_viewport(ProjectTab *p, int view_w) {
    if (!p || view_w <= 60 || p->pixels_per_second <= 1.0f) return;
    double vis_span = (double)view_w / (double)p->pixels_per_second;
    double vis_start = p->scroll_x_sec;
    double vis_end   = vis_start + vis_span;

    if (p->playhead_seconds < vis_start) {
        p->scroll_x_sec = fmax(0.0, p->playhead_seconds - vis_span * 0.15);
    } else if (p->playhead_seconds > vis_end - vis_span * 0.04) {
        if (p->is_playing) {
            p->scroll_x_sec = fmax(0.0, p->playhead_seconds - vis_span * 0.15);
        } else {
            p->scroll_x_sec = fmax(0.0, p->playhead_seconds - vis_span * 0.50);
        }
    }
}

// Deletes an audio/video item at index idx under audio lock and shifts all per-item metadata arrays in lockstep
static void delete_item_at_index(ProjectTab *p, int idx) {
    if (!p || idx < 0 || idx >= p->num_items) return;
    int tab = get_valid_tab_idx();
    if (g_daw.audio_device > 0) SDL_LockAudioDevice(g_daw.audio_device);
    if (p->items[idx].raw_file_handle) {
        fclose(p->items[idx].raw_file_handle);
        p->items[idx].raw_file_handle = NULL;
    }
    if (p->items[idx].peaks) {
        free(p->items[idx].peaks);
        p->items[idx].peaks = NULL;
    }
    for (int k = idx; k < p->num_items - 1; k++) {
        p->items[k] = p->items[k + 1];
        g_item_fade_in[tab][k]   = g_item_fade_in[tab][k + 1];
        g_item_fade_out[tab][k]  = g_item_fade_out[tab][k + 1];
        g_item_vol_db[tab][k]    = g_item_vol_db[tab][k + 1];
        g_item_muted[tab][k]     = g_item_muted[tab][k + 1];
        g_item_color_idx[tab][k] = g_item_color_idx[tab][k + 1];
        g_item_group_id[tab][k]  = g_item_group_id[tab][k + 1];
    }
    int last = p->num_items - 1;
    g_item_fade_in[tab][last]   = 0.0;
    g_item_fade_out[tab][last]  = 0.0;
    g_item_vol_db[tab][last]    = 0.0f;
    g_item_muted[tab][last]     = false;
    g_item_color_idx[tab][last] = 0;
    g_item_group_id[tab][last]  = 0;
    p->num_items--;
    if (g_daw.audio_device > 0) SDL_UnlockAudioDevice(g_daw.audio_device);
}

// Deletes track trk_idx (and any items on it) and shifts remaining tracks down
static void delete_track_at_index(ProjectTab *p, int trk_idx) {
    if (!p || p->num_tracks <= 1 || trk_idx < 0 || trk_idx >= p->num_tracks) return;
    int tab = get_valid_tab_idx();
    for (int i = p->num_items - 1; i >= 0; i--) {
        if (p->items[i].track_idx == trk_idx) {
            delete_item_at_index(p, i);
        } else if (p->items[i].track_idx > trk_idx) {
            p->items[i].track_idx--;
        }
    }
    for (int t = trk_idx; t < p->num_tracks - 1; t++) {
        p->tracks[t] = p->tracks[t + 1];
        p->tracks[t].id = t + 1;
        memcpy(p->track_names[t], p->track_names[t + 1], 64);
        p->track_db[t] = p->track_db[t + 1];
        p->track_heights[t] = p->track_heights[t + 1];
        p->track_selected[t] = p->track_selected[t + 1];
        g_track_hidden[tab][t] = g_track_hidden[tab][t + 1];
    }
    p->num_tracks--;
}

// Checks if any selected track contains clips before deleting:
// If a selected track contains any clip, opens the Delete / Cancel confirmation modal!
static bool request_delete_selected_tracks_with_prompt(ProjectTab *p) {
    if (!p || p->num_tracks <= 1) return false;
    bool any_sel = false;
    bool any_has_clips = false;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->track_selected[t]) {
            any_sel = true;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].track_idx == t) {
                    any_has_clips = true;
                    break;
                }
            }
        }
    }
    if (!any_sel) return false;
    if (any_has_clips) {
        g_track_delete_prompt_open = true;
        return true;
    }
    push_undo_state();
    bool removed = false;
    for (int t = p->num_tracks - 1; t >= 0; t--) {
        if (p->track_selected[t] && p->num_tracks > 1) {
            delete_track_at_index(p, t);
            removed = true;
        }
    }
    if (removed) record_project_change("Removed Selected Track(s)", true);
    return removed;
}

static void execute_confirmed_track_delete(ProjectTab *p) {
    g_track_delete_prompt_open = false;
    if (!p || p->num_tracks <= 1) return;
    push_undo_state();
    bool removed = false;
    for (int t = p->num_tracks - 1; t >= 0; t--) {
        if (p->track_selected[t] && p->num_tracks > 1) {
            delete_track_at_index(p, t);
            removed = true;
        }
    }
    if (removed) record_project_change("Deleted Track(s) and Clips", true);
}

// ================= TRACK CLIPBOARD (COPY TRACK & PASTE AT BOTTOM OF SELECTED TRACK) =================
typedef struct {
    Track track;
    char  name[64];
    float db;
    int   height;
    int   num_clips;
    ClipboardItemEntry clips[64];
} TrackClipboardEntry;

static TrackClipboardEntry g_track_clipboard[MAX_TRACKS];
static int g_track_clipboard_count = 0;

static void copy_selected_tracks_to_clipboard(ProjectTab *p) {
    if (!p) return;
    int tab = get_valid_tab_idx();
    for (int t = 0; t < g_track_clipboard_count; t++) {
        for (int c = 0; c < g_track_clipboard[t].num_clips; c++) {
            if (g_track_clipboard[t].clips[c].item.peaks) {
                free(g_track_clipboard[t].clips[c].item.peaks);
                g_track_clipboard[t].clips[c].item.peaks = NULL;
            }
        }
    }
    g_track_clipboard_count = 0;

    for (int t = 0; t < p->num_tracks && g_track_clipboard_count < MAX_TRACKS; t++) {
        if (!p->track_selected[t]) continue;
        TrackClipboardEntry *te = &g_track_clipboard[g_track_clipboard_count++];
        memset(te, 0, sizeof(TrackClipboardEntry));
        te->track  = p->tracks[t];
        strncpy(te->name, p->track_names[t], sizeof(te->name) - 1);
        te->db     = p->track_db[t];
        te->height = p->track_heights[t];
        for (int i = 0; i < p->num_items && te->num_clips < 64; i++) {
            if (p->items[i].track_idx != t) continue;
            ClipboardItemEntry *ce = &te->clips[te->num_clips++];
            memcpy(&ce->item, &p->items[i], sizeof(AudioItem));
            ce->item.raw_file_handle = NULL;
            ce->item.peaks = NULL;
            if (p->items[i].peak_count > 0 && p->items[i].peaks) {
                ce->item.peaks = (PeakBin *)calloc((size_t)p->items[i].peak_count, sizeof(PeakBin));
                if (ce->item.peaks) {
                    memcpy(ce->item.peaks, p->items[i].peaks, (size_t)p->items[i].peak_count * sizeof(PeakBin));
                }
            }
            ce->fade_in   = g_item_fade_in[tab][i];
            ce->fade_out  = g_item_fade_out[tab][i];
            ce->vol_db    = g_item_vol_db[tab][i];
            ce->muted     = g_item_muted[tab][i];
            ce->color_idx = g_item_color_idx[tab][i];
            ce->group_id  = g_item_group_id[tab][i];
        }
    }
}

// Pastes copied track(s) at the bottom of the currently selected track
static void paste_tracks_at_bottom_of_selected(ProjectTab *p) {
    if (!p || g_track_clipboard_count <= 0 || p->num_tracks >= MAX_TRACKS) return;
    int tab = get_valid_tab_idx();

    // Find bottom-most selected track index (or bottom of all tracks if none selected)
    int insert_after = p->num_tracks - 1;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->track_selected[t]) insert_after = t;
    }

    push_undo_state();
    for (int t = 0; t < p->num_tracks; t++) p->track_selected[t] = false;
    for (int i = 0; i < p->num_items; i++) p->items[i].is_selected = false;

    for (int k = 0; k < g_track_clipboard_count && p->num_tracks < MAX_TRACKS; k++) {
        TrackClipboardEntry *te = &g_track_clipboard[k];
        int ins_idx = insert_after + 1;
        if (ins_idx > p->num_tracks) ins_idx = p->num_tracks;

        // Shift tracks at >= ins_idx down by 1
        for (int t = p->num_tracks; t > ins_idx; t--) {
            p->tracks[t] = p->tracks[t - 1];
            p->tracks[t].id = t + 1;
            memcpy(p->track_names[t], p->track_names[t - 1], 64);
            p->track_db[t] = p->track_db[t - 1];
            p->track_heights[t] = p->track_heights[t - 1];
            p->track_selected[t] = p->track_selected[t - 1];
            g_track_hidden[tab][t] = g_track_hidden[tab][t - 1];
        }
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].track_idx >= ins_idx) p->items[i].track_idx++;
        }
        p->num_tracks++;

        p->tracks[ins_idx] = te->track;
        p->tracks[ins_idx].id = ins_idx + 1;
        snprintf(p->track_names[ins_idx], 64, "%.54s (Copy)", te->name);
        p->track_db[ins_idx] = te->db;
        p->track_heights[ins_idx] = (te->height >= 40) ? te->height : 84;
        p->track_selected[ins_idx] = true;
        g_track_hidden[tab][ins_idx] = false;
        g_last_clicked_track_idx = ins_idx;

        // Paste all clips belonging to this copied track
        for (int c = 0; c < te->num_clips && p->num_items < MAX_ITEMS; c++) {
            int ni = p->num_items++;
            ClipboardItemEntry *ce = &te->clips[c];
            AudioItem *dst = &p->items[ni];
            memcpy(dst, &ce->item, sizeof(AudioItem));
            dst->track_idx = ins_idx;
            dst->is_selected = false;
            dst->peaks = NULL;
            if (ce->item.peak_count > 0 && ce->item.peaks) {
                dst->peaks = (PeakBin *)calloc((size_t)ce->item.peak_count, sizeof(PeakBin));
                if (dst->peaks) {
                    memcpy(dst->peaks, ce->item.peaks, (size_t)ce->item.peak_count * sizeof(PeakBin));
                }
            }
            dst->raw_file_handle = dst->raw_audio_path[0] ? fopen(dst->raw_audio_path, "rb") : NULL;
            g_item_fade_in[tab][ni]   = ce->fade_in;
            g_item_fade_out[tab][ni]  = ce->fade_out;
            g_item_vol_db[tab][ni]    = ce->vol_db;
            g_item_muted[tab][ni]     = ce->muted;
            g_item_color_idx[tab][ni] = ce->color_idx;
            g_item_group_id[tab][ni]  = ce->group_id;
        }

        insert_after = ins_idx;
    }

    record_project_change("Pasted Track(s) Below Selected Track", true);
}

// Copies all selected items into the internal clipboard
static void copy_selected_items_to_clipboard(ProjectTab *p) {
    if (!p) return;
    int tab = get_valid_tab_idx();
    for (int c = 0; c < g_clipboard_count; c++) {
        if (g_clipboard[c].item.peaks) {
            free(g_clipboard[c].item.peaks);
            g_clipboard[c].item.peaks = NULL;
        }
    }
    g_clipboard_count = 0;
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].is_selected && g_clipboard_count < MAX_ITEMS) {
            ClipboardItemEntry *ce = &g_clipboard[g_clipboard_count++];
            memcpy(&ce->item, &p->items[i], sizeof(AudioItem));
            ce->item.raw_file_handle = NULL;
            ce->item.peaks = NULL;
            if (p->items[i].peak_count > 0 && p->items[i].peaks) {
                ce->item.peaks = (PeakBin *)calloc((size_t)p->items[i].peak_count, sizeof(PeakBin));
                if (ce->item.peaks) {
                    memcpy(ce->item.peaks, p->items[i].peaks, (size_t)p->items[i].peak_count * sizeof(PeakBin));
                }
            }
            ce->fade_in   = g_item_fade_in[tab][i];
            ce->fade_out  = g_item_fade_out[tab][i];
            ce->vol_db    = g_item_vol_db[tab][i];
            ce->muted     = g_item_muted[tab][i];
            ce->color_idx = g_item_color_idx[tab][i];
            ce->group_id  = g_item_group_id[tab][i];
        }
    }
}

// Pastes clipboard items onto the currently SELECTED TRACK starting at the playhead position
static void paste_clipboard_items_at_playhead(ProjectTab *p) {
    if (!p || g_clipboard_count <= 0) return;
    int tab = get_valid_tab_idx();
    double min_start = 1e18;
    int min_src_trk = 999999;
    for (int c = 0; c < g_clipboard_count; c++) {
        if (g_clipboard[c].item.start_time < min_start) {
            min_start = g_clipboard[c].item.start_time;
        }
        if (g_clipboard[c].item.track_idx < min_src_trk) {
            min_src_trk = g_clipboard[c].item.track_idx;
        }
    }
    if (min_start > 1e17) min_start = 0.0;
    if (min_src_trk > MAX_TRACKS) min_src_trk = 0;

    // Determine target selected track (Requirement #1: Clip copied paste on selected track near playhead)
    int sel_trk = -1;
    int num_sel_tracks = 0;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->track_selected[t] && !is_track_hidden(t)) {
            if (sel_trk < 0) sel_trk = t;
            num_sel_tracks++;
        }
    }

    push_undo_state();
    for (int i = 0; i < p->num_items; i++) p->items[i].is_selected = false;

    double max_end = p->playhead_seconds;
    for (int c = 0; c < g_clipboard_count; c++) {
        if (p->num_items >= MAX_ITEMS) break;
        int ni = p->num_items++;
        ClipboardItemEntry *ce = &g_clipboard[c];
        AudioItem *dst = &p->items[ni];
        memcpy(dst, &ce->item, sizeof(AudioItem));

        if (sel_trk >= 0) {
            if (num_sel_tracks <= 1) {
                dst->track_idx = sel_trk;
            } else {
                int mapped = sel_trk + (ce->item.track_idx - min_src_trk);
                if (mapped < 0) mapped = 0;
                if (mapped >= p->num_tracks) mapped = p->num_tracks - 1;
                dst->track_idx = mapped;
            }
        } else if (dst->track_idx < 0 || dst->track_idx >= p->num_tracks) {
            dst->track_idx = 0;
        }

        dst->start_time = p->playhead_seconds + (ce->item.start_time - min_start);
        dst->is_selected = true;
        dst->peaks = NULL;
        if (ce->item.peak_count > 0 && ce->item.peaks) {
            dst->peaks = (PeakBin *)calloc((size_t)ce->item.peak_count, sizeof(PeakBin));
            if (dst->peaks) {
                memcpy(dst->peaks, ce->item.peaks, (size_t)ce->item.peak_count * sizeof(PeakBin));
            }
        }
        dst->raw_file_handle = dst->raw_audio_path[0] ? fopen(dst->raw_audio_path, "rb") : NULL;
        g_item_fade_in[tab][ni]   = ce->fade_in;
        g_item_fade_out[tab][ni]  = ce->fade_out;
        g_item_vol_db[tab][ni]    = ce->vol_db;
        g_item_muted[tab][ni]     = ce->muted;
        g_item_color_idx[tab][ni] = ce->color_idx;
        g_item_group_id[tab][ni]  = ce->group_id;
        g_last_clicked_item_idx   = ni;
        if (dst->start_time + dst->duration > max_end) {
            max_end = dst->start_time + dst->duration;
        }
    }
    p->playhead_seconds = max_end;
    record_project_change("Pasted Clip(s) on Selected Track at Playhead", true);
}

#ifndef DAW_THEME_STATE_DEFINED
#define DAW_THEME_STATE_DEFINED

#define MAX_SAVED_THEMES 32
#define THEMES_JSON_FILENAME "themes_settings.json"

typedef struct {
    char name[48];
    uint8_t r, g, b, a;
} SavedThemeEntry;

typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;

    // Active ARGB Theme Color (Default Orange: R=205, G=138, B=72, A=255)
    uint8_t r, g, b, a;
    float h, s, v;

    bool dragging_sv;
    bool dragging_hue;
    bool dragging_alpha;

    bool code_focused;
    char code_buf[64];

    SavedThemeEntry saved[MAX_SAVED_THEMES];
    int num_saved;
    int selected_saved_idx;
    int saved_scroll_y;
    bool saved_sb_dragging;
    int saved_sb_start_my;
    int saved_sb_orig_scroll;

    char status_msg[128];
} ThemeManagerState;

static ThemeManagerState g_theme = {
    false,
    { 340, 88, 580, 420 },
    false, 0, 0,
    205, 138, 72, 255,
    29.77f, 0.6488f, 0.8039f,
    false, false, false,
    false,
    "rgba(205, 138, 72, 1.00)",
    {{{0}, 0, 0, 0, 0}}, 0, 0, 0, false, 0, 0,
    ""
};

// Script Manager window scrolling, edge-resize, search filter, pin & row move state
static int  g_script_scroll_y = 0;
static bool g_script_sb_dragging = false;
static int  g_script_sb_start_my = 0;
static int  g_script_sb_orig_scroll = 0;
static int  g_script_resize_edge = 0;
static int  g_script_resize_start_mx = 0;
static int  g_script_resize_start_my = 0;
static SDL_Rect g_script_resize_orig_rect = { 360, 120, 560, 380 };
static char g_script_status_msg[128] = "";
static char g_script_filter_text[128] = "";
static bool g_script_filter_focused = false;
static bool g_script_pinned[64] = { false };
static int  g_script_filtered_indices[64] = { 0 };
static int  g_script_num_filtered = 0;

// Anchor indices for standard Shift+Click range selection on tracks and clips


// ================= STABILIZED BOX-BASED DYNAMIC SPLIT POPUP STATE =================
typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;

    // Simple Box-Based Parameters (with user-specified defaults)
    bool split_at_transients;
    bool split_when_gate_opens;
    bool split_when_gate_closes;
    bool reduce_splits_enabled;
    int  reduce_splits_val;
    int  min_slice_ms;           // Default: 20 ms
    int  min_silence_ms;         // Default: 500 ms
    float gate_threshold_db;     // Default: -50.0 dB
    float gate_hysteresis_db;    // Default: -6.0 dB
    bool  display_gate_threshold;
    int   action_mode;           // 0 = Selected clip item, 1 = Selected Track
    bool  set_timebase_to_beats;
    bool  set_snap_offset_peak;
    int   snap_offset_first_ms;
    bool  remove_silent_areas;   // Default: true
    int   leading_pad_ms;        // Default: 50 ms
    int   trailing_pad_ms;       // Default: 150 ms
    bool  fade_pad;
    bool  create_chromatic_midi;

    // Box editing state:
    // 1 = min_slice_ms, 2 = min_silence_ms, 3 = gate_threshold_db, 4 = gate_hysteresis_db,
    // 5 = leading_pad_ms, 6 = trailing_pad_ms
    int  dragging_slider_id;
    int  editing_field_id;
    char edit_buf[32];
    char status_msg[128];
} DynamicSplitState;

static DynamicSplitState g_dyn_split = {
    false,
    { 410, 96, 460, 392 },
    false, 0, 0,
    false, false, true,
    false, 11,
    20,
    500,
    -50.0f,
    -6.0f,
    false,
    0,
    false,
    false, 50,
    true,
    50, 150, true,
    false,
    0, 0, "", ""
};

// ================= REPOSITION ITEMS POPUP STATE =================
typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;
    int mode;                // 0 = Item start, 1 = Item end (default 1 matching screenshot)
    double interval_sec;     // default 1.00 seconds
    char interval_str[32];   // "1.00"
    bool interval_focused;
} RepositionDialogState;

static RepositionDialogState g_reposition = {
    false,
    { 450, 210, 380, 220 },
    false, 0, 0,
    1,
    1.00,
    "1.00",
    false
};

// ================= AUDIO RECORDING ENGINE STATE =================
typedef struct {
    bool is_recording;
    int rec_track_idx;
    int rec_item_idx;
    double rec_start_time_sec;
    uint32_t rec_frames_written;
    SDL_AudioDeviceID capture_dev;
    FILE *wav_file;
    FILE *raw_cache_file;
    char current_wav_path[512];
    char current_raw_path[512];
    char folder_path[512];
    float cur_bin_min;
    float cur_bin_max;
    int samples_in_bin;
    int allocated_bins;
    Uint32 start_ticks;
    bool folder_initialized;
} RecordingEngineState;

static RecordingEngineState g_rec = {
    false,
    0, -1, 0.0, 0, 0,
    NULL, NULL,
    "", "",
    "C:\\Users\\USER\\Documents\\Recordings",
    0.0f, 0.0f, 0, 0, 0,
    false
};

static inline void init_default_recording_folder(void) {
    if (g_rec.folder_initialized) return;
    g_rec.folder_initialized = true;
    const char *user_prof = getenv("USERPROFILE");
    if (user_prof && user_prof[0]) {
        snprintf(g_rec.folder_path, sizeof(g_rec.folder_path), "%s\\Documents\\Recordings", user_prof);
    } else {
        snprintf(g_rec.folder_path, sizeof(g_rec.folder_path), "C:\\Users\\USER\\Documents\\Recordings");
    }
}

#ifndef DAW_SHARED_STABILITY_VARS_DEFINED
#define DAW_SHARED_STABILITY_VARS_DEFINED
static int g_last_clicked_track_idx = -1;
static int g_last_clicked_item_idx = -1;
// ================= APP STABILITY & PROCESSING WAIT MODAL STATE =================
static volatile bool g_engine_processing = false;
static bool   g_processing_popup_open = false;
static char   g_processing_title[128] = "Processing, please wait...";
static char   g_processing_detail[160] = "Stabilizing audio timeline...";
static int    g_processing_pct = 100;
static Uint32 g_processing_close_at_ticks = 0;

// Track Delete Confirmation Modal State (when deleting a track that contains clips)
static bool g_track_delete_prompt_open = false;

// Open Project on Dirty Tab Save Confirmation Flag
static bool g_save_prompt_open_project_after = false;

// Tools -> Record Submenu State (Rec | Browse [with folder symbol])
static bool g_submenu_tools_record_open = false;

// Track vs Clip Copy/Paste Mode Flag
static bool g_last_copied_is_track = false;
#endif

// ================= LUA & PYTHON CUSTOM SCRIPT POPUP WINDOW STATE =================
#define MAX_SCRIPT_POPUP_CONTROLS 8
typedef enum {
    SCP_CTRL_INPUT = 0,
    SCP_CTRL_SLIDER = 1,
    SCP_CTRL_CHECKBOX = 2,
    SCP_CTRL_LABEL = 3
} ScriptPopupControlType;

typedef struct {
    ScriptPopupControlType type;
    char label[64];
    char var_name[48];
    char str_val[128];
    double num_val;
    double min_val;
    double max_val;
    bool bool_val;
} ScriptPopupControl;

typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;
    char title[128];
    char script_filepath[512];
    bool is_python;
    int num_controls;
    ScriptPopupControl controls[MAX_SCRIPT_POPUP_CONTROLS];
    int focused_ctrl_idx;
    int dragging_slider_idx;
    char status_msg[128];
} ScriptPopupWindowState;

static ScriptPopupWindowState g_script_popup = {
    false,
    { 400, 140, 480, 340 },
    false, 0, 0,
    "Script Popup Window",
    "",
    false,
    0,
    {{SCP_CTRL_INPUT, "", "", "", 0.0, 0.0, 100.0, false}},
    -1, -1, ""
};

// Draws a crisp Pushpin symbol at (cx, cy) for pinned Actions and Scripts
static inline void draw_pin_symbol_icon(SDL_Renderer *ren, int cx, int cy, bool is_pinned, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    SDL_RenderDrawLine(ren, cx + 2, cy + 1, cx + 8, cy + 1);
    SDL_RenderDrawLine(ren, cx + 3, cy + 2, cx + 7, cy + 2);
    SDL_Rect body = { cx + 3, cy + 3, 5, 4 };
    if (is_pinned) {
        SDL_RenderFillRect(ren, &body);
    } else {
        SDL_RenderDrawRect(ren, &body);
    }
    SDL_RenderDrawLine(ren, cx + 1, cy + 7, cx + 9, cy + 7);
    SDL_RenderDrawLine(ren, cx + 2, cy + 8, cx + 8, cy + 8);
    SDL_RenderDrawLine(ren, cx + 5, cy + 9, cx + 5, cy + 12);
}

// Draws a crisp curved Undo arrow symbol (counter-clockwise arrow) at (cx, cy)
static inline void draw_undo_symbol_icon(SDL_Renderer *ren, int cx, int cy, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    // Left-pointing arrowhead at (cx + 2, cy + 6)
    SDL_RenderDrawLine(ren, cx + 2, cy + 6, cx + 7, cy + 2);
    SDL_RenderDrawLine(ren, cx + 2, cy + 6, cx + 7, cy + 10);
    SDL_RenderDrawLine(ren, cx + 3, cy + 6, cx + 7, cy + 3);
    SDL_RenderDrawLine(ren, cx + 3, cy + 6, cx + 7, cy + 9);
    // Top horizontal & curved right arch
    SDL_RenderDrawLine(ren, cx + 3, cy + 6, cx + 14, cy + 6);
    SDL_RenderDrawLine(ren, cx + 4, cy + 7, cx + 15, cy + 7);
    SDL_RenderDrawLine(ren, cx + 14, cy + 7, cx + 16, cy + 9);
    SDL_RenderDrawLine(ren, cx + 16, cy + 9, cx + 16, cy + 12);
    SDL_RenderDrawLine(ren, cx + 15, cy + 9, cx + 15, cy + 12);
    SDL_RenderDrawLine(ren, cx + 8, cy + 13, cx + 15, cy + 13);
}

// Draws a crisp curved Redo arrow symbol (clockwise arrow) at (cx, cy)
static inline void draw_redo_symbol_icon(SDL_Renderer *ren, int cx, int cy, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    // Right-pointing arrowhead at (cx + 16, cy + 6)
    SDL_RenderDrawLine(ren, cx + 16, cy + 6, cx + 11, cy + 2);
    SDL_RenderDrawLine(ren, cx + 16, cy + 6, cx + 11, cy + 10);
    SDL_RenderDrawLine(ren, cx + 15, cy + 6, cx + 11, cy + 3);
    SDL_RenderDrawLine(ren, cx + 15, cy + 6, cx + 11, cy + 9);
    // Top horizontal & curved left arch
    SDL_RenderDrawLine(ren, cx + 4, cy + 6, cx + 15, cy + 6);
    SDL_RenderDrawLine(ren, cx + 3, cy + 7, cx + 14, cy + 7);
    SDL_RenderDrawLine(ren, cx + 2, cy + 9, cx + 4, cy + 7);
    SDL_RenderDrawLine(ren, cx + 2, cy + 9, cx + 2, cy + 12);
    SDL_RenderDrawLine(ren, cx + 3, cy + 9, cx + 3, cy + 12);
    SDL_RenderDrawLine(ren, cx + 3, cy + 13, cx + 10, cy + 13);
}

// Draws a crisp Folder symbol icon at (cx, cy) for Tools -> Record -> Browse
static inline void draw_folder_symbol_icon(SDL_Renderer *ren, int cx, int cy, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    SDL_Rect tab_r = { cx + 1, cy + 1, 6, 3 };
    SDL_Rect body_r = { cx + 1, cy + 3, 14, 9 };
    SDL_RenderFillRect(ren, &tab_r);
    SDL_RenderDrawRect(ren, &body_r);
    SDL_Rect inner_fill = { cx + 2, cy + 5, 12, 6 };
    SDL_RenderFillRect(ren, &inner_fill);
}

// Draws a filled circle at center (cx, cy) with radius r (used for the red recording dot near the time clock)
static inline void draw_filled_circle_sdl(SDL_Renderer *ren, int cx, int cy, int r, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
    SDL_SetRenderDrawColor(ren, cr, cg, cb, ca);
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        SDL_RenderDrawLine(ren, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static inline void theme_hsv_to_rgb(float h, float s, float v, uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    while (h < 0.0f) h += 360.0f;
    while (h >= 360.0f) h -= 360.0f;

    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf = 0.0f, gf = 0.0f, bf = 0.0f;

    if (h < 60.0f)       { rf = c; gf = x; bf = 0.0f; }
    else if (h < 120.0f) { rf = x; gf = c; bf = 0.0f; }
    else if (h < 180.0f) { rf = 0.0f; gf = c; bf = x; }
    else if (h < 240.0f) { rf = 0.0f; gf = x; bf = c; }
    else if (h < 300.0f) { rf = x; gf = 0.0f; bf = c; }
    else                 { rf = c; gf = 0.0f; bf = x; }

    int ri = (int)((rf + m) * 255.0f + 0.5f);
    int gi = (int)((gf + m) * 255.0f + 0.5f);
    int bi = (int)((bf + m) * 255.0f + 0.5f);
    if (ri < 0) ri = 0; else if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; else if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; else if (bi > 255) bi = 255;
    if (out_r) *out_r = (uint8_t)ri;
    if (out_g) *out_g = (uint8_t)gi;
    if (out_b) *out_b = (uint8_t)bi;
}

static inline void theme_rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, float *out_h, float *out_s, float *out_v) {
    float rf = (float)r / 255.0f;
    float gf = (float)g / 255.0f;
    float bf = (float)b / 255.0f;
    float cmax = fmaxf(rf, fmaxf(gf, bf));
    float cmin = fminf(rf, fminf(gf, bf));
    float delta = cmax - cmin;

    float h = 0.0f;
    if (delta > 0.0001f) {
        if (cmax == rf) {
            h = 60.0f * fmodf((gf - bf) / delta, 6.0f);
        } else if (cmax == gf) {
            h = 60.0f * (((bf - rf) / delta) + 2.0f);
        } else {
            h = 60.0f * (((rf - gf) / delta) + 4.0f);
        }
    }
    if (h < 0.0f) h += 360.0f;
    float s = (cmax > 0.0001f) ? (delta / cmax) : 0.0f;
    float v = cmax;
    if (out_h) *out_h = h;
    if (out_s) *out_s = s;
    if (out_v) *out_v = v;
}

static inline void sync_theme_code_string(void) {
    snprintf(g_theme.code_buf, sizeof(g_theme.code_buf),
             "rgba(%d, %d, %d, %.2f)",
             (int)g_theme.r, (int)g_theme.g, (int)g_theme.b,
             (double)g_theme.a / 255.0);
}

// Computes effective RGB of active theme blended with its alpha over dark base so UI elements stay readable
static inline void get_theme_effective_rgb(uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
    float af = (float)g_theme.a / 255.0f;
    if (af < 0.15f) af = 0.15f;
    int er = (int)(36.0f * (1.0f - af) + (float)g_theme.r * af + 0.5f);
    int eg = (int)(36.0f * (1.0f - af) + (float)g_theme.g * af + 0.5f);
    int eb = (int)(36.0f * (1.0f - af) + (float)g_theme.b * af + 0.5f);
    if (er < 0) er = 0; else if (er > 255) er = 255;
    if (eg < 0) eg = 0; else if (eg > 255) eg = 255;
    if (eb < 0) eb = 0; else if (eb > 255) eb = 255;
    if (out_r) *out_r = (uint8_t)er;
    if (out_g) *out_g = (uint8_t)eg;
    if (out_b) *out_b = (uint8_t)eb;
}

// Computes a subtle theme-tinted surface fill color for headers, active buttons, and scrollbar thumbs
static inline void get_theme_surface_rgb(uint8_t base_val, float weight, uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
    uint8_t er, eg, eb;
    get_theme_effective_rgb(&er, &eg, &eb);
    int sr = (int)((float)base_val * (1.0f - weight) + (float)er * weight + 0.5f);
    int sg = (int)((float)base_val * (1.0f - weight) + (float)eg * weight + 0.5f);
    int sb = (int)((float)base_val * (1.0f - weight) + (float)eb * weight + 0.5f);
    if (sr < 0) sr = 0; else if (sr > 255) sr = 255;
    if (sg < 0) sg = 0; else if (sg > 255) sg = 255;
    if (sb < 0) sb = 0; else if (sb > 255) sb = 255;
    if (out_r) *out_r = (uint8_t)sr;
    if (out_g) *out_g = (uint8_t)sg;
    if (out_b) *out_b = (uint8_t)sb;
}

static inline SDL_Color get_theme_accent_sdl_color(void) {
    uint8_t er, eg, eb;
    get_theme_effective_rgb(&er, &eg, &eb);
    int ar = er + 25; if (ar > 255) ar = 255;
    int ag = eg + 25; if (ag > 255) ag = 255;
    int ab = eb + 20; if (ab > 255) ab = 255;
    SDL_Color c = { (Uint8)ar, (Uint8)ag, (Uint8)ab, 255 };
    return c;
}
static void init_default_saved_themes(void) {
    if (g_theme.num_saved > 0) return;
    static const struct { const char *name; uint8_t r, g, b, a; } defs[] = {
        { "Default Orange", 205, 138,  72, 255 },
        { "Emerald Green",   77, 191,  59, 130 },
        { "Forest Green",    77, 191,  59, 255 },
        { "Studio Blue",     68, 150, 235, 255 },
        { "Royal Purple",   162,  98, 232, 255 },
        { "Crimson Red",    224,  78,  78, 255 },
        { "Cyber Cyan",      48, 198, 208, 255 }
    };
    int nd = (int)(sizeof(defs) / sizeof(defs[0]));
    for (int i = 0; i < nd && g_theme.num_saved < MAX_SAVED_THEMES; i++) {
        SavedThemeEntry *st = &g_theme.saved[g_theme.num_saved++];
        strncpy(st->name, defs[i].name, sizeof(st->name) - 1);
        st->name[sizeof(st->name) - 1] = '\0';
        st->r = defs[i].r;
        st->g = defs[i].g;
        st->b = defs[i].b;
        st->a = defs[i].a;
    }
}
static void save_themes_settings_json(void);
#endif // DAW_THEME_STATE_DEFINED

// Helper to draw a 1px or 2px THEME-COLORED border rectangle across all UI elements (default Orange)
static inline void draw_brown_border_rect(SDL_Renderer *ren, const SDL_Rect *r, bool thick, bool faded) {
    if (!r || r->w <= 0 || r->h <= 0) return;
    uint8_t br, bg, bb;
    get_theme_effective_rgb(&br, &bg, &bb);
    if (thick) {
        SDL_SetRenderDrawColor(ren, br, bg, bb, 255);
        SDL_RenderDrawRect(ren, r);
        if (r->w > 2 && r->h > 2) {
            SDL_Rect inner = { r->x + 1, r->y + 1, r->w - 2, r->h - 2 };
            SDL_RenderDrawRect(ren, &inner);
        }
    } else if (faded) {
        SDL_SetRenderDrawColor(ren, (uint8_t)((br * 58) / 100), (uint8_t)((bg * 57) / 100), (uint8_t)((bb * 63) / 100), 255);
        SDL_RenderDrawRect(ren, r);
    } else {
        SDL_SetRenderDrawColor(ren, (uint8_t)((br * 74) / 100), (uint8_t)((bg * 71) / 100), (uint8_t)((bb * 75) / 100), 255);
        SDL_RenderDrawRect(ren, r);
    }
}

// Helper to draw text clipped strictly inside box_bounds intersected with parent_clip
static void draw_text_clipped(SDL_Renderer *ren, const char *text, int x, int y, SDL_Color col,
                              const SDL_Rect *box_bounds, const SDL_Rect *parent_clip) {
    if (!text || !box_bounds) return;
    int x1 = box_bounds->x;
    int y1 = box_bounds->y;
    int x2 = box_bounds->x + box_bounds->w;
    int y2 = box_bounds->y + box_bounds->h;

    if (parent_clip) {
        if (x1 < parent_clip->x) x1 = parent_clip->x;
        if (y1 < parent_clip->y) y1 = parent_clip->y;
        if (x2 > parent_clip->x + parent_clip->w) x2 = parent_clip->x + parent_clip->w;
        if (y2 > parent_clip->y + parent_clip->h) y2 = parent_clip->y + parent_clip->h;
    }

    if (x2 > x1 && y2 > y1) {
        SDL_Rect c_rect = { x1, y1, x2 - x1, y2 - y1 };
        SDL_RenderSetClipRect(ren, &c_rect);
        draw_text(ren, text, x, y, col);
    }
    SDL_RenderSetClipRect(ren, parent_clip);
}

// Renders the Tools -> Themes ARGB Popup Window (matching image_0d0094.png & image_0d0834.png)
static void render_themes_popup_window(SDL_Renderer *ren) {
    if (!g_theme.is_open) return;
    init_default_saved_themes();

    SDL_Rect *wr = &g_theme.win_rect;
    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
    SDL_Color c_white = { 228, 228, 228, 255 };
    SDL_Color c_muted = { 150, 145, 140, 255 };
    SDL_Color c_accent = get_theme_accent_sdl_color();

    // Drop Shadow
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Rect shadow = { wx + 6, wy + 6, ww, wh };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 175);
    SDL_RenderFillRect(ren, &shadow);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    // Main Window Box
    SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
    SDL_RenderFillRect(ren, wr);
    draw_brown_border_rect(ren, wr, true, false);

    // Title Bar
    SDL_Rect hdr = { wx, wy, ww, 30 };
    uint8_t hr, hg, hb;
    get_theme_surface_rgb(28, 0.22f, &hr, &hg, &hb);
    SDL_SetRenderDrawColor(ren, hr, hg, hb, 255);
    SDL_RenderFillRect(ren, &hdr);
    draw_brown_border_rect(ren, &hdr, false, false);
    draw_text(ren, "Themes - ARGB Color Settings", wx + 14, wy + 9, c_white);
    draw_text(ren, "X", wx + ww - 20, wy + 9, c_white);

    // ---- LEFT SIDE: 2D SV Square, Hue Rainbow Slider, Checkerboard Alpha Slider, rgba() Code Box ----
    int p_x = wx + 18;
    int sv_y = wy + 44;
    int sv_w = 256;
    int sv_h = 164;

    // 1. 2D Saturation / Value Color Square (matching image_0d0094.png top box)
    for (int gy = 0; gy < sv_h; gy += 2) {
        float v_val = 1.0f - ((float)gy / (float)(sv_h - 1));
        for (int gx = 0; gx < sv_w; gx += 2) {
            float s_val = (float)gx / (float)(sv_w - 1);
            uint8_t cr, cg, cb;
            theme_hsv_to_rgb(g_theme.h, s_val, v_val, &cr, &cg, &cb);
            SDL_Rect cell = { p_x + gx, sv_y + gy, 2, 2 };
            SDL_SetRenderDrawColor(ren, cr, cg, cb, 255);
            SDL_RenderFillRect(ren, &cell);
        }
    }
    SDL_Rect sv_rect = { p_x, sv_y, sv_w, sv_h };
    draw_brown_border_rect(ren, &sv_rect, false, false);

    // Selector square inside 2D SV picker
    int cur_sv_x = p_x + (int)(g_theme.s * (float)(sv_w - 1));
    int cur_sv_y = sv_y + (int)((1.0f - g_theme.v) * (float)(sv_h - 1));
    SDL_Rect sv_knob_out = { cur_sv_x - 7, cur_sv_y - 7, 14, 14 };
    SDL_Rect sv_knob_in  = { cur_sv_x - 6, cur_sv_y - 6, 12, 12 };
    SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
    SDL_RenderDrawRect(ren, &sv_knob_out);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderDrawRect(ren, &sv_knob_in);

    // 2. Rainbow Hue Slider Bar (matching image_0d0094.png middle bar)
    int hue_y = wy + 220;
    int bar_h = 18;
    for (int gx = 0; gx < sv_w; gx++) {
        float h_deg = ((float)gx / (float)(sv_w - 1)) * 360.0f;
        uint8_t hr_c, hg_c, hb_c;
        theme_hsv_to_rgb(h_deg, 1.0f, 1.0f, &hr_c, &hg_c, &hb_c);
        SDL_SetRenderDrawColor(ren, hr_c, hg_c, hb_c, 255);
        SDL_RenderDrawLine(ren, p_x + gx, hue_y, p_x + gx, hue_y + bar_h - 1);
    }
    SDL_Rect hue_rect = { p_x, hue_y, sv_w, bar_h };
    draw_brown_border_rect(ren, &hue_rect, false, false);

    int hue_thumb_x = p_x + (int)((g_theme.h / 360.0f) * (float)(sv_w - 1));
    uint8_t pure_r, pure_g, pure_b;
    theme_hsv_to_rgb(g_theme.h, 1.0f, 1.0f, &pure_r, &pure_g, &pure_b);
    SDL_Rect hue_thumb = { hue_thumb_x - 5, hue_y - 3, 10, bar_h + 6 };
    SDL_SetRenderDrawColor(ren, pure_r, pure_g, pure_b, 255);
    SDL_RenderFillRect(ren, &hue_thumb);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderDrawRect(ren, &hue_thumb);

    // 3. Checkerboard Alpha Slider Bar (matching image_0d0094.png bottom bar)
    int alpha_y = wy + 250;
    for (int gy = 0; gy < bar_h; gy += 6) {
        for (int gx = 0; gx < sv_w; gx += 6) {
            bool light = (((gx / 6) + (gy / 6)) % 2) == 0;
            uint8_t chk_c = light ? 215 : 155;
            float a_u = (float)gx / (float)(sv_w - 1);
            uint8_t br_c = (uint8_t)((float)chk_c * (1.0f - a_u) + (float)g_theme.r * a_u + 0.5f);
            uint8_t bg_c = (uint8_t)((float)chk_c * (1.0f - a_u) + (float)g_theme.g * a_u + 0.5f);
            uint8_t bb_c = (uint8_t)((float)chk_c * (1.0f - a_u) + (float)g_theme.b * a_u + 0.5f);
            int cw = (gx + 6 > sv_w) ? (sv_w - gx) : 6;
            int ch = (gy + 6 > bar_h) ? (bar_h - gy) : 6;
            SDL_Rect cell = { p_x + gx, alpha_y + gy, cw, ch };
            SDL_SetRenderDrawColor(ren, br_c, bg_c, bb_c, 255);
            SDL_RenderFillRect(ren, &cell);
        }
    }
    SDL_Rect alpha_rect = { p_x, alpha_y, sv_w, bar_h };
    draw_brown_border_rect(ren, &alpha_rect, false, false);

    int alpha_thumb_x = p_x + (int)(((float)g_theme.a / 255.0f) * (float)(sv_w - 1));
    SDL_Rect alpha_thumb = { alpha_thumb_x - 5, alpha_y - 3, 10, bar_h + 6 };
    SDL_SetRenderDrawColor(ren, g_theme.r, g_theme.g, g_theme.b, 255);
    SDL_RenderFillRect(ren, &alpha_thumb);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderDrawRect(ren, &alpha_thumb);

    // 4. Editable rgba(R, G, B, A) / ARGB Code Input Box (matching image_0d0834.png)
    int code_y = wy + 284;
    SDL_Rect code_box = { p_x, code_y, 212, 30 };
    SDL_SetRenderDrawColor(ren, 16, 15, 14, 255);
    SDL_RenderFillRect(ren, &code_box);
    draw_brown_border_rect(ren, &code_box, g_theme.code_focused, false);

    SDL_Rect code_clip = { code_box.x + 4, code_box.y + 2, code_box.w - 8, code_box.h - 4 };
    draw_text_clipped(ren, g_theme.code_buf, code_box.x + 8, code_y + 9, c_white, &code_clip, NULL);
    if (g_theme.code_focused && ((SDL_GetTicks() / 450) % 2 == 0)) {
        int cx = code_box.x + 8 + (int)strlen(g_theme.code_buf) * 8;
        if (cx < code_box.x + code_box.w - 4) {
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            SDL_RenderDrawLine(ren, cx, code_y + 6, cx, code_y + 24);
        }
    }

    // Live Color Swatch next to rgba() code box
    SDL_Rect swatch_box = { p_x + 218, code_y, 38, 30 };
    SDL_SetRenderDrawColor(ren, g_theme.r, g_theme.g, g_theme.b, 255);
    SDL_RenderFillRect(ren, &swatch_box);
    draw_brown_border_rect(ren, &swatch_box, true, false);

    // ARGB Hex Readout
    char hex_lbl[64];
    snprintf(hex_lbl, sizeof(hex_lbl), "ARGB Hex: #%02X%02X%02X%02X",
             (unsigned int)g_theme.a, (unsigned int)g_theme.r,
             (unsigned int)g_theme.g, (unsigned int)g_theme.b);
    draw_text(ren, hex_lbl, p_x, code_y + 38, c_accent);

    // 5. Left Bottom Buttons: [Save Theme] and [Default Orange]
    int b_y = wy + 350;
    SDL_Rect btn_save_th = { p_x, b_y, 124, 30 };
    SDL_SetRenderDrawColor(ren, hr, hg, hb, 255);
    SDL_RenderFillRect(ren, &btn_save_th);
    draw_brown_border_rect(ren, &btn_save_th, true, false);
    draw_text(ren, "Save Theme", p_x + 22, b_y + 9, c_white);

    SDL_Rect btn_def_th = { p_x + 132, b_y, 124, 30 };
    SDL_SetRenderDrawColor(ren, 42, 38, 34, 255);
    SDL_RenderFillRect(ren, &btn_def_th);
    draw_brown_border_rect(ren, &btn_def_th, false, false);
    draw_text(ren, "Default Orange", p_x + 138, b_y + 9, c_white);

    // ---- RIGHT SIDE: Saved Themes List (Keep Themes for Future) ----
    int rx = wx + 292;
    int rw = ww - 308;
    draw_text(ren, "Saved Themes (Click to Apply):", rx, wy + 44, c_white);

    SDL_Rect list_box = { rx, wy + 64, rw, 316 };
    SDL_SetRenderDrawColor(ren, 18, 17, 16, 255);
    SDL_RenderFillRect(ren, &list_box);
    draw_brown_border_rect(ren, &list_box, false, true);

    int content_h = g_theme.num_saved * 28 + 8;
    int max_sc = (content_h > list_box.h) ? (content_h - list_box.h) : 0;
    if (g_theme.saved_scroll_y > max_sc) g_theme.saved_scroll_y = max_sc;
    if (g_theme.saved_scroll_y < 0) g_theme.saved_scroll_y = 0;

    SDL_Rect list_clip = { list_box.x + 2, list_box.y + 2, list_box.w - 18, list_box.h - 4 };
    SDL_RenderSetClipRect(ren, &list_clip);
    for (int i = 0; i < g_theme.num_saved; i++) {
        int ry = list_box.y + 4 + i * 28 - g_theme.saved_scroll_y;
        if (ry + 26 < list_box.y || ry > list_box.y + list_box.h) continue;

        SavedThemeEntry *st = &g_theme.saved[i];
        bool is_active = (st->r == g_theme.r && st->g == g_theme.g && st->b == g_theme.b && st->a == g_theme.a);
        SDL_Rect row_r = { list_box.x + 4, ry, list_box.w - 22, 25 };
        SDL_SetRenderDrawColor(ren, is_active ? hr : 26, is_active ? hg : 24, is_active ? hb : 22, 255);
        SDL_RenderFillRect(ren, &row_r);
        draw_brown_border_rect(ren, &row_r, is_active, !is_active);

        // Swatch inside row
        SDL_Rect sw = { row_r.x + 5, ry + 4, 17, 17 };
        SDL_SetRenderDrawColor(ren, st->r, st->g, st->b, 255);
        SDL_RenderFillRect(ren, &sw);
        draw_brown_border_rect(ren, &sw, false, false);

        char item_lbl[128];
        snprintf(item_lbl, sizeof(item_lbl), "%s (%d,%d,%d,%.2f)",
                 st->name, (int)st->r, (int)st->g, (int)st->b, (double)st->a / 255.0);
        SDL_Rect row_clip = { row_r.x + 28, ry + 2, row_r.w - 32, 20 };
        draw_text_clipped(ren, item_lbl, row_r.x + 28, ry + 6, is_active ? c_white : c_muted, &row_clip, &list_clip);
    }
    SDL_RenderSetClipRect(ren, NULL);

    // Scrollbar for Saved Themes list
    SDL_Rect sb_track = { list_box.x + list_box.w - 14, list_box.y + 2, 12, list_box.h - 4 };
    SDL_SetRenderDrawColor(ren, 24, 22, 20, 255);
    SDL_RenderFillRect(ren, &sb_track);
    draw_brown_border_rect(ren, &sb_track, false, true);

    int thumb_h = (content_h > list_box.h && content_h > 0)
                  ? fmax(24, (sb_track.h * list_box.h) / content_h)
                  : sb_track.h;
    int thumb_y = sb_track.y + ((max_sc > 0) ? ((g_theme.saved_scroll_y * (sb_track.h - thumb_h)) / max_sc) : 0);
    SDL_Rect sb_thumb = { sb_track.x + 1, thumb_y, sb_track.w - 2, thumb_h };
    uint8_t tr_c, tg_c, tb_c;
    get_theme_surface_rgb(42, 0.45f, &tr_c, &tg_c, &tb_c);
    SDL_SetRenderDrawColor(ren, tr_c, tg_c, tb_c, 255);
    SDL_RenderFillRect(ren, &sb_thumb);
    draw_brown_border_rect(ren, &sb_thumb, false, false);

    // Footer Status Line
    const char *st_txt = g_theme.status_msg[0]
        ? g_theme.status_msg
        : "Saved permanently to themes_settings.json & keyboard_shortcuts.json";
    SDL_Rect st_clip = { wx + 18, wy + wh - 28, ww - 36, 20 };
    draw_text_clipped(ren, st_txt, wx + 18, wy + wh - 24, c_muted, &st_clip, NULL);
}

// Generates a 64x64 studio audio-editing app icon surface (dark studio console, glowing orange waveform, red REC badge)
// and attaches it to the SDL window.
static void set_daw_window_thumbnail_icon(SDL_Window *win) {
    if (!win) return;
    static uint32_t icon_px[64 * 64];
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            uint8_t r = 22, g = 20, b = 18, a = 255;
            // Rounded corner mask
            int dx = (x < 8) ? (8 - x) : ((x > 55) ? (x - 55) : 0);
            int dy = (y < 8) ? (8 - y) : ((y > 55) ? (y - 55) : 0);
            if (dx * dx + dy * dy > 64) {
                icon_px[y * 64 + x] = 0x00000000;
                continue;
            }
            // Outer copper-orange border
            if (x <= 2 || x >= 61 || y <= 2 || y >= 61 || (dx * dx + dy * dy >= 42)) {
                r = 215; g = 138; b = 62;
            } else {
                // Subtle center horizontal axis line
                if (y == 34) { r = 90; g = 62; b = 38; }
                // Synthesized audio waveform bars across x = 8..55
                if (x >= 8 && x <= 55) {
                    float u = (float)(x - 8) / 47.0f;
                    float env = sinf(u * 3.14159265f);
                    float wave = fabsf(sinf(u * 18.84955f) * 0.7f + cosf(u * 9.42477f) * 0.3f);
                    int half_h = (int)(env * wave * 20.0f) + 2;
                    if (abs(y - 34) <= half_h) {
                        if (abs(y - 34) <= half_h / 2) {
                            r = 255; g = 185; b = 75;
                        } else {
                            r = 215; g = 125; b = 45;
                        }
                    }
                }
                // Red round REC dot in top-right corner of the app icon (center 50, 14, radius 5)
                int rdx = x - 50, rdy = y - 14;
                if (rdx * rdx + rdy * rdy <= 25) {
                    r = 240; g = 42; b = 42;
                }
            }
            icon_px[y * 64 + x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(
        icon_px, 64, 64, 32, 64 * 4,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000
    );
    if (surf) {
        SDL_SetWindowIcon(win, surf);
        SDL_FreeSurface(surf);
    }
}

// Renders the "Processing, please wait..." stability popup window and pumps SDL events so the app never freezes
static void render_processing_wait_overlay(SDL_Renderer *ren, SDL_Window *win) {
    if (!g_processing_popup_open || !ren || !win) return;
    int win_w = 1280, win_h = 720;
    SDL_GetWindowSize(win, &win_w, &win_h);

    int pw = 440, ph = 136;
    int px = (win_w - pw) / 2;
    int py = (win_h - ph) / 2;

    SDL_Color c_white  = { 228, 228, 228, 255 };
    SDL_Color c_muted  = { 155, 150, 145, 255 };
    SDL_Color c_accent = get_theme_accent_sdl_color();

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Rect shadow = { px + 6, py + 6, pw, ph };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 175);
    SDL_RenderFillRect(ren, &shadow);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    SDL_Rect box = { px, py, pw, ph };
    SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
    SDL_RenderFillRect(ren, &box);
    draw_brown_border_rect(ren, &box, true, false);

    uint8_t hr, hg, hb;
    get_theme_surface_rgb(32, 0.22f, &hr, &hg, &hb);
    SDL_Rect hdr = { px, py, pw, 30 };
    SDL_SetRenderDrawColor(ren, hr, hg, hb, 255);
    SDL_RenderFillRect(ren, &hdr);
    draw_brown_border_rect(ren, &hdr, false, false);
    draw_text(ren, g_processing_title[0] ? g_processing_title : "Processing, please wait...", px + 14, py + 9, c_white);

    SDL_Rect d_clip = { px + 16, py + 42, pw - 32, 20 };
    draw_text_clipped(ren, g_processing_detail[0] ? g_processing_detail : "Stabilizing audio timeline...", px + 16, py + 44, c_accent, &d_clip, NULL);

    SDL_Rect bar_bg = { px + 16, py + 72, pw - 32, 24 };
    SDL_SetRenderDrawColor(ren, 15, 14, 13, 255);
    SDL_RenderFillRect(ren, &bar_bg);

    int pct = g_processing_pct;
    if (pct < 0) pct = 50;
    if (pct > 100) pct = 100;
    int fill_w = ((bar_bg.w - 4) * pct) / 100;
    if (fill_w > 0) {
        SDL_Rect bar_fill = { bar_bg.x + 2, bar_bg.y + 2, fill_w, bar_bg.h - 4 };
        SDL_SetRenderDrawColor(ren, c_accent.r, c_accent.g, c_accent.b, 255);
        SDL_RenderFillRect(ren, &bar_fill);
    }
    draw_brown_border_rect(ren, &bar_bg, true, false);

    char pbuf[64];
    snprintf(pbuf, sizeof(pbuf), "Processing... %d%%", pct);
    draw_text(ren, pbuf, bar_bg.x + 12, bar_bg.y + 6, c_white);
    draw_text(ren, "Please wait - window will close automatically when ready.", px + 16, py + 106, c_muted);
}

static void show_processing_wait_modal(SDL_Renderer *ren, SDL_Window *win, const char *title, const char *detail, int pct) {
    g_processing_popup_open = true;
    g_processing_close_at_ticks = 0;
    if (title) {
        strncpy(g_processing_title, title, sizeof(g_processing_title) - 1);
        g_processing_title[sizeof(g_processing_title) - 1] = '\0';
    }
    if (detail) {
        strncpy(g_processing_detail, detail, sizeof(g_processing_detail) - 1);
        g_processing_detail[sizeof(g_processing_detail) - 1] = '\0';
    }
    g_processing_pct = pct;
    if (ren && win) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_DROPFILE && ev.drop.file) SDL_free(ev.drop.file);
        }
        render_processing_wait_overlay(ren, win);
        SDL_RenderPresent(ren);
    }
}

static void finish_processing_wait_modal(Uint32 hold_ms) {
    if (hold_ms == 0) {
        g_processing_popup_open = false;
        g_processing_close_at_ticks = 0;
    } else {
        g_processing_close_at_ticks = SDL_GetTicks() + hold_ms;
    }
}

// Renders the Simple, Stabilized Box-Based "Dynamic split" popup window (Requirement #8)
static void render_dynamic_split_window(SDL_Renderer *ren) {
    if (!g_dyn_split.is_open) return;

    SDL_Rect *wr = &g_dyn_split.win_rect;
    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
    SDL_Color c_white  = { 228, 228, 228, 255 };
    SDL_Color c_muted  = { 155, 150, 145, 255 };
    SDL_Color c_accent = get_theme_accent_sdl_color();

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Rect shadow = { wx + 6, wy + 6, ww, wh };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 175);
    SDL_RenderFillRect(ren, &shadow);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
    SDL_RenderFillRect(ren, wr);
    draw_brown_border_rect(ren, wr, true, false);

    uint8_t hr, hg, hb;
    get_theme_surface_rgb(32, 0.22f, &hr, &hg, &hb);
    SDL_Rect hdr = { wx, wy, ww, 30 };
    SDL_SetRenderDrawColor(ren, hr, hg, hb, 255);
    SDL_RenderFillRect(ren, &hdr);
    draw_brown_border_rect(ren, &hdr, false, false);
    draw_text(ren, "Dynamic split", wx + 14, wy + 9, c_white);
    draw_text(ren, "X", wx + ww - 20, wy + 9, c_white);

    // Simple Box-Based Parameter Rows:
    // 1. Min slice length (ms):   [ 20 ]   ms
    // 2. Min silence length (ms): [ 500 ]  ms
    // 3. Threshold (dB):          [ -50.0 ] dB
    // 4. Hysteresis (dB):         [ -6.0 ]  dB
    // 5. Remove silent areas:     [x]
    // 6. Leading pad (ms):        [ 50 ]   ms
    // 7. Trailing pad (ms):       [ 150 ]  ms
    // 8. Action to perform:       [ Selected clip item ]  [ Selected Track ]
    int lbl_x = wx + 22;
    int box_x = wx + 250;
    int box_w = 110;
    int unit_x = box_x + box_w + 10;

    char v1[32], v2[32], v3[32], v4[32], v5[32], v6[32];
    snprintf(v1, sizeof(v1), "%d", g_dyn_split.min_slice_ms);
    snprintf(v2, sizeof(v2), "%d", g_dyn_split.min_silence_ms);
    snprintf(v3, sizeof(v3), "%.1f", g_dyn_split.gate_threshold_db);
    snprintf(v4, sizeof(v4), "%.1f", g_dyn_split.gate_hysteresis_db);
    snprintf(v5, sizeof(v5), "%d", g_dyn_split.leading_pad_ms);
    snprintf(v6, sizeof(v6), "%d", g_dyn_split.trailing_pad_ms);

    struct { int id; int y; const char *lbl; const char *val; const char *unit; } rows[6] = {
        { 1, wy + 44,  "Min slice length (ms):",   v1, "ms" },
        { 2, wy + 78,  "Min silence length (ms):", v2, "ms" },
        { 3, wy + 112, "Threshold (dB):",          v3, "dB" },
        { 4, wy + 146, "Hysteresis (dB):",         v4, "dB" },
        { 5, wy + 214, "Leading pad (ms):",        v5, "ms" },
        { 6, wy + 248, "Trailing pad (ms):",       v6, "ms" }
    };

    for (int r = 0; r < 6; r++) {
        int ry = rows[r].y;
        bool is_ed = (g_dyn_split.editing_field_id == rows[r].id);
        draw_text(ren, rows[r].lbl, lbl_x, ry + 5, c_white);
        SDL_Rect bx = { box_x, ry, box_w, 24 };
        SDL_SetRenderDrawColor(ren, 15, 14, 13, 255);
        SDL_RenderFillRect(ren, &bx);
        draw_brown_border_rect(ren, &bx, is_ed, false);
        const char *txt = is_ed ? g_dyn_split.edit_buf : rows[r].val;
        draw_text(ren, txt, bx.x + 8, ry + 5, is_ed ? c_accent : c_white);
        if (is_ed && ((SDL_GetTicks() / 450) % 2 == 0)) {
            int cx = bx.x + 8 + (int)strlen(txt) * 8;
            if (cx < bx.x + bx.w - 4) {
                SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                SDL_RenderDrawLine(ren, cx, ry + 4, cx, ry + 20);
            }
        }
        draw_text(ren, rows[r].unit, unit_x, ry + 5, c_muted);
    }

    // Row 5 (wy + 182): [x] Remove silent areas
    int rm_y = wy + 182;
    draw_text(ren, "Remove silent areas:", lbl_x, rm_y + 3, c_white);
    SDL_Rect cb_rm = { box_x, rm_y + 1, 20, 20 };
    SDL_SetRenderDrawColor(ren, g_dyn_split.remove_silent_areas ? hr + 24 : 15,
                                g_dyn_split.remove_silent_areas ? hg + 16 : 14,
                                g_dyn_split.remove_silent_areas ? hb + 10 : 13, 255);
    SDL_RenderFillRect(ren, &cb_rm);
    draw_brown_border_rect(ren, &cb_rm, g_dyn_split.remove_silent_areas, false);
    if (g_dyn_split.remove_silent_areas) draw_text(ren, "x", cb_rm.x + 6, cb_rm.y + 3, c_white);
    draw_text(ren, g_dyn_split.remove_silent_areas ? "Enabled" : "Disabled", box_x + 28, rm_y + 3, c_muted);

    // Row 8 (wy + 286): Action to perform: [ Selected clip item ]  or  [ Selected Track ]
    int act_y = wy + 286;
    draw_text(ren, "Action to perform:", lbl_x, act_y + 4, c_accent);

    int opt_y = wy + 308;
    bool mode_clip  = (g_dyn_split.action_mode == 0);
    bool mode_track = (g_dyn_split.action_mode == 1);

    SDL_Rect b_mode_clip = { wx + 22, opt_y, 200, 26 };
    SDL_SetRenderDrawColor(ren, mode_clip ? hr + 22 : 32, mode_clip ? hg + 14 : 28, mode_clip ? hb + 8 : 26, 255);
    SDL_RenderFillRect(ren, &b_mode_clip);
    draw_brown_border_rect(ren, &b_mode_clip, mode_clip, !mode_clip);
    draw_text(ren, "Selected clip item", b_mode_clip.x + 26, opt_y + 6, c_white);

    SDL_Rect b_mode_trk = { wx + 236, opt_y, 200, 26 };
    SDL_SetRenderDrawColor(ren, mode_track ? hr + 22 : 32, mode_track ? hg + 14 : 28, mode_track ? hb + 8 : 26, 255);
    SDL_RenderFillRect(ren, &b_mode_trk);
    draw_brown_border_rect(ren, &b_mode_trk, mode_track, !mode_track);
    draw_text(ren, "Selected Track", b_mode_trk.x + 42, opt_y + 6, c_white);

    // Bottom Buttons: [ Split ] and [ Cancel ]
    int btn_y = wy + wh - 42;
    if (g_dyn_split.status_msg[0] != '\0') {
        SDL_Rect st_c = { wx + 22, btn_y + 4, 210, 20 };
        draw_text_clipped(ren, g_dyn_split.status_msg, wx + 22, btn_y + 8, c_accent, &st_c, NULL);
    }

    SDL_Rect b_split = { wx + ww - 216, btn_y, 96, 28 };
    SDL_SetRenderDrawColor(ren, hr + 24, hg + 16, hb + 10, 255);
    SDL_RenderFillRect(ren, &b_split);
    draw_brown_border_rect(ren, &b_split, true, false);
    draw_text(ren, "Split", b_split.x + 28, btn_y + 8, c_white);

    SDL_Rect b_cancel = { wx + ww - 108, btn_y, 88, 28 };
    SDL_SetRenderDrawColor(ren, 44, 40, 38, 255);
    SDL_RenderFillRect(ren, &b_cancel);
    draw_brown_border_rect(ren, &b_cancel, false, false);
    draw_text(ren, "Cancel", b_cancel.x + 20, btn_y + 8, c_white);
}

// Commits any active numeric text box edit in the simplified Dynamic Split window
static void commit_dyn_split_field_edit(void) {
    if (g_dyn_split.editing_field_id <= 0) return;
    double v = atof(g_dyn_split.edit_buf);
    switch (g_dyn_split.editing_field_id) {
        case 1: g_dyn_split.min_slice_ms = (int)(v < 5 ? 5 : (v > 5000 ? 5000 : v)); break;
        case 2: g_dyn_split.min_silence_ms = (int)(v < 10 ? 10 : (v > 10000 ? 10000 : v)); break;
        case 3: g_dyn_split.gate_threshold_db = (float)(v < -90.0 ? -90.0 : (v > 0.0 ? 0.0 : v)); break;
        case 4: g_dyn_split.gate_hysteresis_db = (float)(v < -30.0 ? -30.0 : (v > 0.0 ? 0.0 : v)); break;
        case 5: g_dyn_split.leading_pad_ms = (int)(v < 0 ? 0 : (v > 2000 ? 2000 : v)); break;
        case 6: g_dyn_split.trailing_pad_ms = (int)(v < 0 ? 0 : (v > 2000 ? 2000 : v)); break;
        default: break;
    }
    g_dyn_split.editing_field_id = 0;
    SDL_StopTextInput();
    save_themes_settings_json();
}

// Renders the "Reposition Items" popup window (matching image_4f535d.png)
static void render_reposition_window(SDL_Renderer *ren) {
    if (!g_reposition.is_open) return;

    SDL_Rect *wr = &g_reposition.win_rect;
    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
    SDL_Color c_white  = { 228, 228, 228, 255 };
    SDL_Color c_muted  = { 155, 150, 145, 255 };
    SDL_Color c_accent = get_theme_accent_sdl_color();

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Rect shadow = { wx + 6, wy + 6, ww, wh };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 175);
    SDL_RenderFillRect(ren, &shadow);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
    SDL_RenderFillRect(ren, wr);
    draw_brown_border_rect(ren, wr, true, false);

    uint8_t hr, hg, hb;
    get_theme_surface_rgb(32, 0.22f, &hr, &hg, &hb);
    SDL_Rect hdr = { wx, wy, ww, 30 };
    SDL_SetRenderDrawColor(ren, hr, hg, hb, 255);
    SDL_RenderFillRect(ren, &hdr);
    draw_brown_border_rect(ren, &hdr, false, false);
    draw_text(ren, "Reposition Items", wx + 14, wy + 9, c_white);
    draw_text(ren, "X", wx + ww - 20, wy + 9, c_white);

    // Group Box: "Add time interval to"
    SDL_Rect grp = { wx + 18, wy + 42, 210, 76 };
    SDL_SetRenderDrawColor(ren, 20, 19, 18, 255);
    SDL_RenderFillRect(ren, &grp);
    draw_brown_border_rect(ren, &grp, false, true);
    draw_text(ren, "Add time interval to", wx + 26, wy + 48, c_accent);

    // Radio 0: ( ) Item start
    bool r0 = (g_reposition.mode == 0);
    SDL_Rect rad0 = { wx + 32, wy + 68, 14, 14 };
    SDL_SetRenderDrawColor(ren, 14, 14, 14, 255);
    SDL_RenderFillRect(ren, &rad0);
    draw_brown_border_rect(ren, &rad0, r0, false);
    if (r0) {
        SDL_Rect dot0 = { rad0.x + 3, rad0.y + 3, 8, 8 };
        SDL_SetRenderDrawColor(ren, c_accent.r, c_accent.g, c_accent.b, 255);
        SDL_RenderFillRect(ren, &dot0);
    }
    draw_text(ren, "Item start", wx + 54, wy + 68, c_white);

    // Radio 1: (*) Item end
    bool r1 = (g_reposition.mode == 1);
    SDL_Rect rad1 = { wx + 32, wy + 92, 14, 14 };
    SDL_SetRenderDrawColor(ren, 14, 14, 14, 255);
    SDL_RenderFillRect(ren, &rad1);
    draw_brown_border_rect(ren, &rad1, r1, false);
    if (r1) {
        SDL_Rect dot1 = { rad1.x + 3, rad1.y + 3, 8, 8 };
        SDL_SetRenderDrawColor(ren, c_accent.r, c_accent.g, c_accent.b, 255);
        SDL_RenderFillRect(ren, &dot1);
    }
    draw_text(ren, "Item end", wx + 54, wy + 92, c_white);

    // Time interval [ 1.00 ] seconds
    int ti_y = wy + 134;
    draw_text(ren, "Time interval", wx + 18, ti_y + 5, c_white);
    SDL_Rect ti_box = { wx + 132, ti_y, 86, 24 };
    SDL_SetRenderDrawColor(ren, 15, 14, 13, 255);
    SDL_RenderFillRect(ren, &ti_box);
    draw_brown_border_rect(ren, &ti_box, g_reposition.interval_focused, false);
    draw_text(ren, g_reposition.interval_str, ti_box.x + 8, ti_y + 5, c_white);
    if (g_reposition.interval_focused && ((SDL_GetTicks() / 450) % 2 == 0)) {
        int cx = ti_box.x + 8 + (int)strlen(g_reposition.interval_str) * 8;
        if (cx < ti_box.x + ti_box.w - 4) {
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            SDL_RenderDrawLine(ren, cx, ti_y + 4, cx, ti_y + 20);
        }
    }
    draw_text(ren, "seconds", wx + 226, ti_y + 5, c_muted);

    // Bottom Buttons: [ OK ]  [ Cancel ]
    int by = wy + wh - 42;
    SDL_Rect b_ok = { wx + ww - 204, by, 88, 28 };
    SDL_SetRenderDrawColor(ren, hr + 22, hg + 14, hb + 8, 255);
    SDL_RenderFillRect(ren, &b_ok);
    draw_brown_border_rect(ren, &b_ok, true, false);
    draw_text(ren, "OK", b_ok.x + 34, by + 8, c_white);

    SDL_Rect b_cancel = { wx + ww - 104, by, 88, 28 };
    SDL_SetRenderDrawColor(ren, 44, 40, 38, 255);
    SDL_RenderFillRect(ren, &b_cancel);
    draw_brown_border_rect(ren, &b_cancel, false, false);
    draw_text(ren, "Cancel", b_cancel.x + 20, by + 8, c_white);
}

// Renders the Lua & Python Custom Script Popup Window (when a user script creates a GUI or input dialog)
static void render_script_popup_window(SDL_Renderer *ren) {
    if (!g_script_popup.is_open) return;

    SDL_Rect *wr = &g_script_popup.win_rect;
    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
    SDL_Color c_white  = { 228, 228, 228, 255 };
    SDL_Color c_muted  = { 155, 150, 145, 255 };
    SDL_Color c_accent = get_theme_accent_sdl_color();

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Rect shadow = { wx + 6, wy + 6, ww, wh };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 175);
    SDL_RenderFillRect(ren, &shadow);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
    SDL_RenderFillRect(ren, wr);
    draw_brown_border_rect(ren, wr, true, false);

    uint8_t hr, hg, hb;
    get_theme_surface_rgb(32, 0.22f, &hr, &hg, &hb);
    SDL_Rect hdr = { wx, wy, ww, 30 };
    SDL_SetRenderDrawColor(ren, hr, hg, hb, 255);
    SDL_RenderFillRect(ren, &hdr);
    draw_brown_border_rect(ren, &hdr, false, false);

    char full_title[160];
    snprintf(full_title, sizeof(full_title), "%s [%s]",
             g_script_popup.title, g_script_popup.is_python ? "Python" : "Lua");
    draw_text(ren, full_title, wx + 14, wy + 9, c_white);
    draw_text(ren, "X", wx + ww - 20, wy + 9, c_white);

    for (int i = 0; i < g_script_popup.num_controls; i++) {
        ScriptPopupControl *ctrl = &g_script_popup.controls[i];
        int cy = wy + 46 + i * 38;
        if (cy + 30 > wy + wh - 48) break;

        if (ctrl->type == SCP_CTRL_LABEL) {
            draw_text(ren, ctrl->str_val, wx + 20, cy + 6, c_accent);
        } else if (ctrl->type == SCP_CTRL_CHECKBOX) {
            SDL_Rect cb = { wx + 20, cy + 4, 18, 18 };
            SDL_SetRenderDrawColor(ren, ctrl->bool_val ? hr + 20 : 14, ctrl->bool_val ? hg + 15 : 14, ctrl->bool_val ? hb + 10 : 14, 255);
            SDL_RenderFillRect(ren, &cb);
            draw_brown_border_rect(ren, &cb, ctrl->bool_val, false);
            if (ctrl->bool_val) draw_text(ren, "x", cb.x + 5, cb.y + 2, c_white);
            draw_text(ren, ctrl->label, wx + 46, cy + 6, c_white);
        } else if (ctrl->type == SCP_CTRL_SLIDER) {
            draw_text(ren, ctrl->label, wx + 20, cy + 6, c_white);
            int sl_x = wx + 180, sl_w = ww - 270;
            if (sl_w < 80) sl_w = 80;
            SDL_Rect trk = { sl_x, cy + 10, sl_w, 6 };
            SDL_SetRenderDrawColor(ren, 14, 14, 14, 255);
            SDL_RenderFillRect(ren, &trk);
            draw_brown_border_rect(ren, &trk, false, true);
            double span = ctrl->max_val - ctrl->min_val;
            float u = (span > 1e-9) ? (float)((ctrl->num_val - ctrl->min_val) / span) : 0.0f;
            if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
            int kx = sl_x + (int)(u * (float)(sl_w - 12));
            SDL_Rect knob = { kx, cy + 4, 12, 18 };
            SDL_SetRenderDrawColor(ren, hr + 35, hg + 25, hb + 15, 255);
            SDL_RenderFillRect(ren, &knob);
            draw_brown_border_rect(ren, &knob, true, false);
            snprintf(ctrl->str_val, sizeof(ctrl->str_val), "%.2f", ctrl->num_val);
            draw_text(ren, ctrl->str_val, sl_x + sl_w + 10, cy + 6, c_accent);
        } else {
            // SCP_CTRL_INPUT
            draw_text(ren, ctrl->label, wx + 20, cy + 6, c_white);
            SDL_Rect ibox = { wx + 180, cy + 1, ww - 200, 24 };
            bool is_foc = (g_script_popup.focused_ctrl_idx == i);
            SDL_SetRenderDrawColor(ren, 15, 14, 13, 255);
            SDL_RenderFillRect(ren, &ibox);
            draw_brown_border_rect(ren, &ibox, is_foc, false);
            draw_text(ren, ctrl->str_val, ibox.x + 8, cy + 6, c_white);
            if (is_foc && ((SDL_GetTicks() / 450) % 2 == 0)) {
                int cx = ibox.x + 8 + (int)strlen(ctrl->str_val) * 8;
                if (cx < ibox.x + ibox.w - 4) {
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_RenderDrawLine(ren, cx, cy + 4, cx, cy + 20);
                }
            }
        }
    }

    int by = wy + wh - 42;
    SDL_Rect b_ok = { wx + ww - 216, by, 100, 28 };
    SDL_SetRenderDrawColor(ren, hr + 22, hg + 14, hb + 8, 255);
    SDL_RenderFillRect(ren, &b_ok);
    draw_brown_border_rect(ren, &b_ok, true, false);
    draw_text(ren, "OK / Run", b_ok.x + 18, by + 8, c_white);

    SDL_Rect b_cancel = { wx + ww - 104, by, 88, 28 };
    SDL_SetRenderDrawColor(ren, 44, 40, 38, 255);
    SDL_RenderFillRect(ren, &b_cancel);
    draw_brown_border_rect(ren, &b_cancel, false, false);
    draw_text(ren, "Cancel", b_cancel.x + 20, by + 8, c_white);
    (void)c_muted;
}

// Computes equal-power crossfade gain (sin/cos S-law) for any overlapping clips and micro de-click ramps at sample time t_sec
static float compute_item_crossfade_gain(ProjectTab *p, int item_idx, double t_sec) {
    AudioItem *it = &p->items[item_idx];
    double it_s = it->start_time;
    double it_e = it->start_time + it->duration;
    if (t_sec < it_s || t_sec > it_e) return 0.0f;

    float env = 1.0f;
    bool has_left_xfade = false;
    bool has_right_xfade = false;

    for (int o = 0; o < p->num_items; o++) {
        if (o == item_idx) continue;
        AudioItem *ot = &p->items[o];
        if (ot->track_idx != it->track_idx) continue;

        double ot_s = ot->start_time;
        double ot_e = ot->start_time + ot->duration;
        double ov_s = fmax(it_s, ot_s);
        double ov_e = fmin(it_e, ot_e);

        if (ov_e > ov_s + 0.0005) {
            bool ot_is_earlier = (ot_s < it_s - 1e-6) || (fabs(ot_s - it_s) <= 1e-6 && o < item_idx);
            if (ot_is_earlier) {
                has_left_xfade = true;
                if (t_sec >= ov_s && t_sec <= ov_e) {
                    float u = (float)((t_sec - ov_s) / (ov_e - ov_s));
                    if (u < 0.0f) u = 0.0f;
                    if (u > 1.0f) u = 1.0f;
                    // Standard Equal-Power (Constant-Power) Fade-In: sin(u * pi/2)
                    env *= sinf(u * 1.57079632679f);
                }
            } else {
                has_right_xfade = true;
                if (t_sec >= ov_s && t_sec <= ov_e) {
                    float u = (float)((t_sec - ov_s) / (ov_e - ov_s));
                    if (u < 0.0f) u = 0.0f;
                    if (u > 1.0f) u = 1.0f;
                    // Standard Equal-Power (Constant-Power) Fade-Out: cos(u * pi/2)
                    env *= cosf(u * 1.57079632679f);
                }
            }
        }
    }

    // Manual clip edge Fade-In and Fade-Out envelope
    double man_fin  = get_item_fade_in(p, item_idx);
    double man_fout = get_item_fade_out(p, item_idx);
    if (man_fin > 0.001) {
        has_left_xfade = true;
        double rel_in = t_sec - it_s;
        if (rel_in < man_fin) {
            float u = (float)(rel_in / man_fin);
            if (u < 0.0f) u = 0.0f;
            if (u > 1.0f) u = 1.0f;
            env *= sinf(u * 1.57079632679f);
        }
    }
    if (man_fout > 0.001) {
        has_right_xfade = true;
        double rel_out = it_e - t_sec;
        if (rel_out < man_fout) {
            float u = (float)(rel_out / man_fout);
            if (u < 0.0f) u = 0.0f;
            if (u > 1.0f) u = 1.0f;
            env *= sinf(u * 1.57079632679f);
        }
    }

    // 2ms anti-click ramp on non-crossfaded hard cuts / touching edges
    const double edge_ramp = 0.002;
    if (!has_left_xfade && (t_sec - it_s) < edge_ramp && it->duration > edge_ramp * 2.0) {
        float u = (float)((t_sec - it_s) / edge_ramp);
        if (u >= 0.0f && u < 1.0f) env *= u;
    }
    if (!has_right_xfade && (it_e - t_sec) < edge_ramp && it->duration > edge_ramp * 2.0) {
        float u = (float)((it_e - t_sec) / edge_ramp);
        if (u >= 0.0f && u < 1.0f) env *= u;
    }

    return env;
}

// Writes a standard PCM WAV header for 16-bit, 24-bit, or 32-bit integer PCM at the chosen sample_rate and channels
static void write_wav_pcm_header(FILE *f, uint32_t total_frames, int sample_rate, int channels, int bit_depth) {
    uint32_t sr = (uint32_t)(sample_rate > 0 ? sample_rate : 48000);
    uint32_t ch = (uint32_t)(channels == 1 ? 1 : 2);
    uint32_t bps = (uint32_t)((bit_depth == 16 || bit_depth == 24 || bit_depth == 32) ? bit_depth : 24);
    uint32_t bytes_per_sample = bps / 8;
    uint16_t block_align = (uint16_t)(ch * bytes_per_sample);
    uint32_t byte_rate = sr * (uint32_t)block_align;
    uint32_t data_bytes = total_frames * (uint32_t)block_align;
    uint32_t riff_size = 36 + data_bytes;
    uint32_t fmt_size = 16;
    uint16_t audio_fmt = 1; // 1 = Integer PCM
    uint16_t ch16 = (uint16_t)ch;
    uint16_t bps16 = (uint16_t)bps;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_fmt, 2, 1, f);
    fwrite(&ch16, 2, 1, f);
    fwrite(&sr, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bps16, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
}

static inline float clamp_sample_f(float v) {
    if (v > 1.0f) return 1.0f;
    if (v < -1.0f) return -1.0f;
    return v;
}

static void write_pcm_frame_samples(FILE *f, float l, float r, int channels, int bit_depth) {
    float vals[2];
    if (channels == 1) {
        vals[0] = clamp_sample_f((l + r) * 0.5f);
    } else {
        vals[0] = clamp_sample_f(l);
        vals[1] = clamp_sample_f(r);
    }

    int count = (channels == 1) ? 1 : 2;
    for (int c = 0; c < count; c++) {
        float s = vals[c];
        if (bit_depth == 16) {
            int16_t v16 = (int16_t)(s * 32767.0f);
            fwrite(&v16, sizeof(int16_t), 1, f);
        } else if (bit_depth == 24) {
            int32_t v24 = (int32_t)(s * 8388607.0f);
            uint8_t b[3];
            b[0] = (uint8_t)(v24 & 0xFF);
            b[1] = (uint8_t)((v24 >> 8) & 0xFF);
            b[2] = (uint8_t)((v24 >> 16) & 0xFF);
            fwrite(b, 1, 3, f);
        } else {
            int32_t v32 = (int32_t)((double)s * 2147483647.0);
            fwrite(&v32, sizeof(int32_t), 1, f);
        }
    }
}

// Updates export processing percentage & elapsed/remaining time and refreshes the popup progress bar live
static void update_export_progress_ui(SDL_Renderer *ren, int pct, const char *stage_msg, bool force_present) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    g_export.progress_pct = pct;

    Uint32 now = SDL_GetTicks();
    g_export.elapsed_sec = (double)(now - g_export.render_start_ticks) / 1000.0;
    if (pct > 1 && pct < 100) {
        g_export.est_remaining_sec = g_export.elapsed_sec * ((double)(100 - pct) / (double)pct);
    } else {
        g_export.est_remaining_sec = 0.0;
    }
    if (stage_msg) {
        strncpy(g_export.status_msg, stage_msg, sizeof(g_export.status_msg) - 1);
        g_export.status_msg[sizeof(g_export.status_msg) - 1] = '\0';
    }

    if (ren && g_export.is_open && (force_present || (now - g_export.last_ui_present_ticks >= 30))) {
        g_export.last_ui_present_ticks = now;
        SDL_Rect *er = &g_export.win_rect;
        int ex = er->x, ey = er->y, ew = er->w;
        SDL_Color c_white = { 224, 224, 224, 255 };
        SDL_Color c_muted = { 150, 145, 140, 255 };

        // Clear progress region inside Export Media popup
        SDL_Rect prog_area = { ex + 16, ey + 464, ew - 32, 64 };
        SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
        SDL_RenderFillRect(ren, &prog_area);

        // Progress Bar Frame
        SDL_Rect bar_bg = { ex + 22, ey + 468, ew - 44, 24 };
        SDL_SetRenderDrawColor(ren, 16, 15, 14, 255);
        SDL_RenderFillRect(ren, &bar_bg);

        int fill_w = ((bar_bg.w - 4) * g_export.progress_pct) / 100;
        if (fill_w > 0) {
            SDL_Rect bar_fill = { bar_bg.x + 2, bar_bg.y + 2, fill_w, bar_bg.h - 4 };
            SDL_SetRenderDrawColor(ren, 195, 128, 54, 255);
            SDL_RenderFillRect(ren, &bar_fill);
        }
        draw_brown_border_rect(ren, &bar_bg, g_export.is_rendering, false);

        char pct_time_buf[160];
        if (g_export.is_rendering) {
            snprintf(pct_time_buf, sizeof(pct_time_buf),
                     "Processing: %d%%   |   Elapsed: %.2fs   |   Remaining: ~%.2fs",
                     g_export.progress_pct, g_export.elapsed_sec, g_export.est_remaining_sec);
        } else {
            snprintf(pct_time_buf, sizeof(pct_time_buf),
                     "Completed: %d%%   |   Total Processing Time: %.2fs",
                     g_export.progress_pct, g_export.elapsed_sec);
        }
        draw_text(ren, pct_time_buf, bar_bg.x + 12, bar_bg.y + 6, c_white);

        if (g_export.status_msg[0] != '\0') {
            SDL_Rect st_clip = { ex + 22, ey + 496, ew - 44, 24 };
            draw_text_clipped(ren, g_export.status_msg, ex + 22, ey + 500, c_muted, &st_clip, NULL);
        }

        SDL_RenderPresent(ren);
    }
}

// Renders a specific time range [exp_start, exp_end] into a WAV file at out_wav_path
// Filter options:
//   only_track_idx >= 0  -> only mix items on that track
//   only_item_idx  >= 0  -> only mix that single item
static bool render_range_to_wav_file(ProjectTab *p, SDL_Renderer *ren, const char *out_wav_path,
                                     double exp_start, double exp_end,
                                     int only_track_idx, int only_item_idx,
                                     int target_sr, int channels, int bit_depth,
                                     bool two_pass, int pct_start, int pct_end) {
    if (!p || !out_wav_path || exp_end <= exp_start + 0.001) return false;
    if (target_sr != 44100 && target_sr != 48000) target_sr = 48000;
    if (channels != 1 && channels != 2) channels = 2;
    if (bit_depth != 16 && bit_depth != 24 && bit_depth != 32) bit_depth = 24;

    uint32_t total_out_frames = (uint32_t)((exp_end - exp_start) * (double)target_sr);
    if (total_out_frames == 0) total_out_frames = 1;

    bool any_solo = false;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->tracks[t].solo) { any_solo = true; break; }
    }

    float norm_gain = 1.0f;
    int render_pct_s = pct_start;

    // Pass 1 (if Two-Pass Render is checked): scan peak amplitude across the target range
    if (two_pass) {
        int pass1_end = pct_start + (pct_end - pct_start) * 35 / 100;
        render_pct_s = pass1_end;
        float max_peak = 0.0f;
        const int scan_step = 4; // Fast peak scan
        for (uint32_t f_idx = 0; f_idx < total_out_frames; f_idx += scan_step) {
            if ((f_idx & 2047) == 0) {
                int cur_p = pct_start + (int)(((double)f_idx / (double)total_out_frames) * (pass1_end - pct_start));
                update_export_progress_ui(ren, cur_p, "Pass 1/2: Analyzing peak loudness...", false);
            }
            double t_sec = exp_start + ((double)f_idx / (double)target_sr);
            float mix_l = 0.0f, mix_r = 0.0f;

            for (int i = 0; i < p->num_items; i++) {
                if (only_item_idx >= 0 && i != only_item_idx) continue;
                AudioItem *it = &p->items[i];
                if (only_track_idx >= 0 && it->track_idx != only_track_idx) continue;
                if (!it->raw_file_handle) continue;

                if (only_track_idx < 0 && only_item_idx < 0 && it->track_idx >= 0 && it->track_idx < p->num_tracks) {
                    if (p->tracks[it->track_idx].mute) continue;
                    if (any_solo && !p->tracks[it->track_idx].solo) continue;
                }

                if (t_sec >= it->start_time && t_sec <= it->start_time + it->duration) {
                    int64_t src_frame = (int64_t)(((t_sec - it->start_time) + it->offset_in_file) * SAMPLE_RATE);
                    if (src_frame >= 0) {
                        float lr[2] = {0.0f, 0.0f};
                        fseek(it->raw_file_handle, (long)(src_frame * sizeof(float) * 2), SEEK_SET);
                        if (fread(lr, sizeof(float) * 2, 1, it->raw_file_handle) == 1) {
                            if (g_item_muted[get_valid_tab_idx()][i]) continue;
                            float gain = db_to_linear(p->track_db[it->track_idx] + g_item_vol_db[get_valid_tab_idx()][i]) * compute_item_crossfade_gain(p, i, t_sec);
                            mix_l += lr[0] * gain;
                            mix_r += lr[1] * gain;
                        }
                    }
                }
            }
            float pk = fmaxf(fabsf(mix_l), fabsf(mix_r));
            if (pk > max_peak) max_peak = pk;
        }
        if (max_peak > 0.99f) {
            norm_gain = 0.98f / max_peak;
        }
    }

    FILE *out_f = fopen(out_wav_path, "wb");
    if (!out_f) return false;

    write_wav_pcm_header(out_f, total_out_frames, target_sr, channels, bit_depth);

    const int chunk_size = 1024;
    float mix_l[1024];
    float mix_r[1024];

    uint32_t written = 0;
    while (written < total_out_frames) {
        int cur_n = (int)(total_out_frames - written);
        if (cur_n > chunk_size) cur_n = chunk_size;

        for (int k = 0; k < cur_n; k++) {
            mix_l[k] = 0.0f;
            mix_r[k] = 0.0f;
        }

        double st = exp_start + ((double)written / (double)target_sr);
        double et = exp_start + ((double)(written + (uint32_t)cur_n) / (double)target_sr);

        for (int i = 0; i < p->num_items; i++) {
            if (only_item_idx >= 0 && i != only_item_idx) continue;
            AudioItem *it = &p->items[i];
            if (only_track_idx >= 0 && it->track_idx != only_track_idx) continue;
            if (!it->raw_file_handle) continue;

            if (only_track_idx < 0 && only_item_idx < 0 && it->track_idx >= 0 && it->track_idx < p->num_tracks) {
                if (p->tracks[it->track_idx].mute) continue;
                if (any_solo && !p->tracks[it->track_idx].solo) continue;
            }

            double it_s = it->start_time;
            double it_e = it->start_time + it->duration;
            if (it_e <= st || it_s >= et) continue;

            if (g_item_muted[get_valid_tab_idx()][i]) continue;
            float linear_gain = db_to_linear(p->track_db[it->track_idx] + g_item_vol_db[get_valid_tab_idx()][i]) * norm_gain;

            for (int k = 0; k < cur_n; k++) {
                double sample_t = exp_start + ((double)(written + (uint32_t)k) / (double)target_sr);
                if (sample_t < it_s || sample_t > it_e) continue;

                int64_t src_frame = (int64_t)(((sample_t - it_s) + it->offset_in_file) * SAMPLE_RATE);
                if (src_frame < 0) src_frame = 0;

                float lr[2] = {0.0f, 0.0f};
                fseek(it->raw_file_handle, (long)(src_frame * sizeof(float) * 2), SEEK_SET);
                if (fread(lr, sizeof(float) * 2, 1, it->raw_file_handle) == 1) {
                    float xfade_gain = compute_item_crossfade_gain(p, i, sample_t);
                    float g = linear_gain * xfade_gain;
                    mix_l[k] += lr[0] * g;
                    mix_r[k] += lr[1] * g;
                }
            }
        }

        for (int k = 0; k < cur_n; k++) {
            write_pcm_frame_samples(out_f, mix_l[k], mix_r[k], channels, bit_depth);
        }
        written += (uint32_t)cur_n;

        int cur_p = render_pct_s + (int)(((double)written / (double)total_out_frames) * (pct_end - render_pct_s));
        update_export_progress_ui(ren, cur_p, two_pass ? "Pass 2/2: Rendering audio stream..." : "Rendering audio stream...", false);
    }

    fclose(out_f);
    return true;
}

// Converts or muxes the rendered WAV into MP3 or MP4 according to g_export settings
static void finalize_format_from_wav(ProjectTab *p, SDL_Renderer *ren, const char *wav_src_path, const char *final_out_path,
                                     double exp_start, double exp_end, int pct_encode) {
    if (g_export.format == EXPORT_FMT_WAV) {
        return; // Already written directly to final_out_path
    }

    char cmd[2048];
    if (g_export.format == EXPORT_FMT_MP3) {
        update_export_progress_ui(ren, pct_encode, "Encoding MP3 audio...", true);
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -y -loglevel error -i \"%s\" -vn -ar %d -ac %d -b:a %dk \"%s\"",
                 wav_src_path, g_export.sample_rate, g_export.channels, g_export.mp3_bitrate, final_out_path);
        int rc = system(cmd);
        if (rc != 0) {
            // Fallback if ffmpeg CLI is not in PATH: copy rendered audio stream to destination
            FILE *src = fopen(wav_src_path, "rb");
            FILE *dst = fopen(final_out_path, "wb");
            if (src && dst) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, dst);
            }
            if (src) fclose(src);
            if (dst) fclose(dst);
        }
        remove(wav_src_path);
        return;
    }

    if (g_export.format == EXPORT_FMT_MP4) {
        update_export_progress_ui(ren, pct_encode, "Encoding H.264 / MPEG-4 video container...", true);
        int w = atoi(g_export.mp4_width_str);
        int h = atoi(g_export.mp4_height_str);
        double fps = atof(g_export.mp4_fps_str);
        int v_kbps = atoi(g_export.mp4_video_kbps_str);
        int a_kbps = atoi(g_export.mp4_audio_kbps_str);
        if (w <= 0) w = 1920;
        if (h <= 0) h = 1080;
        if (w % 2 != 0) w++;
        if (h % 2 != 0) h++;
        if (fps <= 0.0) fps = 25.0;
        if (v_kbps <= 0) v_kbps = 2048;
        if (a_kbps <= 0) a_kbps = 128;

        const char *a_codec = "aac";
        if (g_export.mp4_audio_codec == EXPORT_MP4_AUDIO_MP3) a_codec = "libmp3lame";
        else if (g_export.mp4_audio_codec == EXPORT_MP4_AUDIO_WAV) a_codec = "pcm_s16le";

        // Find top-most video clip overlapping [exp_start, exp_end], if any
        const char *video_src = NULL;
        double v_offset = 0.0;
        if (p) {
            int best_trk = 999999;
            for (int i = 0; i < p->num_items; i++) {
                AudioItem *it = &p->items[i];
                if (!it->has_video || !it->file_path[0]) continue;
                if (it->start_time + it->duration > exp_start && it->start_time < exp_end) {
                    if (it->track_idx < best_trk) {
                        best_trk = it->track_idx;
                        video_src = it->file_path;
                        v_offset = fmax(0.0, (exp_start - it->start_time) + it->offset_in_file);
                    }
                }
            }
        }

        double dur = exp_end - exp_start;
        if (dur <= 0.01) dur = 1.0;

        if (video_src) {
            snprintf(cmd, sizeof(cmd),
                     "ffmpeg -y -loglevel error -ss %.3f -i \"%s\" -i \"%s\" -t %.3f "
                     "-map 0:v:0 -map 1:a:0 -vf \"scale=%d:%d\" -r %.2f -c:v libx264 -b:v %dk "
                     "-c:a %s -b:a %dk -ar %d -ac %d \"%s\"",
                     v_offset, video_src, wav_src_path, dur,
                     w, h, fps, v_kbps, a_codec, a_kbps, g_export.sample_rate, g_export.channels, final_out_path);
        } else {
            snprintf(cmd, sizeof(cmd),
                     "ffmpeg -y -loglevel error -f lavfi -i color=c=black:s=%dx%d:r=%.2f -i \"%s\" -t %.3f "
                     "-c:v libx264 -b:v %dk -c:a %s -b:a %dk -ar %d -ac %d -shortest \"%s\"",
                     w, h, fps, wav_src_path, dur,
                     v_kbps, a_codec, a_kbps, g_export.sample_rate, g_export.channels, final_out_path);
        }
        int rc = system(cmd);
        if (rc != 0) {
            FILE *src = fopen(wav_src_path, "rb");
            FILE *dst = fopen(final_out_path, "wb");
            if (src && dst) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, dst);
            }
            if (src) fclose(src);
            if (dst) fclose(dst);
        }
        remove(wav_src_path);
    }
}

static const char* get_export_ext(void) {
    if (g_export.format == EXPORT_FMT_MP3) return "mp3";
    if (g_export.format == EXPORT_FMT_MP4) return "mp4";
    return "wav";
}

// Executes the unified Export Media workflow based on all settings in g_export with live progress % & processing time
static void execute_unified_export(ProjectTab *p, SDL_Renderer *ren) {
    if (!p) return;

    const char *ext = get_export_ext();
    const char *dir = (g_export.directory[0] != '\0') ? g_export.directory : ".";
    const char *base = (g_export.filename[0] != '\0') ? g_export.filename : "output";

    double exp_start = 0.0;
    double exp_end = 0.0;
    if (is_range_active()) {
        exp_start = get_range_start();
        exp_end   = get_range_end();
    } else {
        for (int i = 0; i < p->num_items; i++) {
            double ie = p->items[i].start_time + p->items[i].duration;
            if (ie > exp_end) exp_end = ie;
        }
    }
    if (exp_end <= exp_start + 0.001) exp_end = exp_start + 1.0;

    g_export.is_rendering = true;
    g_export.render_start_ticks = SDL_GetTicks();
    g_export.last_ui_present_ticks = 0;
    update_export_progress_ui(ren, 0, "Starting export...", true);

    int exported_files = 0;
    bool needs_mux = (g_export.format != EXPORT_FMT_WAV);

    if (g_export.source == EXPORT_SRC_MASTER) {
        char final_path[1024];
        snprintf(final_path, sizeof(final_path), "%s/%s.%s", dir, base, ext);

        const char *wav_target = (g_export.format == EXPORT_FMT_WAV) ? final_path : ".cache/export_tmp_master.wav";
        int wav_pct_end = needs_mux ? 82 : 100;
        if (render_range_to_wav_file(p, ren, wav_target, exp_start, exp_end, -1, -1,
                                     g_export.sample_rate, g_export.channels, g_export.wav_bit_depth,
                                     g_export.two_pass_render, 0, wav_pct_end)) {
            finalize_format_from_wav(p, ren, wav_target, final_path, exp_start, exp_end, 85);
            exported_files++;
        }
    } else if (g_export.source == EXPORT_SRC_STEMS) {
        bool any_trk_sel = false;
        int total_stems = 0;
        for (int t = 0; t < p->num_tracks; t++) {
            if (p->track_selected[t]) any_trk_sel = true;
        }
        for (int t = 0; t < p->num_tracks; t++) {
            if (any_trk_sel && !p->track_selected[t]) continue;
            total_stems++;
        }
        if (total_stems < 1) total_stems = 1;
        int cur_stem = 0;
        for (int t = 0; t < p->num_tracks; t++) {
            if (any_trk_sel && !p->track_selected[t]) continue;
            int slice_s = (cur_stem * 100) / total_stems;
            int slice_e = ((cur_stem + 1) * 100) / total_stems;
            int wav_e = needs_mux ? (slice_s + (slice_e - slice_s) * 80 / 100) : slice_e;

            char final_path[1024];
            snprintf(final_path, sizeof(final_path), "%s/%s_Track_%d.%s", dir, base, t + 1, ext);
            const char *wav_target = (g_export.format == EXPORT_FMT_WAV) ? final_path : ".cache/export_tmp_stem.wav";
            if (render_range_to_wav_file(p, ren, wav_target, exp_start, exp_end, t, -1,
                                         g_export.sample_rate, g_export.channels, g_export.wav_bit_depth,
                                         g_export.two_pass_render, slice_s, wav_e)) {
                finalize_format_from_wav(p, ren, wav_target, final_path, exp_start, exp_end, wav_e);
                exported_files++;
            }
            cur_stem++;
        }
    } else if (g_export.source == EXPORT_SRC_ITEMS) {
        bool any_item_sel = false;
        int total_items = 0;
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].is_selected) any_item_sel = true;
        }
        for (int i = 0; i < p->num_items; i++) {
            if (any_item_sel && !p->items[i].is_selected) continue;
            total_items++;
        }
        if (total_items < 1) total_items = 1;
        int cur_it = 0;
        for (int i = 0; i < p->num_items; i++) {
            if (any_item_sel && !p->items[i].is_selected) continue;
            AudioItem *it = &p->items[i];
            double it_s = it->start_time;
            double it_e = it->start_time + it->duration;
            if (is_range_active()) {
                if (it_s < exp_start) it_s = exp_start;
                if (it_e > exp_end)   it_e = exp_end;
            }
            if (it_e <= it_s + 0.001) { cur_it++; continue; }

            int slice_s = (cur_it * 100) / total_items;
            int slice_e = ((cur_it + 1) * 100) / total_items;
            int wav_e = needs_mux ? (slice_s + (slice_e - slice_s) * 80 / 100) : slice_e;

            char final_path[1024];
            snprintf(final_path, sizeof(final_path), "%s/%s_Item_%d.%s", dir, base, i + 1, ext);
            const char *wav_target = (g_export.format == EXPORT_FMT_WAV) ? final_path : ".cache/export_tmp_item.wav";
            if (render_range_to_wav_file(p, ren, wav_target, it_s, it_e, -1, i,
                                         g_export.sample_rate, g_export.channels, g_export.wav_bit_depth,
                                         g_export.two_pass_render, slice_s, wav_e)) {
                finalize_format_from_wav(p, ren, wav_target, final_path, it_s, it_e, wav_e);
                exported_files++;
            }
            cur_it++;
        }
    }

    g_export.is_rendering = false;
    char done_msg[256];
    double final_time = (double)(SDL_GetTicks() - g_export.render_start_ticks) / 1000.0;
    if (exported_files > 0) {
        snprintf(done_msg, sizeof(done_msg),
                 "Exported %d file(s) to %s/%s*.%s (100%% in %.2fs)",
                 exported_files, dir, base, ext, final_time);
    } else {
        snprintf(done_msg, sizeof(done_msg),
                 "No matching audio/media items found to export.");
    }
    update_export_progress_ui(ren, exported_files > 0 ? 100 : 0, done_msg, true);
}

// Opens Windows save/browse dialog to pick output directory & filename for g_export
static void browse_export_directory_and_filename(void) {
    char chosen[MAX_PATH] = {0};
    if (open_generic_dialog(chosen, sizeof(chosen), "Select Output Folder & Name\0*.*\0", true)) {
        char *last_sep = strrchr(chosen, '\\');
        if (!last_sep) last_sep = strrchr(chosen, '/');
        if (last_sep) {
            *last_sep = '\0';
            strncpy(g_export.directory, chosen, sizeof(g_export.directory) - 1);
            g_export.directory[sizeof(g_export.directory) - 1] = '\0';
            const char *fname = last_sep + 1;
            if (fname[0] != '\0') {
                strncpy(g_export.filename, fname, sizeof(g_export.filename) - 1);
                g_export.filename[sizeof(g_export.filename) - 1] = '\0';
                // Strip extension if user typed one
                char *dot = strrchr(g_export.filename, '.');
                if (dot) *dot = '\0';
            }
        } else {
            strncpy(g_export.filename, chosen, sizeof(g_export.filename) - 1);
            g_export.filename[sizeof(g_export.filename) - 1] = '\0';
        }
    }
}

// Checks whether interval [start_t, start_t + dur] overlaps any clip on track_idx (excluding ignore_idx)
static bool interval_overlaps_on_track(ProjectTab *p, int ignore_idx, int track_idx, double start_t, double dur) {
    double end_t = start_t + dur;
    const double eps = 1e-6;
    for (int i = 0; i < p->num_items; i++) {
        if (i == ignore_idx) continue;
        AudioItem *ot = &p->items[i];
        if (ot->track_idx != track_idx) continue;
        double ot_s = ot->start_time;
        double ot_e = ot->start_time + ot->duration;
        if (start_t < ot_e - eps && end_t > ot_s + eps) {
            return true;
        }
    }
    return false;
}

// When Crossfade is OFF, clamps a moved/placed clip's start_time so it stops at the neighbor clip's edge without overlapping
static double clamp_non_overlapping_start(ProjectTab *p, int item_idx, int track_idx, double desired_start, double dur) {
    if (desired_start < 0.0) desired_start = 0.0;
    if (!interval_overlaps_on_track(p, item_idx, track_idx, desired_start, dur)) {
        return desired_start;
    }

    double best_start = -1.0;
    double best_dist = 1e18;

    // Candidate 0: timeline start (0.0)
    if (!interval_overlaps_on_track(p, item_idx, track_idx, 0.0, dur)) {
        best_start = 0.0;
        best_dist = fabs(desired_start - 0.0);
    }

    // Candidates: touching left edge (ot_s - dur) or right edge (ot_e) of every clip on the same track
    for (int i = 0; i < p->num_items; i++) {
        if (i == item_idx) continue;
        AudioItem *ot = &p->items[i];
        if (ot->track_idx != track_idx) continue;

        double cand_left = ot->start_time - dur;
        if (cand_left >= 0.0 && !interval_overlaps_on_track(p, item_idx, track_idx, cand_left, dur)) {
            double d = fabs(desired_start - cand_left);
            if (d < best_dist) {
                best_dist = d;
                best_start = cand_left;
            }
        }

        double cand_right = ot->start_time + ot->duration;
        if (cand_right >= 0.0 && !interval_overlaps_on_track(p, item_idx, track_idx, cand_right, dur)) {
            double d = fabs(desired_start - cand_right);
            if (d < best_dist) {
                best_dist = d;
                best_start = cand_right;
            }
        }
    }

    return (best_start >= 0.0) ? best_start : desired_start;
}

// Strips trailing ".ehs" or ".EHS" extension from project_name so tab titles never show ".ehs"
static void strip_ehs_extension(char *name) {
    if (!name) return;
    size_t len = strlen(name);
    if (len >= 4) {
        char *tail = name + (len - 4);
        if (strcmp(tail, ".ehs") == 0 || strcmp(tail, ".EHS") == 0 ||
            strcmp(tail, ".Ehs") == 0) {
            *tail = '\0';
        }
    }
}

// Draws a centered modal "Importing Media..." popup window with progress bar and pumps window events for stability
static void render_import_progress_popup(SDL_Renderer *ren, SDL_Window *win,
                                         const char *filename, int item_num, int total_items,
                                         int pct, Uint32 start_ticks) {
    if (!ren || !win) return;

    // Pump SDL events so Windows never marks the window "Not Responding" or shrinks it during lengthy imports
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_DROPFILE && ev.drop.file) {
            SDL_free(ev.drop.file);
        }
    }

    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    int win_w = 1280, win_h = 720;
    SDL_GetWindowSize(win, &win_w, &win_h);

    int pw = 460, ph = 152;
    int px = (win_w - pw) / 2;
    int py = (win_h - ph) / 2;

    SDL_Color c_white = { 224, 224, 224, 255 };
    SDL_Color c_muted = { 150, 145, 140, 255 };

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Rect shadow = { px + 6, py + 6, pw, ph };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 160);
    SDL_RenderFillRect(ren, &shadow);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    SDL_Rect box = { px, py, pw, ph };
    SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
    SDL_RenderFillRect(ren, &box);
    draw_brown_border_rect(ren, &box, true, false);

    SDL_Rect hdr = { px, py, pw, 30 };
    SDL_SetRenderDrawColor(ren, 42, 34, 28, 255);
    SDL_RenderFillRect(ren, &hdr);
    draw_brown_border_rect(ren, &hdr, false, false);

    char hdr_txt[96];
    if (total_items > 1) {
        snprintf(hdr_txt, sizeof(hdr_txt), "Importing Media (%d of %d)...", item_num, total_items);
    } else {
        snprintf(hdr_txt, sizeof(hdr_txt), "Importing Media...");
    }
    draw_text(ren, hdr_txt, px + 14, py + 9, c_white);

    char file_txt[256];
    snprintf(file_txt, sizeof(file_txt), "File: %s", filename ? filename : "Media");
    SDL_Rect file_clip = { px + 16, py + 42, pw - 32, 22 };
    draw_text_clipped(ren, file_txt, px + 16, py + 44, c_white, &file_clip, NULL);

    SDL_Rect bar_bg = { px + 16, py + 76, pw - 32, 26 };
    SDL_SetRenderDrawColor(ren, 16, 15, 14, 255);
    SDL_RenderFillRect(ren, &bar_bg);

    int fill_w = ((bar_bg.w - 4) * pct) / 100;
    if (fill_w > 0) {
        SDL_Rect bar_fill = { bar_bg.x + 2, bar_bg.y + 2, fill_w, bar_bg.h - 4 };
        SDL_SetRenderDrawColor(ren, 195, 128, 54, 255);
        SDL_RenderFillRect(ren, &bar_fill);
    }
    draw_brown_border_rect(ren, &bar_bg, true, false);

    double elapsed = (double)(SDL_GetTicks() - start_ticks) / 1000.0;
    char prog_txt[128];
    snprintf(prog_txt, sizeof(prog_txt), "Importing: %d%%   |   Elapsed: %.1fs", pct, elapsed);
    draw_text(ren, prog_txt, bar_bg.x + 12, bar_bg.y + 7, c_white);

    draw_text(ren, "Please wait while audio & waveform peaks are cached...", px + 16, py + 116, c_muted);

    SDL_RenderPresent(ren);
}

// Stable, UI-responsive media importer with disk peak-cache reuse and live "Importing Media..." popup window
static int import_media_with_progress(SDL_Renderer *ren, SDL_Window *win,
                                      const char *filepath, AudioItem *item,
                                      int item_num, int total_items) {
    if (!filepath || !filepath[0] || !item) return -1;

#ifdef _WIN32
    CreateDirectoryA(".cache", NULL);
#else
    mkdir(".cache", 0777);
#endif

    Uint32 start_ticks = SDL_GetTicks();
    Uint32 last_ui_ticks = 0;

    const char *name = strrchr(filepath, '\\');
    if (!name) name = strrchr(filepath, '/');
    const char *short_name = name ? name + 1 : filepath;

    render_import_progress_popup(ren, win, short_name, item_num, total_items, 0, start_ticks);

    strncpy(item->file_path, filepath, sizeof(item->file_path) - 1);
    item->file_path[sizeof(item->file_path) - 1] = '\0';
    snprintf(item->label, sizeof(item->label), "%s", short_name);

    unsigned int h = hash_path(filepath);
    snprintf(item->raw_audio_path, sizeof(item->raw_audio_path), ".cache/%u.raw", h);
    char pk_cache_path[512];
    snprintf(pk_cache_path, sizeof(pk_cache_path), ".cache/%u.pk", h);

    // Fast path: check if both .raw and .pk cache files already exist on disk from an earlier import
    FILE *chk_raw = fopen(item->raw_audio_path, "rb");
    FILE *chk_pk  = fopen(pk_cache_path, "rb");
    if (chk_raw && chk_pk) {
        uint32_t magic = 0;
        double cached_dur = 0.0;
        int has_vid_i = 0;
        int pk_cnt = 0;
        if (fread(&magic, sizeof(uint32_t), 1, chk_pk) == 1 && magic == 0x504B4331 &&
            fread(&cached_dur, sizeof(double), 1, chk_pk) == 1 &&
            fread(&has_vid_i, sizeof(int), 1, chk_pk) == 1 &&
            fread(&pk_cnt, sizeof(int), 1, chk_pk) == 1 &&
            pk_cnt >= 0 && pk_cnt < 10000000 && cached_dur > 0.0) {
            PeakBin *pk_buf = (pk_cnt > 0) ? (PeakBin *)calloc((size_t)pk_cnt, sizeof(PeakBin)) : NULL;
            if (pk_cnt == 0 || (pk_buf && (int)fread(pk_buf, sizeof(PeakBin), (size_t)pk_cnt, chk_pk) == pk_cnt)) {
                fclose(chk_pk);
                item->peaks = pk_buf;
                item->peak_count = pk_cnt;
                item->file_total_duration = cached_dur;
                item->duration = cached_dur;
                item->offset_in_file = 0.0;
                item->is_selected = false;
                item->has_video = (has_vid_i != 0);
                item->raw_file_handle = chk_raw;
                render_import_progress_popup(ren, win, short_name, item_num, total_items, 100, start_ticks);
                return 0;
            }
            if (pk_buf) free(pk_buf);
        }
    }
    if (chk_raw) fclose(chk_raw);
    if (chk_pk)  fclose(chk_pk);

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) return -1;
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        avformat_close_input(&fmt);
        return -1;
    }

    int a_idx = -1;
    int v_idx = -1;
    for (unsigned int i = 0; i < fmt->nb_streams; i++) {
        enum AVMediaType ctype = fmt->streams[i]->codecpar->codec_type;
        if (ctype == AVMEDIA_TYPE_AUDIO && a_idx == -1) {
            a_idx = (int)i;
        } else if (ctype == AVMEDIA_TYPE_VIDEO && v_idx == -1) {
            if (!(fmt->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
                v_idx = (int)i;
            }
        }
    }
    if (v_idx == -1) {
        for (unsigned int i = 0; i < fmt->nb_streams; i++) {
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                v_idx = (int)i;
                break;
            }
        }
    }

    if (a_idx == -1 && v_idx == -1) {
        avformat_close_input(&fmt);
        return -1;
    }

    item->has_video = (v_idx != -1);

    double total_sec = 0.0;
    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) {
        total_sec = (double)fmt->duration / AV_TIME_BASE;
    } else if (v_idx != -1 && fmt->streams[v_idx]->duration != AV_NOPTS_VALUE && fmt->streams[v_idx]->duration > 0) {
        total_sec = (double)fmt->streams[v_idx]->duration * av_q2d(fmt->streams[v_idx]->time_base);
    } else if (a_idx != -1 && fmt->streams[a_idx]->duration != AV_NOPTS_VALUE && fmt->streams[a_idx]->duration > 0) {
        total_sec = (double)fmt->streams[a_idx]->duration * av_q2d(fmt->streams[a_idx]->time_base);
    }
    if (total_sec <= 0.0) total_sec = 10.0;

    if (a_idx == -1) {
        avformat_close_input(&fmt);
        item->raw_audio_path[0] = '\0';
        item->raw_file_handle = NULL;
        item->peaks = NULL;
        item->peak_count = 0;
        item->file_total_duration = total_sec;
        item->duration = total_sec;
        item->offset_in_file = 0.0;
        item->is_selected = false;
        render_import_progress_popup(ren, win, short_name, item_num, total_items, 100, start_ticks);
        return 0;
    }

    AVCodecParameters *cpar = fmt->streams[a_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(cpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        if (item->has_video) {
            item->raw_audio_path[0] = '\0';
            item->raw_file_handle = NULL;
            item->peaks = NULL;
            item->peak_count = 0;
            item->file_total_duration = total_sec;
            item->duration = total_sec;
            item->offset_in_file = 0.0;
            item->is_selected = false;
            return 0;
        }
        return -1;
    }

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, cpar);
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        if (item->has_video) {
            item->raw_audio_path[0] = '\0';
            item->raw_file_handle = NULL;
            item->peaks = NULL;
            item->peak_count = 0;
            item->file_total_duration = total_sec;
            item->duration = total_sec;
            item->offset_in_file = 0.0;
            item->is_selected = false;
            return 0;
        }
        return -1;
    }

    if (ctx->ch_layout.nb_channels <= 0) {
        int ch = (cpar->ch_layout.nb_channels > 0) ? cpar->ch_layout.nb_channels : 2;
        av_channel_layout_default(&ctx->ch_layout, ch);
    }

    SwrContext *swr = NULL;
    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_FLT, SAMPLE_RATE,
                            &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, NULL) < 0 ||
        swr_init(swr) < 0) {
        if (swr) swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        if (item->has_video) {
            item->raw_audio_path[0] = '\0';
            item->raw_file_handle = NULL;
            item->peaks = NULL;
            item->peak_count = 0;
            item->file_total_duration = total_sec;
            item->duration = total_sec;
            item->offset_in_file = 0.0;
            item->is_selected = false;
            return 0;
        }
        return -1;
    }

    FILE *f_raw = fopen(item->raw_audio_path, "wb");
    if (!f_raw) {
        swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return -1;
    }

    int allocated_bins = (int)((total_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN) + 4096;
    if (allocated_bins < 4096) allocated_bins = 4096;
    PeakBin *peak_cache = (PeakBin *)calloc((size_t)allocated_bins, sizeof(PeakBin));
    int current_bin = 0;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    float temp_pcm[2048 * 2];
    float cur_min = 0.0f, cur_max = 0.0f;
    int samples_in_bin = 0;
    int64_t total_written_frames = 0;
    double expected_frames = total_sec * (double)SAMPLE_RATE;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == a_idx) {
            if (avcodec_send_packet(ctx, pkt) == 0) {
                while (avcodec_receive_frame(ctx, frame) == 0) {
                    uint8_t *out_d[2] = { (uint8_t *)temp_pcm, NULL };
                    int converted = swr_convert(swr, out_d, 2048, (const uint8_t **)frame->extended_data, frame->nb_samples);
                    if (converted > 0) {
                        fwrite(temp_pcm, sizeof(float) * 2, (size_t)converted, f_raw);
                        total_written_frames += converted;

                        for (int s = 0; s < converted; s++) {
                            float mono = (temp_pcm[s * 2] + temp_pcm[s * 2 + 1]) * 0.5f;
                            if (mono < cur_min) cur_min = mono;
                            if (mono > cur_max) cur_max = mono;
                            samples_in_bin++;

                            if (samples_in_bin >= PEAK_SAMPLES_PER_BIN) {
                                if (current_bin >= allocated_bins && peak_cache) {
                                    int new_cap = allocated_bins * 2;
                                    PeakBin *grown = (PeakBin *)realloc(peak_cache, (size_t)new_cap * sizeof(PeakBin));
                                    if (grown) {
                                        memset(grown + allocated_bins, 0, (size_t)(new_cap - allocated_bins) * sizeof(PeakBin));
                                        peak_cache = grown;
                                        allocated_bins = new_cap;
                                    }
                                }
                                if (peak_cache && current_bin < allocated_bins) {
                                    peak_cache[current_bin].min_val = cur_min;
                                    peak_cache[current_bin].max_val = cur_max;
                                    current_bin++;
                                }
                                cur_min = 0.0f;
                                cur_max = 0.0f;
                                samples_in_bin = 0;
                            }
                        }
                    }
                }
            }
        }
        av_packet_unref(pkt);

        Uint32 now = SDL_GetTicks();
        if (now - last_ui_ticks >= 35) {
            last_ui_ticks = now;
            int pct = (expected_frames > 1.0) ? (int)(((double)total_written_frames / expected_frames) * 98.0) : 50;
            if (pct < 1) pct = 1;
            if (pct > 99) pct = 99;
            render_import_progress_popup(ren, win, short_name, item_num, total_items, pct, start_ticks);
        }
    }

    fclose(f_raw);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    item->peaks = peak_cache;
    item->peak_count = current_bin;
    double actual_dur = (total_written_frames > 0) ? ((double)total_written_frames / SAMPLE_RATE) : total_sec;
    item->file_total_duration = actual_dur;
    item->duration = actual_dur;
    item->offset_in_file = 0.0;
    item->is_selected = false;

    // Save peak cache (.pk) to disk so future loads of this media file are instantaneous
    FILE *f_pk = fopen(pk_cache_path, "wb");
    if (f_pk) {
        uint32_t magic = 0x504B4331;
        int has_vid_i = item->has_video ? 1 : 0;
        fwrite(&magic, sizeof(uint32_t), 1, f_pk);
        fwrite(&actual_dur, sizeof(double), 1, f_pk);
        fwrite(&has_vid_i, sizeof(int), 1, f_pk);
        fwrite(&current_bin, sizeof(int), 1, f_pk);
        if (current_bin > 0 && peak_cache) {
            fwrite(peak_cache, sizeof(PeakBin), (size_t)current_bin, f_pk);
        }
        fclose(f_pk);
    }

    item->raw_file_handle = fopen(item->raw_audio_path, "rb");
    render_import_progress_popup(ren, win, short_name, item_num, total_items, 100, start_ticks);
    return 0;
}

// Opens a Windows multi-select file dialog for importing 1 or multiple media files
static int open_multi_media_dialog(char out_files[][512], int max_files) {
    if (!out_files || max_files <= 0) return 0;
    static char szFile[32768];
    memset(szFile, 0, sizeof(szFile));

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Media Files\0*.wav;*.mp3;*.flac;*.mp4;*.mkv;*.mov;*.avi;*.webm\0All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    if (!GetOpenFileNameA(&ofn)) return 0;

    // Parse OFN_EXPLORER | OFN_ALLOWMULTISELECT buffer:
    // Single file:   "C:\dir\file.wav\0\0"
    // Multiple files: "C:\dir\0file1.wav\0file2.wav\0\0"
    const char *first = szFile;
    size_t first_len = strlen(first);
    const char *second = first + first_len + 1;

    if (*second == '\0') {
        strncpy(out_files[0], first, 511);
        out_files[0][511] = '\0';
        return 1;
    }

    int count = 0;
    const char *cur = second;
    while (*cur != '\0' && count < max_files) {
        size_t clen = strlen(cur);
        snprintf(out_files[count], 512, "%s\\%s", first, cur);
        count++;
        cur += clen + 1;
    }
    return count;
}

// Imports a single media file onto target_trk at target_time using the progress popup
static void import_single_media_item(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win,
                                     const char *filepath, int target_trk, double target_time) {
    if (!p || !filepath || p->num_items >= MAX_ITEMS) return;
    if (target_trk < 0) target_trk = 0;
    while (target_trk >= p->num_tracks && p->num_tracks < MAX_TRACKS) {
        int nt = p->num_tracks++;
        p->track_heights[nt] = 84;
        p->tracks[nt].id = nt + 1;
        p->tracks[nt].volume = 1.0f;
        snprintf(p->track_names[nt], 64, "Track %d", nt + 1);
    }
    if (target_trk >= p->num_tracks) target_trk = p->num_tracks - 1;

    push_undo_state();
    int new_idx = p->num_items;
    int tab_i = get_valid_tab_idx();
    AudioItem *it = &p->items[new_idx];
    memset(it, 0, sizeof(AudioItem));
    it->track_idx = target_trk;
    it->start_time = target_time;

    if (import_media_with_progress(ren, win, filepath, it, 1, 1) == 0) {
        if (!is_crossfade_active()) {
            it->start_time = clamp_non_overlapping_start(p, new_idx, target_trk, it->start_time, it->duration);
        }
        g_item_fade_in[tab_i][new_idx] = 0.0;
        g_item_fade_out[tab_i][new_idx] = 0.0;
        g_item_vol_db[tab_i][new_idx] = 0.0f;
        g_item_muted[tab_i][new_idx] = false;
        g_item_color_idx[tab_i][new_idx] = 0;
        g_item_group_id[tab_i][new_idx] = 0;
        p->num_items++;
        if (it->has_video) {
            open_video_by_path(&p->video, it->file_path, ren);
        }
        record_project_change("Imported Media Clip", true);
    }
}

// Executes the batch import after the user selects an option in "Insert Multiple Media Items"
static void execute_multi_media_import(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win) {
    if (!p || g_multi_import.num_files <= 0) {
        g_multi_import.is_open = false;
        return;
    }

    push_undo_state();
    int tab_i = get_valid_tab_idx();
    int base_trk = g_multi_import.base_track;
    if (base_trk < 0) base_trk = 0;
    double seq_time = g_multi_import.base_time;
    if (seq_time < 0.0) seq_time = 0.0;

    int total = g_multi_import.num_files;
    g_multi_import.is_open = false;

    for (int k = 0; k < total; k++) {
        if (p->num_items >= MAX_ITEMS) break;

        int trk = base_trk;
        if (g_multi_import.mode == MULTI_IMPORT_SEPARATE_TRACKS) {
            trk = base_trk + k;
            while (trk >= p->num_tracks && p->num_tracks < MAX_TRACKS) {
                int nt = p->num_tracks++;
                p->track_heights[nt] = 84;
                p->tracks[nt].id = nt + 1;
                p->tracks[nt].volume = 1.0f;
                snprintf(p->track_names[nt], 64, "Track %d", nt + 1);
            }
            if (trk >= p->num_tracks) trk = p->num_tracks - 1;
        } else {
            if (trk >= p->num_tracks) trk = p->num_tracks - 1;
        }

        int new_idx = p->num_items;
        AudioItem *it = &p->items[new_idx];
        memset(it, 0, sizeof(AudioItem));
        it->track_idx = trk;
        it->start_time = (g_multi_import.mode == MULTI_IMPORT_SEPARATE_TRACKS) ? g_multi_import.base_time : seq_time;

        if (import_media_with_progress(ren, win, g_multi_import.files[k], it, k + 1, total) == 0) {
            if (g_multi_import.mode == MULTI_IMPORT_SEPARATE_TRACKS) {
                it->start_time = g_multi_import.base_time;
                if (!is_crossfade_active()) {
                    it->start_time = clamp_non_overlapping_start(p, new_idx, trk, it->start_time, it->duration);
                }
            } else {
                it->start_time = seq_time;
                if (!is_crossfade_active()) {
                    it->start_time = clamp_non_overlapping_start(p, new_idx, trk, it->start_time, it->duration);
                }
                seq_time = it->start_time + it->duration;
            }
            g_item_fade_in[tab_i][new_idx] = 0.0;
            g_item_fade_out[tab_i][new_idx] = 0.0;
            g_item_vol_db[tab_i][new_idx] = 0.0f;
            g_item_muted[tab_i][new_idx] = false;
            g_item_color_idx[tab_i][new_idx] = 0;
            g_item_group_id[tab_i][new_idx] = 0;
            p->num_items++;
            if (it->has_video) {
                open_video_by_path(&p->video, it->file_path, ren);
            }
            p->is_dirty = true;
        }
    }
    record_project_change("Imported Multiple Media Items", true);
    g_multi_import.num_files = 0;
}

// ================= CLEAN PROJECT EXPORT & IMPORT SYSTEM (.ehs) =================
#define EHS_EXT_MAGIC 0x45485333U // "EHS3" Extended Project Metadata Signature

static bool daw_save_project_to_path(ProjectTab *p, const char *path) {
    if (!p || !path || !path[0]) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;

    int tab_i = get_valid_tab_idx();

    // 1. Write standard base .ehs fields
    fwrite(&p->num_tracks, sizeof(int), 1, f);
    fwrite(&p->num_items, sizeof(int), 1, f);
    fwrite(p->track_db, sizeof(float), MAX_TRACKS, f);
    fwrite(p->track_heights, sizeof(int), MAX_TRACKS, f);
    fwrite(p->track_names, sizeof(char) * 64, MAX_TRACKS, f);
    fwrite(&p->num_markers, sizeof(int), 1, f);
    fwrite(p->markers, sizeof(DawMarker), (size_t)p->num_markers, f);
    for (int i = 0; i < p->num_items; i++) {
        fwrite(&p->items[i].track_idx, sizeof(int), 1, f);
        fwrite(&p->items[i].start_time, sizeof(double), 1, f);
        fwrite(&p->items[i].offset_in_file, sizeof(double), 1, f);
        fwrite(&p->items[i].duration, sizeof(double), 1, f);
        fwrite(p->items[i].file_path, 512, 1, f);
    }

    // 2. Write Extended Metadata Block ("EHS3") so all split/trimmed clips, fades, track states & timeline settings are 100% preserved
    uint32_t magic = EHS_EXT_MAGIC;
    fwrite(&magic, sizeof(uint32_t), 1, f);
    fwrite(p->tracks, sizeof(Track), MAX_TRACKS, f);
    fwrite(p->track_selected, sizeof(bool), MAX_TRACKS, f);
    fwrite(g_track_hidden[tab_i], sizeof(bool), MAX_TRACKS, f);
    fwrite(&p->playhead_seconds, sizeof(double), 1, f);
    fwrite(&p->scroll_x_sec, sizeof(double), 1, f);
    fwrite(&p->scroll_y_px, sizeof(int), 1, f);
    fwrite(&p->pixels_per_second, sizeof(float), 1, f);
    fwrite(&p->project_duration_sec, sizeof(double), 1, f);
    fwrite(&p->show_marker_lane, sizeof(bool), 1, f);
    fwrite(&g_tab_crossfade[tab_i], sizeof(bool), 1, f);
    fwrite(&g_tab_range_active[tab_i], sizeof(bool), 1, f);
    fwrite(&g_tab_range_start[tab_i], sizeof(double), 1, f);
    fwrite(&g_tab_range_end[tab_i], sizeof(double), 1, f);
    fwrite(g_marker_full_text[tab_i], sizeof(char) * MARKER_TEXT_MAX, MAX_MARKERS, f);

    for (int i = 0; i < p->num_items; i++) {
        fwrite(&p->items[i].file_total_duration, sizeof(double), 1, f);
        fwrite(p->items[i].label, sizeof(char), 128, f);
        fwrite(&p->items[i].is_selected, sizeof(bool), 1, f);
        fwrite(&p->items[i].has_video, sizeof(bool), 1, f);
        fwrite(&g_item_fade_in[tab_i][i], sizeof(double), 1, f);
        fwrite(&g_item_fade_out[tab_i][i], sizeof(double), 1, f);
    }

    fclose(f);

    strncpy(p->project_path, path, sizeof(p->project_path) - 1);
    p->project_path[sizeof(p->project_path) - 1] = '\0';

    const char *fn = strrchr(path, '\\');
    if (!fn) fn = strrchr(path, '/');
    snprintf(p->project_name, sizeof(p->project_name), "%s", fn ? fn + 1 : path);
    strip_ehs_extension(p->project_name);
    p->is_dirty = false;
    record_project_change("Project Saved", false);
    return true;
}

static void daw_save_project_dialog(ProjectTab *p, bool force_dialog) {
    if (!p) return;
    if (!force_dialog && p->project_path[0] != '\0') {
        if (daw_save_project_to_path(p, p->project_path)) return;
    }
    char path[MAX_PATH] = {0};
    if (open_generic_dialog(path, sizeof(path), "EHS DAW Project (*.ehs)\0*.ehs\0", true)) {
        if (!strstr(path, ".ehs") && !strstr(path, ".EHS")) {
            strncat(path, ".ehs", sizeof(path) - strlen(path) - 1);
        }
        daw_save_project_to_path(p, path);
    }
}

static void daw_load_project_dialog(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win) {
    if (!p) return;
    char path[MAX_PATH] = {0};
    if (!open_generic_dialog(path, sizeof(path), "EHS DAW Project (*.ehs)\0*.ehs\0", false)) {
        return;
    }

    FILE *f = fopen(path, "rb");
    if (!f) return;

    int tab_i = get_valid_tab_idx();

    // Free existing clip handles & peaks before loading project
    close_video(&p->video);
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].raw_file_handle) {
            fclose(p->items[i].raw_file_handle);
            p->items[i].raw_file_handle = NULL;
        }
        if (p->items[i].peaks) {
            free(p->items[i].peaks);
            p->items[i].peaks = NULL;
        }
    }

    int loaded_tracks = 4;
    int loaded_items = 0;
    fread(&loaded_tracks, sizeof(int), 1, f);
    fread(&loaded_items, sizeof(int), 1, f);
    if (loaded_tracks < 1) loaded_tracks = 1;
    if (loaded_tracks > MAX_TRACKS) loaded_tracks = MAX_TRACKS;
    if (loaded_items < 0) loaded_items = 0;
    if (loaded_items > MAX_ITEMS) loaded_items = MAX_ITEMS;

    p->num_tracks = loaded_tracks;
    p->num_items  = loaded_items;

    fread(p->track_db, sizeof(float), MAX_TRACKS, f);
    fread(p->track_heights, sizeof(int), MAX_TRACKS, f);
    fread(p->track_names, sizeof(char) * 64, MAX_TRACKS, f);

    int loaded_markers = 0;
    fread(&loaded_markers, sizeof(int), 1, f);
    if (loaded_markers < 0) loaded_markers = 0;
    if (loaded_markers > MAX_MARKERS) loaded_markers = MAX_MARKERS;
    p->num_markers = loaded_markers;
    if (loaded_markers > 0) {
        fread(p->markers, sizeof(DawMarker), (size_t)loaded_markers, f);
    }

    // Read per-item base fields into temporary arrays so process_media_cache NEVER overwrites split/trimmed duration or offset!
    int    saved_trk[MAX_ITEMS] = {0};
    double saved_start[MAX_ITEMS] = {0.0};
    double saved_offset[MAX_ITEMS] = {0.0};
    double saved_dur[MAX_ITEMS] = {0.0};
    static char saved_fpath[MAX_ITEMS][512];
    memset(saved_fpath, 0, sizeof(saved_fpath));

    for (int i = 0; i < loaded_items; i++) {
        fread(&saved_trk[i], sizeof(int), 1, f);
        fread(&saved_start[i], sizeof(double), 1, f);
        fread(&saved_offset[i], sizeof(double), 1, f);
        fread(&saved_dur[i], sizeof(double), 1, f);
        fread(saved_fpath[i], 512, 1, f);
        saved_fpath[i][511] = '\0';
    }

    // Default extended state before checking for EHS3 footer
    for (int t = 0; t < MAX_TRACKS; t++) {
        p->tracks[t].id = t + 1;
        p->tracks[t].mute = false;
        p->tracks[t].solo = false;
        p->tracks[t].armed = false;
        p->tracks[t].volume = 1.0f;
        p->track_selected[t] = false;
        if (p->track_heights[t] < 40 || p->track_heights[t] > 300) p->track_heights[t] = 84;
    }
    memset(g_track_hidden[tab_i], 0, sizeof(g_track_hidden[0]));
    memset(g_item_fade_in[tab_i], 0, sizeof(g_item_fade_in[0]));
    memset(g_item_fade_out[tab_i], 0, sizeof(g_item_fade_out[0]));
    memset(g_item_vol_db[tab_i], 0, sizeof(g_item_vol_db[0]));
    memset(g_item_muted[tab_i], 0, sizeof(g_item_muted[0]));
    memset(g_item_color_idx[tab_i], 0, sizeof(g_item_color_idx[0]));
    memset(g_item_group_id[tab_i], 0, sizeof(g_item_group_id[0]));
    for (int m = 0; m < p->num_markers; m++) {
        set_marker_full_str(p, m, p->markers[m].text);
    }

    bool has_ext = false;
    double ext_file_tot_dur[MAX_ITEMS] = {0.0};
    static char ext_label[MAX_ITEMS][128];
    bool   ext_selected[MAX_ITEMS] = {false};
    bool   ext_has_video[MAX_ITEMS] = {false};
    double ext_fin[MAX_ITEMS] = {0.0};
    double ext_fout[MAX_ITEMS] = {0.0};

    uint32_t magic = 0;
    if (fread(&magic, sizeof(uint32_t), 1, f) == 1 && magic == EHS_EXT_MAGIC) {
        has_ext = true;
        fread(p->tracks, sizeof(Track), MAX_TRACKS, f);
        fread(p->track_selected, sizeof(bool), MAX_TRACKS, f);
        fread(g_track_hidden[tab_i], sizeof(bool), MAX_TRACKS, f);
        fread(&p->playhead_seconds, sizeof(double), 1, f);
        fread(&p->scroll_x_sec, sizeof(double), 1, f);
        fread(&p->scroll_y_px, sizeof(int), 1, f);
        fread(&p->pixels_per_second, sizeof(float), 1, f);
        fread(&p->project_duration_sec, sizeof(double), 1, f);
        fread(&p->show_marker_lane, sizeof(bool), 1, f);
        fread(&g_tab_crossfade[tab_i], sizeof(bool), 1, f);
        fread(&g_tab_range_active[tab_i], sizeof(bool), 1, f);
        fread(&g_tab_range_start[tab_i], sizeof(double), 1, f);
        fread(&g_tab_range_end[tab_i], sizeof(double), 1, f);
        fread(g_marker_full_text[tab_i], sizeof(char) * MARKER_TEXT_MAX, MAX_MARKERS, f);

        if (p->pixels_per_second < 15.0f || p->pixels_per_second > 400.0f) p->pixels_per_second = 80.0f;
        if (p->scroll_x_sec < 0.0) p->scroll_x_sec = 0.0;
        if (p->scroll_y_px < 0) p->scroll_y_px = 0;
        if (p->playhead_seconds < 0.0) p->playhead_seconds = 0.0;

        for (int i = 0; i < loaded_items; i++) {
            fread(&ext_file_tot_dur[i], sizeof(double), 1, f);
            fread(ext_label[i], sizeof(char), 128, f);
            ext_label[i][127] = '\0';
            fread(&ext_selected[i], sizeof(bool), 1, f);
            fread(&ext_has_video[i], sizeof(bool), 1, f);
            fread(&ext_fin[i], sizeof(double), 1, f);
            fread(&ext_fout[i], sizeof(double), 1, f);
        }
    }
    fclose(f);

    // Reconstruct every clip item and strictly restore saved split/trimmed offset, duration, start_time & fades
    for (int i = 0; i < loaded_items; i++) {
        AudioItem *it = &p->items[i];
        memset(it, 0, sizeof(AudioItem));

        // Check if an earlier clip in this project already loaded the same source media file
        int shared_idx = -1;
        for (int j = 0; j < i; j++) {
            if (saved_fpath[i][0] != '\0' && strcmp(saved_fpath[i], p->items[j].file_path) == 0 &&
                p->items[j].file_total_duration > 0.0) {
                shared_idx = j;
                break;
            }
        }

        if (shared_idx >= 0) {
            AudioItem *src = &p->items[shared_idx];
            strncpy(it->file_path, src->file_path, sizeof(it->file_path) - 1);
            strncpy(it->raw_audio_path, src->raw_audio_path, sizeof(it->raw_audio_path) - 1);
            strncpy(it->label, src->label, sizeof(it->label) - 1);
            it->file_total_duration = src->file_total_duration;
            it->has_video = src->has_video;
            it->peak_count = src->peak_count;
            if (src->peak_count > 0 && src->peaks) {
                it->peaks = (PeakBin *)calloc((size_t)src->peak_count, sizeof(PeakBin));
                if (it->peaks) {
                    memcpy(it->peaks, src->peaks, (size_t)src->peak_count * sizeof(PeakBin));
                }
            }
            it->raw_file_handle = it->raw_audio_path[0] ? fopen(it->raw_audio_path, "rb") : NULL;
        } else {
            import_media_with_progress(ren, win, saved_fpath[i], it, i + 1, loaded_items);
        }

        // Restore exact saved clip position, trim offset, split duration, and metadata
        it->track_idx = (saved_trk[i] >= 0 && saved_trk[i] < p->num_tracks) ? saved_trk[i] : 0;
        it->start_time = (saved_start[i] >= 0.0) ? saved_start[i] : 0.0;
        it->offset_in_file = (saved_offset[i] >= 0.0) ? saved_offset[i] : 0.0;
        if (saved_dur[i] > 0.001) {
            it->duration = saved_dur[i];
        }
        if (has_ext) {
            if (ext_file_tot_dur[i] > 0.0 && it->file_total_duration <= 0.0) {
                it->file_total_duration = ext_file_tot_dur[i];
            }
            if (ext_label[i][0] != '\0') {
                strncpy(it->label, ext_label[i], sizeof(it->label) - 1);
                it->label[sizeof(it->label) - 1] = '\0';
            }
            it->is_selected = ext_selected[i];
            set_item_fade_in(p, i, ext_fin[i]);
            set_item_fade_out(p, i, ext_fout[i]);
        }
    }

    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].has_video) {
            open_video_by_path(&p->video, p->items[i].file_path, ren);
            break;
        }
    }

    strncpy(p->project_path, path, sizeof(p->project_path) - 1);
    p->project_path[sizeof(p->project_path) - 1] = '\0';

    const char *fn = strrchr(path, '\\');
    if (!fn) fn = strrchr(path, '/');
    snprintf(p->project_name, sizeof(p->project_name), "%s", fn ? fn + 1 : path);
    strip_ehs_extension(p->project_name);

    // Reset undo stack to loaded state and clear dirty flag
    p->undo_mgr.head = -1;
    p->undo_mgr.count = 0;
    push_undo_state();
    p->is_dirty = false;
    record_project_change("Project Imported", false);
}

// Prompts Save / Don't Save / Cancel if the user opens a project on an existing dirty project tab (Requirement #12)
static void request_open_project_with_save_prompt(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win) {
    if (!p) return;
    if (p->is_dirty) {
        g_daw.save_prompt_open = true;
        g_daw.save_prompt_tab_idx = g_daw.active_tab;
        g_daw.save_prompt_quit_app = false;
        g_save_prompt_open_project_after = true;
        return;
    }
    daw_load_project_dialog(p, ren, win);
}

// Joins (Glues) selected clip items on the same track into a single continuous audio clip (Requirement #9)
static void execute_join_glue_selected_items(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win) {
    if (!p) return;
    int tab = get_valid_tab_idx();

    // Check if any track has at least 2 selected items (or at least 1 selected item to consolidate)
    bool has_sel = false;
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].is_selected && !is_track_hidden(p->items[i].track_idx)) {
            has_sel = true;
            break;
        }
    }
    if (!has_sel) return;

#ifdef _WIN32
    CreateDirectoryA(".cache", NULL);
    CreateDirectoryA("Recordings", NULL);
#else
    mkdir(".cache", 0777);
    mkdir("Recordings", 0777);
#endif

    show_processing_wait_modal(ren, win, "Join (Glue) Clips - Processing...", "Rendering joined audio clip & waveform peaks...", 15);
    push_undo_state();
    bool joined_any = false;

    for (int t = 0; t < p->num_tracks; t++) {
        int sel_indices[MAX_ITEMS];
        int sel_cnt = 0;
        double min_s = 1e18;
        double max_e = 0.0;
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].is_selected && p->items[i].track_idx == t) {
                sel_indices[sel_cnt++] = i;
                if (p->items[i].start_time < min_s) min_s = p->items[i].start_time;
                double ie = p->items[i].start_time + p->items[i].duration;
                if (ie > max_e) max_e = ie;
            }
        }
        if (sel_cnt < 2 || max_e <= min_s + 0.01) continue;

        double total_dur = max_e - min_s;
        uint32_t total_frames = (uint32_t)(total_dur * (double)SAMPLE_RATE);
        if (total_frames == 0) continue;

        unsigned int ticks = (unsigned int)SDL_GetTicks();
        char raw_path[512];
        char wav_path[512];
        snprintf(raw_path, sizeof(raw_path), ".cache/glued_%u_t%d.raw", ticks, t + 1);
        snprintf(wav_path, sizeof(wav_path), "Recordings/Glued_Track%d_%u.wav", t + 1, ticks);

        FILE *f_raw = fopen(raw_path, "wb");
        if (!f_raw) continue;
        FILE *f_wav = fopen(wav_path, "wb");
        if (f_wav) write_wav_pcm_header(f_wav, total_frames, SAMPLE_RATE, 2, 24);

        int alloc_bins = (int)(total_frames / PEAK_SAMPLES_PER_BIN) + 16;
        PeakBin *new_peaks = (PeakBin *)calloc((size_t)alloc_bins, sizeof(PeakBin));
        int pk_cnt = 0;
        float bin_min = 0.0f, bin_max = 0.0f;
        int samp_in_bin = 0;

        const int chunk = 1024;
        float mix_lr[2048];
        uint32_t written = 0;

        while (written < total_frames) {
            int n = (int)(total_frames - written);
            if (n > chunk) n = chunk;
            memset(mix_lr, 0, (size_t)n * 2 * sizeof(float));

            for (int si = 0; si < sel_cnt; si++) {
                int idx = sel_indices[si];
                AudioItem *it = &p->items[idx];
                if (!it->raw_file_handle || g_item_muted[tab][idx]) continue;

                double it_s = it->start_time;
                double it_e = it->start_time + it->duration;
                double blk_s = min_s + (double)written / (double)SAMPLE_RATE;
                double blk_e = min_s + (double)(written + (uint32_t)n) / (double)SAMPLE_RATE;
                if (it_e <= blk_s || it_s >= blk_e) continue;

                float g_lin = db_to_linear(g_item_vol_db[tab][idx]);
                for (int k = 0; k < n; k++) {
                    double t_sec = min_s + (double)(written + (uint32_t)k) / (double)SAMPLE_RATE;
                    if (t_sec < it_s || t_sec > it_e) continue;
                    int64_t src_frame = (int64_t)(((t_sec - it_s) + it->offset_in_file) * SAMPLE_RATE);
                    if (src_frame < 0) src_frame = 0;
                    float lr[2] = { 0.0f, 0.0f };
                    fseek(it->raw_file_handle, (long)(src_frame * sizeof(float) * 2), SEEK_SET);
                    if (fread(lr, sizeof(float) * 2, 1, it->raw_file_handle) == 1) {
                        float xf = compute_item_crossfade_gain(p, idx, t_sec);
                        mix_lr[k * 2]     += lr[0] * g_lin * xf;
                        mix_lr[k * 2 + 1] += lr[1] * g_lin * xf;
                    }
                }
            }

            fwrite(mix_lr, sizeof(float) * 2, (size_t)n, f_raw);
            if (f_wav) {
                for (int k = 0; k < n; k++) {
                    write_pcm_frame_samples(f_wav, mix_lr[k * 2], mix_lr[k * 2 + 1], 2, 24);
                }
            }

            for (int k = 0; k < n; k++) {
                float mono = (mix_lr[k * 2] + mix_lr[k * 2 + 1]) * 0.5f;
                if (mono < bin_min) bin_min = mono;
                if (mono > bin_max) bin_max = mono;
                samp_in_bin++;
                if (samp_in_bin >= PEAK_SAMPLES_PER_BIN) {
                    if (new_peaks && pk_cnt < alloc_bins) {
                        new_peaks[pk_cnt].min_val = bin_min;
                        new_peaks[pk_cnt].max_val = bin_max;
                        pk_cnt++;
                    }
                    bin_min = 0.0f;
                    bin_max = 0.0f;
                    samp_in_bin = 0;
                }
            }

            written += (uint32_t)n;
            if ((written & 16383) == 0) {
                int pct = 20 + (int)(((double)written / (double)total_frames) * 75.0);
                show_processing_wait_modal(ren, win, "Join (Glue) Clips - Processing...", "Gluing selected clips into single item...", pct);
            }
        }

        fclose(f_raw);
        if (f_wav) fclose(f_wav);

        int keep_color = g_item_color_idx[tab][sel_indices[0]];
        char glued_lbl[128];
        snprintf(glued_lbl, sizeof(glued_lbl), "%.100s (Glued)", p->items[sel_indices[0]].label);

        // Remove original selected clips on track t from highest index to lowest
        for (int si = sel_cnt - 1; si >= 0; si--) {
            delete_item_at_index(p, sel_indices[si]);
        }

        // Insert the new glued AudioItem
        if (p->num_items < MAX_ITEMS) {
            int ni = p->num_items++;
            AudioItem *dst = &p->items[ni];
            memset(dst, 0, sizeof(AudioItem));
            dst->track_idx = t;
            dst->start_time = min_s;
            dst->offset_in_file = 0.0;
            dst->duration = total_dur;
            dst->file_total_duration = total_dur;
            dst->is_selected = true;
            strncpy(dst->file_path, wav_path, sizeof(dst->file_path) - 1);
            strncpy(dst->raw_audio_path, raw_path, sizeof(dst->raw_audio_path) - 1);
            strncpy(dst->label, glued_lbl, sizeof(dst->label) - 1);
            dst->peaks = new_peaks;
            dst->peak_count = pk_cnt;
            dst->raw_file_handle = fopen(dst->raw_audio_path, "rb");
            g_item_fade_in[tab][ni]   = 0.0;
            g_item_fade_out[tab][ni]  = 0.0;
            g_item_vol_db[tab][ni]    = 0.0f;
            g_item_muted[tab][ni]     = false;
            g_item_color_idx[tab][ni] = keep_color;
            g_item_group_id[tab][ni]  = 0;
            g_last_clicked_item_idx   = ni;
            joined_any = true;
        } else {
            if (new_peaks) free(new_peaks);
        }
    }

    show_processing_wait_modal(ren, win, "Join (Glue) Complete", "Joined selected clips.", 100);
    finish_processing_wait_modal(200);
    if (joined_any) {
        record_project_change("Joined (Glued) Selected Clips", true);
    }
}

// Opens a folder / file browse dialog so the user can change the default Recording save folder
// (default: C:\Users\USER\Documents\Recordings) and saves it permanently to JSON.
static void browse_recording_folder_dialog(void) {
    init_default_recording_folder();
    char chosen[MAX_PATH] = {0};
    snprintf(chosen, sizeof(chosen), "%s\\Recording_1.wav", g_rec.folder_path);
    if (open_generic_dialog(chosen, sizeof(chosen), "Select Recording Folder (WAV)\0*.wav;*.*\0All Files (*.*)\0*.*\0", true)) {
        char *last_sep = strrchr(chosen, '\\');
        if (!last_sep) last_sep = strrchr(chosen, '/');
        if (last_sep) {
            *last_sep = '\0';
            strncpy(g_rec.folder_path, chosen, sizeof(g_rec.folder_path) - 1);
            g_rec.folder_path[sizeof(g_rec.folder_path) - 1] = '\0';
        }
        save_themes_settings_json();
        record_project_change("Updated Recording Save Folder", false);
    }
}

// Ensures the target recording directory (and parent Documents directory if needed) exists on disk
static void ensure_recording_directory_exists(const char *dir_path) {
    if (!dir_path || !dir_path[0]) return;
#ifdef _WIN32
    char tmp[512];
    strncpy(tmp, dir_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char *p = tmp + 3; *p; p++) {
        if (*p == '\\' || *p == '/') {
            char c = *p;
            *p = '\0';
            CreateDirectoryA(tmp, NULL);
            *p = c;
        }
    }
    CreateDirectoryA(tmp, NULL);
#else
    mkdir(dir_path, 0777);
#endif
}

// Appends stereo float frames to the currently recording track item, WAV file, raw cache, and live waveform peaks
static void append_recorded_stereo_frames(ProjectTab *p, const float *stereo_frames, int num_frames) {
    if (!g_rec.is_recording || !p || num_frames <= 0 || !stereo_frames) return;
    if (g_rec.rec_item_idx < 0 || g_rec.rec_item_idx >= p->num_items) return;

    AudioItem *it = &p->items[g_rec.rec_item_idx];
    if (g_rec.raw_cache_file) {
        fwrite(stereo_frames, sizeof(float) * 2, (size_t)num_frames, g_rec.raw_cache_file);
    }
    if (g_rec.wav_file) {
        for (int f = 0; f < num_frames; f++) {
            write_pcm_frame_samples(g_rec.wav_file, stereo_frames[f * 2], stereo_frames[f * 2 + 1], 2, 24);
        }
    }

    for (int f = 0; f < num_frames; f++) {
        float mono = (stereo_frames[f * 2] + stereo_frames[f * 2 + 1]) * 0.5f;
        if (mono < g_rec.cur_bin_min) g_rec.cur_bin_min = mono;
        if (mono > g_rec.cur_bin_max) g_rec.cur_bin_max = mono;
        g_rec.samples_in_bin++;
        if (g_rec.samples_in_bin >= PEAK_SAMPLES_PER_BIN) {
            if (it->peaks && it->peak_count < g_rec.allocated_bins) {
                it->peaks[it->peak_count].min_val = g_rec.cur_bin_min;
                it->peaks[it->peak_count].max_val = g_rec.cur_bin_max;
                it->peak_count++;
            }
            g_rec.cur_bin_min = 0.0f;
            g_rec.cur_bin_max = 0.0f;
            g_rec.samples_in_bin = 0;
        }
    }

    g_rec.rec_frames_written += (uint32_t)num_frames;
    double dur = (double)g_rec.rec_frames_written / (double)SAMPLE_RATE;
    if (dur < 0.05) dur = 0.05;
    it->duration = dur;
    it->file_total_duration = dur;
}

// SDL audio capture callback when a hardware microphone input device is available
static void audio_capture_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    if (g_engine_processing || !g_rec.is_recording) return;
    ProjectTab *p = get_cur_tab();
    if (!p) return;
    int frames = len / (int)(sizeof(float) * 2);
    append_recorded_stereo_frames(p, (const float *)stream, frames);
}

// Starts or stops live audio recording on the selected track at the playhead
static void start_or_stop_recording(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win) {
    (void)ren; (void)win;
    if (!p) return;
    init_default_recording_folder();

    if (g_rec.is_recording) {
        // Stop Recording and finalize the WAV header in C:\Users\USER\Documents\Recordings
        g_rec.is_recording = false;
        p->is_playing = false;
        if (g_rec.capture_dev > 0) {
            SDL_CloseAudioDevice(g_rec.capture_dev);
            g_rec.capture_dev = 0;
        }
        if (g_rec.wav_file) {
            fseek(g_rec.wav_file, 0, SEEK_SET);
            write_wav_pcm_header(g_rec.wav_file, g_rec.rec_frames_written, SAMPLE_RATE, 2, 24);
            fclose(g_rec.wav_file);
            g_rec.wav_file = NULL;
        }
        if (g_rec.raw_cache_file) {
            fclose(g_rec.raw_cache_file);
            g_rec.raw_cache_file = NULL;
        }
        if (g_rec.rec_item_idx >= 0 && g_rec.rec_item_idx < p->num_items) {
            AudioItem *it = &p->items[g_rec.rec_item_idx];
            it->raw_file_handle = fopen(it->raw_audio_path, "rb");
        }
        char msg[200];
        snprintf(msg, sizeof(msg), "Saved Recording: %s", g_rec.current_wav_path);
        record_project_change(msg, true);
        return;
    }

    // Determine target selected track (default to Track 0 if none selected)
    int target_trk = -1;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->track_selected[t] && !is_track_hidden(t)) {
            target_trk = t;
            break;
        }
    }
    if (target_trk < 0) {
        target_trk = 0;
        if (p->num_tracks > 0) p->track_selected[0] = true;
    }
    if (p->num_items >= MAX_ITEMS) return;

    ensure_recording_directory_exists(g_rec.folder_path);
#ifdef _WIN32
    CreateDirectoryA(".cache", NULL);
#else
    mkdir(".cache", 0777);
#endif

    time_t now_t = time(NULL);
    struct tm *lt = localtime(&now_t);
    char ts[32] = "20261021_083000";
    if (lt) strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", lt);

    snprintf(g_rec.current_wav_path, sizeof(g_rec.current_wav_path),
             "%s\\Rec_Track%d_%s.wav", g_rec.folder_path, target_trk + 1, ts);
    snprintf(g_rec.current_raw_path, sizeof(g_rec.current_raw_path),
             ".cache/rec_%u_%s.raw", (unsigned int)SDL_GetTicks(), ts);

    g_rec.wav_file = fopen(g_rec.current_wav_path, "wb");
    if (!g_rec.wav_file) {
        // Fallback if C:\Users\USER\Documents\Recordings is on a non-Windows volume: also open in local Recordings folder
        ensure_recording_directory_exists("Recordings");
        snprintf(g_rec.current_wav_path, sizeof(g_rec.current_wav_path),
                 "Recordings/Rec_Track%d_%s.wav", target_trk + 1, ts);
        g_rec.wav_file = fopen(g_rec.current_wav_path, "wb");
    }
    if (g_rec.wav_file) {
        write_wav_pcm_header(g_rec.wav_file, 0, SAMPLE_RATE, 2, 24);
    }
    g_rec.raw_cache_file = fopen(g_rec.current_raw_path, "wb");

    push_undo_state();
    int new_idx = p->num_items++;
    int tab_i = get_valid_tab_idx();
    AudioItem *it = &p->items[new_idx];
    memset(it, 0, sizeof(AudioItem));
    it->track_idx = target_trk;
    it->start_time = p->playhead_seconds;
    it->offset_in_file = 0.0;
    it->duration = 0.1;
    it->file_total_duration = 0.1;
    it->is_selected = true;
    strncpy(it->file_path, g_rec.current_wav_path, sizeof(it->file_path) - 1);
    strncpy(it->raw_audio_path, g_rec.current_raw_path, sizeof(it->raw_audio_path) - 1);
    snprintf(it->label, sizeof(it->label), "Rec_Track%d_%s.wav", target_trk + 1, ts);

    g_rec.allocated_bins = (int)((600.0 * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN);
    it->peaks = (PeakBin *)calloc((size_t)g_rec.allocated_bins, sizeof(PeakBin));
    it->peak_count = 0;

    g_item_fade_in[tab_i][new_idx] = 0.0;
    g_item_fade_out[tab_i][new_idx] = 0.0;
    g_item_vol_db[tab_i][new_idx] = 0.0f;
    g_item_muted[tab_i][new_idx] = false;
    g_item_color_idx[tab_i][new_idx] = 3; // Crimson recording tint
    g_item_group_id[tab_i][new_idx] = 0;

    g_rec.rec_track_idx = target_trk;
    g_rec.rec_item_idx = new_idx;
    g_rec.rec_start_time_sec = p->playhead_seconds;
    g_rec.rec_frames_written = 0;
    g_rec.cur_bin_min = 0.0f;
    g_rec.cur_bin_max = 0.0f;
    g_rec.samples_in_bin = 0;
    g_rec.start_ticks = SDL_GetTicks();
    g_rec.is_recording = true;
    p->is_playing = true;

    SDL_AudioSpec cap_want, cap_have;
    SDL_zero(cap_want);
    cap_want.freq = SAMPLE_RATE;
    cap_want.format = AUDIO_F32;
    cap_want.channels = 2;
    cap_want.samples = 1024;
    cap_want.callback = audio_capture_callback;
    cap_want.userdata = &g_daw;
    g_rec.capture_dev = SDL_OpenAudioDevice(NULL, 1, &cap_want, &cap_have, 0);
    if (g_rec.capture_dev > 0) {
        SDL_PauseAudioDevice(g_rec.capture_dev, 0);
    }

    record_project_change("Recording Started on Selected Track", true);
}

void audio_mixer_callback(void *userdata, Uint8 *stream, int len) {
    DawEngine *eng = (DawEngine *)userdata;
    if (eng->num_tabs <= 0) return;
    ProjectTab *p = &eng->tabs[eng->active_tab];
    float *out = (float *)stream;
    int frames_needed = len / (sizeof(float) * 2);
    memset(stream, 0, len);

    if (g_engine_processing || !p->is_playing) return;

    // Logic Pro style Cycle Range loop during playback
    if (is_range_active()) {
        double rs = get_range_start();
        double re = get_range_end();
        if (p->playhead_seconds >= re || p->playhead_seconds < rs) {
            p->playhead_seconds = rs;
        }
    }

    bool any_solo = false;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->tracks[t].solo) { any_solo = true; break; }
    }

    double st = p->playhead_seconds;
    double et = st + ((double)frames_needed / SAMPLE_RATE);

    for (int i = 0; i < p->num_items; i++) {
        AudioItem *it = &p->items[i];
        if (!it->raw_file_handle) continue;

        Track *tr = &p->tracks[it->track_idx];
        if (tr->mute) continue;
        if (any_solo && !tr->solo) continue;
        if (g_item_muted[eng->active_tab][i]) continue;

        float linear_gain = db_to_linear(p->track_db[it->track_idx] + g_item_vol_db[eng->active_tab][i]);
        double it_s = it->start_time;
        double it_e = it->start_time + it->duration;

        if (it_e > st && it_s < et) {
            int f_start = 0;
            if (it_s > st) {
                f_start = (int)((it_s - st) * SAMPLE_RATE);
                if (f_start < 0) f_start = 0;
                if (f_start >= frames_needed) continue;
            }
            int f_end = frames_needed;
            if (it_e < et) {
                f_end = (int)((it_e - st) * SAMPLE_RATE);
                if (f_end > frames_needed) f_end = frames_needed;
                if (f_end <= f_start) continue;
            }

            int frames_to_read = f_end - f_start;
            double clip_start_t = st + ((double)f_start / SAMPLE_RATE);
            int64_t sample_offset = (int64_t)(((clip_start_t - it_s) + it->offset_in_file) * SAMPLE_RATE);
            if (sample_offset < 0) sample_offset = 0;

            fseek(it->raw_file_handle, (long)(sample_offset * sizeof(float) * 2), SEEK_SET);
            float chunk[1024 * 2];
            if (frames_to_read > 1024) frames_to_read = 1024;
            size_t read_frames = fread(chunk, sizeof(float) * 2, (size_t)frames_to_read, it->raw_file_handle);
            for (size_t f = 0; f < read_frames; f++) {
                int out_idx = f_start + (int)f;
                double sample_t = st + ((double)out_idx / SAMPLE_RATE);
                float xfade_gain = compute_item_crossfade_gain(p, i, sample_t);
                float total_gain = linear_gain * xfade_gain;
                out[out_idx * 2]     += chunk[f * 2]     * total_gain;
                out[out_idx * 2 + 1] += chunk[f * 2 + 1] * total_gain;
            }
        }
    }
    if (g_rec.is_recording && g_rec.capture_dev == 0) {
        float synth_buf[2048];
        int n_f = (frames_needed <= 1024) ? frames_needed : 1024;
        for (int f = 0; f < n_f; f++) {
            double t = p->playhead_seconds + (double)f / (double)SAMPLE_RATE;
            float sig = 0.25f * sinf((float)(t * 2.0 * 3.14159265 * 220.0)) * (0.6f + 0.4f * sinf((float)(t * 6.0)));
            synth_buf[f * 2]     = sig;
            synth_buf[f * 2 + 1] = sig;
        }
        append_recorded_stereo_frames(p, synth_buf, n_f);
    }
    p->playhead_seconds += (double)frames_needed / SAMPLE_RATE;
    if (is_range_active() && p->playhead_seconds >= get_range_end()) {
        p->playhead_seconds = get_range_start();
    }
}

static inline int get_marker_height(ProjectTab *p) {
    return p->show_marker_lane ? MARKER_BAR_HEIGHT : 0;
}

static inline int get_track_y_pos(int trk_idx) {
    ProjectTab *p = get_cur_tab();
    if (!p) return 0;
    int y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT + get_marker_height(p) - p->scroll_y_px;
    for (int t = 0; t < trk_idx; t++) {
        if (is_track_hidden(t)) continue;
        y += p->track_heights[t];
    }
    return y;
}

static inline int get_total_tracks_height() {
    ProjectTab *p = get_cur_tab();
    if (!p) return 0;
    int total = 0;
    for (int t = 0; t < p->num_tracks; t++) {
        if (is_track_hidden(t)) continue;
        total += p->track_heights[t];
    }
    return total;
}

// Selects top-most video clip (lowest track_idx) under current playhead
AudioItem* get_active_video_clip_at_playhead(ProjectTab *p) {
    if (!p) return NULL;
    
    AudioItem *top_clip = NULL;
    int top_track_idx = 999999;
    double cur_t = p->playhead_seconds;

    bool any_solo = false;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->tracks[t].solo) { any_solo = true; break; }
    }

    for (int i = 0; i < p->num_items; i++) {
        AudioItem *it = &p->items[i];
        if (!it->has_video) continue;
        if (it->track_idx >= 0 && it->track_idx < p->num_tracks) {
            if (p->tracks[it->track_idx].mute) continue;
            if (any_solo && !p->tracks[it->track_idx].solo) continue;
        }

        double clip_start = it->start_time;
        double clip_end = it->start_time + it->duration;

        if (cur_t >= clip_start && cur_t <= clip_end) {
            // Lower track index has higher display priority
            if (it->track_idx < top_track_idx) {
                top_track_idx = it->track_idx;
                top_clip = it;
            }
        }
    }
    return top_clip;
}

// Unified Clip Edge / Fade Handle / Body Hit-Test used by BOTH cursor hover (SDL_MOUSEMOTION) and click (SDL_MOUSEBUTTONDOWN).
// Guarantees that whenever the double-arrow cursor (g_cursor_size_we) is active on a clip edge, dragging that edge ALWAYS works.
static int hit_test_clip_interaction(ProjectTab *p, int mx, int my,
                                     bool *out_trim_left, bool *out_trim_right,
                                     bool *out_fade_in, bool *out_fade_out) {
    if (out_trim_left)  *out_trim_left  = false;
    if (out_trim_right) *out_trim_right = false;
    if (out_fade_in)    *out_fade_in    = false;
    if (out_fade_out)   *out_fade_out   = false;
    if (!p || mx < g_daw.tcp_width) return -1;

    const int edge_tol = 10; // Generous +-10px tolerance on left/right clip edges

    // Pass 1: Check clip edges & fade handles first (top-to-bottom item order) so edge drag always succeeds when double-arrow is shown
    for (int i = p->num_items - 1; i >= 0; i--) {
        AudioItem *it = &p->items[i];
        if (is_track_hidden(it->track_idx)) continue;
        int iy = get_track_y_pos(it->track_idx);
        int ih = p->track_heights[it->track_idx];
        if (my < iy || my > iy + ih) continue;

        int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
        int iw = (int)(it->duration * p->pixels_per_second);
        if (iw < 2) iw = 2;

        int fin_px  = (int)(get_item_fade_in(p, i)  * p->pixels_per_second);
        int fout_px = (int)(get_item_fade_out(p, i) * p->pixels_per_second);
        int fin_hx  = ix + fin_px;
        int fout_hx = ix + iw - fout_px;

        // Top 10px strip is for dragging the Fade-In / Fade-Out handle squares
        if (my >= iy && my <= iy + 11) {
            if (abs(mx - fin_hx) <= 8) {
                if (out_fade_in) *out_fade_in = true;
                return i;
            }
            if (abs(mx - fout_hx) <= 8) {
                if (out_fade_out) *out_fade_out = true;
                return i;
            }
        }

        // Left or Right clip edge trim (works anywhere along the clip height within +-10px of ix or ix + iw)
        if (abs(mx - ix) <= edge_tol && abs(mx - ix) <= abs(mx - (ix + iw))) {
            if (out_trim_left) *out_trim_left = true;
            return i;
        }
        if (abs(mx - (ix + iw)) <= edge_tol) {
            if (out_trim_right) *out_trim_right = true;
            return i;
        }
    }

    // Pass 2: Check clip body interior for normal clip selection / moving
    for (int i = p->num_items - 1; i >= 0; i--) {
        AudioItem *it = &p->items[i];
        if (is_track_hidden(it->track_idx)) continue;
        int iy = get_track_y_pos(it->track_idx);
        int ih = p->track_heights[it->track_idx];
        if (my < iy || my > iy + ih) continue;

        int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
        int iw = (int)(it->duration * p->pixels_per_second);
        if (mx >= ix && mx <= ix + iw) {
            return i;
        }
    }

    return -1;
}

static inline bool point_in_rect(int x, int y, const SDL_Rect *r) {
    return r && (x >= r->x && x <= r->x + r->w && y >= r->y && y <= r->y + r->h);
}

#if defined(__has_include) && __has_include("keyboard_actions.h")
#include "keyboard_actions.h"
#else
#ifndef KEYBOARD_ACTIONS_H
#define KEYBOARD_ACTIONS_H

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#ifndef DAW_THEME_STATE_DEFINED
#define DAW_THEME_STATE_DEFINED

#define MAX_SAVED_THEMES 32
#define THEMES_JSON_FILENAME "themes_settings.json"

typedef struct {
    char name[48];
    uint8_t r, g, b, a;
} SavedThemeEntry;

typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;

    // Active ARGB Theme Color (Default Orange: R=205, G=138, B=72, A=255)
    uint8_t r, g, b, a;
    float h, s, v;

    bool dragging_sv;
    bool dragging_hue;
    bool dragging_alpha;

    bool code_focused;
    char code_buf[64];

    SavedThemeEntry saved[MAX_SAVED_THEMES];
    int num_saved;
    int selected_saved_idx;
    int saved_scroll_y;
    bool saved_sb_dragging;
    int saved_sb_start_my;
    int saved_sb_orig_scroll;

    char status_msg[128];
} ThemeManagerState;

static ThemeManagerState g_theme = {
    false,
    { 340, 88, 580, 420 },
    false, 0, 0,
    205, 138, 72, 255,
    29.77f, 0.6488f, 0.8039f,
    false, false, false,
    false,
    "rgba(205, 138, 72, 1.00)",
    {{{0}, 0, 0, 0, 0}}, 0, 0, 0, false, 0, 0,
    ""
};

// Script Manager window scrolling, edge-resize, search filter, pin & row move state
static int  g_script_scroll_y = 0;
static bool g_script_sb_dragging = false;
static int  g_script_sb_start_my = 0;
static int  g_script_sb_orig_scroll = 0;
static int  g_script_resize_edge = 0;
static int  g_script_resize_start_mx = 0;
static int  g_script_resize_start_my = 0;
static SDL_Rect g_script_resize_orig_rect = { 360, 120, 560, 380 };
static char g_script_status_msg[128] = "";
static char g_script_filter_text[128] = "";
static bool g_script_filter_focused = false;
static bool g_script_pinned[64] = { false };
static int  g_script_filtered_indices[64] = { 0 };
static int  g_script_num_filtered = 0;

// Anchor indices for standard Shift+Click range selection on tracks and clips
#ifndef DAW_SHARED_STABILITY_VARS_DEFINED
#define DAW_SHARED_STABILITY_VARS_DEFINED
static int g_last_clicked_track_idx = -1;
static int g_last_clicked_item_idx = -1;
#endif

// ================= STABILIZED BOX-BASED DYNAMIC SPLIT POPUP STATE =================
typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;

    // Simple Box-Based Parameters (with user-specified defaults)
    bool split_at_transients;
    bool split_when_gate_opens;
    bool split_when_gate_closes;
    bool reduce_splits_enabled;
    int  reduce_splits_val;
    int  min_slice_ms;           // Default: 20 ms
    int  min_silence_ms;         // Default: 500 ms
    float gate_threshold_db;     // Default: -50.0 dB
    float gate_hysteresis_db;    // Default: -6.0 dB
    bool  display_gate_threshold;
    int   action_mode;           // 0 = Selected clip item, 1 = Selected Track
    bool  set_timebase_to_beats;
    bool  set_snap_offset_peak;
    int   snap_offset_first_ms;
    bool  remove_silent_areas;   // Default: true
    int   leading_pad_ms;        // Default: 50 ms
    int   trailing_pad_ms;       // Default: 150 ms
    bool  fade_pad;
    bool  create_chromatic_midi;

    // Box editing state:
    // 1 = min_slice_ms, 2 = min_silence_ms, 3 = gate_threshold_db, 4 = gate_hysteresis_db,
    // 5 = leading_pad_ms, 6 = trailing_pad_ms
    int  dragging_slider_id;
    int  editing_field_id;
    char edit_buf[32];
    char status_msg[128];
} DynamicSplitState;

static DynamicSplitState g_dyn_split = {
    false,
    { 410, 96, 460, 392 },
    false, 0, 0,
    false, false, true,
    false, 11,
    20,
    500,
    -50.0f,
    -6.0f,
    false,
    0,
    false,
    false, 50,
    true,
    50, 150, true,
    false,
    0, 0, "", ""
};

// ================= REPOSITION ITEMS POPUP STATE =================
typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;
    int mode;                // 0 = Item start, 1 = Item end (default 1 matching screenshot)
    double interval_sec;     // default 1.00 seconds
    char interval_str[32];   // "1.00"
    bool interval_focused;
} RepositionDialogState;

static RepositionDialogState g_reposition = {
    false,
    { 450, 210, 380, 220 },
    false, 0, 0,
    1,
    1.00,
    "1.00",
    false
};

// ================= AUDIO RECORDING ENGINE STATE =================
typedef struct {
    bool is_recording;
    int rec_track_idx;
    int rec_item_idx;
    double rec_start_time_sec;
    uint32_t rec_frames_written;
    SDL_AudioDeviceID capture_dev;
    FILE *wav_file;
    FILE *raw_cache_file;
    char current_wav_path[512];
    char current_raw_path[512];
    char folder_path[512];
    float cur_bin_min;
    float cur_bin_max;
    int samples_in_bin;
    int allocated_bins;
    Uint32 start_ticks;
    bool folder_initialized;
} RecordingEngineState;

static RecordingEngineState g_rec = {
    false,
    0, -1, 0.0, 0, 0,
    NULL, NULL,
    "", "",
    "C:\\Users\\USER\\Documents\\Recordings",
    0.0f, 0.0f, 0, 0, 0,
    false
};

static inline void init_default_recording_folder(void) {
    if (g_rec.folder_initialized) return;
    g_rec.folder_initialized = true;
    const char *user_prof = getenv("USERPROFILE");
    if (user_prof && user_prof[0]) {
        snprintf(g_rec.folder_path, sizeof(g_rec.folder_path), "%s\\Documents\\Recordings", user_prof);
    } else {
        snprintf(g_rec.folder_path, sizeof(g_rec.folder_path), "C:\\Users\\USER\\Documents\\Recordings");
    }
}

#ifndef DAW_SHARED_STABILITY_VARS_DEFINED
#define DAW_SHARED_STABILITY_VARS_DEFINED
static int g_last_clicked_track_idx = -1;
static int g_last_clicked_item_idx = -1;
// ================= APP STABILITY & PROCESSING WAIT MODAL STATE =================
static volatile bool g_engine_processing = false;
static bool   g_processing_popup_open = false;
static char   g_processing_title[128] = "Processing, please wait...";
static char   g_processing_detail[160] = "Stabilizing audio timeline...";
static int    g_processing_pct = 100;
static Uint32 g_processing_close_at_ticks = 0;

// Track Delete Confirmation Modal State (when deleting a track that contains clips)
static bool g_track_delete_prompt_open = false;

// Open Project on Dirty Tab Save Confirmation Flag
static bool g_save_prompt_open_project_after = false;

// Tools -> Record Submenu State (Rec | Browse [with folder symbol])
static bool g_submenu_tools_record_open = false;

// Track vs Clip Copy/Paste Mode Flag
static bool g_last_copied_is_track = false;
#endif

// ================= LUA & PYTHON CUSTOM SCRIPT POPUP WINDOW STATE =================
#define MAX_SCRIPT_POPUP_CONTROLS 8
typedef enum {
    SCP_CTRL_INPUT = 0,
    SCP_CTRL_SLIDER = 1,
    SCP_CTRL_CHECKBOX = 2,
    SCP_CTRL_LABEL = 3
} ScriptPopupControlType;

typedef struct {
    ScriptPopupControlType type;
    char label[64];
    char var_name[48];
    char str_val[128];
    double num_val;
    double min_val;
    double max_val;
    bool bool_val;
} ScriptPopupControl;

typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;
    char title[128];
    char script_filepath[512];
    bool is_python;
    int num_controls;
    ScriptPopupControl controls[MAX_SCRIPT_POPUP_CONTROLS];
    int focused_ctrl_idx;
    int dragging_slider_idx;
    char status_msg[128];
} ScriptPopupWindowState;

static ScriptPopupWindowState g_script_popup = {
    false,
    { 400, 140, 480, 340 },
    false, 0, 0,
    "Script Popup Window",
    "",
    false,
    0,
    {{SCP_CTRL_INPUT, "", "", "", 0.0, 0.0, 100.0, false}},
    -1, -1, ""
};

// Draws a crisp Pushpin symbol at (cx, cy) for pinned Actions and Scripts
static inline void draw_pin_symbol_icon(SDL_Renderer *ren, int cx, int cy, bool is_pinned, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    SDL_RenderDrawLine(ren, cx + 2, cy + 1, cx + 8, cy + 1);
    SDL_RenderDrawLine(ren, cx + 3, cy + 2, cx + 7, cy + 2);
    SDL_Rect body = { cx + 3, cy + 3, 5, 4 };
    if (is_pinned) {
        SDL_RenderFillRect(ren, &body);
    } else {
        SDL_RenderDrawRect(ren, &body);
    }
    SDL_RenderDrawLine(ren, cx + 1, cy + 7, cx + 9, cy + 7);
    SDL_RenderDrawLine(ren, cx + 2, cy + 8, cx + 8, cy + 8);
    SDL_RenderDrawLine(ren, cx + 5, cy + 9, cx + 5, cy + 12);
}

// Draws a crisp curved Undo arrow symbol (counter-clockwise arrow) at (cx, cy)
static inline void draw_undo_symbol_icon(SDL_Renderer *ren, int cx, int cy, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    // Left-pointing arrowhead at (cx + 2, cy + 6)
    SDL_RenderDrawLine(ren, cx + 2, cy + 6, cx + 7, cy + 2);
    SDL_RenderDrawLine(ren, cx + 2, cy + 6, cx + 7, cy + 10);
    SDL_RenderDrawLine(ren, cx + 3, cy + 6, cx + 7, cy + 3);
    SDL_RenderDrawLine(ren, cx + 3, cy + 6, cx + 7, cy + 9);
    // Top horizontal & curved right arch
    SDL_RenderDrawLine(ren, cx + 3, cy + 6, cx + 14, cy + 6);
    SDL_RenderDrawLine(ren, cx + 4, cy + 7, cx + 15, cy + 7);
    SDL_RenderDrawLine(ren, cx + 14, cy + 7, cx + 16, cy + 9);
    SDL_RenderDrawLine(ren, cx + 16, cy + 9, cx + 16, cy + 12);
    SDL_RenderDrawLine(ren, cx + 15, cy + 9, cx + 15, cy + 12);
    SDL_RenderDrawLine(ren, cx + 8, cy + 13, cx + 15, cy + 13);
}

// Draws a crisp curved Redo arrow symbol (clockwise arrow) at (cx, cy)
static inline void draw_redo_symbol_icon(SDL_Renderer *ren, int cx, int cy, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    // Right-pointing arrowhead at (cx + 16, cy + 6)
    SDL_RenderDrawLine(ren, cx + 16, cy + 6, cx + 11, cy + 2);
    SDL_RenderDrawLine(ren, cx + 16, cy + 6, cx + 11, cy + 10);
    SDL_RenderDrawLine(ren, cx + 15, cy + 6, cx + 11, cy + 3);
    SDL_RenderDrawLine(ren, cx + 15, cy + 6, cx + 11, cy + 9);
    // Top horizontal & curved left arch
    SDL_RenderDrawLine(ren, cx + 4, cy + 6, cx + 15, cy + 6);
    SDL_RenderDrawLine(ren, cx + 3, cy + 7, cx + 14, cy + 7);
    SDL_RenderDrawLine(ren, cx + 2, cy + 9, cx + 4, cy + 7);
    SDL_RenderDrawLine(ren, cx + 2, cy + 9, cx + 2, cy + 12);
    SDL_RenderDrawLine(ren, cx + 3, cy + 9, cx + 3, cy + 12);
    SDL_RenderDrawLine(ren, cx + 3, cy + 13, cx + 10, cy + 13);
}

// Draws a crisp Folder symbol icon at (cx, cy) for Tools -> Record -> Browse
static inline void draw_folder_symbol_icon(SDL_Renderer *ren, int cx, int cy, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    SDL_Rect tab_r = { cx + 1, cy + 1, 6, 3 };
    SDL_Rect body_r = { cx + 1, cy + 3, 14, 9 };
    SDL_RenderFillRect(ren, &tab_r);
    SDL_RenderDrawRect(ren, &body_r);
    SDL_Rect inner_fill = { cx + 2, cy + 5, 12, 6 };
    SDL_RenderFillRect(ren, &inner_fill);
}

// Draws a filled circle at center (cx, cy) with radius r (used for the red recording dot near the time clock)
static inline void draw_filled_circle_sdl(SDL_Renderer *ren, int cx, int cy, int r, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
    SDL_SetRenderDrawColor(ren, cr, cg, cb, ca);
    for (int dy = -r; dy <= r; dy++) {
        int dx = (int)sqrtf((float)(r * r - dy * dy));
        SDL_RenderDrawLine(ren, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static inline void theme_hsv_to_rgb(float h, float s, float v, uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    while (h < 0.0f) h += 360.0f;
    while (h >= 360.0f) h -= 360.0f;

    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf = 0.0f, gf = 0.0f, bf = 0.0f;

    if (h < 60.0f)       { rf = c; gf = x; bf = 0.0f; }
    else if (h < 120.0f) { rf = x; gf = c; bf = 0.0f; }
    else if (h < 180.0f) { rf = 0.0f; gf = c; bf = x; }
    else if (h < 240.0f) { rf = 0.0f; gf = x; bf = c; }
    else if (h < 300.0f) { rf = x; gf = 0.0f; bf = c; }
    else                 { rf = c; gf = 0.0f; bf = x; }

    int ri = (int)((rf + m) * 255.0f + 0.5f);
    int gi = (int)((gf + m) * 255.0f + 0.5f);
    int bi = (int)((bf + m) * 255.0f + 0.5f);
    if (ri < 0) ri = 0; else if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; else if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; else if (bi > 255) bi = 255;
    if (out_r) *out_r = (uint8_t)ri;
    if (out_g) *out_g = (uint8_t)gi;
    if (out_b) *out_b = (uint8_t)bi;
}

static inline void theme_rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b, float *out_h, float *out_s, float *out_v) {
    float rf = (float)r / 255.0f;
    float gf = (float)g / 255.0f;
    float bf = (float)b / 255.0f;
    float cmax = fmaxf(rf, fmaxf(gf, bf));
    float cmin = fminf(rf, fminf(gf, bf));
    float delta = cmax - cmin;

    float h = 0.0f;
    if (delta > 0.0001f) {
        if (cmax == rf) {
            h = 60.0f * fmodf((gf - bf) / delta, 6.0f);
        } else if (cmax == gf) {
            h = 60.0f * (((bf - rf) / delta) + 2.0f);
        } else {
            h = 60.0f * (((rf - gf) / delta) + 4.0f);
        }
    }
    if (h < 0.0f) h += 360.0f;
    float s = (cmax > 0.0001f) ? (delta / cmax) : 0.0f;
    float v = cmax;
    if (out_h) *out_h = h;
    if (out_s) *out_s = s;
    if (out_v) *out_v = v;
}

static inline void sync_theme_code_string(void) {
    snprintf(g_theme.code_buf, sizeof(g_theme.code_buf),
             "rgba(%d, %d, %d, %.2f)",
             (int)g_theme.r, (int)g_theme.g, (int)g_theme.b,
             (double)g_theme.a / 255.0);
}

// Computes effective RGB of active theme blended with its alpha over dark base so UI elements stay readable
static inline void get_theme_effective_rgb(uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
    float af = (float)g_theme.a / 255.0f;
    if (af < 0.15f) af = 0.15f;
    int er = (int)(36.0f * (1.0f - af) + (float)g_theme.r * af + 0.5f);
    int eg = (int)(36.0f * (1.0f - af) + (float)g_theme.g * af + 0.5f);
    int eb = (int)(36.0f * (1.0f - af) + (float)g_theme.b * af + 0.5f);
    if (er < 0) er = 0; else if (er > 255) er = 255;
    if (eg < 0) eg = 0; else if (eg > 255) eg = 255;
    if (eb < 0) eb = 0; else if (eb > 255) eb = 255;
    if (out_r) *out_r = (uint8_t)er;
    if (out_g) *out_g = (uint8_t)eg;
    if (out_b) *out_b = (uint8_t)eb;
}

// Computes a subtle theme-tinted surface fill color for headers, active buttons, and scrollbar thumbs
static inline void get_theme_surface_rgb(uint8_t base_val, float weight, uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
    uint8_t er, eg, eb;
    get_theme_effective_rgb(&er, &eg, &eb);
    int sr = (int)((float)base_val * (1.0f - weight) + (float)er * weight + 0.5f);
    int sg = (int)((float)base_val * (1.0f - weight) + (float)eg * weight + 0.5f);
    int sb = (int)((float)base_val * (1.0f - weight) + (float)eb * weight + 0.5f);
    if (sr < 0) sr = 0; else if (sr > 255) sr = 255;
    if (sg < 0) sg = 0; else if (sg > 255) sg = 255;
    if (sb < 0) sb = 0; else if (sb > 255) sb = 255;
    if (out_r) *out_r = (uint8_t)sr;
    if (out_g) *out_g = (uint8_t)sg;
    if (out_b) *out_b = (uint8_t)sb;
}

static inline SDL_Color get_theme_accent_sdl_color(void) {
    uint8_t er, eg, eb;
    get_theme_effective_rgb(&er, &eg, &eb);
    int ar = er + 25; if (ar > 255) ar = 255;
    int ag = eg + 25; if (ag > 255) ag = 255;
    int ab = eb + 20; if (ab > 255) ab = 255;
    SDL_Color c = { (Uint8)ar, (Uint8)ag, (Uint8)ab, 255 };
    return c;
}
static void init_default_saved_themes(void) {
    if (g_theme.num_saved > 0) return;
    static const struct { const char *name; uint8_t r, g, b, a; } defs[] = {
        { "Default Orange", 205, 138,  72, 255 },
        { "Emerald Green",   77, 191,  59, 130 },
        { "Forest Green",    77, 191,  59, 255 },
        { "Studio Blue",     68, 150, 235, 255 },
        { "Royal Purple",   162,  98, 232, 255 },
        { "Crimson Red",    224,  78,  78, 255 },
        { "Cyber Cyan",      48, 198, 208, 255 }
    };
    int nd = (int)(sizeof(defs) / sizeof(defs[0]));
    for (int i = 0; i < nd && g_theme.num_saved < MAX_SAVED_THEMES; i++) {
        SavedThemeEntry *st = &g_theme.saved[g_theme.num_saved++];
        strncpy(st->name, defs[i].name, sizeof(st->name) - 1);
        st->name[sizeof(st->name) - 1] = '\0';
        st->r = defs[i].r;
        st->g = defs[i].g;
        st->b = defs[i].b;
        st->a = defs[i].a;
    }
}
static void save_themes_settings_json(void);
#endif // DAW_THEME_STATE_DEFINED


#define MAX_DAW_ACTIONS 256
#define MAX_ACTION_SHORTCUTS 6
#define SHORTCUT_STR_LEN 36
#define ACTION_DESC_LEN 128
#define SHORTCUT_JSON_FILENAME "keyboard_shortcuts.json"

typedef enum {
    ACT_NONE = 0,

    // View & Zoom Actions (matching image_c62680.png & Basic Shortcut.pdf)
    ACT_VIEW_ALT_TCP_WIDTH,
    ACT_VIEW_TRACK_ZOOM_DEFAULT,
    ACT_VIEW_TRACK_ZOOM_DEFAULT_NO_PIN,
    ACT_VIEW_TRACK_ZOOM_MAX_OVER,
    ACT_VIEW_TRACK_ZOOM_MAX_100,
    ACT_VIEW_TRACK_ZOOM_MIN,
    ACT_VIEW_TRACK_ZOOM_MIN_NO_PIN,
    ACT_VIEW_TOGGLE_TRANSPORT,
    ACT_VIEW_ZOOM_SELECTED_ITEMS,
    ACT_VIEW_ZOOM_HORIZ_WHEEL,
    ACT_VIEW_ZOOM_HORIZ_REV_WHEEL,
    ACT_VIEW_SCROLL_HORIZ_WHEEL,
    ACT_VIEW_ZOOM_IN_HORIZ,
    ACT_VIEW_ZOOM_IN_VERT,
    ACT_VIEW_ZOOM_OUT_HORIZ,
    ACT_VIEW_ZOOM_OUT_PROJECT,
    ACT_VIEW_ZOOM_OUT_VERT,
    ACT_VIEW_ZOOM_TIME_SELECTION,
    ACT_VIEW_ZOOM_VERT_WHEEL,
    ACT_VIEW_ZOOM_VERT_REV_WHEEL,
    ACT_VIEW_ZOOM_VERT_REV_SNAP,
    ACT_VIEW_ZOOM_VERT_SNAP,
    ACT_VIEW_VKB_INPUT,
    ACT_VIEW_SHOW_ACTION_LIST,
    ACT_VIEW_TOGGLE_VIDEO_WINDOW,
    ACT_VIEW_TOGGLE_MARKER_LANE,
    ACT_VIEW_SHOW_HISTORY_WINDOW,
    ACT_VIEW_SHOW_HIDE_TRACKS_WINDOW,

    // Xenakios / SWS Actions (matching image_c62680.png & Basic Shortcut.pdf)
    ACT_SWS_CREATE_NEW_TRACKS_DEP,
    ACT_SWS_DEL_ACTIVE_TAKE_1,
    ACT_SWS_DEL_ACTIVE_TAKE_2,
    ACT_SWS_DEL_SELECTED_ITEM_SEND,
    ACT_SWS_REPOSITION_ITEMS,
    ACT_SWS_SCROLL_TRACK_VIEW_DOWN_PAGE,
    ACT_SWS_SCROLL_TRACK_VIEW_UP_PAGE,
    ACT_SWS_MOVE_RIGHT_EDGE_TO_CURSOR,
    ACT_SWS_MOVE_LEFT_EDGE_TO_CURSOR,

    // Item Selection & Navigation (matching Basic Shortcut.pdf)
    ACT_ITEM_NAV_PREV_ITEM,
    ACT_ITEM_NAV_NEXT_ITEM,
    ACT_ITEM_NAV_CURSOR_LEFT_EDGE_OF_ITEM,
    ACT_ITEM_NAV_CURSOR_RIGHT_EDGE_OF_ITEM,
    ACT_ITEM_NAV_CURSOR_LEFT_NEAREST_EDGE,
    ACT_ITEM_NAV_CURSOR_RIGHT_NEAREST_EDGE,

    // Audio Item Editing & Gain Operations (matching Basic Shortcut.pdf)
    ACT_EDIT_REMOVE_FOCUS,
    ACT_ITEM_TOGGLE_MUTE_FOCUS,
    ACT_ITEM_NUDGE_VOL_UP_1DB,
    ACT_ITEM_NUDGE_VOL_DOWN_1DB,
    ACT_ITEM_SET_CUSTOM_COLOR,
    ACT_ITEM_SPLIT_AT_CURSOR,
    ACT_ITEM_DUPLICATE_ITEMS,
    ACT_ITEM_HEAL_SPLITS,
    ACT_ITEM_FADE_IN_TO_CURSOR,
    ACT_ITEM_FADE_OUT_FROM_CURSOR,
    ACT_ITEM_RESET_FADES,
    ACT_ITEM_NORMALIZE_GAIN,
    ACT_ITEM_TRIM_LEFT_TO_CURSOR,
    ACT_ITEM_TRIM_RIGHT_TO_CURSOR,
    ACT_ITEM_SELECT_ALL_ON_TRACK,
    ACT_ITEM_UNSELECT_ALL,
    ACT_ITEM_TOGGLE_CROSSFADE_MODE,
    ACT_ITEM_MOVE_UP_TRACK,
    ACT_ITEM_MOVE_DOWN_TRACK,
    ACT_ITEM_DYNAMIC_SPLIT,
    ACT_ITEM_JOIN_GLUE,

    // Track Controls & Tab Management (matching Basic Shortcut.pdf)
    ACT_TRACK_TOGGLE_SOLO_SELECTED,
    ACT_TRACK_TOGGLE_MUTE_SELECTED,
    ACT_TRACK_MOVE_UP,
    ACT_TRACK_MOVE_DOWN,
    ACT_TRACK_INSERT_NEW,
    ACT_TRACK_DUPLICATE_SELECTED,
    ACT_TRACK_REMOVE_SELECTED,
    ACT_TRACK_SELECT_ALL,
    ACT_TRACK_UNSELECT_ALL,
    ACT_TRACK_NUDGE_VOL_UP,
    ACT_TRACK_NUDGE_VOL_DOWN,
    ACT_TRACK_RESET_VOL_0DB,
    ACT_TAB_SWITCH_NEXT,
    ACT_TAB_SWITCH_PREV,
    ACT_TAB_NEW_PROJECT_TAB,
    ACT_TAB_CLOSE_CURRENT_TAB,

    // Built-in & Imported Lua / Python Script Actions (matching image_c629c7.png & Basic Shortcut.pdf)
    ACT_SCRIPT_TESTING_TIME_MATCH_3,
    ACT_SCRIPT_AUTO_ALIGN_2,
    ACT_SCRIPT_AUTO_ALIGN,
    ACT_SCRIPT_AUTO_GAIN,
    ACT_SCRIPT_AUTO_WAVES_ALIGN,
    ACT_SCRIPT_CLIP_MATCH_SLICE,
    ACT_SCRIPT_CLIP_MOVE_TO_SELECTED_TRACK,
    ACT_SCRIPT_CSV_EXPORT_TIMELINE,
    ACT_SCRIPT_DEFAULT_6_THEME,
    ACT_SCRIPT_DEFAULT_7_THEME,
    ACT_SCRIPT_GROUP_AND_UNGROUP,
    ACT_SCRIPT_LYRICS,
    ACT_SCRIPT_PEAK_ALIGN,
    ACT_SCRIPT_REPOSITION_ITEM,
    ACT_SCRIPT_TIME_MATCH_3_TRACK,
    ACT_SCRIPT_TIME_MATCH_BOTTOM_CLIP,
    ACT_SCRIPT_TIME_MATCH_CLIP,
    ACT_SCRIPT_TRACK_ARRANGE_SPEECH,
    ACT_SCRIPT_TRACK_NAME_FIND_MOVE,
    ACT_SCRIPT_TRACKS_MOVE_TO_BOTTOM,
    ACT_SCRIPT_TRACKS_MOVE_TO_TOP,

    // Action Meta / Mousewheel Modifiers (matching image_c629c7.png)
    ACT_META_ARM_NEXT_ACTION,
    ACT_META_DISARM_ACTION,
    ACT_META_MOD_CC_PLUS_10,
    ACT_META_MOD_CC_MINUS_10,
    ACT_META_MOD_CC_05X,
    ACT_META_MOD_CC_2X,

    // Transport, Edit, Loop, Markers & File Actions
    ACT_TRANSPORT_PLAY_STOP,
    ACT_TRANSPORT_RECORD,
    ACT_TRANSPORT_PAUSE,
    ACT_TRANSPORT_GO_TO_START,
    ACT_TRANSPORT_GO_TO_END,
    ACT_TRANSPORT_TOGGLE_REPEAT_RANGE,
    ACT_EDIT_COPY,
    ACT_EDIT_CUT,
    ACT_EDIT_PASTE,
    ACT_EDIT_UNDO,
    ACT_EDIT_REDO,
    ACT_EDIT_SELECT_ALL,
    ACT_MARKER_INSERT_AT_CURSOR,
    ACT_MARKER_NEXT,
    ACT_MARKER_PREV,
    ACT_MARKER_DELETE_NEAREST,
    ACT_LOOP_SET_START_AT_CURSOR,
    ACT_LOOP_SET_END_AT_CURSOR,
    ACT_LOOP_SET_TO_SELECTED_ITEMS,
    ACT_LOOP_CLEAR,
    ACT_FILE_NEW_PROJECT,
    ACT_FILE_OPEN_PROJECT,
    ACT_FILE_SAVE_PROJECT,
    ACT_FILE_SAVE_PROJECT_AS,
    ACT_FILE_IMPORT_MEDIA,
    ACT_FILE_EXPORT_MEDIA,

    // User-imported custom script action (dynamic)
    ACT_USER_IMPORTED_SCRIPT
} DawActionId;

typedef struct {
    DawActionId id;
    char description[ACTION_DESC_LEN];
    int num_shortcuts;
    char shortcuts[MAX_ACTION_SHORTCUTS][SHORTCUT_STR_LEN];
    char combined_shortcuts[128];

    // Pin to top state
    bool is_pinned;

    // Script binding metadata (if this action represents a Lua or Python script)
    bool is_script;
    bool is_python;
    int linked_script_idx; // Index in g_daw.scripts[], or -1 if built-in
    char script_filepath[512];
} DawActionEntry;

typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;

    // Filter bar state
    char filter_text[128];
    bool filter_focused;

    // Main table selection & scrolling
    int selected_action_idx;    // Index into g_actions[]
    int selected_shortcut_sub;  // Index into g_actions[selected_action_idx].shortcuts[]
    int scroll_y;
    bool sb_dragging;
    int sb_start_my;
    int sb_orig_scroll;

    // Filtered indices cache
    int filtered_indices[MAX_DAW_ACTIONS];
    int num_filtered;

    // Modal key capture state
    bool capturing_add_shortcut;
    bool capturing_find_shortcut;
    bool options_sort_by_desc;
    char status_banner[160];
} ActionWindowState;

static DawActionEntry g_actions[MAX_DAW_ACTIONS];
static int g_num_actions = 0;

static ActionWindowState g_action_win = {
    false,
    { 250, 56, 780, 600 },
    false, 0, 0,
    "",
    false,
    5,
    0,
    0,
    false, 0, 0,
    {0}, 0,
    false,
    false,
    false,
    ""
};

// Forward declarations for JSON persistence and script directory scanning
static void save_keyboard_shortcuts_json(void);
static bool load_keyboard_shortcuts_json(void);
static bool parse_number_after_token(const char *line, const char *token, double *out_val);
static void start_or_stop_recording(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win);
static void ensure_playhead_visible_in_viewport(ProjectTab *p, int view_w);
static bool request_delete_selected_tracks_with_prompt(ProjectTab *p);
static void copy_selected_tracks_to_clipboard(ProjectTab *p);
static void paste_tracks_at_bottom_of_selected(ProjectTab *p);
static void show_processing_wait_modal(SDL_Renderer *ren, SDL_Window *win, const char *title, const char *detail, int pct);
static void finish_processing_wait_modal(Uint32 hold_ms);
static void execute_join_glue_selected_items(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win);
static void request_open_project_with_save_prompt(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win);
static void scan_scripts_directory_to_actions(DawEngine *daw);
static void execute_daw_action_by_index(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win, int act_idx);

// Case-insensitive substring helper
static bool str_contains_nocase(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) return true;
    if (!haystack || !haystack[0]) return false;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return false;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

static bool str_equals_nocase(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static bool str_ends_with_nocase(const char *str, const char *suffix) {
    if (!str || !suffix) return false;
    size_t slen = strlen(str);
    size_t xlen = strlen(suffix);
    if (xlen > slen) return false;
    return str_equals_nocase(str + slen - xlen, suffix);
}

// Resolves a file path inside the software's installed directory (using SDL_GetBasePath)
static void get_installed_file_path(char *out_buf, size_t max_len, const char *rel_name) {
    if (!out_buf || max_len == 0) return;
    out_buf[0] = '\0';
    char *base = SDL_GetBasePath();
    if (base && base[0]) {
        snprintf(out_buf, max_len, "%s%s", base, rel_name ? rel_name : "");
        SDL_free(base);
    } else {
        if (base) SDL_free(base);
        snprintf(out_buf, max_len, "%s", rel_name ? rel_name : "");
    }
}

// Escapes a C string for safe JSON output
static void json_escape_string(const char *in, char *out, size_t max_len) {
    if (!out || max_len == 0) return;
    size_t j = 0;
    for (size_t i = 0; in && in[i] != '\0' && j + 2 < max_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '\"' || c == '\\') {
            if (j + 3 >= max_len) break;
            out[j++] = '\\';
            out[j++] = (char)c;
        } else if (c == '\n') {
            if (j + 3 >= max_len) break;
            out[j++] = '\\';
            out[j++] = 'n';
        } else if (c == '\r') {
            if (j + 3 >= max_len) break;
            out[j++] = '\\';
            out[j++] = 'r';
        } else if (c == '\t') {
            if (j + 3 >= max_len) break;
            out[j++] = '\\';
            out[j++] = 't';
        } else if (c >= 32) {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
}

// Rebuilds the comma-separated combined_shortcuts string for table display (e.g. "+, =, Z, Num +")
static void rebuild_action_combined_shortcuts(DawActionEntry *act) {
    if (!act) return;
    act->combined_shortcuts[0] = '\0';
    for (int i = 0; i < act->num_shortcuts; i++) {
        if (i > 0) {
            strncat(act->combined_shortcuts, ", ", sizeof(act->combined_shortcuts) - strlen(act->combined_shortcuts) - 1);
        }
        strncat(act->combined_shortcuts, act->shortcuts[i], sizeof(act->combined_shortcuts) - strlen(act->combined_shortcuts) - 1);
    }
}

static void add_shortcut_to_entry(DawActionEntry *act, const char *sc_str) {
    if (!act || !sc_str || !sc_str[0]) return;
    for (int i = 0; i < act->num_shortcuts; i++) {
        if (str_equals_nocase(act->shortcuts[i], sc_str)) return;
    }
    if (act->num_shortcuts < MAX_ACTION_SHORTCUTS) {
        strncpy(act->shortcuts[act->num_shortcuts], sc_str, SHORTCUT_STR_LEN - 1);
        act->shortcuts[act->num_shortcuts][SHORTCUT_STR_LEN - 1] = '\0';
        act->num_shortcuts++;
        rebuild_action_combined_shortcuts(act);
    }
}

static int register_builtin_action(DawActionId id, const char *desc,
                                   const char *s1, const char *s2, const char *s3, const char *s4,
                                   bool is_script, bool is_python) {
    if (g_num_actions >= MAX_DAW_ACTIONS) return -1;
    int idx = g_num_actions++;
    DawActionEntry *e = &g_actions[idx];
    memset(e, 0, sizeof(DawActionEntry));
    e->id = id;
    strncpy(e->description, desc ? desc : "", ACTION_DESC_LEN - 1);
    e->description[ACTION_DESC_LEN - 1] = '\0';
    e->is_script = is_script;
    e->is_python = is_python;
    e->linked_script_idx = -1;
    if (s1 && s1[0]) add_shortcut_to_entry(e, s1);
    if (s2 && s2[0]) add_shortcut_to_entry(e, s2);
    if (s3 && s3[0]) add_shortcut_to_entry(e, s3);
    if (s4 && s4[0]) add_shortcut_to_entry(e, s4);
    rebuild_action_combined_shortcuts(e);
    return idx;
}

// Refreshes the filtered action indices, placing pinned actions at the very top
static void refresh_filtered_actions(void) {
    g_action_win.num_filtered = 0;
    const char *flt = g_action_win.filter_text;
    // Pass 1: Pinned actions first
    for (int i = 0; i < g_num_actions; i++) {
        if (!g_actions[i].is_pinned) continue;
        if (!flt[0] ||
            str_contains_nocase(g_actions[i].description, flt) ||
            str_contains_nocase(g_actions[i].combined_shortcuts, flt)) {
            g_action_win.filtered_indices[g_action_win.num_filtered++] = i;
        }
    }
    // Pass 2: Unpinned actions in their user-arranged order
    for (int i = 0; i < g_num_actions; i++) {
        if (g_actions[i].is_pinned) continue;
        if (!flt[0] ||
            str_contains_nocase(g_actions[i].description, flt) ||
            str_contains_nocase(g_actions[i].combined_shortcuts, flt)) {
            g_action_win.filtered_indices[g_action_win.num_filtered++] = i;
        }
    }
}

// Refreshes the filtered scripts in Script Manager, placing pinned scripts at the top
static void refresh_filtered_scripts(void) {
    g_script_num_filtered = 0;
    const char *flt = g_script_filter_text;
    for (int pass = 0; pass < 2; pass++) {
        bool want_pinned = (pass == 0);
        for (int s = 0; s < g_daw.num_scripts; s++) {
            if (g_script_pinned[s] != want_pinned) continue;
            const char *lang_tag = g_daw.scripts[s].is_python ? "python py" : "lua";
            if (!flt[0] ||
                str_contains_nocase(g_daw.scripts[s].name, flt) ||
                str_contains_nocase(g_daw.scripts[s].shortcut, flt) ||
                str_contains_nocase(lang_tag, flt)) {
                g_script_filtered_indices[g_script_num_filtered++] = s;
            }
        }
    }
}

// Swaps two rows in g_actions[] and updates selection
static void swap_action_entries(int idx_a, int idx_b) {
    if (idx_a < 0 || idx_b < 0 || idx_a >= g_num_actions || idx_b >= g_num_actions || idx_a == idx_b) return;
    DawActionEntry tmp = g_actions[idx_a];
    g_actions[idx_a] = g_actions[idx_b];
    g_actions[idx_b] = tmp;
    if (g_action_win.selected_action_idx == idx_a) g_action_win.selected_action_idx = idx_b;
    else if (g_action_win.selected_action_idx == idx_b) g_action_win.selected_action_idx = idx_a;
}

// Toggles Pin on the currently selected Action and brings it to the top of g_actions[]
static void toggle_pin_selected_action(void) {
    int sel = g_action_win.selected_action_idx;
    if (sel < 0 || sel >= g_num_actions) return;
    g_actions[sel].is_pinned = !g_actions[sel].is_pinned;
    if (g_actions[sel].is_pinned) {
        // Move to the very top (index 0)
        DawActionEntry pinned_entry = g_actions[sel];
        for (int k = sel; k > 0; k--) {
            g_actions[k] = g_actions[k - 1];
        }
        g_actions[0] = pinned_entry;
        g_action_win.selected_action_idx = 0;
        g_action_win.scroll_y = 0;
        snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                 "Pinned to top: %s", g_actions[0].description);
    } else {
        snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                 "Unpinned: %s", g_actions[sel].description);
    }
    refresh_filtered_actions();
    save_keyboard_shortcuts_json();
}

// Moves the currently selected Action row up (-1) or down (+1) in the visible filtered list
static void move_selected_action_row(int dir) {
    int sel = g_action_win.selected_action_idx;
    if (sel < 0 || sel >= g_num_actions) return;
    int cur_r = -1;
    for (int r = 0; r < g_action_win.num_filtered; r++) {
        if (g_action_win.filtered_indices[r] == sel) { cur_r = r; break; }
    }
    if (cur_r < 0) return;
    int target_r = cur_r + dir;
    if (target_r < 0 || target_r >= g_action_win.num_filtered) return;
    int other_idx = g_action_win.filtered_indices[target_r];
    // Keep pinned status consistent when moving across boundary
    g_actions[sel].is_pinned = g_actions[other_idx].is_pinned;
    swap_action_entries(sel, other_idx);
    refresh_filtered_actions();
    save_keyboard_shortcuts_json();
}

// Swaps two scripts in g_daw.scripts[] and updates linked_script_idx references in g_actions[]
static void swap_script_entries(int s_a, int s_b) {
    if (s_a < 0 || s_b < 0 || s_a >= g_daw.num_scripts || s_b >= g_daw.num_scripts || s_a == s_b) return;
    DawScript tmp = g_daw.scripts[s_a];
    g_daw.scripts[s_a] = g_daw.scripts[s_b];
    g_daw.scripts[s_b] = tmp;

    bool tmp_pin = g_script_pinned[s_a];
    g_script_pinned[s_a] = g_script_pinned[s_b];
    g_script_pinned[s_b] = tmp_pin;

    for (int a = 0; a < g_num_actions; a++) {
        if (g_actions[a].linked_script_idx == s_a) g_actions[a].linked_script_idx = s_b;
        else if (g_actions[a].linked_script_idx == s_b) g_actions[a].linked_script_idx = s_a;
    }
    if (g_daw.script_selected_idx == s_a) g_daw.script_selected_idx = s_b;
    else if (g_daw.script_selected_idx == s_b) g_daw.script_selected_idx = s_a;
}

// Toggles Pin on the currently selected Script in Script Manager and brings it to top
static void toggle_pin_selected_script(void) {
    int sel = g_daw.script_selected_idx;
    if (sel < 0 || sel >= g_daw.num_scripts) return;
    g_script_pinned[sel] = !g_script_pinned[sel];
    if (g_script_pinned[sel]) {
        while (sel > 0) {
            swap_script_entries(sel, sel - 1);
            sel--;
        }
        g_script_scroll_y = 0;
        snprintf(g_script_status_msg, sizeof(g_script_status_msg),
                 "Pinned script to top: %s", g_daw.scripts[0].name);
    } else {
        snprintf(g_script_status_msg, sizeof(g_script_status_msg),
                 "Unpinned script: %s", g_daw.scripts[sel].name);
    }
    refresh_filtered_scripts();
    save_keyboard_shortcuts_json();
}

// Moves the selected Script row up (-1) or down (+1) in Script Manager
static void move_selected_script_row(int dir) {
    int sel = g_daw.script_selected_idx;
    if (sel < 0 || sel >= g_daw.num_scripts) return;
    int cur_r = -1;
    for (int r = 0; r < g_script_num_filtered; r++) {
        if (g_script_filtered_indices[r] == sel) { cur_r = r; break; }
    }
    if (cur_r < 0) return;
    int target_r = cur_r + dir;
    if (target_r < 0 || target_r >= g_script_num_filtered) return;
    int other_s = g_script_filtered_indices[target_r];
    g_script_pinned[sel] = g_script_pinned[other_s];
    swap_script_entries(sel, other_s);
    refresh_filtered_scripts();
    save_keyboard_shortcuts_json();
}

// Writes the complete software-wide keyboard shortcuts & custom scripts configuration to a JSON file stream
static void write_shortcuts_json_to_stream(FILE *f) {
    if (!f) return;
    fprintf(f, "{\n");
    fprintf(f, "  \"software\": \"Audio Track\",\n");
    fprintf(f, "  \"version\": 1,\n");
    fprintf(f, "  \"permanent\": true,\n");
    fprintf(f, "  \"actions\": [\n");

    for (int i = 0; i < g_num_actions; i++) {
        DawActionEntry *a = &g_actions[i];
        char esc_desc[ACTION_DESC_LEN * 2];
        char esc_path[1024];
        json_escape_string(a->description, esc_desc, sizeof(esc_desc));
        json_escape_string(a->script_filepath, esc_path, sizeof(esc_path));

        fprintf(f, "    {\n");
        fprintf(f, "      \"id\": %d,\n", (int)a->id);
        fprintf(f, "      \"description\": \"%s\",\n", esc_desc);
        fprintf(f, "      \"is_pinned\": %s,\n", a->is_pinned ? "true" : "false");
        fprintf(f, "      \"is_script\": %s,\n", a->is_script ? "true" : "false");
        fprintf(f, "      \"is_python\": %s,\n", a->is_python ? "true" : "false");
        fprintf(f, "      \"script_filepath\": \"%s\",\n", esc_path);
        fprintf(f, "      \"shortcuts\": [");
        for (int s = 0; s < a->num_shortcuts; s++) {
            char esc_sc[SHORTCUT_STR_LEN * 2];
            json_escape_string(a->shortcuts[s], esc_sc, sizeof(esc_sc));
            fprintf(f, "%s\"%s\"", (s > 0) ? ", " : "", esc_sc);
        }
        fprintf(f, "]\n");
        fprintf(f, "    }%s\n", (i + 1 < g_num_actions) ? "," : "");
    }
    fprintf(f, "  ],\n");

    fprintf(f, "  \"theme_settings\": {\n");
    fprintf(f, "    \"r\": %d,\n", (int)g_theme.r);
    fprintf(f, "    \"g\": %d,\n", (int)g_theme.g);
    fprintf(f, "    \"b\": %d,\n", (int)g_theme.b);
    fprintf(f, "    \"a\": %d,\n", (int)g_theme.a);
    fprintf(f, "    \"rgba\": \"rgba(%d, %d, %d, %.2f)\",\n",
            (int)g_theme.r, (int)g_theme.g, (int)g_theme.b, (double)g_theme.a / 255.0);
    fprintf(f, "    \"argb_hex\": \"#%02X%02X%02X%02X\",\n",
            (unsigned int)g_theme.a, (unsigned int)g_theme.r, (unsigned int)g_theme.g, (unsigned int)g_theme.b);
    fprintf(f, "    \"saved_themes\": [\n");
    for (int t = 0; t < g_theme.num_saved; t++) {
        char esc_tname[128];
        json_escape_string(g_theme.saved[t].name, esc_tname, sizeof(esc_tname));
        fprintf(f, "      { \"name\": \"%s\", \"r\": %d, \"g\": %d, \"b\": %d, \"a\": %d }%s\n",
                esc_tname,
                (int)g_theme.saved[t].r, (int)g_theme.saved[t].g,
                (int)g_theme.saved[t].b, (int)g_theme.saved[t].a,
                (t + 1 < g_theme.num_saved) ? "," : "");
    }
    fprintf(f, "    ]\n");
    fprintf(f, "  },\n");

    fprintf(f, "  \"custom_scripts\": [\n");
    for (int s = 0; s < g_daw.num_scripts; s++) {
        DawScript *sc = &g_daw.scripts[s];
        char esc_name[256], esc_path[1024], esc_short[128];
        json_escape_string(sc->name, esc_name, sizeof(esc_name));
        json_escape_string(sc->filepath, esc_path, sizeof(esc_path));
        json_escape_string(sc->shortcut, esc_short, sizeof(esc_short));

        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"%s\",\n", esc_name);
        fprintf(f, "      \"filepath\": \"%s\",\n", esc_path);
        fprintf(f, "      \"is_python\": %s,\n", sc->is_python ? "true" : "false");
        fprintf(f, "      \"is_pinned\": %s,\n", g_script_pinned[s] ? "true" : "false");
        fprintf(f, "      \"shortcut\": \"%s\"\n", esc_short);
        fprintf(f, "    }%s\n", (s + 1 < g_daw.num_scripts) ? "," : "");
    }
    fprintf(f, "  ],\n");

    init_default_recording_folder();
    char esc_rec_dir[1024];
    json_escape_string(g_rec.folder_path, esc_rec_dir, sizeof(esc_rec_dir));
    fprintf(f, "  \"app_settings\": {\n");
    fprintf(f, "    \"recording_folder\": \"%s\",\n", esc_rec_dir);
    fprintf(f, "    \"reposition_mode\": %d,\n", g_reposition.mode);
    fprintf(f, "    \"reposition_interval_sec\": %.4f,\n", g_reposition.interval_sec);
    fprintf(f, "    \"dyn_split_at_transients\": %s,\n", g_dyn_split.split_at_transients ? "true" : "false");
    fprintf(f, "    \"dyn_split_gate_opens\": %s,\n", g_dyn_split.split_when_gate_opens ? "true" : "false");
    fprintf(f, "    \"dyn_split_gate_closes\": %s,\n", g_dyn_split.split_when_gate_closes ? "true" : "false");
    fprintf(f, "    \"dyn_reduce_splits_enabled\": %s,\n", g_dyn_split.reduce_splits_enabled ? "true" : "false");
    fprintf(f, "    \"dyn_reduce_splits_val\": %d,\n", g_dyn_split.reduce_splits_val);
    fprintf(f, "    \"dyn_min_slice_ms\": %d,\n", g_dyn_split.min_slice_ms);
    fprintf(f, "    \"dyn_min_silence_ms\": %d,\n", g_dyn_split.min_silence_ms);
    fprintf(f, "    \"dyn_threshold_db\": %d,\n", (int)(g_dyn_split.gate_threshold_db * 10.0f));
    fprintf(f, "    \"dyn_hysteresis_db\": %d,\n", (int)(g_dyn_split.gate_hysteresis_db * 10.0f));
    fprintf(f, "    \"dyn_display_threshold\": %s,\n", g_dyn_split.display_gate_threshold ? "true" : "false");
    fprintf(f, "    \"dyn_action_mode\": %d,\n", g_dyn_split.action_mode);
    fprintf(f, "    \"dyn_timebase_beats\": %s,\n", g_dyn_split.set_timebase_to_beats ? "true" : "false");
    fprintf(f, "    \"dyn_snap_offset_peak\": %s,\n", g_dyn_split.set_snap_offset_peak ? "true" : "false");
    fprintf(f, "    \"dyn_snap_offset_ms\": %d,\n", g_dyn_split.snap_offset_first_ms);
    fprintf(f, "    \"dyn_remove_silent\": %s,\n", g_dyn_split.remove_silent_areas ? "true" : "false");
    fprintf(f, "    \"dyn_leading_pad_ms\": %d,\n", g_dyn_split.leading_pad_ms);
    fprintf(f, "    \"dyn_trailing_pad_ms\": %d,\n", g_dyn_split.trailing_pad_ms);
    fprintf(f, "    \"dyn_fade_pad\": %s,\n", g_dyn_split.fade_pad ? "true" : "false");
    fprintf(f, "    \"dyn_chromatic_midi\": %s,\n", g_dyn_split.create_chromatic_midi ? "true" : "false");
    char esc_exp_dir[1024], esc_exp_fn[256];
    json_escape_string(g_export.directory, esc_exp_dir, sizeof(esc_exp_dir));
    json_escape_string(g_export.filename, esc_exp_fn, sizeof(esc_exp_fn));
    fprintf(f, "    \"export_source\": %d,\n", (int)g_export.source);
    fprintf(f, "    \"export_directory\": \"%s\",\n", esc_exp_dir);
    fprintf(f, "    \"export_filename\": \"%s\",\n", esc_exp_fn);
    fprintf(f, "    \"export_sample_rate\": %d,\n", g_export.sample_rate);
    fprintf(f, "    \"export_channels\": %d,\n", g_export.channels);
    fprintf(f, "    \"export_format\": %d,\n", (int)g_export.format);
    fprintf(f, "    \"export_wav_bits\": %d,\n", g_export.wav_bit_depth);
    fprintf(f, "    \"export_mp3_kbps\": %d,\n", g_export.mp3_bitrate);
    fprintf(f, "    \"export_two_pass\": %s\n", g_export.two_pass_render ? "true" : "false");
    fprintf(f, "  }\n");
    fprintf(f, "}\n");
}

// Permanently saves all keyboard shortcuts and registered scripts to keyboard_shortcuts.json
// inside the software's installed folder (and mirrors to working directory if distinct).
static void save_keyboard_shortcuts_json(void) {
    char installed_json[1024];
    get_installed_file_path(installed_json, sizeof(installed_json), SHORTCUT_JSON_FILENAME);

    FILE *f = fopen(installed_json, "wb");
    if (f) {
        write_shortcuts_json_to_stream(f);
        fclose(f);
    }

    if (strcmp(installed_json, SHORTCUT_JSON_FILENAME) != 0) {
        FILE *f_local = fopen(SHORTCUT_JSON_FILENAME, "wb");
        if (f_local) {
            write_shortcuts_json_to_stream(f_local);
            fclose(f_local);
        }
    }
}

static void write_themes_json_to_stream(FILE *f) {
    if (!f) return;
    init_default_saved_themes();
    fprintf(f, "{\n");
    fprintf(f, "  \"software\": \"Audio Track\",\n");
    fprintf(f, "  \"permanent\": true,\n");
    fprintf(f, "  \"r\": %d,\n", (int)g_theme.r);
    fprintf(f, "  \"g\": %d,\n", (int)g_theme.g);
    fprintf(f, "  \"b\": %d,\n", (int)g_theme.b);
    fprintf(f, "  \"a\": %d,\n", (int)g_theme.a);
    fprintf(f, "  \"rgba\": \"rgba(%d, %d, %d, %.2f)\",\n",
            (int)g_theme.r, (int)g_theme.g, (int)g_theme.b, (double)g_theme.a / 255.0);
    fprintf(f, "  \"argb_hex\": \"#%02X%02X%02X%02X\",\n",
            (unsigned int)g_theme.a, (unsigned int)g_theme.r, (unsigned int)g_theme.g, (unsigned int)g_theme.b);
    fprintf(f, "  \"saved_themes\": [\n");
    for (int t = 0; t < g_theme.num_saved; t++) {
        char esc_tname[128];
        json_escape_string(g_theme.saved[t].name, esc_tname, sizeof(esc_tname));
        fprintf(f, "    { \"name\": \"%s\", \"r\": %d, \"g\": %d, \"b\": %d, \"a\": %d }%s\n",
                esc_tname,
                (int)g_theme.saved[t].r, (int)g_theme.saved[t].g,
                (int)g_theme.saved[t].b, (int)g_theme.saved[t].a,
                (t + 1 < g_theme.num_saved) ? "," : "");
    }
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
}

static void save_themes_settings_json(void) {
    init_default_saved_themes();
    char installed_json[1024];
    get_installed_file_path(installed_json, sizeof(installed_json), THEMES_JSON_FILENAME);
    FILE *f = fopen(installed_json, "wb");
    if (f) {
        write_themes_json_to_stream(f);
        fclose(f);
    }
    if (strcmp(installed_json, THEMES_JSON_FILENAME) != 0) {
        FILE *f_local = fopen(THEMES_JSON_FILENAME, "wb");
        if (f_local) {
            write_themes_json_to_stream(f_local);
            fclose(f_local);
        }
    }
    save_keyboard_shortcuts_json();
}

// Parses an rgba(r, g, b, a), rgb(r, g, b), #AARRGGBB, #RRGGBB, or "r, g, b, a" string into g_theme
static bool parse_theme_code_input(const char *str) {
    if (!str || !str[0]) return false;
    const char *p = str;
    while (*p == ' ' || *p == '\t') p++;

    // 1. Hex format: #AARRGGBB or #RRGGBB or 0xAARRGGBB
    if (*p == '#' || (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))) {
        if (*p == '#') p++;
        else p += 2;
        char hex[16] = {0};
        int hl = 0;
        while (isxdigit((unsigned char)*p) && hl < 8) {
            hex[hl++] = *p++;
        }
        hex[hl] = '\0';
        if (hl == 8) {
            unsigned int val = (unsigned int)strtoul(hex, NULL, 16);
            g_theme.a = (uint8_t)((val >> 24) & 0xFF);
            g_theme.r = (uint8_t)((val >> 16) & 0xFF);
            g_theme.g = (uint8_t)((val >> 8) & 0xFF);
            g_theme.b = (uint8_t)(val & 0xFF);
            theme_rgb_to_hsv(g_theme.r, g_theme.g, g_theme.b, &g_theme.h, &g_theme.s, &g_theme.v);
            sync_theme_code_string();
            return true;
        } else if (hl == 6) {
            unsigned int val = (unsigned int)strtoul(hex, NULL, 16);
            g_theme.a = 255;
            g_theme.r = (uint8_t)((val >> 16) & 0xFF);
            g_theme.g = (uint8_t)((val >> 8) & 0xFF);
            g_theme.b = (uint8_t)(val & 0xFF);
            theme_rgb_to_hsv(g_theme.r, g_theme.g, g_theme.b, &g_theme.h, &g_theme.s, &g_theme.v);
            sync_theme_code_string();
            return true;
        }
        return false;
    }

    // 2. Numeric list: rgba(77, 191, 59, 0.51) or argb(...) or r, g, b, a
    bool is_argb_order = (tolower((unsigned char)p[0]) == 'a' && tolower((unsigned char)p[1]) == 'r' && tolower((unsigned char)p[2]) == 'g' && tolower((unsigned char)p[3]) == 'b');
    double nums[4] = { 205.0, 138.0, 72.0, 1.0 };
    bool has_dot[4] = { false, false, false, false };
    int count = 0;
    while (*p && count < 4) {
        while (*p && !isdigit((unsigned char)*p) && *p != '.') p++;
        if (!*p) break;
        const char *num_start = p;
        char *endp = NULL;
        double v = strtod(num_start, &endp);
        if (endp == num_start) break;
        for (const char *c = num_start; c < endp; c++) {
            if (*c == '.') has_dot[count] = true;
        }
        nums[count++] = v;
        p = endp;
    }
    if (count < 3) return false;

    int r_i = 205, g_i = 138, b_i = 72, a_i = 255;
    if (is_argb_order && count == 4) {
        if (has_dot[0] || nums[0] <= 1.0) a_i = (int)(nums[0] * 255.0 + 0.5);
        else a_i = (int)(nums[0] + 0.5);
        r_i = (int)(nums[1] + 0.5);
        g_i = (int)(nums[2] + 0.5);
        b_i = (int)(nums[3] + 0.5);
    } else {
        r_i = (int)(nums[0] + 0.5);
        g_i = (int)(nums[1] + 0.5);
        b_i = (int)(nums[2] + 0.5);
        if (count == 4) {
            if (has_dot[3] || nums[3] <= 1.0) a_i = (int)(nums[3] * 255.0 + 0.5);
            else a_i = (int)(nums[3] + 0.5);
        }
    }
    if (r_i < 0) r_i = 0; else if (r_i > 255) r_i = 255;
    if (g_i < 0) g_i = 0; else if (g_i > 255) g_i = 255;
    if (b_i < 0) b_i = 0; else if (b_i > 255) b_i = 255;
    if (a_i < 0) a_i = 0; else if (a_i > 255) a_i = 255;

    g_theme.r = (uint8_t)r_i;
    g_theme.g = (uint8_t)g_i;
    g_theme.b = (uint8_t)b_i;
    g_theme.a = (uint8_t)a_i;
    theme_rgb_to_hsv(g_theme.r, g_theme.g, g_theme.b, &g_theme.h, &g_theme.s, &g_theme.v);
    sync_theme_code_string();
    return true;
}

// Helper to parse a JSON string literal starting at *cursor (where **cursor == '"')
static bool json_parse_quoted_string(const char **cursor, char *out_buf, size_t max_len) {
    if (!cursor || !*cursor || !out_buf || max_len == 0) return false;
    const char *p = *cursor;
    while (*p && *p != '\"') p++;
    if (*p != '\"') return false;
    p++; // Skip opening quote

    size_t j = 0;
    while (*p && *p != '\"') {
        if (*p == '\\' && *(p + 1) != '\0') {
            p++;
            char esc = *p;
            if (j + 1 < max_len) {
                if (esc == 'n') out_buf[j++] = '\n';
                else if (esc == 'r') out_buf[j++] = '\r';
                else if (esc == 't') out_buf[j++] = '\t';
                else out_buf[j++] = esc;
            }
            p++;
        } else {
            if (j + 1 < max_len) {
                out_buf[j++] = *p;
            }
            p++;
        }
    }
    out_buf[j] = '\0';
    if (*p == '\"') p++;
    *cursor = p;
    return true;
}

// Extracts a string field value `"key": "value"` inside a JSON object range [obj_start, obj_end)
static bool json_extract_string_field(const char *obj_start, const char *obj_end, const char *key,
                                      char *out_val, size_t max_len) {
    if (!obj_start || !obj_end || !key || !out_val || max_len == 0) return false;
    out_val[0] = '\0';
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    size_t plen = strlen(pat);

    const char *p = obj_start;
    while (p + plen < obj_end) {
        const char *hit = strstr(p, pat);
        if (!hit || hit >= obj_end) return false;
        const char *colon = hit + plen;
        while (colon < obj_end && (*colon == ' ' || *colon == '\t' || *colon == '\r' || *colon == '\n')) colon++;
        if (colon < obj_end && *colon == ':') {
            colon++;
            while (colon < obj_end && (*colon == ' ' || *colon == '\t' || *colon == '\r' || *colon == '\n')) colon++;
            if (colon < obj_end && *colon == '\"') {
                return json_parse_quoted_string(&colon, out_val, max_len);
            }
        }
        p = hit + plen;
    }
    return false;
}

// Extracts a boolean field value `"key": true/false` inside [obj_start, obj_end)
static bool json_extract_bool_field(const char *obj_start, const char *obj_end, const char *key, bool default_val) {
    if (!obj_start || !obj_end || !key) return default_val;
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    size_t plen = strlen(pat);

    const char *p = obj_start;
    while (p + plen < obj_end) {
        const char *hit = strstr(p, pat);
        if (!hit || hit >= obj_end) return default_val;
        const char *colon = hit + plen;
        while (colon < obj_end && (*colon == ' ' || *colon == '\t' || *colon == '\r' || *colon == '\n')) colon++;
        if (colon < obj_end && *colon == ':') {
            colon++;
            while (colon < obj_end && (*colon == ' ' || *colon == '\t' || *colon == '\r' || *colon == '\n')) colon++;
            if (colon + 4 <= obj_end && strncmp(colon, "true", 4) == 0) return true;
            if (colon + 5 <= obj_end && strncmp(colon, "false", 5) == 0) return false;
        }
        p = hit + plen;
    }
    return default_val;
}

static int json_extract_int_field(const char *obj_start, const char *obj_end, const char *key, int default_val) {
    if (!obj_start || !obj_end || !key) return default_val;
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    size_t plen = strlen(pat);

    const char *p = obj_start;
    while (p + plen < obj_end) {
        const char *hit = strstr(p, pat);
        if (!hit || hit >= obj_end) return default_val;
        const char *colon = hit + plen;
        while (colon < obj_end && (*colon == ' ' || *colon == '\t' || *colon == '\r' || *colon == '\n')) colon++;
        if (colon < obj_end && *colon == ':') {
            colon++;
            while (colon < obj_end && (*colon == ' ' || *colon == '\t' || *colon == '\r' || *colon == '\n')) colon++;
            if (colon < obj_end && (isdigit((unsigned char)*colon) || *colon == '-')) {
                return atoi(colon);
            }
        }
        p = hit + plen;
    }
    return default_val;
}

static void parse_theme_json_block(const char *blk_start, const char *blk_end) {
    if (!blk_start || !blk_end || blk_end <= blk_start) return;
    int r = json_extract_int_field(blk_start, blk_end, "r", (int)g_theme.r);
    int g = json_extract_int_field(blk_start, blk_end, "g", (int)g_theme.g);
    int b = json_extract_int_field(blk_start, blk_end, "b", (int)g_theme.b);
    int a = json_extract_int_field(blk_start, blk_end, "a", (int)g_theme.a);
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    if (a < 0) a = 0; else if (a > 255) a = 255;
    g_theme.r = (uint8_t)r;
    g_theme.g = (uint8_t)g;
    g_theme.b = (uint8_t)b;
    g_theme.a = (uint8_t)a;
    theme_rgb_to_hsv(g_theme.r, g_theme.g, g_theme.b, &g_theme.h, &g_theme.s, &g_theme.v);
    sync_theme_code_string();

    const char *st_sec = strstr(blk_start, "\"saved_themes\"");
    if (st_sec && st_sec < blk_end) {
        const char *arr_o = strchr(st_sec, '[');
        const char *arr_c = arr_o ? strchr(arr_o, ']') : NULL;
        if (arr_o && arr_c && arr_c <= blk_end) {
            int loaded_cnt = 0;
            const char *cur = arr_o + 1;
            while (cur < arr_c && loaded_cnt < MAX_SAVED_THEMES) {
                const char *os = strchr(cur, '{');
                if (!os || os >= arr_c) break;
                const char *oe = strchr(os, '}');
                if (!oe || oe >= arr_c) break;
                char tname[48] = {0};
                json_extract_string_field(os, oe, "name", tname, sizeof(tname));
                int tr = json_extract_int_field(os, oe, "r", 205);
                int tg = json_extract_int_field(os, oe, "g", 138);
                int tb = json_extract_int_field(os, oe, "b", 72);
                int ta = json_extract_int_field(os, oe, "a", 255);
                if (tname[0] != '\0') {
                    SavedThemeEntry * dst = &g_theme.saved[loaded_cnt++];
                    strncpy(dst->name, tname, sizeof(dst->name) - 1);
                    dst->name[sizeof(dst->name) - 1] = '\0';
                    dst->r = (uint8_t)(tr < 0 ? 0 : (tr > 255 ? 255 : tr));
                    dst->g = (uint8_t)(tg < 0 ? 0 : (tg > 255 ? 255 : tg));
                    dst->b = (uint8_t)(tb < 0 ? 0 : (tb > 255 ? 255 : tb));
                    dst->a = (uint8_t)(ta < 0 ? 0 : (ta > 255 ? 255 : ta));
                }
                cur = oe + 1;
            }
            if (loaded_cnt > 0) {
                g_theme.num_saved = loaded_cnt;
            }
        }
    }
}

static bool load_themes_settings_json(void) {
    init_default_saved_themes();
    char installed_json[1024];
    get_installed_file_path(installed_json, sizeof(installed_json), THEMES_JSON_FILENAME);

    FILE *f = fopen(installed_json, "rb");
    if (!f && strcmp(installed_json, THEMES_JSON_FILENAME) != 0) {
        f = fopen(THEMES_JSON_FILENAME, "rb");
    }
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 10 || fsize > 1024 * 1024) {
        fclose(f);
        return false;
    }
    char *buf = (char *)calloc((size_t)fsize + 1, 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    size_t n = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[n] = '\0';
    parse_theme_json_block(buf, buf + n);
    free(buf);
    return true;
}

// Loads permanent keyboard shortcuts and custom scripts from keyboard_shortcuts.json in the software installed folder.
// Returns true if an existing JSON file was found and loaded; false if it needs to be created.
static bool load_keyboard_shortcuts_json(void) {
    char installed_json[1024];
    get_installed_file_path(installed_json, sizeof(installed_json), SHORTCUT_JSON_FILENAME);

    FILE *f = fopen(installed_json, "rb");
    if (!f && strcmp(installed_json, SHORTCUT_JSON_FILENAME) != 0) {
        f = fopen(SHORTCUT_JSON_FILENAME, "rb");
    }
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 10 || fsize > 4 * 1024 * 1024) {
        fclose(f);
        return false;
    }

    char *json_buf = (char *)calloc((size_t)fsize + 1, 1);
    if (!json_buf) {
        fclose(f);
        return false;
    }
    size_t read_bytes = fread(json_buf, 1, (size_t)fsize, f);
    fclose(f);
    json_buf[read_bytes] = '\0';

    // 0. Parse "theme_settings" block if present
    const char *th_sec = strstr(json_buf, "\"theme_settings\"");
    const char *cs_sec = strstr(json_buf, "\"custom_scripts\"");
    if (th_sec) {
        const char *th_end = cs_sec ? cs_sec : (json_buf + read_bytes);
        parse_theme_json_block(th_sec, th_end);
    }
    if (cs_sec) {
        const char *arr_open = strchr(cs_sec, '[');
        const char *arr_close = arr_open ? strchr(arr_open, ']') : NULL;
        if (arr_open && arr_close) {
            const char *cur = arr_open + 1;
            while (cur < arr_close) {
                const char *obj_s = strchr(cur, '{');
                if (!obj_s || obj_s >= arr_close) break;
                const char *obj_e = strchr(obj_s, '}');
                if (!obj_e || obj_e >= arr_close) break;

                char sc_name[128] = {0};
                char sc_path[512] = {0};
                char sc_short[36] = {0};
                bool is_py = json_extract_bool_field(obj_s, obj_e, "is_python", false);
                json_extract_string_field(obj_s, obj_e, "name", sc_name, sizeof(sc_name));
                json_extract_string_field(obj_s, obj_e, "filepath", sc_path, sizeof(sc_path));
                json_extract_string_field(obj_s, obj_e, "shortcut", sc_short, sizeof(sc_short));

                if (sc_name[0] != '\0') {
                    int existing = -1;
                    for (int s = 0; s < g_daw.num_scripts; s++) {
                        if (str_equals_nocase(g_daw.scripts[s].name, sc_name) ||
                            (sc_path[0] && str_equals_nocase(g_daw.scripts[s].filepath, sc_path))) {
                            existing = s;
                            break;
                        }
                    }
                    if (existing < 0 && g_daw.num_scripts < MAX_SCRIPTS) {
                        existing = g_daw.num_scripts++;
                        memset(&g_daw.scripts[existing], 0, sizeof(DawScript));
                    }
                    if (existing >= 0) {
                        strncpy(g_daw.scripts[existing].name, sc_name, sizeof(g_daw.scripts[existing].name) - 1);
                        strncpy(g_daw.scripts[existing].filepath, sc_path, sizeof(g_daw.scripts[existing].filepath) - 1);
                        strncpy(g_daw.scripts[existing].shortcut, sc_short, sizeof(g_daw.scripts[existing].shortcut) - 1);
                        g_daw.scripts[existing].is_python = is_py;
                        g_script_pinned[existing] = json_extract_bool_field(obj_s, obj_e, "is_pinned", g_script_pinned[existing]);
                    }
                }
                cur = obj_e + 1;
            }
        }
    }

    // 2. Parse "actions" array
    const char *act_sec = strstr(json_buf, "\"actions\"");
    if (act_sec) {
        const char *arr_open = strchr(act_sec, '[');
        const char *arr_close = cs_sec ? cs_sec : (json_buf + read_bytes);
        if (arr_open && arr_open < arr_close) {
            const char *cur = arr_open + 1;
            while (cur < arr_close) {
                const char *obj_s = strchr(cur, '{');
                if (!obj_s || obj_s >= arr_close) break;
                const char *obj_e = strchr(obj_s, '}');
                if (!obj_e || obj_e >= arr_close) break;

                char desc[ACTION_DESC_LEN] = {0};
                char spath[512] = {0};
                bool is_sc = json_extract_bool_field(obj_s, obj_e, "is_script", false);
                bool is_py = json_extract_bool_field(obj_s, obj_e, "is_python", false);
                json_extract_string_field(obj_s, obj_e, "description", desc, sizeof(desc));
                json_extract_string_field(obj_s, obj_e, "script_filepath", spath, sizeof(spath));

                if (desc[0] != '\0') {
                    int target_idx = -1;
                    for (int i = 0; i < g_num_actions; i++) {
                        if (str_equals_nocase(g_actions[i].description, desc)) {
                            target_idx = i;
                            break;
                        }
                    }
                    if (target_idx < 0 && is_sc && g_num_actions < MAX_DAW_ACTIONS) {
                        target_idx = register_builtin_action(ACT_USER_IMPORTED_SCRIPT, desc,
                                                             NULL, NULL, NULL, NULL, true, is_py);
                    }
                    if (target_idx >= 0) {
                        DawActionEntry *e = &g_actions[target_idx];
                        e->is_pinned = json_extract_bool_field(obj_s, obj_e, "is_pinned", e->is_pinned);
                        if (is_sc) e->is_script = true;
                        if (is_py) e->is_python = true;
                        if (spath[0] != '\0') {
                            strncpy(e->script_filepath, spath, sizeof(e->script_filepath) - 1);
                            e->script_filepath[sizeof(e->script_filepath) - 1] = '\0';
                        }

                        // Parse "shortcuts": [ ... ] inside [obj_s, obj_e)
                        const char *sc_key = strstr(obj_s, "\"shortcuts\"");
                        if (sc_key && sc_key < obj_e) {
                            const char *lb = strchr(sc_key, '[');
                            const char *rb = lb ? strchr(lb, ']') : NULL;
                            if (lb && rb && rb < obj_e) {
                                e->num_shortcuts = 0;
                                e->combined_shortcuts[0] = '\0';
                                const char *sp = lb + 1;
                                while (sp < rb) {
                                    const char *q = strchr(sp, '\"');
                                    if (!q || q >= rb) break;
                                    char one_sc[SHORTCUT_STR_LEN] = {0};
                                    if (json_parse_quoted_string(&q, one_sc, sizeof(one_sc)) && one_sc[0] != '\0') {
                                        add_shortcut_to_entry(e, one_sc);
                                    }
                                    sp = q;
                                }
                                rebuild_action_combined_shortcuts(e);
                            }
                        }
                    }
                }

                cur = obj_e + 1;
            }
        }
    }

    // 3. Parse "app_settings" block if present (permanent popup, recording, dynamic split, reposition & export settings)
    const char *app_sec = strstr(json_buf, "\"app_settings\"");
    if (app_sec) {
        const char *app_end = json_buf + read_bytes;
        init_default_recording_folder();
        char loaded_rec_dir[512] = {0};
        if (json_extract_string_field(app_sec, app_end, "recording_folder", loaded_rec_dir, sizeof(loaded_rec_dir)) && loaded_rec_dir[0]) {
            strncpy(g_rec.folder_path, loaded_rec_dir, sizeof(g_rec.folder_path) - 1);
            g_rec.folder_path[sizeof(g_rec.folder_path) - 1] = '\0';
        }
        g_reposition.mode = json_extract_int_field(app_sec, app_end, "reposition_mode", g_reposition.mode);
        const char *rep_int_hit = strstr(app_sec, "\"reposition_interval_sec\"");
        if (rep_int_hit && rep_int_hit < app_end) {
            double rv = 1.0;
            if (parse_number_after_token(rep_int_hit, "reposition_interval_sec", &rv)) {
                g_reposition.interval_sec = rv;
                snprintf(g_reposition.interval_str, sizeof(g_reposition.interval_str), "%.2f", rv);
            }
        }
        g_dyn_split.split_at_transients    = json_extract_bool_field(app_sec, app_end, "dyn_split_at_transients", g_dyn_split.split_at_transients);
        g_dyn_split.split_when_gate_opens  = json_extract_bool_field(app_sec, app_end, "dyn_split_gate_opens", g_dyn_split.split_when_gate_opens);
        g_dyn_split.split_when_gate_closes = json_extract_bool_field(app_sec, app_end, "dyn_split_gate_closes", g_dyn_split.split_when_gate_closes);
        g_dyn_split.reduce_splits_enabled  = json_extract_bool_field(app_sec, app_end, "dyn_reduce_splits_enabled", g_dyn_split.reduce_splits_enabled);
        g_dyn_split.reduce_splits_val      = json_extract_int_field(app_sec, app_end, "dyn_reduce_splits_val", g_dyn_split.reduce_splits_val);
        g_dyn_split.min_slice_ms           = json_extract_int_field(app_sec, app_end, "dyn_min_slice_ms", g_dyn_split.min_slice_ms);
        g_dyn_split.min_silence_ms         = json_extract_int_field(app_sec, app_end, "dyn_min_silence_ms", g_dyn_split.min_silence_ms);
        int thr10 = json_extract_int_field(app_sec, app_end, "dyn_threshold_db", (int)(g_dyn_split.gate_threshold_db * 10.0f));
        int hys10 = json_extract_int_field(app_sec, app_end, "dyn_hysteresis_db", (int)(g_dyn_split.gate_hysteresis_db * 10.0f));
        g_dyn_split.gate_threshold_db      = (float)thr10 / 10.0f;
        g_dyn_split.gate_hysteresis_db     = (float)hys10 / 10.0f;
        g_dyn_split.display_gate_threshold = json_extract_bool_field(app_sec, app_end, "dyn_display_threshold", g_dyn_split.display_gate_threshold);
        g_dyn_split.action_mode            = json_extract_int_field(app_sec, app_end, "dyn_action_mode", g_dyn_split.action_mode);
        g_dyn_split.set_timebase_to_beats  = json_extract_bool_field(app_sec, app_end, "dyn_timebase_beats", g_dyn_split.set_timebase_to_beats);
        g_dyn_split.set_snap_offset_peak   = json_extract_bool_field(app_sec, app_end, "dyn_snap_offset_peak", g_dyn_split.set_snap_offset_peak);
        g_dyn_split.snap_offset_first_ms   = json_extract_int_field(app_sec, app_end, "dyn_snap_offset_ms", g_dyn_split.snap_offset_first_ms);
        g_dyn_split.remove_silent_areas    = json_extract_bool_field(app_sec, app_end, "dyn_remove_silent", g_dyn_split.remove_silent_areas);
        g_dyn_split.leading_pad_ms         = json_extract_int_field(app_sec, app_end, "dyn_leading_pad_ms", g_dyn_split.leading_pad_ms);
        g_dyn_split.trailing_pad_ms        = json_extract_int_field(app_sec, app_end, "dyn_trailing_pad_ms", g_dyn_split.trailing_pad_ms);
        g_dyn_split.fade_pad               = json_extract_bool_field(app_sec, app_end, "dyn_fade_pad", g_dyn_split.fade_pad);
        g_dyn_split.create_chromatic_midi  = json_extract_bool_field(app_sec, app_end, "dyn_chromatic_midi", g_dyn_split.create_chromatic_midi);

        g_export.source          = (ExportSourceType)json_extract_int_field(app_sec, app_end, "export_source", (int)g_export.source);
        g_export.sample_rate     = json_extract_int_field(app_sec, app_end, "export_sample_rate", g_export.sample_rate);
        g_export.channels        = json_extract_int_field(app_sec, app_end, "export_channels", g_export.channels);
        g_export.format          = (ExportFormatType)json_extract_int_field(app_sec, app_end, "export_format", (int)g_export.format);
        g_export.wav_bit_depth   = json_extract_int_field(app_sec, app_end, "export_wav_bits", g_export.wav_bit_depth);
        g_export.mp3_bitrate     = json_extract_int_field(app_sec, app_end, "export_mp3_kbps", g_export.mp3_bitrate);
        g_export.two_pass_render = json_extract_bool_field(app_sec, app_end, "export_two_pass", g_export.two_pass_render);
        char exp_dir[512] = {0}, exp_fn[128] = {0};
        if (json_extract_string_field(app_sec, app_end, "export_directory", exp_dir, sizeof(exp_dir)) && exp_dir[0]) {
            strncpy(g_export.directory, exp_dir, sizeof(g_export.directory) - 1);
        }
        if (json_extract_string_field(app_sec, app_end, "export_filename", exp_fn, sizeof(exp_fn)) && exp_fn[0]) {
            strncpy(g_export.filename, exp_fn, sizeof(g_export.filename) - 1);
        }
    }

    free(json_buf);
    return true;
}

// Formats an SDL_Keycode + SDL_Keymod into canonical REAPER shortcut notation
static bool format_sdl_key_chord(SDL_Keycode sym, SDL_Keymod mod, char *out_buf, size_t max_len) {
    if (!out_buf || max_len == 0) return false;
    out_buf[0] = '\0';

    // Ignore standalone modifier key presses
    if (sym == 1073742048 || sym == 1073742049 || sym == 1073742050 ||
        sym == 1073742052 || sym == 1073742053 || sym == 1073742054) {
        return false;
    }

    bool ctrl  = (mod & KMOD_CTRL) != 0;
    bool alt   = (mod & KMOD_ALT) != 0;
    bool shift = (mod & KMOD_SHIFT) != 0;

    // Special shifted single-character symbols matching REAPER / Basic Shortcut.pdf:
    // '~' (Shift + `), '_' (Shift + -), '+' (Shift + =), 'Shift+?' (Shift + /)
    if (!ctrl && !alt && shift) {
        if (sym == SDLK_BACKQUOTE) {
            snprintf(out_buf, max_len, "~");
            return true;
        }
        if (sym == SDLK_MINUS) {
            snprintf(out_buf, max_len, "_");
            return true;
        }
        if (sym == SDLK_EQUALS || sym == SDLK_PLUS) {
            snprintf(out_buf, max_len, "+");
            return true;
        }
        if (sym == SDLK_SLASH || sym == SDLK_QUESTION) {
            snprintf(out_buf, max_len, "Shift+?");
            return true;
        }
    }

    const char *key_name = NULL;
    char single_buf[16] = {0};

    if (sym >= SDLK_a && sym <= SDLK_z) {
        single_buf[0] = (char)toupper((unsigned char)sym);
        single_buf[1] = '\0';
        key_name = single_buf;
    } else if (sym >= SDLK_0 && sym <= SDLK_9) {
        single_buf[0] = (char)sym;
        single_buf[1] = '\0';
        key_name = single_buf;
    } else {
        switch (sym) {
            case SDLK_SPACE:        key_name = "Space"; break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:     key_name = "Enter"; break;
            case SDLK_TAB:          key_name = "Tab"; break;
            case SDLK_BACKSPACE:    key_name = "Backspace"; break;
            case SDLK_DELETE:       key_name = "Delete"; break;
            case SDLK_INSERT:       key_name = "Insert"; break;
            case SDLK_HOME:         key_name = "Home"; break;
            case SDLK_END:          key_name = "End"; break;
            case SDLK_PAGEUP:       key_name = "Page Up"; break;
            case SDLK_PAGEDOWN:     key_name = "Page Down"; break;
            case SDLK_UP:           key_name = "Up"; break;
            case SDLK_DOWN:         key_name = "Down"; break;
            case SDLK_LEFT:         key_name = "Left"; break;
            case SDLK_RIGHT:        key_name = "Right"; break;
            case SDLK_CAPSLOCK:     key_name = "Caps Lock"; break;
            case SDLK_BACKQUOTE:    key_name = "`"; break;
            case SDLK_COMMA:        key_name = ","; break;
            case SDLK_PERIOD:       key_name = "."; break;
            case SDLK_SLASH:        key_name = shift ? "?" : "/"; break;
            case SDLK_QUESTION:     key_name = "?"; break;
            case SDLK_EQUALS:       key_name = "="; break;
            case SDLK_PLUS:         key_name = "+"; break;
            case SDLK_MINUS:        key_name = "-"; break;
            case SDLK_UNDERSCORE:   key_name = "_"; break;
            case SDLK_LEFTBRACKET:  key_name = "["; break;
            case SDLK_RIGHTBRACKET: key_name = "]"; break;
            case SDLK_BACKSLASH:    key_name = "\\"; break;
            case SDLK_SEMICOLON:    key_name = ";"; break;
            case SDLK_QUOTE:        key_name = "'"; break;
            case SDLK_KP_0:         key_name = "Num 0"; break;
            case SDLK_KP_1:         key_name = "Num 1"; break;
            case SDLK_KP_2:         key_name = "Num 2"; break;
            case SDLK_KP_3:         key_name = "Num 3"; break;
            case SDLK_KP_4:         key_name = "Num 4"; break;
            case SDLK_KP_5:         key_name = "Num 5"; break;
            case SDLK_KP_6:         key_name = "Num 6"; break;
            case SDLK_KP_7:         key_name = "Num 7"; break;
            case SDLK_KP_8:         key_name = "Num 8"; break;
            case SDLK_KP_9:         key_name = "Num 9"; break;
            case SDLK_KP_PLUS:      key_name = "Num +"; break;
            case SDLK_KP_MINUS:     key_name = "Num -"; break;
            case SDLK_KP_MULTIPLY:  key_name = "Num *"; break;
            case SDLK_KP_DIVIDE:    key_name = "Num /"; break;
            case SDLK_KP_PERIOD:    key_name = "Num ."; break;
            case SDLK_F1:           key_name = "F1"; break;
            case SDLK_F2:           key_name = "F2"; break;
            case SDLK_F3:           key_name = "F3"; break;
            case SDLK_F4:           key_name = "F4"; break;
            case SDLK_F5:           key_name = "F5"; break;
            case SDLK_F6:           key_name = "F6"; break;
            case SDLK_F7:           key_name = "F7"; break;
            case SDLK_F8:           key_name = "F8"; break;
            case SDLK_F9:           key_name = "F9"; break;
            case SDLK_F10:          key_name = "F10"; break;
            case SDLK_F11:          key_name = "F11"; break;
            case SDLK_F12:          key_name = "F12"; break;
            default:                return false;
        }
    }

    char prefix[64] = "";
    if (ctrl)  strcat(prefix, "Ctrl+");
    if (alt)   strcat(prefix, "Alt+");
    if (shift) strcat(prefix, "Shift+");

    snprintf(out_buf, max_len, "%s%s", prefix, key_name);
    return true;
}

// Normalizes a shortcut string by stripping spaces around '+' so "Ctrl + Shift + =" matches "Ctrl+Shift+="
static void normalize_shortcut_str(const char *in, char *out, size_t max_len) {
    size_t j = 0;
    for (size_t i = 0; in && in[i] != '\0' && j + 1 < max_len; i++) {
        if (in[i] == ' ' && (i > 0 && (in[i - 1] == '+' || in[i + 1] == '+'))) {
            continue;
        }
        out[j++] = (char)tolower((unsigned char)in[i]);
    }
    out[j] = '\0';
}

static bool shortcut_strings_match(const char *a, const char *b) {
    char na[64], nb[64];
    normalize_shortcut_str(a, na, sizeof(na));
    normalize_shortcut_str(b, nb, sizeof(nb));
    if (strcmp(na, nb) == 0) return true;
    if ((strcmp(na, "shift+?") == 0 || strcmp(na, "?") == 0 || strcmp(na, "shift+/") == 0) &&
        (strcmp(nb, "shift+?") == 0 || strcmp(nb, "?") == 0 || strcmp(nb, "shift+/") == 0)) {
        return true;
    }
    if ((strcmp(na, "ctrl+shift+=") == 0 || strcmp(na, "ctrl+shift++") == 0) &&
        (strcmp(nb, "ctrl+shift+=") == 0 || strcmp(nb, "ctrl+shift++") == 0)) {
        return true;
    }
    if ((strcmp(na, "ctrl+shift+-") == 0 || strcmp(na, "ctrl+shift+_") == 0) &&
        (strcmp(nb, "ctrl+shift+-") == 0 || strcmp(nb, "ctrl+shift+_") == 0)) {
        return true;
    }
    return false;
}

static int find_action_by_shortcut_str(const char *chord) {
    if (!chord || !chord[0]) return -1;
    for (int i = 0; i < g_num_actions; i++) {
        for (int s = 0; s < g_actions[i].num_shortcuts; s++) {
            if (shortcut_strings_match(g_actions[i].shortcuts[s], chord)) {
                return i;
            }
        }
    }
    return -1;
}

// Synchronizes any scripts in g_daw.scripts[] into g_actions[] so every script appears in the
// Keyboard Shortcut window as "Script: <name>" with its file path attached and customizable shortcut key.
static void sync_imported_scripts_to_actions(DawEngine *daw) {
    if (!daw) return;
    for (int s = 0; s < daw->num_scripts; s++) {
        DawScript *sc = &daw->scripts[s];
        if (!sc->name[0]) continue;

        char expected_desc[ACTION_DESC_LEN];
        if (strncmp(sc->name, "Script:", 7) == 0) {
            snprintf(expected_desc, sizeof(expected_desc), "%s", sc->name);
        } else {
            snprintf(expected_desc, sizeof(expected_desc), "Script: %s", sc->name);
        }

        int found_idx = -1;
        for (int a = 0; a < g_num_actions; a++) {
            if (g_actions[a].linked_script_idx == s ||
                str_equals_nocase(g_actions[a].description, expected_desc)) {
                found_idx = a;
                break;
            }
        }

        if (found_idx >= 0) {
            DawActionEntry *e = &g_actions[found_idx];
            e->is_script = true;
            e->is_python = sc->is_python;
            e->linked_script_idx = s;
            if (sc->filepath[0] != '\0') {
                strncpy(e->script_filepath, sc->filepath, sizeof(e->script_filepath) - 1);
                e->script_filepath[sizeof(e->script_filepath) - 1] = '\0';
            }
            if (sc->shortcut[0] && e->num_shortcuts == 0) {
                add_shortcut_to_entry(e, sc->shortcut);
            } else if (e->num_shortcuts > 0) {
                strncpy(sc->shortcut, e->shortcuts[0], sizeof(sc->shortcut) - 1);
                sc->shortcut[sizeof(sc->shortcut) - 1] = '\0';
            }
        } else if (g_num_actions < MAX_DAW_ACTIONS) {
            int idx = register_builtin_action(ACT_USER_IMPORTED_SCRIPT,
                expected_desc,
                sc->shortcut[0] ? sc->shortcut : NULL,
                NULL, NULL, NULL, true, sc->is_python);
            if (idx >= 0) {
                g_actions[idx].linked_script_idx = s;
                strncpy(g_actions[idx].script_filepath, sc->filepath, sizeof(g_actions[idx].script_filepath) - 1);
                g_actions[idx].script_filepath[sizeof(g_actions[idx].script_filepath) - 1] = '\0';
            }
        }
    }
    refresh_filtered_actions();
    refresh_filtered_scripts();
}

// Registers a discovered .lua or .py file from disk into g_daw.scripts[] and g_actions[]
static void register_discovered_script_file(DawEngine *daw, const char *fullpath, const char *filename) {
    if (!daw || !fullpath || !filename || !filename[0]) return;
    bool is_lua = str_ends_with_nocase(filename, ".lua");
    bool is_py  = str_ends_with_nocase(filename, ".py");
    if (!is_lua && !is_py) return;

    int sc_idx = -1;
    for (int s = 0; s < daw->num_scripts; s++) {
        if (str_equals_nocase(daw->scripts[s].filepath, fullpath) ||
            str_equals_nocase(daw->scripts[s].name, filename)) {
            sc_idx = s;
            break;
        }
    }
    if (sc_idx < 0 && daw->num_scripts < MAX_SCRIPTS) {
        sc_idx = daw->num_scripts++;
        memset(&daw->scripts[sc_idx], 0, sizeof(DawScript));
        strncpy(daw->scripts[sc_idx].name, filename, sizeof(daw->scripts[sc_idx].name) - 1);
        strncpy(daw->scripts[sc_idx].filepath, fullpath, sizeof(daw->scripts[sc_idx].filepath) - 1);
        daw->scripts[sc_idx].is_python = is_py;
    } else if (sc_idx >= 0) {
        if (daw->scripts[sc_idx].filepath[0] == '\0') {
            strncpy(daw->scripts[sc_idx].filepath, fullpath, sizeof(daw->scripts[sc_idx].filepath) - 1);
        }
        daw->scripts[sc_idx].is_python = is_py;
    }
}

// Scans a single directory path for any manually created .lua or .py scripts
static void scan_single_script_folder(DawEngine *daw, const char *dir_path) {
    if (!daw || !dir_path || !dir_path[0]) return;
#ifdef _WIN32
    char search_pat[1024];
    snprintf(search_pat, sizeof(search_pat), "%s\\*.*", dir_path);
    WIN32_FIND_DATAA fdata;
    HANDLE hFind = FindFirstFileA(search_pat, &fdata);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            const char *fname = fdata.cFileName;
            if (str_ends_with_nocase(fname, ".lua") || str_ends_with_nocase(fname, ".py")) {
                char full[1024];
                snprintf(full, sizeof(full), "%s\\%s", dir_path, fname);
                register_discovered_script_file(daw, full, fname);
            }
        }
    } while (FindNextFileA(hFind, &fdata) != 0);
    FindClose(hFind);
#else
    DIR *d = opendir(dir_path);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *fname = ent->d_name;
        if (fname[0] == '.') continue;
        if (str_ends_with_nocase(fname, ".lua") || str_ends_with_nocase(fname, ".py")) {
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", dir_path, fname);
            register_discovered_script_file(daw, full, fname);
        }
    }
    closedir(d);
#endif
}

// Automatically scans scripts/lua, scripts/python, and scripts folders in both the installed folder
// and working directory so any manually created .lua or .py script is detected and ready to use.
static void scan_scripts_directory_to_actions(DawEngine *daw) {
    if (!daw) return;

    char dir_buf[1024];
#ifdef _WIN32
    get_installed_file_path(dir_buf, sizeof(dir_buf), "scripts");
    CreateDirectoryA(dir_buf, NULL);
    get_installed_file_path(dir_buf, sizeof(dir_buf), "scripts\\lua");
    CreateDirectoryA(dir_buf, NULL);
    get_installed_file_path(dir_buf, sizeof(dir_buf), "scripts\\python");
    CreateDirectoryA(dir_buf, NULL);
#else
    get_installed_file_path(dir_buf, sizeof(dir_buf), "scripts");
    mkdir(dir_buf, 0777);
    get_installed_file_path(dir_buf, sizeof(dir_buf), "scripts/lua");
    mkdir(dir_buf, 0777);
    get_installed_file_path(dir_buf, sizeof(dir_buf), "scripts/python");
    mkdir(dir_buf, 0777);
#endif

    const char *rel_dirs[] = {
        "scripts/lua",
        "scripts/python",
        "scripts",
        "lua",
        "python",
        "../scripts/lua",
        "../scripts/python",
        "../scripts",
        "dist/scripts/lua",
        "dist/scripts/python",
        "dist/scripts"
    };
    int num_dirs = (int)(sizeof(rel_dirs) / sizeof(rel_dirs[0]));
    for (int i = 0; i < num_dirs; i++) {
        char inst_dir[1024];
        get_installed_file_path(inst_dir, sizeof(inst_dir), rel_dirs[i]);
        scan_single_script_folder(daw, inst_dir);
        if (strcmp(inst_dir, rel_dirs[i]) != 0) {
            scan_single_script_folder(daw, rel_dirs[i]);
        }
    }

    // Also ensure every built-in script action in g_actions[] is listed in g_daw.scripts[]
    // so the unified Script Manager window displays all Lua and Python scripts together.
    for (int a = 0; a < g_num_actions; a++) {
        if (!g_actions[a].is_script) continue;
        const char *raw_name = g_actions[a].description;
        if (strncmp(raw_name, "Script:", 7) == 0) {
            raw_name += 7;
            while (*raw_name == ' ') raw_name++;
        }
        if (!raw_name[0]) continue;
        int found_s = -1;
        for (int s = 0; s < daw->num_scripts; s++) {
            if (str_equals_nocase(daw->scripts[s].name, raw_name) ||
                str_equals_nocase(daw->scripts[s].name, g_actions[a].description)) {
                found_s = s;
                break;
            }
        }
        if (found_s < 0 && daw->num_scripts < MAX_SCRIPTS) {
            found_s = daw->num_scripts++;
            memset(&daw->scripts[found_s], 0, sizeof(DawScript));
            strncpy(daw->scripts[found_s].name, raw_name, sizeof(daw->scripts[found_s].name) - 1);
            strncpy(daw->scripts[found_s].filepath, g_actions[a].script_filepath, sizeof(daw->scripts[found_s].filepath) - 1);
            daw->scripts[found_s].is_python = g_actions[a].is_python || str_ends_with_nocase(raw_name, ".py");
            if (g_actions[a].num_shortcuts > 0) {
                strncpy(daw->scripts[found_s].shortcut, g_actions[a].shortcuts[0], sizeof(daw->scripts[found_s].shortcut) - 1);
            }
        }
        if (found_s >= 0) {
            g_actions[a].linked_script_idx = found_s;
        }
    }

    sync_imported_scripts_to_actions(daw);
}

// Populates ONLY the keyboard shortcuts listed in Basic Shortcut.pdf, scans script directories,
// and loads/creates the permanent keyboard_shortcuts.json in the software installed folder.
static void init_keyboard_actions_system(void) {
    g_num_actions = 0;

    // ---- VIEW & ZOOM (Basic Shortcut.pdf) ----
    register_builtin_action(ACT_VIEW_ZOOM_HORIZ_WHEEL,
        "View: Zoom horizontally (mousewheel)",
        "Mousewheel", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_VIEW_SCROLL_HORIZ_WHEEL,
        "View: Scroll horizontally (mousewheel)",
        "Alt+Mousewheel", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_VIEW_ZOOM_IN_HORIZ,
        "View: Zoom in horizontal",
        "Z", "+", "=", "Num +", false, false);
    register_builtin_action(ACT_VIEW_ZOOM_OUT_HORIZ,
        "View: Zoom out horizontal",
        "X", "-", "Num -", NULL, false, false);
    register_builtin_action(ACT_VIEW_ZOOM_IN_VERT,
        "View: Zoom in vertical",
        "Up", "Shift+Up", NULL, NULL, false, false);
    int zv_out_idx = register_builtin_action(ACT_VIEW_ZOOM_OUT_VERT,
        "View: Zoom out vertical",
        "Down", "Shift+Down", NULL, NULL, false, false);

    // ---- ITEM SELECTION & NAVIGATION (Basic Shortcut.pdf) ----
    register_builtin_action(ACT_ITEM_NAV_PREV_ITEM,
        "Item navigation: Select and move to previous item",
        ",", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_NAV_NEXT_ITEM,
        "Item navigation: Select and move to next item",
        ".", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_NAV_CURSOR_LEFT_EDGE_OF_ITEM,
        "Item navigation: Move cursor left to edge of item",
        "Num 1", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_NAV_CURSOR_RIGHT_EDGE_OF_ITEM,
        "Item navigation: Move cursor right to edge of item",
        "Num 3", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_NAV_CURSOR_LEFT_NEAREST_EDGE,
        "Item navigation: Move cursor left to nearest item edge",
        "Num 7", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_NAV_CURSOR_RIGHT_NEAREST_EDGE,
        "Item navigation: Move cursor right to nearest item edge",
        "Num 9", NULL, NULL, NULL, false, false);

    // ---- AUDIO ITEM EDITING & GAIN OPERATIONS (Basic Shortcut.pdf) ----
    register_builtin_action(ACT_EDIT_REMOVE_FOCUS,
        "Remove items/tracks/envelope points (depending on focus)",
        "Delete", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_TOGGLE_MUTE_FOCUS,
        "Item properties: Toggle items/tracks mute (depending on focus)",
        "Alt+M", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_NUDGE_VOL_UP_1DB,
        "Item: Nudge items volume +1dB",
        "Ctrl+Shift+=", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_NUDGE_VOL_DOWN_1DB,
        "Item: Nudge items volume -1dB",
        "Ctrl+Shift+-", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_SET_CUSTOM_COLOR,
        "Item: Set to custom color...",
        "Ctrl+Alt+Shift+C", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_MOVE_UP_TRACK,
        "Item: Move selected clip(s) up one track",
        "8", "Num 8", NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_MOVE_DOWN_TRACK,
        "Item: Move selected clip(s) down one track",
        "2", "Num 2", NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_DYNAMIC_SPLIT,
        "Item: Dynamic split items...",
        "Ctrl+D", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_JOIN_GLUE,
        "Item: Join (glue) selected items",
        "J", "Ctrl+J", NULL, NULL, false, false);

    // ---- TRACK CONTROLS & TAB MANAGEMENT (Basic Shortcut.pdf) ----
    register_builtin_action(ACT_TRACK_TOGGLE_SOLO_SELECTED,
        "Track: Toggle solo for selected tracks",
        "Q", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRACK_MOVE_UP,
        "Track: Move tracks up",
        "Alt+Shift+Up", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRACK_MOVE_DOWN,
        "Track: Move tracks down",
        "Alt+Shift+Down", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TAB_SWITCH_NEXT,
        "Project tabs: Switch to next project tab",
        "Ctrl+`", NULL, NULL, NULL, false, false);

    // ---- SWS & CUSTOM LUA SCRIPTS (Basic Shortcut.pdf) ----
    register_builtin_action(ACT_SWS_REPOSITION_ITEMS,
        "Xenakios/SWS: Reposition selected items...",
        "W", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_SWS_SCROLL_TRACK_VIEW_DOWN_PAGE,
        "Xenakios/SWS: Scroll track view down (page)",
        "Caps Lock", "Page Down", NULL, NULL, false, false);
    register_builtin_action(ACT_SWS_SCROLL_TRACK_VIEW_UP_PAGE,
        "Xenakios/SWS: Scroll track view up (page)",
        "Tab", "Page Up", NULL, NULL, false, false);
    register_builtin_action(ACT_SWS_MOVE_RIGHT_EDGE_TO_CURSOR,
        "SWS: Move selected item(s) right edge to edit cursor",
        "Ctrl+Num 1", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_SWS_MOVE_LEFT_EDGE_TO_CURSOR,
        "SWS: Move selected item(s) left edge to edit cursor",
        "Ctrl+Num 3", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_SCRIPT_CLIP_MOVE_TO_SELECTED_TRACK,
        "Script: Clip move to selected track.lua",
        "Alt+S", NULL, NULL, NULL, true, false);
    register_builtin_action(ACT_SCRIPT_TRACKS_MOVE_TO_BOTTOM,
        "Script: Tracks move to bottom.lua",
        "Alt+B", NULL, NULL, NULL, true, false);
    register_builtin_action(ACT_SCRIPT_TRACKS_MOVE_TO_TOP,
        "Script: Tracks Move to Top.lua",
        "Alt+T", NULL, NULL, NULL, true, false);
    register_builtin_action(ACT_SCRIPT_GROUP_AND_UNGROUP,
        "Script: Group and Ungroup.lua",
        "Ctrl+Alt+Shift+G", NULL, NULL, NULL, true, false);

    // ---- TRANSPORT & EDITING (Basic Shortcut.pdf) ----
    register_builtin_action(ACT_TRANSPORT_PLAY_STOP,
        "Transport: Play/stop",
        "Space", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRANSPORT_RECORD,
        "Transport: Record",
        "R", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRANSPORT_GO_TO_START,
        "Transport: Go to start of project",
        "Home", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRANSPORT_GO_TO_END,
        "Transport: Go to end of project",
        "End", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_SPLIT_AT_CURSOR,
        "Item: Split items at edit or play cursor",
        "S", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_EDIT_COPY,
        "Edit: Copy",
        "Ctrl+C", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_EDIT_CUT,
        "Edit: Cut",
        "Ctrl+X", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_EDIT_PASTE,
        "Edit: Paste",
        "Ctrl+V", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_EDIT_UNDO,
        "Edit: Undo",
        "Ctrl+Z", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_EDIT_REDO,
        "Edit: Redo",
        "Ctrl+Shift+Z", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_EDIT_SELECT_ALL,
        "Edit: Select all",
        "Ctrl+A", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_VIEW_SHOW_ACTION_LIST,
        "View: Show action list",
        "Shift+?", NULL, NULL, NULL, false, false);

    if (zv_out_idx >= 0) {
        g_action_win.selected_action_idx = zv_out_idx;
    }

    // Load saved permanent shortcuts from keyboard_shortcuts.json in installed folder,
    // and auto-detect any manually created .lua / .py scripts in scripts/ folders.
    init_default_saved_themes();
    load_keyboard_shortcuts_json();
    load_themes_settings_json();
    scan_scripts_directory_to_actions(&g_daw);
    // Always write/update keyboard_shortcuts.json and themes_settings.json in the software installed folder so they exist permanently
    save_themes_settings_json();
    refresh_filtered_actions();
}

// Ensure the currently selected action is visible inside the filtered table scroll view
static void scroll_action_list_to_selected(int list_h) {
    int filtered_row = -1;
    for (int r = 0; r < g_action_win.num_filtered; r++) {
        if (g_action_win.filtered_indices[r] == g_action_win.selected_action_idx) {
            filtered_row = r;
            break;
        }
    }
    if (filtered_row < 0) return;
    int row_top = filtered_row * 22;
    int row_bot = row_top + 22;
    if (row_top < g_action_win.scroll_y) {
        g_action_win.scroll_y = row_top;
    } else if (row_bot > g_action_win.scroll_y + list_h) {
        g_action_win.scroll_y = row_bot - list_h;
    }
    if (g_action_win.scroll_y < 0) g_action_win.scroll_y = 0;
}

// Imports a Lua (.lua) or Python (.py) script file via file dialog, registers it in g_daw.scripts[]
// and in the Keyboard Shortcut Actions list ("Script: <filename>"), and saves permanently to keyboard_shortcuts.json
static void import_script_into_action_system(bool default_python) {
    char spath[MAX_PATH] = {0};
    const char *flt = default_python
        ? "Python & Lua Scripts (*.py;*.lua)\0*.py;*.lua\0Python (*.py)\0*.py\0Lua (*.lua)\0*.lua\0All Files (*.*)\0*.*\0"
        : "Lua & Python Scripts (*.lua;*.py)\0*.lua;*.py\0Lua (*.lua)\0*.lua\0Python (*.py)\0*.py\0All Files (*.*)\0*.*\0";
    if (!open_generic_dialog(spath, sizeof(spath), flt, false) || !spath[0]) {
        return;
    }

    const char *fn = strrchr(spath, '\\');
    if (!fn) fn = strrchr(spath, '/');
    const char *short_name = fn ? (fn + 1) : spath;

    bool is_py = default_python;
    const char *ext = strrchr(short_name, '.');
    if (ext) {
        if (str_equals_nocase(ext, ".py")) is_py = true;
        else if (str_equals_nocase(ext, ".lua")) is_py = false;
    }

    // Check if already in g_daw.scripts
    int sc_idx = -1;
    for (int s = 0; s < g_daw.num_scripts; s++) {
        if (str_equals_nocase(g_daw.scripts[s].filepath, spath) ||
            str_equals_nocase(g_daw.scripts[s].name, short_name)) {
            sc_idx = s;
            break;
        }
    }

    if (sc_idx < 0 && g_daw.num_scripts < MAX_SCRIPTS) {
        sc_idx = g_daw.num_scripts++;
        DawScript *sc = &g_daw.scripts[sc_idx];
        memset(sc, 0, sizeof(DawScript));
        sc->is_python = is_py;
        strncpy(sc->filepath, spath, sizeof(sc->filepath) - 1);
        strncpy(sc->name, short_name, sizeof(sc->name) - 1);
        sc->shortcut[0] = '\0';
    } else if (sc_idx >= 0) {
        DawScript *sc = &g_daw.scripts[sc_idx];
        sc->is_python = is_py;
        strncpy(sc->filepath, spath, sizeof(sc->filepath) - 1);
        strncpy(sc->name, short_name, sizeof(sc->name) - 1);
    }

    sync_imported_scripts_to_actions(&g_daw);
    save_keyboard_shortcuts_json();

    // Select the newly imported script in the Actions window
    char expected_desc[ACTION_DESC_LEN];
    if (strncmp(short_name, "Script:", 7) == 0) {
        snprintf(expected_desc, sizeof(expected_desc), "%s", short_name);
    } else {
        snprintf(expected_desc, sizeof(expected_desc), "Script: %s", short_name);
    }
    for (int a = 0; a < g_num_actions; a++) {
        if ((sc_idx >= 0 && g_actions[a].linked_script_idx == sc_idx) ||
            str_equals_nocase(g_actions[a].description, expected_desc)) {
            g_action_win.selected_action_idx = a;
            g_action_win.selected_shortcut_sub = 0;
            scroll_action_list_to_selected(g_action_win.win_rect.h - 244);
            break;
        }
    }

    snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
             "Saved %s to keyboard_shortcuts.json", expected_desc);
    record_project_change("Imported Action Script", false);
}

// Helper to parse the first numeric value after a substring in a script line
static bool parse_number_after_token(const char *line, const char *token, double *out_val) {
    if (!line || !token || !out_val) return false;
    const char *hit = strstr(line, token);
    if (!hit) return false;
    const char *p = hit + strlen(token);
    while (*p && !isdigit((unsigned char)*p) && *p != '-' && *p != '+') p++;
    if (!*p) return false;
    char *endp = NULL;
    double v = strtod(p, &endp);
    if (endp == p) return false;
    *out_val = v;
    return true;
}

// Resolves a script's file path if only its description ("Script: foo.lua") is set
static bool resolve_script_filepath(DawActionEntry *act, char *out_path, size_t max_len) {
    if (!act || !out_path || max_len == 0) return false;
    out_path[0] = '\0';
    if (act->script_filepath[0] != '\0') {
        FILE *f = fopen(act->script_filepath, "rb");
        if (f) {
            fclose(f);
            snprintf(out_path, max_len, "%s", act->script_filepath);
            return true;
        }
    }

    const char *raw_name = act->description;
    if (strncmp(raw_name, "Script:", 7) == 0) {
        raw_name += 7;
        while (*raw_name == ' ') raw_name++;
    }
    if (!raw_name[0]) return false;

    const char *folders[] = {
        "",
        "scripts/lua/",
        "scripts/python/",
        "scripts/",
        "lua/",
        "python/",
        "../scripts/lua/",
        "../scripts/python/",
        "../scripts/",
        "dist/scripts/lua/",
        "dist/scripts/python/",
        "dist/scripts/"
    };
    int nf = (int)(sizeof(folders) / sizeof(folders[0]));
    for (int i = 0; i < nf; i++) {
        char rel_cand[600];
        snprintf(rel_cand, sizeof(rel_cand), "%s%s", folders[i], raw_name);

        char inst_cand[1024];
        get_installed_file_path(inst_cand, sizeof(inst_cand), rel_cand);
        FILE *f1 = fopen(inst_cand, "rb");
        if (f1) {
            fclose(f1);
            snprintf(out_path, max_len, "%s", inst_cand);
            strncpy(act->script_filepath, inst_cand, sizeof(act->script_filepath) - 1);
            return true;
        }

        FILE *f2 = fopen(rel_cand, "rb");
        if (f2) {
            fclose(f2);
            snprintf(out_path, max_len, "%s", rel_cand);
            strncpy(act->script_filepath, rel_cand, sizeof(act->script_filepath) - 1);
            return true;
        }
    }
    return false;
}


// ================= UNIVERSAL DYNAMIC SCRIPT EXPRESSION & VARIABLE EVALUATOR =================
#define MAX_SCRIPT_VARS 32
typedef struct {
    char name[48];
    double value;
} ScriptVarBinding;

typedef struct {
    ScriptVarBinding vars[MAX_SCRIPT_VARS];
    int num_vars;
} ScriptEvalContext;

static void script_ctx_set_var(ScriptEvalContext *ctx, const char *name, double val) {
    if (!ctx || !name || !name[0]) return;
    for (int i = 0; i < ctx->num_vars; i++) {
        if (strcmp(ctx->vars[i].name, name) == 0) {
            ctx->vars[i].value = val;
            return;
        }
    }
    if (ctx->num_vars < MAX_SCRIPT_VARS) {
        strncpy(ctx->vars[ctx->num_vars].name, name, sizeof(ctx->vars[0].name) - 1);
        ctx->vars[ctx->num_vars].name[sizeof(ctx->vars[0].name) - 1] = '\0';
        ctx->vars[ctx->num_vars].value = val;
        ctx->num_vars++;
    }
}

static bool script_ctx_get_var(const ScriptEvalContext *ctx, const char *name, double *out_val) {
    if (!ctx || !name || !name[0]) return false;
    for (int i = 0; i < ctx->num_vars; i++) {
        if (strcmp(ctx->vars[i].name, name) == 0) {
            if (out_val) *out_val = ctx->vars[i].value;
            return true;
        }
    }
    return false;
}

static double parse_script_expr_add(const char **p, const ScriptEvalContext *ctx);

static void skip_expr_spaces(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') (*p)++;
}

static double parse_script_expr_primary(const char **p, const ScriptEvalContext *ctx) {
    skip_expr_spaces(p);
    if (**p == '+') { (*p)++; return parse_script_expr_primary(p, ctx); }
    if (**p == '-') { (*p)++; return -parse_script_expr_primary(p, ctx); }
    if (**p == '(') {
        (*p)++;
        double v = parse_script_expr_add(p, ctx);
        skip_expr_spaces(p);
        if (**p == ')') (*p)++;
        return v;
    }
    if (isdigit((unsigned char)**p) || (**p == '.' && isdigit((unsigned char)*(*p + 1)))) {
        char *endp = NULL;
        double v = strtod(*p, &endp);
        if (endp && endp != *p) {
            *p = endp;
            return v;
        }
    }
    if (isalpha((unsigned char)**p) || **p == '_') {
        char ident[48] = {0};
        size_t k = 0;
        while (isalnum((unsigned char)**p) || **p == '_' || **p == '.') {
            if (k + 1 < sizeof(ident)) ident[k++] = **p;
            (*p)++;
        }
        ident[k] = '\0';
        skip_expr_spaces(p);
        // Handle math functions like math.abs(...), math.floor(...), math.max(...), math.min(...)
        if (**p == '(') {
            (*p)++;
            double arg1 = parse_script_expr_add(p, ctx);
            double arg2 = 0.0;
            skip_expr_spaces(p);
            if (**p == ',') {
                (*p)++;
                arg2 = parse_script_expr_add(p, ctx);
            }
            skip_expr_spaces(p);
            if (**p == ')') (*p)++;
            if (str_contains_nocase(ident, "abs")) return fabs(arg1);
            if (str_contains_nocase(ident, "floor")) return floor(arg1);
            if (str_contains_nocase(ident, "ceil")) return ceil(arg1);
            if (str_contains_nocase(ident, "sqrt")) return (arg1 >= 0.0) ? sqrt(arg1) : 0.0;
            if (str_contains_nocase(ident, "max")) return fmax(arg1, arg2);
            if (str_contains_nocase(ident, "min")) return fmin(arg1, arg2);
            if (str_contains_nocase(ident, "GetCursorPosition")) {
                double c = 0.0;
                script_ctx_get_var(ctx, "cursor", &c);
                return c;
            }
            return arg1;
        }
        if (str_equals_nocase(ident, "true")) return 1.0;
        if (str_equals_nocase(ident, "false")) return 0.0;
        double val = 0.0;
        if (script_ctx_get_var(ctx, ident, &val)) return val;
    }
    return 0.0;
}

static double parse_script_expr_mul(const char **p, const ScriptEvalContext *ctx) {
    double left = parse_script_expr_primary(p, ctx);
    for (;;) {
        skip_expr_spaces(p);
        char op = **p;
        if (op != '*' && op != '/' && op != '%') break;
        (*p)++;
        double right = parse_script_expr_primary(p, ctx);
        if (op == '*') left *= right;
        else if (op == '/') left = (fabs(right) > 1e-12) ? (left / right) : 0.0;
        else if (op == '%') left = (fabs(right) > 1e-12) ? fmod(left, right) : 0.0;
    }
    return left;
}

static double parse_script_expr_add(const char **p, const ScriptEvalContext *ctx) {
    double left = parse_script_expr_mul(p, ctx);
    for (;;) {
        skip_expr_spaces(p);
        char op = **p;
        if (op != '+' && op != '-') break;
        // Stop at Lua comment '--'
        if (op == '-' && *(*p + 1) == '-') break;
        (*p)++;
        double right = parse_script_expr_mul(p, ctx);
        if (op == '+') left += right;
        else left -= right;
    }
    return left;
}

static double eval_script_math_expr(const char *expr_str, const ScriptEvalContext *ctx) {
    if (!expr_str || !expr_str[0]) return 0.0;
    const char *p = expr_str;
    return parse_script_expr_add(&p, ctx);
}

// Populates standard per-clip variables into ScriptEvalContext so any formula works out of the box
static void populate_clip_eval_context(ScriptEvalContext *ctx, ProjectTab *p, int item_idx, int sel_order, int sel_total) {
    if (!ctx || !p || item_idx < 0 || item_idx >= p->num_items) return;
    int tab = get_valid_tab_idx();
    AudioItem *it = &p->items[item_idx];
    script_ctx_set_var(ctx, "pos", it->start_time);
    script_ctx_set_var(ctx, "position", it->start_time);
    script_ctx_set_var(ctx, "start_time", it->start_time);
    script_ctx_set_var(ctx, "D_POSITION", it->start_time);
    script_ctx_set_var(ctx, "len", it->duration);
    script_ctx_set_var(ctx, "length", it->duration);
    script_ctx_set_var(ctx, "duration", it->duration);
    script_ctx_set_var(ctx, "D_LENGTH", it->duration);
    script_ctx_set_var(ctx, "end_pos", it->start_time + it->duration);
    script_ctx_set_var(ctx, "center", it->start_time + it->duration * 0.5);
    script_ctx_set_var(ctx, "center_pos", it->start_time + it->duration * 0.5);
    script_ctx_set_var(ctx, "mid", it->start_time + it->duration * 0.5);
    script_ctx_set_var(ctx, "offset", it->offset_in_file);
    script_ctx_set_var(ctx, "D_STARTOFFS", it->offset_in_file);
    script_ctx_set_var(ctx, "fadein", get_item_fade_in(p, item_idx));
    script_ctx_set_var(ctx, "D_FADEINLEN", get_item_fade_in(p, item_idx));
    script_ctx_set_var(ctx, "fadeout", get_item_fade_out(p, item_idx));
    script_ctx_set_var(ctx, "D_FADEOUTLEN", get_item_fade_out(p, item_idx));
    script_ctx_set_var(ctx, "vol", (double)g_item_vol_db[tab][item_idx]);
    script_ctx_set_var(ctx, "D_VOL", (double)g_item_vol_db[tab][item_idx]);
    script_ctx_set_var(ctx, "mute", g_item_muted[tab][item_idx] ? 1.0 : 0.0);
    script_ctx_set_var(ctx, "B_MUTE", g_item_muted[tab][item_idx] ? 1.0 : 0.0);
    script_ctx_set_var(ctx, "color", (double)g_item_color_idx[tab][item_idx]);
    script_ctx_set_var(ctx, "I_CUSTOMCOLOR", (double)g_item_color_idx[tab][item_idx]);
    script_ctx_set_var(ctx, "track", (double)it->track_idx);
    script_ctx_set_var(ctx, "i", (double)sel_order);
    script_ctx_set_var(ctx, "idx", (double)sel_order);
    script_ctx_set_var(ctx, "count", (double)sel_total);
    script_ctx_set_var(ctx, "sel_count", (double)sel_total);
    script_ctx_set_var(ctx, "cursor", p->playhead_seconds);
    script_ctx_set_var(ctx, "playhead", p->playhead_seconds);
    script_ctx_set_var(ctx, "num_tracks", (double)p->num_tracks);
    script_ctx_set_var(ctx, "num_items", (double)p->num_items);
    for (int c = 0; c < g_script_popup.num_controls; c++) {
        ScriptPopupControl *ctrl = &g_script_popup.controls[c];
        double v = (ctrl->type == SCP_CTRL_CHECKBOX) ? (ctrl->bool_val ? 1.0 : 0.0)
                 : ((ctrl->type == SCP_CTRL_SLIDER) ? ctrl->num_val : atof(ctrl->str_val));
        char vname[32];
        snprintf(vname, sizeof(vname), "val%d", c + 1);
        script_ctx_set_var(ctx, vname, v);
        snprintf(vname, sizeof(vname), "input%d", c + 1);
        script_ctx_set_var(ctx, vname, v);
        if (ctrl->var_name[0]) script_ctx_set_var(ctx, ctrl->var_name, v);
    }
}

// Splits a specific clip item_idx at exact timeline position split_t (seconds)
static bool split_single_item_at_time(ProjectTab *p, int item_idx, double split_t, bool *io_pushed_undo) {
    if (!p || item_idx < 0 || item_idx >= p->num_items || p->num_items >= MAX_ITEMS) return false;
    int tab = get_valid_tab_idx();
    AudioItem *it = &p->items[item_idx];
    double it_s = it->start_time;
    double it_e = it->start_time + it->duration;
    if (split_t <= it_s + 0.02 || split_t >= it_e - 0.02) return false;

    if (io_pushed_undo && !(*io_pushed_undo)) {
        push_undo_state();
        *io_pushed_undo = true;
    }

    int r_idx = p->num_items;
    double orig_fin = g_item_fade_in[tab][item_idx];
    double orig_fout = g_item_fade_out[tab][item_idx];
    double left_dur = split_t - it_s;
    double right_dur = it_e - split_t;

    AudioItem *r_item = &p->items[r_idx];
    memcpy(r_item, it, sizeof(AudioItem));
    if (it->peak_count > 0 && it->peaks) {
        r_item->peaks = (PeakBin *)calloc((size_t)it->peak_count, sizeof(PeakBin));
        if (r_item->peaks) {
            memcpy(r_item->peaks, it->peaks, (size_t)it->peak_count * sizeof(PeakBin));
        }
    }

    it->duration = left_dur;
    r_item->start_time = split_t;
    r_item->offset_in_file = it->offset_in_file + left_dur;
    r_item->duration = right_dur;
    r_item->is_selected = true;
    it->is_selected = true;
    r_item->raw_file_handle = r_item->raw_audio_path[0] ? fopen(r_item->raw_audio_path, "rb") : NULL;

    g_item_fade_in[tab][item_idx] = fmin(orig_fin, it->duration);
    g_item_fade_out[tab][item_idx] = 0.0;
    g_item_fade_in[tab][r_idx] = 0.0;
    g_item_fade_out[tab][r_idx] = fmin(orig_fout, r_item->duration);
    g_item_vol_db[tab][r_idx] = g_item_vol_db[tab][item_idx];
    g_item_muted[tab][r_idx] = g_item_muted[tab][item_idx];
    g_item_color_idx[tab][r_idx] = g_item_color_idx[tab][item_idx];
    g_item_group_id[tab][r_idx] = g_item_group_id[tab][item_idx];
    p->num_items++;
    return true;
}

// Extracts the Nth comma-separated argument (0-indexed) inside a function call `FuncName(arg0, arg1, arg2)`
static bool extract_func_call_arg(const char *line, const char *func_name, int arg_index, char *out_arg, size_t max_len) {
    if (!line || !func_name || !out_arg || max_len == 0) return false;
    out_arg[0] = '\0';
    const char *hit = strstr(line, func_name);
    if (!hit) return false;
    const char *lp = strchr(hit, '(');
    if (!lp) return false;
    const char *p = lp + 1;
    int cur_arg = 0;
    int depth = 0;
    const char *arg_s = p;

    while (*p) {
        if (*p == '(') depth++;
        else if (*p == ')') {
            if (depth == 0) {
                if (cur_arg == arg_index) {
                    size_t l = (size_t)(p - arg_s);
                    while (l > 0 && (arg_s[0] == ' ' || arg_s[0] == '\t')) { arg_s++; l--; }
                    while (l > 0 && (arg_s[l - 1] == ' ' || arg_s[l - 1] == '\t')) l--;
                    if (l >= max_len) l = max_len - 1;
                    memcpy(out_arg, arg_s, l);
                    out_arg[l] = '\0';
                    return true;
                }
                break;
            }
            depth--;
        } else if (*p == ',' && depth == 0) {
            if (cur_arg == arg_index) {
                size_t l = (size_t)(p - arg_s);
                while (l > 0 && (arg_s[0] == ' ' || arg_s[0] == '\t')) { arg_s++; l--; }
                while (l > 0 && (arg_s[l - 1] == ' ' || arg_s[l - 1] == '\t')) l--;
                if (l >= max_len) l = max_len - 1;
                memcpy(out_arg, arg_s, l);
                out_arg[l] = '\0';
                return true;
            }
            cur_arg++;
            arg_s = p + 1;
        }
        p++;
    }
    return false;
}

// Splits every currently selected clip at its exact midpoint (start_time + duration * 0.5)
static bool split_selected_items_at_center(ProjectTab *p) {
    if (!p) return false;
    int tab = get_valid_tab_idx();
    int orig_count = p->num_items;
    bool did_split = false;

    for (int i = 0; i < orig_count; i++) {
        AudioItem *it = &p->items[i];
        if (!it->is_selected) continue;
        if (it->duration <= 0.10 || p->num_items >= MAX_ITEMS) continue;

        if (!did_split) {
            push_undo_state();
            did_split = true;
        }

        int r_idx = p->num_items;
        double orig_fin = g_item_fade_in[tab][i];
        double orig_fout = g_item_fade_out[tab][i];
        double half_dur = it->duration * 0.5;
        double mid_t = it->start_time + half_dur;

        AudioItem *r_item = &p->items[r_idx];
        memcpy(r_item, it, sizeof(AudioItem));
        if (it->peak_count > 0 && it->peaks) {
            r_item->peaks = (PeakBin *)calloc((size_t)it->peak_count, sizeof(PeakBin));
            if (r_item->peaks) {
                memcpy(r_item->peaks, it->peaks, (size_t)it->peak_count * sizeof(PeakBin));
            }
        }

        it->duration = half_dur;
        r_item->start_time = mid_t;
        r_item->offset_in_file = it->offset_in_file + half_dur;
        r_item->duration = half_dur;
        r_item->is_selected = true;
        it->is_selected = true;
        r_item->raw_file_handle = r_item->raw_audio_path[0] ? fopen(r_item->raw_audio_path, "rb") : NULL;

        g_item_fade_in[tab][i]      = fmin(orig_fin, it->duration);
        g_item_fade_out[tab][i]     = 0.0;
        g_item_fade_in[tab][r_idx]  = 0.0;
        g_item_fade_out[tab][r_idx] = fmin(orig_fout, r_item->duration);
        g_item_vol_db[tab][r_idx]   = g_item_vol_db[tab][i];
        g_item_muted[tab][r_idx]    = g_item_muted[tab][i];
        g_item_color_idx[tab][r_idx]= g_item_color_idx[tab][i];
        g_item_group_id[tab][r_idx] = g_item_group_id[tab][i];
        p->num_items++;
    }
    return did_split;
}

// Executes the Reposition Items dialog action (adds interval_sec to Item start or Item end for selected items per track)
static void execute_reposition_items_dialog(ProjectTab *p) {
    if (!p) return;
    double iv = atof(g_reposition.interval_str);
    g_reposition.interval_sec = iv;
    save_themes_settings_json();

    bool any_sel = false;
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].is_selected) { any_sel = true; break; }
    }
    if (!any_sel) {
        g_reposition.is_open = false;
        return;
    }

    push_undo_state();
    for (int t = 0; t < p->num_tracks; t++) {
        // Collect selected item indices on track t sorted by start_time
        int trk_items[MAX_ITEMS];
        int cnt = 0;
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].is_selected && p->items[i].track_idx == t) {
                trk_items[cnt++] = i;
            }
        }
        for (int a = 0; a < cnt - 1; a++) {
            for (int b = a + 1; b < cnt; b++) {
                if (p->items[trk_items[b]].start_time < p->items[trk_items[a]].start_time) {
                    int tmp = trk_items[a];
                    trk_items[a] = trk_items[b];
                    trk_items[b] = tmp;
                }
            }
        }
        if (cnt > 1) {
            double ref_pos = p->items[trk_items[0]].start_time;
            double ref_end = ref_pos + p->items[trk_items[0]].duration;
            for (int k = 1; k < cnt; k++) {
                int idx = trk_items[k];
                double new_s = (g_reposition.mode == 0) ? (ref_pos + iv) : (ref_end + iv);
                if (new_s < 0.0) new_s = 0.0;
                p->items[idx].start_time = new_s;
                ref_pos = new_s;
                ref_end = new_s + p->items[idx].duration;
            }
        }
    }
    g_reposition.is_open = false;
    record_project_change("Repositioned Selected Items", true);
}

// Executes Stabilized Simple Box-Based Dynamic Split on Selected Clip Item(s) or Selected Track(s)
// Shows the "Processing, please wait..." modal during analysis & splitting and locks the audio device so it never crashes.
static void execute_dynamic_split_items(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win) {
    if (!p) return;
    save_themes_settings_json();

    float open_lin  = db_to_linear(g_dyn_split.gate_threshold_db);
    float close_lin = db_to_linear(g_dyn_split.gate_threshold_db + g_dyn_split.gate_hysteresis_db);
    if (close_lin > open_lin) close_lin = open_lin * 0.8f;

    double min_slice_sec   = fmax(0.010, (double)g_dyn_split.min_slice_ms / 1000.0);
    double min_silence_sec = fmax(0.020, (double)g_dyn_split.min_silence_ms / 1000.0);
    double lead_pad_sec    = fmax(0.0, (double)g_dyn_split.leading_pad_ms / 1000.0);
    double trail_pad_sec   = fmax(0.0, (double)g_dyn_split.trailing_pad_ms / 1000.0);
    double bin_dur_sec     = (double)PEAK_SAMPLES_PER_BIN / (double)SAMPLE_RATE;

    // Collect target item indices according to Action to perform:
    // 0 = Selected clip item, 1 = Selected Track
    int target_items[MAX_ITEMS];
    int num_targets = 0;

    if (g_dyn_split.action_mode == 1) {
        // Selected Track: select all clips on currently selected track(s)
        for (int i = 0; i < p->num_items; i++) {
            int tr = p->items[i].track_idx;
            if (tr >= 0 && tr < p->num_tracks && p->track_selected[tr] && !is_track_hidden(tr)) {
                p->items[i].is_selected = true;
                target_items[num_targets++] = i;
            }
        }
    } else {
        // Selected clip item (fallback to selected track's clips if no clip is explicitly selected)
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].is_selected && !is_track_hidden(p->items[i].track_idx)) {
                target_items[num_targets++] = i;
            }
        }
        if (num_targets == 0) {
            for (int i = 0; i < p->num_items; i++) {
                int tr = p->items[i].track_idx;
                if (tr >= 0 && tr < p->num_tracks && p->track_selected[tr] && !is_track_hidden(tr)) {
                    p->items[i].is_selected = true;
                    target_items[num_targets++] = i;
                }
            }
        }
    }

    if (num_targets == 0) {
        snprintf(g_dyn_split.status_msg, sizeof(g_dyn_split.status_msg),
                 (g_dyn_split.action_mode == 1) ? "Select a track with clips first." : "Select a clip item first.");
        return;
    }

    g_dyn_split.is_open = false;
    show_processing_wait_modal(ren, win, "Dynamic Split - Processing...", "Analyzing audio gate & silence thresholds...", 10);

    push_undo_state();
    int tab = get_valid_tab_idx();
    int total_created_slices = 0;

    typedef struct {
        double s;
        double e;
        bool is_voice;
    } DynSeg;

    // Process from highest index to lowest so replacing item `idx` never invalidates earlier target indices
    for (int ti = num_targets - 1; ti >= 0; ti--) {
        int idx = target_items[ti];
        if (idx < 0 || idx >= p->num_items) continue;
        AudioItem src_copy = p->items[idx];
        double clip_s = src_copy.start_time;
        double clip_e = src_copy.start_time + src_copy.duration;
        if (src_copy.duration <= min_slice_sec * 1.2) continue;

        int pct = 15 + ((num_targets - 1 - ti) * 75) / (num_targets > 0 ? num_targets : 1);
        show_processing_wait_modal(ren, win, "Dynamic Split - Processing...", src_copy.label, pct);

        DynSeg voice_segs[512];
        int num_voice = 0;

        if (src_copy.peaks && src_copy.peak_count > 2) {
            int start_bin = (int)((src_copy.offset_in_file * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN);
            int end_bin   = (int)(((src_copy.offset_in_file + src_copy.duration) * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN);
            if (start_bin < 0) start_bin = 0;
            if (end_bin > src_copy.peak_count) end_bin = src_copy.peak_count;

            int min_silence_bins = (int)(min_silence_sec / bin_dur_sec);
            if (min_silence_bins < 1) min_silence_bins = 1;

            bool in_voice = false;
            double cur_voice_s = clip_s;
            double last_loud_t = clip_s;
            int quiet_bins = 0;

            for (int b = start_bin; b < end_bin; b++) {
                float a1 = fabsf(src_copy.peaks[b].min_val);
                float a2 = fabsf(src_copy.peaks[b].max_val);
                float amp = (a1 > a2) ? a1 : a2;
                double t_cur = clip_s + (double)(b - start_bin) * bin_dur_sec;

                if (!in_voice) {
                    if (amp >= open_lin) {
                        in_voice = true;
                        cur_voice_s = fmax(clip_s, t_cur - lead_pad_sec);
                        last_loud_t = t_cur + bin_dur_sec;
                        quiet_bins = 0;
                    }
                } else {
                    if (amp >= close_lin) {
                        last_loud_t = t_cur + bin_dur_sec;
                        quiet_bins = 0;
                    } else {
                        quiet_bins++;
                        if (quiet_bins >= min_silence_bins) {
                            double seg_e = fmin(clip_e, last_loud_t + trail_pad_sec);
                            if (seg_e - cur_voice_s >= min_slice_sec && num_voice < 512) {
                                // Merge with previous voice segment if pads overlap
                                if (num_voice > 0 && cur_voice_s <= voice_segs[num_voice - 1].e + 0.005) {
                                    if (seg_e > voice_segs[num_voice - 1].e) {
                                        voice_segs[num_voice - 1].e = seg_e;
                                    }
                                } else {
                                    voice_segs[num_voice].s = cur_voice_s;
                                    voice_segs[num_voice].e = seg_e;
                                    voice_segs[num_voice].is_voice = true;
                                    num_voice++;
                                }
                            }
                            in_voice = false;
                            quiet_bins = 0;
                        }
                    }
                }
            }

            if (in_voice && num_voice < 512) {
                double seg_e = fmin(clip_e, last_loud_t + trail_pad_sec);
                if (seg_e > clip_e) seg_e = clip_e;
                if (seg_e - cur_voice_s >= min_slice_sec) {
                    if (num_voice > 0 && cur_voice_s <= voice_segs[num_voice - 1].e + 0.005) {
                        if (seg_e > voice_segs[num_voice - 1].e) voice_segs[num_voice - 1].e = seg_e;
                    } else {
                        voice_segs[num_voice].s = cur_voice_s;
                        voice_segs[num_voice].e = seg_e;
                        voice_segs[num_voice].is_voice = true;
                        num_voice++;
                    }
                }
            }
        }

        // Fallback if uniform synthetic signal had no silence gaps: split at midpoint so user still sees a clean split
        if (num_voice == 0 || (num_voice == 1 && fabs(voice_segs[0].s - clip_s) < 0.02 && fabs(voice_segs[0].e - clip_e) < 0.02)) {
            if (src_copy.duration >= min_slice_sec * 2.5) {
                double mid = clip_s + src_copy.duration * 0.5;
                voice_segs[0] = (DynSeg){ clip_s, mid, true };
                voice_segs[1] = (DynSeg){ mid, clip_e, true };
                num_voice = 2;
            } else {
                continue;
            }
        }

        // Build final output segments for this clip
        DynSeg final_segs[512];
        int num_final = 0;
        if (g_dyn_split.remove_silent_areas) {
            for (int v = 0; v < num_voice && num_final < 512; v++) {
                if (voice_segs[v].e - voice_segs[v].s >= min_slice_sec) {
                    final_segs[num_final++] = voice_segs[v];
                }
            }
        } else {
            // Keep both active and silent intervals as split clips
            double cur_p = clip_s;
            for (int v = 0; v < num_voice && num_final < 510; v++) {
                if (voice_segs[v].s > cur_p + min_slice_sec) {
                    final_segs[num_final++] = (DynSeg){ cur_p, voice_segs[v].s, false };
                }
                final_segs[num_final++] = voice_segs[v];
                cur_p = voice_segs[v].e;
            }
            if (clip_e > cur_p + min_slice_sec && num_final < 512) {
                final_segs[num_final++] = (DynSeg){ cur_p, clip_e, false };
            }
        }

        if (num_final == 0) continue;

        // Commit slices under audio device lock so audio thread never reads mid-mutation items
        g_engine_processing = true;
        if (g_daw.audio_device > 0) SDL_LockAudioDevice(g_daw.audio_device);

        float orig_vol   = g_item_vol_db[tab][idx];
        bool  orig_mute  = g_item_muted[tab][idx];
        int   orig_color = g_item_color_idx[tab][idx];
        int   orig_grp   = g_item_group_id[tab][idx];

        // Update first slice in-place at `idx`
        p->items[idx].start_time     = final_segs[0].s;
        p->items[idx].offset_in_file = src_copy.offset_in_file + (final_segs[0].s - clip_s);
        p->items[idx].duration       = final_segs[0].e - final_segs[0].s;
        p->items[idx].is_selected    = true;
        g_item_fade_in[tab][idx]     = fmin(0.010, p->items[idx].duration * 0.2);
        g_item_fade_out[tab][idx]    = fmin(0.010, p->items[idx].duration * 0.2);
        total_created_slices++;

        // Append remaining slices (1 .. num_final - 1)
        for (int s = 1; s < num_final && p->num_items < MAX_ITEMS; s++) {
            int ni = p->num_items++;
            AudioItem *dst = &p->items[ni];
            memcpy(dst, &src_copy, sizeof(AudioItem));
            dst->start_time     = final_segs[s].s;
            dst->offset_in_file = src_copy.offset_in_file + (final_segs[s].s - clip_s);
            dst->duration       = final_segs[s].e - final_segs[s].s;
            dst->is_selected    = true;
            dst->peaks          = NULL;
            if (src_copy.peak_count > 0 && src_copy.peaks) {
                dst->peaks = (PeakBin *)calloc((size_t)src_copy.peak_count, sizeof(PeakBin));
                if (dst->peaks) {
                    memcpy(dst->peaks, src_copy.peaks, (size_t)src_copy.peak_count * sizeof(PeakBin));
                }
            }
            dst->raw_file_handle = dst->raw_audio_path[0] ? fopen(dst->raw_audio_path, "rb") : NULL;
            g_item_fade_in[tab][ni]   = fmin(0.010, dst->duration * 0.2);
            g_item_fade_out[tab][ni]  = fmin(0.010, dst->duration * 0.2);
            g_item_vol_db[tab][ni]    = orig_vol;
            g_item_muted[tab][ni]     = orig_mute;
            g_item_color_idx[tab][ni] = orig_color;
            g_item_group_id[tab][ni]  = orig_grp;
            total_created_slices++;
        }

        if (g_daw.audio_device > 0) SDL_UnlockAudioDevice(g_daw.audio_device);
        g_engine_processing = false;
    }

    show_processing_wait_modal(ren, win, "Dynamic Split Complete", "Stabilized timeline clips.", 100);
    finish_processing_wait_modal(220);

    if (total_created_slices > 0) {
        record_project_change("Dynamic Split Items", true);
    }
}

// Helper to parse comma-separated tokens into an array of strings
static int split_csv_tokens(const char *csv, char out_tokens[][128], int max_tokens) {
    if (!csv || !out_tokens || max_tokens <= 0) return 0;
    int cnt = 0;
    const char *p = csv;
    while (*p && cnt < max_tokens) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        while (len > 0 && (*p == ' ' || *p == '\t')) { p++; len--; }
        if (len >= 128) len = 127;
        memcpy(out_tokens[cnt], p, len);
        out_tokens[cnt][len] = '\0';
        cnt++;
        if (!comma) break;
        p = comma + 1;
    }
    return cnt;
}

// Strips surrounding quotes from a function argument string
static void unquote_arg_string(const char *in, char *out, size_t max_len) {
    if (!in || !out || max_len == 0) return;
    const char *s = in;
    while (*s == ' ' || *s == '\t') s++;
    size_t l = strlen(s);
    while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\r' || s[l - 1] == '\n')) l--;
    if (l >= 2 && ((s[0] == '\"' && s[l - 1] == '\"') || (s[0] == '\'' && s[l - 1] == '\''))) {
        s++;
        l -= 2;
    }
    if (l >= max_len) l = max_len - 1;
    memcpy(out, s, l);
    out[l] = '\0';
}

// Scans a Lua (.lua) or Python (.py) script for popup window / dialog creation calls.
// Returns true and opens g_script_popup if the script requests a popup window before running.
static bool inspect_and_open_script_popup_if_needed(const char *filepath, bool is_python, SDL_Window *win) {
    if (!filepath || !filepath[0]) return false;
    FILE *f = fopen(filepath, "rb");
    if (!f) return false;

    bool wants_popup = false;
    memset(&g_script_popup, 0, sizeof(g_script_popup));
    g_script_popup.win_rect = (SDL_Rect){ 390, 130, 480, 320 };
    g_script_popup.focused_ctrl_idx = -1;
    g_script_popup.dragging_slider_idx = -1;
    g_script_popup.is_python = is_python;
    strncpy(g_script_popup.script_filepath, filepath, sizeof(g_script_popup.script_filepath) - 1);
    snprintf(g_script_popup.title, sizeof(g_script_popup.title), "%s Script Window", is_python ? "Python" : "Lua");

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (s[0] == '\0' || s[0] == '\r' || s[0] == '\n') continue;
        if (s[0] == '#' || (s[0] == '-' && s[1] == '-')) continue;

        // 1. REAPER GetUserInputs("Title", num_inputs, "Caption1,Caption2", "Def1,Def2") or RPR_GetUserInputs
        if (str_contains_nocase(s, "GetUserInputs")) {
            const char *fn_tok = str_contains_nocase(s, "RPR_GetUserInputs") ? "RPR_GetUserInputs" : "GetUserInputs";
            char a_title[128] = {0}, a_num[32] = {0}, a_caps[256] = {0}, a_defs[256] = {0};
            extract_func_call_arg(s, fn_tok, 0, a_title, sizeof(a_title));
            extract_func_call_arg(s, fn_tok, 1, a_num, sizeof(a_num));
            extract_func_call_arg(s, fn_tok, 2, a_caps, sizeof(a_caps));
            extract_func_call_arg(s, fn_tok, 3, a_defs, sizeof(a_defs));

            char clean_title[128] = {0}, clean_caps[256] = {0}, clean_defs[256] = {0};
            unquote_arg_string(a_title, clean_title, sizeof(clean_title));
            unquote_arg_string(a_caps, clean_caps, sizeof(clean_caps));
            unquote_arg_string(a_defs, clean_defs, sizeof(clean_defs));
            if (clean_title[0]) strncpy(g_script_popup.title, clean_title, sizeof(g_script_popup.title) - 1);

            char caps_arr[MAX_SCRIPT_POPUP_CONTROLS][128];
            char defs_arr[MAX_SCRIPT_POPUP_CONTROLS][128];
            memset(caps_arr, 0, sizeof(caps_arr));
            memset(defs_arr, 0, sizeof(defs_arr));
            int n_caps = split_csv_tokens(clean_caps, caps_arr, MAX_SCRIPT_POPUP_CONTROLS);
            int n_defs = split_csv_tokens(clean_defs, defs_arr, MAX_SCRIPT_POPUP_CONTROLS);
            int n_req = atoi(a_num);
            if (n_req <= 0 || n_req > MAX_SCRIPT_POPUP_CONTROLS) n_req = (n_caps > 0) ? n_caps : 1;

            for (int k = 0; k < n_req && g_script_popup.num_controls < MAX_SCRIPT_POPUP_CONTROLS; k++) {
                ScriptPopupControl *ctrl = &g_script_popup.controls[g_script_popup.num_controls++];
                ctrl->type = SCP_CTRL_INPUT;
                snprintf(ctrl->label, sizeof(ctrl->label), "%s", (k < n_caps && caps_arr[k][0]) ? caps_arr[k] : "Value:");
                snprintf(ctrl->var_name, sizeof(ctrl->var_name), "val%d", k + 1);
                snprintf(ctrl->str_val, sizeof(ctrl->str_val), "%s", (k < n_defs) ? defs_arr[k] : "0");
                ctrl->num_val = atof(ctrl->str_val);
            }
            wants_popup = true;
        }

        // 2. Custom Window creation: gfx.init("Title", w, h) / daw.create_window("Title", w, h) / create_popup_window("Title", w, h) / tk.Tk()
        if (str_contains_nocase(s, "gfx.init") || str_contains_nocase(s, "create_window") ||
            str_contains_nocase(s, "create_popup_window") || str_contains_nocase(s, "daw_window") ||
            str_contains_nocase(s, "tk.Tk(") || str_contains_nocase(s, "tkinter")) {
            const char *fn_tok = str_contains_nocase(s, "gfx.init") ? "gfx.init"
                               : (str_contains_nocase(s, "create_popup_window") ? "create_popup_window"
                               : (str_contains_nocase(s, "create_window") ? "create_window" : "daw_window"));
            char a_title[128] = {0}, a_w[32] = {0}, a_h[32] = {0};
            if (extract_func_call_arg(s, fn_tok, 0, a_title, sizeof(a_title))) {
                char clean_title[128] = {0};
                unquote_arg_string(a_title, clean_title, sizeof(clean_title));
                if (clean_title[0]) strncpy(g_script_popup.title, clean_title, sizeof(g_script_popup.title) - 1);
            }
            if (extract_func_call_arg(s, fn_tok, 1, a_w, sizeof(a_w))) {
                int w = atoi(a_w);
                if (w >= 300 && w <= 860) g_script_popup.win_rect.w = w;
            }
            if (extract_func_call_arg(s, fn_tok, 2, a_h, sizeof(a_h))) {
                int h = atoi(a_h);
                if (h >= 180 && h <= 640) g_script_popup.win_rect.h = h;
            }
            wants_popup = true;
        }

        // 3. Explicit Slider control in Lua/Python: add_slider("Label", min, max, default)
        if (str_contains_nocase(s, "add_slider") && g_script_popup.num_controls < MAX_SCRIPT_POPUP_CONTROLS) {
            char a_lbl[128] = {0}, a_min[32] = {0}, a_max[32] = {0}, a_def[32] = {0};
            extract_func_call_arg(s, "add_slider", 0, a_lbl, sizeof(a_lbl));
            extract_func_call_arg(s, "add_slider", 1, a_min, sizeof(a_min));
            extract_func_call_arg(s, "add_slider", 2, a_max, sizeof(a_max));
            extract_func_call_arg(s, "add_slider", 3, a_def, sizeof(a_def));
            ScriptPopupControl *ctrl = &g_script_popup.controls[g_script_popup.num_controls++];
            ctrl->type = SCP_CTRL_SLIDER;
            unquote_arg_string(a_lbl, ctrl->label, sizeof(ctrl->label));
            snprintf(ctrl->var_name, sizeof(ctrl->var_name), "val%d", g_script_popup.num_controls);
            ctrl->min_val = a_min[0] ? atof(a_min) : 0.0;
            ctrl->max_val = a_max[0] ? atof(a_max) : 100.0;
            if (ctrl->max_val <= ctrl->min_val) ctrl->max_val = ctrl->min_val + 100.0;
            ctrl->num_val = a_def[0] ? atof(a_def) : ctrl->min_val;
            snprintf(ctrl->str_val, sizeof(ctrl->str_val), "%.2f", ctrl->num_val);
            wants_popup = true;
        }

        // 4. Explicit Input box control in Lua/Python: add_input("Label", "default")
        if ((str_contains_nocase(s, "add_input") || str_contains_nocase(s, "input_dialog")) &&
            g_script_popup.num_controls < MAX_SCRIPT_POPUP_CONTROLS) {
            const char *tok = str_contains_nocase(s, "add_input") ? "add_input" : "input_dialog";
            char a_lbl[128] = {0}, a_def[128] = {0};
            extract_func_call_arg(s, tok, 0, a_lbl, sizeof(a_lbl));
            extract_func_call_arg(s, tok, 1, a_def, sizeof(a_def));
            ScriptPopupControl *ctrl = &g_script_popup.controls[g_script_popup.num_controls++];
            ctrl->type = SCP_CTRL_INPUT;
            unquote_arg_string(a_lbl, ctrl->label, sizeof(ctrl->label));
            unquote_arg_string(a_def, ctrl->str_val, sizeof(ctrl->str_val));
            if (!ctrl->str_val[0]) strcpy(ctrl->str_val, "0.0");
            ctrl->num_val = atof(ctrl->str_val);
            snprintf(ctrl->var_name, sizeof(ctrl->var_name), "val%d", g_script_popup.num_controls);
            wants_popup = true;
        }

        // 5. Explicit Checkbox control in Lua/Python: add_checkbox("Label", true/false)
        if (str_contains_nocase(s, "add_checkbox") && g_script_popup.num_controls < MAX_SCRIPT_POPUP_CONTROLS) {
            char a_lbl[128] = {0}, a_def[32] = {0};
            extract_func_call_arg(s, "add_checkbox", 0, a_lbl, sizeof(a_lbl));
            extract_func_call_arg(s, "add_checkbox", 1, a_def, sizeof(a_def));
            ScriptPopupControl *ctrl = &g_script_popup.controls[g_script_popup.num_controls++];
            ctrl->type = SCP_CTRL_CHECKBOX;
            unquote_arg_string(a_lbl, ctrl->label, sizeof(ctrl->label));
            ctrl->bool_val = str_contains_nocase(a_def, "true") || str_contains_nocase(a_def, "1");
            ctrl->num_val = ctrl->bool_val ? 1.0 : 0.0;
            snprintf(ctrl->var_name, sizeof(ctrl->var_name), "val%d", g_script_popup.num_controls);
            wants_popup = true;
        }

        // 6. ShowMessageBox("Message", "Title", type)
        if (str_contains_nocase(s, "ShowMessageBox") && g_script_popup.num_controls < MAX_SCRIPT_POPUP_CONTROLS) {
            char a_msg[256] = {0}, a_title[128] = {0};
            extract_func_call_arg(s, "ShowMessageBox", 0, a_msg, sizeof(a_msg));
            extract_func_call_arg(s, "ShowMessageBox", 1, a_title, sizeof(a_title));
            char clean_title[128] = {0};
            unquote_arg_string(a_title, clean_title, sizeof(clean_title));
            if (clean_title[0]) strncpy(g_script_popup.title, clean_title, sizeof(g_script_popup.title) - 1);
            ScriptPopupControl *ctrl = &g_script_popup.controls[g_script_popup.num_controls++];
            ctrl->type = SCP_CTRL_LABEL;
            unquote_arg_string(a_msg, ctrl->str_val, sizeof(ctrl->str_val));
            snprintf(ctrl->label, sizeof(ctrl->label), "Message:");
            wants_popup = true;
        }
    }
    fclose(f);

    if (!wants_popup) return false;

    // Ensure at least one interactive parameter control if gfx.init/create_window was called without explicit add_*
    if (g_script_popup.num_controls == 0) {
        ScriptPopupControl *ctrl = &g_script_popup.controls[g_script_popup.num_controls++];
        ctrl->type = SCP_CTRL_INPUT;
        snprintf(ctrl->label, sizeof(ctrl->label), "Parameter Value:");
        snprintf(ctrl->var_name, sizeof(ctrl->var_name), "val1");
        snprintf(ctrl->str_val, sizeof(ctrl->str_val), "1.00");
        ctrl->num_val = 1.0;
    }

    int needed_h = 110 + g_script_popup.num_controls * 42;
    if (needed_h > g_script_popup.win_rect.h) g_script_popup.win_rect.h = needed_h;
    if (g_script_popup.win_rect.h > 540) g_script_popup.win_rect.h = 540;

    int win_w = 1280, win_h = 720;
    if (win) SDL_GetWindowSize(win, &win_w, &win_h);
    g_script_popup.win_rect.x = (win_w - g_script_popup.win_rect.w) / 2;
    g_script_popup.win_rect.y = (win_h - g_script_popup.win_rect.h) / 2;
    g_script_popup.focused_ctrl_idx = (g_script_popup.controls[0].type == SCP_CTRL_INPUT) ? 0 : -1;
    if (g_script_popup.focused_ctrl_idx >= 0) SDL_StartTextInput();
    g_script_popup.is_open = true;
    return true;
}

// Built-in C Lua & Python Script Interpreter:
// Reads a manually created .lua or .py script line-by-line and executes timeline/track/clip/transport commands!
static bool interpret_custom_script_file(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win,
                                         const char *filepath, char *out_custom_msg, size_t msg_len) {
    if (!p || !filepath || !filepath[0]) return false;
    FILE *f = fopen(filepath, "rb");
    if (!f) return false;

    int tab = get_valid_tab_idx();
    bool did_any_action = false;
    bool pushed_undo = false;
    char line[1024];
    char g_recorded_var_names[16][48];
    char g_recorded_var_exprs[16][256];
    int  g_num_recorded_assigns = 0;
    memset(g_recorded_var_names, 0, sizeof(g_recorded_var_names));
    memset(g_recorded_var_exprs, 0, sizeof(g_recorded_var_exprs));

    while (fgets(line, sizeof(line), f)) {
        // Trim leading whitespace
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        // Skip pure comment lines in Lua (--) or Python (#)
        if (s[0] == '\0' || s[0] == '\r' || s[0] == '\n') continue;
        if (s[0] == '#' || (s[0] == '-' && s[1] == '-')) continue;

        // 1. Capture ShowConsoleMsg("...") or print("...") message for user feedback
        if (str_contains_nocase(s, "ShowConsoleMsg") || str_contains_nocase(s, "print(") ||
            str_contains_nocase(s, "ShowMessageBox")) {
            const char *q1 = strchr(s, '\"');
            if (!q1) q1 = strchr(s, '\'');
            if (q1) {
                char qchar = *q1;
                const char *q2 = strchr(q1 + 1, qchar);
                if (q2 && out_custom_msg && msg_len > 0) {
                    size_t clen = (size_t)(q2 - (q1 + 1));
                    if (clen >= msg_len) clen = msg_len - 1;
                    memcpy(out_custom_msg, q1 + 1, clen);
                    out_custom_msg[clen] = '\0';
                }
            }
        }

        // 2. REAPER Main_OnCommand(cmd_id, 0) support
        if (str_contains_nocase(s, "Main_OnCommand")) {
            double cmd_d = 0.0;
            if (parse_number_after_token(s, "Main_OnCommand", &cmd_d)) {
                int cmd_id = (int)cmd_d;
                if (cmd_id == 40044 || cmd_id == 1007 || cmd_id == 1016) {
                    p->is_playing = !p->is_playing;
                    did_any_action = true;
                } else if (cmd_id == 40012 || cmd_id == 40757 || cmd_id == 40061) {
                    // Split items at edit/play cursor
                    for (int a = 0; a < g_num_actions; a++) {
                        if (g_actions[a].id == ACT_ITEM_SPLIT_AT_CURSOR) {
                            execute_daw_action_by_index(p, ren, win, a);
                            did_any_action = true;
                            break;
                        }
                    }
                } else if (cmd_id == 40001) {
                    // Insert new track
                    if (p->num_tracks < MAX_TRACKS) {
                        if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
                        int nt = p->num_tracks++;
                        p->track_heights[nt] = 84;
                        p->tracks[nt].id = nt + 1;
                        p->tracks[nt].volume = 1.0f;
                        snprintf(p->track_names[nt], 64, "Track %d", nt + 1);
                        did_any_action = true;
                    }
                } else if (cmd_id == 40005) {
                    // Remove selected tracks
                    if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
                    for (int t = p->num_tracks - 1; t >= 0; t--) {
                        if (p->track_selected[t] && p->num_tracks > 1) delete_track_at_index(p, t);
                    }
                    did_any_action = true;
                } else if (cmd_id == 40006 || cmd_id == 40697) {
                    // Remove items/tracks
                    for (int a = 0; a < g_num_actions; a++) {
                        if (g_actions[a].id == ACT_EDIT_REMOVE_FOCUS) {
                            execute_daw_action_by_index(p, ren, win, a);
                            did_any_action = true;
                            break;
                        }
                    }
                } else if (cmd_id == 40029) {
                    perform_undo();
                    did_any_action = true;
                } else if (cmd_id == 40030) {
                    perform_redo();
                    did_any_action = true;
                } else if (cmd_id == 40182) {
                    for (int i = 0; i < p->num_items; i++) p->items[i].is_selected = true;
                    did_any_action = true;
                } else if (cmd_id == 40289) {
                    for (int i = 0; i < p->num_items; i++) p->items[i].is_selected = false;
                    did_any_action = true;
                } else if (cmd_id == 40296) {
                    for (int t = 0; t < p->num_tracks; t++) p->track_selected[t] = true;
                    did_any_action = true;
                } else if (cmd_id == 40297) {
                    for (int t = 0; t < p->num_tracks; t++) p->track_selected[t] = false;
                    did_any_action = true;
                } else if (cmd_id == 40108) {
                    for (int a = 0; a < g_num_actions; a++) {
                        if (g_actions[a].id == ACT_SCRIPT_AUTO_GAIN) {
                            execute_daw_action_by_index(p, ren, win, a);
                            did_any_action = true;
                            break;
                        }
                    }
                } else if (cmd_id == 40032 || cmd_id == 40033) {
                    for (int a = 0; a < g_num_actions; a++) {
                        if (g_actions[a].id == ACT_SCRIPT_GROUP_AND_UNGROUP) {
                            execute_daw_action_by_index(p, ren, win, a);
                            did_any_action = true;
                            break;
                        }
                    }
                }
            }
        }

        // 3. MoveMediaItemToTrack / move_clip_to_selected_track / move_to_selected_track
        if (str_contains_nocase(s, "MoveMediaItemToTrack") ||
            str_contains_nocase(s, "move_clip_to_selected_track") ||
            str_contains_nocase(s, "move_to_selected_track") ||
            str_contains_nocase(s, "clip_move_to_selected_track")) {
            int target_trk = -1;
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t] && !is_track_hidden(t)) { target_trk = t; break; }
            }
            if (target_trk >= 0) {
                if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
                for (int i = 0; i < p->num_items; i++) {
                    if (p->items[i].is_selected && p->items[i].track_idx != target_trk) {
                        p->items[i].track_idx = target_trk;
                        if (!is_crossfade_active()) {
                            p->items[i].start_time = clamp_non_overlapping_start(
                                p, i, target_trk, p->items[i].start_time, p->items[i].duration);
                        }
                    }
                }
                did_any_action = true;
            }
        }

        // 4. Track reordering commands in script (move_tracks_to_top, move_tracks_to_bottom, move_tracks_up, move_tracks_down)
        if (str_contains_nocase(s, "move_tracks_to_bottom") || str_contains_nocase(s, "tracks_move_to_bottom")) {
            if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
            for (int pass = 0; pass < p->num_tracks; pass++) {
                for (int t = p->num_tracks - 2; t >= 0; t--) {
                    if (p->track_selected[t] && !p->track_selected[t + 1]) swap_tracks(p, t, t + 1);
                }
            }
            did_any_action = true;
        }
        if (str_contains_nocase(s, "move_tracks_to_top") || str_contains_nocase(s, "tracks_move_to_top")) {
            if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
            for (int pass = 0; pass < p->num_tracks; pass++) {
                for (int t = 1; t < p->num_tracks; t++) {
                    if (p->track_selected[t] && !p->track_selected[t - 1]) swap_tracks(p, t, t - 1);
                }
            }
            did_any_action = true;
        }
        if (str_contains_nocase(s, "move_track_up") || str_contains_nocase(s, "track_move_up")) {
            if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
            for (int t = 1; t < p->num_tracks; t++) {
                if (p->track_selected[t] && !p->track_selected[t - 1]) swap_tracks(p, t, t - 1);
            }
            did_any_action = true;
        }
        if (str_contains_nocase(s, "move_track_down") || str_contains_nocase(s, "track_move_down")) {
            if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
            for (int t = p->num_tracks - 2; t >= 0; t--) {
                if (p->track_selected[t] && !p->track_selected[t + 1]) swap_tracks(p, t, t + 1);
            }
            did_any_action = true;
        }

        // 5. InsertTrackAtIndex / insert_track / add_track
        if (str_contains_nocase(s, "InsertTrackAtIndex") ||
            str_contains_nocase(s, "insert_track") ||
            str_contains_nocase(s, "add_track")) {
            if (p->num_tracks < MAX_TRACKS) {
                if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
                int nt = p->num_tracks++;
                p->track_heights[nt] = 84;
                p->tracks[nt].id = nt + 1;
                p->tracks[nt].volume = 1.0f;
                snprintf(p->track_names[nt], 64, "Track %d", nt + 1);
                did_any_action = true;
            }
        }

        // 6. DeleteTrack / delete_track / remove_track
        if (str_contains_nocase(s, "DeleteTrack(") ||
            str_contains_nocase(s, "delete_track") ||
            str_contains_nocase(s, "remove_track")) {
            if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
            for (int t = p->num_tracks - 1; t >= 0; t--) {
                if (p->track_selected[t] && p->num_tracks > 1) delete_track_at_index(p, t);
            }
            did_any_action = true;
        }

        // 6B. Track custom local variable assignments in Lua/Python (e.g. `local split_pos = pos + len * 0.5`)
        if (!str_contains_nocase(s, "GetMediaItem") && !str_contains_nocase(s, "CountSelected") &&
            !str_contains_nocase(s, "function ") && strchr(s, '=') != NULL &&
            !str_contains_nocase(s, "==") && !str_contains_nocase(s, "~=") && !str_contains_nocase(s, "!=")) {
            const char *lhs = s;
            if (strncmp(lhs, "local ", 6) == 0) {
                lhs += 6;
                while (*lhs == ' ' || *lhs == '	') lhs++;
            }
            const char *eq = strchr(lhs, '=');
            if (eq && eq > lhs) {
                char var_name[48] = {0};
                size_t vl = (size_t)(eq - lhs);
                while (vl > 0 && (lhs[vl - 1] == ' ' || lhs[vl - 1] == '	')) vl--;
                if (vl > 0 && vl < sizeof(var_name)) {
                    memcpy(var_name, lhs, vl);
                    var_name[vl] = '\0';
                    bool valid_id = isalpha((unsigned char)var_name[0]) || var_name[0] == '_';
                    for (size_t k = 1; k < vl && valid_id; k++) {
                        if (!isalnum((unsigned char)var_name[k]) && var_name[k] != '_') valid_id = false;
                    }
                    if (valid_id && g_num_recorded_assigns < 16) {
                        strncpy(g_recorded_var_names[g_num_recorded_assigns], var_name, 47);
                        strncpy(g_recorded_var_exprs[g_num_recorded_assigns], eq + 1, 255);
                        g_num_recorded_assigns++;
                    }
                }
            }
        }

        // 7. Dynamic SplitMediaItem(item, <any math/var expression>) or split helpers
        if (str_contains_nocase(s, "split_selected_clips_center") ||
            str_contains_nocase(s, "split_clips_center") ||
            str_contains_nocase(s, "split_center")) {
            if (split_selected_items_at_center(p)) {
                did_any_action = true;
            }
        } else if (str_contains_nocase(s, "SplitMediaItem")) {
            char expr_arg[256] = {0};
            if (extract_func_call_arg(s, "SplitMediaItem", 1, expr_arg, sizeof(expr_arg)) && expr_arg[0]) {
                int orig_cnt = p->num_items;
                int sel_tot = 0;
                for (int i = 0; i < orig_cnt; i++) if (p->items[i].is_selected) sel_tot++;
                int sel_ord = 0;
                for (int i = 0; i < orig_cnt; i++) {
                    if (!p->items[i].is_selected) continue;
                    ScriptEvalContext ctx;
                    memset(&ctx, 0, sizeof(ctx));
                    populate_clip_eval_context(&ctx, p, i, sel_ord, sel_tot);
                    for (int va = 0; va < g_num_recorded_assigns; va++) {
                        double vval = eval_script_math_expr(g_recorded_var_exprs[va], &ctx);
                        script_ctx_set_var(&ctx, g_recorded_var_names[va], vval);
                    }
                    double split_t = eval_script_math_expr(expr_arg, &ctx);
                    if (split_single_item_at_time(p, i, split_t, &pushed_undo)) {
                        did_any_action = true;
                    }
                    sel_ord++;
                }
            } else if (split_selected_items_at_center(p)) {
                did_any_action = true;
            }
        } else if (str_contains_nocase(s, "split_item") || str_contains_nocase(s, "split_at_cursor")) {
            for (int a = 0; a < g_num_actions; a++) {
                if (g_actions[a].id == ACT_ITEM_SPLIT_AT_CURSOR) {
                    execute_daw_action_by_index(p, ren, win, a);
                    did_any_action = true;
                    break;
                }
            }
        }

        // 8. DeleteTrackMediaItem / delete_item / remove_item
        if (str_contains_nocase(s, "DeleteTrackMediaItem") ||
            str_contains_nocase(s, "delete_item") ||
            str_contains_nocase(s, "remove_item")) {
            if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
            for (int i = p->num_items - 1; i >= 0; i--) {
                if (p->items[i].is_selected) delete_item_at_index(p, i);
            }
            did_any_action = true;
        }

        // 9. Dynamic SetMediaItemInfo_Value(item, "PROP", <any formula/var>) or direct item property helpers
        if (str_contains_nocase(s, "SetMediaItemInfo_Value")) {
            char prop_arg[64] = {0};
            char val_expr[256] = {0};
            if (extract_func_call_arg(s, "SetMediaItemInfo_Value", 1, prop_arg, sizeof(prop_arg)) &&
                extract_func_call_arg(s, "SetMediaItemInfo_Value", 2, val_expr, sizeof(val_expr)) &&
                val_expr[0]) {
                if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
                int sel_tot = 0;
                for (int i = 0; i < p->num_items; i++) if (p->items[i].is_selected) sel_tot++;
                int sel_ord = 0;
                for (int i = 0; i < p->num_items; i++) {
                    if (!p->items[i].is_selected) continue;
                    ScriptEvalContext ctx;
                    memset(&ctx, 0, sizeof(ctx));
                    populate_clip_eval_context(&ctx, p, i, sel_ord, sel_tot);
                    for (int va = 0; va < g_num_recorded_assigns; va++) {
                        double vval = eval_script_math_expr(g_recorded_var_exprs[va], &ctx);
                        script_ctx_set_var(&ctx, g_recorded_var_names[va], vval);
                    }
                    double val = eval_script_math_expr(val_expr, &ctx);
                    if (str_contains_nocase(prop_arg, "D_POSITION")) {
                        p->items[i].start_time = fmax(0.0, val);
                        did_any_action = true;
                    } else if (str_contains_nocase(prop_arg, "D_LENGTH")) {
                        if (val > 0.05) p->items[i].duration = val;
                        did_any_action = true;
                    } else if (str_contains_nocase(prop_arg, "D_STARTOFFS")) {
                        p->items[i].offset_in_file = fmax(0.0, val);
                        did_any_action = true;
                    } else if (str_contains_nocase(prop_arg, "D_FADEINLEN")) {
                        set_item_fade_in(p, i, fmax(0.0, val));
                        did_any_action = true;
                    } else if (str_contains_nocase(prop_arg, "D_FADEOUTLEN")) {
                        set_item_fade_out(p, i, fmax(0.0, val));
                        did_any_action = true;
                    } else if (str_contains_nocase(prop_arg, "D_VOL")) {
                        g_item_vol_db[tab][i] = (float)val;
                        did_any_action = true;
                    } else if (str_contains_nocase(prop_arg, "B_MUTE")) {
                        g_item_muted[tab][i] = (val != 0.0);
                        did_any_action = true;
                    } else if (str_contains_nocase(prop_arg, "I_CUSTOMCOLOR")) {
                        g_item_color_idx[tab][i] = ((int)val) & 7;
                        did_any_action = true;
                    }
                    sel_ord++;
                }
            }
        }
        if (str_contains_nocase(s, "set_item_position")) {
            double pos = 0.0;
            if (parse_number_after_token(s, "set_item_position", &pos) && pos >= 0.0) {
                if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
                for (int i = 0; i < p->num_items; i++) {
                    if (p->items[i].is_selected) p->items[i].start_time = pos;
                }
                did_any_action = true;
            }
        }
        if (str_contains_nocase(s, "set_item_length")) {
            double dur = 0.0;
            if (parse_number_after_token(s, "set_item_length", &dur) && dur > 0.05) {
                if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
                for (int i = 0; i < p->num_items; i++) {
                    if (p->items[i].is_selected) p->items[i].duration = dur;
                }
                did_any_action = true;
            }
        }
        if (str_contains_nocase(s, "set_fade_in")) {
            double fin = 0.0;
            if (parse_number_after_token(s, "set_fade_in", &fin) && fin >= 0.0) {
                if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
                for (int i = 0; i < p->num_items; i++) {
                    if (p->items[i].is_selected) set_item_fade_in(p, i, fin);
                }
                did_any_action = true;
            }
        }
        if (str_contains_nocase(s, "set_fade_out")) {
            double fout = 0.0;
            if (parse_number_after_token(s, "set_fade_out", &fout) && fout >= 0.0) {
                if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
                for (int i = 0; i < p->num_items; i++) {
                    if (p->items[i].is_selected) set_item_fade_out(p, i, fout);
                }
                did_any_action = true;
            }
        }
        if (str_contains_nocase(s, "nudge_volume") || str_contains_nocase(s, "set_item_volume_db") ||
            str_contains_nocase(s, "gain_db")) {
            double db_v = 0.0;
            const char *tok = str_contains_nocase(s, "nudge_volume") ? "nudge_volume"
                            : (str_contains_nocase(s, "set_item_volume_db") ? "set_item_volume_db" : "gain_db");
            if (parse_number_after_token(s, tok, &db_v)) {
                if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
                bool is_set = str_contains_nocase(s, "set_item_volume_db");
                for (int i = 0; i < p->num_items; i++) {
                    if (p->items[i].is_selected) {
                        g_item_vol_db[tab][i] = is_set ? (float)db_v : (g_item_vol_db[tab][i] + (float)db_v);
                    }
                }
                did_any_action = true;
            }
        }

        // 10. Auto-gain / Normalization / Group / Align / Reposition / Color / Mute / Solo
        if (str_contains_nocase(s, "auto_gain") || str_contains_nocase(s, "normalize_items") ||
            str_contains_nocase(s, "normalize(")) {
            for (int a = 0; a < g_num_actions; a++) {
                if (g_actions[a].id == ACT_SCRIPT_AUTO_GAIN) {
                    execute_daw_action_by_index(p, ren, win, a);
                    did_any_action = true;
                    break;
                }
            }
        }
        if (str_contains_nocase(s, "group_and_ungroup") || str_contains_nocase(s, "group_items") ||
            str_contains_nocase(s, "ungroup_items")) {
            for (int a = 0; a < g_num_actions; a++) {
                if (g_actions[a].id == ACT_SCRIPT_GROUP_AND_UNGROUP) {
                    execute_daw_action_by_index(p, ren, win, a);
                    did_any_action = true;
                    break;
                }
            }
        }
        if (str_contains_nocase(s, "reposition_items") || str_contains_nocase(s, "reposition(")) {
            for (int a = 0; a < g_num_actions; a++) {
                if (g_actions[a].id == ACT_SWS_REPOSITION_ITEMS) {
                    execute_daw_action_by_index(p, ren, win, a);
                    did_any_action = true;
                    break;
                }
            }
        }
        if (str_contains_nocase(s, "auto_align") || str_contains_nocase(s, "align_items") ||
            str_contains_nocase(s, "time_match")) {
            double ref_start = -1.0;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) { ref_start = p->items[i].start_time; break; }
            }
            if (ref_start >= 0.0) {
                if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
                for (int i = 0; i < p->num_items; i++) {
                    if (p->items[i].is_selected) p->items[i].start_time = ref_start;
                }
                did_any_action = true;
            }
        }
        if (str_contains_nocase(s, "I_CUSTOMCOLOR") || str_contains_nocase(s, "set_custom_color") ||
            str_contains_nocase(s, "color_items")) {
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) {
                    g_item_color_idx[tab][i] = (g_item_color_idx[tab][i] + 1) % 8;
                }
            }
            did_any_action = true;
        }
        if (str_contains_nocase(s, "toggle_mute") || str_contains_nocase(s, "B_MUTE")) {
            for (int a = 0; a < g_num_actions; a++) {
                if (g_actions[a].id == ACT_ITEM_TOGGLE_MUTE_FOCUS) {
                    execute_daw_action_by_index(p, ren, win, a);
                    did_any_action = true;
                    break;
                }
            }
        }
        if (str_contains_nocase(s, "toggle_solo") || str_contains_nocase(s, "I_SOLO")) {
            for (int a = 0; a < g_num_actions; a++) {
                if (g_actions[a].id == ACT_TRACK_TOGGLE_SOLO_SELECTED) {
                    execute_daw_action_by_index(p, ren, win, a);
                    did_any_action = true;
                    break;
                }
            }
        }

        // 11. SetEditCurPos / set_cursor_pos / AddProjectMarker / add_marker / GetSet_LoopTimeRange
        if (str_contains_nocase(s, "SetEditCurPos") || str_contains_nocase(s, "set_cursor_pos")) {
            double cpos = 0.0;
            const char *tok = str_contains_nocase(s, "SetEditCurPos") ? "SetEditCurPos" : "set_cursor_pos";
            if (parse_number_after_token(s, tok, &cpos) && cpos >= 0.0) {
                p->playhead_seconds = cpos;
                did_any_action = true;
            }
        }
        if (str_contains_nocase(s, "AddProjectMarker") || str_contains_nocase(s, "add_marker")) {
            p->show_marker_lane = true;
            if (p->num_markers < MAX_MARKERS) {
                if (!pushed_undo) { push_undo_state(); pushed_undo = true; }
                int new_idx = p->num_markers++;
                p->markers[new_idx].time_sec = p->playhead_seconds;
                char def_txt[64];
                snprintf(def_txt, sizeof(def_txt), "Marker %d", p->num_markers);
                const char *q1 = strchr(s, '\"');
                if (q1) {
                    const char *q2 = strchr(q1 + 1, '\"');
                    if (q2 && q2 > q1 + 1) {
                        size_t ml = (size_t)(q2 - (q1 + 1));
                        if (ml >= sizeof(def_txt)) ml = sizeof(def_txt) - 1;
                        memcpy(def_txt, q1 + 1, ml);
                        def_txt[ml] = '\0';
                    }
                }
                set_marker_full_str(p, new_idx, def_txt);
                g_selected_marker_idx = new_idx;
                did_any_action = true;
            }
        }
        if (str_contains_nocase(s, "export_csv") || str_contains_nocase(s, "project_timeline.csv")) {
            FILE *csv = fopen("project_timeline.csv", "w");
            if (csv) {
                fprintf(csv, "ItemIndex,TrackNumber,TrackName,ClipName,StartTimeSec,DurationSec,EndTimeSec,FilePath\n");
                for (int i = 0; i < p->num_items; i++) {
                    AudioItem *it = &p->items[i];
                    const char *tname = (it->track_idx >= 0 && it->track_idx < p->num_tracks) ? p->track_names[it->track_idx] : "Track";
                    fprintf(csv, "%d,%d,\"%s\",\"%s\",%.3f,%.3f,%.3f,\"%s\"\n",
                            i + 1, it->track_idx + 1, tname, it->label,
                            it->start_time, it->duration, it->start_time + it->duration, it->file_path);
                }
                fclose(csv);
            }
            did_any_action = true;
        }
    }

    fclose(f);
    return did_any_action;
}

// Executes a Lua or Python script action (runs custom script file via built-in interpreter + native C timeline actions)
static void execute_script_action_entry(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win, DawActionEntry *act) {
    if (!p || !act) return;
    int tab = get_valid_tab_idx();

    // 1. Resolve and interpret the external .lua or .py file on disk if it exists
    char resolved_path[1024] = {0};
    char custom_msg[160] = {0};
    if (resolve_script_filepath(act, resolved_path, sizeof(resolved_path))) {
        if (inspect_and_open_script_popup_if_needed(resolved_path, act->is_python, win)) {
            snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                     "Opened script popup window: %s", g_script_popup.title);
            return;
        }
        if (interpret_custom_script_file(p, ren, win, resolved_path, custom_msg, sizeof(custom_msg))) {
            const char *msg_to_show = custom_msg[0] ? custom_msg : act->description;
            record_project_change(msg_to_show, true);
            snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                     "Executed %s", msg_to_show);
            return;
        }
    }

    const char *desc = act->description;

    // 2. Built-in & Name-Matched Script Actions:
    // Script: Clip move to selected track.lua (Alt+S)
    if (act->id == ACT_SCRIPT_CLIP_MOVE_TO_SELECTED_TRACK ||
        str_contains_nocase(desc, "Clip move to selected track") ||
        str_contains_nocase(desc, "move_to_selected_track")) {
        int target_trk = -1;
        for (int t = 0; t < p->num_tracks; t++) {
            if (p->track_selected[t] && !is_track_hidden(t)) {
                target_trk = t;
                break;
            }
        }
        if (target_trk >= 0) {
            bool any_moved = false;
            push_undo_state();
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected && p->items[i].track_idx != target_trk) {
                    p->items[i].track_idx = target_trk;
                    if (!is_crossfade_active()) {
                        p->items[i].start_time = clamp_non_overlapping_start(
                            p, i, target_trk, p->items[i].start_time, p->items[i].duration);
                    }
                    any_moved = true;
                }
            }
            if (any_moved) {
                record_project_change("Script: Clip move to selected track.lua", true);
                snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                         "Moved selected clip(s) to Track %d (%s)", target_trk + 1, p->track_names[target_trk]);
            }
        }
        return;
    }

    // Script: Tracks move to bottom.lua (Alt+B)
    if (act->id == ACT_SCRIPT_TRACKS_MOVE_TO_BOTTOM ||
        str_contains_nocase(desc, "Tracks move to bottom") ||
        str_contains_nocase(desc, "move_to_bottom")) {
        push_undo_state();
        for (int pass = 0; pass < p->num_tracks; pass++) {
            for (int t = p->num_tracks - 2; t >= 0; t--) {
                if (p->track_selected[t] && !p->track_selected[t + 1]) {
                    swap_tracks(p, t, t + 1);
                }
            }
        }
        record_project_change("Script: Tracks move to bottom.lua", true);
        snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                 "Executed: %s", desc);
        return;
    }

    // Script: Tracks Move to Top.lua (Alt+T)
    if (act->id == ACT_SCRIPT_TRACKS_MOVE_TO_TOP ||
        str_contains_nocase(desc, "Tracks Move to Top") ||
        str_contains_nocase(desc, "move_to_top")) {
        push_undo_state();
        for (int pass = 0; pass < p->num_tracks; pass++) {
            for (int t = 1; t < p->num_tracks; t++) {
                if (p->track_selected[t] && !p->track_selected[t - 1]) {
                    swap_tracks(p, t, t - 1);
                }
            }
        }
        record_project_change("Script: Tracks Move to Top.lua", true);
        snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                 "Executed: %s", desc);
        return;
    }

    // Script: Group and Ungroup.lua (Ctrl+Alt+Shift+G)
    if (act->id == ACT_SCRIPT_GROUP_AND_UNGROUP ||
        str_contains_nocase(desc, "Group and Ungroup") ||
        str_contains_nocase(desc, "group")) {
        int sel_cnt = 0;
        int first_grp = -1;
        bool all_same_nonzero_grp = true;
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].is_selected) {
                sel_cnt++;
                int g = g_item_group_id[tab][i];
                if (first_grp == -1) first_grp = g;
                if (g <= 0 || g != first_grp) all_same_nonzero_grp = false;
            }
        }
        if (sel_cnt > 0) {
            if (all_same_nonzero_grp) {
                for (int i = 0; i < p->num_items; i++) {
                    if (p->items[i].is_selected || g_item_group_id[tab][i] == first_grp) {
                        g_item_group_id[tab][i] = 0;
                    }
                }
                record_project_change("Script: Ungrouped Selected Items", true);
                snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                         "Ungrouped %d selected item(s)", sel_cnt);
            } else {
                int new_grp = g_next_item_group_id++;
                for (int i = 0; i < p->num_items; i++) {
                    if (p->items[i].is_selected) {
                        g_item_group_id[tab][i] = new_grp;
                    }
                }
                record_project_change("Script: Grouped Selected Items", true);
                snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                         "Grouped %d selected item(s) [Group #%d]", sel_cnt, new_grp);
            }
        }
        return;
    }

    // Script: Auto gain / normalize
    if (act->id == ACT_SCRIPT_AUTO_GAIN ||
        str_contains_nocase(desc, "Auto gain") ||
        str_contains_nocase(desc, "normalize") ||
        str_contains_nocase(desc, "gain")) {
        push_undo_state();
        for (int i = 0; i < p->num_items; i++) {
            if (!p->items[i].is_selected) continue;
            AudioItem *it = &p->items[i];
            float max_pk = 0.0f;
            if (it->peaks && it->peak_count > 0) {
                for (int b = 0; b < it->peak_count; b++) {
                    float a1 = fabsf(it->peaks[b].min_val);
                    float a2 = fabsf(it->peaks[b].max_val);
                    if (a1 > max_pk) max_pk = a1;
                    if (a2 > max_pk) max_pk = a2;
                }
            }
            if (max_pk > 0.001f) {
                float target_db = -1.0f - (20.0f * log10f(max_pk));
                if (target_db > 18.0f) target_db = 18.0f;
                if (target_db < -24.0f) target_db = -24.0f;
                g_item_vol_db[tab][i] = target_db;
            }
        }
        record_project_change(desc, true);
        snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                 "Executed: %s", desc);
        return;
    }

    // Script: Auto align / Peak align / Time matching
    if (act->id == ACT_SCRIPT_AUTO_ALIGN || act->id == ACT_SCRIPT_AUTO_ALIGN_2 ||
        act->id == ACT_SCRIPT_AUTO_WAVES_ALIGN || act->id == ACT_SCRIPT_PEAK_ALIGN ||
        act->id == ACT_SCRIPT_TIME_MATCH_CLIP || act->id == ACT_SCRIPT_TIME_MATCH_BOTTOM_CLIP ||
        act->id == ACT_SCRIPT_TIME_MATCH_3_TRACK || act->id == ACT_SCRIPT_TESTING_TIME_MATCH_3 ||
        str_contains_nocase(desc, "align") || str_contains_nocase(desc, "Time match")) {
        double ref_start = -1.0;
        double ref_dur = -1.0;
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].is_selected) {
                ref_start = p->items[i].start_time;
                ref_dur = p->items[i].duration;
                break;
            }
        }
        if (ref_start >= 0.0) {
            push_undo_state();
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) {
                    p->items[i].start_time = ref_start;
                    if (str_contains_nocase(desc, "Time match") && ref_dur > 0.05) {
                        if (p->items[i].file_total_duration <= 0.0 ||
                            p->items[i].offset_in_file + ref_dur <= p->items[i].file_total_duration) {
                            p->items[i].duration = ref_dur;
                        }
                    }
                }
            }
            record_project_change(desc, true);
            snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                     "Executed: %s", desc);
        }
        return;
    }

    // Script: Reposition item.lua
    if (act->id == ACT_SCRIPT_REPOSITION_ITEM || str_contains_nocase(desc, "Reposition")) {
        push_undo_state();
        for (int t = 0; t < p->num_tracks; t++) {
            double cur_t = -1.0;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected && p->items[i].track_idx == t) {
                    if (cur_t < 0.0) {
                        cur_t = p->items[i].start_time + p->items[i].duration;
                    } else {
                        p->items[i].start_time = cur_t;
                        cur_t += p->items[i].duration;
                    }
                }
            }
        }
        record_project_change(desc, true);
        snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                 "Executed: %s", desc);
        return;
    }

    // Script: Split Center (splits selected clips at 50% midpoint)
    if (str_contains_nocase(desc, "split center") ||
        str_contains_nocase(desc, "split_center") ||
        str_contains_nocase(desc, "center split")) {
        if (split_selected_items_at_center(p)) {
            record_project_change("Split Selected Clip(s) at Center", true);
            snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                     "Split selected clip(s) at center");
        } else {
            snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                     "Select one or more clips to split at center");
        }
        return;
    }

    // Script: Split / Slice
    if (act->id == ACT_SCRIPT_CLIP_MATCH_SLICE ||
        str_contains_nocase(desc, "split") || str_contains_nocase(desc, "slice")) {
        for (int a = 0; a < g_num_actions; a++) {
            if (g_actions[a].id == ACT_ITEM_SPLIT_AT_CURSOR) {
                execute_daw_action_by_index(p, ren, win, a);
                break;
            }
        }
        snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                 "Executed: %s", desc);
        return;
    }

    // Script: Color / Theme
    if (str_contains_nocase(desc, "color") || str_contains_nocase(desc, "theme")) {
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].is_selected) {
                g_item_color_idx[tab][i] = (g_item_color_idx[tab][i] + 1) % 8;
            }
        }
        record_project_change(desc, true);
        snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                 "Executed: %s", desc);
        return;
    }

    // Script: CSV export Project Timeline.lua
    if (act->id == ACT_SCRIPT_CSV_EXPORT_TIMELINE || str_contains_nocase(desc, "CSV")) {
        FILE *csv = fopen("project_timeline.csv", "w");
        if (csv) {
            fprintf(csv, "ItemIndex,TrackNumber,TrackName,ClipName,StartTimeSec,DurationSec,EndTimeSec,FilePath\n");
            for (int i = 0; i < p->num_items; i++) {
                AudioItem *it = &p->items[i];
                const char *tname = (it->track_idx >= 0 && it->track_idx < p->num_tracks) ? p->track_names[it->track_idx] : "Track";
                fprintf(csv, "%d,%d,\"%s\",\"%s\",%.3f,%.3f,%.3f,\"%s\"\n",
                        i + 1, it->track_idx + 1, tname, it->label,
                        it->start_time, it->duration, it->start_time + it->duration, it->file_path);
            }
            fclose(csv);
        }
        record_project_change("Script: CSV export Project Timeline.lua", false);
        snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                 "Exported timeline to project_timeline.csv");
        return;
    }

    // Generic custom script execution
    const char *final_lbl = custom_msg[0] ? custom_msg : desc;
    record_project_change(final_lbl, true);
    snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
             "Executed %s", final_lbl);
}

// Executes any built-in or script action by its index in g_actions[]
static void execute_daw_action_by_index(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win, int act_idx) {
    if (!p || act_idx < 0 || act_idx >= g_num_actions) return;
    DawActionEntry *act = &g_actions[act_idx];
    int tab = get_valid_tab_idx();
    int win_w = 1280, win_h = 720;
    if (win) SDL_GetWindowSize(win, &win_w, &win_h);
    int view_w = win_w - g_daw.tcp_width - SCROLLBAR_SIZE;
    int view_h = win_h - TABBAR_HEIGHT - TOPBAR_HEIGHT - RULER_HEIGHT - (p->show_marker_lane ? MARKER_BAR_HEIGHT : 0) - SCROLLBAR_SIZE;

    if (act->is_script || act->id == ACT_USER_IMPORTED_SCRIPT) {
        execute_script_action_entry(p, ren, win, act);
        return;
    }

    switch (act->id) {
        case ACT_VIEW_ALT_TCP_WIDTH: {
            if (g_daw.tcp_width > 40) {
                g_saved_alt_tcp_width = g_daw.tcp_width;
                g_daw.tcp_width = 0;
            } else {
                g_daw.tcp_width = (g_saved_alt_tcp_width >= 140) ? g_saved_alt_tcp_width : 200;
            }
            break;
        }
        case ACT_VIEW_TRACK_ZOOM_DEFAULT:
        case ACT_VIEW_TRACK_ZOOM_DEFAULT_NO_PIN: {
            for (int t = 0; t < p->num_tracks; t++) p->track_heights[t] = 84;
            break;
        }
        case ACT_VIEW_TRACK_ZOOM_MAX_OVER:
        case ACT_VIEW_TRACK_ZOOM_MAX_100: {
            int max_h = (view_h > 120) ? view_h : 220;
            if (max_h > 260) max_h = 260;
            bool already_max = (p->num_tracks > 0 && p->track_heights[0] >= max_h - 4);
            for (int t = 0; t < p->num_tracks; t++) {
                p->track_heights[t] = already_max ? 84 : max_h;
            }
            break;
        }
        case ACT_VIEW_TRACK_ZOOM_MIN:
        case ACT_VIEW_TRACK_ZOOM_MIN_NO_PIN: {
            bool already_min = (p->num_tracks > 0 && p->track_heights[0] <= 52);
            for (int t = 0; t < p->num_tracks; t++) {
                p->track_heights[t] = already_min ? 84 : 50;
            }
            break;
        }
        case ACT_VIEW_TOGGLE_TRANSPORT: {
            g_show_transport_bar = !g_show_transport_bar;
            break;
        }
        case ACT_VIEW_ZOOM_SELECTED_ITEMS: {
            double min_s = 1e18, max_e = 0.0;
            bool any_sel = false;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) {
                    any_sel = true;
                    if (p->items[i].start_time < min_s) min_s = p->items[i].start_time;
                    double ie = p->items[i].start_time + p->items[i].duration;
                    if (ie > max_e) max_e = ie;
                }
            }
            if (any_sel && max_e > min_s + 0.05 && view_w > 100) {
                double span = (max_e - min_s) * 1.15;
                float new_pps = (float)((double)view_w / span);
                if (new_pps < 15.0f) new_pps = 15.0f;
                if (new_pps > 400.0f) new_pps = 400.0f;
                p->pixels_per_second = new_pps;
                p->scroll_x_sec = fmax(0.0, min_s - (max_e - min_s) * 0.05);
            }
            break;
        }
        case ACT_VIEW_ZOOM_IN_HORIZ:
        case ACT_VIEW_ZOOM_HORIZ_WHEEL: {
            p->pixels_per_second = fminf(400.0f, p->pixels_per_second * 1.25f);
            break;
        }
        case ACT_VIEW_ZOOM_OUT_HORIZ:
        case ACT_VIEW_ZOOM_HORIZ_REV_WHEEL: {
            p->pixels_per_second = fmaxf(15.0f, p->pixels_per_second * 0.8f);
            break;
        }
        case ACT_VIEW_SCROLL_HORIZ_WHEEL: {
            p->scroll_x_sec += 40.0 / p->pixels_per_second;
            break;
        }
        case ACT_VIEW_ZOOM_IN_VERT:
        case ACT_VIEW_ZOOM_VERT_WHEEL:
        case ACT_VIEW_ZOOM_VERT_SNAP: {
            for (int t = 0; t < p->num_tracks; t++) {
                int nh = p->track_heights[t] + 14;
                if (nh > 260) nh = 260;
                p->track_heights[t] = nh;
            }
            break;
        }
        case ACT_VIEW_ZOOM_OUT_VERT:
        case ACT_VIEW_ZOOM_VERT_REV_WHEEL:
        case ACT_VIEW_ZOOM_VERT_REV_SNAP: {
            for (int t = 0; t < p->num_tracks; t++) {
                int nh = p->track_heights[t] - 14;
                if (nh < 50) nh = 50;
                p->track_heights[t] = nh;
            }
            break;
        }
        case ACT_VIEW_ZOOM_OUT_PROJECT: {
            double max_e = 10.0;
            for (int i = 0; i < p->num_items; i++) {
                double ie = p->items[i].start_time + p->items[i].duration;
                if (ie > max_e) max_e = ie;
            }
            if (view_w > 100) {
                float new_pps = (float)((double)view_w / (max_e * 1.08));
                if (new_pps < 15.0f) new_pps = 15.0f;
                if (new_pps > 400.0f) new_pps = 400.0f;
                p->pixels_per_second = new_pps;
                p->scroll_x_sec = 0.0;
            }
            break;
        }
        case ACT_VIEW_ZOOM_TIME_SELECTION: {
            if (is_range_active() && view_w > 100) {
                double rs = get_range_start();
                double re = get_range_end();
                double span = (re - rs) * 1.1;
                if (span > 0.05) {
                    float new_pps = (float)((double)view_w / span);
                    if (new_pps < 15.0f) new_pps = 15.0f;
                    if (new_pps > 400.0f) new_pps = 400.0f;
                    p->pixels_per_second = new_pps;
                    p->scroll_x_sec = fmax(0.0, rs - (re - rs) * 0.05);
                }
            }
            break;
        }
        case ACT_VIEW_SHOW_ACTION_LIST: {
            g_action_win.is_open = !g_action_win.is_open;
            if (g_action_win.is_open) {
                scan_scripts_directory_to_actions(&g_daw);
                g_action_win.win_rect.x = (win_w - g_action_win.win_rect.w) / 2;
                g_action_win.win_rect.y = (win_h - g_action_win.win_rect.h) / 2;
            }
            break;
        }
        case ACT_VIEW_TOGGLE_VIDEO_WINDOW: {
            p->video.is_open = !p->video.is_open;
            break;
        }
        case ACT_VIEW_TOGGLE_MARKER_LANE: {
            p->show_marker_lane = !p->show_marker_lane;
            break;
        }
        case ACT_VIEW_SHOW_HISTORY_WINDOW: {
            g_history_popup_open = !g_history_popup_open;
            if (g_history_popup_open) {
                g_history_win_rect.x = (win_w - g_history_win_rect.w) / 2;
                g_history_win_rect.y = (win_h - g_history_win_rect.h) / 2;
            }
            break;
        }
        case ACT_VIEW_SHOW_HIDE_TRACKS_WINDOW: {
            g_hide_popup_open = !g_hide_popup_open;
            if (g_hide_popup_open) {
                g_hide_win_rect.x = (win_w - g_hide_win_rect.w) / 2;
                g_hide_win_rect.y = (win_h - g_hide_win_rect.h) / 2;
            }
            break;
        }

        // ---- XENAKIOS / SWS ACTIONS ----
        case ACT_SWS_CREATE_NEW_TRACKS_DEP:
        case ACT_TRACK_INSERT_NEW: {
            if (p->num_tracks < MAX_TRACKS) {
                push_undo_state();
                int nt = p->num_tracks++;
                p->track_heights[nt] = 84;
                p->tracks[nt].id = nt + 1;
                p->tracks[nt].volume = 1.0f;
                snprintf(p->track_names[nt], 64, "Track %d", nt + 1);
                record_project_change("Added New Track", true);
            }
            break;
        }
        case ACT_SWS_REPOSITION_ITEMS: {
            g_reposition.is_open = true;
            g_reposition.win_rect.x = (win_w - g_reposition.win_rect.w) / 2;
            g_reposition.win_rect.y = (win_h - g_reposition.win_rect.h) / 2;
            g_reposition.interval_focused = true;
            SDL_StartTextInput();
            break;
        }
        case ACT_ITEM_DYNAMIC_SPLIT: {
            g_dyn_split.is_open = true;
            g_dyn_split.win_rect.x = (win_w - g_dyn_split.win_rect.w) / 2;
            g_dyn_split.win_rect.y = (win_h - g_dyn_split.win_rect.h) / 2;
            g_dyn_split.editing_field_id = 0;
            g_dyn_split.dragging_slider_id = 0;
            g_dyn_split.status_msg[0] = '\0';
            break;
        }
        case ACT_ITEM_JOIN_GLUE: {
            execute_join_glue_selected_items(p, ren, win);
            break;
        }
        case ACT_ITEM_MOVE_UP_TRACK: {
            bool moved = false;
            for (int i = 0; i < p->num_items; i++) {
                if (!p->items[i].is_selected) continue;
                int cur_t = p->items[i].track_idx;
                int prev_t = -1;
                for (int t = cur_t - 1; t >= 0; t--) {
                    if (!is_track_hidden(t)) { prev_t = t; break; }
                }
                if (prev_t >= 0) {
                    if (!moved) { push_undo_state(); moved = true; }
                    p->items[i].track_idx = prev_t;
                    if (!is_crossfade_active()) {
                        p->items[i].start_time = clamp_non_overlapping_start(
                            p, i, prev_t, p->items[i].start_time, p->items[i].duration);
                    }
                }
            }
            if (moved) record_project_change("Moved Selected Clip(s) Up (8)", true);
            break;
        }
        case ACT_ITEM_MOVE_DOWN_TRACK: {
            bool moved = false;
            for (int i = 0; i < p->num_items; i++) {
                if (!p->items[i].is_selected) continue;
                int cur_t = p->items[i].track_idx;
                int next_t = -1;
                for (int t = cur_t + 1; t < p->num_tracks; t++) {
                    if (!is_track_hidden(t)) { next_t = t; break; }
                }
                if (next_t >= 0) {
                    if (!moved) { push_undo_state(); moved = true; }
                    p->items[i].track_idx = next_t;
                    if (!is_crossfade_active()) {
                        p->items[i].start_time = clamp_non_overlapping_start(
                            p, i, next_t, p->items[i].start_time, p->items[i].duration);
                    }
                }
            }
            if (moved) record_project_change("Moved Selected Clip(s) Down (2)", true);
            break;
        }
        case ACT_SWS_SCROLL_TRACK_VIEW_DOWN_PAGE: {
            int max_h_trk = 0;
            for (int t = 0; t < p->num_tracks; t++) {
                if (!is_track_hidden(t)) max_h_trk += p->track_heights[t];
            }
            int max_y_sc = max_h_trk - view_h;
            if (max_y_sc < 0) max_y_sc = 0;
            p->scroll_y_px += 180;
            if (p->scroll_y_px > max_y_sc) p->scroll_y_px = max_y_sc;
            break;
        }
        case ACT_SWS_SCROLL_TRACK_VIEW_UP_PAGE: {
            p->scroll_y_px -= 180;
            if (p->scroll_y_px < 0) p->scroll_y_px = 0;
            break;
        }
        case ACT_SWS_MOVE_RIGHT_EDGE_TO_CURSOR: {
            push_undo_state();
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) {
                    double ns = p->playhead_seconds - p->items[i].duration;
                    if (ns < 0.0) ns = 0.0;
                    p->items[i].start_time = ns;
                }
            }
            record_project_change("SWS: Move right edge to edit cursor", true);
            break;
        }
        case ACT_SWS_MOVE_LEFT_EDGE_TO_CURSOR: {
            push_undo_state();
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) {
                    p->items[i].start_time = fmax(0.0, p->playhead_seconds);
                }
            }
            record_project_change("SWS: Move left edge to edit cursor", true);
            break;
        }

        // ---- ITEM SELECTION & NAVIGATION (ONLY FOR SELECTED TRACK) ----
        case ACT_ITEM_NAV_PREV_ITEM: {
            bool any_trk_sel = false;
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t] && !is_track_hidden(t)) { any_trk_sel = true; break; }
            }
            if (!any_trk_sel) {
                for (int i = 0; i < p->num_items; i++) {
                    if (p->items[i].is_selected && !is_track_hidden(p->items[i].track_idx)) {
                        p->track_selected[p->items[i].track_idx] = true;
                        any_trk_sel = true;
                        break;
                    }
                }
            }
            if (!any_trk_sel) break;

            int best_idx = -1;
            double best_t = -1.0;
            for (int i = 0; i < p->num_items; i++) {
                int tr = p->items[i].track_idx;
                if (tr < 0 || tr >= p->num_tracks || !p->track_selected[tr] || is_track_hidden(tr)) continue;
                double st = p->items[i].start_time;
                if (st < p->playhead_seconds - 0.001 && st > best_t) {
                    best_t = st;
                    best_idx = i;
                }
            }
            if (best_idx < 0) {
                double earliest = 1e18;
                for (int i = 0; i < p->num_items; i++) {
                    int tr = p->items[i].track_idx;
                    if (tr < 0 || tr >= p->num_tracks || !p->track_selected[tr] || is_track_hidden(tr)) continue;
                    if (p->items[i].start_time < earliest) {
                        earliest = p->items[i].start_time;
                        best_idx = i;
                    }
                }
            }
            if (best_idx >= 0) {
                for (int i = 0; i < p->num_items; i++) p->items[i].is_selected = (i == best_idx);
                g_last_clicked_item_idx = best_idx;
                p->playhead_seconds = p->items[best_idx].start_time;
            }
            break;
        }
        case ACT_ITEM_NAV_NEXT_ITEM: {
            bool any_trk_sel = false;
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t] && !is_track_hidden(t)) { any_trk_sel = true; break; }
            }
            if (!any_trk_sel) {
                for (int i = 0; i < p->num_items; i++) {
                    if (p->items[i].is_selected && !is_track_hidden(p->items[i].track_idx)) {
                        p->track_selected[p->items[i].track_idx] = true;
                        any_trk_sel = true;
                        break;
                    }
                }
            }
            if (!any_trk_sel) break;

            int best_idx = -1;
            double best_t = 1e18;
            for (int i = 0; i < p->num_items; i++) {
                int tr = p->items[i].track_idx;
                if (tr < 0 || tr >= p->num_tracks || !p->track_selected[tr] || is_track_hidden(tr)) continue;
                double st = p->items[i].start_time;
                if (st > p->playhead_seconds + 0.001 && st < best_t) {
                    best_t = st;
                    best_idx = i;
                }
            }
            if (best_idx >= 0) {
                for (int i = 0; i < p->num_items; i++) p->items[i].is_selected = (i == best_idx);
                g_last_clicked_item_idx = best_idx;
                p->playhead_seconds = p->items[best_idx].start_time;
            }
            break;
        }
        case ACT_ITEM_NAV_CURSOR_LEFT_EDGE_OF_ITEM: {
            double target_s = -1.0;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) {
                    if (target_s < 0.0 || p->items[i].start_time < target_s) {
                        target_s = p->items[i].start_time;
                    }
                }
            }
            if (target_s < 0.0) {
                for (int i = 0; i < p->num_items; i++) {
                    double s = p->items[i].start_time;
                    double e = s + p->items[i].duration;
                    if (p->playhead_seconds >= s && p->playhead_seconds <= e) {
                        target_s = s;
                        break;
                    }
                }
            }
            if (target_s >= 0.0) p->playhead_seconds = target_s;
            break;
        }
        case ACT_ITEM_NAV_CURSOR_RIGHT_EDGE_OF_ITEM: {
            double target_e = -1.0;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) {
                    double e = p->items[i].start_time + p->items[i].duration;
                    if (e > target_e) target_e = e;
                }
            }
            if (target_e < 0.0) {
                for (int i = 0; i < p->num_items; i++) {
                    double s = p->items[i].start_time;
                    double e = s + p->items[i].duration;
                    if (p->playhead_seconds >= s && p->playhead_seconds <= e) {
                        target_e = e;
                        break;
                    }
                }
            }
            if (target_e >= 0.0) p->playhead_seconds = target_e;
            break;
        }
        case ACT_ITEM_NAV_CURSOR_LEFT_NEAREST_EDGE: {
            double best_edge = -1.0;
            for (int i = 0; i < p->num_items; i++) {
                if (is_track_hidden(p->items[i].track_idx)) continue;
                double s = p->items[i].start_time;
                double e = s + p->items[i].duration;
                if (s < p->playhead_seconds - 0.001 && s > best_edge) best_edge = s;
                if (e < p->playhead_seconds - 0.001 && e > best_edge) best_edge = e;
            }
            if (best_edge >= 0.0) p->playhead_seconds = best_edge;
            else p->playhead_seconds = 0.0;
            break;
        }
        case ACT_ITEM_NAV_CURSOR_RIGHT_NEAREST_EDGE: {
            double best_edge = 1e18;
            for (int i = 0; i < p->num_items; i++) {
                if (is_track_hidden(p->items[i].track_idx)) continue;
                double s = p->items[i].start_time;
                double e = s + p->items[i].duration;
                if (s > p->playhead_seconds + 0.001 && s < best_edge) best_edge = s;
                if (e > p->playhead_seconds + 0.001 && e < best_edge) best_edge = e;
            }
            if (best_edge < 1e17) p->playhead_seconds = best_edge;
            break;
        }

        // ---- AUDIO ITEM EDITING & GAIN OPERATIONS ----
        case ACT_EDIT_REMOVE_FOCUS:
        case ACT_SWS_DEL_ACTIVE_TAKE_1:
        case ACT_SWS_DEL_ACTIVE_TAKE_2:
        case ACT_SWS_DEL_SELECTED_ITEM_SEND: {
            if (g_selected_marker_idx >= 0 && g_selected_marker_idx < p->num_markers) {
                delete_marker_at(p, g_selected_marker_idx);
                break;
            }
            bool any_item_sel = false;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) { any_item_sel = true; break; }
            }
            if (any_item_sel) {
                push_undo_state();
                for (int i = p->num_items - 1; i >= 0; i--) {
                    if (p->items[i].is_selected) {
                        delete_item_at_index(p, i);
                    }
                }
                record_project_change("Removed Selected Media Item(s)", true);
            } else {
                request_delete_selected_tracks_with_prompt(p);
            }
            break;
        }
        case ACT_ITEM_TOGGLE_MUTE_FOCUS: {
            bool any_item_sel = false;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) {
                    g_item_muted[tab][i] = !g_item_muted[tab][i];
                    any_item_sel = true;
                }
            }
            if (any_item_sel) {
                record_project_change("Toggled Item Mute", true);
            } else {
                for (int t = 0; t < p->num_tracks; t++) {
                    if (p->track_selected[t]) {
                        p->tracks[t].mute = !p->tracks[t].mute;
                    }
                }
                record_project_change("Toggled Track Mute", true);
            }
            break;
        }
        case ACT_ITEM_NUDGE_VOL_UP_1DB: {
            bool any_item_sel = false;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) {
                    g_item_vol_db[tab][i] = fminf(24.0f, g_item_vol_db[tab][i] + 1.0f);
                    any_item_sel = true;
                }
            }
            if (!any_item_sel) {
                for (int t = 0; t < p->num_tracks; t++) {
                    if (p->track_selected[t]) p->track_db[t] = fminf(24.0f, p->track_db[t] + 1.0f);
                }
            }
            record_project_change("Item: Nudge volume +1dB", true);
            break;
        }
        case ACT_ITEM_NUDGE_VOL_DOWN_1DB: {
            bool any_item_sel = false;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) {
                    g_item_vol_db[tab][i] = fmaxf(-60.0f, g_item_vol_db[tab][i] - 1.0f);
                    any_item_sel = true;
                }
            }
            if (!any_item_sel) {
                for (int t = 0; t < p->num_tracks; t++) {
                    if (p->track_selected[t]) p->track_db[t] = fmaxf(-60.0f, p->track_db[t] - 1.0f);
                }
            }
            record_project_change("Item: Nudge volume -1dB", true);
            break;
        }
        case ACT_ITEM_SET_CUSTOM_COLOR: {
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) {
                    g_item_color_idx[tab][i] = (g_item_color_idx[tab][i] + 1) % 8;
                }
            }
            record_project_change("Set Item Custom Color", true);
            break;
        }
        case ACT_ITEM_SPLIT_AT_CURSOR: {
            double cur_t = p->playhead_seconds;
            bool any_sel = false;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) { any_sel = true; break; }
            }
            bool did_split = false;
            int orig_count = p->num_items;
            for (int i = 0; i < orig_count; i++) {
                AudioItem *it = &p->items[i];
                if (any_sel && !it->is_selected) continue;
                double it_s = it->start_time, it_e = it->start_time + it->duration;
                if (cur_t > it_s + 0.05 && cur_t < it_e - 0.05 && p->num_items < MAX_ITEMS) {
                    if (!did_split) {
                        push_undo_state();
                        did_split = true;
                    }
                    int r_idx = p->num_items;
                    double orig_fin = g_item_fade_in[tab][i];
                    double orig_fout = g_item_fade_out[tab][i];
                    AudioItem *r_item = &p->items[r_idx];
                    memcpy(r_item, it, sizeof(AudioItem));
                    if (it->peak_count > 0 && it->peaks) {
                        r_item->peaks = (PeakBin *)calloc((size_t)it->peak_count, sizeof(PeakBin));
                        if (r_item->peaks) {
                            memcpy(r_item->peaks, it->peaks, (size_t)it->peak_count * sizeof(PeakBin));
                        }
                    }
                    double delta = cur_t - it_s;
                    it->duration = delta;
                    r_item->start_time = cur_t;
                    r_item->offset_in_file = it->offset_in_file + delta;
                    r_item->duration = it_e - cur_t;
                    r_item->is_selected = true;
                    it->is_selected = false;
                    r_item->raw_file_handle = r_item->raw_audio_path[0] ? fopen(r_item->raw_audio_path, "rb") : NULL;
                    g_item_fade_in[tab][i] = fmin(orig_fin, it->duration);
                    g_item_fade_out[tab][i] = 0.0;
                    g_item_fade_in[tab][r_idx] = 0.0;
                    g_item_fade_out[tab][r_idx] = fmin(orig_fout, r_item->duration);
                    g_item_vol_db[tab][r_idx] = g_item_vol_db[tab][i];
                    g_item_muted[tab][r_idx] = g_item_muted[tab][i];
                    g_item_color_idx[tab][r_idx] = g_item_color_idx[tab][i];
                    g_item_group_id[tab][r_idx] = g_item_group_id[tab][i];
                    p->num_items++;
                }
            }
            if (did_split) record_project_change("Split Media Clip", true);
            break;
        }
        case ACT_ITEM_DUPLICATE_ITEMS: {
            int orig_count = p->num_items;
            bool did_dup = false;
            for (int i = 0; i < orig_count; i++) {
                if (p->items[i].is_selected && p->num_items < MAX_ITEMS) {
                    if (!did_dup) { push_undo_state(); did_dup = true; }
                    int ni = p->num_items++;
                    AudioItem *src = &p->items[i];
                    AudioItem *dst = &p->items[ni];
                    memcpy(dst, src, sizeof(AudioItem));
                    dst->start_time = src->start_time + src->duration;
                    if (src->peak_count > 0 && src->peaks) {
                        dst->peaks = (PeakBin *)calloc((size_t)src->peak_count, sizeof(PeakBin));
                        if (dst->peaks) memcpy(dst->peaks, src->peaks, (size_t)src->peak_count * sizeof(PeakBin));
                    }
                    dst->raw_file_handle = dst->raw_audio_path[0] ? fopen(dst->raw_audio_path, "rb") : NULL;
                    g_item_fade_in[tab][ni] = g_item_fade_in[tab][i];
                    g_item_fade_out[tab][ni] = g_item_fade_out[tab][i];
                    g_item_vol_db[tab][ni] = g_item_vol_db[tab][i];
                    g_item_muted[tab][ni] = g_item_muted[tab][i];
                    g_item_color_idx[tab][ni] = g_item_color_idx[tab][i];
                    g_item_group_id[tab][ni] = g_item_group_id[tab][i];
                    src->is_selected = false;
                    dst->is_selected = true;
                }
            }
            if (did_dup) record_project_change("Duplicated Selected Item(s)", true);
            break;
        }
        case ACT_ITEM_FADE_IN_TO_CURSOR: {
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected && p->playhead_seconds > p->items[i].start_time) {
                    set_item_fade_in(p, i, p->playhead_seconds - p->items[i].start_time);
                }
            }
            record_project_change("Fade In To Cursor", true);
            break;
        }
        case ACT_ITEM_FADE_OUT_FROM_CURSOR: {
            for (int i = 0; i < p->num_items; i++) {
                double ie = p->items[i].start_time + p->items[i].duration;
                if (p->items[i].is_selected && p->playhead_seconds < ie) {
                    set_item_fade_out(p, i, ie - p->playhead_seconds);
                }
            }
            record_project_change("Fade Out From Cursor", true);
            break;
        }
        case ACT_ITEM_RESET_FADES: {
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) {
                    set_item_fade_in(p, i, 0.0);
                    set_item_fade_out(p, i, 0.0);
                }
            }
            record_project_change("Removed Clip Fades", true);
            break;
        }
        case ACT_ITEM_NORMALIZE_GAIN: {
            for (int i = 0; i < p->num_items; i++) {
                if (!p->items[i].is_selected) continue;
                AudioItem *it = &p->items[i];
                float max_pk = 0.0f;
                if (it->peaks && it->peak_count > 0) {
                    for (int b = 0; b < it->peak_count; b++) {
                        float a1 = fabsf(it->peaks[b].min_val);
                        float a2 = fabsf(it->peaks[b].max_val);
                        if (a1 > max_pk) max_pk = a1;
                        if (a2 > max_pk) max_pk = a2;
                    }
                }
                if (max_pk > 0.001f) {
                    g_item_vol_db[tab][i] = fmaxf(-24.0f, fminf(18.0f, -0.5f - 20.0f * log10f(max_pk)));
                }
            }
            record_project_change("Normalized Item(s)", true);
            break;
        }
        case ACT_ITEM_TRIM_LEFT_TO_CURSOR: {
            push_undo_state();
            for (int i = 0; i < p->num_items; i++) {
                AudioItem *it = &p->items[i];
                double ie = it->start_time + it->duration;
                if (it->is_selected && p->playhead_seconds > it->start_time && p->playhead_seconds < ie - 0.05) {
                    double d = p->playhead_seconds - it->start_time;
                    it->start_time = p->playhead_seconds;
                    it->offset_in_file += d;
                    it->duration -= d;
                }
            }
            record_project_change("Trimmed Left Edge to Cursor", true);
            break;
        }
        case ACT_ITEM_TRIM_RIGHT_TO_CURSOR: {
            push_undo_state();
            for (int i = 0; i < p->num_items; i++) {
                AudioItem *it = &p->items[i];
                double ie = it->start_time + it->duration;
                if (it->is_selected && p->playhead_seconds > it->start_time + 0.05 && p->playhead_seconds < ie) {
                    it->duration = p->playhead_seconds - it->start_time;
                }
            }
            record_project_change("Trimmed Right Edge to Cursor", true);
            break;
        }
        case ACT_ITEM_SELECT_ALL_ON_TRACK: {
            for (int i = 0; i < p->num_items; i++) {
                int tr = p->items[i].track_idx;
                if (tr >= 0 && tr < p->num_tracks && p->track_selected[tr]) {
                    p->items[i].is_selected = true;
                }
            }
            break;
        }
        case ACT_ITEM_UNSELECT_ALL: {
            for (int i = 0; i < p->num_items; i++) p->items[i].is_selected = false;
            break;
        }
        case ACT_ITEM_TOGGLE_CROSSFADE_MODE: {
            set_crossfade_active(!is_crossfade_active());
            break;
        }

        // ---- TRACK CONTROLS & TAB MANAGEMENT ----
        case ACT_TRACK_TOGGLE_SOLO_SELECTED: {
            bool any_sel = false;
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t]) {
                    p->tracks[t].solo = !p->tracks[t].solo;
                    any_sel = true;
                }
            }
            if (!any_sel && p->num_tracks > 0) {
                p->tracks[0].solo = !p->tracks[0].solo;
            }
            record_project_change("Track: Toggle solo for selected tracks", true);
            break;
        }
        case ACT_TRACK_TOGGLE_MUTE_SELECTED: {
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t]) p->tracks[t].mute = !p->tracks[t].mute;
            }
            record_project_change("Track: Toggle mute for selected tracks", true);
            break;
        }
        case ACT_TRACK_MOVE_UP: {
            push_undo_state();
            for (int t = 1; t < p->num_tracks; t++) {
                if (p->track_selected[t] && !p->track_selected[t - 1]) {
                    swap_tracks(p, t, t - 1);
                }
            }
            record_project_change("Track: Move tracks up", true);
            break;
        }
        case ACT_TRACK_MOVE_DOWN: {
            push_undo_state();
            for (int t = p->num_tracks - 2; t >= 0; t--) {
                if (p->track_selected[t] && !p->track_selected[t + 1]) {
                    swap_tracks(p, t, t + 1);
                }
            }
            record_project_change("Track: Move tracks down", true);
            break;
        }
        case ACT_TRACK_DUPLICATE_SELECTED: {
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t] && p->num_tracks < MAX_TRACKS) {
                    push_undo_state();
                    int nt = p->num_tracks++;
                    p->tracks[nt] = p->tracks[t];
                    p->tracks[nt].id = nt + 1;
                    p->track_db[nt] = p->track_db[t];
                    p->track_heights[nt] = p->track_heights[t];
                    snprintf(p->track_names[nt], 64, "%.54s (Copy)", p->track_names[t]);
                    record_project_change("Duplicated Track", true);
                    break;
                }
            }
            break;
        }
        case ACT_TRACK_REMOVE_SELECTED: {
            request_delete_selected_tracks_with_prompt(p);
            break;
        }
        case ACT_TRACK_SELECT_ALL: {
            for (int t = 0; t < p->num_tracks; t++) p->track_selected[t] = true;
            break;
        }
        case ACT_TRACK_UNSELECT_ALL: {
            for (int t = 0; t < p->num_tracks; t++) p->track_selected[t] = false;
            break;
        }
        case ACT_TRACK_NUDGE_VOL_UP: {
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t]) p->track_db[t] = fminf(24.0f, p->track_db[t] + 1.0f);
            }
            record_project_change("Track Volume +1dB", true);
            break;
        }
        case ACT_TRACK_NUDGE_VOL_DOWN: {
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t]) p->track_db[t] = fmaxf(-60.0f, p->track_db[t] - 1.0f);
            }
            record_project_change("Track Volume -1dB", true);
            break;
        }
        case ACT_TRACK_RESET_VOL_0DB: {
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t]) p->track_db[t] = 0.0f;
            }
            record_project_change("Reset Track Volume to 0dB", true);
            break;
        }
        case ACT_TAB_SWITCH_NEXT: {
            if (g_daw.num_tabs > 1) {
                g_daw.active_tab = (g_daw.active_tab + 1) % g_daw.num_tabs;
                g_selected_marker_idx = -1;
            }
            break;
        }
        case ACT_TAB_SWITCH_PREV: {
            if (g_daw.num_tabs > 1) {
                g_daw.active_tab = (g_daw.active_tab - 1 + g_daw.num_tabs) % g_daw.num_tabs;
                g_selected_marker_idx = -1;
            }
            break;
        }
        case ACT_TAB_NEW_PROJECT_TAB:
        case ACT_FILE_NEW_PROJECT: {
            if (g_daw.num_tabs < MAX_TABS) {
                int nt = g_daw.num_tabs;
                char name[64];
                snprintf(name, sizeof(name), "Project %d", nt + 1);
                init_tab_defaults(nt, name);
                g_tab_crossfade[nt] = true;
                g_tab_range_active[nt] = false;
                g_tab_range_start[nt] = 0.0;
                g_tab_range_end[nt] = 0.0;
                memset(g_marker_full_text[nt], 0, sizeof(g_marker_full_text[0]));
                memset(g_item_fade_in[nt], 0, sizeof(g_item_fade_in[0]));
                memset(g_item_fade_out[nt], 0, sizeof(g_item_fade_out[0]));
                memset(g_item_vol_db[nt], 0, sizeof(g_item_vol_db[0]));
                memset(g_item_muted[nt], 0, sizeof(g_item_muted[0]));
                memset(g_item_color_idx[nt], 0, sizeof(g_item_color_idx[0]));
                memset(g_item_group_id[nt], 0, sizeof(g_item_group_id[0]));
                memset(g_track_hidden[nt], 0, sizeof(g_track_hidden[0]));
                memset(g_tab_history[nt], 0, sizeof(g_tab_history[0]));
                g_tab_history_count[nt] = 0;
                g_tab_notification[nt][0] = '\0';
                g_daw.active_tab = g_daw.num_tabs++;
                g_selected_marker_idx = -1;
                record_project_change("Project Created", false);
            }
            break;
        }
        case ACT_TAB_CLOSE_CURRENT_TAB: {
            request_close_tab(g_daw.active_tab);
            break;
        }

        // ---- TRANSPORT, EDIT, MARKERS, LOOP & FILE ----
        case ACT_TRANSPORT_PLAY_STOP:
        case ACT_TRANSPORT_PAUSE: {
            if (g_rec.is_recording) {
                start_or_stop_recording(p, ren, win);
            } else {
                p->is_playing = !p->is_playing;
            }
            break;
        }
        case ACT_TRANSPORT_RECORD: {
            start_or_stop_recording(p, ren, win);
            break;
        }
        case ACT_TRANSPORT_GO_TO_START: {
            p->playhead_seconds = 0.0;
            p->scroll_x_sec = 0.0;
            ensure_playhead_visible_in_viewport(p, view_w);
            break;
        }
        case ACT_TRANSPORT_GO_TO_END: {
            double max_e = 0.0;
            for (int i = 0; i < p->num_items; i++) {
                double ie = p->items[i].start_time + p->items[i].duration;
                if (ie > max_e) max_e = ie;
            }
            p->playhead_seconds = max_e;
            ensure_playhead_visible_in_viewport(p, view_w);
            break;
        }
        case ACT_TRANSPORT_TOGGLE_REPEAT_RANGE: {
            handle_top_menu_range_button(p);
            break;
        }
        case ACT_EDIT_COPY: {
            bool any_clip_sel = false;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected) { any_clip_sel = true; break; }
            }
            if (any_clip_sel) {
                copy_selected_items_to_clipboard(p);
                g_last_copied_is_track = false;
                record_project_change("Copied Selected Clip(s)", false);
            } else {
                copy_selected_tracks_to_clipboard(p);
                g_last_copied_is_track = true;
                record_project_change("Copied Selected Track(s)", false);
            }
            break;
        }
        case ACT_EDIT_CUT: {
            copy_selected_items_to_clipboard(p);
            g_last_copied_is_track = false;
            if (g_clipboard_count > 0) {
                push_undo_state();
                for (int i = p->num_items - 1; i >= 0; i--) {
                    if (p->items[i].is_selected) {
                        delete_item_at_index(p, i);
                    }
                }
                record_project_change("Edit: Cut", true);
            }
            break;
        }
        case ACT_EDIT_PASTE: {
            if (g_last_copied_is_track) {
                paste_tracks_at_bottom_of_selected(p);
            } else if (g_clipboard_count > 0) {
                paste_clipboard_items_at_playhead(p);
                ensure_playhead_visible_in_viewport(p, view_w);
            } else {
                paste_tracks_at_bottom_of_selected(p);
            }
            break;
        }
        case ACT_EDIT_UNDO: {
            perform_undo();
            record_project_change("Undo Action", true);
            break;
        }
        case ACT_EDIT_REDO: {
            perform_redo();
            record_project_change("Redo Action", true);
            break;
        }
        case ACT_EDIT_SELECT_ALL: {
            for (int i = 0; i < p->num_items; i++) {
                if (!is_track_hidden(p->items[i].track_idx)) {
                    p->items[i].is_selected = true;
                }
            }
            break;
        }
        case ACT_MARKER_INSERT_AT_CURSOR: {
            p->show_marker_lane = true;
            if (p->num_markers < MAX_MARKERS) {
                push_undo_state();
                int new_idx = p->num_markers++;
                p->markers[new_idx].time_sec = p->playhead_seconds;
                char def_txt[64];
                snprintf(def_txt, sizeof(def_txt), "Marker %d", p->num_markers);
                set_marker_full_str(p, new_idx, def_txt);
                g_selected_marker_idx = new_idx;
                record_project_change("Added Marker", true);
            }
            break;
        }
        case ACT_MARKER_NEXT: {
            double best_t = 1e18;
            for (int m = 0; m < p->num_markers; m++) {
                if (p->markers[m].time_sec > p->playhead_seconds + 0.001 && p->markers[m].time_sec < best_t) {
                    best_t = p->markers[m].time_sec;
                }
            }
            if (best_t < 1e17) p->playhead_seconds = best_t;
            break;
        }
        case ACT_MARKER_PREV: {
            double best_t = -1.0;
            for (int m = 0; m < p->num_markers; m++) {
                if (p->markers[m].time_sec < p->playhead_seconds - 0.001 && p->markers[m].time_sec > best_t) {
                    best_t = p->markers[m].time_sec;
                }
            }
            if (best_t >= 0.0) p->playhead_seconds = best_t;
            else p->playhead_seconds = 0.0;
            break;
        }
        case ACT_MARKER_DELETE_NEAREST: {
            if (g_selected_marker_idx >= 0 && g_selected_marker_idx < p->num_markers) {
                delete_marker_at(p, g_selected_marker_idx);
            } else if (p->num_markers > 0) {
                int best_m = 0;
                double best_d = fabs(p->markers[0].time_sec - p->playhead_seconds);
                for (int m = 1; m < p->num_markers; m++) {
                    double d = fabs(p->markers[m].time_sec - p->playhead_seconds);
                    if (d < best_d) { best_d = d; best_m = m; }
                }
                delete_marker_at(p, best_m);
            }
            break;
        }
        case ACT_LOOP_SET_START_AT_CURSOR: {
            double re = is_range_active() ? get_range_end() : (p->playhead_seconds + 5.0);
            if (re <= p->playhead_seconds + 0.05) re = p->playhead_seconds + 5.0;
            set_tab_range(p->playhead_seconds, re, true);
            break;
        }
        case ACT_LOOP_SET_END_AT_CURSOR: {
            double rs = is_range_active() ? get_range_start() : 0.0;
            if (p->playhead_seconds > rs + 0.05) {
                set_tab_range(rs, p->playhead_seconds, true);
            }
            break;
        }
        case ACT_LOOP_SET_TO_SELECTED_ITEMS: {
            handle_top_menu_range_button(p);
            break;
        }
        case ACT_LOOP_CLEAR: {
            g_tab_range_active[tab] = false;
            break;
        }
        case ACT_FILE_OPEN_PROJECT: {
            request_open_project_with_save_prompt(p, ren, win);
            break;
        }
        case ACT_FILE_SAVE_PROJECT: {
            daw_save_project_dialog(p, false);
            break;
        }
        case ACT_FILE_SAVE_PROJECT_AS: {
            daw_save_project_dialog(p, true);
            break;
        }
        case ACT_FILE_IMPORT_MEDIA: {
            static char sel_files[MAX_MULTI_IMPORT_FILES][512];
            int num_sel = open_multi_media_dialog(sel_files, MAX_MULTI_IMPORT_FILES);
            int target_trk = 0;
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t] && !is_track_hidden(t)) { target_trk = t; break; }
            }
            if (num_sel == 1) {
                import_single_media_item(p, ren, win, sel_files[0], target_trk, p->playhead_seconds);
            } else if (num_sel > 1) {
                g_multi_import.num_files = num_sel;
                for (int k = 0; k < num_sel; k++) {
                    strncpy(g_multi_import.files[k], sel_files[k], 511);
                    g_multi_import.files[k][511] = '\0';
                }
                g_multi_import.base_track = target_trk;
                g_multi_import.base_time = p->playhead_seconds;
                g_multi_import.mode = MULTI_IMPORT_SEPARATE_TRACKS;
                g_multi_import.win_rect.x = (win_w - g_multi_import.win_rect.w) / 2;
                g_multi_import.win_rect.y = (win_h - g_multi_import.win_rect.h) / 2;
                g_multi_import.is_open = true;
            }
            break;
        }
        case ACT_FILE_EXPORT_MEDIA: {
            g_export.is_open = true;
            g_export.win_rect.x = (win_w - g_export.win_rect.w) / 2;
            g_export.win_rect.y = (win_h - g_export.win_rect.h) / 2;
            g_export.is_rendering = false;
            g_export.progress_pct = 0;
            g_export.elapsed_sec = 0.0;
            g_export.est_remaining_sec = 0.0;
            g_export.mp4_aac_dropdown_open = false;
            g_export.status_msg[0] = '\0';
            break;
        }
        default: {
            snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                     "Executed: %s", act->description);
            break;
        }
    }
}

// Handles global keyboard shortcut dispatch when no text box or modal capture is active
static bool handle_global_keyboard_shortcut(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win,
                                            SDL_Keycode sym, SDL_Keymod mod) {
    char chord[64];
    if (!format_sdl_key_chord(sym, mod, chord, sizeof(chord))) {
        return false;
    }
    int act_idx = find_action_by_shortcut_str(chord);
    if (act_idx >= 0) {
        execute_daw_action_by_index(p, ren, win, act_idx);
        return true;
    }
    return false;
}

// Handles SDL_KEYDOWN when the REAPER-style Actions (Keyboard Shortcuts) window is open
static bool handle_action_window_keydown(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win,
                                         SDL_Keycode sym, SDL_Keymod mod) {
    if (!g_action_win.is_open) return false;

    // 1. Key Capture Modal for [Add...] shortcut to selected action (saves permanently to keyboard_shortcuts.json)
    if (g_action_win.capturing_add_shortcut) {
        if (sym == SDLK_ESCAPE) {
            g_action_win.capturing_add_shortcut = false;
            return true;
        }
        char chord[64];
        if (format_sdl_key_chord(sym, mod, chord, sizeof(chord))) {
            int sel = g_action_win.selected_action_idx;
            if (sel >= 0 && sel < g_num_actions) {
                DawActionEntry *act = &g_actions[sel];
                add_shortcut_to_entry(act, chord);
                g_action_win.selected_shortcut_sub = act->num_shortcuts - 1;
                if (act->linked_script_idx >= 0 && act->linked_script_idx < g_daw.num_scripts) {
                    strncpy(g_daw.scripts[act->linked_script_idx].shortcut, chord,
                            sizeof(g_daw.scripts[act->linked_script_idx].shortcut) - 1);
                }
                save_keyboard_shortcuts_json();
                snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                         "Saved shortcut '%s' -> %s (keyboard_shortcuts.json)", chord, act->description);
                record_project_change("Saved Keyboard Shortcut", false);
            }
            g_action_win.capturing_add_shortcut = false;
        }
        return true;
    }

    // 2. Key Capture Modal for [Find shortcut...]
    if (g_action_win.capturing_find_shortcut) {
        if (sym == SDLK_ESCAPE) {
            g_action_win.capturing_find_shortcut = false;
            return true;
        }
        char chord[64];
        if (format_sdl_key_chord(sym, mod, chord, sizeof(chord))) {
            int found = find_action_by_shortcut_str(chord);
            if (found >= 0) {
                g_action_win.filter_text[0] = '\0';
                refresh_filtered_actions();
                g_action_win.selected_action_idx = found;
                g_action_win.selected_shortcut_sub = 0;
                scroll_action_list_to_selected(g_action_win.win_rect.h - 244);
                snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                         "Found shortcut '%s' -> %s", chord, g_actions[found].description);
            } else {
                snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                         "Shortcut '%s' is not currently assigned to any action.", chord);
            }
            g_action_win.capturing_find_shortcut = false;
        }
        return true;
    }

    // 3. Filter text box typing focus
    if (g_action_win.filter_focused) {
        if (sym == SDLK_ESCAPE || sym == SDLK_RETURN) {
            g_action_win.filter_focused = false;
            SDL_StopTextInput();
            return true;
        }
        if (sym == SDLK_BACKSPACE) {
            size_t l = strlen(g_action_win.filter_text);
            if (l > 0) {
                g_action_win.filter_text[l - 1] = '\0';
                g_action_win.scroll_y = 0;
                refresh_filtered_actions();
                if (g_action_win.num_filtered > 0) {
                    g_action_win.selected_action_idx = g_action_win.filtered_indices[0];
                    g_action_win.selected_shortcut_sub = 0;
                }
            }
            return true;
        }
        return true;
    }

    // 4. Navigation & execution inside Actions window when Filter is not focused
    if (sym == SDLK_ESCAPE) {
        g_action_win.is_open = false;
        return true;
    }
    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
        if (g_action_win.selected_action_idx >= 0 && g_action_win.selected_action_idx < g_num_actions) {
            execute_daw_action_by_index(p, ren, win, g_action_win.selected_action_idx);
        }
        return true;
    }

    // Allow triggering any assigned shortcut even while the Actions window is open
    return handle_global_keyboard_shortcut(p, ren, win, sym, mod);
}

// Handles mouse click inside the REAPER-style Actions (Keyboard Shortcuts) popup window
static bool handle_action_window_mouse_down(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win,
                                            int mx, int my, int clicks) {
    if (!g_action_win.is_open) return false;
    SDL_Rect *wr = &g_action_win.win_rect;
    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;

    if (g_action_win.capturing_add_shortcut || g_action_win.capturing_find_shortcut) {
        int mw = 430, mh = 134;
        int mx_box = wx + (ww - mw) / 2;
        int my_box = wy + (wh - mh) / 2;
        SDL_Rect cancel_btn = { mx_box + mw - 106, my_box + mh - 40, 90, 28 };
        if (point_in_rect(mx, my, &cancel_btn)) {
            g_action_win.capturing_add_shortcut = false;
            g_action_win.capturing_find_shortcut = false;
        }
        return true;
    }

    if (!point_in_rect(mx, my, wr)) {
        return false;
    }

    // Unfocus filter text input unless clicked inside filter box below
    if (g_action_win.filter_focused) {
        g_action_win.filter_focused = false;
        SDL_StopTextInput();
    }

    // 1. Title bar drag or [X] close (wy .. wy + 30)
    if (my >= wy && my <= wy + 30) {
        if (mx >= wx + ww - 32) {
            g_action_win.is_open = false;
        } else {
            g_action_win.is_dragging = true;
            g_action_win.drag_off_x = mx - wx;
            g_action_win.drag_off_y = my - wy;
        }
        return true;
    }

    // 2. Top Filter & Pin / Move Row (wy + 38 .. wy + 64)
    if (my >= wy + 38 && my <= wy + 64) {
        if (mx >= wx + 58 && mx <= wx + 286) {
            g_action_win.filter_focused = true;
            SDL_StartTextInput();
        } else if (mx >= wx + 292 && mx <= wx + 350) {
            // [Clear]
            g_action_win.filter_text[0] = '\0';
            g_action_win.scroll_y = 0;
            refresh_filtered_actions();
        } else if (mx >= wx + 356 && mx <= wx + 420) {
            // [Pin] -> Toggle Pin on selected action (brings to top & saves permanently)
            toggle_pin_selected_action();
        } else if (mx >= wx + 426 && mx <= wx + 464) {
            // [Up ^] -> Move selected action row up
            move_selected_action_row(-1);
            scroll_action_list_to_selected(g_action_win.win_rect.h - 244);
        } else if (mx >= wx + 470 && mx <= wx + 510) {
            // [Dn v] -> Move selected action row down
            move_selected_action_row(1);
            scroll_action_list_to_selected(g_action_win.win_rect.h - 244);
        } else if (mx >= wx + 516 && mx <= wx + 592) {
            // [Refresh] -> Rescan scripts folder for newly added .lua / .py scripts & save keyboard_shortcuts.json
            scan_scripts_directory_to_actions(&g_daw);
            save_keyboard_shortcuts_json();
            refresh_filtered_actions();
            snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                     "Refreshed scripts folder (%d actions ready)", g_num_actions);
        } else if (mx >= wx + 598 && mx <= wx + 766) {
            // [Find shortcut...]
            g_action_win.capturing_find_shortcut = true;
        }
        return true;
    }

    // 3. Main Table (Shortcut | Description) at { wx + 14, wy + 72, ww - 28, wh - 220 }
    int tbl_x = wx + 14;
    int tbl_y = wy + 72;
    int tbl_w = ww - 28;
    int tbl_h = wh - 220;
    int rows_y = tbl_y + 24;
    int rows_h = tbl_h - 24;
    int sb_x = tbl_x + tbl_w - 16;

    if (my >= rows_y && my < rows_y + rows_h && mx >= sb_x && mx <= tbl_x + tbl_w) {
        g_action_win.sb_dragging = true;
        g_action_win.sb_start_my = my;
        g_action_win.sb_orig_scroll = g_action_win.scroll_y;
        return true;
    }

    if (my >= rows_y && my < rows_y + rows_h && mx >= tbl_x && mx < sb_x) {
        int clicked_row = (my - (rows_y + 2) + g_action_win.scroll_y) / 22;
        if (clicked_row >= 0 && clicked_row < g_action_win.num_filtered) {
            int act_idx = g_action_win.filtered_indices[clicked_row];
            g_action_win.selected_action_idx = act_idx;
            g_action_win.selected_shortcut_sub = 0;
            if (clicks >= 2) {
                execute_daw_action_by_index(p, ren, win, act_idx);
            }
        }
        return true;
    }

    // 4. Bottom-Left Group Box: "Shortcuts for selected action" { wx + 14, wy + wh - 140, 560, 126 }
    int sub_box_x = wx + 26;
    int sub_box_y = wy + wh - 116;
    int sub_box_w = 424;
    int sub_box_h = 92;

    if (mx >= sub_box_x && mx <= sub_box_x + sub_box_w &&
        my >= sub_box_y && my <= sub_box_y + sub_box_h) {
        int sel = g_action_win.selected_action_idx;
        if (sel >= 0 && sel < g_num_actions) {
            int sub_r = (my - (sub_box_y + 4)) / 20;
            if (sub_r >= 0 && sub_r < g_actions[sel].num_shortcuts) {
                g_action_win.selected_shortcut_sub = sub_r;
            }
        }
        return true;
    }

    // [Add...] button inside "Shortcuts for selected action" { wx + 462, wy + wh - 116, 86, 32 }
    if (mx >= wx + 462 && mx <= wx + 548 && my >= wy + wh - 116 && my <= wy + wh - 84) {
        if (g_action_win.selected_action_idx >= 0 && g_action_win.selected_action_idx < g_num_actions) {
            g_action_win.capturing_add_shortcut = true;
        }
        return true;
    }

    // [Delete] button inside "Shortcuts for selected action" { wx + 462, wy + wh - 76, 86, 32 }
    if (mx >= wx + 462 && mx <= wx + 548 && my >= wy + wh - 76 && my <= wy + wh - 44) {
        int sel = g_action_win.selected_action_idx;
        if (sel >= 0 && sel < g_num_actions) {
            DawActionEntry *act = &g_actions[sel];
            int sub = g_action_win.selected_shortcut_sub;
            if (sub >= 0 && sub < act->num_shortcuts) {
                for (int s = sub; s < act->num_shortcuts - 1; s++) {
                    memcpy(act->shortcuts[s], act->shortcuts[s + 1], SHORTCUT_STR_LEN);
                }
                act->num_shortcuts--;
                if (g_action_win.selected_shortcut_sub >= act->num_shortcuts) {
                    g_action_win.selected_shortcut_sub = act->num_shortcuts > 0 ? act->num_shortcuts - 1 : 0;
                }
                rebuild_action_combined_shortcuts(act);
                if (act->linked_script_idx >= 0 && act->linked_script_idx < g_daw.num_scripts) {
                    if (act->num_shortcuts > 0) {
                        strncpy(g_daw.scripts[act->linked_script_idx].shortcut, act->shortcuts[0],
                                sizeof(g_daw.scripts[act->linked_script_idx].shortcut) - 1);
                    } else {
                        g_daw.scripts[act->linked_script_idx].shortcut[0] = '\0';
                    }
                }
                save_keyboard_shortcuts_json();
                snprintf(g_action_win.status_banner, sizeof(g_action_win.status_banner),
                         "Deleted shortcut & saved keyboard_shortcuts.json");
                record_project_change("Deleted Keyboard Shortcut", false);
            }
        }
        return true;
    }

    // 5. Bottom-Right Buttons: [New action...] and [Run]
    // [New action...] { wx + 590, wy + wh - 104, 176, 34 }
    if (mx >= wx + 590 && mx <= wx + 766 && my >= wy + wh - 104 && my <= wy + wh - 70) {
        import_script_into_action_system(false);
        return true;
    }

    // [Run] { wx + 590, wy + wh - 62, 176, 34 }
    if (mx >= wx + 590 && mx <= wx + 766 && my >= wy + wh - 62 && my <= wy + wh - 28) {
        if (g_action_win.selected_action_idx >= 0 && g_action_win.selected_action_idx < g_num_actions) {
            execute_daw_action_by_index(p, ren, win, g_action_win.selected_action_idx);
        }
        return true;
    }

    return true;
}

// Renders the REAPER-style "Actions" (Keyboard Shortcuts) popup window (matching image_c62680.png & image_c629c7.png)
static void render_action_window(SDL_Renderer *ren) {
    if (!g_action_win.is_open) return;

    SDL_Rect *wr = &g_action_win.win_rect;
    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
    SDL_Color c_white = { 228, 228, 228, 255 };
    SDL_Color c_muted = { 150, 145, 140, 255 };
    SDL_Color c_accent = get_theme_accent_sdl_color();

    // Drop Shadow
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Rect shadow = { wx + 6, wy + 6, ww, wh };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 175);
    SDL_RenderFillRect(ren, &shadow);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    // Main Window Background
    SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
    SDL_RenderFillRect(ren, wr);
    draw_brown_border_rect(ren, wr, true, false);

    // Title Bar
    SDL_Rect hdr = { wx, wy, ww, 30 };
    SDL_SetRenderDrawColor(ren, 42, 34, 28, 255);
    SDL_RenderFillRect(ren, &hdr);
    draw_brown_border_rect(ren, &hdr, false, false);
    draw_text(ren, "Actions", wx + 14, wy + 9, c_white);
    if (g_action_win.status_banner[0] != '\0') {
        SDL_Rect st_clip = { wx + 110, wy + 4, ww - 150, 22 };
        draw_text_clipped(ren, g_action_win.status_banner, wx + 110, wy + 9, c_accent, &st_clip, NULL);
    }
    draw_text(ren, "X", wx + ww - 20, wy + 9, c_white);

    // ---- Top Filter + Pin & Move Row ----
    draw_text(ren, "Filter", wx + 14, wy + 45, c_white);

    SDL_Rect flt_box = { wx + 58, wy + 38, 228, 26 };
    SDL_SetRenderDrawColor(ren, 16, 15, 14, 255);
    SDL_RenderFillRect(ren, &flt_box);
    draw_brown_border_rect(ren, &flt_box, g_action_win.filter_focused, false);

    SDL_Rect flt_clip = { flt_box.x + 4, flt_box.y + 2, flt_box.w - 8, flt_box.h - 4 };
    draw_text_clipped(ren, g_action_win.filter_text, flt_box.x + 6, wy + 45, c_white, &flt_clip, NULL);
    if (g_action_win.filter_focused && ((SDL_GetTicks() / 450) % 2 == 0)) {
        int cx = flt_box.x + 6 + (int)strlen(g_action_win.filter_text) * 8;
        if (cx < flt_box.x + flt_box.w - 4) {
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            SDL_RenderDrawLine(ren, cx, wy + 43, cx, wy + 59);
        }
    }

    SDL_Rect btn_clear = { wx + 292, wy + 38, 58, 26 };
    SDL_SetRenderDrawColor(ren, 44, 38, 34, 255);
    SDL_RenderFillRect(ren, &btn_clear);
    draw_brown_border_rect(ren, &btn_clear, false, false);
    draw_text(ren, "Clear", wx + 301, wy + 45, c_white);

    bool cur_pinned = (g_action_win.selected_action_idx >= 0 && g_action_win.selected_action_idx < g_num_actions &&
                       g_actions[g_action_win.selected_action_idx].is_pinned);
    SDL_Rect btn_pin = { wx + 356, wy + 38, 64, 26 };
    SDL_SetRenderDrawColor(ren, cur_pinned ? 74 : 44, cur_pinned ? 52 : 38, cur_pinned ? 30 : 34, 255);
    SDL_RenderFillRect(ren, &btn_pin);
    draw_brown_border_rect(ren, &btn_pin, cur_pinned, false);
    draw_pin_symbol_icon(ren, wx + 362, wy + 44, cur_pinned, cur_pinned ? c_accent : c_white);
    draw_text(ren, "Pin", wx + 378, wy + 45, c_white);

    SDL_Rect btn_up = { wx + 426, wy + 38, 38, 26 };
    SDL_SetRenderDrawColor(ren, 44, 38, 34, 255);
    SDL_RenderFillRect(ren, &btn_up);
    draw_brown_border_rect(ren, &btn_up, false, false);
    draw_text(ren, "Up^", wx + 433, wy + 45, c_white);

    SDL_Rect btn_dn = { wx + 470, wy + 38, 40, 26 };
    SDL_SetRenderDrawColor(ren, 44, 38, 34, 255);
    SDL_RenderFillRect(ren, &btn_dn);
    draw_brown_border_rect(ren, &btn_dn, false, false);
    draw_text(ren, "Dn v", wx + 474, wy + 45, c_white);

    SDL_Rect btn_opt = { wx + 516, wy + 38, 76, 26 };
    SDL_SetRenderDrawColor(ren, 44, 38, 34, 255);
    SDL_RenderFillRect(ren, &btn_opt);
    draw_brown_border_rect(ren, &btn_opt, false, false);
    draw_text(ren, "Refresh", wx + 526, wy + 45, c_white);

    SDL_Rect btn_find = { wx + 598, wy + 38, 168, 26 };
    SDL_SetRenderDrawColor(ren, 52, 42, 34, 255);
    SDL_RenderFillRect(ren, &btn_find);
    draw_brown_border_rect(ren, &btn_find, true, false);
    draw_text(ren, "Find shortcut...", wx + 618, wy + 45, c_white);

    // ---- Main Table: Shortcut | Description ----
    int tbl_x = wx + 14;
    int tbl_y = wy + 72;
    int tbl_w = ww - 28;
    int tbl_h = wh - 220;
    int col1_w = 190;

    SDL_Rect tbl_rect = { tbl_x, tbl_y, tbl_w, tbl_h };
    SDL_SetRenderDrawColor(ren, 18, 17, 16, 255);
    SDL_RenderFillRect(ren, &tbl_rect);
    draw_brown_border_rect(ren, &tbl_rect, false, false);

    // Table Header Bar
    SDL_Rect tbl_hdr = { tbl_x, tbl_y, tbl_w, 24 };
    SDL_SetRenderDrawColor(ren, 36, 30, 26, 255);
    SDL_RenderFillRect(ren, &tbl_hdr);
    draw_brown_border_rect(ren, &tbl_hdr, false, false);
    SDL_SetRenderDrawColor(ren, 152, 98, 54, 255);
    SDL_RenderDrawLine(ren, tbl_x + col1_w, tbl_y, tbl_x + col1_w, tbl_y + tbl_h);

    draw_text(ren, "Shortcut", tbl_x + 8, tbl_y + 6, c_white);
    draw_text(ren, "Description", tbl_x + col1_w + 10, tbl_y + 6, c_white);
    draw_text(ren, "^", tbl_x + col1_w + 260, tbl_y + 6, c_muted);

    // Table Rows
    int rows_y = tbl_y + 24;
    int rows_h = tbl_h - 24;
    int content_h = g_action_win.num_filtered * 22 + 6;
    int max_sc = (content_h > rows_h) ? (content_h - rows_h) : 0;
    if (g_action_win.scroll_y > max_sc) g_action_win.scroll_y = max_sc;
    if (g_action_win.scroll_y < 0) g_action_win.scroll_y = 0;

    SDL_Rect rows_clip = { tbl_x + 1, rows_y + 1, tbl_w - 17, rows_h - 2 };
    SDL_RenderSetClipRect(ren, &rows_clip);

    for (int r = 0; r < g_action_win.num_filtered; r++) {
        int ry = rows_y + 2 + r * 22 - g_action_win.scroll_y;
        if (ry + 22 < rows_y || ry > rows_y + rows_h) continue;

        int act_idx = g_action_win.filtered_indices[r];
        DawActionEntry *act = &g_actions[act_idx];
        bool is_sel = (act_idx == g_action_win.selected_action_idx);

        SDL_Rect row_bg = { tbl_x + 2, ry, tbl_w - 18, 21 };
        if (is_sel) {
            SDL_SetRenderDrawColor(ren, 52, 78, 86, 255);
            SDL_RenderFillRect(ren, &row_bg);
            draw_brown_border_rect(ren, &row_bg, false, false);
        } else if ((r & 1) == 1) {
            SDL_SetRenderDrawColor(ren, 22, 21, 20, 255);
            SDL_RenderFillRect(ren, &row_bg);
        }

        // Column 1: Shortcut(s)
        if (act->combined_shortcuts[0] != '\0') {
            SDL_Rect sc_clip = { tbl_x + 6, ry + 1, col1_w - 12, 19 };
            draw_text_clipped(ren, act->combined_shortcuts, tbl_x + 8, ry + 4,
                              is_sel ? c_white : c_accent, &sc_clip, &rows_clip);
        }

        // Column 2: Description (with Pin symbol when pinned)
        int desc_text_x = tbl_x + col1_w + 10;
        if (act->is_pinned) {
            draw_pin_symbol_icon(ren, tbl_x + col1_w + 8, ry + 4, true, c_accent);
            desc_text_x += 16;
        }
        SDL_Rect desc_clip = { tbl_x + col1_w + 6, ry + 1, tbl_w - col1_w - 26, 19 };
        draw_text_clipped(ren, act->description, desc_text_x, ry + 4,
                          act->is_pinned ? c_accent : c_white, &desc_clip, &rows_clip);
    }
    SDL_RenderSetClipRect(ren, NULL);

    // Vertical Scrollbar for Main Table
    SDL_Rect sb_track = { tbl_x + tbl_w - 15, rows_y + 1, 14, rows_h - 2 };
    SDL_SetRenderDrawColor(ren, 24, 22, 20, 255);
    SDL_RenderFillRect(ren, &sb_track);
    draw_brown_border_rect(ren, &sb_track, false, true);

    int thumb_h = (content_h > rows_h && content_h > 0)
                  ? fmax(24, (sb_track.h * rows_h) / content_h)
                  : sb_track.h;
    int thumb_y = sb_track.y + ((max_sc > 0) ? ((g_action_win.scroll_y * (sb_track.h - thumb_h)) / max_sc) : 0);
    SDL_Rect sb_thumb = { sb_track.x + 1, thumb_y, sb_track.w - 2, thumb_h };
    SDL_SetRenderDrawColor(ren, 92, 64, 42, 255);
    SDL_RenderFillRect(ren, &sb_thumb);
    draw_brown_border_rect(ren, &sb_thumb, false, false);

    // ---- Bottom-Left Group Box: "Shortcuts for selected action" ----
    SDL_Rect grp_box = { wx + 14, wy + wh - 140, 560, 126 };
    SDL_SetRenderDrawColor(ren, 22, 20, 19, 255);
    SDL_RenderFillRect(ren, &grp_box);
    draw_brown_border_rect(ren, &grp_box, false, false);
    draw_text(ren, "Shortcuts for selected action", wx + 24, wy + wh - 134, c_white);

    SDL_Rect sub_list = { wx + 26, wy + wh - 116, 424, 92 };
    SDL_SetRenderDrawColor(ren, 15, 14, 13, 255);
    SDL_RenderFillRect(ren, &sub_list);
    draw_brown_border_rect(ren, &sub_list, false, true);

    int sel_act = g_action_win.selected_action_idx;
    bool has_sel_shortcut = false;
    if (sel_act >= 0 && sel_act < g_num_actions) {
        DawActionEntry *act = &g_actions[sel_act];
        has_sel_shortcut = (act->num_shortcuts > 0);
        for (int s = 0; s < act->num_shortcuts; s++) {
            int sy = sub_list.y + 4 + s * 20;
            if (sy + 18 > sub_list.y + sub_list.h) break;
            bool sub_sel = (s == g_action_win.selected_shortcut_sub);
            if (sub_sel) {
                SDL_Rect s_row = { sub_list.x + 2, sy - 1, sub_list.w - 4, 19 };
                SDL_SetRenderDrawColor(ren, 18, 96, 192, 255);
                SDL_RenderFillRect(ren, &s_row);
                draw_brown_border_rect(ren, &s_row, false, false);
            }
            draw_text(ren, act->shortcuts[s], sub_list.x + 8, sy + 2, c_white);
        }

        // If selected action is an imported script with a file path, display its path at bottom of sub_list
        if (act->is_script && act->script_filepath[0] != '\0') {
            char path_info[300];
            snprintf(path_info, sizeof(path_info), "Path: %s", act->script_filepath);
            SDL_Rect p_clip = { sub_list.x + 4, sub_list.y + sub_list.h - 18, sub_list.w - 8, 16 };
            draw_text_clipped(ren, path_info, sub_list.x + 6, sub_list.y + sub_list.h - 16, c_muted, &p_clip, NULL);
        }
    }

    // [Add...] Button
    SDL_Rect btn_add = { wx + 462, wy + wh - 116, 86, 32 };
    SDL_SetRenderDrawColor(ren, 54, 42, 32, 255);
    SDL_RenderFillRect(ren, &btn_add);
    draw_brown_border_rect(ren, &btn_add, true, false);
    draw_text(ren, "Add...", wx + 484, wy + wh - 106, c_white);

    // [Delete] Button
    SDL_Rect btn_del = { wx + 462, wy + wh - 76, 86, 32 };
    SDL_SetRenderDrawColor(ren, has_sel_shortcut ? 48 : 30, has_sel_shortcut ? 36 : 28, has_sel_shortcut ? 28 : 26, 255);
    SDL_RenderFillRect(ren, &btn_del);
    draw_brown_border_rect(ren, &btn_del, false, !has_sel_shortcut);
    draw_text(ren, "Delete", wx + 481, wy + wh - 66, has_sel_shortcut ? c_white : c_muted);

    // ---- Bottom-Right Buttons: [New action...] and [Run] ----
    SDL_Rect btn_new_act = { wx + 590, wy + wh - 104, 176, 34 };
    SDL_SetRenderDrawColor(ren, 52, 40, 32, 255);
    SDL_RenderFillRect(ren, &btn_new_act);
    draw_brown_border_rect(ren, &btn_new_act, false, false);
    draw_text(ren, "New action...", wx + 626, wy + wh - 93, c_white);

    SDL_Rect btn_run = { wx + 590, wy + wh - 62, 176, 34 };
    SDL_SetRenderDrawColor(ren, 76, 52, 32, 255);
    SDL_RenderFillRect(ren, &btn_run);
    draw_brown_border_rect(ren, &btn_run, true, false);
    draw_text(ren, "Run", wx + 664, wy + wh - 51, c_white);

    // ---- Key Capture Modal Overlay (for [Add...] or [Find shortcut...]) ----
    if (g_action_win.capturing_add_shortcut || g_action_win.capturing_find_shortcut) {
        int mw = 430, mh = 134;
        int mx_box = wx + (ww - mw) / 2;
        int my_box = wy + (wh - mh) / 2;

        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_Rect dim = { wx, wy, ww, wh };
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 150);
        SDL_RenderFillRect(ren, &dim);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

        SDL_Rect mbox = { mx_box, my_box, mw, mh };
        SDL_SetRenderDrawColor(ren, 30, 26, 22, 255);
        SDL_RenderFillRect(ren, &mbox);
        draw_brown_border_rect(ren, &mbox, true, false);

        SDL_Rect mhdr = { mx_box, my_box, mw, 28 };
        SDL_SetRenderDrawColor(ren, 48, 36, 28, 255);
        SDL_RenderFillRect(ren, &mhdr);
        draw_brown_border_rect(ren, &mhdr, false, false);

        if (g_action_win.capturing_add_shortcut) {
            draw_text(ren, "Keyboard Shortcut - Add Shortcut", mx_box + 12, my_box + 8, c_white);
            draw_text(ren, "Press any key combination to assign to action:", mx_box + 16, my_box + 44, c_white);
        } else {
            draw_text(ren, "Find Keyboard Shortcut", mx_box + 12, my_box + 8, c_white);
            draw_text(ren, "Press any key combination to find matching action:", mx_box + 16, my_box + 44, c_white);
        }
        draw_text(ren, "(Saved permanently to keyboard_shortcuts.json)", mx_box + 16, my_box + 68, c_muted);

        SDL_Rect cancel_btn = { mx_box + mw - 106, my_box + mh - 40, 90, 28 };
        SDL_SetRenderDrawColor(ren, 48, 42, 38, 255);
        SDL_RenderFillRect(ren, &cancel_btn);
        draw_brown_border_rect(ren, &cancel_btn, false, false);
        draw_text(ren, "Cancel", mx_box + mw - 86, my_box + mh - 32, c_white);
    }
}

#endif // KEYBOARD_ACTIONS_H
#endif

// Returns true if any modal popup is open OR if the mouse cursor (mx, my) is over any open popup window or dropdown menu.
// Used to keep mouse cursor and focus strictly on the active popup/menu and never on the background panel.
static bool is_cursor_captured_by_popup_or_menu(ProjectTab *p, int mx, int my, SDL_Cursor **out_cursor) {
    if (out_cursor) *out_cursor = g_cursor_arrow;

    if (g_daw.save_prompt_open) return true;
    if (g_track_delete_prompt_open) return true;
    if (g_processing_popup_open) return true;
    if (g_multi_import.is_open) return true;
    if (g_dyn_split.is_open && point_in_rect(mx, my, &g_dyn_split.win_rect)) return true;
    if (g_reposition.is_open && point_in_rect(mx, my, &g_reposition.win_rect)) return true;
    if (g_script_popup.is_open && point_in_rect(mx, my, &g_script_popup.win_rect)) return true;

    if (g_export.is_open) {
        if (out_cursor) {
            int ex = g_export.win_rect.x, ey = g_export.win_rect.y;
            bool over_text = false;
            if (my >= ey + 98 && my <= ey + 124 && mx >= ex + 130 && mx <= ex + 545) over_text = true;
            else if (my >= ey + 132 && my <= ey + 158 && mx >= ex + 130 && mx <= ex + 545) over_text = true;
            else if (g_export.format == EXPORT_FMT_MP4 && !g_export.mp4_aac_dropdown_open) {
                int sy_sub = ey + 278;
                int r1_y = sy_sub + 8;
                int r2_y = sy_sub + 46;
                if (my >= r1_y && my <= r1_y + 26 &&
                    ((mx >= ex + 295 && mx <= ex + 365) || (mx >= ex + 390 && mx <= ex + 460) || (mx >= ex + 568 && mx <= ex + 648))) {
                    over_text = true;
                } else if (my >= r2_y && my <= r2_y + 26 && mx >= ex + 295 && mx <= ex + 395) {
                    over_text = true;
                }
            }
            if (over_text) *out_cursor = g_cursor_ibeam;
        }
        return true;
    }

    if (g_action_win.is_open) {
        if (g_action_win.capturing_add_shortcut || g_action_win.capturing_find_shortcut) {
            return true;
        }
        if (point_in_rect(mx, my, &g_action_win.win_rect)) {
            if (out_cursor) {
                int wx = g_action_win.win_rect.x, wy = g_action_win.win_rect.y;
                if (mx >= wx + 62 && mx <= wx + 412 && my >= wy + 38 && my <= wy + 64) {
                    *out_cursor = g_cursor_ibeam;
                }
            }
            return true;
        }
    }
    if (g_theme.is_open && point_in_rect(mx, my, &g_theme.win_rect)) {
        if (out_cursor) {
            int wx = g_theme.win_rect.x, wy = g_theme.win_rect.y;
            if (mx >= wx + 18 && mx <= wx + 230 && my >= wy + 284 && my <= wy + 314) {
                *out_cursor = g_cursor_ibeam;
            }
        }
        return true;
    }
    if (g_hide_popup_open && point_in_rect(mx, my, &g_hide_win_rect)) return true;
    if (g_history_popup_open && point_in_rect(mx, my, &g_history_win_rect)) return true;
    if (g_daw.script_manager_open) {
        VideoResizeEdge sc_edge = hit_test_video_resize_edge(&g_daw.script_win_rect, mx, my);
        if (sc_edge != VID_RESIZE_NONE) {
            if (out_cursor) {
                *out_cursor = (sc_edge == VID_RESIZE_BOTTOM) ? g_cursor_size_ns : g_cursor_size_we;
            }
            return true;
        }
        if (point_in_rect(mx, my, &g_daw.script_win_rect)) return true;
    }

    if (p && p->video.is_open) {
        VideoResizeEdge v_edge = g_video_fullscreen[get_valid_tab_idx()]
                                 ? VID_RESIZE_NONE
                                 : hit_test_video_resize_edge(&p->video.rect, mx, my);
        if (v_edge != VID_RESIZE_NONE) {
            if (out_cursor) {
                *out_cursor = (v_edge == VID_RESIZE_BOTTOM) ? g_cursor_size_ns : g_cursor_size_we;
            }
            return true;
        }
        if (point_in_rect(mx, my, &p->video.rect)) {
            return true;
        }
    }

    int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
    if (g_daw.menu_file_open) {
        SDL_Rect f_box = { 8, m_start, 196, 126 };
        if (point_in_rect(mx, my, &f_box)) return true;
    }
    if (g_daw.menu_action_open) {
        SDL_Rect a_box = { 60, m_start, 178, 135 };
        if (point_in_rect(mx, my, &a_box)) return true;
    }
    if (g_menu_tools_open) {
        SDL_Rect t_box = { 126, m_start, 150, 108 };
        if (point_in_rect(mx, my, &t_box)) return true;
        if (g_submenu_tools_record_open) {
            SDL_Rect r_sub = { 276, m_start + 80, 146, 56 };
            if (point_in_rect(mx, my, &r_sub)) return true;
        }
    }
    if (g_marker_context_menu_open) {
        SDL_Rect cm_box = { g_marker_context_x, g_marker_context_y, 128, 28 };
        if (point_in_rect(mx, my, &cm_box)) return true;
    }

    return false;
}

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return 1;

    SDL_Window *win = SDL_CreateWindow(
        "Audio Track",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

#ifdef _WIN32
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(win, &wmInfo)) {
        HWND hwnd = wmInfo.info.win.window;
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));
    }
#endif

    g_cursor_arrow   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    g_cursor_size_we = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    g_cursor_size_ns = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    g_cursor_ibeam   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);

    memset(&g_daw, 0, sizeof(DawEngine));
    memset(g_marker_full_text, 0, sizeof(g_marker_full_text));
    memset(g_editing_marker_full_buf, 0, sizeof(g_editing_marker_full_buf));
    memset(g_item_fade_in, 0, sizeof(g_item_fade_in));
    memset(g_item_fade_out, 0, sizeof(g_item_fade_out));
    memset(g_item_vol_db, 0, sizeof(g_item_vol_db));
    memset(g_item_muted, 0, sizeof(g_item_muted));
    memset(g_item_color_idx, 0, sizeof(g_item_color_idx));
    memset(g_item_group_id, 0, sizeof(g_item_group_id));
    memset(g_track_hidden, 0, sizeof(g_track_hidden));
    memset(g_tab_history, 0, sizeof(g_tab_history));
    memset(g_tab_history_count, 0, sizeof(g_tab_history_count));
    memset(g_tab_notification, 0, sizeof(g_tab_notification));

    g_daw.num_tabs = 1;
    g_daw.active_tab = 0;
    g_daw.tcp_width = 200;
    g_daw.save_prompt_open = false;
    g_daw.save_prompt_tab_idx = -1;
    g_daw.save_prompt_quit_app = false;
    g_daw.input_focus = INPUT_FOCUS_NONE;
    g_daw.editing_track_name_idx = -1;
    g_daw.editing_marker_idx = -1;
    g_daw.script_selected_idx = -1;
    g_daw.script_win_rect = (SDL_Rect){ 360, 120, 560, 380 };

    set_daw_window_thumbnail_icon(win);
    init_default_recording_folder();
    init_tab_defaults(0, "Project 1");
    init_keyboard_actions_system();
    push_undo_state();
    g_daw.tabs[0].is_dirty = false;
    record_project_change("Project Initialized", false);

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_mixer_callback;
    want.userdata = &g_daw;
    g_daw.audio_device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (g_daw.audio_device > 0) SDL_PauseAudioDevice(g_daw.audio_device, 0);

    bool running = true;
    bool dragging_scrollbar_h = false, dragging_scrollbar_v = false;
    int scrollbar_h_start_mx = 0, scrollbar_v_start_my = 0;
    double orig_scroll_x = 0; int orig_scroll_y = 0;
    bool scrubbing_ruler = false;

    // Logic Pro style Cycle Range strip dragging state
    bool dragging_range_left = false;
    bool dragging_range_right = false;
    bool dragging_range_body = false;
    double range_drag_start_mouse_sec = 0.0;
    double range_orig_start_sec = 0.0;
    double range_orig_end_sec = 0.0;

    bool marquee_selecting = false;
    int marquee_start_x = 0, marquee_start_y = 0;
    int marquee_cur_x = 0, marquee_cur_y = 0;

    int dragging_item_idx = -1;
    bool dragging_trim_left = false, dragging_trim_right = false;
    bool dragging_fade_in = false, dragging_fade_out = false;
    double drag_start_mouse_time = 0.0, item_orig_start = 0.0, item_orig_dur = 0.0, item_orig_offset = 0.0;

    bool resizing_tcp_width = false;
    int resizing_track_idx = -1;
    int track_orig_height = 0;
    int mouse_drag_start_y = 0;

    // Track Header Up/Down Reorder Drag State
    int dragging_track_order_idx = -1;
    bool dragging_track_order_pushed_undo = false;

    SDL_Event e;
    while (running) {
        ProjectTab *p = get_cur_tab();
        int win_w, win_h;
        SDL_GetWindowSize(win, &win_w, &win_h);

        int m_lane_h = get_marker_height(p);
        int view_w = win_w - g_daw.tcp_width - SCROLLBAR_SIZE;
        int view_h = win_h - TABBAR_HEIGHT - TOPBAR_HEIGHT - RULER_HEIGHT - m_lane_h - SCROLLBAR_SIZE;
        int max_h_tracks = get_total_tracks_height();

        for (int i = 0; i < p->num_items; i++) {
            double end_t = p->items[i].start_time + p->items[i].duration + 60.0;
            if (end_t > p->project_duration_sec) p->project_duration_sec = end_t;
        }
        int total_w_timeline = (int)(p->project_duration_sec * p->pixels_per_second);

        static double s_last_observed_playhead[MAX_TABS] = { -1.0 };
        int cur_tab_chk = get_valid_tab_idx();
        if (fabs(p->playhead_seconds - s_last_observed_playhead[cur_tab_chk]) > 1e-5) {
            ensure_playhead_visible_in_viewport(p, view_w);
            s_last_observed_playhead[cur_tab_chk] = p->playhead_seconds;
        }
        if (g_processing_popup_open && g_processing_close_at_ticks > 0 &&
            SDL_GetTicks() >= g_processing_close_at_ticks) {
            g_processing_popup_open = false;
            g_processing_close_at_ticks = 0;
        }

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                bool has_any_dirty = false;
                for (int t = 0; t < g_daw.num_tabs; t++) {
                    if (g_daw.tabs[t].is_dirty) {
                        has_any_dirty = true;
                        g_daw.save_prompt_tab_idx = t;
                        break;
                    }
                }
                if (has_any_dirty) {
                    g_daw.save_prompt_open = true;
                    g_daw.save_prompt_quit_app = true;
                } else {
                    running = false;
                }
            } else if (e.type == SDL_MOUSEWHEEL) {
                int mx, my; SDL_GetMouseState(&mx, &my);
                if (g_daw.save_prompt_open || g_export.is_open || g_multi_import.is_open) continue;
                if (g_hide_popup_open && point_in_rect(mx, my, &g_hide_win_rect)) {
                    int list_h = g_hide_win_rect.h - 106;
                    int content_h = p->num_tracks * 26 + 8;
                    int max_sc = (content_h > list_h) ? (content_h - list_h) : 0;
                    g_hide_scroll_y -= e.wheel.y * 26;
                    if (g_hide_scroll_y < 0) g_hide_scroll_y = 0;
                    if (g_hide_scroll_y > max_sc) g_hide_scroll_y = max_sc;
                    continue;
                }
                if (g_action_win.is_open && point_in_rect(mx, my, &g_action_win.win_rect)) {
                    int rows_h = (g_action_win.win_rect.h - 220) - 24;
                    int content_h = g_action_win.num_filtered * 22 + 6;
                    int max_sc = (content_h > rows_h) ? (content_h - rows_h) : 0;
                    g_action_win.scroll_y -= e.wheel.y * 44;
                    if (g_action_win.scroll_y < 0) g_action_win.scroll_y = 0;
                    if (g_action_win.scroll_y > max_sc) g_action_win.scroll_y = max_sc;
                    continue;
                }
                if (g_daw.script_manager_open && point_in_rect(mx, my, &g_daw.script_win_rect)) {
                    int list_h = g_daw.script_win_rect.h - 82;
                    int content_h = g_daw.num_scripts * 24 + 8;
                    int max_sc = (content_h > list_h) ? (content_h - list_h) : 0;
                    g_script_scroll_y -= e.wheel.y * 28;
                    if (g_script_scroll_y < 0) g_script_scroll_y = 0;
                    if (g_script_scroll_y > max_sc) g_script_scroll_y = max_sc;
                    continue;
                }
                if (g_theme.is_open && point_in_rect(mx, my, &g_theme.win_rect)) {
                    int list_h = 316;
                    int content_h = g_theme.num_saved * 28 + 8;
                    int max_sc = (content_h > list_h) ? (content_h - list_h) : 0;
                    g_theme.saved_scroll_y -= e.wheel.y * 28;
                    if (g_theme.saved_scroll_y < 0) g_theme.saved_scroll_y = 0;
                    if (g_theme.saved_scroll_y > max_sc) g_theme.saved_scroll_y = max_sc;
                    continue;
                }
                if (g_history_popup_open && point_in_rect(mx, my, &g_history_win_rect)) {
                    int tab_i = get_valid_tab_idx();
                    int list_h = g_history_win_rect.h - 86;
                    int content_h = g_tab_history_count[tab_i] * 24 + 8;
                    int max_sc = (content_h > list_h) ? (content_h - list_h) : 0;
                    g_history_scroll_y -= e.wheel.y * 24;
                    if (g_history_scroll_y < 0) g_history_scroll_y = 0;
                    if (g_history_scroll_y > max_sc) g_history_scroll_y = max_sc;
                    continue;
                }
                if (is_cursor_captured_by_popup_or_menu(p, mx, my, NULL)) continue;
                SDL_Keymod mod = SDL_GetModState();
                if (mod & KMOD_CTRL) {
                    // Ctrl+Mousewheel: View: Zoom vertically (mousewheel)
                    for (int t = 0; t < p->num_tracks; t++) {
                        int nh = p->track_heights[t] + e.wheel.y * 10;
                        if (nh < 50) nh = 50;
                        if (nh > 260) nh = 260;
                        p->track_heights[t] = nh;
                    }
                } else if ((mod & KMOD_ALT) || (mod & KMOD_SHIFT)) {
                    // Alt+Mousewheel: View: Scroll horizontally (mousewheel)
                    p->scroll_x_sec -= e.wheel.y * (40.0f / p->pixels_per_second);
                    if (p->scroll_x_sec < 0) p->scroll_x_sec = 0;
                } else if (mx < g_daw.tcp_width) {
                    // Mousewheel over TCP: Vertical track scroll
                    p->scroll_y_px -= e.wheel.y * 36;
                    int max_y_scroll = max_h_tracks - view_h;
                    if (max_y_scroll < 0) max_y_scroll = 0;
                    if (p->scroll_y_px < 0) p->scroll_y_px = 0;
                    if (p->scroll_y_px > max_y_scroll) p->scroll_y_px = max_y_scroll;
                } else {
                    // Mousewheel over timeline: View: Zoom horizontally (mousewheel)
                    double mouse_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                    p->pixels_per_second += e.wheel.y * 12.0f;
                    if (p->pixels_per_second < 15.0f) p->pixels_per_second = 15.0f;
                    if (p->pixels_per_second > 400.0f) p->pixels_per_second = 400.0f;
                    p->scroll_x_sec = mouse_time - (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                    if (p->scroll_x_sec < 0) p->scroll_x_sec = 0;
                }
            } else if (e.type == SDL_TEXTINPUT) {
                if (g_daw.save_prompt_open) continue;
                if (g_script_popup.is_open && g_script_popup.focused_ctrl_idx >= 0 &&
                    g_script_popup.focused_ctrl_idx < g_script_popup.num_controls) {
                    ScriptPopupControl *ctrl = &g_script_popup.controls[g_script_popup.focused_ctrl_idx];
                    if (strlen(ctrl->str_val) + strlen(e.text.text) < sizeof(ctrl->str_val) - 1) {
                        strcat(ctrl->str_val, e.text.text);
                        ctrl->num_val = atof(ctrl->str_val);
                    }
                    continue;
                }
                if (g_reposition.is_open && g_reposition.interval_focused) {
                    if (strlen(g_reposition.interval_str) + strlen(e.text.text) < sizeof(g_reposition.interval_str) - 1) {
                        strcat(g_reposition.interval_str, e.text.text);
                    }
                    continue;
                }
                if (g_dyn_split.is_open && g_dyn_split.editing_field_id > 0) {
                    if (strlen(g_dyn_split.edit_buf) + strlen(e.text.text) < sizeof(g_dyn_split.edit_buf) - 1) {
                        strcat(g_dyn_split.edit_buf, e.text.text);
                    }
                    continue;
                }
                if (g_daw.script_manager_open && g_script_filter_focused) {
                    if (strlen(g_script_filter_text) + strlen(e.text.text) < sizeof(g_script_filter_text) - 1) {
                        strcat(g_script_filter_text, e.text.text);
                        g_script_scroll_y = 0;
                        refresh_filtered_scripts();
                        if (g_script_num_filtered > 0) {
                            g_daw.script_selected_idx = g_script_filtered_indices[0];
                        }
                    }
                    continue;
                }
                if (g_theme.is_open && g_theme.code_focused) {
                    if (strlen(g_theme.code_buf) + strlen(e.text.text) < sizeof(g_theme.code_buf) - 1) {
                        strcat(g_theme.code_buf, e.text.text);
                        if (parse_theme_code_input(g_theme.code_buf)) {
                            save_themes_settings_json();
                        }
                    }
                    continue;
                }
                if (g_action_win.is_open && g_action_win.filter_focused &&
                    !g_action_win.capturing_add_shortcut && !g_action_win.capturing_find_shortcut) {
                    if (strlen(g_action_win.filter_text) + strlen(e.text.text) < sizeof(g_action_win.filter_text) - 1) {
                        strcat(g_action_win.filter_text, e.text.text);
                        g_action_win.scroll_y = 0;
                        refresh_filtered_actions();
                        if (g_action_win.num_filtered > 0) {
                            g_action_win.selected_action_idx = g_action_win.filtered_indices[0];
                            g_action_win.selected_shortcut_sub = 0;
                        }
                    }
                    continue;
                }
                if (g_export.is_open && g_export.active_field != EXP_FIELD_NONE) {
                    size_t max_len = 0;
                    char *buf = get_active_export_field_buf(&max_len);
                    if (buf && strlen(buf) + strlen(e.text.text) < max_len) {
                        strcat(buf, e.text.text);
                    }
                    continue;
                }
                if (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME) {
                    if (strlen(g_daw.editing_name_buf) + strlen(e.text.text) < 63) {
                        strcat(g_daw.editing_name_buf, e.text.text);
                    }
                } else if (g_daw.input_focus == INPUT_FOCUS_MARKER) {
                    size_t cur_len = strlen(g_editing_marker_full_buf);
                    size_t add_len = strlen(e.text.text);
                    if (g_marker_cursor_pos < 0) g_marker_cursor_pos = 0;
                    if ((size_t)g_marker_cursor_pos > cur_len) g_marker_cursor_pos = (int)cur_len;
                    if (cur_len + add_len < MARKER_TEXT_MAX - 1) {
                        memmove(g_editing_marker_full_buf + g_marker_cursor_pos + add_len,
                                g_editing_marker_full_buf + g_marker_cursor_pos,
                                cur_len - (size_t)g_marker_cursor_pos + 1);
                        memcpy(g_editing_marker_full_buf + g_marker_cursor_pos, e.text.text, add_len);
                        g_marker_cursor_pos += (int)add_len;
                        strncpy(g_daw.editing_marker_buf, g_editing_marker_full_buf, 63);
                        g_daw.editing_marker_buf[63] = '\0';
                    }
                }
            } else if (e.type == SDL_DROPFILE) {
                if (g_daw.save_prompt_open || g_export.is_open || g_multi_import.is_open) {
                    SDL_free(e.drop.file);
                    continue;
                }
                char *file = e.drop.file;
                if (file && file[0] && g_drop_batch_count < MAX_MULTI_IMPORT_FILES) {
                    if (g_drop_batch_count == 0) {
                        int my = 0; SDL_GetMouseState(NULL, &my);
                        int trk = 0;
                        int cur_y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT + m_lane_h - p->scroll_y_px;
                        for (int t = 0; t < p->num_tracks; t++) {
                            if (is_track_hidden(t)) continue;
                            if (my >= cur_y && my < cur_y + p->track_heights[t]) { trk = t; break; }
                            cur_y += p->track_heights[t];
                        }
                        g_drop_batch_track = trk;
                        g_drop_batch_time = p->playhead_seconds;
                    }
                    strncpy(g_drop_batch_files[g_drop_batch_count], file, 511);
                    g_drop_batch_files[g_drop_batch_count][511] = '\0';
                    g_drop_batch_count++;
                }
                SDL_free(file);
            } else if (e.type == SDL_KEYDOWN) {
                if (g_track_delete_prompt_open) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_track_delete_prompt_open = false;
                    } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        execute_confirmed_track_delete(p);
                    }
                    continue;
                }
                if (g_daw.save_prompt_open) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_save_prompt_open_project_after = false;
                        handle_save_modal_choice(3, &running);
                    }
                    continue;
                }
                if (g_script_popup.is_open) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_script_popup.is_open = false;
                        SDL_StopTextInput();
                        continue;
                    }
                    if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        g_script_popup.is_open = false;
                        SDL_StopTextInput();
                        char custom_msg[160] = {0};
                        interpret_custom_script_file(p, ren, win, g_script_popup.script_filepath, custom_msg, sizeof(custom_msg));
                        record_project_change(custom_msg[0] ? custom_msg : g_script_popup.title, true);
                        continue;
                    }
                    if (e.key.keysym.sym == SDLK_BACKSPACE && g_script_popup.focused_ctrl_idx >= 0 &&
                        g_script_popup.focused_ctrl_idx < g_script_popup.num_controls) {
                        char *sb = g_script_popup.controls[g_script_popup.focused_ctrl_idx].str_val;
                        size_t l = strlen(sb);
                        if (l > 0) sb[l - 1] = '\0';
                        g_script_popup.controls[g_script_popup.focused_ctrl_idx].num_val = atof(sb);
                        continue;
                    }
                    continue;
                }
                if (g_reposition.is_open) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_reposition.is_open = false;
                        g_reposition.interval_focused = false;
                        SDL_StopTextInput();
                        continue;
                    }
                    if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        g_reposition.interval_focused = false;
                        SDL_StopTextInput();
                        execute_reposition_items_dialog(p);
                        continue;
                    }
                    if (e.key.keysym.sym == SDLK_BACKSPACE && g_reposition.interval_focused) {
                        size_t l = strlen(g_reposition.interval_str);
                        if (l > 0) g_reposition.interval_str[l - 1] = '\0';
                        continue;
                    }
                    continue;
                }
                if (g_dyn_split.is_open) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        if (g_dyn_split.editing_field_id > 0) {
                            g_dyn_split.editing_field_id = 0;
                            SDL_StopTextInput();
                        } else {
                            g_dyn_split.is_open = false;
                        }
                        continue;
                    }
                    if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        if (g_dyn_split.editing_field_id > 0) {
                            commit_dyn_split_field_edit();
                        } else {
                            execute_dynamic_split_items(p, ren, win);
                        }
                        continue;
                    }
                    if (e.key.keysym.sym == SDLK_BACKSPACE && g_dyn_split.editing_field_id > 0) {
                        size_t l = strlen(g_dyn_split.edit_buf);
                        if (l > 0) g_dyn_split.edit_buf[l - 1] = '\0';
                        continue;
                    }
                    continue;
                }
                if (g_daw.script_manager_open && g_script_filter_focused) {
                    if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN) {
                        g_script_filter_focused = false;
                        SDL_StopTextInput();
                    } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                        size_t l = strlen(g_script_filter_text);
                        if (l > 0) {
                            g_script_filter_text[l - 1] = '\0';
                            g_script_scroll_y = 0;
                            refresh_filtered_scripts();
                            if (g_script_num_filtered > 0) {
                                g_daw.script_selected_idx = g_script_filtered_indices[0];
                            }
                        }
                    }
                    continue;
                }
                if (g_theme.is_open && g_theme.code_focused) {
                    if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        if (parse_theme_code_input(g_theme.code_buf)) {
                            save_themes_settings_json();
                            snprintf(g_theme.status_msg, sizeof(g_theme.status_msg),
                                     "Applied & saved %s to themes_settings.json", g_theme.code_buf);
                        }
                        g_theme.code_focused = false;
                        SDL_StopTextInput();
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                        sync_theme_code_string();
                        g_theme.code_focused = false;
                        SDL_StopTextInput();
                    } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                        size_t l = strlen(g_theme.code_buf);
                        if (l > 0) g_theme.code_buf[l - 1] = '\0';
                    }
                    continue;
                }
                if (g_multi_import.is_open) {
                    if (e.key.keysym.sym == SDLK_RETURN) {
                        execute_multi_media_import(p, ren, win);
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_multi_import.is_open = false;
                        g_multi_import.num_files = 0;
                    }
                    continue;
                }
                if (g_export.is_open) {
                    if (g_export.active_field != EXP_FIELD_NONE) {
                        size_t max_len = 0;
                        char *buf = get_active_export_field_buf(&max_len);
                        if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_ESCAPE) {
                            g_export.active_field = EXP_FIELD_NONE;
                            SDL_StopTextInput();
                        } else if (e.key.keysym.sym == SDLK_BACKSPACE && buf) {
                            size_t l = strlen(buf);
                            if (l > 0) buf[l - 1] = '\0';
                        }
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_export.is_open = false;
                    }
                    continue;
                }
                SDL_Keymod mod = SDL_GetModState();
                if (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME) {
                    if (e.key.keysym.sym == SDLK_RETURN) {
                        strncpy(p->track_names[g_daw.editing_track_name_idx], g_daw.editing_name_buf, 63);
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_track_name_idx = -1;
                        SDL_StopTextInput();
                        record_project_change("Renamed Track", true);
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_track_name_idx = -1;
                        SDL_StopTextInput();
                    } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                        size_t l = strlen(g_daw.editing_name_buf);
                        if (l > 0) g_daw.editing_name_buf[l - 1] = '\0';
                    }
                    continue;
                }
                if (g_daw.input_focus == INPUT_FOCUS_MARKER) {
                    size_t l = strlen(g_editing_marker_full_buf);
                    if (g_marker_cursor_pos < 0) g_marker_cursor_pos = 0;
                    if ((size_t)g_marker_cursor_pos > l) g_marker_cursor_pos = (int)l;

                    if (e.key.keysym.sym == SDLK_RETURN) {
                        set_marker_full_str(p, g_daw.editing_marker_idx, g_editing_marker_full_buf);
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_marker_idx = -1;
                        SDL_StopTextInput();
                        record_project_change("Edited Marker Text", true);
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_marker_idx = -1;
                        SDL_StopTextInput();
                    } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                        if (g_marker_cursor_pos > 0 && l > 0) {
                            memmove(g_editing_marker_full_buf + g_marker_cursor_pos - 1,
                                    g_editing_marker_full_buf + g_marker_cursor_pos,
                                    l - (size_t)g_marker_cursor_pos + 1);
                            g_marker_cursor_pos--;
                            strncpy(g_daw.editing_marker_buf, g_editing_marker_full_buf, 63);
                            g_daw.editing_marker_buf[63] = '\0';
                        }
                    } else if (e.key.keysym.sym == SDLK_DELETE) {
                        if ((size_t)g_marker_cursor_pos < l) {
                            memmove(g_editing_marker_full_buf + g_marker_cursor_pos,
                                    g_editing_marker_full_buf + g_marker_cursor_pos + 1,
                                    l - (size_t)g_marker_cursor_pos);
                            strncpy(g_daw.editing_marker_buf, g_editing_marker_full_buf, 63);
                            g_daw.editing_marker_buf[63] = '\0';
                        }
                    } else if (e.key.keysym.sym == SDLK_LEFT) {
                        if (g_marker_cursor_pos > 0) g_marker_cursor_pos--;
                    } else if (e.key.keysym.sym == SDLK_RIGHT) {
                        if ((size_t)g_marker_cursor_pos < l) g_marker_cursor_pos++;
                    } else if (e.key.keysym.sym == SDLK_HOME) {
                        g_marker_cursor_pos = 0;
                    } else if (e.key.keysym.sym == SDLK_END) {
                        g_marker_cursor_pos = (int)l;
                    }
                    continue;
                }

                if (e.key.keysym.sym == SDLK_ESCAPE && p->video.is_open && g_video_fullscreen[get_valid_tab_idx()]) {
                    int v_tab = get_valid_tab_idx();
                    g_video_fullscreen[v_tab] = false;
                    if (g_video_saved_rect[v_tab].w >= 200 && g_video_saved_rect[v_tab].h >= 120) {
                        p->video.rect = g_video_saved_rect[v_tab];
                    } else {
                        p->video.rect = (SDL_Rect){ 680, 110, 480, 294 };
                    }
                    continue;
                }
                if (g_action_win.is_open) {
                    if (handle_action_window_keydown(p, ren, win, e.key.keysym.sym, mod)) {
                        continue;
                    }
                }

                // Dispatch all built-in and customized keyboard shortcuts through the unified Action & Shortcut Engine
                if (handle_global_keyboard_shortcut(p, ren, win, e.key.keysym.sym, mod)) {
                    continue;
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x, my = e.button.y;
                SDL_Keymod mod = SDL_GetModState();
                bool shift_pressed = (mod & KMOD_SHIFT);
                bool ctrl_pressed = (mod & KMOD_CTRL);

                if (g_track_delete_prompt_open) {
                    int dia_w = 390, dia_h = 140;
                    int dia_x = (win_w - dia_w) / 2;
                    int dia_y = (win_h - dia_h) / 2;
                    int btn_y = dia_y + dia_h - 44;
                    if (mx >= dia_x + 70 && mx <= dia_x + 186 && my >= btn_y && my <= btn_y + 30) {
                        execute_confirmed_track_delete(p);
                    } else if (mx >= dia_x + 204 && mx <= dia_x + 320 && my >= btn_y && my <= btn_y + 30) {
                        g_track_delete_prompt_open = false;
                    }
                    continue;
                }

                if (g_daw.save_prompt_open) {
                    int dia_w = 380, dia_h = 140;
                    int dia_x = (win_w - dia_w) / 2;
                    int dia_y = (win_h - dia_h) / 2;
                    int btn_y = dia_y + dia_h - 44;
                    bool open_proj_after = g_save_prompt_open_project_after;

                    // [Save]
                    if (mx >= dia_x + 18 && mx <= dia_x + 118 && my >= btn_y && my <= btn_y + 30) {
                        int t_idx = g_daw.save_prompt_tab_idx;
                        g_daw.save_prompt_open = false;
                        g_daw.save_prompt_tab_idx = -1;
                        g_daw.save_prompt_quit_app = false;
                        g_save_prompt_open_project_after = false;
                        if (t_idx >= 0 && t_idx < g_daw.num_tabs) {
                            daw_save_project_dialog(&g_daw.tabs[t_idx], false);
                        }
                        if (open_proj_after) {
                            daw_load_project_dialog(p, ren, win);
                        }
                        continue;
                    }
                    // [Don't Save]
                    if (mx >= dia_x + 130 && mx <= dia_x + 250 && my >= btn_y && my <= btn_y + 30) {
                        g_save_prompt_open_project_after = false;
                        if (open_proj_after) {
                            g_daw.save_prompt_open = false;
                            g_daw.save_prompt_tab_idx = -1;
                            g_daw.save_prompt_quit_app = false;
                            p->is_dirty = false;
                            daw_load_project_dialog(p, ren, win);
                        } else {
                            handle_save_modal_choice(2, &running);
                        }
                        continue;
                    }
                    // [Cancel]
                    if (mx >= dia_x + 262 && mx <= dia_x + 362 && my >= btn_y && my <= btn_y + 30) {
                        g_save_prompt_open_project_after = false;
                        handle_save_modal_choice(3, &running);
                        continue;
                    }
                    continue;
                }

                // ================= LUA & PYTHON SCRIPT POPUP WINDOW INTERACTIONS =================
                if (g_script_popup.is_open) {
                    SDL_Rect *wr = &g_script_popup.win_rect;
                    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
                    if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + 30) {
                        if (mx >= wx + ww - 28) {
                            g_script_popup.is_open = false;
                            SDL_StopTextInput();
                        } else {
                            g_script_popup.is_dragging = true;
                            g_script_popup.drag_off_x = mx - wx;
                            g_script_popup.drag_off_y = my - wy;
                        }
                        continue;
                    }
                    if (mx >= wx && mx <= wx + ww && my >= wy + 30 && my <= wy + wh) {
                        g_script_popup.focused_ctrl_idx = -1;
                        SDL_StopTextInput();
                        for (int i = 0; i < g_script_popup.num_controls; i++) {
                            ScriptPopupControl *ctrl = &g_script_popup.controls[i];
                            int cy = wy + 46 + i * 38;
                            if (my >= cy && my <= cy + 28) {
                                if (ctrl->type == SCP_CTRL_CHECKBOX && mx >= wx + 20 && mx <= wx + 240) {
                                    ctrl->bool_val = !ctrl->bool_val;
                                    ctrl->num_val = ctrl->bool_val ? 1.0 : 0.0;
                                } else if (ctrl->type == SCP_CTRL_SLIDER) {
                                    int sl_x = wx + 180, sl_w = ww - 270;
                                    if (sl_w < 80) sl_w = 80;
                                    if (mx >= sl_x && mx <= sl_x + sl_w) {
                                        g_script_popup.dragging_slider_idx = i;
                                        double u = (double)(mx - sl_x) / (double)(sl_w > 1 ? sl_w : 1);
                                        if (u < 0.0) u = 0.0; else if (u > 1.0) u = 1.0;
                                        ctrl->num_val = ctrl->min_val + u * (ctrl->max_val - ctrl->min_val);
                                    }
                                } else if (ctrl->type == SCP_CTRL_INPUT && mx >= wx + 180 && mx <= wx + ww - 20) {
                                    g_script_popup.focused_ctrl_idx = i;
                                    SDL_StartTextInput();
                                }
                            }
                        }
                        int by = wy + wh - 42;
                        if (my >= by && my <= by + 28) {
                            if (mx >= wx + ww - 216 && mx <= wx + ww - 116) {
                                // [OK / Run]
                                g_script_popup.is_open = false;
                                SDL_StopTextInput();
                                char custom_msg[160] = {0};
                                interpret_custom_script_file(p, ren, win, g_script_popup.script_filepath, custom_msg, sizeof(custom_msg));
                                record_project_change(custom_msg[0] ? custom_msg : g_script_popup.title, true);
                            } else if (mx >= wx + ww - 104 && mx <= wx + ww - 16) {
                                g_script_popup.is_open = false;
                                SDL_StopTextInput();
                            }
                        }
                        continue;
                    }
                }

                // ================= REPOSITION ITEMS POPUP INTERACTIONS =================
                if (g_reposition.is_open) {
                    SDL_Rect *wr = &g_reposition.win_rect;
                    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
                    if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + 30) {
                        if (mx >= wx + ww - 28) {
                            g_reposition.is_open = false;
                            g_reposition.interval_focused = false;
                            SDL_StopTextInput();
                        } else {
                            g_reposition.is_dragging = true;
                            g_reposition.drag_off_x = mx - wx;
                            g_reposition.drag_off_y = my - wy;
                        }
                        continue;
                    }
                    if (mx >= wx && mx <= wx + ww && my >= wy + 30 && my <= wy + wh) {
                        g_reposition.interval_focused = false;
                        SDL_StopTextInput();
                        if (mx >= wx + 24 && mx <= wx + 200 && my >= wy + 64 && my <= wy + 84) {
                            g_reposition.mode = 0; // Item start
                            save_themes_settings_json();
                        } else if (mx >= wx + 24 && mx <= wx + 200 && my >= wy + 88 && my <= wy + 110) {
                            g_reposition.mode = 1; // Item end
                            save_themes_settings_json();
                        } else if (mx >= wx + 132 && mx <= wx + 218 && my >= wy + 132 && my <= wy + 158) {
                            g_reposition.interval_focused = true;
                            SDL_StartTextInput();
                        }
                        int by = wy + wh - 42;
                        if (my >= by && my <= by + 28) {
                            if (mx >= wx + ww - 204 && mx <= wx + ww - 116) {
                                execute_reposition_items_dialog(p);
                            } else if (mx >= wx + ww - 104 && mx <= wx + ww - 16) {
                                g_reposition.is_open = false;
                            }
                        }
                        continue;
                    }
                }

                // ================= DYNAMIC SPLIT ITEMS POPUP INTERACTIONS (SIMPLE BOX SYSTEM) =================
                if (g_dyn_split.is_open) {
                    SDL_Rect *wr = &g_dyn_split.win_rect;
                    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
                    if (g_dyn_split.editing_field_id > 0) {
                        commit_dyn_split_field_edit();
                    }
                    if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + 30) {
                        if (mx >= wx + ww - 28) {
                            g_dyn_split.is_open = false;
                        } else {
                            g_dyn_split.is_dragging = true;
                            g_dyn_split.drag_off_x = mx - wx;
                            g_dyn_split.drag_off_y = my - wy;
                        }
                        continue;
                    }
                    if (mx >= wx && mx <= wx + ww && my >= wy + 30 && my <= wy + wh) {
                        int box_x = wx + 250, box_w = 110;
                        // 1. Min slice length (ms) box
                        if (my >= wy + 44 && my <= wy + 68 && mx >= box_x && mx <= box_x + box_w) {
                            g_dyn_split.editing_field_id = 1;
                            snprintf(g_dyn_split.edit_buf, sizeof(g_dyn_split.edit_buf), "%d", g_dyn_split.min_slice_ms);
                            SDL_StartTextInput();
                        }
                        // 2. Min silence length (ms) box
                        else if (my >= wy + 78 && my <= wy + 102 && mx >= box_x && mx <= box_x + box_w) {
                            g_dyn_split.editing_field_id = 2;
                            snprintf(g_dyn_split.edit_buf, sizeof(g_dyn_split.edit_buf), "%d", g_dyn_split.min_silence_ms);
                            SDL_StartTextInput();
                        }
                        // 3. Threshold (dB) box
                        else if (my >= wy + 112 && my <= wy + 136 && mx >= box_x && mx <= box_x + box_w) {
                            g_dyn_split.editing_field_id = 3;
                            snprintf(g_dyn_split.edit_buf, sizeof(g_dyn_split.edit_buf), "%.1f", g_dyn_split.gate_threshold_db);
                            SDL_StartTextInput();
                        }
                        // 4. Hysteresis (dB) box
                        else if (my >= wy + 146 && my <= wy + 170 && mx >= box_x && mx <= box_x + box_w) {
                            g_dyn_split.editing_field_id = 4;
                            snprintf(g_dyn_split.edit_buf, sizeof(g_dyn_split.edit_buf), "%.1f", g_dyn_split.gate_hysteresis_db);
                            SDL_StartTextInput();
                        }
                        // 5. Remove silent areas checkbox
                        else if (my >= wy + 180 && my <= wy + 204 && mx >= wx + 22 && mx <= wx + 380) {
                            g_dyn_split.remove_silent_areas = !g_dyn_split.remove_silent_areas;
                            save_themes_settings_json();
                        }
                        // 6. Leading pad (ms) box
                        else if (my >= wy + 214 && my <= wy + 238 && mx >= box_x && mx <= box_x + box_w) {
                            g_dyn_split.editing_field_id = 5;
                            snprintf(g_dyn_split.edit_buf, sizeof(g_dyn_split.edit_buf), "%d", g_dyn_split.leading_pad_ms);
                            SDL_StartTextInput();
                        }
                        // 7. Trailing pad (ms) box
                        else if (my >= wy + 248 && my <= wy + 272 && mx >= box_x && mx <= box_x + box_w) {
                            g_dyn_split.editing_field_id = 6;
                            snprintf(g_dyn_split.edit_buf, sizeof(g_dyn_split.edit_buf), "%d", g_dyn_split.trailing_pad_ms);
                            SDL_StartTextInput();
                        }
                        // 8. Action to perform: [Selected clip item] or [Selected Track]
                        else if (my >= wy + 308 && my <= wy + 334) {
                            if (mx >= wx + 22 && mx <= wx + 222) {
                                g_dyn_split.action_mode = 0; // Selected clip item
                                save_themes_settings_json();
                            } else if (mx >= wx + 236 && mx <= wx + 436) {
                                g_dyn_split.action_mode = 1; // Selected Track
                                save_themes_settings_json();
                            }
                        }
                        // 9. Bottom buttons: [Split] and [Cancel]
                        int btn_y = wy + wh - 42;
                        if (my >= btn_y && my <= btn_y + 28) {
                            if (mx >= wx + ww - 216 && mx <= wx + ww - 120) {
                                execute_dynamic_split_items(p, ren, win);
                            } else if (mx >= wx + ww - 108 && mx <= wx + ww - 20) {
                                g_dyn_split.is_open = false;
                            }
                        }
                        continue;
                    }
                }

                // ================= UNIFIED EXPORT MEDIA POPUP INTERACTIONS =================
                if (g_export.is_open) {
                    SDL_Rect *er = &g_export.win_rect;
                    int ex = er->x, ey = er->y, ew = er->w, eh = er->h;

                    // Stop any previous text input unless a field is clicked below
                    g_export.active_field = EXP_FIELD_NONE;
                    SDL_StopTextInput();

                    // Title bar drag or [X] close
                    if (mx >= ex && mx <= ex + ew && my >= ey && my <= ey + 30) {
                        if (mx >= ex + ew - 30) {
                            g_export.is_open = false;
                        } else {
                            g_export.is_dragging = true;
                            g_export.drag_off_x = mx - ex;
                            g_export.drag_off_y = my - ey;
                        }
                        continue;
                    }

                    if (mx >= ex && mx <= ex + ew && my >= ey + 30 && my <= ey + eh) {
                        // 1. Source row (ey + 48 .. ey + 76)
                        int sy_src = ey + 48;
                        if (my >= sy_src && my <= sy_src + 26) {
                            if (mx >= ex + 110 && mx <= ex + 245) g_export.source = EXPORT_SRC_MASTER;
                            else if (mx >= ex + 255 && mx <= ex + 465) g_export.source = EXPORT_SRC_STEMS;
                            else if (mx >= ex + 475 && mx <= ex + 662) g_export.source = EXPORT_SRC_ITEMS;
                        }

                        // 2. Output Directory row (ey + 98 .. ey + 124) & [Browse...] button
                        int sy_dir = ey + 98;
                        if (my >= sy_dir && my <= sy_dir + 26) {
                            if (mx >= ex + 130 && mx <= ex + 545) {
                                g_export.active_field = EXP_FIELD_DIR;
                                SDL_StartTextInput();
                            } else if (mx >= ex + 555 && mx <= ex + 662) {
                                browse_export_directory_and_filename();
                            }
                        }

                        // 3. Output File Name row (ey + 132 .. ey + 158)
                        int sy_fn = ey + 132;
                        if (my >= sy_fn && my <= sy_fn + 26) {
                            if (mx >= ex + 130 && mx <= ex + 545) {
                                g_export.active_field = EXP_FIELD_FILENAME;
                                SDL_StartTextInput();
                            }
                        }

                        // 4. Sample Rate (48,000 / 44,100) & Channels (Mono / Stereo) row (ey + 182 .. ey + 208)
                        int sy_sr = ey + 182;
                        if (my >= sy_sr && my <= sy_sr + 26) {
                            if (mx >= ex + 130 && mx <= ex + 235) g_export.sample_rate = 48000;
                            else if (mx >= ex + 243 && mx <= ex + 348) g_export.sample_rate = 44100;
                            else if (mx >= ex + 455 && mx <= ex + 545) g_export.channels = 1;
                            else if (mx >= ex + 553 && mx <= ex + 655) g_export.channels = 2;
                        }

                        // 5. Format Tabs: WAV / MP3 / MP4 (ey + 232 .. ey + 260)
                        int sy_fmt = ey + 232;
                        if (my >= sy_fmt && my <= sy_fmt + 28) {
                            if (mx >= ex + 130 && mx <= ex + 260) g_export.format = EXPORT_FMT_WAV;
                            else if (mx >= ex + 270 && mx <= ex + 400) g_export.format = EXPORT_FMT_MP3;
                            else if (mx >= ex + 410 && mx <= ex + 655) g_export.format = EXPORT_FMT_MP4;
                        }

                        // 6. Format-specific sub-options (ey + 276 .. ey + 420)
                        int sy_sub = ey + 278;
                        if (g_export.format == EXPORT_FMT_WAV) {
                            if (my >= sy_sub + 12 && my <= sy_sub + 40) {
                                if (mx >= ex + 140 && mx <= ex + 280) g_export.wav_bit_depth = 16;
                                else if (mx >= ex + 292 && mx <= ex + 432) g_export.wav_bit_depth = 24;
                                else if (mx >= ex + 444 && mx <= ex + 584) g_export.wav_bit_depth = 32;
                            }
                        } else if (g_export.format == EXPORT_FMT_MP3) {
                            if (my >= sy_sub + 12 && my <= sy_sub + 40) {
                                if (mx >= ex + 140 && mx <= ex + 280) g_export.mp3_bitrate = 128;
                                else if (mx >= ex + 292 && mx <= ex + 432) g_export.mp3_bitrate = 192;
                                else if (mx >= ex + 444 && mx <= ex + 584) g_export.mp3_bitrate = 320;
                            }
                        } else if (g_export.format == EXPORT_FMT_MP4) {
                            // Row 1: File: [MPEG-4]  Size: [1920] x [1080]  Framerate: [25.00]
                            int r1_y = sy_sub + 8;
                            if (my >= r1_y && my <= r1_y + 26) {
                                if (mx >= ex + 295 && mx <= ex + 365) {
                                    g_export.active_field = EXP_FIELD_MP4_WIDTH;
                                    SDL_StartTextInput();
                                } else if (mx >= ex + 390 && mx <= ex + 460) {
                                    g_export.active_field = EXP_FIELD_MP4_HEIGHT;
                                    SDL_StartTextInput();
                                } else if (mx >= ex + 568 && mx <= ex + 648) {
                                    g_export.active_field = EXP_FIELD_MP4_FPS;
                                    SDL_StartTextInput();
                                }
                            }
                            // Row 2: Video: [H.264]  [2048] kbps
                            int r2_y = sy_sub + 46;
                            if (my >= r2_y && my <= r2_y + 26) {
                                if (mx >= ex + 295 && mx <= ex + 395) {
                                    g_export.active_field = EXP_FIELD_MP4_V_KBPS;
                                    SDL_StartTextInput();
                                }
                            }
                            // Row 3: Audio: [AAC]  [ 120 / 192 / 320 kbps v ] dropdown menu
                            int r3_y = sy_sub + 84;
                            if (g_export.mp4_aac_dropdown_open) {
                                int dd_x = ex + 295;
                                int dd_y = r3_y + 26;
                                int dd_w = 130;
                                if (mx >= dd_x && mx <= dd_x + dd_w && my >= dd_y && my <= dd_y + 78) {
                                    int sel_row = (my - dd_y) / 26;
                                    if (sel_row == 0) strcpy(g_export.mp4_audio_kbps_str, "120");
                                    else if (sel_row == 1) strcpy(g_export.mp4_audio_kbps_str, "192");
                                    else if (sel_row == 2) strcpy(g_export.mp4_audio_kbps_str, "320");
                                    g_export.mp4_aac_dropdown_open = false;
                                    continue;
                                }
                            }
                            if (my >= r3_y && my <= r3_y + 26) {
                                g_export.mp4_audio_codec = EXPORT_MP4_AUDIO_AAC;
                                if (mx >= ex + 295 && mx <= ex + 425) {
                                    g_export.mp4_aac_dropdown_open = !g_export.mp4_aac_dropdown_open;
                                    continue;
                                }
                            }
                            g_export.mp4_aac_dropdown_open = false;
                        } else {
                            g_export.mp4_aac_dropdown_open = false;
                        }

                        // 7. Option: Two-pass render checkbox (ey + 436 .. ey + 460)
                        int sy_opt = ey + 436;
                        if (my >= sy_opt && my <= sy_opt + 24 && mx >= ex + 22 && mx <= ex + 240) {
                            g_export.two_pass_render = !g_export.two_pass_render;
                        }

                        save_themes_settings_json();
                        // 8. Bottom buttons: [Render / Export] and [Close]
                        int btn_y = ey + eh - 44;
                        if (my >= btn_y && my <= btn_y + 32) {
                            if (mx >= ex + ew - 270 && mx <= ex + ew - 125) {
                                execute_unified_export(p, ren);
                            } else if (mx >= ex + ew - 115 && mx <= ex + ew - 20) {
                                g_export.is_open = false;
                            }
                        }
                        continue;
                    }
                    continue;
                }

                // ================= INSERT MULTIPLE MEDIA ITEMS POPUP INTERACTIONS =================
                if (g_multi_import.is_open) {
                    SDL_Rect *mr = &g_multi_import.win_rect;
                    int mmx = mr->x, mmy = mr->y, mmw = mr->w, mmh = mr->h;

                    // Title bar drag or [X] close
                    if (mx >= mmx && mx <= mmx + mmw && my >= mmy && my <= mmy + 30) {
                        if (mx >= mmx + mmw - 30) {
                            g_multi_import.is_open = false;
                            g_multi_import.num_files = 0;
                        } else {
                            g_multi_import.is_dragging = true;
                            g_multi_import.drag_off_x = mx - mmx;
                            g_multi_import.drag_off_y = my - mmy;
                        }
                        continue;
                    }

                    if (mx >= mmx && mx <= mmx + mmw && my >= mmy + 30 && my <= mmy + mmh) {
                        // Option 1: Same time position on separate tracks (y: mmy + 52 .. mmy + 84)
                        if (my >= mmy + 52 && my <= mmy + 84 && mx >= mmx + 20 && mx <= mmx + mmw - 20) {
                            g_multi_import.mode = MULTI_IMPORT_SEPARATE_TRACKS;
                            if (e.button.clicks >= 2) {
                                execute_multi_media_import(p, ren, win);
                            }
                            continue;
                        }
                        // Option 2: Sequential time positions on a single track (y: mmy + 94 .. mmy + 126)
                        if (my >= mmy + 94 && my <= mmy + 126 && mx >= mmx + 20 && mx <= mmx + mmw - 20) {
                            g_multi_import.mode = MULTI_IMPORT_SEQUENTIAL_SINGLE;
                            if (e.button.clicks >= 2) {
                                execute_multi_media_import(p, ren, win);
                            }
                            continue;
                        }
                        // Bottom Buttons: [OK / Insert] and [Cancel]
                        int by = mmy + mmh - 44;
                        if (my >= by && my <= by + 30) {
                            if (mx >= mmx + mmw - 210 && mx <= mmx + mmw - 115) {
                                execute_multi_media_import(p, ren, win);
                            } else if (mx >= mmx + mmw - 105 && mx <= mmx + mmw - 18) {
                                g_multi_import.is_open = false;
                                g_multi_import.num_files = 0;
                            }
                        }
                        continue;
                    }
                    continue;
                }

                // ================= THEMES (ARGB) POPUP WINDOW INTERACTIONS =================
                if (g_theme.is_open) {
                    init_default_saved_themes();
                    SDL_Rect *wr = &g_theme.win_rect;
                    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;

                    if (g_theme.code_focused) {
                        parse_theme_code_input(g_theme.code_buf);
                        save_themes_settings_json();
                        g_theme.code_focused = false;
                        SDL_StopTextInput();
                    }

                    // Title bar drag or [X] close
                    if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + 30) {
                        if (mx >= wx + ww - 28) {
                            g_theme.is_open = false;
                        } else {
                            g_theme.is_dragging = true;
                            g_theme.drag_off_x = mx - wx;
                            g_theme.drag_off_y = my - wy;
                        }
                        continue;
                    }

                    if (mx >= wx && mx <= wx + ww && my >= wy + 30 && my <= wy + wh) {
                        int p_x = wx + 18;
                        int sv_y = wy + 44, sv_w = 256, sv_h = 164;
                        int hue_y = wy + 220, bar_h = 18;
                        int alpha_y = wy + 250;
                        int code_y = wy + 284;
                        int b_y = wy + 350;

                        // 1. 2D Saturation/Value Square click/drag
                        if (mx >= p_x && mx <= p_x + sv_w && my >= sv_y && my <= sv_y + sv_h) {
                            g_theme.dragging_sv = true;
                            g_theme.s = (float)(mx - p_x) / (float)(sv_w - 1);
                            g_theme.v = 1.0f - ((float)(my - sv_y) / (float)(sv_h - 1));
                            if (g_theme.s < 0.0f) g_theme.s = 0.0f; else if (g_theme.s > 1.0f) g_theme.s = 1.0f;
                            if (g_theme.v < 0.0f) g_theme.v = 0.0f; else if (g_theme.v > 1.0f) g_theme.v = 1.0f;
                            theme_hsv_to_rgb(g_theme.h, g_theme.s, g_theme.v, &g_theme.r, &g_theme.g, &g_theme.b);
                            sync_theme_code_string();
                            save_themes_settings_json();
                            continue;
                        }

                        // 2. Hue Rainbow Bar click/drag
                        if (mx >= p_x && mx <= p_x + sv_w && my >= hue_y - 4 && my <= hue_y + bar_h + 4) {
                            g_theme.dragging_hue = true;
                            g_theme.h = ((float)(mx - p_x) / (float)(sv_w - 1)) * 360.0f;
                            if (g_theme.h < 0.0f) g_theme.h = 0.0f; else if (g_theme.h > 359.9f) g_theme.h = 359.9f;
                            theme_hsv_to_rgb(g_theme.h, g_theme.s, g_theme.v, &g_theme.r, &g_theme.g, &g_theme.b);
                            sync_theme_code_string();
                            save_themes_settings_json();
                            continue;
                        }

                        // 3. Alpha Bar click/drag
                        if (mx >= p_x && mx <= p_x + sv_w && my >= alpha_y - 4 && my <= alpha_y + bar_h + 4) {
                            g_theme.dragging_alpha = true;
                            float af = (float)(mx - p_x) / (float)(sv_w - 1);
                            if (af < 0.0f) af = 0.0f; else if (af > 1.0f) af = 1.0f;
                            g_theme.a = (uint8_t)(af * 255.0f + 0.5f);
                            sync_theme_code_string();
                            save_themes_settings_json();
                            continue;
                        }

                        // 4. rgba(...) Code Input Box click
                        if (mx >= p_x && mx <= p_x + 212 && my >= code_y && my <= code_y + 30) {
                            g_theme.code_focused = true;
                            SDL_StartTextInput();
                            continue;
                        }

                        // 5. [Save Theme] button -> Saves current ARGB theme into Saved Themes list for future use
                        if (mx >= p_x && mx <= p_x + 124 && my >= b_y && my <= b_y + 30) {
                            int slot = -1;
                            for (int i = 0; i < g_theme.num_saved; i++) {
                                if (g_theme.saved[i].r == g_theme.r && g_theme.saved[i].g == g_theme.g &&
                                    g_theme.saved[i].b == g_theme.b && g_theme.saved[i].a == g_theme.a) {
                                    slot = i;
                                    break;
                                }
                            }
                            if (slot < 0) {
                                if (g_theme.num_saved < MAX_SAVED_THEMES) {
                                    slot = g_theme.num_saved++;
                                } else {
                                    slot = MAX_SAVED_THEMES - 1;
                                }
                                SavedThemeEntry *st = &g_theme.saved[slot];
                                snprintf(st->name, sizeof(st->name), "Custom Theme %d", slot);
                                st->r = g_theme.r;
                                st->g = g_theme.g;
                                st->b = g_theme.b;
                                st->a = g_theme.a;
                            }
                            g_theme.selected_saved_idx = slot;
                            save_themes_settings_json();
                            snprintf(g_theme.status_msg, sizeof(g_theme.status_msg),
                                     "Saved '%s' (%s) to themes_settings.json",
                                     g_theme.saved[slot].name, g_theme.code_buf);
                            record_project_change("Saved Custom Theme", false);
                            continue;
                        }

                        // 6. [Default Orange] button -> Restores default Orange theme
                        if (mx >= p_x + 132 && mx <= p_x + 256 && my >= b_y && my <= b_y + 30) {
                            g_theme.r = 205;
                            g_theme.g = 138;
                            g_theme.b = 72;
                            g_theme.a = 255;
                            theme_rgb_to_hsv(g_theme.r, g_theme.g, g_theme.b, &g_theme.h, &g_theme.s, &g_theme.v);
                            sync_theme_code_string();
                            save_themes_settings_json();
                            snprintf(g_theme.status_msg, sizeof(g_theme.status_msg),
                                     "Restored Default Orange theme & saved to JSON");
                            record_project_change("Restored Default Orange Theme", false);
                            continue;
                        }

                        // 7. Right-side Saved Themes list & scrollbar
                        int rx = wx + 292;
                        int rw = ww - 308;
                        int list_y = wy + 64;
                        int list_h = 316;
                        int sb_x = rx + rw - 14;
                        if (my >= list_y && my < list_y + list_h && mx >= sb_x && mx <= rx + rw) {
                            g_theme.saved_sb_dragging = true;
                            g_theme.saved_sb_start_my = my;
                            g_theme.saved_sb_orig_scroll = g_theme.saved_scroll_y;
                            continue;
                        }
                        if (my >= list_y && my < list_y + list_h && mx >= rx && mx < sb_x) {
                            int clicked_row = (my - (list_y + 4) + g_theme.saved_scroll_y) / 28;
                            if (clicked_row >= 0 && clicked_row < g_theme.num_saved) {
                                SavedThemeEntry *st = &g_theme.saved[clicked_row];
                                g_theme.selected_saved_idx = clicked_row;
                                g_theme.r = st->r;
                                g_theme.g = st->g;
                                g_theme.b = st->b;
                                g_theme.a = st->a;
                                theme_rgb_to_hsv(g_theme.r, g_theme.g, g_theme.b, &g_theme.h, &g_theme.s, &g_theme.v);
                                sync_theme_code_string();
                                save_themes_settings_json();
                                snprintf(g_theme.status_msg, sizeof(g_theme.status_msg),
                                         "Applied theme '%s' & saved forever to JSON", st->name);
                                record_project_change("Applied Saved Theme", false);
                            }
                            continue;
                        }
                        continue;
                    }
                }

                // ================= HIDE TRACKS POPUP WINDOW INTERACTIONS (WITH SCROLLBAR) =================
                if (g_hide_popup_open) {
                    SDL_Rect *hr = &g_hide_win_rect;
                    int hx = hr->x, hy = hr->y, hw = hr->w, hh = hr->h;

                    if (mx >= hx && mx <= hx + hw && my >= hy && my <= hy + 28) {
                        if (mx >= hx + hw - 28) {
                            g_hide_popup_open = false;
                        } else {
                            g_hide_win_dragging = true;
                            g_hide_drag_off_x = mx - hx;
                            g_hide_drag_off_y = my - hy;
                        }
                        continue;
                    }

                    if (mx >= hx && mx <= hx + hw && my >= hy + 28 && my <= hy + hh) {
                        int list_y = hy + 58;
                        int list_h = hh - 106;
                        int sb_x = hx + hw - 28;
                        if (my >= list_y && my < list_y + list_h && mx >= sb_x && mx <= hx + hw - 14) {
                            g_hide_sb_dragging = true;
                            g_hide_sb_start_my = my;
                            g_hide_sb_orig_scroll = g_hide_scroll_y;
                            continue;
                        }
                        if (my >= list_y && my < list_y + list_h && mx >= hx + 14 && mx < sb_x) {
                            int row = (my - (list_y + 4) + g_hide_scroll_y) / 26;
                            if (row >= 0 && row < p->num_tracks) {
                                set_track_hidden(row, !is_track_hidden(row));
                                record_project_change(is_track_hidden(row) ? "Hidden Track" : "Shown Track", true);
                            }
                        }

                        int b_y = hy + hh - 38;
                        if (my >= b_y && my <= b_y + 28) {
                            if (mx >= hx + 14 && mx <= hx + 114) {
                                for (int t = 0; t < p->num_tracks; t++) set_track_hidden(t, false);
                                record_project_change("Shown All Tracks", true);
                            } else if (mx >= hx + 124 && mx <= hx + 224) {
                                for (int t = 0; t < p->num_tracks; t++) set_track_hidden(t, true);
                                record_project_change("Hidden All Tracks", true);
                            } else if (mx >= hx + hw - 94 && mx <= hx + hw - 14) {
                                g_hide_popup_open = false;
                            }
                        }
                        continue;
                    }
                }

                // ================= REAPER-STYLE ACTIONS & KEYBOARD SHORTCUTS WINDOW INTERACTIONS =================
                if (g_action_win.is_open) {
                    if (handle_action_window_mouse_down(p, ren, win, mx, my, e.button.clicks)) {
                        continue;
                    }
                }

                // ================= PROJECT HISTORY POPUP WINDOW INTERACTIONS (100 ITEMS) =================
                if (g_history_popup_open) {
                    SDL_Rect *hsr = &g_history_win_rect;
                    int hx = hsr->x, hy = hsr->y, hw = hsr->w, hh = hsr->h;

                    if (mx >= hx && mx <= hx + hw && my >= hy && my <= hy + 28) {
                        if (mx >= hx + hw - 28) {
                            g_history_popup_open = false;
                        } else {
                            g_history_win_dragging = true;
                            g_history_drag_off_x = mx - hx;
                            g_history_drag_off_y = my - hy;
                        }
                        continue;
                    }

                    if (mx >= hx && mx <= hx + hw && my >= hy + 28 && my <= hy + hh) {
                        int list_y = hy + 38;
                        int list_h = hh - 86;
                        int sb_x = hx + hw - 28;
                        if (my >= list_y && my < list_y + list_h && mx >= sb_x && mx <= hx + hw - 14) {
                            g_history_sb_dragging = true;
                            g_history_sb_start_my = my;
                            g_history_sb_orig_scroll = g_history_scroll_y;
                            continue;
                        }

                        int b_y = hy + hh - 38;
                        if (my >= b_y && my <= b_y + 28 && mx >= hx + hw - 98 && mx <= hx + hw - 14) {
                            g_history_popup_open = false;
                        }
                        continue;
                    }
                }

                // Handle Marker Right-Click Context Menu ("Delete Marker")
                if (g_marker_context_menu_open) {
                    int cm_w = 128, cm_h = 28;
                    if (mx >= g_marker_context_x && mx <= g_marker_context_x + cm_w &&
                        my >= g_marker_context_y && my <= g_marker_context_y + cm_h) {
                        if (g_marker_context_target_idx >= 0 && g_marker_context_target_idx < p->num_markers) {
                            delete_marker_at(p, g_marker_context_target_idx);
                        }
                        g_marker_context_menu_open = false;
                        g_marker_context_target_idx = -1;
                        continue;
                    }
                    g_marker_context_menu_open = false;
                    g_marker_context_target_idx = -1;
                }

                if (g_daw.input_focus != INPUT_FOCUS_NONE) {
                    if (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME) {
                        strncpy(p->track_names[g_daw.editing_track_name_idx], g_daw.editing_name_buf, 63);
                        p->is_dirty = true;
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_track_name_idx = -1;
                        SDL_StopTextInput();
                    } else if (g_daw.input_focus == INPUT_FOCUS_MARKER) {
                        int em = g_daw.editing_marker_idx;
                        int marker_y_chk = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT;
                        bool clicked_inside_editing_marker = false;
                        if (em >= 0 && em < p->num_markers && p->show_marker_lane &&
                            my >= marker_y_chk && my <= marker_y_chk + MARKER_BAR_HEIGHT &&
                            e.button.button == SDL_BUTTON_LEFT) {
                            int mk_x = g_daw.tcp_width + (int)((p->markers[em].time_sec - p->scroll_x_sec) * p->pixels_per_second);
                            int tag_w = get_marker_tag_width(g_editing_marker_full_buf);
                            if (mx >= mk_x && mx <= mk_x + tag_w) {
                                clicked_inside_editing_marker = true;
                                int char_pos = (mx - (mk_x + 6) + 4) / 8;
                                int max_l = (int)strlen(g_editing_marker_full_buf);
                                if (char_pos < 0) char_pos = 0;
                                if (char_pos > max_l) char_pos = max_l;
                                g_marker_cursor_pos = char_pos;
                            }
                        }
                        if (clicked_inside_editing_marker) {
                            continue;
                        }
                        if (em >= 0 && em < p->num_markers) {
                            set_marker_full_str(p, em, g_editing_marker_full_buf);
                            p->is_dirty = true;
                        }
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_marker_idx = -1;
                        SDL_StopTextInput();
                    }
                }

                if (p->video.is_open) {
                    int v_tab = get_valid_tab_idx();
                    VideoResizeEdge hit_edge = g_video_fullscreen[v_tab]
                                               ? VID_RESIZE_NONE
                                               : hit_test_video_resize_edge(&p->video.rect, mx, my);
                    if (hit_edge != VID_RESIZE_NONE) {
                        p->video.is_resizing = true;
                        g_video_resize_edge = hit_edge;
                        g_video_resize_start_mx = mx;
                        g_video_resize_start_my = my;
                        g_video_resize_orig_rect = p->video.rect;
                        continue;
                    }
                    if (mx >= p->video.rect.x && mx <= p->video.rect.x + p->video.rect.w &&
                        my >= p->video.rect.y && my <= p->video.rect.y + p->video.rect.h) {
                        if (mx >= p->video.rect.x + p->video.rect.w - 24 && my <= p->video.rect.y + 24) {
                            g_video_fullscreen[v_tab] = false;
                            p->video.is_open = false;
                        } else if (mx >= p->video.rect.x + p->video.rect.w - 50 &&
                                   mx <  p->video.rect.x + p->video.rect.w - 24 &&
                                   my <= p->video.rect.y + 24) {
                            // Full Screen / Minimize (Restore) button near X
                            if (!g_video_fullscreen[v_tab]) {
                                g_video_saved_rect[v_tab] = p->video.rect;
                                g_video_fullscreen[v_tab] = true;
                                p->video.rect = (SDL_Rect){ 0, 0, win_w, win_h };
                            } else {
                                g_video_fullscreen[v_tab] = false;
                                if (g_video_saved_rect[v_tab].w >= 200 && g_video_saved_rect[v_tab].h >= 120) {
                                    p->video.rect = g_video_saved_rect[v_tab];
                                } else {
                                    p->video.rect = (SDL_Rect){ 680, 110, 480, 294 };
                                }
                            }
                        } else if (my <= p->video.rect.y + 24) {
                            if (e.button.clicks >= 2) {
                                if (!g_video_fullscreen[v_tab]) {
                                    g_video_saved_rect[v_tab] = p->video.rect;
                                    g_video_fullscreen[v_tab] = true;
                                    p->video.rect = (SDL_Rect){ 0, 0, win_w, win_h };
                                } else {
                                    g_video_fullscreen[v_tab] = false;
                                    if (g_video_saved_rect[v_tab].w >= 200 && g_video_saved_rect[v_tab].h >= 120) {
                                        p->video.rect = g_video_saved_rect[v_tab];
                                    } else {
                                        p->video.rect = (SDL_Rect){ 680, 110, 480, 294 };
                                    }
                                }
                            } else if (!g_video_fullscreen[v_tab]) {
                                p->video.is_dragging = true;
                                p->video.drag_x = mx - p->video.rect.x;
                                p->video.drag_y = my - p->video.rect.y;
                            }
                        }
                        continue;
                    }
                }

                // Chrome-Style Tab Interactions
                if (my < TABBAR_HEIGHT) {
                    int max_tab_w = 160;
                    int add_btn_w = 34;
                    int cur_tab_w = max_tab_w;
                    if (g_daw.num_tabs > 0) {
                        int calc_w = (win_w - add_btn_w - 20) / g_daw.num_tabs;
                        if (calc_w < cur_tab_w) cur_tab_w = calc_w;
                        if (cur_tab_w < 70) cur_tab_w = 70;
                    }

                    int cur_tab_x = 0;
                    bool clicked_tab = false;
                    for (int t = 0; t < g_daw.num_tabs; t++) {
                        if (mx >= cur_tab_x && mx < cur_tab_x + cur_tab_w) {
                            clicked_tab = true;
                            if (mx >= cur_tab_x + cur_tab_w - 24 && mx <= cur_tab_x + cur_tab_w - 4) {
                                request_close_tab(t);
                            } else {
                                g_daw.active_tab = t;
                                g_selected_marker_idx = -1;
                            }
                            break;
                        }
                        cur_tab_x += cur_tab_w;
                    }

                    if (!clicked_tab && mx >= cur_tab_x && mx <= cur_tab_x + add_btn_w && g_daw.num_tabs < MAX_TABS) {
                        char name[64]; snprintf(name, sizeof(name), "Project %d", g_daw.num_tabs + 1);
                        init_tab_defaults(g_daw.num_tabs, name);
                        g_tab_crossfade[g_daw.num_tabs] = true;
                        g_tab_range_active[g_daw.num_tabs] = false;
                        g_tab_range_start[g_daw.num_tabs] = 0.0;
                        g_tab_range_end[g_daw.num_tabs] = 0.0;
                        memset(g_marker_full_text[g_daw.num_tabs], 0, sizeof(g_marker_full_text[0]));
                        memset(g_item_fade_in[g_daw.num_tabs], 0, sizeof(g_item_fade_in[0]));
                        memset(g_item_fade_out[g_daw.num_tabs], 0, sizeof(g_item_fade_out[0]));
                        memset(g_track_hidden[g_daw.num_tabs], 0, sizeof(g_track_hidden[0]));
                        memset(g_tab_history[g_daw.num_tabs], 0, sizeof(g_tab_history[0]));
                        g_tab_history_count[g_daw.num_tabs] = 0;
                        g_tab_notification[g_daw.num_tabs][0] = '\0';
                        g_daw.active_tab = g_daw.num_tabs++;
                        g_selected_marker_idx = -1;
                        record_project_change("Project Created", false);
                    }
                    continue;
                }

                if (g_daw.script_manager_open) {
                    SDL_Rect *sr = &g_daw.script_win_rect;
                    VideoResizeEdge sc_edge = hit_test_video_resize_edge(sr, mx, my);
                    if (sc_edge != VID_RESIZE_NONE) {
                        g_script_resize_edge = (int)sc_edge;
                        g_script_resize_start_mx = mx;
                        g_script_resize_start_my = my;
                        g_script_resize_orig_rect = *sr;
                        continue;
                    }
                    if (mx >= sr->x && mx <= sr->x + sr->w && my >= sr->y && my <= sr->y + 28) {
                        if (mx >= sr->x + sr->w - 26) {
                            g_daw.script_manager_open = false;
                        } else {
                            g_daw.script_win_dragging = true;
                            g_daw.script_drag_offset_x = mx - sr->x;
                            g_daw.script_drag_offset_y = my - sr->y;
                        }
                        continue;
                    }
                    if (mx >= sr->x && mx <= sr->x + sr->w && my >= sr->y + 28 && my <= sr->y + sr->h) {
                        if (g_script_filter_focused) {
                            g_script_filter_focused = false;
                            SDL_StopTextInput();
                        }
                        refresh_filtered_scripts();

                        // Top Filter & Pin / Move Up / Move Down row (sr->y + 34 .. sr->y + 60)
                        int top_y = sr->y + 34;
                        if (my >= top_y && my <= top_y + 26) {
                            int flt_w = sr->w - 310;
                            if (flt_w < 110) flt_w = 110;
                            int flt_x = sr->x + 56;
                            int clr_x = flt_x + flt_w + 6;
                            int pin_x = clr_x + 58;
                            int up_x  = pin_x + 64;
                            int dn_x  = up_x + 42;
                            if (mx >= flt_x && mx <= flt_x + flt_w) {
                                g_script_filter_focused = true;
                                SDL_StartTextInput();
                            } else if (mx >= clr_x && mx <= clr_x + 52) {
                                g_script_filter_text[0] = '\0';
                                g_script_scroll_y = 0;
                                refresh_filtered_scripts();
                            } else if (mx >= pin_x && mx <= pin_x + 58) {
                                toggle_pin_selected_script();
                            } else if (mx >= up_x && mx <= up_x + 38) {
                                move_selected_script_row(-1);
                            } else if (mx >= dn_x && mx <= dn_x + 40) {
                                move_selected_script_row(1);
                            }
                            continue;
                        }

                        int list_y = sr->y + 66;
                        int list_h = sr->h - 112;
                        int sb_x = sr->x + sr->w - 26;

                        // Scrollbar drag inside Script Manager
                        if (my >= list_y && my < list_y + list_h && mx >= sb_x && mx <= sr->x + sr->w - 12) {
                            g_script_sb_dragging = true;
                            g_script_sb_start_my = my;
                            g_script_sb_orig_scroll = g_script_scroll_y;
                            continue;
                        }

                        // Script row selection & double-click execution (using filtered & pinned order)
                        if (my >= list_y && my < list_y + list_h && mx >= sr->x + 12 && mx < sb_x) {
                            int r = (my - (list_y + 4) + g_script_scroll_y) / 24;
                            if (r >= 0 && r < g_script_num_filtered) {
                                int sc_idx = g_script_filtered_indices[r];
                                g_daw.script_selected_idx = sc_idx;
                                if (e.button.clicks >= 2) {
                                    scan_scripts_directory_to_actions(&g_daw);
                                    for (int a = 0; a < g_num_actions; a++) {
                                        if (g_actions[a].linked_script_idx == sc_idx ||
                                            str_contains_nocase(g_actions[a].description, g_daw.scripts[sc_idx].name)) {
                                            execute_daw_action_by_index(p, ren, win, a);
                                            snprintf(g_script_status_msg, sizeof(g_script_status_msg),
                                                     "Executed: %s", g_daw.scripts[sc_idx].name);
                                            break;
                                        }
                                    }
                                }
                            }
                            continue;
                        }

                        // Bottom buttons: [Refresh Script] and [Run Script] (Delete removed)
                        int by = sr->y + sr->h - 38;
                        if (my >= by && my <= by + 28) {
                            if (mx >= sr->x + 12 && mx <= sr->x + 142) {
                                // [Refresh Script]: Rescans scripts/ folders for any new .lua or .py files
                                scan_scripts_directory_to_actions(&g_daw);
                                save_keyboard_shortcuts_json();
                                snprintf(g_script_status_msg, sizeof(g_script_status_msg),
                                         "Refreshed scripts folder (%d scripts loaded)", g_daw.num_scripts);
                                record_project_change("Refreshed Scripts Folder", false);
                            } else if (mx >= sr->x + 152 && mx <= sr->x + 266) {
                                // [Run Script]: Executes selected Lua or Python script
                                if (g_daw.script_selected_idx >= 0 && g_daw.script_selected_idx < g_daw.num_scripts) {
                                    int sel_s = g_daw.script_selected_idx;
                                    scan_scripts_directory_to_actions(&g_daw);
                                    for (int a = 0; a < g_num_actions; a++) {
                                        if (g_actions[a].linked_script_idx == sel_s ||
                                            str_contains_nocase(g_actions[a].description, g_daw.scripts[sel_s].name)) {
                                            execute_daw_action_by_index(p, ren, win, a);
                                            snprintf(g_script_status_msg, sizeof(g_script_status_msg),
                                                     "Executed: %s", g_daw.scripts[sel_s].name);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        continue;
                    }
                }

                if (g_daw.menu_file_open) {
                    int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                    if (mx >= 8 && mx <= 204 && my >= m_start && my <= m_start + 126) {
                        if (my >= m_start && my < m_start + 24) {
                            // Row 0: Import Project (.ehs) -> Prompt Save/Don't Save if current project tab has unsaved edits
                            g_daw.menu_file_open = false;
                            request_open_project_with_save_prompt(p, ren, win);
                            continue;
                        }
                        else if (my >= m_start + 24 && my < m_start + 48) {
                            // Row 1: Export Project (.ehs)
                            g_daw.menu_file_open = false;
                            daw_save_project_dialog(p, false);
                            continue;
                        }
                        else if (my >= m_start + 48 && my < m_start + 72) {
                            // Row 2: Project Save As
                            g_daw.menu_file_open = false;
                            daw_save_project_dialog(p, true);
                            continue;
                        }
                        else if (my >= m_start + 72 && my < m_start + 96) {
                            // Row 3: Import Media (supports single or multiple media files)
                            g_daw.menu_file_open = false;
                            static char sel_files[MAX_MULTI_IMPORT_FILES][512];
                            int num_sel = open_multi_media_dialog(sel_files, MAX_MULTI_IMPORT_FILES);
                            int target_trk = 0;
                            for (int t = 0; t < p->num_tracks; t++) {
                                if (p->track_selected[t] && !is_track_hidden(t)) { target_trk = t; break; }
                            }
                            if (num_sel == 1) {
                                import_single_media_item(p, ren, win, sel_files[0], target_trk, p->playhead_seconds);
                            } else if (num_sel > 1) {
                                g_multi_import.num_files = num_sel;
                                for (int k = 0; k < num_sel; k++) {
                                    strncpy(g_multi_import.files[k], sel_files[k], 511);
                                    g_multi_import.files[k][511] = '\0';
                                }
                                g_multi_import.base_track = target_trk;
                                g_multi_import.base_time = p->playhead_seconds;
                                g_multi_import.mode = MULTI_IMPORT_SEPARATE_TRACKS;
                                g_multi_import.win_rect.x = (win_w - g_multi_import.win_rect.w) / 2;
                                g_multi_import.win_rect.y = (win_h - g_multi_import.win_rect.h) / 2;
                                g_multi_import.is_open = true;
                            }
                            continue;
                        }
                        else if (my >= m_start + 96 && my <= m_start + 126) {
                            // Row 4: Export Media (no 3 dots)
                            g_export.is_open = true;
                            g_export.win_rect.x = (win_w - g_export.win_rect.w) / 2;
                            g_export.win_rect.y = (win_h - g_export.win_rect.h) / 2;
                            g_export.is_rendering = false;
                            g_export.progress_pct = 0;
                            g_export.elapsed_sec = 0.0;
                            g_export.est_remaining_sec = 0.0;
                            g_export.mp4_aac_dropdown_open = false;
                            g_export.status_msg[0] = '\0';
                            g_daw.menu_file_open = false;
                            g_daw.submenu_export_audio_open = false;
                            g_daw.submenu_export_video_open = false;
                            continue;
                        }
                    } else if (my > TABBAR_HEIGHT + TOPBAR_HEIGHT || mx > 260) {
                        g_daw.menu_file_open = false;
                        continue;
                    }
                }

                if (g_daw.menu_action_open) {
                    int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                    if (mx >= 60 && mx <= 238 && my >= m_start && my <= m_start + 135) {
                        if (my >= m_start && my < m_start + 27) {
                            // Submenu 1: Keyboard Shortcut -> Opens REAPER-style Actions & Keyboard Shortcuts popup window
                            scan_scripts_directory_to_actions(&g_daw);
                            save_keyboard_shortcuts_json();
                            g_action_win.is_open = true;
                            g_action_win.win_rect.x = (win_w - g_action_win.win_rect.w) / 2;
                            g_action_win.win_rect.y = (win_h - g_action_win.win_rect.h) / 2;
                            g_daw.menu_action_open = false;
                            continue;
                        } else if (my >= m_start + 27 && my < m_start + 54) {
                            // Submenu 2: Scripts -> Opens unified Lua & Python Script Manager popup window
                            scan_scripts_directory_to_actions(&g_daw);
                            save_keyboard_shortcuts_json();
                            refresh_filtered_scripts();
                            g_daw.script_manager_open = true;
                            g_daw.menu_action_open = false;
                            continue;
                        } else if (my >= m_start + 54 && my < m_start + 81) {
                            // Submenu 3: Dynamic split -> Opens simple box-based Dynamic split popup window
                            g_dyn_split.is_open = true;
                            g_dyn_split.win_rect.x = (win_w - g_dyn_split.win_rect.w) / 2;
                            g_dyn_split.win_rect.y = (win_h - g_dyn_split.win_rect.h) / 2;
                            g_dyn_split.editing_field_id = 0;
                            g_dyn_split.dragging_slider_id = 0;
                            g_daw.menu_action_open = false;
                            continue;
                        } else if (my >= m_start + 81 && my < m_start + 108) {
                            // Submenu 4: Reposition -> Opens Reposition Items popup window
                            g_reposition.is_open = true;
                            g_reposition.win_rect.x = (win_w - g_reposition.win_rect.w) / 2;
                            g_reposition.win_rect.y = (win_h - g_reposition.win_rect.h) / 2;
                            g_reposition.interval_focused = true;
                            SDL_StartTextInput();
                            g_daw.menu_action_open = false;
                            continue;
                        } else if (my >= m_start + 108 && my <= m_start + 135) {
                            // Submenu 5: Join (Glue) -> Joins selected clips on the same track into one clip
                            g_daw.menu_action_open = false;
                            execute_join_glue_selected_items(p, ren, win);
                            continue;
                        }
                    } else if (my > TABBAR_HEIGHT + TOPBAR_HEIGHT || mx < 60 || mx > 260) {
                        g_daw.menu_action_open = false;
                        continue;
                    }
                }

                if (g_menu_tools_open) {
                    int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                    // Check Tools -> Record 2-item submenu first if open (Rec | Browse)
                    if (g_submenu_tools_record_open && mx >= 276 && mx <= 422 && my >= m_start + 80 && my <= m_start + 136) {
                        if (my < m_start + 108) {
                            // Submenu item 1: Rec
                            g_menu_tools_open = false;
                            g_submenu_tools_record_open = false;
                            start_or_stop_recording(p, ren, win);
                        } else {
                            // Submenu item 2: Browse [with folder symbol]
                            g_menu_tools_open = false;
                            g_submenu_tools_record_open = false;
                            browse_recording_folder_dialog();
                        }
                        continue;
                    }
                    if (mx >= 126 && mx <= 276 && my >= m_start && my <= m_start + 108) {
                        if (my >= m_start && my < m_start + 27) {
                            // Submenu 1: Marker
                            p->show_marker_lane = !p->show_marker_lane;
                            g_menu_tools_open = false;
                            g_submenu_tools_record_open = false;
                            continue;
                        } else if (my >= m_start + 27 && my < m_start + 54) {
                            // Submenu 2: History (opens 100-entry Project History popup window)
                            g_history_popup_open = true;
                            g_history_win_rect.x = (win_w - g_history_win_rect.w) / 2;
                            g_history_win_rect.y = (win_h - g_history_win_rect.h) / 2;
                            g_menu_tools_open = false;
                            g_submenu_tools_record_open = false;
                            continue;
                        } else if (my >= m_start + 54 && my < m_start + 81) {
                            // Submenu 3: Themes (opens ARGB Themes popup window)
                            init_default_saved_themes();
                            sync_theme_code_string();
                            g_theme.is_open = true;
                            g_theme.win_rect.x = (win_w - g_theme.win_rect.w) / 2;
                            g_theme.win_rect.y = (win_h - g_theme.win_rect.h) / 2;
                            g_menu_tools_open = false;
                            g_submenu_tools_record_open = false;
                            continue;
                        } else if (my >= m_start + 81 && my <= m_start + 108) {
                            // Submenu 4: Record -> Opens 2-item submenu: Rec | Browse [with folder symbol]
                            g_submenu_tools_record_open = !g_submenu_tools_record_open;
                            continue;
                        }
                    } else if (my > TABBAR_HEIGHT + TOPBAR_HEIGHT || mx < 126 || mx > 430) {
                        g_menu_tools_open = false;
                        g_submenu_tools_record_open = false;
                        continue;
                    }
                }

                if (my >= TABBAR_HEIGHT && my <= TABBAR_HEIGHT + TOPBAR_HEIGHT) {
                    // Left-side top menu: File | Action | Tools | Video
                    if (mx >= 8 && mx <= 54) {
                        g_daw.menu_file_open = !g_daw.menu_file_open;
                        g_daw.menu_action_open = false;
                        g_menu_tools_open = false;
                        g_submenu_tools_record_open = false;
                    }
                    else if (mx >= 60 && mx <= 120) {
                        g_daw.menu_action_open = !g_daw.menu_action_open;
                        g_daw.menu_file_open = false;
                        g_menu_tools_open = false;
                        g_submenu_tools_record_open = false;
                    }
                    else if (mx >= 126 && mx <= 184) {
                        g_menu_tools_open = !g_menu_tools_open;
                        if (!g_menu_tools_open) g_submenu_tools_record_open = false;
                        g_daw.menu_file_open = false;
                        g_daw.menu_action_open = false;
                    }
                    else if (mx >= 190 && mx <= 250) {
                        p->video.is_open = !p->video.is_open;
                    }
                    // Right-side top menu: [Undo Symbol] | [Redo Symbol] | Range | Crossfade | Hide | Save
                    else if (mx >= win_w - 372 && mx <= win_w - 332) { perform_undo(); record_project_change("Undo Action", true); }
                    else if (mx >= win_w - 326 && mx <= win_w - 286) { perform_redo(); record_project_change("Redo Action", true); }
                    else if (mx >= win_w - 280 && mx <= win_w - 220) { handle_top_menu_range_button(p); }
                    else if (mx >= win_w - 214 && mx <= win_w - 124) { set_crossfade_active(!is_crossfade_active()); }
                    else if (mx >= win_w - 118 && mx <= win_w - 66)  {
                        g_hide_popup_open = !g_hide_popup_open;
                        if (g_hide_popup_open) {
                            g_hide_win_rect.x = (win_w - g_hide_win_rect.w) / 2;
                            g_hide_win_rect.y = (win_h - g_hide_win_rect.h) / 2;
                        }
                    }
                    else if (mx >= win_w - 60 && mx <= win_w - 8) {
                        daw_save_project_dialog(p, false);
                    }
                    continue;
                }

                if (abs(mx - g_daw.tcp_width) <= 5 && my > TABBAR_HEIGHT + TOPBAR_HEIGHT) {
                    resizing_tcp_width = true;
                    continue;
                }

                int corner_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                if (mx < g_daw.tcp_width && my > corner_y && my <= corner_y + RULER_HEIGHT + m_lane_h) {
                    if (mx >= 8 && mx <= 88 && p->num_tracks < MAX_TRACKS) {
                        push_undo_state();
                        p->track_heights[p->num_tracks] = 84;
                        snprintf(p->track_names[p->num_tracks], 64, "Track %d", p->num_tracks + 1);
                        p->num_tracks++;
                        record_project_change("Added New Track", true);
                    } else if (mx >= 94 && mx <= 174) {
                        if (g_rec.is_recording) start_or_stop_recording(p, ren, win);
                        else p->is_playing = !p->is_playing;
                    }
                    continue;
                }



                if (my >= win_h - SCROLLBAR_SIZE && mx >= g_daw.tcp_width && mx < win_w - SCROLLBAR_SIZE) {
                    dragging_scrollbar_h = true; scrollbar_h_start_mx = mx; orig_scroll_x = p->scroll_x_sec;
                    continue;
                }
                if (mx >= win_w - SCROLLBAR_SIZE && my >= TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT + m_lane_h && my < win_h - SCROLLBAR_SIZE) {
                    dragging_scrollbar_v = true; scrollbar_v_start_my = my; orig_scroll_y = p->scroll_y_px;
                    continue;
                }

                // ================= MARKER LANE INTERACTIONS (FIXED POSITION, ONLY RIGHT-CLICK ADDS) =================
                int marker_y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT;
                if (p->show_marker_lane && mx >= g_daw.tcp_width && my > marker_y && my <= marker_y + MARKER_BAR_HEIGHT) {
                    double m_time = fmax(0.0, p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second);

                    // Hit-test existing markers using their non-overlapping clamped width
                    int hit_m = -1;
                    for (int m = p->num_markers - 1; m >= 0; m--) {
                        int mk_x = g_daw.tcp_width + (int)((p->markers[m].time_sec - p->scroll_x_sec) * p->pixels_per_second);
                        int vis_w = get_marker_clamped_width(p, m, mk_x);
                        if (mx >= mk_x && mx < mk_x + vis_w) {
                            hit_m = m;
                            break;
                        }
                    }

                    if (e.button.button == SDL_BUTTON_RIGHT) {
                        if (hit_m >= 0) {
                            // Right-clicked an existing marker -> open "Delete Marker" context menu
                            g_selected_marker_idx = hit_m;
                            g_marker_context_menu_open = true;
                            g_marker_context_target_idx = hit_m;
                            g_marker_context_x = mx;
                            g_marker_context_y = marker_y + MARKER_BAR_HEIGHT + 2;
                        } else if (p->num_markers < MAX_MARKERS) {
                            // Right-click on empty marker lane -> Add fixed Marker
                            push_undo_state();
                            int new_idx = p->num_markers++;
                            DawMarker *mk = &p->markers[new_idx];
                            mk->time_sec = m_time;
                            char def_txt[64];
                            snprintf(def_txt, sizeof(def_txt), "Marker %d", p->num_markers);
                            set_marker_full_str(p, new_idx, def_txt);
                            g_selected_marker_idx = new_idx;
                            record_project_change("Added Marker", true);
                        }
                    } else if (e.button.button == SDL_BUTTON_LEFT) {
                        if (hit_m >= 0) {
                            g_selected_marker_idx = hit_m;
                            if (e.button.clicks >= 2) {
                                // Double-click on marker -> Open typing cursor and reveal full text
                                g_daw.input_focus = INPUT_FOCUS_MARKER;
                                g_daw.editing_marker_idx = hit_m;
                                const char *full_str = get_marker_full_str(p, hit_m);
                                strncpy(g_editing_marker_full_buf, full_str, MARKER_TEXT_MAX - 1);
                                g_editing_marker_full_buf[MARKER_TEXT_MAX - 1] = '\0';
                                strncpy(g_daw.editing_marker_buf, full_str, 63);
                                g_daw.editing_marker_buf[63] = '\0';
                                g_marker_cursor_pos = (int)strlen(g_editing_marker_full_buf);
                                SDL_StartTextInput();
                            }
                        } else {
                            g_selected_marker_idx = -1;
                        }
                    }
                    continue;
                }

                // ================= TIME RULER & LOGIC PRO YELLOW CYCLE RANGE STRIP =================
                int ruler_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                int header_top_y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT + m_lane_h;

                // Check if user clicked on the yellow Cycle Range strip or its draggable left/right edges
                if (is_range_active() && e.button.button == SDL_BUTTON_LEFT && mx >= g_daw.tcp_width) {
                    int rx1 = g_daw.tcp_width + (int)((get_range_start() - p->scroll_x_sec) * p->pixels_per_second);
                    int rx2 = g_daw.tcp_width + (int)((get_range_end()   - p->scroll_x_sec) * p->pixels_per_second);
                    bool in_ruler_strip = (my >= ruler_y && my <= ruler_y + RULER_HEIGHT);
                    bool in_top_strip   = (my >= header_top_y && my <= header_top_y + 10);

                    if (in_ruler_strip || in_top_strip) {
                        if (abs(mx - rx1) <= 8) {
                            dragging_range_left = true;
                            range_orig_start_sec = get_range_start();
                            range_orig_end_sec   = get_range_end();
                            continue;
                        } else if (abs(mx - rx2) <= 8) {
                            dragging_range_right = true;
                            range_orig_start_sec = get_range_start();
                            range_orig_end_sec   = get_range_end();
                            continue;
                        } else if (mx > rx1 + 8 && mx < rx2 - 8 &&
                                   ((my >= ruler_y + RULER_HEIGHT - 10 && my <= ruler_y + RULER_HEIGHT) || in_top_strip)) {
                            dragging_range_body = true;
                            range_drag_start_mouse_sec = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                            range_orig_start_sec = get_range_start();
                            range_orig_end_sec   = get_range_end();
                            continue;
                        }
                    }
                }

                if (mx >= g_daw.tcp_width && my > ruler_y && my <= ruler_y + RULER_HEIGHT) {
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        scrubbing_ruler = true;
                        p->playhead_seconds = fmax(0.0, p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second);
                    }
                    // Right-clicking on the timeline ruler does NOT open the marker strip or add a marker
                    continue;
                }

                // ================= TRACK CONTROLS, SELECTION & UP/DOWN DRAG REORDER =================
                if (mx < g_daw.tcp_width && my > header_top_y) {
                    int cur_y = header_top_y - p->scroll_y_px;
                    for (int t = 0; t < p->num_tracks; t++) {
                        if (is_track_hidden(t)) continue;
                        int th = p->track_heights[t];
                        if (abs(my - (cur_y + th)) <= 5) {
                            resizing_track_idx = t;
                            track_orig_height = th;
                            mouse_drag_start_y = my;
                            break;
                        }
                        if (my >= cur_y && my < cur_y + th) {
                            int rel_y = my - cur_y;

                            if (ctrl_pressed) {
                                p->track_selected[t] = !p->track_selected[t];
                                g_last_clicked_track_idx = t;
                            } else if (shift_pressed) {
                                int anchor = g_last_clicked_track_idx;
                                if (anchor < 0 || anchor >= p->num_tracks) {
                                    // Find existing single selected track if any, else make t the anchor
                                    for (int tr = 0; tr < p->num_tracks; tr++) {
                                        if (p->track_selected[tr]) { anchor = tr; break; }
                                    }
                                }
                                if (anchor < 0 || anchor >= p->num_tracks) {
                                    for (int tr = 0; tr < p->num_tracks; tr++) p->track_selected[tr] = false;
                                    p->track_selected[t] = true;
                                    g_last_clicked_track_idx = t;
                                } else {
                                    int t_min = (anchor < t) ? anchor : t;
                                    int t_max = (anchor > t) ? anchor : t;
                                    for (int tr = 0; tr < p->num_tracks; tr++) {
                                        p->track_selected[tr] = (!is_track_hidden(tr) && tr >= t_min && tr <= t_max);
                                    }
                                }
                            } else {
                                for (int tr = 0; tr < p->num_tracks; tr++) p->track_selected[tr] = false;
                                p->track_selected[t] = true;
                                g_last_clicked_track_idx = t;
                            }
                            for (int o = 0; o < p->num_items; o++) p->items[o].is_selected = false;

                            bool handled_btn = false;
                            if (mx >= g_daw.tcp_width - 28 && mx <= g_daw.tcp_width - 6) {
                                dragging_track_order_idx = t;
                                dragging_track_order_pushed_undo = false;
                                handled_btn = true;
                            } else if (rel_y >= 30 && rel_y <= 56) {
                                if (mx >= 8 && mx <= 26) { p->track_db[t] -= 1.0f; record_project_change("Adjusted Track Volume", true); handled_btn = true; }
                                else if (mx >= 76 && mx <= 94) { p->track_db[t] += 1.0f; record_project_change("Adjusted Track Volume", true); handled_btn = true; }
                                else if (mx >= 110 && mx <= 132) { p->tracks[t].mute = !p->tracks[t].mute; record_project_change("Toggled Track Mute", true); handled_btn = true; }
                                else if (mx >= 138 && mx <= 160) { p->tracks[t].solo = !p->tracks[t].solo; record_project_change("Toggled Track Solo", true); handled_btn = true; }
                            } else if (rel_y >= 6 && rel_y <= 28 && mx >= 8 && mx <= g_daw.tcp_width - 32) {
                                if (e.button.clicks >= 2) {
                                    g_daw.input_focus = INPUT_FOCUS_TRACK_NAME;
                                    g_daw.editing_track_name_idx = t;
                                    strncpy(g_daw.editing_name_buf, p->track_names[t], 63);
                                    SDL_StartTextInput();
                                    handled_btn = true;
                                }
                            }

                            if (!handled_btn && e.button.button == SDL_BUTTON_LEFT) {
                                dragging_track_order_idx = t;
                                dragging_track_order_pushed_undo = false;
                            }
                            break;
                        }
                        cur_y += th;
                    }
                    continue;
                }

                if (mx >= g_daw.tcp_width && my > header_top_y && my < win_h - SCROLLBAR_SIZE) {
                    double click_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;

                    bool hit_tl = false, hit_tr = false, hit_fi = false, hit_fo = false;
                    int hit_idx = hit_test_clip_interaction(p, mx, my, &hit_tl, &hit_tr, &hit_fi, &hit_fo);

                    if (hit_idx >= 0) {
                        AudioItem *it = &p->items[hit_idx];
                        push_undo_state();
                        if (ctrl_pressed) {
                            it->is_selected = !it->is_selected;
                            g_last_clicked_item_idx = hit_idx;
                        } else if (shift_pressed) {
                            int anchor = g_last_clicked_item_idx;
                            if (anchor < 0 || anchor >= p->num_items) {
                                for (int o = 0; o < p->num_items; o++) {
                                    if (p->items[o].is_selected) { anchor = o; break; }
                                }
                            }
                            if (anchor < 0 || anchor >= p->num_items || anchor == hit_idx) {
                                for (int o = 0; o < p->num_items; o++) p->items[o].is_selected = false;
                                it->is_selected = true;
                                g_last_clicked_item_idx = hit_idx;
                            } else {
                                int trk_a = p->items[anchor].track_idx;
                                int trk_b = it->track_idx;
                                int trk_min = (trk_a < trk_b) ? trk_a : trk_b;
                                int trk_max = (trk_a > trk_b) ? trk_a : trk_b;
                                double t_a = p->items[anchor].start_time;
                                double t_b = it->start_time;
                                double t_min = (t_a < t_b) ? t_a : t_b;
                                double t_max = (t_a > t_b) ? t_a : t_b;
                                for (int o = 0; o < p->num_items; o++) {
                                    AudioItem *cand = &p->items[o];
                                    if (!is_track_hidden(cand->track_idx) &&
                                        cand->track_idx >= trk_min && cand->track_idx <= trk_max &&
                                        cand->start_time >= t_min - 1e-4 && cand->start_time <= t_max + 1e-4) {
                                        cand->is_selected = true;
                                    } else {
                                        cand->is_selected = false;
                                    }
                                }
                            }
                        } else {
                            if (!it->is_selected) {
                                for (int o = 0; o < p->num_items; o++) p->items[o].is_selected = false;
                                it->is_selected = true;
                            }
                            g_last_clicked_item_idx = hit_idx;
                        }
                        dragging_item_idx = hit_idx;
                        drag_start_mouse_time = click_time;
                        item_orig_start = it->start_time;
                        item_orig_dur = it->duration;
                        item_orig_offset = it->offset_in_file;
                        for (int gi = 0; gi < p->num_items; gi++) {
                            g_drag_group_orig_start[gi] = p->items[gi].start_time;
                        }
                        dragging_trim_left  = hit_tl;
                        dragging_trim_right = hit_tr;
                        dragging_fade_in    = hit_fi;
                        dragging_fade_out   = hit_fo;
                    } else {
                        if (!ctrl_pressed && !shift_pressed) {
                            for (int o = 0; o < p->num_items; o++) p->items[o].is_selected = false;
                        }
                        marquee_selecting = true;
                        marquee_start_x = mx; marquee_start_y = my;
                        marquee_cur_x = mx; marquee_cur_y = my;
                    }
                    continue;
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                p->video.is_dragging = false; p->video.is_resizing = false; g_video_resize_edge = VID_RESIZE_NONE;
                g_export.is_dragging = false;
                g_hide_win_dragging = false;
                g_hide_sb_dragging = false;
                g_history_win_dragging = false;
                g_history_sb_dragging = false;
                g_action_win.is_dragging = false;
                g_action_win.sb_dragging = false;
                g_multi_import.is_dragging = false;
                dragging_scrollbar_h = false; dragging_scrollbar_v = false;
                scrubbing_ruler = false;
                dragging_range_left = false;
                dragging_range_right = false;
                dragging_range_body = false;
                resizing_tcp_width = false;
                resizing_track_idx = -1;
                dragging_track_order_idx = -1;
                dragging_track_order_pushed_undo = false;
                g_daw.script_win_dragging = false;
                g_script_sb_dragging = false;
                g_script_resize_edge = 0;
                if (g_theme.dragging_sv || g_theme.dragging_hue || g_theme.dragging_alpha) {
                    save_themes_settings_json();
                }
                g_theme.is_dragging = false;
                g_theme.dragging_sv = false;
                g_theme.dragging_hue = false;
                g_theme.dragging_alpha = false;
                g_theme.saved_sb_dragging = false;
                if (g_dyn_split.dragging_slider_id != 0) save_themes_settings_json();
                g_dyn_split.is_dragging = false;
                g_dyn_split.dragging_slider_id = 0;
                g_reposition.is_dragging = false;
                g_script_popup.is_dragging = false;
                g_script_popup.dragging_slider_idx = -1;
                if (dragging_item_idx != -1) {
                    if (dragging_fade_in) record_project_change("Adjusted Clip Fade-In", true);
                    else if (dragging_fade_out) record_project_change("Adjusted Clip Fade-Out", true);
                    else if (dragging_trim_left || dragging_trim_right) record_project_change("Trimmed Clip Edge", true);
                    else record_project_change("Moved Media Clip", true);
                }
                dragging_item_idx = -1;
                dragging_trim_left = false; dragging_trim_right = false;
                dragging_fade_in = false; dragging_fade_out = false;

                if (marquee_selecting) {
                    marquee_selecting = false;
                    int x1 = fmin(marquee_start_x, marquee_cur_x);
                    int x2 = fmax(marquee_start_x, marquee_cur_x);
                    int y1 = fmin(marquee_start_y, marquee_cur_y);
                    int y2 = fmax(marquee_start_y, marquee_cur_y);

                    for (int i = 0; i < p->num_items; i++) {
                        AudioItem *it = &p->items[i];
                        if (is_track_hidden(it->track_idx)) continue;
                        int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
                        int iw = (int)(it->duration * p->pixels_per_second);
                        int iy = get_track_y_pos(it->track_idx);
                        int ih = p->track_heights[it->track_idx];
                        if (ix < x2 && ix + iw > x1 && iy < y2 && iy + ih > y1) {
                            it->is_selected = true;
                        }
                    }
                }
            } else if (e.type == SDL_MOUSEMOTION) {
                int mx = e.motion.x, my = e.motion.y;

                if (g_script_popup.is_open && g_script_popup.is_dragging) {
                    g_script_popup.win_rect.x = mx - g_script_popup.drag_off_x;
                    g_script_popup.win_rect.y = my - g_script_popup.drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_script_popup.is_open && g_script_popup.dragging_slider_idx >= 0 &&
                    g_script_popup.dragging_slider_idx < g_script_popup.num_controls) {
                    ScriptPopupControl *ctrl = &g_script_popup.controls[g_script_popup.dragging_slider_idx];
                    int sl_x = g_script_popup.win_rect.x + 180;
                    int sl_w = g_script_popup.win_rect.w - 270;
                    if (sl_w < 80) sl_w = 80;
                    double u = (double)(mx - sl_x) / (double)sl_w;
                    if (u < 0.0) u = 0.0; else if (u > 1.0) u = 1.0;
                    ctrl->num_val = ctrl->min_val + u * (ctrl->max_val - ctrl->min_val);
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_reposition.is_open && g_reposition.is_dragging) {
                    g_reposition.win_rect.x = mx - g_reposition.drag_off_x;
                    g_reposition.win_rect.y = my - g_reposition.drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_dyn_split.is_open && g_dyn_split.is_dragging) {
                    g_dyn_split.win_rect.x = mx - g_dyn_split.drag_off_x;
                    g_dyn_split.win_rect.y = my - g_dyn_split.drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_dyn_split.is_open && g_dyn_split.dragging_slider_id != 0) {
                    int sl_x = g_dyn_split.win_rect.x + 162, sl_w = 232;
                    float u = (float)(mx - sl_x) / (float)sl_w;
                    if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
                    if (g_dyn_split.dragging_slider_id == 1) g_dyn_split.reduce_splits_val = 1 + (int)(u * 99.0f + 0.5f);
                    else if (g_dyn_split.dragging_slider_id == 2) g_dyn_split.min_slice_ms = 1 + (int)(u * 999.0f + 0.5f);
                    else if (g_dyn_split.dragging_slider_id == 3) g_dyn_split.min_silence_ms = 10 + (int)(u * 1990.0f + 0.5f);
                    else if (g_dyn_split.dragging_slider_id == 4) g_dyn_split.gate_threshold_db = -90.0f + u * 90.0f;
                    else if (g_dyn_split.dragging_slider_id == 5) g_dyn_split.gate_hysteresis_db = -24.0f + u * 24.0f;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }

                if (g_export.is_open && g_export.is_dragging) {
                    g_export.win_rect.x = mx - g_export.drag_off_x;
                    g_export.win_rect.y = my - g_export.drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }

                if (g_multi_import.is_open && g_multi_import.is_dragging) {
                    g_multi_import.win_rect.x = mx - g_multi_import.drag_off_x;
                    g_multi_import.win_rect.y = my - g_multi_import.drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }

                if (g_hide_popup_open && g_hide_win_dragging) {
                    g_hide_win_rect.x = mx - g_hide_drag_off_x;
                    g_hide_win_rect.y = my - g_hide_drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }

                if (g_hide_popup_open && g_hide_sb_dragging) {
                    int list_h = g_hide_win_rect.h - 106;
                    int content_h = p->num_tracks * 26 + 8;
                    int max_sc = (content_h > list_h) ? (content_h - list_h) : 0;
                    if (max_sc > 0 && list_h > 0) {
                        int delta_y = my - g_hide_sb_start_my;
                        g_hide_scroll_y = g_hide_sb_orig_scroll + (delta_y * content_h) / list_h;
                        if (g_hide_scroll_y < 0) g_hide_scroll_y = 0;
                        if (g_hide_scroll_y > max_sc) g_hide_scroll_y = max_sc;
                    }
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }

                if (g_action_win.is_open && g_action_win.is_dragging) {
                    g_action_win.win_rect.x = mx - g_action_win.drag_off_x;
                    g_action_win.win_rect.y = my - g_action_win.drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }

                if (g_action_win.is_open && g_action_win.sb_dragging) {
                    int rows_h = (g_action_win.win_rect.h - 220) - 24;
                    int content_h = g_action_win.num_filtered * 22 + 6;
                    int max_sc = (content_h > rows_h) ? (content_h - rows_h) : 0;
                    if (max_sc > 0 && rows_h > 0) {
                        int delta_y = my - g_action_win.sb_start_my;
                        g_action_win.scroll_y = g_action_win.sb_orig_scroll + (delta_y * content_h) / rows_h;
                        if (g_action_win.scroll_y < 0) g_action_win.scroll_y = 0;
                        if (g_action_win.scroll_y > max_sc) g_action_win.scroll_y = max_sc;
                    }
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }

                if (g_history_popup_open && g_history_win_dragging) {
                    g_history_win_rect.x = mx - g_history_drag_off_x;
                    g_history_win_rect.y = my - g_history_drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }

                if (g_history_popup_open && g_history_sb_dragging) {
                    int tab_i = get_valid_tab_idx();
                    int list_h = g_history_win_rect.h - 86;
                    int content_h = g_tab_history_count[tab_i] * 24 + 8;
                    int max_sc = (content_h > list_h) ? (content_h - list_h) : 0;
                    if (max_sc > 0 && list_h > 0) {
                        int delta_y = my - g_history_sb_start_my;
                        g_history_scroll_y = g_history_sb_orig_scroll + (delta_y * content_h) / list_h;
                        if (g_history_scroll_y < 0) g_history_scroll_y = 0;
                        if (g_history_scroll_y > max_sc) g_history_scroll_y = max_sc;
                    }
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }

                if (g_theme.is_open && g_theme.is_dragging) {
                    g_theme.win_rect.x = mx - g_theme.drag_off_x;
                    g_theme.win_rect.y = my - g_theme.drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_theme.is_open && g_theme.dragging_sv) {
                    int p_x = g_theme.win_rect.x + 18;
                    int sv_y = g_theme.win_rect.y + 44, sv_w = 256, sv_h = 164;
                    g_theme.s = (float)(mx - p_x) / (float)(sv_w - 1);
                    g_theme.v = 1.0f - ((float)(my - sv_y) / (float)(sv_h - 1));
                    if (g_theme.s < 0.0f) g_theme.s = 0.0f; else if (g_theme.s > 1.0f) g_theme.s = 1.0f;
                    if (g_theme.v < 0.0f) g_theme.v = 0.0f; else if (g_theme.v > 1.0f) g_theme.v = 1.0f;
                    theme_hsv_to_rgb(g_theme.h, g_theme.s, g_theme.v, &g_theme.r, &g_theme.g, &g_theme.b);
                    sync_theme_code_string();
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_theme.is_open && g_theme.dragging_hue) {
                    int p_x = g_theme.win_rect.x + 18, sv_w = 256;
                    g_theme.h = ((float)(mx - p_x) / (float)(sv_w - 1)) * 360.0f;
                    if (g_theme.h < 0.0f) g_theme.h = 0.0f; else if (g_theme.h > 359.9f) g_theme.h = 359.9f;
                    theme_hsv_to_rgb(g_theme.h, g_theme.s, g_theme.v, &g_theme.r, &g_theme.g, &g_theme.b);
                    sync_theme_code_string();
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_theme.is_open && g_theme.dragging_alpha) {
                    int p_x = g_theme.win_rect.x + 18, sv_w = 256;
                    float af = (float)(mx - p_x) / (float)(sv_w - 1);
                    if (af < 0.0f) af = 0.0f; else if (af > 1.0f) af = 1.0f;
                    g_theme.a = (uint8_t)(af * 255.0f + 0.5f);
                    sync_theme_code_string();
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_theme.is_open && g_theme.saved_sb_dragging) {
                    int list_h = 316;
                    int content_h = g_theme.num_saved * 28 + 8;
                    int max_sc = (content_h > list_h) ? (content_h - list_h) : 0;
                    if (max_sc > 0 && list_h > 0) {
                        int delta_y = my - g_theme.saved_sb_start_my;
                        g_theme.saved_scroll_y = g_theme.saved_sb_orig_scroll + (delta_y * content_h) / list_h;
                        if (g_theme.saved_scroll_y < 0) g_theme.saved_scroll_y = 0;
                        if (g_theme.saved_scroll_y > max_sc) g_theme.saved_scroll_y = max_sc;
                    }
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_daw.script_manager_open && g_script_resize_edge != 0) {
                    int dx = mx - g_script_resize_start_mx;
                    int dy = my - g_script_resize_start_my;
                    SDL_Rect nr = g_script_resize_orig_rect;
                    if (g_script_resize_edge == VID_RESIZE_RIGHT || g_script_resize_edge == VID_RESIZE_BOTTOM_RIGHT) {
                        nr.w = g_script_resize_orig_rect.w + dx;
                        if (nr.w < 360) nr.w = 360;
                        if (nr.w > win_w - 20) nr.w = win_w - 20;
                    }
                    if (g_script_resize_edge == VID_RESIZE_LEFT || g_script_resize_edge == VID_RESIZE_BOTTOM_LEFT) {
                        int nw = g_script_resize_orig_rect.w - dx;
                        if (nw < 360) nw = 360;
                        if (nw > win_w - 20) nw = win_w - 20;
                        nr.x = (g_script_resize_orig_rect.x + g_script_resize_orig_rect.w) - nw;
                        nr.w = nw;
                    }
                    if (g_script_resize_edge == VID_RESIZE_BOTTOM ||
                        g_script_resize_edge == VID_RESIZE_BOTTOM_LEFT ||
                        g_script_resize_edge == VID_RESIZE_BOTTOM_RIGHT) {
                        nr.h = g_script_resize_orig_rect.h + dy;
                        if (nr.h < 220) nr.h = 220;
                        if (nr.h > win_h - 20) nr.h = win_h - 20;
                    }
                    g_daw.script_win_rect = nr;
                    SDL_SetCursor((g_script_resize_edge == VID_RESIZE_BOTTOM) ? g_cursor_size_ns : g_cursor_size_we);
                    continue;
                }
                if (g_daw.script_manager_open && g_script_sb_dragging) {
                    int list_h = g_daw.script_win_rect.h - 82;
                    int content_h = g_daw.num_scripts * 24 + 8;
                    int max_sc = (content_h > list_h) ? (content_h - list_h) : 0;
                    if (max_sc > 0 && list_h > 0) {
                        int delta_y = my - g_script_sb_start_my;
                        g_script_scroll_y = g_script_sb_orig_scroll + (delta_y * content_h) / list_h;
                        if (g_script_scroll_y < 0) g_script_scroll_y = 0;
                        if (g_script_scroll_y > max_sc) g_script_scroll_y = max_sc;
                    }
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_daw.script_win_dragging) {
                    g_daw.script_win_rect.x = mx - g_daw.script_drag_offset_x;
                    g_daw.script_win_rect.y = my - g_daw.script_drag_offset_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }

                if (p->video.is_resizing) {
                    double v_asp = (p->video.width > 0 && p->video.height > 0)
                                   ? ((double)p->video.width / (double)p->video.height)
                                   : (16.0 / 9.0);
                    int dx = mx - g_video_resize_start_mx;
                    int dy = my - g_video_resize_start_my;
                    double orig_w = (double)g_video_resize_orig_rect.w;
                    double orig_canvas_h = (double)(g_video_resize_orig_rect.h - 24);
                    if (orig_canvas_h < 90.0) orig_canvas_h = 90.0;

                    double target_w = orig_w;
                    if (g_video_resize_edge == VID_RESIZE_RIGHT) {
                        target_w = orig_w + (double)dx;
                    } else if (g_video_resize_edge == VID_RESIZE_LEFT) {
                        target_w = orig_w - (double)dx;
                    } else if (g_video_resize_edge == VID_RESIZE_BOTTOM) {
                        double target_ch = orig_canvas_h + (double)dy;
                        target_w = target_ch * v_asp;
                    } else if (g_video_resize_edge == VID_RESIZE_BOTTOM_LEFT) {
                        double dw_x = -(double)dx;
                        double dw_y = (double)dy * v_asp;
                        target_w = orig_w + ((fabs(dw_x) >= fabs(dw_y)) ? dw_x : dw_y);
                    } else {
                        // VID_RESIZE_BOTTOM_RIGHT
                        double dw_x = (double)dx;
                        double dw_y = (double)dy * v_asp;
                        target_w = orig_w + ((fabs(dw_x) >= fabs(dw_y)) ? dw_x : dw_y);
                    }

                    if (target_w < 220.0) target_w = 220.0;
                    if (target_w > (double)(win_w - 16)) target_w = (double)(win_w - 16);

                    int new_w = (int)(target_w + 0.5);
                    int new_h = 24 + (int)((target_w / v_asp) + 0.5);
                    if (new_h < 148) {
                        new_h = 148;
                        new_w = (int)((double)(new_h - 24) * v_asp + 0.5);
                    }
                    if (new_h > win_h - 16) {
                        new_h = win_h - 16;
                        new_w = (int)((double)(new_h - 24) * v_asp + 0.5);
                    }

                    if (g_video_resize_edge == VID_RESIZE_LEFT || g_video_resize_edge == VID_RESIZE_BOTTOM_LEFT) {
                        int right_anchor = g_video_resize_orig_rect.x + g_video_resize_orig_rect.w;
                        p->video.rect.x = right_anchor - new_w;
                    }
                    p->video.rect.w = new_w;
                    p->video.rect.h = new_h;
                    SDL_SetCursor((g_video_resize_edge == VID_RESIZE_BOTTOM) ? g_cursor_size_ns : g_cursor_size_we);
                    continue;
                }
                if (p->video.is_dragging) {
                    p->video.rect.x = mx - p->video.drag_x;
                    p->video.rect.y = my - p->video.drag_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }

                // Request #1: When cursor is over any popup window or dropdown menu, keep cursor focused on popup/menu, not background panel
                if (dragging_item_idx == -1 && !resizing_tcp_width && resizing_track_idx < 0 &&
                    dragging_track_order_idx < 0 && !dragging_range_left && !dragging_range_right && !dragging_range_body &&
                    !dragging_scrollbar_h && !dragging_scrollbar_v && !scrubbing_ruler) {
                    SDL_Cursor *popup_cur = g_cursor_arrow;
                    if (is_cursor_captured_by_popup_or_menu(p, mx, my, &popup_cur)) {
                        SDL_SetCursor(popup_cur);
                        continue;
                    }
                }

                int ruler_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                int header_top_y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT + m_lane_h;
                bool hovered_edge = false;
                bool hovered_ibeam = false;

                if (is_range_active() && mx >= g_daw.tcp_width &&
                    ((my >= ruler_y && my <= ruler_y + RULER_HEIGHT) || (my >= header_top_y && my <= header_top_y + 10))) {
                    int rx1 = g_daw.tcp_width + (int)((get_range_start() - p->scroll_x_sec) * p->pixels_per_second);
                    int rx2 = g_daw.tcp_width + (int)((get_range_end()   - p->scroll_x_sec) * p->pixels_per_second);
                    if (abs(mx - rx1) <= 8 || abs(mx - rx2) <= 8) {
                        SDL_SetCursor(g_cursor_size_we);
                        hovered_edge = true;
                    }
                }

                if (!hovered_edge && abs(mx - g_daw.tcp_width) <= 5 && my > TABBAR_HEIGHT + TOPBAR_HEIGHT) {
                    SDL_SetCursor(g_cursor_size_we);
                    hovered_edge = true;
                } else if (!hovered_edge && mx < g_daw.tcp_width && my > header_top_y) {
                    int cur_y = header_top_y - p->scroll_y_px;
                    for (int t = 0; t < p->num_tracks; t++) {
                        if (is_track_hidden(t)) continue;
                        int th = p->track_heights[t];
                        if (abs(my - (cur_y + th)) <= 5) {
                            SDL_SetCursor(g_cursor_size_ns);
                            hovered_edge = true;
                            break;
                        }
                        if (mx >= g_daw.tcp_width - 28 && mx <= g_daw.tcp_width - 6 && my >= cur_y + 6 && my <= cur_y + th - 6) {
                            SDL_SetCursor(g_cursor_arrow);
                            break;
                        }
                        if (my >= cur_y + 6 && my <= cur_y + 28 && mx >= 8 && mx <= g_daw.tcp_width - 32) {
                            SDL_SetCursor(g_cursor_ibeam);
                            hovered_ibeam = true;
                            break;
                        }
                        cur_y += th;
                    }
                } else if (!hovered_edge && mx >= g_daw.tcp_width && my > header_top_y && my < win_h - SCROLLBAR_SIZE) {
                    // Request #5: Use exact same hit_test_clip_interaction as mouse down so double-arrow ALWAYS means edge is draggable
                    bool h_tl = false, h_tr = false, h_fi = false, h_fo = false;
                    int h_idx = hit_test_clip_interaction(p, mx, my, &h_tl, &h_tr, &h_fi, &h_fo);
                    if (h_idx >= 0 && (h_tl || h_tr || h_fi || h_fo)) {
                        SDL_SetCursor(g_cursor_size_we);
                        hovered_edge = true;
                    }
                }
                if (!hovered_edge && !hovered_ibeam && !resizing_tcp_width && resizing_track_idx < 0 &&
                    dragging_track_order_idx < 0 && !dragging_range_left && !dragging_range_right &&
                    !dragging_fade_in && !dragging_fade_out && !dragging_trim_left && !dragging_trim_right) {
                    SDL_SetCursor(g_cursor_arrow);
                }

                if (dragging_range_left || dragging_range_right || dragging_range_body) {
                    double cur_mouse_sec = fmax(0.0, p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second);
                    if (dragging_range_left) {
                        SDL_SetCursor(g_cursor_size_we);
                        double new_s = cur_mouse_sec;
                        if (new_s > range_orig_end_sec - 0.05) new_s = range_orig_end_sec - 0.05;
                        if (new_s < 0.0) new_s = 0.0;
                        set_tab_range(new_s, range_orig_end_sec, true);
                    } else if (dragging_range_right) {
                        SDL_SetCursor(g_cursor_size_we);
                        double new_e = cur_mouse_sec;
                        if (new_e < range_orig_start_sec + 0.05) new_e = range_orig_start_sec + 0.05;
                        set_tab_range(range_orig_start_sec, new_e, true);
                    } else if (dragging_range_body) {
                        double delta_s = cur_mouse_sec - range_drag_start_mouse_sec;
                        double span = range_orig_end_sec - range_orig_start_sec;
                        double new_s = range_orig_start_sec + delta_s;
                        if (new_s < 0.0) new_s = 0.0;
                        set_tab_range(new_s, new_s + span, true);
                    }
                }

                if (marquee_selecting) {
                    marquee_cur_x = mx; marquee_cur_y = my;
                }

                if (resizing_tcp_width && mx >= 140 && mx <= 380) {
                    g_daw.tcp_width = mx;
                }
                if (resizing_track_idx >= 0) {
                    int delta = my - mouse_drag_start_y;
                    int new_h = track_orig_height + delta;
                    if (new_h >= 50 && new_h <= 260) p->track_heights[resizing_track_idx] = new_h;
                }

                if (dragging_track_order_idx >= 0 && dragging_track_order_idx < p->num_tracks) {
                    SDL_SetCursor(g_cursor_arrow);
                    int cur_y = header_top_y - p->scroll_y_px;
                    int target_trk = dragging_track_order_idx;
                    for (int t = 0; t < p->num_tracks; t++) {
                        if (is_track_hidden(t)) continue;
                        int th = p->track_heights[t];
                        int mid_y = cur_y + th / 2;
                        if (t < dragging_track_order_idx && my < mid_y) {
                            target_trk = t;
                            break;
                        } else if (t > dragging_track_order_idx && my > mid_y) {
                            target_trk = t;
                        }
                        cur_y += th;
                    }
                    if (target_trk != dragging_track_order_idx) {
                        if (!dragging_track_order_pushed_undo) {
                            push_undo_state();
                            dragging_track_order_pushed_undo = true;
                        }
                        while (dragging_track_order_idx > target_trk) {
                            swap_tracks(p, dragging_track_order_idx, dragging_track_order_idx - 1);
                            dragging_track_order_idx--;
                        }
                        while (dragging_track_order_idx < target_trk) {
                            swap_tracks(p, dragging_track_order_idx, dragging_track_order_idx + 1);
                            dragging_track_order_idx++;
                        }
                    }
                }



                if (dragging_scrollbar_h) {
                    float delta = (float)(mx - scrollbar_h_start_mx);
                    double max_x_scroll = (total_w_timeline > view_w) ? (double)(total_w_timeline - view_w) / p->pixels_per_second : 0.0;
                    p->scroll_x_sec = orig_scroll_x + (delta / (float)view_w) * (p->project_duration_sec);
                    if (p->scroll_x_sec < 0) p->scroll_x_sec = 0;
                    if (p->scroll_x_sec > max_x_scroll) p->scroll_x_sec = max_x_scroll;
                }
                if (dragging_scrollbar_v) {
                    float delta = (float)(my - scrollbar_v_start_my);
                    int max_y = max_h_tracks - view_h; if (max_y < 0) max_y = 0;
                    p->scroll_y_px = (int)(orig_scroll_y + (delta / view_h) * max_h_tracks);
                    if (p->scroll_y_px < 0) p->scroll_y_px = 0;
                    if (p->scroll_y_px > max_y) p->scroll_y_px = max_y;
                }
                if (scrubbing_ruler && mx >= g_daw.tcp_width && mx < win_w - SCROLLBAR_SIZE) {
                    p->playhead_seconds = fmax(0.0, p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second);
                }
                if (dragging_item_idx != -1) {
                    AudioItem *it = &p->items[dragging_item_idx];
                    double cur_mouse_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                    double delta_t = cur_mouse_time - drag_start_mouse_time;
                    bool xfade_on = is_crossfade_active();

                    if (dragging_fade_in) {
                        SDL_SetCursor(g_cursor_size_we);
                        double new_fin = cur_mouse_time - it->start_time;
                        set_item_fade_in(p, dragging_item_idx, new_fin);
                    } else if (dragging_fade_out) {
                        SDL_SetCursor(g_cursor_size_we);
                        double new_fout = (it->start_time + it->duration) - cur_mouse_time;
                        set_item_fade_out(p, dragging_item_idx, new_fout);
                    } else if (dragging_trim_left) {
                        double new_start = item_orig_start + delta_t;
                        if (new_start < 0.0) new_start = 0.0;
                        if (item_orig_offset + (new_start - item_orig_start) < 0.0) {
                            new_start = item_orig_start - item_orig_offset;
                        }
                        if (!xfade_on) {
                            for (int o = 0; o < p->num_items; o++) {
                                if (o == dragging_item_idx) continue;
                                AudioItem *ot = &p->items[o];
                                if (ot->track_idx != it->track_idx) continue;
                                double ot_e = ot->start_time + ot->duration;
                                if (ot_e <= item_orig_start + 1e-5 && new_start < ot_e) {
                                    new_start = ot_e;
                                }
                            }
                        }
                        double adj_delta = new_start - item_orig_start;
                        if (item_orig_dur - adj_delta > 0.1) {
                            it->start_time = new_start;
                            it->duration = item_orig_dur - adj_delta;
                            it->offset_in_file = item_orig_offset + adj_delta;
                            set_item_fade_in(p, dragging_item_idx, get_item_fade_in(p, dragging_item_idx));
                        }
                    } else if (dragging_trim_right) {
                        double new_dur = item_orig_dur + delta_t;
                        if (it->file_total_duration > 0.0 && it->offset_in_file + new_dur > it->file_total_duration) {
                            new_dur = it->file_total_duration - it->offset_in_file;
                        }
                        if (!xfade_on) {
                            for (int o = 0; o < p->num_items; o++) {
                                if (o == dragging_item_idx) continue;
                                AudioItem *ot = &p->items[o];
                                if (ot->track_idx != it->track_idx) continue;
                                if (ot->start_time >= item_orig_start + item_orig_dur - 1e-5) {
                                    double max_dur = ot->start_time - it->start_time;
                                    if (new_dur > max_dur) new_dur = max_dur;
                                }
                            }
                        }
                        if (new_dur > 0.1) {
                            it->duration = new_dur;
                            set_item_fade_out(p, dragging_item_idx, get_item_fade_out(p, dragging_item_idx));
                        }
                    } else {
                        int target_trk = it->track_idx;
                        int cur_y = header_top_y - p->scroll_y_px;
                        for (int t = 0; t < p->num_tracks; t++) {
                            if (is_track_hidden(t)) continue;
                            if (my >= cur_y && my < cur_y + p->track_heights[t]) { target_trk = t; break; }
                            cur_y += p->track_heights[t];
                        }
                        double desired_start = fmax(0.0, item_orig_start + delta_t);
                        it->track_idx = target_trk;
                        if (!xfade_on) {
                            it->start_time = clamp_non_overlapping_start(p, dragging_item_idx, target_trk, desired_start, it->duration);
                        } else {
                            it->start_time = desired_start;
                        }
                        double actual_delta = it->start_time - item_orig_start;
                        int my_grp = g_item_group_id[get_valid_tab_idx()][dragging_item_idx];
                        if (my_grp > 0) {
                            for (int gi = 0; gi < p->num_items; gi++) {
                                if (gi == dragging_item_idx) continue;
                                if (g_item_group_id[get_valid_tab_idx()][gi] == my_grp) {
                                    p->items[gi].start_time = fmax(0.0, g_drag_group_orig_start[gi] + actual_delta);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Process any drag-and-drop media files collected in this frame
        if (g_drop_batch_count > 0) {
            if (g_drop_batch_count == 1) {
                import_single_media_item(p, ren, win, g_drop_batch_files[0], g_drop_batch_track, g_drop_batch_time);
            } else {
                g_multi_import.num_files = g_drop_batch_count;
                for (int k = 0; k < g_drop_batch_count; k++) {
                    strncpy(g_multi_import.files[k], g_drop_batch_files[k], 511);
                    g_multi_import.files[k][511] = '\0';
                }
                g_multi_import.base_track = g_drop_batch_track;
                g_multi_import.base_time = g_drop_batch_time;
                g_multi_import.mode = MULTI_IMPORT_SEPARATE_TRACKS;
                g_multi_import.win_rect.x = (win_w - g_multi_import.win_rect.w) / 2;
                g_multi_import.win_rect.y = (win_h - g_multi_import.win_rect.h) / 2;
                g_multi_import.is_open = true;
            }
            g_drop_batch_count = 0;
        }

        // ================= TOP-TRACK VIDEO SYNCHRONIZATION =================
        if (p->video.is_open) {
            AudioItem *v_clip = get_active_video_clip_at_playhead(p);

            if (v_clip != NULL) {
                double media_internal_time = (p->playhead_seconds - v_clip->start_time) + v_clip->offset_in_file;
                
                // Load the original media container (file_path), NOT the raw PCM audio cache (raw_audio_path)
                if (strncmp(p->video.current_loaded_path, v_clip->file_path, sizeof(p->video.current_loaded_path)) != 0 || !p->video.texture) {
                    open_video_by_path(&p->video, v_clip->file_path, ren);
                }

                if (p->video.texture) {
                    sync_video_to_audio_clock(&p->video, media_internal_time, scrubbing_ruler || !p->is_playing);
                    p->video.has_active_frame = true;
                } else {
                    p->video.has_active_frame = false;
                }
            } else {
                p->video.has_active_frame = false;
            }
        }

        // ================= RENDERING =================
        SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
        SDL_RenderClear(ren);

        SDL_Color c_white = { 224, 224, 224, 255 };
        SDL_Color c_muted = { 130, 130, 130, 255 };

        // 1. Google Chrome Style Tab Bar (with Brown Borders)
        SDL_Rect tab_bar = { 0, 0, win_w, TABBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 18, 18, 18, 255);
        SDL_RenderFillRect(ren, &tab_bar);
        draw_brown_border_rect(ren, &tab_bar, false, true);

        int max_tab_w = 160;
        int add_btn_w = 34;
        int cur_tab_w = max_tab_w;
        if (g_daw.num_tabs > 0) {
            int calc_w = (win_w - add_btn_w - 20) / g_daw.num_tabs;
            if (calc_w < cur_tab_w) cur_tab_w = calc_w;
            if (cur_tab_w < 70) cur_tab_w = 70;
        }

        int cur_tab_x = 0;
        for (int t = 0; t < g_daw.num_tabs; t++) {
            bool is_active = (t == g_daw.active_tab);
            SDL_Rect tr = { cur_tab_x, 2, cur_tab_w - 2, TABBAR_HEIGHT - 2 };
            SDL_SetRenderDrawColor(ren, is_active ? 40 : 22, is_active ? 34 : 22, is_active ? 28 : 22, 255);
            SDL_RenderFillRect(ren, &tr);
            draw_brown_border_rect(ren, &tr, is_active, !is_active);

            strip_ehs_extension(g_daw.tabs[t].project_name);
            char label_buf[70];
            if (g_daw.tabs[t].is_dirty) {
                snprintf(label_buf, sizeof(label_buf), "*%s", g_daw.tabs[t].project_name);
            } else {
                snprintf(label_buf, sizeof(label_buf), "%s", g_daw.tabs[t].project_name);
            }
            SDL_Rect tab_lbl_bounds = { cur_tab_x + 4, 2, cur_tab_w - 26, TABBAR_HEIGHT - 4 };
            draw_text_clipped(ren, label_buf, cur_tab_x + 10, 9, is_active ? c_white : c_muted, &tab_lbl_bounds, NULL);

            draw_text(ren, "x", cur_tab_x + cur_tab_w - 18, 9, is_active ? c_white : c_muted);
            cur_tab_x += cur_tab_w;
        }

        SDL_Rect btn_add_tab = { cur_tab_x, 2, add_btn_w, TABBAR_HEIGHT - 2 };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &btn_add_tab);
        draw_brown_border_rect(ren, &btn_add_tab, false, false);
        draw_text(ren, "+", cur_tab_x + 12, 9, c_white);

        // Request #6: Project Change Notification Badge in the Top Tab Bar Section
        int notif_x = cur_tab_x + add_btn_w + 10;
        int notif_w = win_w - notif_x - 8;
        if (notif_w > 120) {
            int cur_t_idx = get_valid_tab_idx();
            SDL_Rect notif_box = { notif_x, 3, notif_w, TABBAR_HEIGHT - 6 };
            SDL_SetRenderDrawColor(ren, p->is_dirty ? 36 : 24, p->is_dirty ? 28 : 24, p->is_dirty ? 22 : 24, 255);
            SDL_RenderFillRect(ren, &notif_box);
            draw_brown_border_rect(ren, &notif_box, p->is_dirty, !p->is_dirty);

            // Indicator dot
            SDL_Rect dot_r = { notif_x + 8, 11, 8, 8 };
            if (p->is_dirty) {
                SDL_SetRenderDrawColor(ren, 235, 145, 45, 255);
            } else {
                SDL_SetRenderDrawColor(ren, 95, 175, 95, 255);
            }
            SDL_RenderFillRect(ren, &dot_r);

            char notif_disp[200];
            if (g_tab_notification[cur_t_idx][0] != '\0') {
                snprintf(notif_disp, sizeof(notif_disp), "%s", g_tab_notification[cur_t_idx]);
            } else {
                snprintf(notif_disp, sizeof(notif_disp), "Ready");
            }
            SDL_Rect notif_clip = { notif_x + 22, 4, notif_w - 28, TABBAR_HEIGHT - 8 };
            draw_text_clipped(ren, notif_disp, notif_x + 22, 9, c_white, &notif_clip, NULL);
        }

        // 2. Top Bar (Left: File Action Tools Video | Right: Undo Redo Zoom- Zoom+ Range Crossfade Hide Save)
        SDL_Rect topbar = { 0, TABBAR_HEIGHT, win_w, TOPBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 28, 28, 28, 255);
        SDL_RenderFillRect(ren, &topbar);
        draw_brown_border_rect(ren, &topbar, false, true);

        // Left-side top menu: File | Action | Tools | Video
        SDL_Rect btn_file = { 8, TABBAR_HEIGHT + 5, 46, 26 };
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderFillRect(ren, &btn_file);
        draw_brown_border_rect(ren, &btn_file, g_daw.menu_file_open, false);
        draw_text(ren, "File", 16, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect btn_act = { 60, TABBAR_HEIGHT + 5, 60, 26 };
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderFillRect(ren, &btn_act);
        draw_brown_border_rect(ren, &btn_act, g_daw.menu_action_open, false);
        draw_text(ren, "Action", 66, TABBAR_HEIGHT + 13, c_white);

        bool tools_active = g_menu_tools_open || g_history_popup_open || g_theme.is_open || p->show_marker_lane;
        SDL_Rect btn_tools = { 126, TABBAR_HEIGHT + 5, 58, 26 };
        SDL_SetRenderDrawColor(ren, tools_active ? 62 : 40, tools_active ? 46 : 40, tools_active ? 32 : 40, 255);
        SDL_RenderFillRect(ren, &btn_tools);
        draw_brown_border_rect(ren, &btn_tools, g_menu_tools_open, false);
        draw_text(ren, "Tools", 135, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect v_menu = { 190, TABBAR_HEIGHT + 5, 60, 26 };
        SDL_SetRenderDrawColor(ren, p->video.is_open ? 62 : 40, p->video.is_open ? 46 : 40, p->video.is_open ? 32 : 40, 255);
        SDL_RenderFillRect(ren, &v_menu);
        draw_brown_border_rect(ren, &v_menu, p->video.is_open, false);
        draw_text(ren, "Video", 200, TABBAR_HEIGHT + 13, c_white);

        // Center Timecode Display + Red Round Recording Indicator Dot Near Time Clock
        char clock_buf[32];
        format_timecode(p->playhead_seconds, clock_buf, sizeof(clock_buf));
        int left_menu_end = 266;
        int right_menu_start = win_w - 526;
        int clock_x = left_menu_end + ((right_menu_start - left_menu_end) / 2) - 54;
        if (clock_x < left_menu_end) clock_x = left_menu_end;
        SDL_Rect clock_bg = { clock_x - 10, TABBAR_HEIGHT + 4, 128, 28 };
        SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
        SDL_RenderFillRect(ren, &clock_bg);
        draw_brown_border_rect(ren, &clock_bg, g_rec.is_recording, false);
        draw_text(ren, clock_buf, clock_x - 4, TABBAR_HEIGHT + 12, c_white);

        if (g_rec.is_recording) {
            // Bright red round dot right near the time clock (pulses subtly while recording)
            int dot_cx = clock_bg.x + clock_bg.w + 14;
            int dot_cy = TABBAR_HEIGHT + 18;
            draw_filled_circle_sdl(ren, dot_cx, dot_cy, 8, 95, 18, 18, 255);
            draw_filled_circle_sdl(ren, dot_cx, dot_cy, 6, 242, 32, 32, 255);
            if (((SDL_GetTicks() / 350) % 2) == 0) {
                draw_filled_circle_sdl(ren, dot_cx - 2, dot_cy - 2, 2, 255, 145, 145, 255);
            }
        }

        // Right-side top menu: [Undo Symbol] | [Redo Symbol] | Range | Crossfade | Hide | Save (Zoom- / Zoom+ removed)
        SDL_Rect btn_undo = { win_w - 372, TABBAR_HEIGHT + 5, 40, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_undo);
        draw_brown_border_rect(ren, &btn_undo, false, false);
        draw_undo_symbol_icon(ren, btn_undo.x + 11, btn_undo.y + 5, c_white);

        SDL_Rect btn_redo = { win_w - 326, TABBAR_HEIGHT + 5, 40, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_redo);
        draw_brown_border_rect(ren, &btn_redo, false, false);
        draw_redo_symbol_icon(ren, btn_redo.x + 11, btn_redo.y + 5, c_white);

        bool range_on = is_range_active();
        SDL_Rect range_btn = { win_w - 280, TABBAR_HEIGHT + 5, 60, 26 };
        SDL_SetRenderDrawColor(ren, range_on ? 95 : 40, range_on ? 78 : 40, range_on ? 22 : 40, 255);
        SDL_RenderFillRect(ren, &range_btn);
        draw_brown_border_rect(ren, &range_btn, range_on, false);
        draw_text(ren, "Range", win_w - 270, TABBAR_HEIGHT + 13, c_white);

        bool xfade_active = is_crossfade_active();
        SDL_Rect xfade_btn = { win_w - 214, TABBAR_HEIGHT + 5, 90, 26 };
        SDL_SetRenderDrawColor(ren, xfade_active ? 62 : 40, xfade_active ? 46 : 40, xfade_active ? 32 : 40, 255);
        SDL_RenderFillRect(ren, &xfade_btn);
        draw_brown_border_rect(ren, &xfade_btn, xfade_active, false);
        draw_text(ren, "Crossfade", win_w - 205, TABBAR_HEIGHT + 13, c_white);

        bool hide_active = g_hide_popup_open || has_any_hidden_track(p);
        SDL_Rect hide_btn = { win_w - 118, TABBAR_HEIGHT + 5, 52, 26 };
        SDL_SetRenderDrawColor(ren, hide_active ? 62 : 40, hide_active ? 46 : 40, hide_active ? 32 : 40, 255);
        SDL_RenderFillRect(ren, &hide_btn);
        draw_brown_border_rect(ren, &hide_btn, hide_active, false);
        draw_text(ren, "Hide", win_w - 108, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect save_btn = { win_w - 60, TABBAR_HEIGHT + 5, 52, 26 };
        SDL_SetRenderDrawColor(ren, p->is_dirty ? 68 : 42, p->is_dirty ? 48 : 42, p->is_dirty ? 32 : 42, 255);
        SDL_RenderFillRect(ren, &save_btn);
        draw_brown_border_rect(ren, &save_btn, p->is_dirty, false);
        draw_text(ren, "Save", win_w - 50, TABBAR_HEIGHT + 13, c_white);

        // 3. Corner Header (+ Track and Play/Stop; Recording & Browse moved to Tools -> Record submenu)
        int corner_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
        SDL_Rect corner_area = { 0, corner_y, g_daw.tcp_width, RULER_HEIGHT + m_lane_h };
        SDL_SetRenderDrawColor(ren, 32, 32, 32, 255);
        SDL_RenderFillRect(ren, &corner_area);
        draw_brown_border_rect(ren, &corner_area, false, true);

        SDL_Rect btn_corner_add = { 8, corner_y + 8, 76, 24 };
        SDL_SetRenderDrawColor(ren, 48, 48, 48, 255);
        SDL_RenderFillRect(ren, &btn_corner_add);
        draw_brown_border_rect(ren, &btn_corner_add, false, false);
        draw_text(ren, "+ Track", 18, corner_y + 15, c_white);

        SDL_Rect btn_corner_play = { 94, corner_y + 8, 76, 24 };
        SDL_SetRenderDrawColor(ren, p->is_playing ? 68 : 48, p->is_playing ? 50 : 48, p->is_playing ? 34 : 48, 255);
        SDL_RenderFillRect(ren, &btn_corner_play);
        draw_brown_border_rect(ren, &btn_corner_play, p->is_playing, false);
        draw_text(ren, p->is_playing ? "Stop" : "Play", 116, corner_y + 15, c_white);

        // 4. Time Ruler & Logic Pro Yellow Cycle Range Strip
        SDL_Rect ruler = { g_daw.tcp_width, corner_y, view_w, RULER_HEIGHT };
        SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
        SDL_RenderFillRect(ren, &ruler);
        draw_brown_border_rect(ren, &ruler, false, true);

        double min_px_gap = 90.0;
        double sec_per_tick = min_px_gap / p->pixels_per_second;
        double nice_steps[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0 };
        int num_steps = sizeof(nice_steps) / sizeof(nice_steps[0]);
        double chosen_step = nice_steps[num_steps - 1];
        for (int s = 0; s < num_steps; s++) {
            if (nice_steps[s] >= sec_per_tick) { chosen_step = nice_steps[s]; break; }
        }

        double start_tick = floor(p->scroll_x_sec / chosen_step) * chosen_step;
        double end_tick = p->scroll_x_sec + (double)view_w / p->pixels_per_second + chosen_step;

        for (double t = start_tick; t <= end_tick; t += chosen_step) {
            int rx = g_daw.tcp_width + (int)((t - p->scroll_x_sec) * p->pixels_per_second);
            if (rx < g_daw.tcp_width || rx > g_daw.tcp_width + view_w) continue;

            char t_str[32];
            if (chosen_step < 1.0) snprintf(t_str, sizeof(t_str), "%.2fs", t);
            else if (t < 60.0) snprintf(t_str, sizeof(t_str), "%02ds", (int)t);
            else if (t < 3600.0) {
                int m = (int)(t / 60); int s = (int)(t - m * 60);
                snprintf(t_str, sizeof(t_str), "%02d:%02d", m, s);
            } else {
                int h = (int)(t / 3600); int m = (int)((t - h * 3600) / 60);
                snprintf(t_str, sizeof(t_str), "%02d:%02d:00", h, m);
            }
            draw_text(ren, t_str, rx + 4, corner_y + 4, c_muted);
            SDL_SetRenderDrawColor(ren, 118, 78, 46, 255);
            SDL_RenderDrawLine(ren, rx, corner_y + 15, rx, corner_y + RULER_HEIGHT);
        }

        // Render Logic Pro Yellow Cycle Strip in the Time Ruler when Range is active
        if (range_on) {
            SDL_RenderSetClipRect(ren, &ruler);
            int rx1 = g_daw.tcp_width + (int)((get_range_start() - p->scroll_x_sec) * p->pixels_per_second);
            int rx2 = g_daw.tcp_width + (int)((get_range_end()   - p->scroll_x_sec) * p->pixels_per_second);
            int rw = rx2 - rx1;
            if (rw < 4) rw = 4;

            SDL_Rect cycle_ruler_strip = { rx1, corner_y + RULER_HEIGHT - 9, rw, 8 };
            SDL_SetRenderDrawColor(ren, 238, 196, 24, 255);
            SDL_RenderFillRect(ren, &cycle_ruler_strip);

            // Draggable Left & Right Edge Caps on the Yellow Strip
            SDL_Rect cap_l = { rx1, corner_y + RULER_HEIGHT - 10, 4, 9 };
            SDL_Rect cap_r = { rx1 + rw - 4, corner_y + RULER_HEIGHT - 10, 4, 9 };
            SDL_SetRenderDrawColor(ren, 255, 235, 95, 255);
            SDL_RenderFillRect(ren, &cap_l);
            SDL_RenderFillRect(ren, &cap_r);
            SDL_RenderSetClipRect(ren, NULL);
        }

        int marker_y = corner_y + RULER_HEIGHT;
        if (p->show_marker_lane) {
            SDL_Rect marker_clip = { g_daw.tcp_width, marker_y, view_w, MARKER_BAR_HEIGHT };
            SDL_RenderSetClipRect(ren, &marker_clip);

            SDL_Rect marker_bg = { g_daw.tcp_width, marker_y, view_w, MARKER_BAR_HEIGHT };
            SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
            SDL_RenderFillRect(ren, &marker_bg);
            draw_brown_border_rect(ren, &marker_bg, false, true);

            // Pass 1: Draw all non-editing markers (clamped so they never cross the next marker)
            for (int m = 0; m < p->num_markers; m++) {
                bool is_editing_this = (g_daw.input_focus == INPUT_FOCUS_MARKER && g_daw.editing_marker_idx == m);
                if (is_editing_this) continue;

                int mk_x = g_daw.tcp_width + (int)((p->markers[m].time_sec - p->scroll_x_sec) * p->pixels_per_second);
                bool is_selected_this = (g_selected_marker_idx == m);
                const char *disp_txt = get_marker_full_str(p, m);
                int vis_w = get_marker_clamped_width(p, m, mk_x);

                if (mk_x + vis_w >= g_daw.tcp_width && mk_x <= g_daw.tcp_width + view_w) {
                    SDL_Rect m_tag = { mk_x, marker_y + 2, vis_w, MARKER_BAR_HEIGHT - 4 };
                    SDL_SetRenderDrawColor(ren,
                        is_selected_this ? 48 : 36,
                        is_selected_this ? 36 : 30,
                        is_selected_this ? 26 : 24,
                        255);
                    SDL_RenderFillRect(ren, &m_tag);

                    // Top/bottom border in brown
                    draw_brown_border_rect(ren, &m_tag, is_selected_this, false);

                    // Left and right edges of marker text box in bright Orange
                    SDL_SetRenderDrawColor(ren, 255, 140, 0, 255);
                    SDL_RenderDrawLine(ren, m_tag.x, m_tag.y, m_tag.x, m_tag.y + m_tag.h - 1);
                    if (m_tag.w > 1) {
                        SDL_RenderDrawLine(ren, m_tag.x + 1, m_tag.y, m_tag.x + 1, m_tag.y + m_tag.h - 1);
                        SDL_RenderDrawLine(ren, m_tag.x + m_tag.w - 1, m_tag.y, m_tag.x + m_tag.w - 1, m_tag.y + m_tag.h - 1);
                    }
                    if (m_tag.w > 3) {
                        SDL_RenderDrawLine(ren, m_tag.x + m_tag.w - 2, m_tag.y, m_tag.x + m_tag.w - 2, m_tag.y + m_tag.h - 1);
                    }

                    // Clip marker text so it only shows until the next marker (inside the orange edges)
                    SDL_Rect txt_bounds = { m_tag.x + 3, m_tag.y + 1, m_tag.w - 6, m_tag.h - 2 };
                    draw_text_clipped(ren, disp_txt, mk_x + 6, marker_y + 6, c_white, &txt_bounds, &marker_clip);
                }
            }

            // Pass 2: Draw the currently-editing (double-clicked) marker on top with its complete text visible
            if (g_daw.input_focus == INPUT_FOCUS_MARKER &&
                g_daw.editing_marker_idx >= 0 && g_daw.editing_marker_idx < p->num_markers) {
                int m = g_daw.editing_marker_idx;
                int mk_x = g_daw.tcp_width + (int)((p->markers[m].time_sec - p->scroll_x_sec) * p->pixels_per_second);
                const char *disp_txt = g_editing_marker_full_buf;
                int full_w = get_marker_tag_width(disp_txt);

                if (mk_x + full_w >= g_daw.tcp_width && mk_x <= g_daw.tcp_width + view_w) {
                    SDL_Rect m_tag = { mk_x, marker_y + 2, full_w, MARKER_BAR_HEIGHT - 4 };
                    SDL_SetRenderDrawColor(ren, 56, 40, 26, 255);
                    SDL_RenderFillRect(ren, &m_tag);

                    // Top/bottom border in thick brown
                    draw_brown_border_rect(ren, &m_tag, true, false);

                    // Left and right edges of marker text box in bright Orange
                    SDL_SetRenderDrawColor(ren, 255, 140, 0, 255);
                    SDL_RenderDrawLine(ren, m_tag.x, m_tag.y, m_tag.x, m_tag.y + m_tag.h - 1);
                    SDL_RenderDrawLine(ren, m_tag.x + 1, m_tag.y, m_tag.x + 1, m_tag.y + m_tag.h - 1);
                    SDL_RenderDrawLine(ren, m_tag.x + m_tag.w - 1, m_tag.y, m_tag.x + m_tag.w - 1, m_tag.y + m_tag.h - 1);
                    SDL_RenderDrawLine(ren, m_tag.x + m_tag.w - 2, m_tag.y, m_tag.x + m_tag.w - 2, m_tag.y + m_tag.h - 1);

                    SDL_Rect txt_bounds = { m_tag.x + 3, m_tag.y + 1, m_tag.w - 6, m_tag.h - 2 };
                    draw_text_clipped(ren, disp_txt, mk_x + 6, marker_y + 6, c_white, &txt_bounds, &marker_clip);

                    // Blinking Typing Cursor
                    int cur_pos = g_marker_cursor_pos;
                    int max_l = (int)strlen(g_editing_marker_full_buf);
                    if (cur_pos < 0) cur_pos = 0;
                    if (cur_pos > max_l) cur_pos = max_l;
                    int cx = mk_x + 6 + cur_pos * 8;
                    if ((SDL_GetTicks() / 450) % 2 == 0) {
                        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                        SDL_RenderDrawLine(ren, cx, marker_y + 4, cx, marker_y + MARKER_BAR_HEIGHT - 4);
                        SDL_RenderDrawLine(ren, cx + 1, marker_y + 4, cx + 1, marker_y + MARKER_BAR_HEIGHT - 4);
                    }
                }
            }

            SDL_RenderSetClipRect(ren, NULL);
        }

        int header_top_y = marker_y + m_lane_h;
        SDL_Rect timeline_clip = { g_daw.tcp_width, header_top_y, view_w, view_h };
        SDL_RenderSetClipRect(ren, &timeline_clip);

        // Subtle vertical timeline grid lines in faded brown
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        uint8_t th_r, th_g, th_b;
        get_theme_effective_rgb(&th_r, &th_g, &th_b);
        for (double t = start_tick; t <= end_tick; t += chosen_step) {
            int gx = g_daw.tcp_width + (int)((t - p->scroll_x_sec) * p->pixels_per_second);
            if (gx < g_daw.tcp_width || gx > g_daw.tcp_width + view_w) continue;
            SDL_SetRenderDrawColor(ren, (uint8_t)((th_r * 58) / 100), (uint8_t)((th_g * 57) / 100), (uint8_t)((th_b * 63) / 100), 65);
            SDL_RenderDrawLine(ren, gx, header_top_y, gx, header_top_y + view_h);
        }

        // Timeline tracks: Each track has an identical simple faded theme edge; selected track edge is slightly thicker
        int track_cur_y = header_top_y - p->scroll_y_px;
        for (int i = 0; i < p->num_tracks; i++) {
            if (is_track_hidden(i)) continue;
            int th = p->track_heights[i];
            SDL_Rect lane_border = { g_daw.tcp_width, track_cur_y, view_w, th };
            if (p->track_selected[i]) {
                SDL_SetRenderDrawColor(ren, th_r, th_g, th_b, 235);
                SDL_RenderDrawRect(ren, &lane_border);
                if (th > 4 && view_w > 4) {
                    SDL_Rect lane_inner = { g_daw.tcp_width + 1, track_cur_y + 1, view_w - 2, th - 2 };
                    SDL_RenderDrawRect(ren, &lane_inner);
                }
            } else {
                SDL_SetRenderDrawColor(ren, (uint8_t)((th_r * 58) / 100), (uint8_t)((th_g * 57) / 100), (uint8_t)((th_b * 63) / 100), 135);
                SDL_RenderDrawRect(ren, &lane_border);
            }
            track_cur_y += th;
        }
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

        // 5. Render Clips (Pass 1A: Clip Backgrounds, Pass 1B: Faded Waveforms, Manual Fade Curves/Handles & Clipped Labels, Pass 2: Crossfade Overlays)
        for (int i = 0; i < p->num_items; i++) {
            AudioItem *it = &p->items[i];
            if (is_track_hidden(it->track_idx)) continue;
            int iy = get_track_y_pos(it->track_idx) + 2;
            int ih = p->track_heights[it->track_idx] - 4;
            int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
            int iw = (int)(it->duration * p->pixels_per_second);

            if (ix + iw < g_daw.tcp_width || ix > g_daw.tcp_width + view_w || iw <= 0) continue;

            int c_idx = g_item_color_idx[get_valid_tab_idx()][i] & 7;
            static const uint8_t pal_bg[8][3] = {
                { 36, 32, 28 }, { 64, 42, 24 }, { 24, 54, 48 }, { 62, 28, 34 },
                { 30, 38, 64 }, { 58, 50, 24 }, { 52, 30, 58 }, { 26, 50, 62 }
            };
            static const uint8_t pal_hdr[8][3] = {
                { 48, 38, 30 }, { 92, 58, 30 }, { 32, 78, 68 }, { 88, 36, 46 },
                { 42, 54, 92 }, { 84, 72, 32 }, { 74, 42, 84 }, { 36, 72, 88 }
            };
            uint8_t br = pal_bg[c_idx][0] + (it->is_selected ? 14 : 0);
            uint8_t bg_c = pal_bg[c_idx][1] + (it->is_selected ? 10 : 0);
            uint8_t bb = pal_bg[c_idx][2] + (it->is_selected ? 8 : 0);
            SDL_Rect clip_r = { ix, iy, iw, ih };
            SDL_SetRenderDrawColor(ren, br, bg_c, bb, 255);
            SDL_RenderFillRect(ren, &clip_r);

            int hdr_h = (ih > 18) ? 18 : ih;
            SDL_Rect hdr_r = { ix, iy, iw, hdr_h };
            SDL_SetRenderDrawColor(ren,
                pal_hdr[c_idx][0] + (it->is_selected ? 18 : 0),
                pal_hdr[c_idx][1] + (it->is_selected ? 12 : 0),
                pal_hdr[c_idx][2] + (it->is_selected ? 8 : 0), 255);
            SDL_RenderFillRect(ren, &hdr_r);
        }

        // Pass 1B: Render Waveforms (attenuated by manual fade + crossfade envelope), Manual Fade Curves & Draggable Fade Handles, Brown Borders & Clipped Labels
        for (int i = 0; i < p->num_items; i++) {
            AudioItem *it = &p->items[i];
            if (is_track_hidden(it->track_idx)) continue;
            int iy = get_track_y_pos(it->track_idx) + 2;
            int ih = p->track_heights[it->track_idx] - 4;
            int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
            int iw = (int)(it->duration * p->pixels_per_second);

            if (ix + iw < g_daw.tcp_width || ix > g_daw.tcp_width + view_w || iw <= 0) continue;

            int cur_tab_w = get_valid_tab_idx();
            float gain_multiplier = g_item_muted[cur_tab_w][i]
                ? 0.15f
                : db_to_linear(p->track_db[it->track_idx] + g_item_vol_db[cur_tab_w][i]);
            int mid_y = iy + ih / 2;

            SDL_SetRenderDrawColor(ren, 96, 66, 42, 255);
            SDL_RenderDrawLine(ren, ix, mid_y, ix + iw, mid_y);

            if (it->peaks && it->peak_count > 0) {
                for (int col = 0; col < iw; col++) {
                    int px = ix + col;
                    if (px < g_daw.tcp_width || px > g_daw.tcp_width + view_w) continue;

                    double rel_sec = (double)col / p->pixels_per_second;
                    double timeline_sec = it->start_time + rel_sec;
                    double cur_item_sec = it->offset_in_file + rel_sec;
                    int bin_idx = (int)((cur_item_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN);

                    if (bin_idx >= 0 && bin_idx < it->peak_count) {
                        float xfade_env = compute_item_crossfade_gain(p, i, timeline_sec);
                        float env_gain = gain_multiplier * xfade_env;
                        float mn = it->peaks[bin_idx].min_val * env_gain;
                        float mx_v = it->peaks[bin_idx].max_val * env_gain;

                        int y1 = mid_y - (int)(mx_v * (ih / 2.0f - 4));
                        int y2 = mid_y - (int)(mn * (ih / 2.0f - 4));
                        if (y1 < iy + 2) y1 = iy + 2;
                        if (y2 > iy + ih - 2) y2 = iy + ih - 2;
                        if (y1 == y2) y2 += 1;

                        if (xfade_env < 0.999f) {
                            SDL_SetRenderDrawColor(ren, 165, 205, 225, 255);
                        } else {
                            SDL_SetRenderDrawColor(ren, 196, 202, 210, 255);
                        }
                        SDL_RenderDrawLine(ren, px, y1, px, y2);
                    }
                }
            }

            // Render manual Clip Fade-In and Fade-Out curves & shaded upper exterior regions
            double fin_sec  = get_item_fade_in(p, i);
            double fout_sec = get_item_fade_out(p, i);
            int fin_px  = (int)(fin_sec  * p->pixels_per_second);
            int fout_px = (int)(fout_sec * p->pixels_per_second);
            if (fin_px > iw) fin_px = iw;
            if (fout_px > iw) fout_px = iw;

            int top_pad = iy + 2;
            int bot_pad = iy + ih - 2;
            int curve_h = bot_pad - top_pad;
            if (curve_h < 6) curve_h = 6;

            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            if (fin_px > 1) {
                int prev_x = ix;
                int prev_y = bot_pad;
                for (int px = 0; px <= fin_px; px++) {
                    int cur_x = ix + px;
                    float u = (float)px / (float)fin_px;
                    float g_in = sinf(u * 1.57079632679f);
                    int y_in = bot_pad - (int)(g_in * curve_h);
                    if (cur_x >= g_daw.tcp_width && cur_x <= g_daw.tcp_width + view_w && y_in > iy + 1) {
                        SDL_SetRenderDrawColor(ren, 10, 12, 16, 130);
                        SDL_RenderDrawLine(ren, cur_x, iy + 1, cur_x, y_in);
                    }
                    if (px > 0) {
                        SDL_SetRenderDrawColor(ren, 255, 165, 50, 245);
                        SDL_RenderDrawLine(ren, prev_x, prev_y, cur_x, y_in);
                        SDL_RenderDrawLine(ren, prev_x, prev_y + 1, cur_x, y_in + 1);
                    }
                    prev_x = cur_x;
                    prev_y = y_in;
                }
            }

            if (fout_px > 1) {
                int start_ox = ix + iw - fout_px;
                int prev_x = start_ox;
                int prev_y = top_pad;
                for (int px = 0; px <= fout_px; px++) {
                    int cur_x = start_ox + px;
                    float u = 1.0f - ((float)px / (float)fout_px);
                    float g_out = sinf(u * 1.57079632679f);
                    int y_out = bot_pad - (int)(g_out * curve_h);
                    if (cur_x >= g_daw.tcp_width && cur_x <= g_daw.tcp_width + view_w && y_out > iy + 1) {
                        SDL_SetRenderDrawColor(ren, 10, 12, 16, 130);
                        SDL_RenderDrawLine(ren, cur_x, iy + 1, cur_x, y_out);
                    }
                    if (px > 0) {
                        SDL_SetRenderDrawColor(ren, 255, 165, 50, 245);
                        SDL_RenderDrawLine(ren, prev_x, prev_y, cur_x, y_out);
                        SDL_RenderDrawLine(ren, prev_x, prev_y + 1, cur_x, y_out + 1);
                    }
                    prev_x = cur_x;
                    prev_y = y_out;
                }
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            // Optional Dynamic Split Gate Threshold overlay line in media items
            if (g_dyn_split.is_open && g_dyn_split.display_gate_threshold && it->is_selected) {
                float thr_lin = db_to_linear(g_dyn_split.gate_threshold_db);
                int dy_thr = (int)(thr_lin * (ih / 2.0f - 4));
                SDL_SetRenderDrawColor(ren, 80, 235, 120, 220);
                SDL_RenderDrawLine(ren, ix, mid_y - dy_thr, ix + iw, mid_y - dy_thr);
                SDL_RenderDrawLine(ren, ix, mid_y + dy_thr, ix + iw, mid_y + dy_thr);
            }

            // Brown clip border (thicker when clip is selected)
            SDL_Rect clip_r = { ix, iy, iw, ih };
            draw_brown_border_rect(ren, &clip_r, it->is_selected, false);

            // Draggable Top-Left Fade-In Handle & Top-Right Fade-Out Handle on every clip
            if (iw >= 14) {
                int fin_hx = ix + fin_px;
                if (fin_hx > ix + iw - 7) fin_hx = ix + iw - 7;
                SDL_Rect h_fin = { fin_hx, iy + 1, 7, 7 };
                SDL_SetRenderDrawColor(ren, (dragging_item_idx == i && dragging_fade_in) ? 255 : 235,
                                            (dragging_item_idx == i && dragging_fade_in) ? 215 : 150,
                                            45, 255);
                SDL_RenderFillRect(ren, &h_fin);

                int fout_hx = ix + iw - fout_px - 7;
                if (fout_hx < ix) fout_hx = ix;
                SDL_Rect h_fout = { fout_hx, iy + 1, 7, 7 };
                SDL_SetRenderDrawColor(ren, (dragging_item_idx == i && dragging_fade_out) ? 255 : 235,
                                            (dragging_item_idx == i && dragging_fade_out) ? 215 : 150,
                                            45, 255);
                SDL_RenderFillRect(ren, &h_fout);
            }

            // Clip name + [MUTE] / [G] / [+dB] badges strictly clipped to clip length
            if (iw > 16) {
                char full_clip_lbl[200];
                char prefix_tags[64] = "";
                if (g_item_muted[cur_tab_w][i]) strcat(prefix_tags, "[MUTE] ");
                if (g_item_group_id[cur_tab_w][i] > 0) {
                    char gbuf[24];
                    snprintf(gbuf, sizeof(gbuf), "[G%d] ", g_item_group_id[cur_tab_w][i]);
                    strcat(prefix_tags, gbuf);
                }
                if (fabsf(g_item_vol_db[cur_tab_w][i]) >= 0.05f) {
                    char vbuf[24];
                    snprintf(vbuf, sizeof(vbuf), "[%+.1fdB] ", g_item_vol_db[cur_tab_w][i]);
                    strcat(prefix_tags, vbuf);
                }
                snprintf(full_clip_lbl, sizeof(full_clip_lbl), "%s%s", prefix_tags, it->label);
                SDL_Rect clip_text_bounds = { ix + 8, iy + 1, iw - 16, ih - 2 };
                draw_text_clipped(ren, full_clip_lbl, ix + 10, iy + 4, c_white, &clip_text_bounds, &timeline_clip);
            }

            // Live Fade Duration Tooltip when dragging Fade-In or Fade-Out handle
            if (dragging_item_idx == i && (dragging_fade_in || dragging_fade_out)) {
                char fade_tip[48];
                if (dragging_fade_in) {
                    snprintf(fade_tip, sizeof(fade_tip), "Fade In: %.2fs", fin_sec);
                } else {
                    snprintf(fade_tip, sizeof(fade_tip), "Fade Out: %.2fs", fout_sec);
                }
                SDL_Rect tip_box = { ix + 8, iy + 20, 116, 20 };
                SDL_SetRenderDrawColor(ren, 20, 18, 16, 240);
                SDL_RenderFillRect(ren, &tip_box);
                draw_brown_border_rect(ren, &tip_box, true, false);
                draw_text(ren, fade_tip, tip_box.x + 6, tip_box.y + 4, c_white);
            }
        }

        // Pass 2: Standard Audio Editing Crossfade Box & Equal-Power X-Curves on any Overlapping Clips
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < p->num_items; i++) {
            AudioItem *it = &p->items[i];
            if (is_track_hidden(it->track_idx)) continue;
            double it_s = it->start_time;
            double it_e = it->start_time + it->duration;

            for (int o = 0; o < p->num_items; o++) {
                if (o == i) continue;
                AudioItem *ot = &p->items[o];
                if (ot->track_idx != it->track_idx) continue;

                double ot_s = ot->start_time;
                double ot_e = ot->start_time + ot->duration;

                bool ot_is_earlier = (ot_s < it_s - 1e-6) || (fabs(ot_s - it_s) <= 1e-6 && o < i);
                if (!ot_is_earlier) continue;

                double overlap_s = it_s;
                double overlap_e = fmin(it_e, ot_e);
                if (overlap_e <= overlap_s + 0.001) continue;

                int iy = get_track_y_pos(it->track_idx) + 2;
                int ih = p->track_heights[it->track_idx] - 4;
                int ox = g_daw.tcp_width + (int)((overlap_s - p->scroll_x_sec) * p->pixels_per_second);
                int ox_end = g_daw.tcp_width + (int)((overlap_e - p->scroll_x_sec) * p->pixels_per_second);
                int ow = ox_end - ox;
                if (ow < 2) ow = 2;

                if (ox + ow < g_daw.tcp_width || ox > g_daw.tcp_width + view_w) continue;

                SDL_Rect xf_box = { ox, iy, ow, ih };
                SDL_SetRenderDrawColor(ren, 185, 120, 55, 32);
                SDL_RenderFillRect(ren, &xf_box);

                int top_pad = iy + 3;
                int bot_pad = iy + ih - 4;
                int curve_h = bot_pad - top_pad;
                if (curve_h < 8) curve_h = 8;

                int prev_x = ox;
                int prev_y_out = top_pad;
                int prev_y_in  = bot_pad;

                for (int px = 0; px < ow; px++) {
                    int cur_x = ox + px;
                    float u = (ow > 1) ? ((float)px / (float)(ow - 1)) : 0.5f;

                    float gain_out = cosf(u * 1.57079632679f);
                    float gain_in  = sinf(u * 1.57079632679f);

                    int y_out = bot_pad - (int)(gain_out * curve_h);
                    int y_in  = bot_pad - (int)(gain_in  * curve_h);

                    int y_top_env = (y_out < y_in) ? y_out : y_in;
                    if (cur_x >= g_daw.tcp_width && cur_x <= g_daw.tcp_width + view_w && y_top_env > iy + 1) {
                        SDL_SetRenderDrawColor(ren, 12, 14, 18, 115);
                        SDL_RenderDrawLine(ren, cur_x, iy + 1, cur_x, y_top_env);
                    }

                    if (px > 0) {
                        SDL_SetRenderDrawColor(ren, 245, 115, 75, 245);
                        SDL_RenderDrawLine(ren, prev_x, prev_y_out, cur_x, y_out);
                        SDL_RenderDrawLine(ren, prev_x, prev_y_out + 1, cur_x, y_out + 1);

                        SDL_SetRenderDrawColor(ren, 85, 215, 240, 245);
                        SDL_RenderDrawLine(ren, prev_x, prev_y_in, cur_x, y_in);
                        SDL_RenderDrawLine(ren, prev_x, prev_y_in + 1, cur_x, y_in + 1);
                    }

                    prev_x = cur_x;
                    prev_y_out = y_out;
                    prev_y_in  = y_in;
                }

                SDL_SetRenderDrawColor(ren, 205, 138, 72, 235);
                SDL_RenderDrawRect(ren, &xf_box);

                int cross_x = ox + ow / 2;
                int cross_y = bot_pad - (int)(0.70710678f * curve_h);
                SDL_Rect cross_handle = { cross_x - 3, cross_y - 3, 6, 6 };
                SDL_SetRenderDrawColor(ren, 245, 210, 165, 255);
                SDL_RenderFillRect(ren, &cross_handle);

                SDL_Rect h_tl = { ox, iy, 5, 5 };
                SDL_Rect h_tr = { ox + ow - 5, iy, 5, 5 };
                SDL_Rect h_bl = { ox, iy + ih - 5, 5, 5 };
                SDL_Rect h_br = { ox + ow - 5, iy + ih - 5, 5, 5 };
                SDL_SetRenderDrawColor(ren, 205, 138, 72, 255);
                SDL_RenderFillRect(ren, &h_tl);
                SDL_RenderFillRect(ren, &h_tr);
                SDL_RenderFillRect(ren, &h_bl);
                SDL_RenderFillRect(ren, &h_br);
            }
        }

        // Logic Pro Style Yellow Cycle Range Strip & Range Highlight in Timeline
        if (range_on) {
            int rx1 = g_daw.tcp_width + (int)((get_range_start() - p->scroll_x_sec) * p->pixels_per_second);
            int rx2 = g_daw.tcp_width + (int)((get_range_end()   - p->scroll_x_sec) * p->pixels_per_second);
            int rw = rx2 - rx1;
            if (rw < 4) rw = 4;

            // Subtle translucent yellow wash over the cycle range across tracks
            SDL_Rect range_wash = { rx1, header_top_y, rw, view_h };
            SDL_SetRenderDrawColor(ren, 240, 200, 30, 20);
            SDL_RenderFillRect(ren, &range_wash);

            // Solid Logic Pro Yellow Strip along the top of the timeline
            SDL_Rect yellow_strip = { rx1, header_top_y, rw, 8 };
            SDL_SetRenderDrawColor(ren, 242, 202, 28, 245);
            SDL_RenderFillRect(ren, &yellow_strip);

            // Bright draggable left & right edge handles on the yellow strip + vertical locator lines
            SDL_Rect handle_l = { rx1, header_top_y, 5, 10 };
            SDL_Rect handle_r = { rx1 + rw - 5, header_top_y, 5, 10 };
            SDL_SetRenderDrawColor(ren, 255, 235, 95, 255);
            SDL_RenderFillRect(ren, &handle_l);
            SDL_RenderFillRect(ren, &handle_r);

            SDL_SetRenderDrawColor(ren, 242, 202, 28, 175);
            SDL_RenderDrawLine(ren, rx1, header_top_y, rx1, header_top_y + view_h);
            SDL_RenderDrawLine(ren, rx1 + rw - 1, header_top_y, rx1 + rw - 1, header_top_y + view_h);
        }
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

        if (marquee_selecting) {
            int mx1 = fmin(marquee_start_x, marquee_cur_x);
            int my1 = fmin(marquee_start_y, marquee_cur_y);
            int mw = abs(marquee_cur_x - marquee_start_x);
            int mh = abs(marquee_cur_y - marquee_start_y);
            SDL_Rect mr = { mx1, my1, mw, mh };
            draw_brown_border_rect(ren, &mr, false, false);
        }

        int cur_px = g_daw.tcp_width + (int)((p->playhead_seconds - p->scroll_x_sec) * p->pixels_per_second);
        if (cur_px >= g_daw.tcp_width && cur_px <= g_daw.tcp_width + view_w) {
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            SDL_RenderDrawLine(ren, cur_px, corner_y, cur_px, header_top_y + view_h);
        }
        SDL_RenderSetClipRect(ren, NULL);

        // 6. Track Header (TCP) with Identical Simple Faded Brown Edge & Thicker Selected Brown Edge
        SDL_Rect tcp_clip = { 0, header_top_y, g_daw.tcp_width, view_h };
        SDL_RenderSetClipRect(ren, &tcp_clip);
        for (int i = 0; i < p->num_tracks; i++) {
            if (is_track_hidden(i)) continue;
            int ty = get_track_y_pos(i);
            int th = p->track_heights[i];
            bool is_sel = p->track_selected[i];
            bool is_dragging_this_trk = (dragging_track_order_idx == i);

            SDL_Rect tp = { 1, ty + 1, g_daw.tcp_width - 2, th - 2 };
            SDL_SetRenderDrawColor(ren,
                is_dragging_this_trk ? 48 : (is_sel ? 38 : 26),
                is_dragging_this_trk ? 36 : (is_sel ? 30 : 26),
                is_dragging_this_trk ? 26 : (is_sel ? 24 : 26),
                255);
            SDL_RenderFillRect(ren, &tp);

            // Every track has a simple identical faded brown edge; selected/dragged track edge is slightly thicker
            if (is_dragging_this_trk || is_sel) {
                draw_brown_border_rect(ren, &tp, true, false);
            } else {
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 118, 78, 46, 145);
                SDL_RenderDrawRect(ren, &tp);
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            }

            // Top Part: Track Name Box with clean brown border
            SDL_Rect name_box = { 8, ty + 6, g_daw.tcp_width - 38, 22 };
            bool is_editing = (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME && g_daw.editing_track_name_idx == i);

            SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
            SDL_RenderFillRect(ren, &name_box);
            draw_brown_border_rect(ren, &name_box, is_editing, false);

            SDL_Rect name_txt_bounds = { name_box.x + 2, name_box.y + 1, name_box.w - 4, name_box.h - 2 };
            if (is_editing) {
                draw_text_clipped(ren, g_daw.editing_name_buf, 12, ty + 10, c_white, &name_txt_bounds, &tcp_clip);
                int text_w = (int)strlen(g_daw.editing_name_buf) * 8;
                if ((SDL_GetTicks() / 500) % 2 == 0) {
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_RenderDrawLine(ren, 12 + text_w, ty + 8, 12 + text_w, ty + 24);
                }
            } else {
                draw_text_clipped(ren, p->track_names[i], 12, ty + 10, c_white, &name_txt_bounds, &tcp_clip);
            }

            // Right-side Up/Down Drag Grip Handle
            int grip_x = g_daw.tcp_width - 24;
            SDL_Rect grip_box = { grip_x, ty + 6, 18, 48 };
            SDL_SetRenderDrawColor(ren, is_dragging_this_trk ? 56 : 36, is_dragging_this_trk ? 42 : 32, is_dragging_this_trk ? 28 : 28, 255);
            SDL_RenderFillRect(ren, &grip_box);
            draw_brown_border_rect(ren, &grip_box, is_dragging_this_trk, false);
            // Draw grip bars (=)
            SDL_SetRenderDrawColor(ren, 195, 140, 88, 255);
            for (int gy = ty + 22; gy <= ty + 36; gy += 4) {
                SDL_RenderDrawLine(ren, grip_x + 4, gy, grip_x + 13, gy);
            }

            // Bottom Part: Controls
            int bot_y = ty + 34;

            // DB Controls
            SDL_Rect btn_minus = { 8, bot_y, 18, 20 };
            SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
            SDL_RenderFillRect(ren, &btn_minus);
            draw_brown_border_rect(ren, &btn_minus, false, false);
            draw_text(ren, "-", 14, bot_y + 4, c_white);

            char db_txt[16];
            snprintf(db_txt, sizeof(db_txt), "%+.1f", p->track_db[i]);
            draw_text(ren, db_txt, 30, bot_y + 5, c_white);

            SDL_Rect btn_plus = { 76, bot_y, 18, 20 };
            SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
            SDL_RenderFillRect(ren, &btn_plus);
            draw_brown_border_rect(ren, &btn_plus, false, false);
            draw_text(ren, "+", 81, bot_y + 4, c_white);

            // M & S Buttons
            SDL_Rect m_btn = { 110, bot_y, 22, 20 };
            SDL_SetRenderDrawColor(ren, p->tracks[i].mute ? 180 : 40, p->tracks[i].mute ? 40 : 40, 40, 255);
            SDL_RenderFillRect(ren, &m_btn);
            draw_brown_border_rect(ren, &m_btn, p->tracks[i].mute, false);
            draw_text(ren, "M", 117, bot_y + 4, c_white);

            SDL_Rect s_btn = { 138, bot_y, 22, 20 };
            SDL_SetRenderDrawColor(ren, p->tracks[i].solo ? 180 : 40, p->tracks[i].solo ? 140 : 40, 40, 255);
            SDL_RenderFillRect(ren, &s_btn);
            draw_brown_border_rect(ren, &s_btn, p->tracks[i].solo, false);
            draw_text(ren, "S", 145, bot_y + 4, c_white);
        }
        SDL_RenderSetClipRect(ren, NULL);

        // Brown vertical border line between TCP and Timeline
        SDL_SetRenderDrawColor(ren, 152, 98, 54, 255);
        SDL_RenderDrawLine(ren, g_daw.tcp_width - 1, TABBAR_HEIGHT + TOPBAR_HEIGHT, g_daw.tcp_width - 1, win_h);

        // Scrollbars (with Brown Borders)
        SDL_Rect bar_h = { g_daw.tcp_width, win_h - SCROLLBAR_SIZE, view_w, SCROLLBAR_SIZE };
        SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
        SDL_RenderFillRect(ren, &bar_h);
        draw_brown_border_rect(ren, &bar_h, false, true);

        float thumb_w_sz = (total_w_timeline > 0) ? fmaxf(30.0f, (float)view_w * ((float)view_w / total_w_timeline)) : view_w;
        double max_scroll_sec = (total_w_timeline > view_w) ? (double)(total_w_timeline - view_w) / p->pixels_per_second : 0.0001;
        float thumb_x = g_daw.tcp_width + (max_scroll_sec > 0 ? (float)(p->scroll_x_sec / max_scroll_sec) * (view_w - thumb_w_sz) : 0);
        SDL_Rect thumb_h = { (int)thumb_x, win_h - SCROLLBAR_SIZE + 2, (int)thumb_w_sz, SCROLLBAR_SIZE - 4 };
        SDL_SetRenderDrawColor(ren, 62, 48, 36, 255);
        SDL_RenderFillRect(ren, &thumb_h);
        draw_brown_border_rect(ren, &thumb_h, false, false);

        SDL_Rect bar_v = { win_w - SCROLLBAR_SIZE, header_top_y, SCROLLBAR_SIZE, view_h };
        SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
        SDL_RenderFillRect(ren, &bar_v);
        draw_brown_border_rect(ren, &bar_v, false, true);

        float thumb_h_sz = (max_h_tracks > 0) ? fmaxf(30.0f, (float)view_h * ((float)view_h / max_h_tracks)) : view_h;
        float thumb_y = header_top_y + (max_h_tracks > view_h ? ((float)p->scroll_y_px / (max_h_tracks - view_h)) * (view_h - thumb_h_sz) : 0);
        SDL_Rect thumb_v = { win_w - SCROLLBAR_SIZE + 2, (int)thumb_y, SCROLLBAR_SIZE - 4, (int)thumb_h_sz };
        SDL_SetRenderDrawColor(ren, 62, 48, 36, 255);
        SDL_RenderFillRect(ren, &thumb_v);
        draw_brown_border_rect(ren, &thumb_v, false, false);

        // Marker Right-Click Context Menu ("Delete Marker") with Brown Border
        if (g_marker_context_menu_open && g_marker_context_target_idx >= 0 && g_marker_context_target_idx < p->num_markers) {
            SDL_Rect cm_box = { g_marker_context_x, g_marker_context_y, 128, 28 };
            SDL_SetRenderDrawColor(ren, 34, 28, 24, 255);
            SDL_RenderFillRect(ren, &cm_box);
            draw_brown_border_rect(ren, &cm_box, true, false);
            draw_text(ren, "Delete Marker", g_marker_context_x + 12, g_marker_context_y + 8, c_white);
        }

        // 7. Action Menu (Submenus: Keyboard Shortcut, Scripts, Dynamic split, Reposition, Join (Glue))
        if (g_daw.menu_action_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            SDL_Rect a_box = { 60, m_start, 178, 135 };
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, &a_box);
            draw_brown_border_rect(ren, &a_box, true, false);
            draw_text(ren, "Keyboard Shortcut", 68, m_start + 7, c_white);
            draw_text(ren, "Scripts", 68, m_start + 34, c_white);
            draw_text(ren, "Dynamic split", 68, m_start + 61, c_white);
            draw_text(ren, "Reposition", 68, m_start + 88, c_white);
            draw_text(ren, "Join (Glue)", 68, m_start + 115, c_white);
        }

        // Unified Script Manager (Lua & Python Scripts, Edge-Draggable Resize, Vertical Scrollbar, Refresh Script & Run Script)
        if (g_daw.script_manager_open) {
            SDL_Rect *sr = &g_daw.script_win_rect;
            if (sr->w < 360) sr->w = 360;
            if (sr->h < 220) sr->h = 220;

            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_Rect sc_shadow = { sr->x + 6, sr->y + 6, sr->w, sr->h };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 165);
            SDL_RenderFillRect(ren, &sc_shadow);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, sr);
            draw_brown_border_rect(ren, sr, true, false);

            SDL_Rect s_hdr = { sr->x, sr->y, sr->w, 28 };
            uint8_t shr, shg, shb;
            get_theme_surface_rgb(32, 0.22f, &shr, &shg, &shb);
            SDL_SetRenderDrawColor(ren, shr, shg, shb, 255);
            SDL_RenderFillRect(ren, &s_hdr);
            draw_brown_border_rect(ren, &s_hdr, false, false);

            draw_text(ren, "Script Manager (Lua & Python)", sr->x + 12, sr->y + 8, c_white);
            draw_text(ren, "X", sr->x + sr->w - 20, sr->y + 8, c_white);

            refresh_filtered_scripts();
            SDL_Color c_accent = get_theme_accent_sdl_color();

            // Top Filter Search Box + [Clear] + [Pin] + [Up^] + [Dn v]
            int top_y = sr->y + 34;
            int flt_w = sr->w - 310;
            if (flt_w < 110) flt_w = 110;
            int flt_x = sr->x + 56;
            int clr_x = flt_x + flt_w + 6;
            int pin_x = clr_x + 58;
            int up_x  = pin_x + 64;
            int dn_x  = up_x + 42;

            draw_text(ren, "Filter", sr->x + 12, top_y + 6, c_white);
            SDL_Rect s_flt_box = { flt_x, top_y, flt_w, 24 };
            SDL_SetRenderDrawColor(ren, 15, 14, 13, 255);
            SDL_RenderFillRect(ren, &s_flt_box);
            draw_brown_border_rect(ren, &s_flt_box, g_script_filter_focused, false);

            SDL_Rect s_flt_clip = { s_flt_box.x + 4, s_flt_box.y + 2, s_flt_box.w - 8, s_flt_box.h - 4 };
            draw_text_clipped(ren, g_script_filter_text, s_flt_box.x + 6, top_y + 5, c_white, &s_flt_clip, NULL);
            if (g_script_filter_focused && ((SDL_GetTicks() / 450) % 2 == 0)) {
                int cx = s_flt_box.x + 6 + (int)strlen(g_script_filter_text) * 8;
                if (cx < s_flt_box.x + s_flt_box.w - 4) {
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_RenderDrawLine(ren, cx, top_y + 4, cx, top_y + 20);
                }
            }

            SDL_Rect b_s_clr = { clr_x, top_y, 52, 24 };
            SDL_SetRenderDrawColor(ren, 44, 38, 34, 255);
            SDL_RenderFillRect(ren, &b_s_clr);
            draw_brown_border_rect(ren, &b_s_clr, false, false);
            draw_text(ren, "Clear", clr_x + 6, top_y + 5, c_white);

            bool sel_sc_pinned = (g_daw.script_selected_idx >= 0 && g_daw.script_selected_idx < g_daw.num_scripts &&
                                  g_script_pinned[g_daw.script_selected_idx]);
            SDL_Rect b_s_pin = { pin_x, top_y, 58, 24 };
            SDL_SetRenderDrawColor(ren, sel_sc_pinned ? 74 : 44, sel_sc_pinned ? 52 : 38, sel_sc_pinned ? 30 : 34, 255);
            SDL_RenderFillRect(ren, &b_s_pin);
            draw_brown_border_rect(ren, &b_s_pin, sel_sc_pinned, false);
            draw_pin_symbol_icon(ren, pin_x + 5, top_y + 5, sel_sc_pinned, sel_sc_pinned ? c_accent : c_white);
            draw_text(ren, "Pin", pin_x + 22, top_y + 5, c_white);

            SDL_Rect b_s_up = { up_x, top_y, 38, 24 };
            SDL_SetRenderDrawColor(ren, 44, 38, 34, 255);
            SDL_RenderFillRect(ren, &b_s_up);
            draw_brown_border_rect(ren, &b_s_up, false, false);
            draw_text(ren, "Up^", up_x + 6, top_y + 5, c_white);

            SDL_Rect b_s_dn = { dn_x, top_y, 40, 24 };
            SDL_SetRenderDrawColor(ren, 44, 38, 34, 255);
            SDL_RenderFillRect(ren, &b_s_dn);
            draw_brown_border_rect(ren, &b_s_dn, false, false);
            draw_text(ren, "Dn v", dn_x + 4, top_y + 5, c_white);

            int list_y = sr->y + 66;
            int list_h = sr->h - 112;
            SDL_Rect list_box = { sr->x + 12, list_y, sr->w - 24, list_h };
            SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
            SDL_RenderFillRect(ren, &list_box);
            draw_brown_border_rect(ren, &list_box, false, false);

            int content_h = g_script_num_filtered * 24 + 8;
            int max_sc = (content_h > list_h) ? (content_h - list_h) : 0;
            if (g_script_scroll_y > max_sc) g_script_scroll_y = max_sc;
            if (g_script_scroll_y < 0) g_script_scroll_y = 0;

            SDL_Rect sc_clip = { list_box.x + 2, list_box.y + 2, list_box.w - 18, list_box.h - 4 };
            SDL_RenderSetClipRect(ren, &sc_clip);
            for (int r = 0; r < g_script_num_filtered; r++) {
                int s = g_script_filtered_indices[r];
                int sy = list_box.y + 4 + (r * 24) - g_script_scroll_y;
                if (sy + 22 < list_box.y || sy > list_box.y + list_box.h) continue;

                bool is_sel = (s == g_daw.script_selected_idx);
                SDL_Rect s_row = { list_box.x + 4, sy, list_box.w - 22, 22 };
                if (is_sel) {
                    SDL_SetRenderDrawColor(ren, shr + 14, shg + 10, shb + 8, 255);
                    SDL_RenderFillRect(ren, &s_row);
                    draw_brown_border_rect(ren, &s_row, false, false);
                } else if ((r & 1) == 1) {
                    SDL_SetRenderDrawColor(ren, 21, 20, 19, 255);
                    SDL_RenderFillRect(ren, &s_row);
                }

                int text_start_x = s_row.x + 6;
                if (g_script_pinned[s]) {
                    draw_pin_symbol_icon(ren, s_row.x + 5, sy + 4, true, c_accent);
                    text_start_x += 14;
                }

                const char *type_tag = g_daw.scripts[s].is_python ? "[PY] " : "[LUA]";
                draw_text(ren, type_tag, text_start_x, sy + 4, is_sel ? c_white : c_accent);

                int sc_col_w = g_daw.scripts[s].shortcut[0] ? 90 : 8;
                SDL_Rect name_clip = { text_start_x + 46, sy + 1, s_row.w - (text_start_x - s_row.x) - 50 - sc_col_w, 20 };
                draw_text_clipped(ren, g_daw.scripts[s].name, text_start_x + 46, sy + 4,
                                  g_script_pinned[s] ? c_accent : c_white, &name_clip, &sc_clip);

                if (g_daw.scripts[s].shortcut[0] != '\0') {
                    SDL_Rect key_clip = { s_row.x + s_row.w - 86, sy + 1, 80, 20 };
                    draw_text_clipped(ren, g_daw.scripts[s].shortcut, s_row.x + s_row.w - 84, sy + 4, c_accent, &key_clip, &sc_clip);
                }
            }
            SDL_RenderSetClipRect(ren, NULL);

            // Vertical Scrollbar for Script Manager
            SDL_Rect sb_track = { list_box.x + list_box.w - 14, list_box.y + 2, 12, list_box.h - 4 };
            SDL_SetRenderDrawColor(ren, 24, 22, 20, 255);
            SDL_RenderFillRect(ren, &sb_track);
            draw_brown_border_rect(ren, &sb_track, false, true);

            int thumb_h = (content_h > list_h && content_h > 0)
                          ? fmax(24, (sb_track.h * list_h) / content_h)
                          : sb_track.h;
            int thumb_y = sb_track.y + ((max_sc > 0) ? ((g_script_scroll_y * (sb_track.h - thumb_h)) / max_sc) : 0);
            SDL_Rect sb_thumb = { sb_track.x + 1, thumb_y, sb_track.w - 2, thumb_h };
            uint8_t sbr, sbg, sbb;
            get_theme_surface_rgb(42, 0.45f, &sbr, &sbg, &sbb);
            SDL_SetRenderDrawColor(ren, sbr, sbg, sbb, 255);
            SDL_RenderFillRect(ren, &sb_thumb);
            draw_brown_border_rect(ren, &sb_thumb, false, false);

            // Bottom Buttons: [Refresh Script] and [Run Script] (Delete removed)
            int by = sr->y + sr->h - 38;
            SDL_Rect btn_ref_sc = { sr->x + 12, by, 130, 28 };
            SDL_SetRenderDrawColor(ren, 44, 40, 36, 255);
            SDL_RenderFillRect(ren, &btn_ref_sc);
            draw_brown_border_rect(ren, &btn_ref_sc, false, false);
            draw_text(ren, "Refresh Script", sr->x + 21, by + 8, c_white);

            SDL_Rect btn_run_sc = { sr->x + 152, by, 114, 28 };
            SDL_SetRenderDrawColor(ren, shr + 16, shg + 10, shb + 6, 255);
            SDL_RenderFillRect(ren, &btn_run_sc);
            draw_brown_border_rect(ren, &btn_run_sc, true, false);
            draw_text(ren, "Run Script", sr->x + 169, by + 8, c_white);

            if (g_script_status_msg[0] != '\0' && sr->w > 300) {
                SDL_Rect st_clip = { sr->x + 276, by + 2, sr->w - 288, 24 };
                draw_text_clipped(ren, g_script_status_msg, sr->x + 276, by + 8, c_muted, &st_clip, NULL);
            }
        }

        // Tools Menu (Submenus: Marker, History, Themes, Record -> Rec | Browse [with folder symbol])
        if (g_menu_tools_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            SDL_Rect t_box = { 126, m_start, 150, 108 };
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, &t_box);
            draw_brown_border_rect(ren, &t_box, true, false);

            draw_text(ren, p->show_marker_lane ? "Marker [ON]" : "Marker", 136, m_start + 7, c_white);
            draw_text(ren, "History", 136, m_start + 34, c_white);
            draw_text(ren, "Themes", 136, m_start + 61, c_white);
            draw_text(ren, "Record        >", 136, m_start + 88, g_rec.is_recording ? get_theme_accent_sdl_color() : c_white);

            if (g_submenu_tools_record_open) {
                SDL_Rect r_sub = { 276, m_start + 80, 146, 56 };
                SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
                SDL_RenderFillRect(ren, &r_sub);
                draw_brown_border_rect(ren, &r_sub, true, false);

                // Item 1: Rec (with red dot indicator)
                draw_filled_circle_sdl(ren, 290, m_start + 94, 5,
                                       g_rec.is_recording ? 245 : 195,
                                       g_rec.is_recording ? 32  : 42,
                                       g_rec.is_recording ? 32  : 42, 255);
                draw_text(ren, g_rec.is_recording ? "Rec [STOP]" : "Rec", 304, m_start + 88, c_white);

                // Item 2: Browse (with folder symbol)
                SDL_Color c_folder = get_theme_accent_sdl_color();
                draw_folder_symbol_icon(ren, 283, m_start + 114, c_folder);
                draw_text(ren, "Browse", 304, m_start + 115, c_white);
            }
        }

        // File Menu (Import Project, Export Project, Project Save As, Import Media, Export Media)
        if (g_daw.menu_file_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            SDL_Rect m_box = { 8, m_start, 196, 126 };
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, &m_box);
            draw_brown_border_rect(ren, &m_box, true, false);

            draw_text(ren, "Import Project (.ehs)", 16, m_start + 6, c_white);
            draw_text(ren, "Export Project (.ehs)", 16, m_start + 30, c_white);
            draw_text(ren, "Project Save As", 16, m_start + 54, c_white);
            draw_text(ren, "Import Media", 16, m_start + 78, c_white);
            draw_text(ren, "Export Media", 16, m_start + 102, c_white);
        }

        // Floating / Fullscreen Video Monitor (Aspect-Ratio Preserving & Fullscreen/Minimize Button near X)
        if (p->video.is_open) {
            int v_tab = get_valid_tab_idx();
            if (g_video_fullscreen[v_tab]) {
                p->video.rect.x = 0;
                p->video.rect.y = 0;
                p->video.rect.w = win_w;
                p->video.rect.h = win_h;
            } else {
                if (p->video.rect.w < 240) p->video.rect.w = 480;
                if (p->video.rect.h < 159) p->video.rect.h = 294;
                SDL_Rect s_rect = { p->video.rect.x + 4, p->video.rect.y + 4, p->video.rect.w, p->video.rect.h };
                SDL_SetRenderDrawColor(ren, 0, 0, 0, 140);
                SDL_RenderFillRect(ren, &s_rect);
            }

            SDL_Rect v_bar = { p->video.rect.x, p->video.rect.y, p->video.rect.w, 24 };
            SDL_SetRenderDrawColor(ren, 36, 36, 36, 255);
            SDL_RenderFillRect(ren, &v_bar);
            draw_brown_border_rect(ren, &v_bar, false, false);

            draw_text(ren, "Video Monitor", p->video.rect.x + 8, p->video.rect.y + 7, c_white);

            // Full Screen / Minimize (Restore) Symbol Button right near X
            int fs_btn_x = p->video.rect.x + p->video.rect.w - 46;
            int fs_btn_y = p->video.rect.y + 3;
            SDL_Rect fs_btn = { fs_btn_x, fs_btn_y, 18, 18 };
            SDL_SetRenderDrawColor(ren, 48, 40, 34, 255);
            SDL_RenderFillRect(ren, &fs_btn);
            draw_brown_border_rect(ren, &fs_btn, false, false);

            SDL_SetRenderDrawColor(ren, 224, 224, 224, 255);
            if (!g_video_fullscreen[v_tab]) {
                // Full Screen / Expand icon (square window box with top header line)
                SDL_Rect ico_max = { fs_btn_x + 4, fs_btn_y + 4, 10, 10 };
                SDL_RenderDrawRect(ren, &ico_max);
                SDL_RenderDrawLine(ren, fs_btn_x + 4, fs_btn_y + 5, fs_btn_x + 13, fs_btn_y + 5);
            } else {
                // Minimize / Restore Screen icon (two overlapping window squares)
                SDL_Rect ico_back = { fs_btn_x + 6, fs_btn_y + 3, 8, 8 };
                SDL_Rect ico_front = { fs_btn_x + 3, fs_btn_y + 6, 8, 8 };
                SDL_RenderDrawRect(ren, &ico_back);
                SDL_SetRenderDrawColor(ren, 48, 40, 34, 255);
                SDL_RenderFillRect(ren, &ico_front);
                SDL_SetRenderDrawColor(ren, 224, 224, 224, 255);
                SDL_RenderDrawRect(ren, &ico_front);
            }

            // Close [X] Button
            draw_text(ren, "X", p->video.rect.x + p->video.rect.w - 18, p->video.rect.y + 7, c_white);

            SDL_Rect v_canvas = { p->video.rect.x, p->video.rect.y + 24, p->video.rect.w, p->video.rect.h - 24 };

            // Background is always solid black (letterbox / pillarbox bars)
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderFillRect(ren, &v_canvas);

            if (p->video.has_active_frame && p->video.texture) {
                double v_asp = (p->video.width > 0 && p->video.height > 0)
                               ? ((double)p->video.width / (double)p->video.height)
                               : (16.0 / 9.0);
                double c_asp = (v_canvas.h > 0) ? ((double)v_canvas.w / (double)v_canvas.h) : v_asp;
                SDL_Rect dst_rect = v_canvas;
                if (c_asp > v_asp) {
                    dst_rect.h = v_canvas.h;
                    dst_rect.w = (int)((double)v_canvas.h * v_asp);
                    dst_rect.x = v_canvas.x + (v_canvas.w - dst_rect.w) / 2;
                    dst_rect.y = v_canvas.y;
                } else {
                    dst_rect.w = v_canvas.w;
                    dst_rect.h = (int)((double)v_canvas.w / v_asp);
                    dst_rect.x = v_canvas.x;
                    dst_rect.y = v_canvas.y + (v_canvas.h - dst_rect.h) / 2;
                }
                SDL_RenderCopy(ren, p->video.texture, NULL, &dst_rect);
            } else if (!p->video.texture) {
                draw_text(ren, "No Video Stream Loaded",
                          v_canvas.x + (v_canvas.w - 176) / 2,
                          v_canvas.y + v_canvas.h / 2 - 4, c_muted);
            }
            draw_brown_border_rect(ren, &v_canvas, false, false);


        }

        // ================= UNIFIED EXPORT MEDIA POPUP WINDOW =================
        if (g_export.is_open) {
            SDL_Rect *er = &g_export.win_rect;
            int ex = er->x, ey = er->y, ew = er->w, eh = er->h;

            // Subtle drop shadow
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_Rect shadow = { ex + 6, ey + 6, ew, eh };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 160);
            SDL_RenderFillRect(ren, &shadow);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            // Window Body
            SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
            SDL_RenderFillRect(ren, er);
            draw_brown_border_rect(ren, er, true, false);

            // Title Bar
            SDL_Rect hdr = { ex, ey, ew, 30 };
            SDL_SetRenderDrawColor(ren, 42, 34, 28, 255);
            SDL_RenderFillRect(ren, &hdr);
            draw_brown_border_rect(ren, &hdr, false, false);
            draw_text(ren, "Export Media", ex + 14, ey + 9, c_white);
            draw_text(ren, "X", ex + ew - 20, ey + 9, c_white);

            // Time Bounds Indicator in Header
            char bounds_txt[96];
            if (is_range_active()) {
                snprintf(bounds_txt, sizeof(bounds_txt), "Bounds: Cycle Range (%.2fs - %.2fs)",
                         get_range_start(), get_range_end());
            } else {
                snprintf(bounds_txt, sizeof(bounds_txt), "Bounds: Entire Project");
            }
            draw_text(ren, bounds_txt, ex + 180, ey + 9, c_muted);

            // ---- 1. SOURCE SECTION ----
            int sy_src = ey + 48;
            draw_text(ren, "Source:", ex + 18, sy_src + 7, c_white);

            SDL_Rect b_src_master = { ex + 110, sy_src, 135, 26 };
            SDL_SetRenderDrawColor(ren, g_export.source == EXPORT_SRC_MASTER ? 68 : 38,
                                        g_export.source == EXPORT_SRC_MASTER ? 48 : 36,
                                        g_export.source == EXPORT_SRC_MASTER ? 32 : 34, 255);
            SDL_RenderFillRect(ren, &b_src_master);
            draw_brown_border_rect(ren, &b_src_master, g_export.source == EXPORT_SRC_MASTER, false);
            draw_text(ren, "Master mix", ex + 132, sy_src + 7, c_white);

            SDL_Rect b_src_stems = { ex + 255, sy_src, 210, 26 };
            SDL_SetRenderDrawColor(ren, g_export.source == EXPORT_SRC_STEMS ? 68 : 38,
                                        g_export.source == EXPORT_SRC_STEMS ? 48 : 36,
                                        g_export.source == EXPORT_SRC_STEMS ? 32 : 34, 255);
            SDL_RenderFillRect(ren, &b_src_stems);
            draw_brown_border_rect(ren, &b_src_stems, g_export.source == EXPORT_SRC_STEMS, false);
            draw_text(ren, "Selected tracks (stems)", ex + 266, sy_src + 7, c_white);

            SDL_Rect b_src_items = { ex + 475, sy_src, 187, 26 };
            SDL_SetRenderDrawColor(ren, g_export.source == EXPORT_SRC_ITEMS ? 68 : 38,
                                        g_export.source == EXPORT_SRC_ITEMS ? 48 : 36,
                                        g_export.source == EXPORT_SRC_ITEMS ? 32 : 34, 255);
            SDL_RenderFillRect(ren, &b_src_items);
            draw_brown_border_rect(ren, &b_src_items, g_export.source == EXPORT_SRC_ITEMS, false);
            draw_text(ren, "Selected media items", ex + 486, sy_src + 7, c_white);

            // ---- 2. OUTPUT SECTION (Directory + Browse, File Name) ----
            int sy_dir = ey + 98;
            draw_text(ren, "Directory:", ex + 18, sy_dir + 7, c_white);

            SDL_Rect box_dir = { ex + 130, sy_dir, 415, 26 };
            bool ed_dir = (g_export.active_field == EXP_FIELD_DIR);
            SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
            SDL_RenderFillRect(ren, &box_dir);
            draw_brown_border_rect(ren, &box_dir, ed_dir, false);
            SDL_Rect box_dir_clip = { box_dir.x + 4, box_dir.y + 2, box_dir.w - 8, box_dir.h - 4 };
            draw_text_clipped(ren, g_export.directory, box_dir.x + 6, sy_dir + 7, c_white, &box_dir_clip, NULL);
            if (ed_dir && ((SDL_GetTicks() / 450) % 2 == 0)) {
                int cx = box_dir.x + 6 + (int)strlen(g_export.directory) * 8;
                if (cx < box_dir.x + box_dir.w - 4) {
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_RenderDrawLine(ren, cx, sy_dir + 5, cx, sy_dir + 21);
                }
            }

            SDL_Rect btn_browse = { ex + 555, sy_dir, 107, 26 };
            SDL_SetRenderDrawColor(ren, 52, 40, 30, 255);
            SDL_RenderFillRect(ren, &btn_browse);
            draw_brown_border_rect(ren, &btn_browse, false, false);
            draw_text(ren, "Browse...", ex + 572, sy_dir + 7, c_white);

            int sy_fn = ey + 132;
            draw_text(ren, "File Name:", ex + 18, sy_fn + 7, c_white);

            SDL_Rect box_fn = { ex + 130, sy_fn, 415, 26 };
            bool ed_fn = (g_export.active_field == EXP_FIELD_FILENAME);
            SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
            SDL_RenderFillRect(ren, &box_fn);
            draw_brown_border_rect(ren, &box_fn, ed_fn, false);
            SDL_Rect box_fn_clip = { box_fn.x + 4, box_fn.y + 2, box_fn.w - 8, box_fn.h - 4 };
            draw_text_clipped(ren, g_export.filename, box_fn.x + 6, sy_fn + 7, c_white, &box_fn_clip, NULL);
            if (ed_fn && ((SDL_GetTicks() / 450) % 2 == 0)) {
                int cx = box_fn.x + 6 + (int)strlen(g_export.filename) * 8;
                if (cx < box_fn.x + box_fn.w - 4) {
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_RenderDrawLine(ren, cx, sy_fn + 5, cx, sy_fn + 21);
                }
            }
            char ext_preview[16];
            snprintf(ext_preview, sizeof(ext_preview), ".%s", get_export_ext());
            draw_text(ren, ext_preview, ex + 555, sy_fn + 7, c_muted);

            // ---- 3. SAMPLE RATE & CHANNELS SECTION ----
            int sy_sr = ey + 182;
            draw_text(ren, "Sample Rate:", ex + 18, sy_sr + 7, c_white);

            SDL_Rect b_sr_48 = { ex + 130, sy_sr, 105, 26 };
            SDL_SetRenderDrawColor(ren, g_export.sample_rate == 48000 ? 68 : 38,
                                        g_export.sample_rate == 48000 ? 48 : 36,
                                        g_export.sample_rate == 48000 ? 32 : 34, 255);
            SDL_RenderFillRect(ren, &b_sr_48);
            draw_brown_border_rect(ren, &b_sr_48, g_export.sample_rate == 48000, false);
            draw_text(ren, "48,000 Hz", ex + 146, sy_sr + 7, c_white);

            SDL_Rect b_sr_44 = { ex + 243, sy_sr, 105, 26 };
            SDL_SetRenderDrawColor(ren, g_export.sample_rate == 44100 ? 68 : 38,
                                        g_export.sample_rate == 44100 ? 48 : 36,
                                        g_export.sample_rate == 44100 ? 32 : 34, 255);
            SDL_RenderFillRect(ren, &b_sr_44);
            draw_brown_border_rect(ren, &b_sr_44, g_export.sample_rate == 44100, false);
            draw_text(ren, "44,100 Hz", ex + 259, sy_sr + 7, c_white);

            draw_text(ren, "Channels:", ex + 372, sy_sr + 7, c_white);

            SDL_Rect b_ch_mono = { ex + 455, sy_sr, 90, 26 };
            SDL_SetRenderDrawColor(ren, g_export.channels == 1 ? 68 : 38,
                                        g_export.channels == 1 ? 48 : 36,
                                        g_export.channels == 1 ? 32 : 34, 255);
            SDL_RenderFillRect(ren, &b_ch_mono);
            draw_brown_border_rect(ren, &b_ch_mono, g_export.channels == 1, false);
            draw_text(ren, "Mono", ex + 482, sy_sr + 7, c_white);

            SDL_Rect b_ch_stereo = { ex + 553, sy_sr, 102, 26 };
            SDL_SetRenderDrawColor(ren, g_export.channels == 2 ? 68 : 38,
                                        g_export.channels == 2 ? 48 : 36,
                                        g_export.channels == 2 ? 32 : 34, 255);
            SDL_RenderFillRect(ren, &b_ch_stereo);
            draw_brown_border_rect(ren, &b_ch_stereo, g_export.channels == 2, false);
            draw_text(ren, "Stereo", ex + 578, sy_sr + 7, c_white);

            // ---- 4. FORMAT SECTION ----
            int sy_fmt = ey + 232;
            draw_text(ren, "Format:", ex + 18, sy_fmt + 7, c_white);

            SDL_Rect b_fmt_wav = { ex + 130, sy_fmt, 130, 28 };
            SDL_SetRenderDrawColor(ren, g_export.format == EXPORT_FMT_WAV ? 68 : 38,
                                        g_export.format == EXPORT_FMT_WAV ? 48 : 36,
                                        g_export.format == EXPORT_FMT_WAV ? 32 : 34, 255);
            SDL_RenderFillRect(ren, &b_fmt_wav);
            draw_brown_border_rect(ren, &b_fmt_wav, g_export.format == EXPORT_FMT_WAV, false);
            draw_text(ren, "WAV (PCM)", ex + 158, sy_fmt + 8, c_white);

            SDL_Rect b_fmt_mp3 = { ex + 270, sy_fmt, 130, 28 };
            SDL_SetRenderDrawColor(ren, g_export.format == EXPORT_FMT_MP3 ? 68 : 38,
                                        g_export.format == EXPORT_FMT_MP3 ? 48 : 36,
                                        g_export.format == EXPORT_FMT_MP3 ? 32 : 34, 255);
            SDL_RenderFillRect(ren, &b_fmt_mp3);
            draw_brown_border_rect(ren, &b_fmt_mp3, g_export.format == EXPORT_FMT_MP3, false);
            draw_text(ren, "MP3 Audio", ex + 298, sy_fmt + 8, c_white);

            SDL_Rect b_fmt_mp4 = { ex + 410, sy_fmt, 245, 28 };
            SDL_SetRenderDrawColor(ren, g_export.format == EXPORT_FMT_MP4 ? 68 : 38,
                                        g_export.format == EXPORT_FMT_MP4 ? 48 : 36,
                                        g_export.format == EXPORT_FMT_MP4 ? 32 : 34, 255);
            SDL_RenderFillRect(ren, &b_fmt_mp4);
            draw_brown_border_rect(ren, &b_fmt_mp4, g_export.format == EXPORT_FMT_MP4, false);
            draw_text(ren, "MP4 (MPEG-4 Video)", ex + 455, sy_fmt + 8, c_white);

            // Format Details Sub-Panel (ey + 272 .. ey + 422)
            SDL_Rect sub_panel = { ex + 18, ey + 272, ew - 36, 152 };
            SDL_SetRenderDrawColor(ren, 20, 19, 18, 255);
            SDL_RenderFillRect(ren, &sub_panel);
            draw_brown_border_rect(ren, &sub_panel, false, true);

            int sy_sub = ey + 278;
            if (g_export.format == EXPORT_FMT_WAV) {
                draw_text(ren, "WAV Bit Depth:", ex + 34, sy_sub + 19, c_white);

                SDL_Rect b_w16 = { ex + 150, sy_sub + 12, 130, 28 };
                SDL_SetRenderDrawColor(ren, g_export.wav_bit_depth == 16 ? 68 : 38,
                                            g_export.wav_bit_depth == 16 ? 48 : 36,
                                            g_export.wav_bit_depth == 16 ? 32 : 34, 255);
                SDL_RenderFillRect(ren, &b_w16);
                draw_brown_border_rect(ren, &b_w16, g_export.wav_bit_depth == 16, false);
                draw_text(ren, "16 bit PCM", ex + 174, sy_sub + 20, c_white);

                SDL_Rect b_w24 = { ex + 292, sy_sub + 12, 130, 28 };
                SDL_SetRenderDrawColor(ren, g_export.wav_bit_depth == 24 ? 68 : 38,
                                            g_export.wav_bit_depth == 24 ? 48 : 36,
                                            g_export.wav_bit_depth == 24 ? 32 : 34, 255);
                SDL_RenderFillRect(ren, &b_w24);
                draw_brown_border_rect(ren, &b_w24, g_export.wav_bit_depth == 24, false);
                draw_text(ren, "24 bit PCM", ex + 316, sy_sub + 20, c_white);

                SDL_Rect b_w32 = { ex + 434, sy_sub + 12, 130, 28 };
                SDL_SetRenderDrawColor(ren, g_export.wav_bit_depth == 32 ? 68 : 38,
                                            g_export.wav_bit_depth == 32 ? 48 : 36,
                                            g_export.wav_bit_depth == 32 ? 32 : 34, 255);
                SDL_RenderFillRect(ren, &b_w32);
                draw_brown_border_rect(ren, &b_w32, g_export.wav_bit_depth == 32, false);
                draw_text(ren, "32 bit PCM", ex + 458, sy_sub + 20, c_white);

                draw_text(ren, "Uncompressed studio-quality linear PCM WAVE format.", ex + 34, sy_sub + 62, c_muted);
            } else if (g_export.format == EXPORT_FMT_MP3) {
                draw_text(ren, "MP3 Bitrate:", ex + 34, sy_sub + 19, c_white);

                SDL_Rect b_m128 = { ex + 150, sy_sub + 12, 130, 28 };
                SDL_SetRenderDrawColor(ren, g_export.mp3_bitrate == 128 ? 68 : 38,
                                            g_export.mp3_bitrate == 128 ? 48 : 36,
                                            g_export.mp3_bitrate == 128 ? 32 : 34, 255);
                SDL_RenderFillRect(ren, &b_m128);
                draw_brown_border_rect(ren, &b_m128, g_export.mp3_bitrate == 128, false);
                draw_text(ren, "128 kbps", ex + 182, sy_sub + 20, c_white);

                SDL_Rect b_m192 = { ex + 292, sy_sub + 12, 130, 28 };
                SDL_SetRenderDrawColor(ren, g_export.mp3_bitrate == 192 ? 68 : 38,
                                            g_export.mp3_bitrate == 192 ? 48 : 36,
                                            g_export.mp3_bitrate == 192 ? 32 : 34, 255);
                SDL_RenderFillRect(ren, &b_m192);
                draw_brown_border_rect(ren, &b_m192, g_export.mp3_bitrate == 192, false);
                draw_text(ren, "192 kbps", ex + 324, sy_sub + 20, c_white);

                SDL_Rect b_m320 = { ex + 434, sy_sub + 12, 130, 28 };
                SDL_SetRenderDrawColor(ren, g_export.mp3_bitrate == 320 ? 68 : 38,
                                            g_export.mp3_bitrate == 320 ? 48 : 36,
                                            g_export.mp3_bitrate == 320 ? 32 : 34, 255);
                SDL_RenderFillRect(ren, &b_m320);
                draw_brown_border_rect(ren, &b_m320, g_export.mp3_bitrate == 320, false);
                draw_text(ren, "320 kbps", ex + 466, sy_sub + 20, c_white);

                draw_text(ren, "MPEG-1/2 Audio Layer III constant bitrate encoding.", ex + 34, sy_sub + 62, c_muted);
            } else if (g_export.format == EXPORT_FMT_MP4) {
                // Row 1 (matching image_736e17.png):
                // File: [ MPEG-4 ]   Size: [ 1920 ] x [ 1080 ]   Framerate: [ 25.00 ]
                int r1_y = sy_sub + 8;
                draw_text(ren, "File:", ex + 34, r1_y + 7, c_white);
                SDL_Rect box_mpeg4 = { ex + 92, r1_y, 136, 26 };
                SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
                SDL_RenderFillRect(ren, &box_mpeg4);
                draw_brown_border_rect(ren, &box_mpeg4, false, false);
                draw_text(ren, "MPEG-4", ex + 104, r1_y + 7, c_white);
                draw_text(ren, "v", ex + 210, r1_y + 7, c_muted);

                draw_text(ren, "Size:", ex + 244, r1_y + 7, c_white);
                SDL_Rect box_w = { ex + 295, r1_y, 70, 26 };
                bool ed_w = (g_export.active_field == EXP_FIELD_MP4_WIDTH);
                SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
                SDL_RenderFillRect(ren, &box_w);
                draw_brown_border_rect(ren, &box_w, ed_w, false);
                draw_text(ren, g_export.mp4_width_str, box_w.x + 8, r1_y + 7, c_white);
                if (ed_w && ((SDL_GetTicks() / 450) % 2 == 0)) {
                    int cx = box_w.x + 8 + (int)strlen(g_export.mp4_width_str) * 8;
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_RenderDrawLine(ren, cx, r1_y + 5, cx, r1_y + 21);
                }

                draw_text(ren, "x", ex + 373, r1_y + 7, c_white);

                SDL_Rect box_h = { ex + 390, r1_y, 70, 26 };
                bool ed_h = (g_export.active_field == EXP_FIELD_MP4_HEIGHT);
                SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
                SDL_RenderFillRect(ren, &box_h);
                draw_brown_border_rect(ren, &box_h, ed_h, false);
                draw_text(ren, g_export.mp4_height_str, box_h.x + 8, r1_y + 7, c_white);
                if (ed_h && ((SDL_GetTicks() / 450) % 2 == 0)) {
                    int cx = box_h.x + 8 + (int)strlen(g_export.mp4_height_str) * 8;
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_RenderDrawLine(ren, cx, r1_y + 5, cx, r1_y + 21);
                }

                draw_text(ren, "Framerate:", ex + 476, r1_y + 7, c_white);
                SDL_Rect box_fps = { ex + 568, r1_y, 80, 26 };
                bool ed_fps = (g_export.active_field == EXP_FIELD_MP4_FPS);
                SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
                SDL_RenderFillRect(ren, &box_fps);
                draw_brown_border_rect(ren, &box_fps, ed_fps, false);
                draw_text(ren, g_export.mp4_fps_str, box_fps.x + 8, r1_y + 7, c_white);
                if (ed_fps && ((SDL_GetTicks() / 450) % 2 == 0)) {
                    int cx = box_fps.x + 8 + (int)strlen(g_export.mp4_fps_str) * 8;
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_RenderDrawLine(ren, cx, r1_y + 5, cx, r1_y + 21);
                }

                // Row 2 (matching image_736e58.png):
                // Video: [ H.264 ]   [ 2048 ] kbps
                int r2_y = sy_sub + 46;
                draw_text(ren, "Video:", ex + 34, r2_y + 7, c_white);
                SDL_Rect box_vcodec = { ex + 92, r2_y, 176, 26 };
                SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
                SDL_RenderFillRect(ren, &box_vcodec);
                draw_brown_border_rect(ren, &box_vcodec, false, false);
                draw_text(ren, "H.264", ex + 104, r2_y + 7, c_white);
                draw_text(ren, "v", ex + 250, r2_y + 7, c_muted);

                SDL_Rect box_vkbps = { ex + 295, r2_y, 100, 26 };
                bool ed_vk = (g_export.active_field == EXP_FIELD_MP4_V_KBPS);
                SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
                SDL_RenderFillRect(ren, &box_vkbps);
                draw_brown_border_rect(ren, &box_vkbps, ed_vk, false);
                draw_text(ren, g_export.mp4_video_kbps_str, box_vkbps.x + 8, r2_y + 7, c_white);
                if (ed_vk && ((SDL_GetTicks() / 450) % 2 == 0)) {
                    int cx = box_vkbps.x + 8 + (int)strlen(g_export.mp4_video_kbps_str) * 8;
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_RenderDrawLine(ren, cx, r2_y + 5, cx, r2_y + 21);
                }
                draw_text(ren, "kbps", ex + 405, r2_y + 7, c_white);

                // Row 3: Audio: [ AAC ]   [ 120 / 192 / 320 kbps  v ] dropdown menu
                int r3_y = sy_sub + 84;
                draw_text(ren, "Audio:", ex + 34, r3_y + 7, c_white);

                SDL_Rect box_ac_aac = { ex + 92, r3_y, 176, 26 };
                SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
                SDL_RenderFillRect(ren, &box_ac_aac);
                draw_brown_border_rect(ren, &box_ac_aac, true, false);
                draw_text(ren, "AAC", ex + 104, r3_y + 7, c_white);

                SDL_Rect box_akbps = { ex + 295, r3_y, 130, 26 };
                SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
                SDL_RenderFillRect(ren, &box_akbps);
                draw_brown_border_rect(ren, &box_akbps, g_export.mp4_aac_dropdown_open, false);
                char aac_rate_lbl[32];
                snprintf(aac_rate_lbl, sizeof(aac_rate_lbl), "%s kbps", g_export.mp4_audio_kbps_str);
                draw_text(ren, aac_rate_lbl, box_akbps.x + 10, r3_y + 7, c_white);
                draw_text(ren, "v", box_akbps.x + box_akbps.w - 18, r3_y + 7, c_muted);
            }

            // ---- 5. OPTION: TWO-PASS RENDER CHECKBOX ----
            int sy_opt = ey + 436;
            SDL_Rect chk_box = { ex + 22, sy_opt + 2, 20, 20 };
            SDL_SetRenderDrawColor(ren, g_export.two_pass_render ? 72 : 20,
                                        g_export.two_pass_render ? 50 : 20,
                                        g_export.two_pass_render ? 30 : 20, 255);
            SDL_RenderFillRect(ren, &chk_box);
            draw_brown_border_rect(ren, &chk_box, g_export.two_pass_render, false);
            if (g_export.two_pass_render) {
                draw_text(ren, "x", ex + 28, sy_opt + 5, c_white);
            }
            draw_text(ren, "Option: Two-pass render", ex + 50, sy_opt + 6, c_white);

            // Render MP4 AAC Bitrate Dropdown Menu on top if open (120 kbps, 192 kbps, 320 kbps)
            if (g_export.format == EXPORT_FMT_MP4 && g_export.mp4_aac_dropdown_open) {
                int dd_x = ex + 295;
                int dd_y = sy_sub + 84 + 26;
                int dd_w = 130;
                SDL_Rect dd_box = { dd_x, dd_y, dd_w, 78 };
                SDL_SetRenderDrawColor(ren, 24, 22, 20, 255);
                SDL_RenderFillRect(ren, &dd_box);
                draw_brown_border_rect(ren, &dd_box, true, false);

                const char *rates[3] = { "120", "192", "320" };
                const char *rate_labels[3] = { "120 kbps", "192 kbps", "320 kbps" };
                for (int r_i = 0; r_i < 3; r_i++) {
                    bool is_cur = (strcmp(g_export.mp4_audio_kbps_str, rates[r_i]) == 0);
                    SDL_Rect r_item = { dd_x + 2, dd_y + r_i * 26 + 2, dd_w - 4, 22 };
                    SDL_SetRenderDrawColor(ren, is_cur ? 68 : 30, is_cur ? 48 : 28, is_cur ? 32 : 26, 255);
                    SDL_RenderFillRect(ren, &r_item);
                    draw_brown_border_rect(ren, &r_item, is_cur, !is_cur);
                    draw_text(ren, rate_labels[r_i], dd_x + 12, dd_y + r_i * 26 + 7, c_white);
                }
            }

            // ---- 6. PROCESSING PROGRESS BAR, PERCENTAGE (%) & PROCESSING TIME ----
            SDL_Rect bar_bg = { ex + 22, ey + 468, ew - 44, 24 };
            SDL_SetRenderDrawColor(ren, 16, 15, 14, 255);
            SDL_RenderFillRect(ren, &bar_bg);

            int fill_w = ((bar_bg.w - 4) * g_export.progress_pct) / 100;
            if (fill_w > 0) {
                SDL_Rect bar_fill = { bar_bg.x + 2, bar_bg.y + 2, fill_w, bar_bg.h - 4 };
                SDL_SetRenderDrawColor(ren, 195, 128, 54, 255);
                SDL_RenderFillRect(ren, &bar_fill);
            }
            draw_brown_border_rect(ren, &bar_bg, g_export.progress_pct > 0, false);

            char pct_time_buf[160];
            if (g_export.is_rendering) {
                snprintf(pct_time_buf, sizeof(pct_time_buf),
                         "Processing: %d%%   |   Time: %.2fs   |   Remaining: ~%.2fs",
                         g_export.progress_pct, g_export.elapsed_sec, g_export.est_remaining_sec);
            } else if (g_export.progress_pct >= 100) {
                snprintf(pct_time_buf, sizeof(pct_time_buf),
                         "Completed: 100%%   |   Processing Time: %.2fs",
                         g_export.elapsed_sec);
            } else {
                snprintf(pct_time_buf, sizeof(pct_time_buf),
                         "Progress: %d%%   |   Processing Time: %.2fs",
                         g_export.progress_pct, g_export.elapsed_sec);
            }
            draw_text(ren, pct_time_buf, bar_bg.x + 12, bar_bg.y + 6, c_white);

            if (g_export.status_msg[0] != 0) {
                SDL_Rect st_clip = { ex + 22, ey + 498, ew - 44, 22 };
                draw_text_clipped(ren, g_export.status_msg, ex + 22, ey + 500, c_muted, &st_clip, NULL);
            }

            // ---- 7. BOTTOM ACTION BUTTONS ----
            int btn_y = ey + eh - 44;
            SDL_Rect btn_render = { ex + ew - 270, btn_y, 145, 32 };
            SDL_SetRenderDrawColor(ren, 78, 52, 32, 255);
            SDL_RenderFillRect(ren, &btn_render);
            draw_brown_border_rect(ren, &btn_render, true, false);
            draw_text(ren, "Render / Export", ex + ew - 258, btn_y + 10, c_white);

            SDL_Rect btn_close = { ex + ew - 115, btn_y, 95, 32 };
            SDL_SetRenderDrawColor(ren, 44, 40, 38, 255);
            SDL_RenderFillRect(ren, &btn_close);
            draw_brown_border_rect(ren, &btn_close, false, false);
            draw_text(ren, "Cancel", ex + ew - 92, btn_y + 10, c_white);
        }

        // ================= HIDE TRACKS POPUP WINDOW (WITH SCROLLING) =================
        if (g_hide_popup_open) {
            SDL_Rect *hr = &g_hide_win_rect;
            int hx = hr->x, hy = hr->y, hw = hr->w, hh = hr->h;

            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_Rect h_shadow = { hx + 5, hy + 5, hw, hh };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 155);
            SDL_RenderFillRect(ren, &h_shadow);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
            SDL_RenderFillRect(ren, hr);
            draw_brown_border_rect(ren, hr, true, false);

            SDL_Rect h_hdr = { hx, hy, hw, 28 };
            SDL_SetRenderDrawColor(ren, 42, 34, 28, 255);
            SDL_RenderFillRect(ren, &h_hdr);
            draw_brown_border_rect(ren, &h_hdr, false, false);
            draw_text(ren, "Hide Tracks", hx + 12, hy + 8, c_white);
            draw_text(ren, "X", hx + hw - 18, hy + 8, c_white);

            draw_text(ren, "Click checkbox to hide track (scroll wheel / bar):", hx + 14, hy + 36, c_muted);

            SDL_Rect list_box = { hx + 14, hy + 58, hw - 28, hh - 106 };
            SDL_SetRenderDrawColor(ren, 18, 17, 16, 255);
            SDL_RenderFillRect(ren, &list_box);
            draw_brown_border_rect(ren, &list_box, false, true);

            int content_h = p->num_tracks * 26 + 8;
            int max_sc = (content_h > list_box.h) ? (content_h - list_box.h) : 0;
            if (g_hide_scroll_y > max_sc) g_hide_scroll_y = max_sc;
            if (g_hide_scroll_y < 0) g_hide_scroll_y = 0;

            SDL_Rect items_clip = { list_box.x + 2, list_box.y + 2, list_box.w - 18, list_box.h - 4 };
            SDL_RenderSetClipRect(ren, &items_clip);
            for (int t = 0; t < p->num_tracks; t++) {
                int ry = list_box.y + 4 + t * 26 - g_hide_scroll_y;
                if (ry + 24 < list_box.y || ry > list_box.y + list_box.h) continue;

                bool hid = is_track_hidden(t);
                SDL_Rect row_r = { list_box.x + 4, ry, list_box.w - 22, 24 };
                SDL_SetRenderDrawColor(ren, hid ? 48 : 28, hid ? 34 : 26, hid ? 24 : 24, 255);
                SDL_RenderFillRect(ren, &row_r);
                draw_brown_border_rect(ren, &row_r, hid, !hid);

                SDL_Rect chk = { row_r.x + 6, ry + 3, 18, 18 };
                SDL_SetRenderDrawColor(ren, hid ? 92 : 16, hid ? 58 : 16, hid ? 28 : 16, 255);
                SDL_RenderFillRect(ren, &chk);
                draw_brown_border_rect(ren, &chk, hid, false);
                if (hid) {
                    draw_text(ren, "x", chk.x + 5, chk.y + 3, c_white);
                }

                char trk_lbl[96];
                snprintf(trk_lbl, sizeof(trk_lbl), "%d. %s", t + 1, p->track_names[t]);
                SDL_Rect lbl_clip = { row_r.x + 30, ry + 2, row_r.w - 104, 20 };
                draw_text_clipped(ren, trk_lbl, row_r.x + 32, ry + 5, c_white, &lbl_clip, &items_clip);

                draw_text(ren, hid ? "Hidden" : "Visible", row_r.x + row_r.w - 64, ry + 5, hid ? c_white : c_muted);
            }
            SDL_RenderSetClipRect(ren, NULL);

            // Vertical Scrollbar inside Hide Tracks list
            SDL_Rect sb_track = { list_box.x + list_box.w - 14, list_box.y + 2, 12, list_box.h - 4 };
            SDL_SetRenderDrawColor(ren, 24, 22, 20, 255);
            SDL_RenderFillRect(ren, &sb_track);
            draw_brown_border_rect(ren, &sb_track, false, true);

            int thumb_h = (content_h > list_box.h && content_h > 0)
                          ? fmax(24, (sb_track.h * list_box.h) / content_h)
                          : sb_track.h;
            int thumb_y = sb_track.y + ((max_sc > 0) ? ((g_hide_scroll_y * (sb_track.h - thumb_h)) / max_sc) : 0);
            SDL_Rect sb_thumb = { sb_track.x + 1, thumb_y, sb_track.w - 2, thumb_h };
            SDL_SetRenderDrawColor(ren, 92, 64, 42, 255);
            SDL_RenderFillRect(ren, &sb_thumb);
            draw_brown_border_rect(ren, &sb_thumb, false, false);

            int b_y = hy + hh - 38;
            SDL_Rect b_show_all = { hx + 14, b_y, 100, 28 };
            SDL_SetRenderDrawColor(ren, 44, 40, 36, 255);
            SDL_RenderFillRect(ren, &b_show_all);
            draw_brown_border_rect(ren, &b_show_all, false, false);
            draw_text(ren, "Show All", hx + 30, b_y + 8, c_white);

            SDL_Rect b_hide_all = { hx + 124, b_y, 100, 28 };
            SDL_SetRenderDrawColor(ren, 44, 40, 36, 255);
            SDL_RenderFillRect(ren, &b_hide_all);
            draw_brown_border_rect(ren, &b_hide_all, false, false);
            draw_text(ren, "Hide All", hx + 140, b_y + 8, c_white);

            SDL_Rect b_close = { hx + hw - 94, b_y, 80, 28 };
            SDL_SetRenderDrawColor(ren, 68, 46, 30, 255);
            SDL_RenderFillRect(ren, &b_close);
            draw_brown_border_rect(ren, &b_close, true, false);
            draw_text(ren, "Close", hx + hw - 74, b_y + 8, c_white);
        }

        // ================= PROJECT HISTORY POPUP WINDOW (100 CHANGES LIST) =================
        if (g_history_popup_open) {
            SDL_Rect *hsr = &g_history_win_rect;
            int hx = hsr->x, hy = hsr->y, hw = hsr->w, hh = hsr->h;
            int tab_i = get_valid_tab_idx();
            int h_cnt = g_tab_history_count[tab_i];

            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_Rect hs_shadow = { hx + 6, hy + 6, hw, hh };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 160);
            SDL_RenderFillRect(ren, &hs_shadow);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
            SDL_RenderFillRect(ren, hsr);
            draw_brown_border_rect(ren, hsr, true, false);

            SDL_Rect hs_hdr = { hx, hy, hw, 28 };
            SDL_SetRenderDrawColor(ren, 42, 34, 28, 255);
            SDL_RenderFillRect(ren, &hs_hdr);
            draw_brown_border_rect(ren, &hs_hdr, false, false);
            char hs_title[96];
            snprintf(hs_title, sizeof(hs_title), "Project History (%d / 100 Changes)", h_cnt);
            draw_text(ren, hs_title, hx + 12, hy + 8, c_white);
            draw_text(ren, "X", hx + hw - 18, hy + 8, c_white);

            SDL_Rect list_box = { hx + 14, hy + 38, hw - 28, hh - 86 };
            SDL_SetRenderDrawColor(ren, 18, 17, 16, 255);
            SDL_RenderFillRect(ren, &list_box);
            draw_brown_border_rect(ren, &list_box, false, true);

            int content_h = h_cnt * 24 + 8;
            int max_sc = (content_h > list_box.h) ? (content_h - list_box.h) : 0;
            if (g_history_scroll_y > max_sc) g_history_scroll_y = max_sc;
            if (g_history_scroll_y < 0) g_history_scroll_y = 0;

            SDL_Rect hs_clip = { list_box.x + 2, list_box.y + 2, list_box.w - 18, list_box.h - 4 };
            SDL_RenderSetClipRect(ren, &hs_clip);
            for (int r = 0; r < h_cnt; r++) {
                // Display newest changes first (from h_cnt - 1 down to 0)
                int src_idx = (h_cnt - 1) - r;
                int ry = list_box.y + 4 + r * 24 - g_history_scroll_y;
                if (ry + 22 < list_box.y || ry > list_box.y + list_box.h) continue;

                SDL_Rect row_r = { list_box.x + 4, ry, list_box.w - 22, 22 };
                SDL_SetRenderDrawColor(ren, (r == 0) ? 42 : 26, (r == 0) ? 32 : 24, (r == 0) ? 24 : 22, 255);
                SDL_RenderFillRect(ren, &row_r);
                draw_brown_border_rect(ren, &row_r, r == 0, r != 0);

                char row_txt[160];
                snprintf(row_txt, sizeof(row_txt), "%03d. %s", src_idx + 1, g_tab_history[tab_i][src_idx]);
                SDL_Rect r_clip = { row_r.x + 6, ry + 2, row_r.w - 12, 18 };
                draw_text_clipped(ren, row_txt, row_r.x + 8, ry + 4, (r == 0) ? c_white : c_muted, &r_clip, &hs_clip);
            }
            SDL_RenderSetClipRect(ren, NULL);

            // Vertical Scrollbar inside History list
            SDL_Rect sb_track = { list_box.x + list_box.w - 14, list_box.y + 2, 12, list_box.h - 4 };
            SDL_SetRenderDrawColor(ren, 24, 22, 20, 255);
            SDL_RenderFillRect(ren, &sb_track);
            draw_brown_border_rect(ren, &sb_track, false, true);

            int thumb_h = (content_h > list_box.h && content_h > 0)
                          ? fmax(24, (sb_track.h * list_box.h) / content_h)
                          : sb_track.h;
            int thumb_y = sb_track.y + ((max_sc > 0) ? ((g_history_scroll_y * (sb_track.h - thumb_h)) / max_sc) : 0);
            SDL_Rect sb_thumb = { sb_track.x + 1, thumb_y, sb_track.w - 2, thumb_h };
            SDL_SetRenderDrawColor(ren, 92, 64, 42, 255);
            SDL_RenderFillRect(ren, &sb_thumb);
            draw_brown_border_rect(ren, &sb_thumb, false, false);

            int b_y = hy + hh - 38;
            draw_text(ren, "Format: yy:mm:dd::hh:mm:ss:", hx + 16, b_y + 8, c_muted);

            SDL_Rect b_close = { hx + hw - 98, b_y, 84, 28 };
            SDL_SetRenderDrawColor(ren, 68, 46, 30, 255);
            SDL_RenderFillRect(ren, &b_close);
            draw_brown_border_rect(ren, &b_close, true, false);
            draw_text(ren, "Close", hx + hw - 76, b_y + 8, c_white);
        }

        // ================= INSERT MULTIPLE MEDIA ITEMS POPUP WINDOW =================
        if (g_multi_import.is_open) {
            SDL_Rect *mr = &g_multi_import.win_rect;
            int mmx = mr->x, mmy = mr->y, mmw = mr->w, mmh = mr->h;

            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_Rect m_shadow = { mmx + 6, mmy + 6, mmw, mmh };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 165);
            SDL_RenderFillRect(ren, &m_shadow);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
            SDL_RenderFillRect(ren, mr);
            draw_brown_border_rect(ren, mr, true, false);

            SDL_Rect m_hdr = { mmx, mmy, mmw, 30 };
            SDL_SetRenderDrawColor(ren, 42, 34, 28, 255);
            SDL_RenderFillRect(ren, &m_hdr);
            draw_brown_border_rect(ren, &m_hdr, false, false);
            draw_text(ren, "Insert Multiple Media Items", mmx + 14, mmy + 9, c_white);
            draw_text(ren, "X", mmx + mmw - 20, mmy + 9, c_white);

            // Option 1: Same time position on separate tracks
            bool opt1 = (g_multi_import.mode == MULTI_IMPORT_SEPARATE_TRACKS);
            SDL_Rect r_opt1 = { mmx + 20, mmy + 52, mmw - 40, 32 };
            SDL_SetRenderDrawColor(ren, opt1 ? 52 : 30, opt1 ? 38 : 28, opt1 ? 26 : 26, 255);
            SDL_RenderFillRect(ren, &r_opt1);
            draw_brown_border_rect(ren, &r_opt1, opt1, !opt1);

            SDL_Rect rad1_outer = { mmx + 32, mmy + 60, 16, 16 };
            SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
            SDL_RenderFillRect(ren, &rad1_outer);
            draw_brown_border_rect(ren, &rad1_outer, opt1, false);
            if (opt1) {
                SDL_Rect rad1_inner = { mmx + 36, mmy + 64, 8, 8 };
                SDL_SetRenderDrawColor(ren, 235, 155, 65, 255);
                SDL_RenderFillRect(ren, &rad1_inner);
            }
            draw_text(ren, "Same time position on separate tracks", mmx + 58, mmy + 62, c_white);

            // Option 2: Sequential time positions on a single track
            bool opt2 = (g_multi_import.mode == MULTI_IMPORT_SEQUENTIAL_SINGLE);
            SDL_Rect r_opt2 = { mmx + 20, mmy + 94, mmw - 40, 32 };
            SDL_SetRenderDrawColor(ren, opt2 ? 52 : 30, opt2 ? 38 : 28, opt2 ? 26 : 26, 255);
            SDL_RenderFillRect(ren, &r_opt2);
            draw_brown_border_rect(ren, &r_opt2, opt2, !opt2);

            SDL_Rect rad2_outer = { mmx + 32, mmy + 102, 16, 16 };
            SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
            SDL_RenderFillRect(ren, &rad2_outer);
            draw_brown_border_rect(ren, &rad2_outer, opt2, false);
            if (opt2) {
                SDL_Rect rad2_inner = { mmx + 36, mmy + 106, 8, 8 };
                SDL_SetRenderDrawColor(ren, 235, 155, 65, 255);
                SDL_RenderFillRect(ren, &rad2_inner);
            }
            draw_text(ren, "Sequential time positions on a single track", mmx + 58, mmy + 104, c_white);

            char cnt_lbl[64];
            snprintf(cnt_lbl, sizeof(cnt_lbl), "%d media files selected", g_multi_import.num_files);
            draw_text(ren, cnt_lbl, mmx + 22, mmy + mmh - 34, c_muted);

            int by = mmy + mmh - 44;
            SDL_Rect b_ok = { mmx + mmw - 210, by, 95, 30 };
            SDL_SetRenderDrawColor(ren, 78, 52, 32, 255);
            SDL_RenderFillRect(ren, &b_ok);
            draw_brown_border_rect(ren, &b_ok, true, false);
            draw_text(ren, "OK", mmx + mmw - 172, by + 9, c_white);

            SDL_Rect b_can = { mmx + mmw - 105, by, 87, 30 };
            SDL_SetRenderDrawColor(ren, 44, 40, 38, 255);
            SDL_RenderFillRect(ren, &b_can);
            draw_brown_border_rect(ren, &b_can, false, false);
            draw_text(ren, "Cancel", mmx + mmw - 86, by + 9, c_white);
        }

        // REAPER-Style Actions & Keyboard Shortcuts Popup Window
        if (g_action_win.is_open) {
            render_action_window(ren);
        }

        // Tools -> Themes ARGB Popup Window
        if (g_theme.is_open) {
            render_themes_popup_window(ren);
        }

        // Action -> Dynamic Split Items Popup Window (Ctrl+D)
        if (g_dyn_split.is_open) {
            render_dynamic_split_window(ren);
        }

        // Action -> Reposition Items Popup Window (W)
        if (g_reposition.is_open) {
            render_reposition_window(ren);
        }

        // Lua & Python Custom Script Popup Window
        if (g_script_popup.is_open) {
            render_script_popup_window(ren);
        }

        // 8. Save Confirmation Dialog Modal (with Brown Borders)
        if (g_daw.save_prompt_open) {
            SDL_Rect dim_overlay = { 0, 0, win_w, win_h };
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
            SDL_RenderFillRect(ren, &dim_overlay);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            int dia_w = 380, dia_h = 140;
            int dia_x = (win_w - dia_w) / 2;
            int dia_y = (win_h - dia_h) / 2;

            SDL_Rect dia_rect = { dia_x, dia_y, dia_w, dia_h };
            SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
            SDL_RenderFillRect(ren, &dia_rect);
            draw_brown_border_rect(ren, &dia_rect, true, false);

            draw_text(ren, g_save_prompt_open_project_after ? "Save changes before opening project?" : "Save changes before closing?", dia_x + 18, dia_y + 18, c_white);

            char prompt_sub[128];
            int t_idx = g_daw.save_prompt_tab_idx;
            const char *pname = (t_idx >= 0 && t_idx < g_daw.num_tabs) ? g_daw.tabs[t_idx].project_name : "Project";
            snprintf(prompt_sub, sizeof(prompt_sub), "Project \"%s\" has unsaved changes.", pname);
            draw_text(ren, prompt_sub, dia_x + 18, dia_y + 44, c_muted);

            int btn_y = dia_y + dia_h - 44;

            // [ Save ]
            SDL_Rect b_save = { dia_x + 18, btn_y, 100, 30 };
            SDL_SetRenderDrawColor(ren, 72, 48, 30, 255);
            SDL_RenderFillRect(ren, &b_save);
            draw_brown_border_rect(ren, &b_save, true, false);
            draw_text(ren, "Save", dia_x + 52, btn_y + 8, c_white);

            // [ Don't Save ]
            SDL_Rect b_discard = { dia_x + 130, btn_y, 120, 30 };
            SDL_SetRenderDrawColor(ren, 55, 55, 55, 255);
            SDL_RenderFillRect(ren, &b_discard);
            draw_brown_border_rect(ren, &b_discard, false, false);
            draw_text(ren, "Don't Save", dia_x + 148, btn_y + 8, c_white);

            // [ Cancel ]
            SDL_Rect b_cancel = { dia_x + 262, btn_y, 100, 30 };
            SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
            SDL_RenderFillRect(ren, &b_cancel);
            draw_brown_border_rect(ren, &b_cancel, false, false);
            draw_text(ren, "Cancel", dia_x + 292, btn_y + 8, c_white);
        }

        // 9. Track Delete Confirmation Modal (when deleting a track that contains clips)
        if (g_track_delete_prompt_open) {
            SDL_Rect dim_overlay = { 0, 0, win_w, win_h };
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
            SDL_RenderFillRect(ren, &dim_overlay);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            int dia_w = 390, dia_h = 140;
            int dia_x = (win_w - dia_w) / 2;
            int dia_y = (win_h - dia_h) / 2;

            SDL_Rect dia_rect = { dia_x, dia_y, dia_w, dia_h };
            SDL_SetRenderDrawColor(ren, 30, 28, 26, 255);
            SDL_RenderFillRect(ren, &dia_rect);
            draw_brown_border_rect(ren, &dia_rect, true, false);

            draw_text(ren, "Delete selected track(s)?", dia_x + 18, dia_y + 18, c_white);
            draw_text(ren, "Selected track contains clip(s). Delete anyway?", dia_x + 18, dia_y + 46, c_muted);

            int btn_y = dia_y + dia_h - 44;
            SDL_Rect b_del = { dia_x + 70, btn_y, 116, 30 };
            SDL_SetRenderDrawColor(ren, 145, 38, 34, 255);
            SDL_RenderFillRect(ren, &b_del);
            draw_brown_border_rect(ren, &b_del, true, false);
            draw_text(ren, "Delete", dia_x + 104, btn_y + 8, c_white);

            SDL_Rect b_can = { dia_x + 204, btn_y, 116, 30 };
            SDL_SetRenderDrawColor(ren, 48, 44, 40, 255);
            SDL_RenderFillRect(ren, &b_can);
            draw_brown_border_rect(ren, &b_can, false, false);
            draw_text(ren, "Cancel", dia_x + 238, btn_y + 8, c_white);
        }

        // 10. Processing / Stabilization Wait Popup Overlay
        if (g_processing_popup_open) {
            render_processing_wait_overlay(ren, win);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    save_themes_settings_json();
    if (g_daw.audio_device > 0) SDL_CloseAudioDevice(g_daw.audio_device);
    for (int t = 0; t < g_daw.num_tabs; t++) {
        close_video(&g_daw.tabs[t].video);
        for (int i = 0; i < g_daw.tabs[t].num_items; i++) {
            if (g_daw.tabs[t].items[i].raw_file_handle) fclose(g_daw.tabs[t].items[i].raw_file_handle);
            if (g_daw.tabs[t].items[i].peaks) free(g_daw.tabs[t].items[i].peaks);
        }
    }
    SDL_FreeCursor(g_cursor_arrow);
    SDL_FreeCursor(g_cursor_size_we);
    SDL_FreeCursor(g_cursor_size_ns);
    SDL_FreeCursor(g_cursor_ibeam);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
