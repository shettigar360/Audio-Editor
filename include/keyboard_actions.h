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

#ifndef DAW_TEXT_EDIT_HELPER_DEFINED
#define DAW_TEXT_EDIT_HELPER_DEFINED
typedef struct {
    char *active_buf;
    int cursor_pos;
    bool select_all;
} DawTextEditState;

static DawTextEditState g_text_edit = { NULL, 0, false };

static inline void daw_text_edit_focus_buf(char *buf, bool select_all_init) {
    if (!buf) {
        g_text_edit.active_buf = NULL;
        g_text_edit.cursor_pos = 0;
        g_text_edit.select_all = false;
        return;
    }
    g_text_edit.active_buf = buf;
    g_text_edit.cursor_pos = (int)strlen(buf);
    g_text_edit.select_all = select_all_init;
}

static inline void daw_text_edit_sync_buf(char *buf) {
    if (!buf) return;
    int len = (int)strlen(buf);
    if (g_text_edit.active_buf != buf) {
        g_text_edit.active_buf = buf;
        g_text_edit.cursor_pos = len;
        g_text_edit.select_all = false;
    } else {
        if (g_text_edit.cursor_pos < 0) g_text_edit.cursor_pos = 0;
        if (g_text_edit.cursor_pos > len) g_text_edit.cursor_pos = len;
    }
}

static inline void daw_text_edit_click_cursor(char *buf, int mx, int text_start_x) {
    if (!buf) return;
    int len = (int)strlen(buf);
    g_text_edit.active_buf = buf;
    g_text_edit.select_all = false;
    int pos = (mx - text_start_x + 4) / 8;
    if (pos < 0) pos = 0;
    if (pos > len) pos = len;
    g_text_edit.cursor_pos = pos;
}

static inline void daw_text_edit_insert_text(char *buf, size_t max_len, const char *ins) {
    if (!buf || !ins || max_len == 0) return;
    daw_text_edit_sync_buf(buf);
    if (g_text_edit.select_all) {
        buf[0] = '\0';
        g_text_edit.cursor_pos = 0;
        g_text_edit.select_all = false;
    }
    size_t cur_len = strlen(buf);
    size_t add_len = strlen(ins);
    if (add_len == 0) return;
    if ((size_t)g_text_edit.cursor_pos > cur_len) g_text_edit.cursor_pos = (int)cur_len;
    if (cur_len >= max_len) return;
    if (cur_len + add_len > max_len) {
        add_len = max_len - cur_len;
    }
    memmove(buf + g_text_edit.cursor_pos + add_len,
            buf + g_text_edit.cursor_pos,
            cur_len - (size_t)g_text_edit.cursor_pos + 1);
    memcpy(buf + g_text_edit.cursor_pos, ins, add_len);
    g_text_edit.cursor_pos += (int)add_len;
}

static inline bool daw_text_edit_handle_keydown(char *buf, size_t max_len, SDL_Keycode sym, SDL_Keymod mod) {
    if (!buf || max_len == 0) return false;
    daw_text_edit_sync_buf(buf);
    int len = (int)strlen(buf);
    bool ctrl = (mod & KMOD_CTRL) != 0;

    if (ctrl) {
        if (sym == SDLK_a) {
            g_text_edit.select_all = true;
            g_text_edit.cursor_pos = len;
            return true;
        }
        if (sym == SDLK_c) {
            SDL_SetClipboardText(buf);
            return true;
        }
        if (sym == SDLK_x) {
            SDL_SetClipboardText(buf);
            buf[0] = '\0';
            g_text_edit.cursor_pos = 0;
            g_text_edit.select_all = false;
            return true;
        }
        if (sym == SDLK_v) {
            if (SDL_HasClipboardText()) {
                char *clip = SDL_GetClipboardText();
                if (clip) {
                    char clean[512];
                    size_t j = 0;
                    for (size_t i = 0; clip[i] != '\0' && j + 1 < sizeof(clean); i++) {
                        if (clip[i] != '\r' && clip[i] != '\n') {
                            clean[j++] = clip[i];
                        }
                    }
                    clean[j] = '\0';
                    daw_text_edit_insert_text(buf, max_len, clean);
                    SDL_free(clip);
                }
            }
            return true;
        }
    }

    if (sym == SDLK_LEFT) {
        if (g_text_edit.select_all) {
            g_text_edit.select_all = false;
            g_text_edit.cursor_pos = 0;
        } else if (g_text_edit.cursor_pos > 0) {
            g_text_edit.cursor_pos--;
        }
        return true;
    }
    if (sym == SDLK_RIGHT) {
        if (g_text_edit.select_all) {
            g_text_edit.select_all = false;
            g_text_edit.cursor_pos = len;
        } else if (g_text_edit.cursor_pos < len) {
            g_text_edit.cursor_pos++;
        }
        return true;
    }
    if (sym == SDLK_HOME) {
        g_text_edit.select_all = false;
        g_text_edit.cursor_pos = 0;
        return true;
    }
    if (sym == SDLK_END) {
        g_text_edit.select_all = false;
        g_text_edit.cursor_pos = len;
        return true;
    }
    if (sym == SDLK_BACKSPACE) {
        if (g_text_edit.select_all) {
            buf[0] = '\0';
            g_text_edit.cursor_pos = 0;
            g_text_edit.select_all = false;
        } else if (g_text_edit.cursor_pos > 0 && len > 0) {
            memmove(buf + g_text_edit.cursor_pos - 1,
                    buf + g_text_edit.cursor_pos,
                    (size_t)(len - g_text_edit.cursor_pos + 1));
            g_text_edit.cursor_pos--;
        }
        return true;
    }
    if (sym == SDLK_DELETE) {
        if (g_text_edit.select_all) {
            buf[0] = '\0';
            g_text_edit.cursor_pos = 0;
            g_text_edit.select_all = false;
        } else if (g_text_edit.cursor_pos < len) {
            memmove(buf + g_text_edit.cursor_pos,
                    buf + g_text_edit.cursor_pos + 1,
                    (size_t)(len - g_text_edit.cursor_pos));
        }
        return true;
    }
    return false;
}

