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

// Per-tab Track Hide state & Hide Tracks Popup Window state
static bool     g_track_hidden[MAX_TABS][MAX_TRACKS];
static bool     g_hide_popup_open = false;
static SDL_Rect g_hide_win_rect = { 450, 110, 380, 340 };
static bool     g_hide_win_dragging = false;
static int      g_hide_drag_off_x = 0;
static int      g_hide_drag_off_y = 0;

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
    ExportMp4AudioCodec mp4_audio_codec; // AAC, WAV, MP3
    char mp4_audio_kbps_str[16];    // e.g. "128"

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
    "128",
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
    p->is_dirty = true;
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

    p->is_dirty = true;
}

// Helper to draw a 1px or 2px BROWN border rectangle across all UI elements
static inline void draw_brown_border_rect(SDL_Renderer *ren, const SDL_Rect *r, bool thick, bool faded) {
    if (!r || r->w <= 0 || r->h <= 0) return;
    if (thick) {
        SDL_SetRenderDrawColor(ren, 205, 138, 72, 255);
        SDL_RenderDrawRect(ren, r);
        if (r->w > 2 && r->h > 2) {
            SDL_Rect inner = { r->x + 1, r->y + 1, r->w - 2, r->h - 2 };
            SDL_RenderDrawRect(ren, &inner);
        }
    } else if (faded) {
        SDL_SetRenderDrawColor(ren, 118, 78, 46, 255);
        SDL_RenderDrawRect(ren, r);
    } else {
        SDL_SetRenderDrawColor(ren, 152, 98, 54, 255);
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
                            float gain = db_to_linear(p->track_db[it->track_idx]) * compute_item_crossfade_gain(p, i, t_sec);
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

            float linear_gain = db_to_linear(p->track_db[it->track_idx]) * norm_gain;

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
        p->num_items++;
        if (it->has_video) {
            open_video_by_path(&p->video, it->file_path, ren);
        }
        p->is_dirty = true;
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
            p->num_items++;
            if (it->has_video) {
                open_video_by_path(&p->video, it->file_path, ren);
            }
            p->is_dirty = true;
        }
    }
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
}