static inline void daw_draw_text_edit_overlay(SDL_Renderer *ren, char *buf, int text_x, int box_y, int box_h, int max_x) {
    if (!ren || !buf) return;
    daw_text_edit_sync_buf(buf);
    int len = (int)strlen(buf);
    if (g_text_edit.select_all && len > 0) {
        int sel_w = len * 8 + 2;
        if (text_x + sel_w > max_x) sel_w = max_x - text_x;
        if (sel_w > 0) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_Rect sel_r = { text_x - 1, box_y + 3, sel_w, box_h - 6 };
            SDL_SetRenderDrawColor(ren, 55, 125, 225, 110);
            SDL_RenderFillRect(ren, &sel_r);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }
    }
    if ((SDL_GetTicks() / 450) % 2 == 0) {
        int cx = text_x + g_text_edit.cursor_pos * 8;
        if (cx > max_x) cx = max_x;
        if (cx >= text_x) {
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            SDL_RenderDrawLine(ren, cx, box_y + 4, cx, box_y + box_h - 4);
        }
    }
}
#endif // DAW_TEXT_EDIT_HELPER_DEFINED

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
    { 340, 88, 580, 396 },
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

#ifndef DAW_14POINT_STATE_DEFINED
#define DAW_14POINT_STATE_DEFINED
#define NUM_INBUILT_THEMES 6
static int  g_universal_peak_color_idx = 0;
static bool g_rec_master_enabled = false; // Near Rec menu ON/OFF button (default OFF)
static bool g_menu_settings_open = false;
static bool g_mouse_click_empty_moves_playhead = false;
static bool g_mouse_click_universal_moves_playhead = false;
static bool g_mouse_settings_open = false;
static SDL_Rect g_mouse_win_rect = { 420, 180, 440, 220 };
static bool g_mouse_win_dragging = false;
static int  g_mouse_drag_off_x = 0;
static int  g_mouse_drag_off_y = 0;
static bool g_time_jump_open = false;
static SDL_Rect g_time_jump_rect = { 510, 82, 268, 124 };
static bool g_time_jump_dragging = false;
static int  g_time_jump_drag_off_x = 0;
static int  g_time_jump_drag_off_y = 0;
static char g_time_jump_buf[32] = "";
static bool g_video_pinned_global = false;
static SDL_Window   *g_video_ext_win = NULL;
static SDL_Renderer *g_video_ext_ren = NULL;
static SDL_Texture  *g_video_ext_tex = NULL;
static int           g_video_ext_tex_w = 0;
static int           g_video_ext_tex_h = 0;

#ifndef DAW_TRACK_LOCK_VARS_DEFINED
#define DAW_TRACK_LOCK_VARS_DEFINED
// Per-tab & Per-track Lock State + Main Lock Two-Section Popup Window (All Track | Selected Track)
static bool g_track_locked[MAX_TABS][MAX_TRACKS];
static bool g_track_lock_lr_only[MAX_TABS][MAX_TRACKS];
static bool g_track_lock_ud_only[MAX_TABS][MAX_TRACKS];
static bool g_lock_opt_full[MAX_TABS]            = { false };
static bool g_lock_opt_complete_unlock[MAX_TABS] = { false };
static bool g_lock_opt_prevent_lr[MAX_TABS]      = { false };
static bool g_lock_opt_prevent_ud[MAX_TABS]      = { false };
static bool g_lock_popup_open = false;
static SDL_Rect g_lock_win_rect = { 260, 140, 740, 256 };
static bool g_lock_win_dragging = false;
static int  g_lock_drag_off_x = 0;
static int  g_lock_drag_off_y = 0;

static inline bool is_track_locked_tab(int tab, int trk) {
    if (tab < 0 || tab >= MAX_TABS || trk < 0 || trk >= MAX_TRACKS) return false;
    return g_track_locked[tab][trk];
}
static inline bool is_track_full_locked_tab(int tab, int trk) {
    if (!is_track_locked_tab(tab, trk)) return false;
    return (!g_track_lock_lr_only[tab][trk] && !g_track_lock_ud_only[tab][trk]);
}
static inline bool is_track_lr_locked_tab(int tab, int trk) {
    if (!is_track_locked_tab(tab, trk)) return false;
    return is_track_full_locked_tab(tab, trk) || g_track_lock_lr_only[tab][trk];
}
static inline bool is_track_ud_locked_tab(int tab, int trk) {
    if (!is_track_locked_tab(tab, trk)) return false;
    return is_track_full_locked_tab(tab, trk) || g_track_lock_ud_only[tab][trk];
}

// Draws a crisp Padlock symbol icon at (cx, cy)
static inline void draw_lock_symbol_icon(SDL_Renderer *ren, int cx, int cy, bool is_locked, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    // Shackle arch at top
    SDL_RenderDrawLine(ren, cx + 4, cy + 1, cx + 8, cy + 1);
    SDL_RenderDrawLine(ren, cx + 3, cy + 2, cx + 3, cy + 5);
    if (is_locked) {
        SDL_RenderDrawLine(ren, cx + 9, cy + 2, cx + 9, cy + 5);
    } else {
        SDL_RenderDrawLine(ren, cx + 9, cy + 2, cx + 9, cy + 3);
    }
    // Padlock body
    SDL_Rect body = { cx + 2, cy + 5, 9, 7 };
    if (is_locked) {
        SDL_RenderFillRect(ren, &body);
        SDL_SetRenderDrawColor(ren, 18, 16, 14, 255);
        SDL_RenderDrawLine(ren, cx + 6, cy + 7, cx + 6, cy + 9);
    } else {
        SDL_RenderDrawRect(ren, &body);
    }
}
#endif
#endif

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
    "Temporary Media",
    0.0f, 0.0f, 0, 0, 0,
    false
};

static inline void init_default_recording_folder(void) {
    if (g_rec.folder_initialized) return;
    g_rec.folder_initialized = true;
    char *base = SDL_GetBasePath();
    if (base && base[0]) {
        size_t blen = strlen(base);
        while (blen > 0 && (base[blen - 1] == '\\' || base[blen - 1] == '/')) {
            base[--blen] = '\0';
        }
#ifdef _WIN32
        snprintf(g_rec.folder_path, sizeof(g_rec.folder_path), "%s\\Temporary Media", base);
        CreateDirectoryA(g_rec.folder_path, NULL);
        CreateDirectoryA("Temporary Media", NULL);
#else
        snprintf(g_rec.folder_path, sizeof(g_rec.folder_path), "%s/Temporary Media", base);
        mkdir(g_rec.folder_path, 0777);
        mkdir("Temporary Media", 0777);
#endif
        SDL_free(base);
    } else {
        if (base) SDL_free(base);
        snprintf(g_rec.folder_path, sizeof(g_rec.folder_path), "Temporary Media");
#ifdef _WIN32
        CreateDirectoryA("Temporary Media", NULL);
#else
        mkdir("Temporary Media", 0777);
#endif
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
static bool g_theme_defaults_initialized = false;
static void init_default_saved_themes(void) {
    if (g_theme_defaults_initialized || g_theme.num_saved > 0) {
        g_theme_defaults_initialized = true;
        return;
    }
    g_theme_defaults_initialized = true;
    static const struct { const char *name; uint8_t r, g, b, a; } defs[NUM_INBUILT_THEMES] = {
        { "Default Orange", 205, 138,  72, 255 },
        { "Emerald Green",   77, 191,  59, 255 },
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
    ACT_ITEM_SHOW_PROPERTIES,

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
    ACT_ACTION_OPEN_SCRIPTS,
    ACT_TOOLS_OPEN_THEMES,
    ACT_EDIT_TRACK_COLOR,
    ACT_EDIT_PEAKS_COLOR,
    ACT_SETTINGS_OPEN_MOUSE,
    ACT_TRANSPORT_Toggle_RECORD_ARM_ON_OFF,
    ACT_TRANSPORT_JUMP_TO_TIME_POPUP,
    ACT_TRACK_MASTER_MUTE_ALL,
    ACT_TRACK_MASTER_SOLO_ALL,
    ACT_VIDEO_TOGGLE_GLOBAL_PIN,
    ACT_NAV_FRAME_RIGHT,
    ACT_NAV_FRAME_LEFT,
    ACT_NAV_5SEC_RIGHT,
    ACT_NAV_5SEC_LEFT,

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
static void daw_set_playhead_seconds(ProjectTab *p, double new_sec);
static float get_min_zoom_pps(ProjectTab *p, int view_w);
static void open_clip_properties_window(ProjectTab *p, int win_w, int win_h);
static void open_color_picker_dialog(int target_mode);
static double get_project_step_fps(ProjectTab *p);
static void toggle_master_mute_all_tracks(ProjectTab *p);
static void toggle_master_solo_all_tracks(ProjectTab *p);
static void toggle_video_global_pin_state(ProjectTab *p, SDL_Window *main_win);
static void open_time_jump_popup(ProjectTab *p, int win_w, int win_h);
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
    fprintf(f, "    \"universal_peak_color_idx\": %d,\n", g_universal_peak_color_idx);
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
    fprintf(f, "    \"export_two_pass\": %s,\n", g_export.two_pass_render ? "true" : "false");
    fprintf(f, "    \"universal_peak_color_idx\": %d,\n", g_universal_peak_color_idx);
    fprintf(f, "    \"mouse_click_empty_moves_playhead\": %s,\n", g_mouse_click_empty_moves_playhead ? "true" : "false");
    fprintf(f, "    \"mouse_click_universal_moves_playhead\": %s\n", g_mouse_click_universal_moves_playhead ? "true" : "false");
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
    fprintf(f, "  \"universal_peak_color_idx\": %d,\n", g_universal_peak_color_idx);
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
    int upk = json_extract_int_field(blk_start, blk_end, "universal_peak_color_idx", g_universal_peak_color_idx);
    if (upk >= 0 && upk < NUM_STUDIO_COLORS) g_universal_peak_color_idx = upk;

    // Always keep the 6 inbuilt themes locked at indices 0..5, and append any extra custom saved themes at >= 6
    g_theme_defaults_initialized = false;
    g_theme.num_saved = 0;
    init_default_saved_themes();

    const char *st_sec = strstr(blk_start, "\"saved_themes\"");
    if (st_sec && st_sec < blk_end) {
        const char *arr_o = strchr(st_sec, '[');
        const char *arr_c = arr_o ? strchr(arr_o, ']') : NULL;
        if (arr_o && arr_c && arr_c <= blk_end) {
            const char *cur = arr_o + 1;
            while (cur < arr_c && g_theme.num_saved < MAX_SAVED_THEMES) {
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
                    bool is_inbuilt = false;
                    for (int ib = 0; ib < NUM_INBUILT_THEMES; ib++) {
                        if (strcmp(g_theme.saved[ib].name, tname) == 0 ||
                            strcmp(tname, "Forest Green") == 0) {
                            is_inbuilt = true;
                            break;
                        }
                    }
                    if (!is_inbuilt) {
                        SavedThemeEntry *dst = &g_theme.saved[g_theme.num_saved++];
                        strncpy(dst->name, tname, sizeof(dst->name) - 1);
                        dst->name[sizeof(dst->name) - 1] = '\0';
                        dst->r = (uint8_t)(tr < 0 ? 0 : (tr > 255 ? 255 : tr));
                        dst->g = (uint8_t)(tg < 0 ? 0 : (tg > 255 ? 255 : tg));
                        dst->b = (uint8_t)(tb < 0 ? 0 : (tb > 255 ? 255 : tb));
                        dst->a = (uint8_t)(ta < 0 ? 0 : (ta > 255 ? 255 : ta));
                    }
                }
                cur = oe + 1;
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
        int app_upk = json_extract_int_field(app_sec, app_end, "universal_peak_color_idx", g_universal_peak_color_idx);
        if (app_upk >= 0 && app_upk < NUM_STUDIO_COLORS) g_universal_peak_color_idx = app_upk;
        g_mouse_click_empty_moves_playhead = json_extract_bool_field(app_sec, app_end, "mouse_click_empty_moves_playhead", g_mouse_click_empty_moves_playhead);
        g_mouse_click_universal_moves_playhead = json_extract_bool_field(app_sec, app_end, "mouse_click_universal_moves_playhead", g_mouse_click_universal_moves_playhead);
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
        "Edit: Clip color (Item: Set to custom color...)",
        "Ctrl+Alt+Shift+C", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_MOVE_UP_TRACK,
        "Item: Move selected clip(s) up one track",
        "8", "Num 8", NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_MOVE_DOWN_TRACK,
        "Item: Move selected clip(s) down one track",
        "2", "Num 2", NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_DYNAMIC_SPLIT,
        "Action: Dynamic split (Item: Dynamic split items...)",
        "Ctrl+D", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_JOIN_GLUE,
        "Edit: Join (Glue) selected items",
        "J", "Ctrl+J", NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_SHOW_PROPERTIES,
        "Tools: Clip properties",
        "Ctrl+I", "Ctrl+=", NULL, NULL, false, false);

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
        "Action: Reposition (Xenakios/SWS: Reposition selected items...)",
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
        "Transport: Play/Pause (near left of Time Clock)",
        "Space", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRANSPORT_RECORD,
        "Transport: Record (requires Record ON/OFF switch ON)",
        "R", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_NAV_FRAME_RIGHT,
        "View: Move cursor right one frame",
        "Right", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_NAV_FRAME_LEFT,
        "View: Move cursor left one frame",
        "Left", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_NAV_5SEC_RIGHT,
        "View: Move cursor right 5 seconds",
        "Ctrl+Right", NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_NAV_5SEC_LEFT,
        "View: Move cursor left 5 seconds",
        "Ctrl+Left", NULL, NULL, NULL, false, false);
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
        "Settings: Keyboard Shortcut (Show action list)",
        "Shift+?", NULL, NULL, NULL, false, false);

    // ---- ALL MENU, SUBMENU, BUTTON & TIMELINE ACTIONS (No default shortcut added unless already assigned; user can add manually) ----
    // File Menu
    register_builtin_action(ACT_FILE_OPEN_PROJECT,
        "File: Import Project (.ehs)",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_FILE_SAVE_PROJECT,
        "File: Export Project (.ehs) / Save Project",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_FILE_SAVE_PROJECT_AS,
        "File: Project Save As",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_FILE_IMPORT_MEDIA,
        "File: Import Media",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_FILE_EXPORT_MEDIA,
        "File: Export Media",
        NULL, NULL, NULL, NULL, false, false);

    // Action Menu
    register_builtin_action(ACT_ACTION_OPEN_SCRIPTS,
        "Action: Scripts (Script Manager)",
        NULL, NULL, NULL, NULL, false, false);

    // Tools Menu
    register_builtin_action(ACT_VIEW_TOGGLE_MARKER_LANE,
        "Tools: Marker (Toggle Marker Lane)",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_VIEW_SHOW_HISTORY_WINDOW,
        "Tools: History (Project History)",
        NULL, NULL, NULL, NULL, false, false);

    // Settings Menu (Keyboard Shortcut, Themes, Mouse)
    register_builtin_action(ACT_TOOLS_OPEN_THEMES,
        "Settings: Themes (ARGB Color Settings)",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_SETTINGS_OPEN_MOUSE,
        "Settings: Mouse (Playhead Click Behavior)",
        NULL, NULL, NULL, NULL, false, false);

    // Edit Menu
    register_builtin_action(ACT_EDIT_TRACK_COLOR,
        "Edit: Track color",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_EDIT_PEAKS_COLOR,
        "Edit: Peaks Color (Universal Software Peaks Color)",
        NULL, NULL, NULL, NULL, false, false);

    // Top Bar Record ON/OFF, Time Clock Jump, Video Global Pin, Master M/S
    register_builtin_action(ACT_TRANSPORT_Toggle_RECORD_ARM_ON_OFF,
        "Transport: Toggle Record ON/OFF Switch",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRANSPORT_JUMP_TO_TIME_POPUP,
        "Transport: Jump to Time Popup (Double-click Time Clock)",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_VIDEO_TOGGLE_GLOBAL_PIN,
        "Video: Toggle Pin Video Window Globally (Outside App)",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRACK_MASTER_MUTE_ALL,
        "Track: Master Mute All Tracks (M next to + Track)",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRACK_MASTER_SOLO_ALL,
        "Track: Master Solo All Tracks (S next to + Track)",
        NULL, NULL, NULL, NULL, false, false);

    // Top Bar & Corner Header Buttons
    register_builtin_action(ACT_VIEW_TOGGLE_VIDEO_WINDOW,
        "Video: Toggle Video Monitor",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRANSPORT_TOGGLE_REPEAT_RANGE,
        "Range: Toggle Cycle Range",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_TOGGLE_CROSSFADE_MODE,
        "Crossfade: Toggle Crossfade Mode",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_VIEW_SHOW_HIDE_TRACKS_WINDOW,
        "Hide: Hide Tracks Window",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRACK_INSERT_NEW,
        "Track: Add New Track (+ Track)",
        NULL, NULL, NULL, NULL, false, false);

    // Track Control Panel (TCP) Buttons & Track Actions
    register_builtin_action(ACT_TRACK_TOGGLE_MUTE_SELECTED,
        "Track: Toggle mute for selected tracks (M)",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRACK_NUDGE_VOL_UP,
        "Track: Nudge track volume +1dB (+)",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRACK_NUDGE_VOL_DOWN,
        "Track: Nudge track volume -1dB (-)",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRACK_RESET_VOL_0DB,
        "Track: Reset selected track volume to 0.0 dB",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRACK_DUPLICATE_SELECTED,
        "Track: Duplicate selected track",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRACK_REMOVE_SELECTED,
        "Track: Remove selected track(s)",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRACK_SELECT_ALL,
        "Track: Select all tracks",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TRACK_UNSELECT_ALL,
        "Track: Unselect all tracks",
        NULL, NULL, NULL, NULL, false, false);

    // Clip / Item Editing & Fade Actions
    register_builtin_action(ACT_ITEM_DUPLICATE_ITEMS,
        "Item: Duplicate selected items",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_FADE_IN_TO_CURSOR,
        "Item: Fade in to cursor",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_FADE_OUT_FROM_CURSOR,
        "Item: Fade out from cursor",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_RESET_FADES,
        "Item: Reset item fades to 0",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_NORMALIZE_GAIN,
        "Item: Normalize item gain",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_TRIM_LEFT_TO_CURSOR,
        "Item: Trim left edge to cursor",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_TRIM_RIGHT_TO_CURSOR,
        "Item: Trim right edge to cursor",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_SELECT_ALL_ON_TRACK,
        "Item: Select all items on selected track(s)",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_ITEM_UNSELECT_ALL,
        "Item: Unselect all items",
        NULL, NULL, NULL, NULL, false, false);

    // Marker Actions
    register_builtin_action(ACT_MARKER_INSERT_AT_CURSOR,
        "Markers: Insert marker at current position",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_MARKER_NEXT,
        "Markers: Go to next marker",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_MARKER_PREV,
        "Markers: Go to previous marker",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_MARKER_DELETE_NEAREST,
        "Markers: Delete selected or nearest marker",
        NULL, NULL, NULL, NULL, false, false);

    // Cycle Range / Loop Actions
    register_builtin_action(ACT_LOOP_SET_START_AT_CURSOR,
        "Range: Set cycle range start at cursor",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_LOOP_SET_END_AT_CURSOR,
        "Range: Set cycle range end at cursor",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_LOOP_SET_TO_SELECTED_ITEMS,
        "Range: Set cycle range to selected items",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_LOOP_CLEAR,
        "Range: Clear cycle range",
        NULL, NULL, NULL, NULL, false, false);

    // View & Zoom Actions
    register_builtin_action(ACT_VIEW_ZOOM_SELECTED_ITEMS,
        "View: Zoom to selected items",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_VIEW_ZOOM_OUT_PROJECT,
        "View: Zoom out to full project",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_VIEW_ZOOM_TIME_SELECTION,
        "View: Zoom to cycle range selection",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_VIEW_TRACK_ZOOM_DEFAULT,
        "View: Reset all track heights to default",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_VIEW_TRACK_ZOOM_MAX_OVER,
        "View: Toggle maximum track height",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_VIEW_TRACK_ZOOM_MIN,
        "View: Toggle minimum track height",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_VIEW_ALT_TCP_WIDTH,
        "View: Toggle Track Control Panel (TCP) width",
        NULL, NULL, NULL, NULL, false, false);

    // Project Tab Actions
    register_builtin_action(ACT_TAB_SWITCH_PREV,
        "Project tabs: Switch to previous project tab",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TAB_NEW_PROJECT_TAB,
        "Project tabs: New Project Tab (+)",
        NULL, NULL, NULL, NULL, false, false);
    register_builtin_action(ACT_TAB_CLOSE_CURRENT_TAB,
        "Project tabs: Close Current Tab",
        NULL, NULL, NULL, NULL, false, false);

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
    if (is_track_full_locked_tab(tab, it->track_idx)) return false;
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
        it->peaks = daw_register_shared_peaks(it->raw_audio_path, it->peaks, it->peak_count, it->file_total_duration, it->has_video);
        r_item->peaks = it->peaks;
    }

    it->duration = left_dur;
    r_item->start_time = split_t;
    r_item->offset_in_file = it->offset_in_file + left_dur;
    r_item->duration = right_dur;
    r_item->is_selected = true;
    it->is_selected = true;
    r_item->raw_file_handle = daw_fopen_LockFreeRead(r_item->raw_audio_path);

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
        if (!it->is_selected || is_track_full_locked_tab(tab, it->track_idx)) continue;
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
            it->peaks = daw_register_shared_peaks(it->raw_audio_path, it->peaks, it->peak_count, it->file_total_duration, it->has_video);
            r_item->peaks = it->peaks;
        }

        it->duration = half_dur;
        r_item->start_time = mid_t;
        r_item->offset_in_file = it->offset_in_file + half_dur;
        r_item->duration = half_dur;
        r_item->is_selected = true;
        it->is_selected = true;
        r_item->raw_file_handle = daw_fopen_LockFreeRead(r_item->raw_audio_path);

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
    int rep_tab = get_valid_tab_idx();
    for (int t = 0; t < p->num_tracks; t++) {
        if (is_track_lr_locked_tab(rep_tab, t)) continue;
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
            if (tr >= 0 && tr < p->num_tracks && p->track_selected[tr] && !is_track_hidden(tr) && !is_track_full_locked_tab(get_valid_tab_idx(), tr)) {
                p->items[i].is_selected = true;
                target_items[num_targets++] = i;
            }
        }
    } else {
        // Selected clip item (fallback to selected track's clips if no clip is explicitly selected)
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].is_selected && !is_track_hidden(p->items[i].track_idx) && !is_track_full_locked_tab(get_valid_tab_idx(), p->items[i].track_idx)) {
                target_items[num_targets++] = i;
            }
        }
        if (num_targets == 0) {
            for (int i = 0; i < p->num_items; i++) {
                int tr = p->items[i].track_idx;
                if (tr >= 0 && tr < p->num_tracks && p->track_selected[tr] && !is_track_hidden(tr) && !is_track_full_locked_tab(get_valid_tab_idx(), tr)) {
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
            if (src_copy.peak_count > 0 && src_copy.peaks) {
                dst->peaks = daw_register_shared_peaks(src_copy.raw_audio_path, src_copy.peaks, src_copy.peak_count, src_copy.file_total_duration, src_copy.has_video);
            } else {
                dst->peaks = NULL;
            }
            dst->raw_file_handle = daw_fopen_LockFreeRead(dst->raw_audio_path);
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
                daw_set_playhead_seconds(p, cpos);
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
        if (target_trk >= 0 && !is_track_ud_locked_tab(tab, target_trk)) {
            bool any_moved = false;
            push_undo_state();
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected && p->items[i].track_idx != target_trk &&
                    !is_track_ud_locked_tab(tab, p->items[i].track_idx)) {
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
            push_undo_state();
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
                float min_pps = get_min_zoom_pps(p, view_w);
                float new_pps = (float)((double)view_w / span);
                if (new_pps < min_pps) new_pps = min_pps;
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
            float min_pps = get_min_zoom_pps(p, view_w);
            p->pixels_per_second = fmaxf(min_pps, p->pixels_per_second * 0.80f);
            if (p->pixels_per_second <= min_pps * 1.02f) {
                p->scroll_x_sec = 0.0;
            }
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
            if (view_w > 100) {
                p->pixels_per_second = get_min_zoom_pps(p, view_w);
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
                    float min_pps = get_min_zoom_pps(p, view_w);
                    float new_pps = (float)((double)view_w / span);
                    if (new_pps < min_pps) new_pps = min_pps;
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
            if (p->video.is_open) {
                close_video(&p->video);
                p->video.is_open = false;
            } else {
                p->video.is_open = true;
            }
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
        case ACT_ITEM_SHOW_PROPERTIES: {
            open_clip_properties_window(p, win_w, win_h);
            break;
        }
        case ACT_ITEM_MOVE_UP_TRACK: {
            bool moved = false;
            for (int i = 0; i < p->num_items; i++) {
                if (!p->items[i].is_selected || is_track_full_locked_tab(tab, p->items[i].track_idx)) continue;
                int cur_t = p->items[i].track_idx;
                if (is_track_ud_locked_tab(tab, cur_t)) continue;
                int prev_t = -1;
                for (int t = cur_t - 1; t >= 0; t--) {
                    if (!is_track_hidden(t) && !is_track_ud_locked_tab(tab, t)) { prev_t = t; break; }
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
                if (!p->items[i].is_selected || is_track_full_locked_tab(tab, p->items[i].track_idx)) continue;
                int cur_t = p->items[i].track_idx;
                if (is_track_ud_locked_tab(tab, cur_t)) continue;
                int next_t = -1;
                for (int t = cur_t + 1; t < p->num_tracks; t++) {
                    if (!is_track_hidden(t) && !is_track_ud_locked_tab(tab, t)) { next_t = t; break; }
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
                    if (p->items[i].is_selected && !is_track_hidden(p->items[i].track_idx) && !is_track_full_locked_tab(get_valid_tab_idx(), p->items[i].track_idx)) {
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
                daw_set_playhead_seconds(p, p->items[best_idx].start_time);
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
                    if (p->items[i].is_selected && !is_track_hidden(p->items[i].track_idx) && !is_track_full_locked_tab(get_valid_tab_idx(), p->items[i].track_idx)) {
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
                daw_set_playhead_seconds(p, p->items[best_idx].start_time);
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
            if (target_s >= 0.0) daw_set_playhead_seconds(p, target_s);
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
            if (target_e >= 0.0) daw_set_playhead_seconds(p, target_e);
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
            if (best_edge >= 0.0) daw_set_playhead_seconds(p, best_edge);
            else daw_set_playhead_seconds(p, 0.0);
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
            if (best_edge < 1e17) daw_set_playhead_seconds(p, best_edge);
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
                    if (p->items[i].is_selected && !is_track_full_locked_tab(tab, p->items[i].track_idx)) {
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
            push_undo_state();
            bool any_item_sel = false;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected && !is_track_full_locked_tab(tab, p->items[i].track_idx)) {
                    g_item_muted[tab][i] = !g_item_muted[tab][i];
                    any_item_sel = true;
                }
            }
            if (any_item_sel) {
                record_project_change("Toggled Item Mute", true);
            } else {
                for (int t = 0; t < p->num_tracks; t++) {
                    if (p->track_selected[t] && !is_track_full_locked_tab(tab, t)) {
                        p->tracks[t].mute = !p->tracks[t].mute;
                    }
                }
                record_project_change("Toggled Track Mute", true);
            }
            break;
        }
        case ACT_ITEM_NUDGE_VOL_UP_1DB: {
            push_undo_state();
            bool any_item_sel = false;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected && !is_track_full_locked_tab(tab, p->items[i].track_idx)) {
                    g_item_vol_db[tab][i] = fminf(24.0f, g_item_vol_db[tab][i] + 1.0f);
                    any_item_sel = true;
                }
            }
            if (!any_item_sel) {
                for (int t = 0; t < p->num_tracks; t++) {
                    if (p->track_selected[t] && !is_track_full_locked_tab(tab, t)) p->track_db[t] = fminf(24.0f, p->track_db[t] + 1.0f);
                }
            }
            record_project_change("Item: Nudge volume +1dB", true);
            break;
        }
        case ACT_ITEM_NUDGE_VOL_DOWN_1DB: {
            push_undo_state();
            bool any_item_sel = false;
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected && !is_track_full_locked_tab(tab, p->items[i].track_idx)) {
                    g_item_vol_db[tab][i] = fmaxf(-60.0f, g_item_vol_db[tab][i] - 1.0f);
                    any_item_sel = true;
                }
            }
            if (!any_item_sel) {
                for (int t = 0; t < p->num_tracks; t++) {
                    if (p->track_selected[t] && !is_track_full_locked_tab(tab, t)) p->track_db[t] = fmaxf(-60.0f, p->track_db[t] - 1.0f);
                }
            }
            record_project_change("Item: Nudge volume -1dB", true);
            break;
        }
        case ACT_EDIT_TRACK_COLOR: {
            open_color_picker_dialog(0);
            break;
        }
        case ACT_ITEM_SET_CUSTOM_COLOR: {
            open_color_picker_dialog(1);
            break;
        }
        case ACT_EDIT_PEAKS_COLOR: {
            open_color_picker_dialog(2);
            break;
        }
        case ACT_ACTION_OPEN_SCRIPTS: {
            scan_scripts_directory_to_actions(&g_daw);
            save_keyboard_shortcuts_json();
            refresh_filtered_scripts();
            g_daw.script_manager_open = !g_daw.script_manager_open;
            break;
        }
        case ACT_TOOLS_OPEN_THEMES: {
            init_default_saved_themes();
            sync_theme_code_string();
            g_theme.is_open = !g_theme.is_open;
            if (g_theme.is_open) {
                g_theme.win_rect.x = (win_w - g_theme.win_rect.w) / 2;
                g_theme.win_rect.y = (win_h - g_theme.win_rect.h) / 2;
            }
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
                if (is_track_full_locked_tab(tab, it->track_idx)) continue;
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
                        it->peaks = daw_register_shared_peaks(it->raw_audio_path, it->peaks, it->peak_count, it->file_total_duration, it->has_video);
                        r_item->peaks = it->peaks;
                    }
                    double delta = cur_t - it_s;
                    it->duration = delta;
                    r_item->start_time = cur_t;
                    r_item->offset_in_file = it->offset_in_file + delta;
                    r_item->duration = it_e - cur_t;
                    r_item->is_selected = true;
                    it->is_selected = false;
                    r_item->raw_file_handle = daw_fopen_LockFreeRead(r_item->raw_audio_path);
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
                        src->peaks = daw_register_shared_peaks(src->raw_audio_path, src->peaks, src->peak_count, src->file_total_duration, src->has_video);
                        dst->peaks = src->peaks;
                    }
                    dst->raw_file_handle = daw_fopen_LockFreeRead(dst->raw_audio_path);
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
            push_undo_state();
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected && !is_track_full_locked_tab(tab, p->items[i].track_idx) && p->playhead_seconds > p->items[i].start_time) {
                    set_item_fade_in(p, i, p->playhead_seconds - p->items[i].start_time);
                }
            }
            record_project_change("Fade In To Cursor", true);
            break;
        }
        case ACT_ITEM_FADE_OUT_FROM_CURSOR: {
            push_undo_state();
            for (int i = 0; i < p->num_items; i++) {
                double ie = p->items[i].start_time + p->items[i].duration;
                if (p->items[i].is_selected && !is_track_full_locked_tab(tab, p->items[i].track_idx) && p->playhead_seconds < ie) {
                    set_item_fade_out(p, i, ie - p->playhead_seconds);
                }
            }
            record_project_change("Fade Out From Cursor", true);
            break;
        }
        case ACT_ITEM_RESET_FADES: {
            push_undo_state();
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected && !is_track_full_locked_tab(tab, p->items[i].track_idx)) {
                    set_item_fade_in(p, i, 0.0);
                    set_item_fade_out(p, i, 0.0);
                }
            }
            record_project_change("Removed Clip Fades", true);
            break;
        }
        case ACT_ITEM_NORMALIZE_GAIN: {
            push_undo_state();
            for (int i = 0; i < p->num_items; i++) {
                if (!p->items[i].is_selected || is_track_full_locked_tab(tab, p->items[i].track_idx)) continue;
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
                if (it->is_selected && !is_track_full_locked_tab(tab, it->track_idx) && p->playhead_seconds > it->start_time && p->playhead_seconds < ie - 0.05) {
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
                if (it->is_selected && !is_track_full_locked_tab(tab, it->track_idx) && p->playhead_seconds > it->start_time + 0.05 && p->playhead_seconds < ie) {
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
            push_undo_state();
            bool any_sel = false;
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t] && !is_track_full_locked_tab(tab, t)) {
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
            push_undo_state();
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t] && !is_track_full_locked_tab(tab, t)) p->tracks[t].mute = !p->tracks[t].mute;
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
            push_undo_state();
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t] && !is_track_full_locked_tab(tab, t)) p->track_db[t] = fminf(24.0f, p->track_db[t] + 1.0f);
            }
            record_project_change("Track Volume +1dB", true);
            break;
        }
        case ACT_TRACK_NUDGE_VOL_DOWN: {
            push_undo_state();
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t] && !is_track_full_locked_tab(tab, t)) p->track_db[t] = fmaxf(-60.0f, p->track_db[t] - 1.0f);
            }
            record_project_change("Track Volume -1dB", true);
            break;
        }
        case ACT_TRACK_RESET_VOL_0DB: {
            push_undo_state();
            for (int t = 0; t < p->num_tracks; t++) {
                if (p->track_selected[t] && !is_track_full_locked_tab(tab, t)) p->track_db[t] = 0.0f;
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
            if (g_rec_master_enabled) {
                start_or_stop_recording(p, ren, win);
            } else {
                record_project_change("Record is OFF (Turn ON switch next to Record)", false);
            }
            break;
        }
        case ACT_TRANSPORT_Toggle_RECORD_ARM_ON_OFF: {
            g_rec_master_enabled = !g_rec_master_enabled;
            if (!g_rec_master_enabled && g_rec.is_recording) {
                start_or_stop_recording(p, ren, win);
            }
            record_project_change(g_rec_master_enabled ? "Record Switch: ON" : "Record Switch: OFF", false);
            break;
        }
        case ACT_TRANSPORT_JUMP_TO_TIME_POPUP: {
            open_time_jump_popup(p, win_w, win_h);
            break;
        }
        case ACT_SETTINGS_OPEN_MOUSE: {
            g_mouse_settings_open = !g_mouse_settings_open;
            if (g_mouse_settings_open) {
                g_mouse_win_rect.x = (win_w - g_mouse_win_rect.w) / 2;
                g_mouse_win_rect.y = (win_h - g_mouse_win_rect.h) / 2;
            }
            break;
        }
        case ACT_VIDEO_TOGGLE_GLOBAL_PIN: {
            toggle_video_global_pin_state(p, win);
            break;
        }
        case ACT_TRACK_MASTER_MUTE_ALL: {
            toggle_master_mute_all_tracks(p);
            break;
        }
        case ACT_TRACK_MASTER_SOLO_ALL: {
            toggle_master_solo_all_tracks(p);
            break;
        }
        case ACT_NAV_FRAME_RIGHT: {
            double fps = get_project_step_fps(p);
            daw_set_playhead_seconds(p, p->playhead_seconds + (1.0 / fps));
            ensure_playhead_visible_in_viewport(p, view_w);
            break;
        }
        case ACT_NAV_FRAME_LEFT: {
            double fps = get_project_step_fps(p);
            daw_set_playhead_seconds(p, fmax(0.0, p->playhead_seconds - (1.0 / fps)));
            ensure_playhead_visible_in_viewport(p, view_w);
            break;
        }
        case ACT_NAV_5SEC_RIGHT: {
            daw_set_playhead_seconds(p, p->playhead_seconds + 5.0);
            ensure_playhead_visible_in_viewport(p, view_w);
            break;
        }
        case ACT_NAV_5SEC_LEFT: {
            daw_set_playhead_seconds(p, fmax(0.0, p->playhead_seconds - 5.0));
            ensure_playhead_visible_in_viewport(p, view_w);
            break;
        }
        case ACT_TRANSPORT_GO_TO_START: {
            daw_set_playhead_seconds(p, 0.0);
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
            daw_set_playhead_seconds(p, max_e);
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
                    if (p->items[i].is_selected && !is_track_full_locked_tab(tab, p->items[i].track_idx)) {
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
            if (best_t < 1e17) daw_set_playhead_seconds(p, best_t);
            break;
        }
        case ACT_MARKER_PREV: {
            double best_t = -1.0;
            for (int m = 0; m < p->num_markers; m++) {
                if (p->markers[m].time_sec < p->playhead_seconds - 0.001 && p->markers[m].time_sec > best_t) {
                    best_t = p->markers[m].time_sec;
                }
            }
            if (best_t >= 0.0) daw_set_playhead_seconds(p, best_t);
            else daw_set_playhead_seconds(p, 0.0);
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
            g_lock_popup_open = false;
            g_time_jump_open = false;
            g_mouse_settings_open = false;
            g_export.is_open = true;
            g_export.win_rect.x = (win_w - g_export.win_rect.w) / 2;
            g_export.win_rect.y = (win_h - g_export.win_rect.h) / 2;
            g_export.is_rendering = false;
            g_export.progress_pct = 0;
            g_export.elapsed_sec = 0.0;
            g_export.est_remaining_sec = 0.0;
            g_export.mp4_aac_dropdown_open = false;
            g_export.mp4_size_dropdown_open = false;
            g_export.overwrite_prompt_open = false;
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

    // 3. Filter text box typing focus (supports Ctrl+A/C/X/V, Left/Right/Home/End, Backspace/Delete)
    if (g_action_win.filter_focused) {
        if (sym == SDLK_ESCAPE || sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
            g_action_win.filter_focused = false;
            SDL_StopTextInput();
            return true;
        }
        if (daw_text_edit_handle_keydown(g_action_win.filter_text, sizeof(g_action_win.filter_text) - 1, sym, mod)) {
            g_action_win.scroll_y = 0;
            refresh_filtered_actions();
            if (g_action_win.num_filtered > 0) {
                g_action_win.selected_action_idx = g_action_win.filtered_indices[0];
                g_action_win.selected_shortcut_sub = 0;
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

    // 2. Top Filter & Pin Row (wy + 38 .. wy + 64) - Up/Down options removed
    if (my >= wy + 38 && my <= wy + 64) {
        if (mx >= wx + 58 && mx <= wx + 366) {
            g_action_win.filter_focused = true;
            daw_text_edit_click_cursor(g_action_win.filter_text, mx, wx + 64);
            SDL_StartTextInput();
        } else if (mx >= wx + 372 && mx <= wx + 434) {
            // [Clear]
            g_action_win.filter_text[0] = '\0';
            g_action_win.scroll_y = 0;
            refresh_filtered_actions();
        } else if (mx >= wx + 440 && mx <= wx + 508) {
            // [Pin] -> Toggle Pin on selected action (brings to top & saves permanently)
            toggle_pin_selected_action();
        } else if (mx >= wx + 514 && mx <= wx + 592) {
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

    SDL_Rect flt_box = { wx + 58, wy + 38, 308, 26 };
    SDL_SetRenderDrawColor(ren, 16, 15, 14, 255);
    SDL_RenderFillRect(ren, &flt_box);
    draw_brown_border_rect(ren, &flt_box, g_action_win.filter_focused, false);

    SDL_Rect flt_clip = { flt_box.x + 4, flt_box.y + 2, flt_box.w - 8, flt_box.h - 4 };
    if (g_action_win.filter_focused) {
        daw_draw_text_edit_overlay(ren, g_action_win.filter_text, flt_box.x + 6, flt_box.y, flt_box.h, flt_box.x + flt_box.w - 4);
    }
    draw_text_clipped(ren, g_action_win.filter_text, flt_box.x + 6, wy + 45, c_white, &flt_clip, NULL);

    SDL_Rect btn_clear = { wx + 372, wy + 38, 62, 26 };
    SDL_SetRenderDrawColor(ren, 44, 38, 34, 255);
    SDL_RenderFillRect(ren, &btn_clear);
    draw_brown_border_rect(ren, &btn_clear, false, false);
    draw_text(ren, "Clear", wx + 383, wy + 45, c_white);

    bool cur_pinned = (g_action_win.selected_action_idx >= 0 && g_action_win.selected_action_idx < g_num_actions &&
                       g_actions[g_action_win.selected_action_idx].is_pinned);
    SDL_Rect btn_pin = { wx + 440, wy + 38, 68, 26 };
    SDL_SetRenderDrawColor(ren, cur_pinned ? 74 : 44, cur_pinned ? 52 : 38, cur_pinned ? 30 : 34, 255);
    SDL_RenderFillRect(ren, &btn_pin);
    draw_brown_border_rect(ren, &btn_pin, cur_pinned, false);
    draw_pin_symbol_icon(ren, wx + 448, wy + 44, cur_pinned, cur_pinned ? c_accent : c_white);
    draw_text(ren, "Pin", wx + 464, wy + 45, c_white);

    SDL_Rect btn_opt = { wx + 514, wy + 38, 78, 26 };
    SDL_SetRenderDrawColor(ren, 44, 38, 34, 255);
    SDL_RenderFillRect(ren, &btn_opt);
    draw_brown_border_rect(ren, &btn_opt, false, false);
    draw_text(ren, "Refresh", wx + 525, wy + 45, c_white);

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