void audio_mixer_callback(void *userdata, Uint8 *stream, int len) {
    DawEngine *eng = (DawEngine *)userdata;
    if (eng->num_tabs <= 0) return;
    ProjectTab *p = &eng->tabs[eng->active_tab];
    float *out = (float *)stream;
    int frames_needed = len / (sizeof(float) * 2);
    memset(stream, 0, len);

    if (!p->is_playing) return;

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

        float linear_gain = db_to_linear(p->track_db[it->track_idx]);
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

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return 1;

    SDL_Window *win = SDL_CreateWindow(
        "Professional Multitrack DAW",
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
    memset(g_track_hidden, 0, sizeof(g_track_hidden));

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
    g_daw.script_win_rect = (SDL_Rect){ 420, 180, 440, 270 };

    init_tab_defaults(0, "Project 1");
    push_undo_state();
    g_daw.tabs[0].is_dirty = false;

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

        if (p->is_playing) {
            double visible_end_sec = p->scroll_x_sec + ((double)view_w / p->pixels_per_second);
            if (p->playhead_seconds >= visible_end_sec) {
                p->scroll_x_sec = p->playhead_seconds;
            }
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
                if (g_daw.save_prompt_open || g_export.is_open) continue;
                SDL_Keymod mod = SDL_GetModState();
                int mx, my; SDL_GetMouseState(&mx, &my);
                if (mod & KMOD_CTRL) {
                    double mouse_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                    p->pixels_per_second += e.wheel.y * 12.0f;
                    if (p->pixels_per_second < 15.0f) p->pixels_per_second = 15.0f;
                    if (p->pixels_per_second > 400.0f) p->pixels_per_second = 400.0f;
                    p->scroll_x_sec = mouse_time - (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                    if (p->scroll_x_sec < 0) p->scroll_x_sec = 0;
                } else if (mod & KMOD_SHIFT) {
                    p->scroll_x_sec -= e.wheel.y * (30.0f / p->pixels_per_second);
                    if (p->scroll_x_sec < 0) p->scroll_x_sec = 0;
                } else {
                    p->scroll_y_px -= e.wheel.y * 36;
                    int max_y_scroll = max_h_tracks - view_h;
                    if (max_y_scroll < 0) max_y_scroll = 0;
                    if (p->scroll_y_px < 0) p->scroll_y_px = 0;
                    if (p->scroll_y_px > max_y_scroll) p->scroll_y_px = max_y_scroll;
                }
            } else if (e.type == SDL_TEXTINPUT) {
                if (g_daw.save_prompt_open) continue;
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
                if (g_daw.save_prompt_open) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        handle_save_modal_choice(3, &running);
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
                        p->is_dirty = true;
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
                        p->is_dirty = true;
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

                // Note: Pressing 'M' does NOT add a marker (markers only added via right-click)
                if ((mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_z) perform_undo();
                else if ((mod & KMOD_CTRL) && (e.key.keysym.sym == SDLK_y || ((mod & KMOD_SHIFT) && e.key.keysym.sym == SDLK_z))) perform_redo();
                else if ((mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_s) {
                    daw_save_project_dialog(p, false);
                }
                else if (e.key.keysym.sym == SDLK_DELETE || e.key.keysym.sym == SDLK_BACKSPACE) {
                    if (g_selected_marker_idx >= 0 && g_selected_marker_idx < p->num_markers) {
                        delete_marker_at(p, g_selected_marker_idx);
                    }
                }
                else if (e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_PLUS || e.key.keysym.sym == SDLK_KP_PLUS)
                    p->pixels_per_second = fminf(400.0f, p->pixels_per_second * 0.8f > 0 ? p->pixels_per_second * 1.25f : 100.0f);
                else if (e.key.keysym.sym == SDLK_MINUS || e.key.keysym.sym == SDLK_KP_MINUS)
                    p->pixels_per_second = fmaxf(15.0f, p->pixels_per_second * 0.8f);
                else if (e.key.keysym.sym == SDLK_SPACE) p->is_playing = !p->is_playing;
                else if (e.key.keysym.sym == SDLK_v) p->video.is_open = !p->video.is_open;
                else if (e.key.keysym.sym == SDLK_HOME) { p->playhead_seconds = 0.0; }
                else if (e.key.keysym.sym == SDLK_s && !(mod & KMOD_CTRL)) {
                    double cur_t = p->playhead_seconds;
                    for (int i = 0; i < p->num_items; i++) {
                        AudioItem *it = &p->items[i];
                        if (it->is_selected) {
                            double it_s = it->start_time, it_e = it->start_time + it->duration;
                            if (cur_t > it_s + 0.05 && cur_t < it_e - 0.05 && p->num_items < MAX_ITEMS) {
                                push_undo_state();
                                int r_idx = p->num_items;
                                int tab_i = get_valid_tab_idx();
                                double orig_fin = g_item_fade_in[tab_i][i];
                                double orig_fout = g_item_fade_out[tab_i][i];
                                AudioItem *r_item = &p->items[r_idx];
                                memcpy(r_item, it, sizeof(AudioItem));
                                double delta = cur_t - it_s;
                                it->duration = delta;
                                r_item->start_time = cur_t;
                                r_item->offset_in_file = it->offset_in_file + delta;
                                r_item->duration = it_e - cur_t;
                                r_item->is_selected = true;
                                it->is_selected = false;
                                r_item->raw_file_handle = r_item->raw_audio_path[0] ? fopen(r_item->raw_audio_path, "rb") : NULL;
                                p->num_items++;
                                g_item_fade_in[tab_i][i] = fmin(orig_fin, it->duration);
                                g_item_fade_out[tab_i][i] = 0.0;
                                g_item_fade_in[tab_i][r_idx] = 0.0;
                                g_item_fade_out[tab_i][r_idx] = fmin(orig_fout, r_item->duration);
                                break;
                            }
                        }
                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x, my = e.button.y;
                SDL_Keymod mod = SDL_GetModState();
                bool shift_pressed = (mod & KMOD_SHIFT);
                bool ctrl_pressed = (mod & KMOD_CTRL);

                if (g_daw.save_prompt_open) {
                    int dia_w = 380, dia_h = 140;
                    int dia_x = (win_w - dia_w) / 2;
                    int dia_y = (win_h - dia_h) / 2;
                    int btn_y = dia_y + dia_h - 44;

                    // [Save]
                    if (mx >= dia_x + 18 && mx <= dia_x + 118 && my >= btn_y && my <= btn_y + 30) {
                        int t_idx = g_daw.save_prompt_tab_idx;
                        g_daw.save_prompt_open = false;
                        g_daw.save_prompt_tab_idx = -1;
                        g_daw.save_prompt_quit_app = false;
                        if (t_idx >= 0 && t_idx < g_daw.num_tabs) {
                            daw_save_project_dialog(&g_daw.tabs[t_idx], false);
                        }
                        continue;
                    }
                    // [Don't Save]
                    if (mx >= dia_x + 130 && mx <= dia_x + 250 && my >= btn_y && my <= btn_y + 30) {
                        handle_save_modal_choice(2, &running);
                        continue;
                    }
                    // [Cancel]
                    if (mx >= dia_x + 262 && mx <= dia_x + 362 && my >= btn_y && my <= btn_y + 30) {
                        handle_save_modal_choice(3, &running);
                        continue;
                    }
                    continue;
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
                            // Row 3: Audio: [AAC]  [128] kbps (AAC only for MP4)
                            int r3_y = sy_sub + 84;
                            if (my >= r3_y && my <= r3_y + 26) {
                                g_export.mp4_audio_codec = EXPORT_MP4_AUDIO_AAC;
                                if (mx >= ex + 295 && mx <= ex + 395) {
                                    g_export.active_field = EXP_FIELD_MP4_A_KBPS;
                                    SDL_StartTextInput();
                                }
                            }
                        }

                        // 7. Option: Two-pass render checkbox (ey + 440 .. ey + 466)
                        int sy_opt = ey + 440;
                        if (my >= sy_opt && my <= sy_opt + 24 && mx >= ex + 22 && mx <= ex + 240) {
                            g_export.two_pass_render = !g_export.two_pass_render;
                        }

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

                // ================= HIDE TRACKS POPUP WINDOW INTERACTIONS =================
                if (g_hide_popup_open) {
                    SDL_Rect *hr = &g_hide_win_rect;
                    int hx = hr->x, hy = hr->y, hw = hr->w, hh = hr->h;

                    // Title bar drag or [X] close
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
                        // Track checkbox list area (hy + 58 .. hy + hh - 48)
                        int list_y = hy + 58;
                        int list_h = hh - 106;
                        if (my >= list_y && my < list_y + list_h && mx >= hx + 14 && mx <= hx + hw - 14) {
                            int row = (my - (list_y + 4)) / 26;
                            if (row >= 0 && row < p->num_tracks) {
                                set_track_hidden(row, !is_track_hidden(row));
                            }
                        }

                        // Bottom action buttons: [Show All], [Hide All], [Close]
                        int b_y = hy + hh - 38;
                        if (my >= b_y && my <= b_y + 28) {
                            if (mx >= hx + 14 && mx <= hx + 114) {
                                for (int t = 0; t < p->num_tracks; t++) set_track_hidden(t, false);
                            } else if (mx >= hx + 124 && mx <= hx + 224) {
                                for (int t = 0; t < p->num_tracks; t++) set_track_hidden(t, true);
                            } else if (mx >= hx + hw - 94 && mx <= hx + hw - 14) {
                                g_hide_popup_open = false;
                            }
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
                        g_daw.active_tab = g_daw.num_tabs++;
                        g_selected_marker_idx = -1;
                    }
                    continue;
                }

                if (g_daw.script_manager_open) {
                    SDL_Rect *sr = &g_daw.script_win_rect;
                    if (mx >= sr->x && mx <= sr->x + sr->w && my >= sr->y && my <= sr->y + 26) {
                        if (mx >= sr->x + sr->w - 24) {
                            g_daw.script_manager_open = false;
                        } else {
                            g_daw.script_win_dragging = true;
                            g_daw.script_drag_offset_x = mx - sr->x;
                            g_daw.script_drag_offset_y = my - sr->y;
                        }
                        continue;
                    }
                    if (mx >= sr->x && mx <= sr->x + sr->w && my >= sr->y + 26 && my <= sr->y + sr->h) {
                        int by = sr->y + sr->h - 38;
                        if (my >= by && my <= by + 28) {
                            if (mx >= sr->x + 12 && mx <= sr->x + 104) {
                                char spath[MAX_PATH];
                                const char *flt = g_daw.script_manager_is_python ? "Python (*.py)\0*.py\0" : "Lua (*.lua)\0*.lua\0";
                                if (open_generic_dialog(spath, sizeof(spath), flt, false) && g_daw.num_scripts < MAX_SCRIPTS) {
                                    DawScript *sc = &g_daw.scripts[g_daw.num_scripts++];
                                    sc->is_python = g_daw.script_manager_is_python;
                                    strncpy(sc->filepath, spath, sizeof(sc->filepath) - 1);
                                    const char *fn = strrchr(spath, '\\');
                                    if (!fn) fn = strrchr(spath, '/');
                                    strncpy(sc->name, fn ? fn + 1 : spath, sizeof(sc->name) - 1);
                                    strcpy(sc->shortcut, "F8");
                                }
                            } else if (mx >= sr->x + 114 && mx <= sr->x + 206) {
                                if (g_daw.script_selected_idx >= 0 && g_daw.script_selected_idx < g_daw.num_scripts) {
                                    printf("Executed script: %s\n", g_daw.scripts[g_daw.script_selected_idx].filepath);
                                }
                            } else if (mx >= sr->x + 216 && mx <= sr->x + 308) {
                                if (g_daw.script_selected_idx >= 0 && g_daw.script_selected_idx < g_daw.num_scripts) {
                                    for (int s = g_daw.script_selected_idx; s < g_daw.num_scripts - 1; s++) {
                                        g_daw.scripts[s] = g_daw.scripts[s + 1];
                                    }
                                    g_daw.num_scripts--;
                                    g_daw.script_selected_idx = -1;
                                }
                            }
                        }
                        int ly = sr->y + 34;
                        if (my >= ly && my <= ly + 160 && mx >= sr->x + 12 && mx <= sr->x + sr->w - 12) {
                            int r = (my - ly) / 22;
                            if (r >= 0 && r < g_daw.num_scripts) g_daw.script_selected_idx = r;
                        }
                        continue;
                    }
                }

                if ((g_daw.menu_file_open || g_daw.menu_action_open) && (mx > 320 || my > 270)) {
                    g_daw.menu_file_open = false;
                    g_daw.menu_action_open = false;
                    g_daw.submenu_export_audio_open = false;
                    g_daw.submenu_export_video_open = false;
                    continue;
                }

                if (g_daw.menu_file_open) {
                    if (mx >= 8 && mx <= 200) {
                        int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                        if (my >= m_start && my < m_start + 24) {
                            // Row 0: Import Project (.ehs) -> Clean Project Import with full metadata restoration
                            g_daw.menu_file_open = false;
                            daw_load_project_dialog(p, ren, win);
                            continue;
                        }
                        else if (my >= m_start + 24 && my < m_start + 48) {
                            // Row 1: Export Project (.ehs) -> Clean Project Export with full metadata
                            g_daw.menu_file_open = false;
                            daw_save_project_dialog(p, true);
                            continue;
                        }
                        else if (my >= m_start + 48 && my < m_start + 72) {
                            // Row 2: Import Media (supports single or multiple media files)
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
                        else if (my >= m_start + 72 && my < m_start + 100) {
                            // Row 3: Opens the unified Export Media Popup Window
                            g_export.is_open = true;
                            g_export.win_rect.x = (win_w - g_export.win_rect.w) / 2;
                            g_export.win_rect.y = (win_h - g_export.win_rect.h) / 2;
                            g_export.is_rendering = false;
                            g_export.progress_pct = 0;
                            g_export.elapsed_sec = 0.0;
                            g_export.est_remaining_sec = 0.0;
                            g_export.status_msg[0] = '\0';
                            g_daw.menu_file_open = false;
                            g_daw.submenu_export_audio_open = false;
                            g_daw.submenu_export_video_open = false;
                            continue;
                        }
                    }
                }

                if (g_daw.menu_action_open) {
                    if (mx >= 60 && mx <= 194) {
                        int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                        if (my >= m_start && my < m_start + 26) {
                            g_daw.script_manager_open = true;
                            g_daw.script_manager_is_python = false;
                            g_daw.menu_action_open = false;
                            continue;
                        } else if (my >= m_start + 26 && my < m_start + 52) {
                            g_daw.script_manager_open = true;
                            g_daw.script_manager_is_python = true;
                            g_daw.menu_action_open = false;
                            continue;
                        }
                    }
                }

                if (my >= TABBAR_HEIGHT && my <= TABBAR_HEIGHT + TOPBAR_HEIGHT) {
                    // Left-side top menu: File | Action | Marker | Video
                    if (mx >= 8 && mx <= 54) { g_daw.menu_file_open = !g_daw.menu_file_open; g_daw.menu_action_open = false; }
                    else if (mx >= 60 && mx <= 120) { g_daw.menu_action_open = !g_daw.menu_action_open; g_daw.menu_file_open = false; }
                    else if (mx >= 126 && mx <= 190) { p->show_marker_lane = !p->show_marker_lane; }
                    else if (mx >= 196 && mx <= 256) { p->video.is_open = !p->video.is_open; }
                    // Right-side top menu: Undo | Redo | Zoom- | Zoom+ | Range | Crossfade | Hide | Save
                    else if (mx >= win_w - 520 && mx <= win_w - 468) { perform_undo(); }
                    else if (mx >= win_w - 462 && mx <= win_w - 410) { perform_redo(); }
                    else if (mx >= win_w - 404 && mx <= win_w - 348) { p->pixels_per_second = fmaxf(15.0f, p->pixels_per_second * 0.8f); }
                    else if (mx >= win_w - 342 && mx <= win_w - 286) { p->pixels_per_second = fminf(400.0f, p->pixels_per_second * 1.25f); }
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
                    } else if (mx >= 94 && mx <= 180) p->is_playing = !p->is_playing;
                    continue;
                }

                if (p->video.is_open && mx >= p->video.rect.x && mx <= p->video.rect.x + p->video.rect.w &&
                    my >= p->video.rect.y && my <= p->video.rect.y + p->video.rect.h) {
                    if (mx >= p->video.rect.x + p->video.rect.w - 18 && my >= p->video.rect.y + p->video.rect.h - 18) {
                        p->video.is_resizing = true;
                    } else if (mx >= p->video.rect.x + p->video.rect.w - 24 && my <= p->video.rect.y + 24) {
                        p->video.is_open = false;
                    } else if (my <= p->video.rect.y + 24) {
                        p->video.is_dragging = true;
                        p->video.drag_x = mx - p->video.rect.x;
                        p->video.drag_y = my - p->video.rect.y;
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
                            p->is_dirty = true;
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
                    if (e.button.button == SDL_BUTTON_RIGHT) {
                        // Right-clicking the ruler auto-shows the marker lane and adds a marker
                        p->show_marker_lane = true;
                        if (p->num_markers < MAX_MARKERS) {
                            push_undo_state();
                            double m_time = fmax(0.0, p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second);
                            int new_idx = p->num_markers++;
                            DawMarker *mk = &p->markers[new_idx];
                            mk->time_sec = m_time;
                            char def_txt[64];
                            snprintf(def_txt, sizeof(def_txt), "Marker %d", p->num_markers);
                            set_marker_full_str(p, new_idx, def_txt);
                            g_selected_marker_idx = new_idx;
                            p->is_dirty = true;
                        }
                    } else {
                        scrubbing_ruler = true;
                        p->playhead_seconds = fmax(0.0, p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second);
                    }
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
                            } else if (shift_pressed) {
                                for (int tr = 0; tr <= t; tr++) p->track_selected[tr] = true;
                            } else {
                                for (int tr = 0; tr < p->num_tracks; tr++) p->track_selected[tr] = false;
                                p->track_selected[t] = true;
                            }

                            bool handled_btn = false;
                            if (mx >= g_daw.tcp_width - 28 && mx <= g_daw.tcp_width - 6) {
                                dragging_track_order_idx = t;
                                dragging_track_order_pushed_undo = false;
                                handled_btn = true;
                            } else if (rel_y >= 30 && rel_y <= 56) {
                                if (mx >= 8 && mx <= 26) { p->track_db[t] -= 1.0f; p->is_dirty = true; handled_btn = true; }
                                else if (mx >= 76 && mx <= 94) { p->track_db[t] += 1.0f; p->is_dirty = true; handled_btn = true; }
                                else if (mx >= 110 && mx <= 132) { p->tracks[t].mute = !p->tracks[t].mute; p->is_dirty = true; handled_btn = true; }
                                else if (mx >= 138 && mx <= 160) { p->tracks[t].solo = !p->tracks[t].solo; p->is_dirty = true; handled_btn = true; }
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
                    int cur_y = header_top_y - p->scroll_y_px;
                    int click_track = -1;
                    for (int t = 0; t < p->num_tracks; t++) {
                        if (is_track_hidden(t)) continue;
                        if (my >= cur_y && my < cur_y + p->track_heights[t]) { click_track = t; break; }
                        cur_y += p->track_heights[t];
                    }

                    bool clicked_clip = false;
                    for (int i = p->num_items - 1; i >= 0; i--) {
                        AudioItem *it = &p->items[i];
                        if (is_track_hidden(it->track_idx)) continue;
                        if (it->track_idx == click_track && click_time >= it->start_time && click_time <= it->start_time + it->duration) {
                            clicked_clip = true;
                            push_undo_state();
                            if (ctrl_pressed) it->is_selected = !it->is_selected;
                            else if (!shift_pressed && !it->is_selected) {
                                for (int o = 0; o < p->num_items; o++) p->items[o].is_selected = false;
                                it->is_selected = true;
                            }
                            dragging_item_idx = i;
                            drag_start_mouse_time = click_time;
                            item_orig_start = it->start_time;
                            item_orig_dur = it->duration;
                            item_orig_offset = it->offset_in_file;

                            int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
                            int iw = (int)(it->duration * p->pixels_per_second);
                            int iy = get_track_y_pos(it->track_idx) + 2;
                            int fin_px  = (int)(get_item_fade_in(p, i)  * p->pixels_per_second);
                            int fout_px = (int)(get_item_fade_out(p, i) * p->pixels_per_second);
                            int fin_hx  = ix + fin_px;
                            int fout_hx = ix + iw - fout_px;

                            if (my >= iy && my <= iy + 18 && abs(mx - fin_hx) <= 10) {
                                dragging_fade_in = true;
                            } else if (my >= iy && my <= iy + 18 && abs(mx - fout_hx) <= 10) {
                                dragging_fade_out = true;
                            } else if (mx <= ix + 8) {
                                dragging_trim_left = true;
                            } else if (mx >= ix + iw - 8) {
                                dragging_trim_right = true;
                            }
                            break;
                        }
                    }

                    if (!clicked_clip) {
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
                p->video.is_dragging = false; p->video.is_resizing = false;
                g_export.is_dragging = false;
                g_hide_win_dragging = false;
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
                if (dragging_item_idx != -1) p->is_dirty = true;
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

                if (g_export.is_open && g_export.is_dragging) {
                    g_export.win_rect.x = mx - g_export.drag_off_x;
                    g_export.win_rect.y = my - g_export.drag_off_y;
                    continue;
                }

                if (g_multi_import.is_open && g_multi_import.is_dragging) {
                    g_multi_import.win_rect.x = mx - g_multi_import.drag_off_x;
                    g_multi_import.win_rect.y = my - g_multi_import.drag_off_y;
                    continue;
                }

                if (g_hide_popup_open && g_hide_win_dragging) {
                    g_hide_win_rect.x = mx - g_hide_drag_off_x;
                    g_hide_win_rect.y = my - g_hide_drag_off_y;
                    continue;
                }

                if (g_daw.script_win_dragging) {
                    g_daw.script_win_rect.x = mx - g_daw.script_drag_offset_x;
                    g_daw.script_win_rect.y = my - g_daw.script_drag_offset_y;
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
                            SDL_SetCursor(g_cursor_size_ns);
                            hovered_edge = true;
                            break;
                        }
                        if (my >= cur_y + 6 && my <= cur_y + 28 && mx >= 8 && mx <= g_daw.tcp_width - 32) {
                            SDL_SetCursor(g_cursor_ibeam);
                            hovered_ibeam = true;
                            break;
                        }
                        cur_y += th;
                    }
                } else if (!hovered_edge && mx >= g_daw.tcp_width && my > header_top_y) {
                    for (int i = 0; i < p->num_items; i++) {
                        AudioItem *it = &p->items[i];
                        if (is_track_hidden(it->track_idx)) continue;
                        int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
                        int iw = (int)(it->duration * p->pixels_per_second);
                        int iy = get_track_y_pos(it->track_idx);
                        int fin_hx  = ix + (int)(get_item_fade_in(p, i)  * p->pixels_per_second);
                        int fout_hx = ix + iw - (int)(get_item_fade_out(p, i) * p->pixels_per_second);
                        if (my >= iy && my <= iy + p->track_heights[it->track_idx]) {
                            if (abs(mx - ix) <= 8 || abs(mx - (ix + iw)) <= 8 ||
                                (my <= iy + 20 && (abs(mx - fin_hx) <= 10 || abs(mx - fout_hx) <= 10))) {
                                SDL_SetCursor(g_cursor_size_we);
                                hovered_edge = true;
                                break;
                            }
                        }
                    }
                }
                if (!hovered_edge && !hovered_ibeam && !resizing_tcp_width && resizing_track_idx < 0 &&
                    dragging_track_order_idx < 0 && !dragging_range_left && !dragging_range_right &&
                    !dragging_fade_in && !dragging_fade_out) {
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
                    SDL_SetCursor(g_cursor_size_ns);
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

                if (p->video.is_resizing) {
                    int new_w = mx - p->video.rect.x;
                    int new_h = my - p->video.rect.y;
                    if (new_w >= 160) p->video.rect.w = new_w;
                    if (new_h >= 100) p->video.rect.h = new_h;
                }
                if (p->video.is_dragging) {
                    p->video.rect.x = mx - p->video.drag_x;
                    p->video.rect.y = my - p->video.drag_y;
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

        // 2. Top Bar (Left: File Action Marker Video | Right: Undo Redo Zoom- Zoom+ Range Crossfade Hide)
        SDL_Rect topbar = { 0, TABBAR_HEIGHT, win_w, TOPBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 28, 28, 28, 255);
        SDL_RenderFillRect(ren, &topbar);
        draw_brown_border_rect(ren, &topbar, false, true);

        // Left-side top menu: File | Action | Marker | Video
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

        SDL_Rect btn_mk_toggle = { 126, TABBAR_HEIGHT + 5, 64, 26 };
        SDL_SetRenderDrawColor(ren, p->show_marker_lane ? 62 : 40, p->show_marker_lane ? 46 : 40, p->show_marker_lane ? 32 : 40, 255);
        SDL_RenderFillRect(ren, &btn_mk_toggle);
        draw_brown_border_rect(ren, &btn_mk_toggle, p->show_marker_lane, false);
        draw_text(ren, "Marker", 134, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect v_menu = { 196, TABBAR_HEIGHT + 5, 60, 26 };
        SDL_SetRenderDrawColor(ren, p->video.is_open ? 62 : 40, p->video.is_open ? 46 : 40, p->video.is_open ? 32 : 40, 255);
        SDL_RenderFillRect(ren, &v_menu);
        draw_brown_border_rect(ren, &v_menu, p->video.is_open, false);
        draw_text(ren, "Video", 206, TABBAR_HEIGHT + 13, c_white);

        // Center Timecode Display
        char clock_buf[32];
        format_timecode(p->playhead_seconds, clock_buf, sizeof(clock_buf));
        int left_menu_end = 266;
        int right_menu_start = win_w - 526;
        int clock_x = left_menu_end + ((right_menu_start - left_menu_end) / 2) - 54;
        if (clock_x < left_menu_end) clock_x = left_menu_end;
        SDL_Rect clock_bg = { clock_x - 10, TABBAR_HEIGHT + 4, 128, 28 };
        SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
        SDL_RenderFillRect(ren, &clock_bg);
        draw_brown_border_rect(ren, &clock_bg, false, false);
        draw_text(ren, clock_buf, clock_x - 4, TABBAR_HEIGHT + 12, c_white);

        // Right-side top menu: Undo | Redo | Zoom- | Zoom+ | Range | Crossfade | Hide | Save
        SDL_Rect btn_undo = { win_w - 520, TABBAR_HEIGHT + 5, 52, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_undo);
        draw_brown_border_rect(ren, &btn_undo, false, false);
        draw_text(ren, "Undo", win_w - 510, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect btn_redo = { win_w - 462, TABBAR_HEIGHT + 5, 52, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_redo);
        draw_brown_border_rect(ren, &btn_redo, false, false);
        draw_text(ren, "Redo", win_w - 452, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect btn_zm_out = { win_w - 404, TABBAR_HEIGHT + 5, 56, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_zm_out);
        draw_brown_border_rect(ren, &btn_zm_out, false, false);
        draw_text(ren, "Zoom-", win_w - 396, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect btn_zm_in = { win_w - 342, TABBAR_HEIGHT + 5, 56, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_zm_in);
        draw_brown_border_rect(ren, &btn_zm_in, false, false);
        draw_text(ren, "Zoom+", win_w - 334, TABBAR_HEIGHT + 13, c_white);

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

        // 3. Corner Header
        int corner_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
        SDL_Rect corner_area = { 0, corner_y, g_daw.tcp_width, RULER_HEIGHT + m_lane_h };
        SDL_SetRenderDrawColor(ren, 32, 32, 32, 255);
        SDL_RenderFillRect(ren, &corner_area);
        draw_brown_border_rect(ren, &corner_area, false, true);

        SDL_Rect btn_corner_add = { 8, corner_y + 10, 72, 26 };
        SDL_SetRenderDrawColor(ren, 48, 48, 48, 255);
        SDL_RenderFillRect(ren, &btn_corner_add);
        draw_brown_border_rect(ren, &btn_corner_add, false, false);
        draw_text(ren, "+ Track", 16, corner_y + 18, c_white);

        SDL_Rect btn_corner_play = { 86, corner_y + 10, 72, 26 };
        SDL_SetRenderDrawColor(ren, p->is_playing ? 68 : 48, p->is_playing ? 50 : 48, p->is_playing ? 34 : 48, 255);
        SDL_RenderFillRect(ren, &btn_corner_play);
        draw_brown_border_rect(ren, &btn_corner_play, p->is_playing, false);
        draw_text(ren, p->is_playing ? "Pause" : "Play", 102, corner_y + 18, c_white);

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
        for (double t = start_tick; t <= end_tick; t += chosen_step) {
            int gx = g_daw.tcp_width + (int)((t - p->scroll_x_sec) * p->pixels_per_second);
            if (gx < g_daw.tcp_width || gx > g_daw.tcp_width + view_w) continue;
            SDL_SetRenderDrawColor(ren, 118, 78, 46, 65);
            SDL_RenderDrawLine(ren, gx, header_top_y, gx, header_top_y + view_h);
        }

        // Timeline tracks: Each track has an identical simple faded brown edge; selected track edge is slightly thicker
        int track_cur_y = header_top_y - p->scroll_y_px;
        for (int i = 0; i < p->num_tracks; i++) {
            if (is_track_hidden(i)) continue;
            int th = p->track_heights[i];
            SDL_Rect lane_border = { g_daw.tcp_width, track_cur_y, view_w, th };
            if (p->track_selected[i]) {
                SDL_SetRenderDrawColor(ren, 205, 138, 72, 235);
                SDL_RenderDrawRect(ren, &lane_border);
                if (th > 4 && view_w > 4) {
                    SDL_Rect lane_inner = { g_daw.tcp_width + 1, track_cur_y + 1, view_w - 2, th - 2 };
                    SDL_RenderDrawRect(ren, &lane_inner);
                }
            } else {
                SDL_SetRenderDrawColor(ren, 118, 78, 46, 135);
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

            SDL_Rect clip_r = { ix, iy, iw, ih };
            SDL_SetRenderDrawColor(ren, it->is_selected ? 48 : 36, it->is_selected ? 38 : 32, it->is_selected ? 30 : 28, 255);
            SDL_RenderFillRect(ren, &clip_r);

            int hdr_h = (ih > 18) ? 18 : ih;
            SDL_Rect hdr_r = { ix, iy, iw, hdr_h };
            SDL_SetRenderDrawColor(ren, it->is_selected ? 68 : 48, it->is_selected ? 50 : 38, it->is_selected ? 34 : 30, 255);
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

            float gain_multiplier = db_to_linear(p->track_db[it->track_idx]);
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

            // Clip name strictly clipped to clip length (text only shows until clip right edge)
            if (iw > 16) {
                SDL_Rect clip_text_bounds = { ix + 8, iy + 1, iw - 16, ih - 2 };
                draw_text_clipped(ren, it->label, ix + 10, iy + 4, c_white, &clip_text_bounds, &timeline_clip);
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

        // 7. Action Menu (with Brown Border)
        if (g_daw.menu_action_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            SDL_Rect a_box = { 60, m_start, 134, 56 };
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, &a_box);
            draw_brown_border_rect(ren, &a_box, false, false);
            draw_text(ren, "Lua Scripts...", 64, m_start + 8, c_white);
            draw_text(ren, "Python Scripts...", 64, m_start + 32, c_white);
        }

        // Script Manager (with Brown Borders)
        if (g_daw.script_manager_open) {
            SDL_Rect *sr = &g_daw.script_win_rect;
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, sr);
            draw_brown_border_rect(ren, sr, false, false);

            SDL_Rect s_hdr = { sr->x, sr->y, sr->w, 26 };
            SDL_SetRenderDrawColor(ren, 36, 36, 36, 255);
            SDL_RenderFillRect(ren, &s_hdr);
            draw_brown_border_rect(ren, &s_hdr, false, false);

            char win_title[64];
            snprintf(win_title, sizeof(win_title), "%s Script Manager", g_daw.script_manager_is_python ? "Python" : "Lua");
            draw_text(ren, win_title, sr->x + 8, sr->y + 8, c_white);
            draw_text(ren, "X", sr->x + sr->w - 18, sr->y + 8, c_white);

            SDL_Rect list_box = { sr->x + 12, sr->y + 34, sr->w - 24, 180 };
            SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
            SDL_RenderFillRect(ren, &list_box);
            draw_brown_border_rect(ren, &list_box, false, false);

            for (int s = 0; s < g_daw.num_scripts; s++) {
                int sy = sr->y + 38 + (s * 22);
                if (s == g_daw.script_selected_idx) {
                    SDL_Rect s_row = { sr->x + 14, sy - 2, sr->w - 28, 20 };
                    SDL_SetRenderDrawColor(ren, 56, 42, 30, 255);
                    SDL_RenderFillRect(ren, &s_row);
                    draw_brown_border_rect(ren, &s_row, false, false);
                }
                draw_text(ren, g_daw.scripts[s].name, sr->x + 18, sy + 2, c_white);
                draw_text(ren, g_daw.scripts[s].shortcut, sr->x + sr->w - 70, sy + 2, c_white);
            }

            int by = sr->y + sr->h - 38;
            SDL_Rect btn_add_sc = { sr->x + 12, by, 92, 28 };
            SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
            SDL_RenderFillRect(ren, &btn_add_sc);
            draw_brown_border_rect(ren, &btn_add_sc, false, false);
            draw_text(ren, "Add Script", sr->x + 18, by + 8, c_white);

            SDL_Rect btn_run_sc = { sr->x + 114, by, 92, 28 };
            SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
            SDL_RenderFillRect(ren, &btn_run_sc);
            draw_brown_border_rect(ren, &btn_run_sc, false, false);
            draw_text(ren, "Run Script", sr->x + 120, by + 8, c_white);

            SDL_Rect btn_del_sc = { sr->x + 216, by, 92, 28 };
            SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
            SDL_RenderFillRect(ren, &btn_del_sc);
            draw_brown_border_rect(ren, &btn_del_sc, false, false);
            draw_text(ren, "Delete", sr->x + 236, by + 8, c_white);
        }

        // File Menu (Unified single "Export Media..." option)
        if (g_daw.menu_file_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            SDL_Rect m_box = { 8, m_start, 192, 102 };
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, &m_box);
            draw_brown_border_rect(ren, &m_box, false, false);

            draw_text(ren, "Import Project (.ehs)", 16, m_start + 6, c_white);
            draw_text(ren, "Export Project (.ehs)", 16, m_start + 30, c_white);
            draw_text(ren, "Import Media", 16, m_start + 54, c_white);
            draw_text(ren, "Export Media...", 16, m_start + 78, c_white);
        }

        // Floating Video Monitor (With Black Gap Handling & Brown Borders)
        if (p->video.is_open) {
            SDL_Rect s_rect = { p->video.rect.x + 4, p->video.rect.y + 4, p->video.rect.w, p->video.rect.h };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 140);
            SDL_RenderFillRect(ren, &s_rect);

            SDL_Rect v_bar = { p->video.rect.x, p->video.rect.y, p->video.rect.w, 24 };
            SDL_SetRenderDrawColor(ren, 36, 36, 36, 255);
            SDL_RenderFillRect(ren, &v_bar);
            draw_brown_border_rect(ren, &v_bar, false, false);

            draw_text(ren, "Video Monitor", p->video.rect.x + 8, p->video.rect.y + 7, c_white);
            draw_text(ren, "X", p->video.rect.x + p->video.rect.w - 18, p->video.rect.y + 7, c_white);

            SDL_Rect v_canvas = { p->video.rect.x, p->video.rect.y + 24, p->video.rect.w, p->video.rect.h - 24 };
            
            // Background is always solid black
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderFillRect(ren, &v_canvas);

            if (p->video.has_active_frame && p->video.texture) {
                SDL_RenderCopy(ren, p->video.texture, NULL, &v_canvas);
            } else if (!p->video.texture) {
                draw_text(ren, "No Video Stream Loaded", p->video.rect.x + 120, p->video.rect.y + 140, c_muted);
            }
            draw_brown_border_rect(ren, &v_canvas, false, false);

            SDL_Rect resize_handle = { p->video.rect.x + p->video.rect.w - 12, p->video.rect.y + p->video.rect.h - 12, 10, 10 };
            SDL_SetRenderDrawColor(ren, 152, 98, 54, 255);
            SDL_RenderFillRect(ren, &resize_handle);
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

                // Row 3 (matching image_736e58.png — AAC only for MP4 video):
                // Audio: [ AAC ]   [ 128 ] kbps
                int r3_y = sy_sub + 84;
                draw_text(ren, "Audio:", ex + 34, r3_y + 7, c_white);

                SDL_Rect box_ac_aac = { ex + 92, r3_y, 176, 26 };
                SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
                SDL_RenderFillRect(ren, &box_ac_aac);
                draw_brown_border_rect(ren, &box_ac_aac, true, false);
                draw_text(ren, "AAC", ex + 104, r3_y + 7, c_white);

                SDL_Rect box_akbps = { ex + 295, r3_y, 100, 26 };
                bool ed_ak = (g_export.active_field == EXP_FIELD_MP4_A_KBPS);
                SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
                SDL_RenderFillRect(ren, &box_akbps);
                draw_brown_border_rect(ren, &box_akbps, ed_ak, false);
                draw_text(ren, g_export.mp4_audio_kbps_str, box_akbps.x + 8, r3_y + 7, c_white);
                if (ed_ak && ((SDL_GetTicks() / 450) % 2 == 0)) {
                    int cx = box_akbps.x + 8 + (int)strlen(g_export.mp4_audio_kbps_str) * 8;
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_RenderDrawLine(ren, cx, r3_y + 5, cx, r3_y + 21);
                }
                draw_text(ren, "kbps", ex + 405, r3_y + 7, c_white);
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

        // ================= HIDE TRACKS POPUP WINDOW =================
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

            draw_text(ren, "Click checkbox to hide track:", hx + 14, hy + 36, c_muted);

            SDL_Rect list_box = { hx + 14, hy + 58, hw - 28, hh - 106 };
            SDL_SetRenderDrawColor(ren, 18, 17, 16, 255);
            SDL_RenderFillRect(ren, &list_box);
            draw_brown_border_rect(ren, &list_box, false, true);

            SDL_RenderSetClipRect(ren, &list_box);
            for (int t = 0; t < p->num_tracks; t++) {
                int ry = list_box.y + 4 + t * 26;
                if (ry + 24 < list_box.y || ry > list_box.y + list_box.h) continue;

                bool hid = is_track_hidden(t);
                SDL_Rect row_r = { list_box.x + 4, ry, list_box.w - 8, 24 };
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
                draw_text_clipped(ren, trk_lbl, row_r.x + 32, ry + 5, c_white, &lbl_clip, &list_box);

                draw_text(ren, hid ? "Hidden" : "Visible", row_r.x + row_r.w - 64, ry + 5, hid ? c_white : c_muted);
            }
            SDL_RenderSetClipRect(ren, NULL);

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

            draw_text(ren, "Save changes before closing?", dia_x + 18, dia_y + 18, c_white);

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

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

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
