#define DAW_COMPILING_MAIN_C 1
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

#include "font.h"
#ifndef DAW_FAST_FONT_ATLAS_DEFINED
#define DAW_FAST_FONT_ATLAS_DEFINED
static SDL_Renderer *g_daw_font_atlas_ren = NULL;
static SDL_Texture  *g_daw_font_atlas_tex = NULL;

static inline void daw_ensure_font_atlas_tex(SDL_Renderer *ren) {
    if (!ren) return;
    if (g_daw_font_atlas_ren == ren && g_daw_font_atlas_tex != NULL) return;
    static uint32_t atlas_px[95 * 8 * 12];
    static bool bitmap_built = false;
    if (!bitmap_built) {
        for (int i = 0; i < 95 * 8 * 12; i++) atlas_px[i] = 0x00000000U;
        for (int g = 0; g < 95; g++) {
            for (int row = 0; row < 12; row++) {
                unsigned short bits = FONT_BITMAP[g][row];
                for (int col = 0; col < 8; col++) {
                    if (bits & (0x80 >> col)) {
                        atlas_px[row * (95 * 8) + g * 8 + col] = 0xFFFFFFFFU;
                    }
                }
            }
        }
        bitmap_built = true;
    }
    if (g_daw_font_atlas_ren == NULL) {
        SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(
            atlas_px, 95 * 8, 12, 32, (95 * 8) * 4,
            0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0xFF000000U
        );
        if (surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
            SDL_FreeSurface(surf);
            if (tex) {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                g_daw_font_atlas_ren = ren;
                g_daw_font_atlas_tex = tex;
            }
        }
    }
}

static inline void daw_fast_draw_text(SDL_Renderer *ren, const char *str, int x, int y, SDL_Color color) {
    if (!str || !ren) return;
    if (!g_daw_font_atlas_tex) {
        daw_ensure_font_atlas_tex(ren);
    }
    if (g_daw_font_atlas_tex && g_daw_font_atlas_ren == ren) {
        SDL_SetTextureColorMod(g_daw_font_atlas_tex, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(g_daw_font_atlas_tex, color.a);
        int cur_x = x;
        while (*str) {
            unsigned char ch = (unsigned char)*str++;
            if (ch > 32 && ch <= 126) {
                SDL_Rect src_r = { ((int)ch - 32) * 8, 0, 8, 12 };
                SDL_Rect dst_r = { cur_x, y, 8, 12 };
                SDL_RenderCopy(ren, g_daw_font_atlas_tex, &src_r, &dst_r);
            }
            cur_x += 8;
        }
        return;
    }
    draw_text(ren, str, x, y, color);
}
#define draw_text daw_fast_draw_text
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
#include "video_player.h"
#include "audio_engine.h"
#include "daw_types.h"
#include "daw_tabs.h"
#include "project_io.h"

#ifndef SDL_TEXTUREACCESS_TARGET
#define SDL_TEXTUREACCESS_TARGET 2
#endif
#ifdef _WIN32
#ifndef WM_SETICON
#define WM_SETICON 0x0080
#endif
#ifndef ICON_SMALL
#define ICON_SMALL 0
#endif
#ifndef ICON_BIG
#define ICON_BIG 1
#endif
#ifndef IMAGE_ICON
#define IMAGE_ICON 1
#endif
#ifndef LR_DEFAULTCOLOR
#define LR_DEFAULTCOLOR 0x00000000
#endif
#ifndef LR_LOADFROMFILE
#define LR_LOADFROMFILE 0x00000010
#endif
#ifndef GCLP_HICON
#define GCLP_HICON (-14)
#endif
#ifndef GCLP_HICONSM
#define GCLP_HICONSM (-34)
#endif
#endif


#ifndef OFN_ALLOWMULTISELECT
#define OFN_ALLOWMULTISELECT 0x00000200
#endif
#ifndef OFN_EXPLORER
#define OFN_EXPLORER 0x00080000
#endif
#ifdef _WIN32
#ifndef MOVEFILE_REPLACE_EXISTING
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#endif
#ifndef MOVEFILE_COPY_ALLOWED
#define MOVEFILE_COPY_ALLOWED 0x00000002
#endif
#else
#include <dirent.h>
#include <unistd.h>
#endif

DawEngine g_daw;
SharedPeakPoolEntry g_shared_peak_pool[MAX_SHARED_PEAK_ENTRIES];

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
static int    g_track_color_idx[MAX_TABS][MAX_TRACKS];
static int    g_item_peak_color_idx[MAX_TABS][MAX_ITEMS];
static int    g_track_peak_color_idx[MAX_TABS][MAX_TRACKS];
static int    g_tab_peak_color_idx[MAX_TABS];
static int    g_item_group_id[MAX_TABS][MAX_ITEMS];
static int    g_next_item_group_id = 1;
static double g_drag_group_orig_start[MAX_ITEMS];
static int    g_drag_item_orig_track[MAX_ITEMS];
static volatile uint32_t g_playhead_seek_serial[MAX_TABS] = { 0 };
static double g_last_observed_playhead[MAX_TABS] = { -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0 };

#define NUM_STUDIO_COLORS 24
static const char* __attribute__((unused)) g_studio_color_names[NUM_STUDIO_COLORS] = {
    "Default Theme", "Copper Orange", "Emerald Teal", "Crimson Red",
    "Royal Blue",    "Amber Gold",    "Violet Purple","Cyber Cyan",
    "Forest Green",  "Coral Rose",    "Indigo Slate", "Chartreuse",
    "Magenta Pink",  "Turquoise",     "Sunset Peach", "Lavender",
    "Olive Moss",    "Ruby Wine",     "Sky Azure",    "Mint Leaf",
    "Sand Ochre",    "Plum Berry",    "Electric Teal","Silver Ice"
};
static const uint8_t g_studio_pal_bg[NUM_STUDIO_COLORS][3] = {
    { 36, 32, 28 },  { 118, 68, 32 }, { 28, 102, 86 }, { 122, 40, 50 },
    { 40, 72, 128 }, { 118, 94, 30 }, { 92, 48, 118 }, { 30, 96, 118 },
    { 40, 104, 48 }, { 124, 52, 72 }, { 58, 56, 118 }, { 82, 110, 34 },
    { 122, 38, 102 },{ 28, 108, 110 },{ 128, 64, 44 }, { 82, 66, 122 },
    { 86, 94, 38 },  { 112, 34, 58 }, { 38, 82, 132 }, { 38, 110, 76 },
    { 112, 86, 48 }, { 104, 42, 90 }, { 26, 108, 122 },{ 76, 82, 92 }
};
static const uint8_t g_studio_pal_hdr[NUM_STUDIO_COLORS][3] = {
    { 138, 144, 152 }, { 235, 142,  64 }, {  52, 208, 172 }, { 232,  76,  92 },
    {  82, 146, 235 }, { 235, 190,  58 }, { 184, 102, 235 }, {  58, 204, 232 },
    {  86, 210,  96 }, { 235, 104, 140 }, { 124, 120, 235 }, { 166, 218,  62 },
    { 232,  88, 196 }, {  48, 214, 208 }, { 235, 128,  88 }, { 162, 136, 235 },
    { 180, 194,  74 }, { 222,  68, 108 }, {  90, 168, 235 }, {  74, 218, 144 },
    { 214, 172, 104 }, { 208,  96, 182 }, {  44, 210, 232 }, { 196, 204, 216 }
};
static const uint8_t g_studio_pal_peak[NUM_STUDIO_COLORS][3] = {
    { 196, 202, 210 }, { 255, 168,  88 }, {  72, 238, 198 }, { 255,  98, 112 },
    { 102, 168, 255 }, { 255, 214,  78 }, { 208, 128, 255 }, {  78, 232, 255 },
    { 108, 238, 118 }, { 255, 128, 164 }, { 148, 144, 255 }, { 192, 245,  82 },
    { 255, 110, 220 }, {  64, 242, 235 }, { 255, 152, 112 }, { 188, 162, 255 },
    { 205, 218,  96 }, { 245,  88, 128 }, { 112, 192, 255 }, {  96, 245, 168 },
    { 238, 196, 128 }, { 232, 118, 205 }, {  58, 238, 255 }, { 224, 232, 242 }
};
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
    int peak_color_idx;
    int group_id;
} ClipboardItemEntry;

static ClipboardItemEntry g_clipboard[MAX_ITEMS];
static int g_clipboard_count = 0;

// Per-tab Track Hide state & Hide Tracks Popup Window state (with scrolling)
static bool     g_track_hidden[MAX_TABS][MAX_TRACKS];
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
static bool     g_menu_edit_open = false;
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
    EXPORT_MP4_AUDIO_AAC = 0
} ExportMp4AudioCodec;

#define NUM_MP4_RES_PRESETS 6
typedef struct {
    const char *label;
    int width;
    int height;
} Mp4ResolutionPreset;

static const Mp4ResolutionPreset g_mp4_res_presets[NUM_MP4_RES_PRESETS] = {
    { "640 x 360",              640,  360 },
    { "854 x 480",              854,  480 },
    { "1280 x 720",            1280,  720 },
    { "1920 x 1080 (Full HD)", 1920, 1080 },
    { "2560 x 1440 (2K)",      2560, 1440 },
    { "3840 x 2160 (4K)",      3840, 2160 }
};

#define NUM_MP4_V_KBPS_PRESETS 6
static const int g_mp4_v_kbps_presets[NUM_MP4_V_KBPS_PRESETS] = {
    1024, 2048, 4096, 8000, 12000, 20000
};

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

    // MP4 specific settings (matching studio layout)
    char mp4_width_str[16];         // Default "1920"
    char mp4_height_str[16];        // Default "1080"
    char mp4_fps_str[16];           // Default "25.00"
    char mp4_video_kbps_str[16];    // Default "2048"
    ExportMp4AudioCodec mp4_audio_codec; // AAC only
    char mp4_audio_kbps_str[16];    // "120", "192", or "320"
    bool mp4_aac_dropdown_open;     // Dropdown for 120 / 192 / 320 kbps
    int  mp4_res_preset_idx;        // Default 3 -> 1920 x 1080 (Full HD)
    bool mp4_size_dropdown_open;    // Resolution Size dropdown menu open
    bool mp4_size_manual_mode;      // True when "Manual" selected in Size dropdown
    bool mp4_v_kbps_dropdown_open;  // Video Bitrate dropdown menu open
    bool mp4_v_kbps_manual_mode;    // True when "Manual" selected in Video Bitrate dropdown

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

    // "File Already Exists" modal prompt inside Export Media workflow
    bool overwrite_prompt_open;
    char pending_conflict_path[1024];
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
    3,
    false,
    false,
    false,
    false,
    false,
    false,
    0,
    0.0,
    0.0,
    0,
    0,
    EXP_FIELD_NONE,
    "",
    false,
    ""
};

static SDL_Texture *g_active_tl_cache_tex = NULL;

static void apply_mp4_res_preset(int idx) {
    if (idx < 0 || idx >= NUM_MP4_RES_PRESETS) idx = 3;
    g_export.mp4_res_preset_idx = idx;
    g_export.mp4_size_manual_mode = false;
    snprintf(g_export.mp4_width_str, sizeof(g_export.mp4_width_str), "%d", g_mp4_res_presets[idx].width);
    snprintf(g_export.mp4_height_str, sizeof(g_export.mp4_height_str), "%d", g_mp4_res_presets[idx].height);
}

static int get_current_mp4_res_preset_idx(void) {
    if (g_export.mp4_size_manual_mode) return -1;
    int w = atoi(g_export.mp4_width_str);
    int h = atoi(g_export.mp4_height_str);
    for (int i = 0; i < NUM_MP4_RES_PRESETS; i++) {
        if (g_mp4_res_presets[i].width == w && g_mp4_res_presets[i].height == h) {
            g_export.mp4_res_preset_idx = i;
            return i;
        }
    }
    return -1;
}

static int get_current_mp4_v_kbps_preset_idx(void) {
    if (g_export.mp4_v_kbps_manual_mode) return -1;
    int vk = atoi(g_export.mp4_video_kbps_str);
    for (int i = 0; i < NUM_MP4_V_KBPS_PRESETS; i++) {
        if (g_mp4_v_kbps_presets[i] == vk) return i;
    }
    return -1;
}

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
// Tiny Range Timings Popup Window state (near top-bar Range button)
typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;
    int active_field; // 0 = none, 1 = start_buf, 2 = end_buf
    char start_buf[32];
    char end_buf[32];
} RangePopupState;

static RangePopupState g_range_popup = {
    false,
    { 940, 76, 260, 128 },
    false, 0, 0,
    0,
    "00:00:00.000",
    "00:00:05.000"
};

static inline void sync_range_popup_buffers_from_tab(void) {
    if (g_range_popup.active_field != 1) {
        format_timecode(get_range_start(), g_range_popup.start_buf, sizeof(g_range_popup.start_buf));
    }
    if (g_range_popup.active_field != 2) {
        format_timecode(get_range_end(), g_range_popup.end_buf, sizeof(g_range_popup.end_buf));
    }
}

// App Hearing Volume (Monitor-Only Level, 0..100%) per-tab & popup state
static float g_app_master_vol[MAX_TABS];
static float g_app_track_vol[MAX_TABS][MAX_TRACKS];
static bool  g_app_vol_initialized = false;

static inline void init_tab_app_volume_defaults(int tab) {
    if (tab < 0 || tab >= MAX_TABS) return;
    g_app_master_vol[tab] = 100.0f;
    for (int t = 0; t < MAX_TRACKS; t++) {
        g_app_track_vol[tab][t] = 100.0f;
    }
}

static inline void ensure_app_vol_defaults_initialized(void) {
    if (g_app_vol_initialized) return;
    g_app_vol_initialized = true;
    for (int tb = 0; tb < MAX_TABS; tb++) {
        init_tab_app_volume_defaults(tb);
    }
}

typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;
    int dragging_slider_idx; // -1 = none, 0 = master, 1..MAX_TRACKS = track + 1
    int scroll_y;
    bool sb_dragging;
    int sb_start_my;
    int sb_orig_scroll;
} AppVolumePopupState;

static AppVolumePopupState g_vol_popup = {
    false,
    { 360, 76, 480, 320 },
    false, 0, 0,
    -1,
    0, false, 0, 0
};

// Per-tab New Project Setup modal state & Per-track Infinity -dB state
static bool  g_tab_needs_project_setup[MAX_TABS] = { false };
static bool  g_track_inf_db[MAX_TABS][MAX_TRACKS] = { { false } };
static float g_track_pre_inf_db[MAX_TABS][MAX_TRACKS] = { { 0.0f } };

// Project Auto Save state & Crash-Recovery Guard state
typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;
    bool enabled;
    int interval_minutes;
    char interval_buf[16];
    bool interval_focused;
    Uint32 last_save_ticks;
    bool pending_autosave;
    char status_msg[128];
} AutoSaveState;

static AutoSaveState g_autosave = {
    false,
    { 400, 170, 480, 244 },
    false, 0, 0,
    true,
    30,
    "30",
    false,
    0,
    false,
    ""
};

static bool          g_autosave_in_progress = false;
static SDL_Window   *g_main_win = NULL;
static SDL_Renderer *g_main_ren = NULL;
static jmp_buf       g_crash_recovery_jmp;
static volatile sig_atomic_t g_crash_jmp_armed = 0;
static volatile sig_atomic_t g_fault_recovered_flag = 0;

static void daw_fault_signal_handler(int sig) {
    signal(sig, daw_fault_signal_handler);
    if (g_crash_jmp_armed) {
        g_crash_jmp_armed = 0;
        g_fault_recovered_flag = sig;
        longjmp(g_crash_recovery_jmp, 1);
    }
}

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

    if (g_tab_range_active[idx]) {
        g_range_popup.is_open = true;
        g_range_popup.active_field = 0;
        sync_range_popup_buffers_from_tab();
    } else {
        g_range_popup.is_open = false;
        g_range_popup.active_field = 0;
        SDL_StopTextInput();
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

    int tmp_col = g_track_color_idx[tab_idx][t1];
    g_track_color_idx[tab_idx][t1] = g_track_color_idx[tab_idx][t2];
    g_track_color_idx[tab_idx][t2] = tmp_col;

    int tmp_pcol = g_track_peak_color_idx[tab_idx][t1];
    g_track_peak_color_idx[tab_idx][t1] = g_track_peak_color_idx[tab_idx][t2];
    g_track_peak_color_idx[tab_idx][t2] = tmp_pcol;

    bool tmp_lck = g_track_locked[tab_idx][t1];
    g_track_locked[tab_idx][t1] = g_track_locked[tab_idx][t2];
    g_track_locked[tab_idx][t2] = tmp_lck;
    bool tmp_lck_lr = g_track_lock_lr_only[tab_idx][t1];
    g_track_lock_lr_only[tab_idx][t1] = g_track_lock_lr_only[tab_idx][t2];
    g_track_lock_lr_only[tab_idx][t2] = tmp_lck_lr;
    bool tmp_lck_ud = g_track_lock_ud_only[tab_idx][t1];
    g_track_lock_ud_only[tab_idx][t1] = g_track_lock_ud_only[tab_idx][t2];
    g_track_lock_ud_only[tab_idx][t2] = tmp_lck_ud;

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

// ================= BULLETPROOF DEEP UNDO / REDO ENGINE (100% AUDIO, PEAK & METADATA RECOVERY) =================
#define MAX_DEEP_UNDO 32

typedef struct {
    int num_items;
    int num_tracks;
    AudioItem *items;
    double *fade_in;
    double *fade_out;
    float  *vol_db;
    bool   *muted;
    int    *color_idx;
    int    *peak_color_idx;
    int    *group_id;

    Track  tracks[MAX_TRACKS];
    char   track_names[MAX_TRACKS][64];
    float  track_db[MAX_TRACKS];
    int    track_heights[MAX_TRACKS];
    bool   track_selected[MAX_TRACKS];
    bool   track_hidden[MAX_TRACKS];
    int    track_color_idx[MAX_TRACKS];
    int    track_peak_color_idx[MAX_TRACKS];
    bool   track_locked[MAX_TRACKS];
    bool   track_lock_lr_only[MAX_TRACKS];
    bool   track_lock_ud_only[MAX_TRACKS];
    bool   lock_opt_full;
    bool   lock_opt_complete_unlock;
    bool   lock_opt_prevent_lr;
    bool   lock_opt_prevent_ud;
    int    tab_peak_color_idx;

    bool   show_marker_lane;
    bool   tab_crossfade;
    bool   range_active;
    double range_start;
    double range_end;

    int       num_markers;
    DawMarker markers[MAX_MARKERS];
    char      marker_full_text[MAX_MARKERS][MARKER_TEXT_MAX];
} DeepUndoEntry;

static DeepUndoEntry *g_deep_undo_stack[MAX_TABS] = { NULL };
static int g_deep_undo_head[MAX_TABS]  = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int g_deep_undo_count[MAX_TABS] = {  0,  0,  0,  0,  0,  0,  0,  0 };
static bool g_deep_undo_has_redo_tip[MAX_TABS] = { false };

static void free_deep_undo_entry_peaks(DeepUndoEntry *ent) {
    if (!ent) return;
    if (ent->items)          { free(ent->items);          ent->items = NULL; }
    if (ent->fade_in)        { free(ent->fade_in);        ent->fade_in = NULL; }
    if (ent->fade_out)       { free(ent->fade_out);       ent->fade_out = NULL; }
    if (ent->vol_db)         { free(ent->vol_db);         ent->vol_db = NULL; }
    if (ent->muted)          { free(ent->muted);          ent->muted = NULL; }
    if (ent->color_idx)      { free(ent->color_idx);      ent->color_idx = NULL; }
    if (ent->peak_color_idx) { free(ent->peak_color_idx); ent->peak_color_idx = NULL; }
    if (ent->group_id)       { free(ent->group_id);       ent->group_id = NULL; }
    ent->num_items = 0;
}

static void reset_tab_undo_history(int tab) {
    if (tab < 0 || tab >= MAX_TABS) return;
    if (g_deep_undo_stack[tab]) {
        for (int k = 0; k < MAX_DEEP_UNDO; k++) {
            free_deep_undo_entry_peaks(&g_deep_undo_stack[tab][k]);
        }
    }
    g_deep_undo_head[tab] = -1;
    g_deep_undo_count[tab] = 0;
    g_deep_undo_has_redo_tip[tab] = false;
}

static void capture_tab_into_deep_entry(ProjectTab *p, int tab, DeepUndoEntry *ent) {
    if (!p || !ent) return;
    free_deep_undo_entry_peaks(ent);
    memset(ent, 0, sizeof(DeepUndoEntry));

    ent->num_items  = p->num_items;
    ent->num_tracks = p->num_tracks;
    if (p->num_items > 0) {
        size_t n_it = (size_t)p->num_items;
        ent->items          = (AudioItem *)calloc(n_it, sizeof(AudioItem));
        ent->fade_in        = (double *)calloc(n_it, sizeof(double));
        ent->fade_out       = (double *)calloc(n_it, sizeof(double));
        ent->vol_db         = (float *)calloc(n_it, sizeof(float));
        ent->muted          = (bool *)calloc(n_it, sizeof(bool));
        ent->color_idx      = (int *)calloc(n_it, sizeof(int));
        ent->peak_color_idx = (int *)calloc(n_it, sizeof(int));
        ent->group_id       = (int *)calloc(n_it, sizeof(int));
        if (!ent->items || !ent->fade_in || !ent->fade_out || !ent->vol_db ||
            !ent->muted || !ent->color_idx || !ent->peak_color_idx || !ent->group_id) {
            free_deep_undo_entry_peaks(ent);
            return;
        }
    }
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].peak_count > 0 && p->items[i].peaks) {
            p->items[i].peaks = daw_register_shared_peaks(
                p->items[i].raw_audio_path, p->items[i].peaks,
                p->items[i].peak_count, p->items[i].file_total_duration, p->items[i].has_video);
        }
        ent->items[i] = p->items[i];
        ent->items[i].raw_file_handle = NULL;
        ent->fade_in[i]        = g_item_fade_in[tab][i];
        ent->fade_out[i]       = g_item_fade_out[tab][i];
        ent->vol_db[i]         = g_item_vol_db[tab][i];
        ent->muted[i]          = g_item_muted[tab][i];
        ent->color_idx[i]      = g_item_color_idx[tab][i];
        ent->peak_color_idx[i] = g_item_peak_color_idx[tab][i];
        ent->group_id[i]       = g_item_group_id[tab][i];
    }

    memcpy(ent->tracks, p->tracks, sizeof(p->tracks));
    memcpy(ent->track_names, p->track_names, sizeof(p->track_names));
    memcpy(ent->track_db, p->track_db, sizeof(p->track_db));
    memcpy(ent->track_heights, p->track_heights, sizeof(p->track_heights));
    memcpy(ent->track_selected, p->track_selected, sizeof(p->track_selected));
    memcpy(ent->track_hidden, g_track_hidden[tab], sizeof(ent->track_hidden));
    memcpy(ent->track_color_idx, g_track_color_idx[tab], sizeof(ent->track_color_idx));
    memcpy(ent->track_peak_color_idx, g_track_peak_color_idx[tab], sizeof(ent->track_peak_color_idx));
    memcpy(ent->track_locked, g_track_locked[tab], sizeof(ent->track_locked));
    memcpy(ent->track_lock_lr_only, g_track_lock_lr_only[tab], sizeof(ent->track_lock_lr_only));
    memcpy(ent->track_lock_ud_only, g_track_lock_ud_only[tab], sizeof(ent->track_lock_ud_only));
    ent->lock_opt_full            = g_lock_opt_full[tab];
    ent->lock_opt_complete_unlock = g_lock_opt_complete_unlock[tab];
    ent->lock_opt_prevent_lr      = g_lock_opt_prevent_lr[tab];
    ent->lock_opt_prevent_ud      = g_lock_opt_prevent_ud[tab];
    ent->tab_peak_color_idx = g_tab_peak_color_idx[tab];

    ent->show_marker_lane = p->show_marker_lane;
    ent->tab_crossfade    = g_tab_crossfade[tab];
    ent->range_active     = g_tab_range_active[tab];
    ent->range_start      = g_tab_range_start[tab];
    ent->range_end        = g_tab_range_end[tab];

    ent->num_markers = p->num_markers;
    memcpy(ent->markers, p->markers, sizeof(DawMarker) * (size_t)p->num_markers);
    memcpy(ent->marker_full_text, g_marker_full_text[tab], sizeof(ent->marker_full_text));
}

static void restore_tab_from_deep_entry(ProjectTab *p, int tab, const DeepUndoEntry *ent) {
    if (!p || !ent) return;

    g_engine_processing = true;
    if (g_daw.audio_device > 0) SDL_LockAudioDevice(g_daw.audio_device);

    // 1. Safely close current file handles on the tab (peaks live in shared pool)
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].raw_file_handle) {
            fclose(p->items[i].raw_file_handle);
            p->items[i].raw_file_handle = NULL;
        }
        daw_free_peaks_if_unpooled(p->items[i].peaks);
        p->items[i].peaks = NULL;
    }

    // 2. Restore track & marker states
    p->num_tracks = ent->num_tracks;
    memcpy(p->tracks, ent->tracks, sizeof(p->tracks));
    memcpy(p->track_names, ent->track_names, sizeof(p->track_names));
    memcpy(p->track_db, ent->track_db, sizeof(p->track_db));
    memcpy(p->track_heights, ent->track_heights, sizeof(p->track_heights));
    memcpy(p->track_selected, ent->track_selected, sizeof(p->track_selected));
    memcpy(g_track_hidden[tab], ent->track_hidden, sizeof(ent->track_hidden));
    memcpy(g_track_color_idx[tab], ent->track_color_idx, sizeof(ent->track_color_idx));
    memcpy(g_track_peak_color_idx[tab], ent->track_peak_color_idx, sizeof(ent->track_peak_color_idx));
    memcpy(g_track_locked[tab], ent->track_locked, sizeof(ent->track_locked));
    memcpy(g_track_lock_lr_only[tab], ent->track_lock_lr_only, sizeof(ent->track_lock_lr_only));
    memcpy(g_track_lock_ud_only[tab], ent->track_lock_ud_only, sizeof(ent->track_lock_ud_only));
    g_lock_opt_full[tab]            = ent->lock_opt_full;
    g_lock_opt_complete_unlock[tab] = ent->lock_opt_complete_unlock;
    g_lock_opt_prevent_lr[tab]      = ent->lock_opt_prevent_lr;
    g_lock_opt_prevent_ud[tab]      = ent->lock_opt_prevent_ud;
    g_tab_peak_color_idx[tab] = ent->tab_peak_color_idx;

    p->show_marker_lane     = ent->show_marker_lane;
    g_tab_crossfade[tab]    = ent->tab_crossfade;
    g_tab_range_active[tab] = ent->range_active;
    g_tab_range_start[tab]  = ent->range_start;
    g_tab_range_end[tab]    = ent->range_end;

    p->num_markers = ent->num_markers;
    memcpy(p->markers, ent->markers, sizeof(DawMarker) * (size_t)ent->num_markers);
    memcpy(g_marker_full_text[tab], ent->marker_full_text, sizeof(ent->marker_full_text));

    // 3. Restore all clips with shared peaks and 64-bit buffered file handles
    p->num_items = ent->num_items;
    for (int i = 0; i < ent->num_items; i++) {
        p->items[i] = ent->items[i];
        p->items[i].raw_file_handle = NULL;
        if (p->items[i].raw_audio_path[0] != '\0') {
            p->items[i].raw_file_handle = daw_fopen_LockFreeRead(p->items[i].raw_audio_path);
        }
        // Fallback: if raw cache file was missing, regenerate cache from source media file while preserving edits
        if (!p->items[i].raw_file_handle && p->items[i].file_path[0] != '\0') {
            double sv_st  = p->items[i].start_time;
            double sv_off = p->items[i].offset_in_file;
            double sv_dur = p->items[i].duration;
            int    sv_trk = p->items[i].track_idx;
            bool   sv_sel = p->items[i].is_selected;
            char   sv_lbl[128];
            strncpy(sv_lbl, p->items[i].label, sizeof(sv_lbl) - 1);
            sv_lbl[sizeof(sv_lbl) - 1] = '\0';
            process_media_cache(p->items[i].file_path, &p->items[i]);
            p->items[i].start_time     = sv_st;
            p->items[i].offset_in_file = sv_off;
            p->items[i].duration       = sv_dur;
            p->items[i].track_idx      = sv_trk;
            p->items[i].is_selected    = sv_sel;
            if (sv_lbl[0]) strncpy(p->items[i].label, sv_lbl, sizeof(p->items[i].label) - 1);
        }

        g_item_fade_in[tab][i]        = ent->fade_in[i];
        g_item_fade_out[tab][i]       = ent->fade_out[i];
        g_item_vol_db[tab][i]         = ent->vol_db[i];
        g_item_muted[tab][i]          = ent->muted[i];
        g_item_color_idx[tab][i]      = ent->color_idx[i];
        g_item_peak_color_idx[tab][i] = ent->peak_color_idx[i];
        g_item_group_id[tab][i]       = ent->group_id[i];
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
        g_deep_undo_has_redo_tip[tab] = false;
    }

    int max_push_head = MAX_DEEP_UNDO - 2;
    if (g_deep_undo_has_redo_tip[tab] && g_deep_undo_head[tab] >= 0) {
        // When branching after an Undo/Redo, g_deep_undo_head[tab] is already the current live state slot;
        // overwrite it with the exact live state and discard future redo slots so no duplicate snapshot is created!
        if (g_deep_undo_head[tab] > max_push_head) {
            free_deep_undo_entry_peaks(&g_deep_undo_stack[tab][0]);
            for (int i = 0; i < max_push_head; i++) {
                g_deep_undo_stack[tab][i] = g_deep_undo_stack[tab][i + 1];
            }
            memset(&g_deep_undo_stack[tab][max_push_head], 0, sizeof(DeepUndoEntry));
            g_deep_undo_head[tab] = max_push_head;
        }
    } else {
        if (g_deep_undo_head[tab] < max_push_head) {
            g_deep_undo_head[tab]++;
        } else {
            free_deep_undo_entry_peaks(&g_deep_undo_stack[tab][0]);
            for (int i = 0; i < max_push_head; i++) {
                g_deep_undo_stack[tab][i] = g_deep_undo_stack[tab][i + 1];
            }
            memset(&g_deep_undo_stack[tab][max_push_head], 0, sizeof(DeepUndoEntry));
            g_deep_undo_head[tab] = max_push_head;
        }
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

// Race-free playhead setter: synchronizes audio mixer callback and video decoder immediately on any seek
static void daw_set_playhead_seconds(ProjectTab *p, double new_sec) {
    if (!p) return;
    if (new_sec < 0.0) new_sec = 0.0;
    int tab = get_valid_tab_idx();
    if (g_daw.audio_device > 0) SDL_LockAudioDevice(g_daw.audio_device);
    p->playhead_seconds = new_sec;
    g_playhead_seek_serial[tab]++;
    p->video.current_pts_sec = -1.0;
    if (g_daw.audio_device > 0) SDL_UnlockAudioDevice(g_daw.audio_device);
}

// Computes the full project duration across all clips and markers (supports 3h, 4h, or longer projects)
static double get_complete_project_duration_sec(ProjectTab *p) {
    if (!p) return 300.0;
    double max_e = 0.0;
    for (int i = 0; i < p->num_items; i++) {
        double ie = p->items[i].start_time + p->items[i].duration;
        if (ie > max_e) max_e = ie;
    }
    for (int m = 0; m < p->num_markers; m++) {
        if (p->markers[m].time_sec + 5.0 > max_e) max_e = p->markers[m].time_sec + 5.0;
    }
    if (max_e <= 1.0) {
        max_e = (p->project_duration_sec > 60.0) ? p->project_duration_sec : 300.0;
    }
    return max_e;
}

// Computes the minimum pixels_per_second so zooming out fits the complete project duration into a single screen
static float get_min_zoom_pps(ProjectTab *p, int view_w) {
    if (view_w < 100) view_w = 100;
    double full_dur = get_complete_project_duration_sec(p) * 1.02;
    if (full_dur < 10.0) full_dur = 10.0;
    float min_pps = (float)((double)view_w / full_dur);
    if (min_pps < 0.002f) min_pps = 0.002f;
    return min_pps;
}

// Automatically scrolls p->scroll_x_sec so the playhead is always visible on screen wherever it moves
static void ensure_playhead_visible_in_viewport(ProjectTab *p, int view_w) {
    if (!p || view_w <= 60 || p->pixels_per_second <= 0.0005f) return;
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
        daw_free_peaks_if_unpooled(p->items[idx].peaks);
        p->items[idx].peaks = NULL;
    }
    for (int k = idx; k < p->num_items - 1; k++) {
        p->items[k] = p->items[k + 1];
        g_item_fade_in[tab][k]   = g_item_fade_in[tab][k + 1];
        g_item_fade_out[tab][k]  = g_item_fade_out[tab][k + 1];
        g_item_vol_db[tab][k]         = g_item_vol_db[tab][k + 1];
        g_item_muted[tab][k]          = g_item_muted[tab][k + 1];
        g_item_color_idx[tab][k]      = g_item_color_idx[tab][k + 1];
        g_item_peak_color_idx[tab][k] = g_item_peak_color_idx[tab][k + 1];
        g_item_group_id[tab][k]       = g_item_group_id[tab][k + 1];
    }
    int last = p->num_items - 1;
    g_item_fade_in[tab][last]        = 0.0;
    g_item_fade_out[tab][last]       = 0.0;
    g_item_vol_db[tab][last]         = 0.0f;
    g_item_muted[tab][last]          = false;
    g_item_color_idx[tab][last]      = 0;
    g_item_peak_color_idx[tab][last] = 0;
    g_item_group_id[tab][last]       = 0;
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
        g_track_color_idx[tab][t] = g_track_color_idx[tab][t + 1];
        g_track_peak_color_idx[tab][t] = g_track_peak_color_idx[tab][t + 1];
        g_track_locked[tab][t] = g_track_locked[tab][t + 1];
        g_track_lock_lr_only[tab][t] = g_track_lock_lr_only[tab][t + 1];
        g_track_lock_ud_only[tab][t] = g_track_lock_ud_only[tab][t + 1];
    }
    p->num_tracks--;
}

// Checks if any selected track contains clips before deleting:
// If a selected track contains any clip, opens the Delete / Cancel confirmation modal!
static bool request_delete_selected_tracks_with_prompt(ProjectTab *p) {
    if (!p || p->num_tracks <= 1) return false;
    bool any_sel = false;
    bool any_has_clips = false;
    int tab_del = get_valid_tab_idx();
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->track_selected[t] && !is_track_full_locked_tab(tab_del, t)) {
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
        if (p->track_selected[t] && !is_track_full_locked_tab(get_valid_tab_idx(), t) && p->num_tracks > 1) {
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
        if (p->track_selected[t] && !is_track_full_locked_tab(get_valid_tab_idx(), t) && p->num_tracks > 1) {
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
    int   color_idx;
    int   peak_color_idx;
    int   num_clips;
    ClipboardItemEntry clips[64];
} TrackClipboardEntry;

static TrackClipboardEntry g_track_clipboard[MAX_TRACKS];
static int g_track_clipboard_count = 0;

static void copy_selected_tracks_to_clipboard(ProjectTab *p) {
    if (!p) return;
    int tab = get_valid_tab_idx();
    g_track_clipboard_count = 0;

    for (int t = 0; t < p->num_tracks && g_track_clipboard_count < MAX_TRACKS; t++) {
        if (!p->track_selected[t]) continue;
        TrackClipboardEntry *te = &g_track_clipboard[g_track_clipboard_count++];
        memset(te, 0, sizeof(TrackClipboardEntry));
        te->track  = p->tracks[t];
        strncpy(te->name, p->track_names[t], sizeof(te->name) - 1);
        te->db             = p->track_db[t];
        te->height         = p->track_heights[t];
        te->color_idx      = g_track_color_idx[tab][t];
        te->peak_color_idx = g_track_peak_color_idx[tab][t];
        for (int i = 0; i < p->num_items && te->num_clips < 64; i++) {
            if (p->items[i].track_idx != t) continue;
            ClipboardItemEntry *ce = &te->clips[te->num_clips++];
            if (p->items[i].peak_count > 0 && p->items[i].peaks) {
                p->items[i].peaks = daw_register_shared_peaks(
                    p->items[i].raw_audio_path, p->items[i].peaks,
                    p->items[i].peak_count, p->items[i].file_total_duration, p->items[i].has_video);
            }
            memcpy(&ce->item, &p->items[i], sizeof(AudioItem));
            ce->item.raw_file_handle = NULL;
            ce->fade_in        = g_item_fade_in[tab][i];
            ce->fade_out       = g_item_fade_out[tab][i];
            ce->vol_db         = g_item_vol_db[tab][i];
            ce->muted          = g_item_muted[tab][i];
            ce->color_idx      = g_item_color_idx[tab][i];
            ce->peak_color_idx = g_item_peak_color_idx[tab][i];
            ce->group_id       = g_item_group_id[tab][i];
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
            g_track_color_idx[tab][t] = g_track_color_idx[tab][t - 1];
            g_track_peak_color_idx[tab][t] = g_track_peak_color_idx[tab][t - 1];
            g_track_locked[tab][t] = g_track_locked[tab][t - 1];
            g_track_lock_lr_only[tab][t] = g_track_lock_lr_only[tab][t - 1];
            g_track_lock_ud_only[tab][t] = g_track_lock_ud_only[tab][t - 1];
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
        g_track_color_idx[tab][ins_idx] = te->color_idx;
        g_track_peak_color_idx[tab][ins_idx] = te->peak_color_idx;
        g_track_locked[tab][ins_idx] = false;
        g_track_lock_lr_only[tab][ins_idx] = false;
        g_track_lock_ud_only[tab][ins_idx] = false;
        g_last_clicked_track_idx = ins_idx;

        // Paste all clips belonging to this copied track
        for (int c = 0; c < te->num_clips && p->num_items < MAX_ITEMS; c++) {
            int ni = p->num_items++;
            ClipboardItemEntry *ce = &te->clips[c];
            AudioItem *dst = &p->items[ni];
            memcpy(dst, &ce->item, sizeof(AudioItem));
            dst->track_idx = ins_idx;
            dst->is_selected = false;
            dst->raw_file_handle = daw_fopen_LockFreeRead(dst->raw_audio_path);
            g_item_fade_in[tab][ni]        = ce->fade_in;
            g_item_fade_out[tab][ni]       = ce->fade_out;
            g_item_vol_db[tab][ni]         = ce->vol_db;
            g_item_muted[tab][ni]          = ce->muted;
            g_item_color_idx[tab][ni]      = ce->color_idx;
            g_item_peak_color_idx[tab][ni] = ce->peak_color_idx;
            g_item_group_id[tab][ni]       = ce->group_id;
        }

        insert_after = ins_idx;
    }

    record_project_change("Pasted Track(s) Below Selected Track", true);
}

// Inserts a new track immediately below the currently selected track (or at the bottom if no track is selected)
static void insert_new_track_below_selected(ProjectTab *p) {
    if (!p || p->num_tracks >= MAX_TRACKS) return;
    int tab = get_valid_tab_idx();
    ensure_app_vol_defaults_initialized();

    int insert_after = p->num_tracks - 1;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->track_selected[t]) insert_after = t;
    }
    int ins_idx = insert_after + 1;
    if (ins_idx < 0) ins_idx = 0;
    if (ins_idx > p->num_tracks) ins_idx = p->num_tracks;

    push_undo_state();
    for (int t = 0; t < p->num_tracks; t++) p->track_selected[t] = false;

    for (int t = p->num_tracks; t > ins_idx; t--) {
        p->tracks[t] = p->tracks[t - 1];
        p->tracks[t].id = t + 1;
        memcpy(p->track_names[t], p->track_names[t - 1], 64);
        p->track_db[t] = p->track_db[t - 1];
        p->track_heights[t] = p->track_heights[t - 1];
        p->track_selected[t] = false;
        g_track_hidden[tab][t] = g_track_hidden[tab][t - 1];
        g_track_color_idx[tab][t] = g_track_color_idx[tab][t - 1];
        g_track_peak_color_idx[tab][t] = g_track_peak_color_idx[tab][t - 1];
        g_track_locked[tab][t] = g_track_locked[tab][t - 1];
        g_track_lock_lr_only[tab][t] = g_track_lock_lr_only[tab][t - 1];
        g_track_lock_ud_only[tab][t] = g_track_lock_ud_only[tab][t - 1];
        g_track_inf_db[tab][t] = g_track_inf_db[tab][t - 1];
        g_track_pre_inf_db[tab][t] = g_track_pre_inf_db[tab][t - 1];
        g_app_track_vol[tab][t] = g_app_track_vol[tab][t - 1];
    }
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].track_idx >= ins_idx) p->items[i].track_idx++;
    }
    p->num_tracks++;

    memset(&p->tracks[ins_idx], 0, sizeof(Track));
    p->tracks[ins_idx].id = ins_idx + 1;
    snprintf(p->track_names[ins_idx], 64, "Track %d", p->num_tracks);
    p->track_db[ins_idx] = 0.0f;
    p->track_heights[ins_idx] = 84;
    p->track_selected[ins_idx] = true;
    g_track_hidden[tab][ins_idx] = false;
    g_track_color_idx[tab][ins_idx] = 0;
    g_track_peak_color_idx[tab][ins_idx] = 0;
    g_track_locked[tab][ins_idx] = false;
    g_track_lock_lr_only[tab][ins_idx] = false;
    g_track_lock_ud_only[tab][ins_idx] = false;
    g_track_inf_db[tab][ins_idx] = false;
    g_track_pre_inf_db[tab][ins_idx] = 0.0f;
    g_app_track_vol[tab][ins_idx] = 100.0f;
    g_last_clicked_track_idx = ins_idx;

    record_project_change("Added Track Below Selected Track", true);
}

// Copies all selected items into the internal clipboard
static void copy_selected_items_to_clipboard(ProjectTab *p) {
    if (!p) return;
    int tab = get_valid_tab_idx();
    g_clipboard_count = 0;
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].is_selected && g_clipboard_count < MAX_ITEMS) {
            ClipboardItemEntry *ce = &g_clipboard[g_clipboard_count++];
            if (p->items[i].peak_count > 0 && p->items[i].peaks) {
                p->items[i].peaks = daw_register_shared_peaks(
                    p->items[i].raw_audio_path, p->items[i].peaks,
                    p->items[i].peak_count, p->items[i].file_total_duration, p->items[i].has_video);
            }
            memcpy(&ce->item, &p->items[i], sizeof(AudioItem));
            ce->item.raw_file_handle = NULL;
            ce->fade_in   = g_item_fade_in[tab][i];
            ce->fade_out  = g_item_fade_out[tab][i];
            ce->vol_db         = g_item_vol_db[tab][i];
            ce->muted          = g_item_muted[tab][i];
            ce->color_idx      = g_item_color_idx[tab][i];
            ce->peak_color_idx = g_item_peak_color_idx[tab][i];
            ce->group_id       = g_item_group_id[tab][i];
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
        dst->raw_file_handle = daw_fopen_LockFreeRead(dst->raw_audio_path);
        g_item_fade_in[tab][ni]   = ce->fade_in;
        g_item_fade_out[tab][ni]  = ce->fade_out;
        g_item_vol_db[tab][ni]         = ce->vol_db;
        g_item_muted[tab][ni]          = ce->muted;
        g_item_color_idx[tab][ni]      = ce->color_idx;
        g_item_peak_color_idx[tab][ni] = ce->peak_color_idx;
        g_item_group_id[tab][ni]       = ce->group_id;
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

#endif // DAW_14POINT_STATE_DEFINED

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
    "media",
    0.0f, 0.0f, 0, 0, 0,
    false
};

static inline void init_default_recording_folder(void) {
    if (g_rec.folder_initialized) return;
    g_rec.folder_initialized = true;
    if (!g_rec.folder_path[0]) {
        snprintf(g_rec.folder_path, sizeof(g_rec.folder_path), "media");
    }
}


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

static void delete_saved_theme_at_index(int del_idx) {
    if (del_idx < NUM_INBUILT_THEMES || del_idx >= g_theme.num_saved) {
        if (del_idx >= 0 && del_idx < NUM_INBUILT_THEMES) {
            snprintf(g_theme.status_msg, sizeof(g_theme.status_msg),
                     "Inbuilt theme '%s' cannot be deleted (only manually added themes)",
                     g_theme.saved[del_idx].name);
        }
        return;
    }
    char deleted_name[64] = {0};
    strncpy(deleted_name, g_theme.saved[del_idx].name, sizeof(deleted_name) - 1);
    for (int k = del_idx; k < g_theme.num_saved - 1; k++) {
        g_theme.saved[k] = g_theme.saved[k + 1];
    }
    g_theme.num_saved--;
    if (g_theme.selected_saved_idx >= g_theme.num_saved) {
        g_theme.selected_saved_idx = (g_theme.num_saved > 0) ? (g_theme.num_saved - 1) : 0;
    }
    save_themes_settings_json();
    snprintf(g_theme.status_msg, sizeof(g_theme.status_msg),
             "Deleted saved theme '%s' from themes_settings.json", deleted_name);
    record_project_change("Deleted Saved Theme", false);
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
    if (g_theme.code_focused) {
        daw_draw_text_edit_overlay(ren, g_theme.code_buf, code_box.x + 8, code_box.y, code_box.h, code_box.x + code_box.w - 4);
    }
    draw_text_clipped(ren, g_theme.code_buf, code_box.x + 8, code_y + 9, c_white, &code_clip, NULL);

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

    // 5. Left Bottom Buttons: [Save Theme], [Delete], and [Default]
    int b_y = wy + 350;
    SDL_Rect btn_save_th = { p_x, b_y, 84, 30 };
    SDL_SetRenderDrawColor(ren, hr, hg, hb, 255);
    SDL_RenderFillRect(ren, &btn_save_th);
    draw_brown_border_rect(ren, &btn_save_th, true, false);
    draw_text(ren, "Save Theme", p_x + 4, b_y + 9, c_white);

    bool can_del_theme = (g_theme.num_saved > NUM_INBUILT_THEMES &&
                          g_theme.selected_saved_idx >= NUM_INBUILT_THEMES &&
                          g_theme.selected_saved_idx < g_theme.num_saved);
    SDL_Rect btn_del_th = { p_x + 88, b_y, 72, 30 };
    SDL_SetRenderDrawColor(ren, can_del_theme ? 92 : 34, can_del_theme ? 32 : 30, can_del_theme ? 30 : 28, 255);
    SDL_RenderFillRect(ren, &btn_del_th);
    draw_brown_border_rect(ren, &btn_del_th, false, !can_del_theme);
    draw_text(ren, "Delete", p_x + 100, b_y + 9, can_del_theme ? c_white : c_muted);

    SDL_Rect btn_def_th = { p_x + 164, b_y, 92, 30 };
    SDL_SetRenderDrawColor(ren, 42, 38, 34, 255);
    SDL_RenderFillRect(ren, &btn_def_th);
    draw_brown_border_rect(ren, &btn_def_th, false, false);
    draw_text(ren, "Default", p_x + 182, b_y + 9, c_white);

    // ---- RIGHT SIDE: Saved Themes List (Click to Apply / x Delete) ----
    int rx = wx + 292;
    int rw = ww - 308;
    draw_text(ren, "Saved Themes (Click to Apply / x Delete):", rx, wy + 44, c_white);

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
        bool is_sel_row = (i == g_theme.selected_saved_idx);
        bool is_active = is_sel_row || (st->r == g_theme.r && st->g == g_theme.g && st->b == g_theme.b && st->a == g_theme.a);
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
        SDL_Rect row_clip = { row_r.x + 26, ry + 2, row_r.w - 52, 20 };
        draw_text_clipped(ren, item_lbl, row_r.x + 26, ry + 6, is_active ? c_white : c_muted, &row_clip, &list_clip);

        // Per-row [x] delete button ONLY for extra manually added themes (i >= NUM_INBUILT_THEMES)
        if (i >= NUM_INBUILT_THEMES) {
            SDL_Rect del_r = { row_r.x + row_r.w - 22, ry + 3, 18, 19 };
            SDL_SetRenderDrawColor(ren, 68, 28, 28, 255);
            SDL_RenderFillRect(ren, &del_r);
            draw_brown_border_rect(ren, &del_r, false, true);
            draw_text_clipped(ren, "x", del_r.x + 5, del_r.y + 3, c_white, &del_r, &list_clip);
        }
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

    (void)wh;
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
#ifdef _WIN32
    // Write clean app_icon.ico (without red recording dot) to disk and set both ICON_BIG and ICON_SMALL on the Win32 HWND + Window Class
    if (1) {
        FILE *f_ico = fopen("app_icon.ico", "wb");
        if (f_ico) {
            uint8_t ico_hdr[22] = {
                0, 0, 1, 0, 1, 0,
                64, 64, 0, 0, 1, 0, 32, 0,
                0x28, 0x42, 0x00, 0x00, // 40 + 64*64*4 + 64*8 = 16936 bytes (0x4228)
                22, 0, 0, 0
            };
            uint8_t bmp_hdr[40] = {
                40, 0, 0, 0,
                64, 0, 0, 0,
                128, 0, 0, 0, // 2 * 64 height for XOR + AND mask
                1, 0, 32, 0,
                0, 0, 0, 0,
                0x00, 0x42, 0x00, 0x00,
                0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 0
            };
            fwrite(ico_hdr, 1, sizeof(ico_hdr), f_ico);
            fwrite(bmp_hdr, 1, sizeof(bmp_hdr), f_ico);
            for (int row = 63; row >= 0; row--) {
                fwrite(&icon_px[row * 64], sizeof(uint32_t), 64, f_ico);
            }
            uint8_t mask_zero[64 * 8] = {0};
            fwrite(mask_zero, 1, sizeof(mask_zero), f_ico);
            fclose(f_ico);
        }
    }
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(win, &wmInfo)) {
        HWND hwnd = wmInfo.info.win.window;
        HINSTANCE hInst = GetModuleHandleA(NULL);
        HICON hIconBig = (HICON)LoadImageA(hInst, MAKEINTRESOURCEA(1), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR);
        HICON hIconSm  = (HICON)LoadImageA(hInst, MAKEINTRESOURCEA(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        if (!hIconBig) hIconBig = (HICON)LoadImageA(NULL, "app_icon.ico", IMAGE_ICON, 64, 64, LR_LOADFROMFILE);
        if (!hIconSm)  hIconSm  = (HICON)LoadImageA(NULL, "app_icon.ico", IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
        if (hIconBig) {
            SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
            SetClassLongPtrA(hwnd, GCLP_HICON, (LONG_PTR)hIconBig);
        }
        if (hIconSm) {
            SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);
            SetClassLongPtrA(hwnd, GCLP_HICONSM, (LONG_PTR)hIconSm);
        }
    }
#endif
}

// Renders the "Processing, please wait..." stability popup window and pumps SDL events so the app never freezes
static void ensure_popup_fits_text_width(SDL_Rect *wr, int min_w, int min_h, const char *txt, int pad_w) {
    if (!wr) return;
    if (wr->w < min_w) wr->w = min_w;
    if (wr->h < min_h) wr->h = min_h;
    if (txt && txt[0]) {
        int needed = (int)strlen(txt) * 8 + pad_w;
        if (wr->w < needed) wr->w = needed;
    }
}

static void render_processing_wait_overlay(SDL_Renderer *ren, SDL_Window *win) {
    if (!g_processing_popup_open || !ren || !win) return;
    int win_w = 1280, win_h = 720;
    SDL_GetWindowSize(win, &win_w, &win_h);

    int pw = 500, ph = 136;
    int t_need = (int)strlen(g_processing_title) * 8 + 48;
    int d_need = (int)strlen(g_processing_detail) * 8 + 48;
    if (pw < t_need) pw = t_need;
    if (pw < d_need) pw = d_need;
    if (pw > win_w - 32 && win_w > 360) pw = win_w - 32;
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

// Requirement 6: Check if selected track has "Infinity -dB" (-inf dB) enabled
static bool is_selected_track_infinity_db_on(ProjectTab *p) {
    if (!p || p->num_tracks <= 0) return false;
    int tab = get_valid_tab_idx();
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->track_selected[t]) {
            return (g_track_inf_db[tab][t] || p->track_db[t] <= -60.0f);
        }
    }
    int fb = (g_last_clicked_track_idx >= 0 && g_last_clicked_track_idx < p->num_tracks) ? g_last_clicked_track_idx : 0;
    return (g_track_inf_db[tab][fb] || p->track_db[fb] <= -60.0f);
}

// Requirement 6: Toggle "Infinity -dB" ON/OFF for the selected track(s) (like REAPER -inf dB)
static void toggle_selected_track_infinity_db(ProjectTab *p) {
    if (!p || p->num_tracks <= 0) return;
    int tab = get_valid_tab_idx();
    bool any_sel = false;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->track_selected[t]) {
            any_sel = true;
            break;
        }
    }
    if (!any_sel) {
        int fb = (g_last_clicked_track_idx >= 0 && g_last_clicked_track_idx < p->num_tracks) ? g_last_clicked_track_idx : 0;
        p->track_selected[fb] = true;
    }
    bool turn_on = !is_selected_track_infinity_db_on(p);
    push_undo_state();
    for (int t = 0; t < p->num_tracks; t++) {
        if (!p->track_selected[t]) continue;
        if (turn_on) {
            if (p->track_db[t] > -59.0f) {
                g_track_pre_inf_db[tab][t] = p->track_db[t];
            } else if (g_track_pre_inf_db[tab][t] <= -59.0f) {
                g_track_pre_inf_db[tab][t] = 0.0f;
            }
            g_track_inf_db[tab][t] = true;
            p->track_db[t] = -120.0f; // -inf dB silence
        } else {
            g_track_inf_db[tab][t] = false;
            float restore_db = g_track_pre_inf_db[tab][t];
            if (restore_db <= -59.0f || restore_db > 24.0f) restore_db = 0.0f;
            p->track_db[t] = restore_db;
        }
    }
    p->is_dirty = true;
    record_project_change(turn_on ? "Track Infinity -dB: ON (-inf dB)" : "Track Infinity -dB: OFF (Restored dB)", true);
}

// Requirement 8: Stabilize application state on any unexpected fault or stall and show Wait Processing popup
static void stabilize_app_state_and_recover(ProjectTab *p, const char *reason) {
    g_engine_processing = false;
    if (g_daw.active_tab < 0 || g_daw.active_tab >= MAX_TABS) g_daw.active_tab = 0;
    if (g_daw.num_tabs <= 0 || g_daw.num_tabs > MAX_TABS) g_daw.num_tabs = 1;
    if (p) {
        if (p->num_tracks <= 0) p->num_tracks = 1;
        if (p->num_tracks > MAX_TRACKS) p->num_tracks = MAX_TRACKS;
        if (p->num_items < 0) p->num_items = 0;
        if (p->num_items > MAX_ITEMS) p->num_items = MAX_ITEMS;
        if (p->num_markers < 0) p->num_markers = 0;
        if (p->num_markers > MAX_MARKERS) p->num_markers = MAX_MARKERS;
        if (!(p->pixels_per_second >= 0.01f && p->pixels_per_second <= 5000.0f)) p->pixels_per_second = 50.0f;
        if (!(p->playhead_seconds >= 0.0 && p->playhead_seconds < 864000.0)) p->playhead_seconds = 0.0;
        if (!(p->scroll_x_sec >= 0.0 && p->scroll_x_sec < 864000.0)) p->scroll_x_sec = 0.0;
    }
    show_processing_wait_modal(
        g_main_ren, g_main_win,
        "Wait - Processing / Stabilizing...",
        reason ? reason : "Stabilizing application state, please wait...",
        100
    );
    finish_processing_wait_modal(550);
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
        if (is_ed) {
            daw_draw_text_edit_overlay(ren, g_dyn_split.edit_buf, bx.x + 8, bx.y, bx.h, bx.x + bx.w - 4);
        }
        draw_text(ren, txt, bx.x + 8, ry + 5, is_ed ? c_accent : c_white);
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
    if (g_reposition.interval_focused) {
        daw_draw_text_edit_overlay(ren, g_reposition.interval_str, ti_box.x + 8, ti_box.y, ti_box.h, ti_box.x + ti_box.w - 4);
    }
    draw_text(ren, g_reposition.interval_str, ti_box.x + 8, ti_y + 5, c_white);
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

// ================= TRACK COLOR, CLIP COLOR & PEAKS COLOR PICKER POPUP WINDOW =================
typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;
    int target_mode; // 0 = Track color, 1 = Clip color, 2 = Peaks Color
} ColorPickerDialogState;

static ColorPickerDialogState g_color_picker = {
    false,
    { 400, 96, 440, 396 },
    false, 0, 0,
    0
};

static void open_color_picker_dialog(int target_mode) {
    if (target_mode < 0 || target_mode > 2) target_mode = 0;
    g_color_picker.target_mode = target_mode;
    g_color_picker.is_open = true;
}

static void apply_studio_color_choice(ProjectTab *p, int color_idx) {
    if (!p) return;
    int tab = get_valid_tab_idx();
    if (color_idx < 0 || color_idx >= NUM_STUDIO_COLORS) color_idx = 0;

    if (g_color_picker.target_mode == 0) {
        // Track color
        bool any_trk = false;
        for (int t = 0; t < p->num_tracks; t++) {
            if (p->track_selected[t]) { any_trk = true; break; }
        }
        if (!any_trk && p->num_tracks > 0) {
            int fallback_t = (g_last_clicked_track_idx >= 0 && g_last_clicked_track_idx < p->num_tracks)
                             ? g_last_clicked_track_idx : 0;
            p->track_selected[fallback_t] = true;
        }
        push_undo_state();
        for (int t = 0; t < p->num_tracks; t++) {
            if (p->track_selected[t] && !is_track_full_locked_tab(tab, t)) {
                g_track_color_idx[tab][t] = color_idx;
            }
        }
        record_project_change("Changed Selected Track Color", true);
    } else if (g_color_picker.target_mode == 1) {
        // Clip color
        bool any_clip = false;
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].is_selected && !is_track_full_locked_tab(tab, p->items[i].track_idx)) { any_clip = true; break; }
        }
        if (!any_clip && g_last_clicked_item_idx >= 0 && g_last_clicked_item_idx < p->num_items &&
            !is_track_full_locked_tab(tab, p->items[g_last_clicked_item_idx].track_idx)) {
            p->items[g_last_clicked_item_idx].is_selected = true;
            any_clip = true;
        }
        if (any_clip) {
            push_undo_state();
            for (int i = 0; i < p->num_items; i++) {
                if (p->items[i].is_selected && !is_track_full_locked_tab(tab, p->items[i].track_idx)) {
                    g_item_color_idx[tab][i] = color_idx;
                }
            }
            record_project_change("Changed Selected Clip Color", true);
        }
    } else {
        // Universal Peaks Color (target_mode == 2): applies universally across the entire software and saves forever like themes
        g_universal_peak_color_idx = color_idx;
        for (int t_idx = 0; t_idx < MAX_TABS; t_idx++) {
            g_tab_peak_color_idx[t_idx] = color_idx;
            for (int tr = 0; tr < MAX_TRACKS; tr++) {
                g_track_peak_color_idx[t_idx][tr] = color_idx;
            }
            for (int it = 0; it < MAX_ITEMS; it++) {
                g_item_peak_color_idx[t_idx][it] = color_idx;
            }
        }
        save_themes_settings_json();
        record_project_change("Changed Universal Peaks Color (Saved Forever)", false);
    }
}

static void render_color_picker_window(SDL_Renderer *ren, ProjectTab *p) {
    if (!g_color_picker.is_open || !p) return;
    SDL_Rect *wr = &g_color_picker.win_rect;
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
    const char *cp_title = (g_color_picker.target_mode == 0) ? "Edit - Track Color"
                         : ((g_color_picker.target_mode == 1) ? "Edit - Clip Color" : "Edit - Peaks Color");
    draw_text(ren, cp_title, wx + 14, wy + 9, c_white);
    draw_text(ren, "X", wx + ww - 20, wy + 9, c_white);

    // Mode switch tabs: [Track color] | [Clip color] | [Peaks Color]
    int m = g_color_picker.target_mode;
    SDL_Rect b_m_trk = { wx + 16, wy + 38, 132, 26 };
    SDL_SetRenderDrawColor(ren, (m == 0) ? hr + 24 : 32, (m == 0) ? hg + 16 : 28, (m == 0) ? hb + 10 : 26, 255);
    SDL_RenderFillRect(ren, &b_m_trk);
    draw_brown_border_rect(ren, &b_m_trk, m == 0, m != 0);
    draw_text(ren, "Track color", b_m_trk.x + 22, wy + 45, c_white);

    SDL_Rect b_m_clip = { wx + 154, wy + 38, 132, 26 };
    SDL_SetRenderDrawColor(ren, (m == 1) ? hr + 24 : 32, (m == 1) ? hg + 16 : 28, (m == 1) ? hb + 10 : 26, 255);
    SDL_RenderFillRect(ren, &b_m_clip);
    draw_brown_border_rect(ren, &b_m_clip, m == 1, m != 1);
    draw_text(ren, "Clip color", b_m_clip.x + 26, wy + 45, c_white);

    SDL_Rect b_m_peak = { wx + 292, wy + 38, 132, 26 };
    SDL_SetRenderDrawColor(ren, (m == 2) ? hr + 24 : 32, (m == 2) ? hg + 16 : 28, (m == 2) ? hb + 10 : 26, 255);
    SDL_RenderFillRect(ren, &b_m_peak);
    draw_brown_border_rect(ren, &b_m_peak, m == 2, m != 2);
    draw_text(ren, "Peaks Color", b_m_peak.x + 22, wy + 45, c_white);

    // 4x6 Grid of 24 Studio Colors (text sentence below the 3 buttons removed as requested)
    int grid_x = wx + 16;
    int grid_y = wy + 74;
    int cell_w = 98;
    int cell_h = 38;
    int gap_x  = 6;
    int gap_y  = 6;
    (void)c_muted;

    for (int c = 0; c < NUM_STUDIO_COLORS; c++) {
        int col = c % 4;
        int row = c / 4;
        int cx = grid_x + col * (cell_w + gap_x);
        int cy = grid_y + row * (cell_h + gap_y);

        SDL_Rect sw = { cx, cy, cell_w, cell_h };
        if (m == 2) {
            SDL_SetRenderDrawColor(ren, g_studio_pal_peak[c][0], g_studio_pal_peak[c][1], g_studio_pal_peak[c][2], 255);
        } else {
            SDL_SetRenderDrawColor(ren, g_studio_pal_hdr[c][0], g_studio_pal_hdr[c][1], g_studio_pal_hdr[c][2], 255);
        }
        SDL_RenderFillRect(ren, &sw);
        draw_brown_border_rect(ren, &sw, false, false);
    }

    int by = wy + wh - 36;
    SDL_Rect b_close = { wx + ww - 104, by, 88, 26 };
    SDL_SetRenderDrawColor(ren, 48, 42, 38, 255);
    SDL_RenderFillRect(ren, &b_close);
    draw_brown_border_rect(ren, &b_close, false, false);
    draw_text(ren, "Close", b_close.x + 24, by + 7, c_white);
    (void)c_accent;
}

// ================= SELECTED CLIP PROPERTY DETAILS WINDOW (CTRL+I, EXPANDABLE + MEDIA STREAM SPECS) =================
typedef struct {
    bool is_open;
    SDL_Rect win_rect;
    bool is_dragging;
    int drag_off_x;
    int drag_off_y;
    bool is_expanded;
    SDL_Rect saved_norm_rect;
    int resize_edge;
    int resize_start_mx;
    int resize_start_my;
    SDL_Rect resize_orig_rect;

    // Cached probed FFmpeg media stream properties for the currently inspected clip
    char probed_path[512];
    bool has_audio_stream;
    int  audio_sample_rate_hz;
    int  audio_bitrate_kbps;
    int  audio_channels;
    bool has_video_stream;
    int  video_width;
    int  video_height;
    double video_fps;
    int  video_bitrate_kbps;
} ClipPropertiesWindowState;

static ClipPropertiesWindowState g_clip_props = {
    false,
    { 310, 86, 650, 448 },
    false, 0, 0,
    false,
    { 310, 86, 650, 448 },
    0, 0, 0,
    { 310, 86, 650, 448 },
    "",
    false, 48000, 0, 2,
    false, 0, 0, 0.0, 0
};

// Probes original media file with FFmpeg to retrieve exact Video (FPS, Resolution, Video bitrate kbps)
// and Audio (Audio bitrate kbps, Sample rate Hz) metadata. Cached by file path for zero frame overhead.
static void probe_clip_media_stream_specs(AudioItem *it, ProjectTab *p) {
    if (!it) return;
    if (it->file_path[0] != '\0' && strcmp(g_clip_props.probed_path, it->file_path) == 0) {
        return;
    }
    strncpy(g_clip_props.probed_path, it->file_path, sizeof(g_clip_props.probed_path) - 1);
    g_clip_props.probed_path[sizeof(g_clip_props.probed_path) - 1] = '\0';

    g_clip_props.has_audio_stream     = true;
    g_clip_props.audio_sample_rate_hz = SAMPLE_RATE;
    g_clip_props.audio_bitrate_kbps   = (SAMPLE_RATE * 2 * 24) / 1000;
    g_clip_props.audio_channels       = 2;
    g_clip_props.has_video_stream     = it->has_video;
    g_clip_props.video_width          = (p && p->video.width > 0) ? p->video.width : 0;
    g_clip_props.video_height         = (p && p->video.height > 0) ? p->video.height : 0;
    g_clip_props.video_fps            = 0.0;
    g_clip_props.video_bitrate_kbps   = 0;

    if (!it->file_path[0]) return;

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, it->file_path, NULL, NULL) < 0) return;
    if (avformat_find_stream_info(fmt, NULL) >= 0) {
        int a_idx = -1, v_idx = -1;
        for (unsigned int s = 0; s < fmt->nb_streams; s++) {
            AVStream *st = fmt->streams[s];
            if (!st || !st->codecpar) continue;
            if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && a_idx < 0) {
                a_idx = (int)s;
            } else if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && v_idx < 0 &&
                       !(st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
                v_idx = (int)s;
            }
        }
        if (v_idx < 0) {
            for (unsigned int s = 0; s < fmt->nb_streams; s++) {
                if (fmt->streams[s]->codecpar && fmt->streams[s]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                    v_idx = (int)s;
                    break;
                }
            }
        }

        int64_t container_kbps = (fmt->bit_rate > 0) ? (fmt->bit_rate / 1000) : 0;

        if (a_idx >= 0) {
            AVCodecParameters *ac = fmt->streams[a_idx]->codecpar;
            g_clip_props.has_audio_stream = true;
            if (ac->sample_rate > 0) g_clip_props.audio_sample_rate_hz = ac->sample_rate;
            if (ac->ch_layout.nb_channels > 0) g_clip_props.audio_channels = ac->ch_layout.nb_channels;
            if (ac->bit_rate > 0) {
                g_clip_props.audio_bitrate_kbps = (int)(ac->bit_rate / 1000);
            } else if (ac->bits_per_coded_sample > 0 && ac->sample_rate > 0) {
                int ch = (ac->ch_layout.nb_channels > 0) ? ac->ch_layout.nb_channels : 2;
                g_clip_props.audio_bitrate_kbps = (ac->sample_rate * ch * ac->bits_per_coded_sample) / 1000;
            } else if (v_idx < 0 && container_kbps > 0) {
                g_clip_props.audio_bitrate_kbps = (int)container_kbps;
            }
        } else {
            g_clip_props.has_audio_stream = false;
        }

        if (v_idx >= 0) {
            AVStream *vst = fmt->streams[v_idx];
            AVCodecParameters *vc = vst->codecpar;
            g_clip_props.has_video_stream = true;
            if (vc->width > 0)  g_clip_props.video_width  = vc->width;
            if (vc->height > 0) g_clip_props.video_height = vc->height;

            double fps = 0.0;
            if (vst->avg_frame_rate.den > 0 && vst->avg_frame_rate.num > 0) {
                fps = av_q2d(vst->avg_frame_rate);
            }
            if (fps <= 0.01 && vst->r_frame_rate.den > 0 && vst->r_frame_rate.num > 0) {
                fps = av_q2d(vst->r_frame_rate);
            }
            g_clip_props.video_fps = (fps > 0.01) ? fps : 25.0;

            if (vc->bit_rate > 0) {
                g_clip_props.video_bitrate_kbps = (int)(vc->bit_rate / 1000);
            } else if (container_kbps > 0) {
                int a_kb = g_clip_props.has_audio_stream ? g_clip_props.audio_bitrate_kbps : 0;
                int est_v = (int)container_kbps - a_kb;
                g_clip_props.video_bitrate_kbps = (est_v > 0) ? est_v : (int)container_kbps;
            }
        } else {
            g_clip_props.has_video_stream = false;
        }
    }
    avformat_close_input(&fmt);
}

AudioItem* get_active_video_clip_at_playhead(ProjectTab *p);
static double get_project_step_fps(ProjectTab *p) {
    if (p) {
        AudioItem *vc = get_active_video_clip_at_playhead(p);
        if (vc) {
            probe_clip_media_stream_specs(vc, p);
            if (g_clip_props.video_fps > 1.0) return g_clip_props.video_fps;
        }
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].has_video) {
                probe_clip_media_stream_specs(&p->items[i], p);
                if (g_clip_props.video_fps > 1.0) return g_clip_props.video_fps;
                break;
            }
        }
    }
    double exp_fps = atof(g_export.mp4_fps_str);
    return (exp_fps >= 1.0 && exp_fps <= 240.0) ? exp_fps : 25.0;
}

static void open_clip_properties_window(ProjectTab *p, int win_w, int win_h) {
    (void)p;
    g_clip_props.probed_path[0] = '\0';
    g_clip_props.win_rect.w = 600;
    g_clip_props.win_rect.h = 370;
    g_clip_props.win_rect.x = (win_w - g_clip_props.win_rect.w) / 2;
    g_clip_props.win_rect.y = (win_h - g_clip_props.win_rect.h) / 2;
    g_clip_props.is_open = true;
}

static void render_clip_properties_window(SDL_Renderer *ren, ProjectTab *p) {
    if (!g_clip_props.is_open || !p) return;
    SDL_Rect *wr = &g_clip_props.win_rect;

    int sel_idx = -1;
    int sel_count = 0;
    double sel_min_s = 1e18;
    double sel_max_e = 0.0;
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].is_selected) {
            if (sel_idx < 0) sel_idx = i;
            sel_count++;
            if (p->items[i].start_time < sel_min_s) sel_min_s = p->items[i].start_time;
            double ie = p->items[i].start_time + p->items[i].duration;
            if (ie > sel_max_e) sel_max_e = ie;
        }
    }
    if (sel_idx < 0 && g_last_clicked_item_idx >= 0 && g_last_clicked_item_idx < p->num_items) {
        sel_idx = g_last_clicked_item_idx;
        sel_count = 1;
        sel_min_s = p->items[sel_idx].start_time;
        sel_max_e = sel_min_s + p->items[sel_idx].duration;
    }

    // Prepare all display strings and automatically expand wr->w horizontally so all text fits completely!
    char row_name[512] = "", row_file[512] = "", row_trk[512] = "", row_st[512] = "";
    char row_end[512] = "", row_dur[512] = "", row_off[512] = "", row_aud[512] = "";
    char row_vid[512] = "", row_gain[512] = "", row_fade[512] = "", row_grp[512] = "";
    int col_i = 0;
    int tab = get_valid_tab_idx();

    if (sel_idx >= 0 && sel_idx < p->num_items) {
        AudioItem *it = &p->items[sel_idx];
        probe_clip_media_stream_specs(it, p);
        int trk = it->track_idx;
        const char *tname = (trk >= 0 && trk < p->num_tracks) ? p->track_names[trk] : "Track";
        col_i = g_item_color_idx[tab][sel_idx] % NUM_STUDIO_COLORS;
        if (col_i < 0) col_i = 0;

        char st_tc[32], end_tc[32], dur_tc[32], off_tc[32], tot_tc[32];
        format_timecode(it->start_time, st_tc, sizeof(st_tc));
        format_timecode(it->start_time + it->duration, end_tc, sizeof(end_tc));
        format_timecode(it->duration, dur_tc, sizeof(dur_tc));
        format_timecode(it->offset_in_file, off_tc, sizeof(off_tc));
        format_timecode(it->file_total_duration, tot_tc, sizeof(tot_tc));

        if (sel_count > 1) snprintf(row_name, sizeof(row_name), "%s  (+%d more selected)", it->label, sel_count - 1);
        else snprintf(row_name, sizeof(row_name), "%s", it->label);

        snprintf(row_file, sizeof(row_file), "%s", it->file_path[0] ? it->file_path : "(Internal Recording)");
        snprintf(row_trk, sizeof(row_trk), "Track %d - %s", trk + 1, tname);
        snprintf(row_st, sizeof(row_st), "%s  (%.3f sec)", st_tc, it->start_time);
        snprintf(row_end, sizeof(row_end), "%s  (%.3f sec)", end_tc, it->start_time + it->duration);
        if (sel_count > 1) {
            snprintf(row_dur, sizeof(row_dur), "%s  (%.3f sec | Selection Span: %.3f sec)",
                     dur_tc, it->duration, sel_max_e - sel_min_s);
        } else {
            snprintf(row_dur, sizeof(row_dur), "%s  (%.3f sec)", dur_tc, it->duration);
        }
        snprintf(row_off, sizeof(row_off), "%s  |  Total File Length: %s", off_tc, tot_tc);

        if (g_clip_props.has_audio_stream || it->peak_count > 0) {
            int a_kbps = (g_clip_props.audio_bitrate_kbps > 0) ? g_clip_props.audio_bitrate_kbps : 1536;
            int a_sr   = (g_clip_props.audio_sample_rate_hz > 0) ? g_clip_props.audio_sample_rate_hz : SAMPLE_RATE;
            snprintf(row_aud, sizeof(row_aud),
                     "Audio Bitrate: %d kbps   |   Sample Rate: %d Hz   (%d ch)",
                     a_kbps, a_sr, g_clip_props.audio_channels);
        } else {
            snprintf(row_aud, sizeof(row_aud), "Not available (Video-only clip)");
        }

        if (it->has_video || g_clip_props.has_video_stream) {
            int vw = (g_clip_props.video_width > 0) ? g_clip_props.video_width : ((p->video.width > 0) ? p->video.width : 1920);
            int vh = (g_clip_props.video_height > 0) ? g_clip_props.video_height : ((p->video.height > 0) ? p->video.height : 1080);
            double vfps = (g_clip_props.video_fps > 0.01) ? g_clip_props.video_fps : 25.0;
            int vkbps = (g_clip_props.video_bitrate_kbps > 0) ? g_clip_props.video_bitrate_kbps : 2048;
            snprintf(row_vid, sizeof(row_vid),
                     "Frame Rate: %.2f fps  |  Resolution: %dx%d  |  Bitrate: %d kbps",
                     vfps, vw, vh, vkbps);
        } else {
            snprintf(row_vid, sizeof(row_vid), "Not available (Audio-only clip)");
        }

        float trk_db = (trk >= 0 && trk < p->num_tracks) ? p->track_db[trk] : 0.0f;
        snprintf(row_gain, sizeof(row_gain), "Clip: %+.1f dB  |  Track: %+.1f dB  |  Muted: %s",
                 g_item_vol_db[tab][sel_idx], trk_db, g_item_muted[tab][sel_idx] ? "YES" : "NO");
        snprintf(row_fade, sizeof(row_fade), "Fade-In: %.3f sec   |   Fade-Out: %.3f sec",
                 get_item_fade_in(p, sel_idx), get_item_fade_out(p, sel_idx));
        if (g_item_group_id[tab][sel_idx] > 0) {
            snprintf(row_grp, sizeof(row_grp), "Group #%d", g_item_group_id[tab][sel_idx]);
        } else {
            snprintf(row_grp, sizeof(row_grp), "Ungrouped");
        }

        const char *all_rows[] = {
            row_name, row_file, row_trk, row_st, row_end, row_dur,
            row_off, row_aud, row_vid, row_gain, row_fade, row_grp
        };
        int max_chars = 0;
        for (int r = 0; r < 12; r++) {
            int l = (int)strlen(all_rows[r]);
            if (r == 11) l += 4;
            if (l > max_chars) max_chars = l;
        }
        int auto_w = 188 + max_chars * 8 + 36;
        if (auto_w < 540) auto_w = 540;
        int out_rw = 1280;
        if (auto_w > out_rw - 24) auto_w = out_rw - 24;
        if (g_clip_props.resize_edge == 0 && !g_clip_props.is_expanded) {
            wr->w = auto_w;
            if (wr->x + wr->w > out_rw - 12) {
                wr->x = (out_rw - wr->w) / 2;
                if (wr->x < 12) wr->x = 12;
            }
        }
    }

    if (wr->w < 520) wr->w = 520;
    if (wr->h < 368) wr->h = 368;
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
    draw_text(ren, "Clip properties", wx + 14, wy + 9, c_white);

    // Expand / Restore button [+] / [-] next to [X]
    SDL_Rect exp_btn = { wx + ww - 54, wy + 4, 22, 22 };
    SDL_SetRenderDrawColor(ren, g_clip_props.is_expanded ? 68 : 44, g_clip_props.is_expanded ? 48 : 38, 32, 255);
    SDL_RenderFillRect(ren, &exp_btn);
    draw_brown_border_rect(ren, &exp_btn, g_clip_props.is_expanded, false);
    draw_text(ren, g_clip_props.is_expanded ? "-" : "+", exp_btn.x + 7, exp_btn.y + 4, c_white);

    draw_text(ren, "X", wx + ww - 20, wy + 9, c_white);

    SDL_Rect body_box = { wx + 14, wy + 38, ww - 28, wh - 50 };
    SDL_SetRenderDrawColor(ren, 18, 17, 16, 255);
    SDL_RenderFillRect(ren, &body_box);
    draw_brown_border_rect(ren, &body_box, false, true);

    if (sel_idx < 0 || sel_idx >= p->num_items) {
        draw_text(ren, "No media clip is currently selected.", wx + 28, wy + 80, c_accent);
        draw_text(ren, "Click any clip on the timeline and press Ctrl+I.", wx + 28, wy + 108, c_muted);
    } else {
        AudioItem *it = &p->items[sel_idx];
        int ry = wy + 46;
        int lx = wx + 24;
        int vx = wx + 188;
        SDL_Rect val_clip = { vx, wy + 40, ww - 206, wh - 54 };

        draw_text(ren, "Clip Name:", lx, ry, c_muted);
        draw_text_clipped(ren, row_name, vx, ry, c_accent, &val_clip, NULL);
        ry += 25;

        draw_text(ren, "Source File:", lx, ry, c_muted);
        draw_text_clipped(ren, row_file, vx, ry, c_white, &val_clip, NULL);
        ry += 25;

        draw_text(ren, "Track:", lx, ry, c_muted);
        draw_text_clipped(ren, row_trk, vx, ry, c_white, &val_clip, NULL);
        ry += 25;

        draw_text(ren, "Timeline Start:", lx, ry, c_muted);
        draw_text_clipped(ren, row_st, vx, ry, c_white, &val_clip, NULL);
        ry += 25;

        draw_text(ren, "Timeline End:", lx, ry, c_muted);
        draw_text_clipped(ren, row_end, vx, ry, c_white, &val_clip, NULL);
        ry += 25;

        draw_text(ren, "Clip Duration:", lx, ry, c_muted);
        draw_text_clipped(ren, row_dur, vx, ry, c_accent, &val_clip, NULL);
        ry += 25;

        draw_text(ren, "Source Offset:", lx, ry, c_muted);
        draw_text_clipped(ren, row_off, vx, ry, c_white, &val_clip, NULL);
        ry += 25;

        draw_text(ren, "Audio :", lx, ry, c_muted);
        draw_text_clipped(ren, row_aud, vx, ry, (g_clip_props.has_audio_stream || it->peak_count > 0) ? c_accent : c_muted, &val_clip, NULL);
        ry += 25;

        draw_text(ren, "Video :", lx, ry, c_muted);
        draw_text_clipped(ren, row_vid, vx, ry, (it->has_video || g_clip_props.has_video_stream) ? c_accent : c_white, &val_clip, NULL);
        ry += 25;

        draw_text(ren, "Gain & Mute:", lx, ry, c_muted);
        draw_text_clipped(ren, row_gain, vx, ry, g_item_muted[tab][sel_idx] ? c_accent : c_white, &val_clip, NULL);
        ry += 25;

        draw_text(ren, "Fade In / Out:", lx, ry, c_muted);
        draw_text_clipped(ren, row_fade, vx, ry, c_white, &val_clip, NULL);
        ry += 25;

        draw_text(ren, "Color & Group:", lx, ry, c_muted);
        SDL_Rect c_sw = { vx, ry - 2, 18, 16 };
        SDL_SetRenderDrawColor(ren, g_studio_pal_hdr[col_i][0], g_studio_pal_hdr[col_i][1], g_studio_pal_hdr[col_i][2], 255);
        SDL_RenderFillRect(ren, &c_sw);
        draw_brown_border_rect(ren, &c_sw, false, false);
        draw_text_clipped(ren, row_grp, vx + 26, ry, c_white, &val_clip, NULL);
    }

    // Bottom-right corner resize grip indicator
    SDL_SetRenderDrawColor(ren, 175, 125, 75, 255);
    SDL_RenderDrawLine(ren, wx + ww - 10, wy + wh - 3, wx + ww - 3, wy + wh - 10);
    SDL_RenderDrawLine(ren, wx + ww - 14, wy + wh - 3, wx + ww - 3, wy + wh - 14);
}

// ================= MASTER MUTE (M) & MASTER SOLO (S) ALL TRACKS HELPERS (REQUIREMENT 13) =================
static void toggle_master_mute_all_tracks(ProjectTab *p) {
    if (!p || p->num_tracks <= 0) return;
    bool all_muted = true;
    for (int t = 0; t < p->num_tracks; t++) {
        if (!p->tracks[t].mute) { all_muted = false; break; }
    }
    push_undo_state();
    for (int t = 0; t < p->num_tracks; t++) {
        p->tracks[t].mute = !all_muted;
    }
    record_project_change(!all_muted ? "Muted All Tracks (Master M)" : "Unmuted All Tracks (Master M)", true);
}

static void toggle_master_solo_all_tracks(ProjectTab *p) {
    if (!p || p->num_tracks <= 0) return;
    bool all_soloed = true;
    for (int t = 0; t < p->num_tracks; t++) {
        if (!p->tracks[t].solo) { all_soloed = false; break; }
    }
    push_undo_state();
    for (int t = 0; t < p->num_tracks; t++) {
        p->tracks[t].solo = !all_soloed;
    }
    record_project_change(!all_soloed ? "Soloed All Tracks (Master S)" : "Unsoloed All Tracks (Master S)", true);
}

static void toggle_master_lock_all_tracks(ProjectTab *p) {
    if (!p || p->num_tracks <= 0) return;
    int tab = get_valid_tab_idx();
    bool all_locked = true;
    for (int t = 0; t < p->num_tracks; t++) {
        if (!g_track_locked[tab][t]) { all_locked = false; break; }
    }
    push_undo_state();
    for (int t = 0; t < p->num_tracks; t++) {
        g_track_locked[tab][t] = !all_locked;
    }
    record_project_change(!all_locked ? "Locked All Tracks" : "Unlocked All Tracks", true);
}

static void render_track_lock_popup_window(SDL_Renderer *ren) {
    if (!g_lock_popup_open) return;
    int tab = get_valid_tab_idx();
    ProjectTab *p = get_cur_tab();
    SDL_Rect *wr = &g_lock_win_rect;

    int sel_trk = -1;
    if (p) {
        for (int t = 0; t < p->num_tracks; t++) {
            if (p->track_selected[t]) { sel_trk = t; break; }
        }
        if (sel_trk < 0 && p->num_tracks > 0) {
            sel_trk = (g_last_clicked_track_idx >= 0 && g_last_clicked_track_idx < p->num_tracks) ? g_last_clicked_track_idx : 0;
        }
    }
    char sel_hdr[128];
    if (p && sel_trk >= 0 && sel_trk < p->num_tracks) {
        snprintf(sel_hdr, sizeof(sel_hdr), "Selected Track (%s)", p->track_names[sel_trk]);
    } else {
        snprintf(sel_hdr, sizeof(sel_hdr), "Selected Track");
    }

    const char *all_lbls[3] = {
        "1. All Track Complete Lock",
        "2. Lock Left Right move",
        "3. Lock clip Up Down move"
    };
    const char *sel_lbls[3] = {
        "1. All Clips Complete Lock",
        "2. Lock clip Left Right move",
        "3. Lock clip Up Down move"
    };

    // Expand window automatically until all text fits completely and is visible
    int max_col_px = (int)strlen(sel_hdr) * 8 + 32;
    for (int i = 0; i < 3; i++) {
        int w_a = (int)strlen(all_lbls[i]) * 8 + 60;
        int w_s = (int)strlen(sel_lbls[i]) * 8 + 60;
        if (w_a > max_col_px) max_col_px = w_a;
        if (w_s > max_col_px) max_col_px = w_s;
    }
    int min_win_w = max_col_px * 2 + 48;
    int footer_need_w = (int)strlen("Complete lock blocks clip edge drag, dB gain, color & delete.") * 8 + 140;
    if (min_win_w < footer_need_w) min_win_w = footer_need_w;
    if (wr->w < min_win_w) wr->w = min_win_w;
    if (wr->h < 256) wr->h = 256;

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
    draw_lock_symbol_icon(ren, wx + 12, wy + 8, true, c_accent);
    draw_text(ren, "Track Lock Settings", wx + 30, wy + 9, c_white);
    draw_text(ren, "X", wx + ww - 20, wy + 9, c_white);

    int col_w = (ww - 44) / 2;
    int left_x = wx + 14;
    int right_x = wx + 30 + col_w;
    int box_y = wy + 40;
    int box_h = wh - 86;

    // Left Section Box: All Tracks
    SDL_Rect left_box = { left_x, box_y, col_w, box_h };
    SDL_SetRenderDrawColor(ren, 20, 19, 18, 255);
    SDL_RenderFillRect(ren, &left_box);
    draw_brown_border_rect(ren, &left_box, false, true);
    draw_text(ren, "All Tracks", left_x + 12, box_y + 8, c_accent);

    struct { bool val; const char *lbl; int y; } all_opts[3] = {
        { g_lock_opt_full[tab],       all_lbls[0], box_y + 30 },
        { g_lock_opt_prevent_lr[tab], all_lbls[1], box_y + 66 },
        { g_lock_opt_prevent_ud[tab], all_lbls[2], box_y + 102 }
    };

    for (int i = 0; i < 3; i++) {
        int ry = all_opts[i].y;
        bool chk = all_opts[i].val;
        SDL_Rect row_r = { left_x + 8, ry, col_w - 16, 30 };
        SDL_SetRenderDrawColor(ren, chk ? 48 : 28, chk ? 36 : 26, chk ? 26 : 24, 255);
        SDL_RenderFillRect(ren, &row_r);
        draw_brown_border_rect(ren, &row_r, chk, !chk);

        SDL_Rect cb = { left_x + 16, ry + 6, 18, 18 };
        SDL_SetRenderDrawColor(ren, chk ? hr + 24 : 15, chk ? hg + 16 : 14, chk ? hb + 10 : 13, 255);
        SDL_RenderFillRect(ren, &cb);
        draw_brown_border_rect(ren, &cb, chk, false);
        if (chk) draw_text(ren, "x", cb.x + 5, cb.y + 2, c_white);

        SDL_Rect r_clip = { left_x + 40, ry + 2, col_w - 48, 26 };
        draw_text_clipped(ren, all_opts[i].lbl, left_x + 40, ry + 8, chk ? c_accent : c_white, &r_clip, NULL);
    }

    // Right Section Box: Selected Track
    SDL_Rect right_box = { right_x, box_y, col_w, box_h };
    SDL_SetRenderDrawColor(ren, 20, 19, 18, 255);
    SDL_RenderFillRect(ren, &right_box);
    draw_brown_border_rect(ren, &right_box, false, true);

    SDL_Rect sh_clip = { right_x + 10, box_y + 4, col_w - 20, 20 };
    draw_text_clipped(ren, sel_hdr, right_x + 12, box_y + 8, c_accent, &sh_clip, NULL);

    bool sel_full = (sel_trk >= 0) ? is_track_full_locked_tab(tab, sel_trk) : false;
    bool sel_lr   = (sel_trk >= 0) ? g_track_lock_lr_only[tab][sel_trk] : false;
    bool sel_ud   = (sel_trk >= 0) ? g_track_lock_ud_only[tab][sel_trk] : false;

    struct { bool val; const char *lbl; int y; } sel_opts[3] = {
        { sel_full, sel_lbls[0], box_y + 30 },
        { sel_lr,   sel_lbls[1], box_y + 66 },
        { sel_ud,   sel_lbls[2], box_y + 102 }
    };

    for (int i = 0; i < 3; i++) {
        int ry = sel_opts[i].y;
        bool chk = sel_opts[i].val;
        SDL_Rect row_r = { right_x + 8, ry, col_w - 16, 30 };
        SDL_SetRenderDrawColor(ren, chk ? 48 : 28, chk ? 36 : 26, chk ? 26 : 24, 255);
        SDL_RenderFillRect(ren, &row_r);
        draw_brown_border_rect(ren, &row_r, chk, !chk);

        SDL_Rect cb = { right_x + 16, ry + 6, 18, 18 };
        SDL_SetRenderDrawColor(ren, chk ? hr + 24 : 15, chk ? hg + 16 : 14, chk ? hb + 10 : 13, 255);
        SDL_RenderFillRect(ren, &cb);
        draw_brown_border_rect(ren, &cb, chk, false);
        if (chk) draw_text(ren, "x", cb.x + 5, cb.y + 2, c_white);

        SDL_Rect r_clip = { right_x + 40, ry + 2, col_w - 48, 26 };
        draw_text_clipped(ren, sel_opts[i].lbl, right_x + 40, ry + 8, chk ? c_accent : c_white, &r_clip, NULL);
    }

    int by = wy + wh - 38;
    draw_text(ren, "Complete lock blocks clip edge drag, dB gain, color & delete.", wx + 16, by + 8, c_muted);
    SDL_Rect b_ok = { wx + ww - 98, by, 82, 26 };
    SDL_SetRenderDrawColor(ren, hr + 22, hg + 14, hb + 8, 255);
    SDL_RenderFillRect(ren, &b_ok);
    draw_brown_border_rect(ren, &b_ok, true, false);
    draw_text(ren, "Close", b_ok.x + 21, by + 6, c_white);
}

// Draws crisp Play (triangle) or Pause (double bars) symbol only (Requirement 12)
static inline void draw_play_pause_symbol_icon(SDL_Renderer *ren, int cx, int cy, bool is_playing, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    if (is_playing) {
        // Pause symbol: two crisp vertical bars
        SDL_Rect b1 = { cx + 2, cy + 2, 4, 12 };
        SDL_Rect b2 = { cx + 9, cy + 2, 4, 12 };
        SDL_RenderFillRect(ren, &b1);
        SDL_RenderFillRect(ren, &b2);
    } else {
        // Play symbol: filled right-pointing triangle
        for (int dy = 0; dy <= 12; dy++) {
            int half = (dy <= 6) ? dy : (12 - dy);
            int w = half * 2;
            if (w < 1) w = 1;
            SDL_RenderDrawLine(ren, cx + 3, cy + 2 + dy, cx + 3 + w, cy + 2 + dy);
        }
    }
}

// Draws crisp Speaker / Volume symbol icon at (cx, cy) for the top-bar App Hearing Volume button
static inline void draw_volume_speaker_symbol_icon(SDL_Renderer *ren, int cx, int cy, bool is_muted, SDL_Color col) {
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    // Speaker back box
    SDL_Rect box = { cx + 1, cy + 5, 4, 6 };
    SDL_RenderFillRect(ren, &box);
    // Speaker cone flare
    for (int dx = 0; dx < 5; dx++) {
        SDL_RenderDrawLine(ren, cx + 4 + dx, cy + 5 - dx, cx + 4 + dx, cy + 10 + dx);
    }
    if (is_muted) {
        // Draw small mute X on right of speaker cone
        SDL_SetRenderDrawColor(ren, 235, 75, 75, 255);
        SDL_RenderDrawLine(ren, cx + 11, cy + 5, cx + 15, cy + 10);
        SDL_RenderDrawLine(ren, cx + 15, cy + 5, cx + 11, cy + 10);
    } else {
        // Sound waves on right of speaker cone
        SDL_RenderDrawLine(ren, cx + 11, cy + 5, cx + 12, cy + 6);
        SDL_RenderDrawLine(ren, cx + 12, cy + 6, cx + 12, cy + 9);
        SDL_RenderDrawLine(ren, cx + 12, cy + 9, cx + 11, cy + 10);

        SDL_RenderDrawLine(ren, cx + 13, cy + 3, cx + 15, cy + 5);
        SDL_RenderDrawLine(ren, cx + 15, cy + 5, cx + 15, cy + 10);
        SDL_RenderDrawLine(ren, cx + 15, cy + 10, cx + 13, cy + 12);
    }
}

// ================= SETTINGS -> MOUSE POPUP WINDOW (REQUIREMENT 9) =================
static void render_mouse_settings_window(SDL_Renderer *ren) {
    if (!g_mouse_settings_open) return;
    SDL_Rect *wr = &g_mouse_win_rect;
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
    draw_text(ren, "Settings - Mouse", wx + 14, wy + 9, c_white);
    draw_text(ren, "X", wx + ww - 20, wy + 9, c_white);

    // Option 1: Click on timeline, Playhead move. [default] (no checkbox required)
    int r1_y = wy + 46;
    SDL_Rect row1 = { wx + 18, r1_y, ww - 36, 32 };
    SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
    SDL_RenderFillRect(ren, &row1);
    draw_brown_border_rect(ren, &row1, false, true);
    draw_text(ren, "Click on timeline, Playhead move. [default]", wx + 28, r1_y + 9, c_accent);

    // Option 2: [x] Click on empty space, Playhead move.
    int r2_y = wy + 88;
    SDL_Rect row2 = { wx + 18, r2_y, ww - 36, 32 };
    SDL_SetRenderDrawColor(ren, g_mouse_click_empty_moves_playhead ? 48 : 30,
                                g_mouse_click_empty_moves_playhead ? 36 : 28,
                                g_mouse_click_empty_moves_playhead ? 26 : 26, 255);
    SDL_RenderFillRect(ren, &row2);
    draw_brown_border_rect(ren, &row2, g_mouse_click_empty_moves_playhead, !g_mouse_click_empty_moves_playhead);
    SDL_Rect cb2 = { wx + 28, r2_y + 7, 18, 18 };
    SDL_SetRenderDrawColor(ren, g_mouse_click_empty_moves_playhead ? hr + 24 : 15,
                                g_mouse_click_empty_moves_playhead ? hg + 16 : 14,
                                g_mouse_click_empty_moves_playhead ? hb + 10 : 13, 255);
    SDL_RenderFillRect(ren, &cb2);
    draw_brown_border_rect(ren, &cb2, g_mouse_click_empty_moves_playhead, false);
    if (g_mouse_click_empty_moves_playhead) draw_text(ren, "x", cb2.x + 5, cb2.y + 2, c_white);
    draw_text(ren, "Click on empty space, Playhead move.", wx + 56, r2_y + 9, c_white);

    // Option 3: [x] Click universal, Playhead move.
    int r3_y = wy + 130;
    SDL_Rect row3 = { wx + 18, r3_y, ww - 36, 32 };
    SDL_SetRenderDrawColor(ren, g_mouse_click_universal_moves_playhead ? 48 : 30,
                                g_mouse_click_universal_moves_playhead ? 36 : 28,
                                g_mouse_click_universal_moves_playhead ? 26 : 26, 255);
    SDL_RenderFillRect(ren, &row3);
    draw_brown_border_rect(ren, &row3, g_mouse_click_universal_moves_playhead, !g_mouse_click_universal_moves_playhead);
    SDL_Rect cb3 = { wx + 28, r3_y + 7, 18, 18 };
    SDL_SetRenderDrawColor(ren, g_mouse_click_universal_moves_playhead ? hr + 24 : 15,
                                g_mouse_click_universal_moves_playhead ? hg + 16 : 14,
                                g_mouse_click_universal_moves_playhead ? hb + 10 : 13, 255);
    SDL_RenderFillRect(ren, &cb3);
    draw_brown_border_rect(ren, &cb3, g_mouse_click_universal_moves_playhead, false);
    if (g_mouse_click_universal_moves_playhead) draw_text(ren, "x", cb3.x + 5, cb3.y + 2, c_white);
    draw_text(ren, "Click universal, Playhead move.", wx + 56, r3_y + 9, c_white);

    // Close / OK button
    int by = wy + wh - 40;
    draw_text(ren, "Saved automatically", wx + 20, by + 8, c_muted);
    SDL_Rect b_ok = { wx + ww - 102, by, 84, 28 };
    SDL_SetRenderDrawColor(ren, hr + 22, hg + 14, hb + 8, 255);
    SDL_RenderFillRect(ren, &b_ok);
    draw_brown_border_rect(ren, &b_ok, true, false);
    draw_text(ren, "OK", b_ok.x + 32, by + 8, c_white);
}

// ================= TINY TIME CLOCK MANUAL ENTRY POPUP (REQUIREMENT 14: hh:mm:ss OR hhmmss) =================
static void open_time_jump_popup(ProjectTab *p, int win_w, int win_h) {
    if (!p) return;
    int sec_total = (int)(p->playhead_seconds + 0.5);
    if (sec_total < 0) sec_total = 0;
    int hh = sec_total / 3600;
    int mm = (sec_total % 3600) / 60;
    int ss = sec_total % 60;
    snprintf(g_time_jump_buf, sizeof(g_time_jump_buf), "%02d:%02d:%02d", hh, mm, ss);
    g_time_jump_rect.x = (win_w - g_time_jump_rect.w) / 2;
    g_time_jump_rect.y = TABBAR_HEIGHT + TOPBAR_HEIGHT + 6;
    if (g_time_jump_rect.y + g_time_jump_rect.h > win_h) {
        g_time_jump_rect.y = (win_h - g_time_jump_rect.h) / 2;
    }
    g_time_jump_open = true;
    SDL_StartTextInput();
}

// Parses both "hh:mm:ss" (or "mm:ss" or "hh:mm:ss.ms") AND direct "hhmmss" digits without colons!
static bool parse_manual_time_entry_to_seconds(const char *str, double *out_sec) {
    if (!str || !out_sec) return false;
    while (*str == ' ' || *str == '	') str++;
    if (!*str) return false;

    if (strchr(str, ':') != NULL) {
        double parts[3] = { 0.0, 0.0, 0.0 };
        int n_parts = 0;
        const char *cur = str;
        while (*cur && n_parts < 3) {
            parts[n_parts++] = atof(cur);
            const char *col = strchr(cur, ':');
            if (!col) break;
            cur = col + 1;
        }
        double total = 0.0;
        if (n_parts == 3) total = parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];
        else if (n_parts == 2) total = parts[0] * 60.0 + parts[1];
        else total = parts[0];
        if (total < 0.0) total = 0.0;
        *out_sec = total;
        return true;
    }

    // Direct digits format: hhmmss (e.g. "010203" -> 1h 2m 3s, "0230" -> 2m 30s, "45" -> 45s) or decimal seconds
    char digits[16] = {0};
    int nd = 0;
    bool has_dot = false;
    double frac = 0.0;
    const char *dot = strchr(str, '.');
    if (dot) {
        has_dot = true;
        frac = atof(dot);
    }
    for (const char *p = str; *p && *p != '.'; p++) {
        if (isdigit((unsigned char)*p) && nd < 12) {
            digits[nd++] = *p;
        }
    }
    digits[nd] = '\0';
    if (nd == 0 && !has_dot) return false;

    long val = atol(digits);
    int hh = (int)(val / 10000L);
    int mm = (int)((val / 100L) % 100L);
    int ss = (int)(val % 100L);
    double total = (double)hh * 3600.0 + (double)mm * 60.0 + (double)ss + frac;
    if (total < 0.0) total = 0.0;
    *out_sec = total;
    return true;
}

static void commit_time_jump_popup(ProjectTab *p, int view_w) {
    if (!p) return;
    double target_sec = 0.0;
    if (parse_manual_time_entry_to_seconds(g_time_jump_buf, &target_sec)) {
        daw_set_playhead_seconds(p, target_sec);
        ensure_playhead_visible_in_viewport(p, view_w);
        record_project_change("Playhead Jumped to Manual Time", false);
    }
    g_time_jump_open = false;
    SDL_StopTextInput();
}

static void render_time_jump_popup_window(SDL_Renderer *ren) {
    if (!g_time_jump_open) return;
    SDL_Rect *wr = &g_time_jump_rect;
    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
    SDL_Color c_white  = { 228, 228, 228, 255 };
    SDL_Color c_muted  = { 155, 150, 145, 255 };
    SDL_Color c_accent = get_theme_accent_sdl_color();

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Rect shadow = { wx + 5, wy + 5, ww, wh };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 175);
    SDL_RenderFillRect(ren, &shadow);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
    SDL_RenderFillRect(ren, wr);
    draw_brown_border_rect(ren, wr, true, false);

    uint8_t hr, hg, hb;
    get_theme_surface_rgb(32, 0.22f, &hr, &hg, &hb);
    SDL_Rect hdr = { wx, wy, ww, 26 };
    SDL_SetRenderDrawColor(ren, hr, hg, hb, 255);
    SDL_RenderFillRect(ren, &hdr);
    draw_brown_border_rect(ren, &hdr, false, false);
    draw_text(ren, "Jump to Time (hh:mm:ss / hhmmss)", wx + 8, wy + 7, c_white);
    draw_text(ren, "X", wx + ww - 18, wy + 7, c_white);

    draw_text(ren, "Time:", wx + 14, wy + 44, c_muted);
    SDL_Rect tbox = { wx + 62, wy + 38, ww - 76, 26 };
    SDL_SetRenderDrawColor(ren, 15, 14, 13, 255);
    SDL_RenderFillRect(ren, &tbox);
    draw_brown_border_rect(ren, &tbox, true, false);
    daw_draw_text_edit_overlay(ren, g_time_jump_buf, tbox.x + 8, tbox.y, tbox.h, tbox.x + tbox.w - 4);
    draw_text(ren, g_time_jump_buf, tbox.x + 8, tbox.y + 7, c_accent);

    int by = wy + wh - 36;
    SDL_Rect b_ok = { wx + ww - 166, by, 72, 26 };
    SDL_SetRenderDrawColor(ren, hr + 22, hg + 14, hb + 8, 255);
    SDL_RenderFillRect(ren, &b_ok);
    draw_brown_border_rect(ren, &b_ok, true, false);
    draw_text(ren, "OK", b_ok.x + 28, by + 7, c_white);

    SDL_Rect b_can = { wx + ww - 86, by, 72, 26 };
    SDL_SetRenderDrawColor(ren, 44, 40, 38, 255);
    SDL_RenderFillRect(ren, &b_can);
    draw_brown_border_rect(ren, &b_can, false, false);
    draw_text(ren, "Cancel", b_can.x + 12, by + 7, c_white);
}

// ================= REQUIREMENT 2: TINY RANGE POPUP WINDOW NEAR RANGE BUTTON =================
static void commit_range_popup_edits(void) {
    double cur_s = get_range_start();
    double cur_e = get_range_end();
    double new_s = cur_s;
    double new_e = cur_e;

    if (g_range_popup.active_field == 1 || g_range_popup.active_field == 0) {
        double parsed_s = 0.0;
        if (parse_manual_time_entry_to_seconds(g_range_popup.start_buf, &parsed_s)) {
            new_s = parsed_s;
        }
    }
    if (g_range_popup.active_field == 2 || g_range_popup.active_field == 0) {
        double parsed_e = 0.0;
        if (parse_manual_time_entry_to_seconds(g_range_popup.end_buf, &parsed_e)) {
            new_e = parsed_e;
        }
    }
    if (new_s < 0.0) new_s = 0.0;
    if (new_e <= new_s + 0.05) new_e = new_s + 0.05;

    if (fabs(new_s - cur_s) > 1e-4 || fabs(new_e - cur_e) > 1e-4) {
        push_undo_state();
        set_tab_range(new_s, new_e, true);
        record_project_change("Updated Range Start/End Timings", true);
    }
    g_range_popup.active_field = 0;
    SDL_StopTextInput();
    sync_range_popup_buffers_from_tab();
}

static void render_range_popup_window(SDL_Renderer *ren, int win_w) {
    if (!is_range_active()) {
        if (g_range_popup.is_open) {
            g_range_popup.is_open = false;
            g_range_popup.active_field = 0;
            SDL_StopTextInput();
        }
        return;
    }
    if (!g_range_popup.is_open || !ren) return;

    // Keep buffers synced with live range whenever not actively typing in that field
    sync_range_popup_buffers_from_tab();

    SDL_Rect *wr = &g_range_popup.win_rect;
    if (!g_range_popup.is_dragging && (wr->x + wr->w > win_w - 6 || wr->x < 8)) {
        wr->x = win_w - wr->w - 14;
        if (wr->x < 8) wr->x = 8;
        wr->y = TABBAR_HEIGHT + TOPBAR_HEIGHT + 4;
    }

    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
    SDL_Color c_white  = { 228, 228, 228, 255 };
    SDL_Color c_muted  = { 155, 150, 145, 255 };
    SDL_Color c_accent = get_theme_accent_sdl_color();

    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Rect shadow = { wx + 5, wy + 5, ww, wh };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 170);
    SDL_RenderFillRect(ren, &shadow);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(ren, 26, 24, 22, 255);
    SDL_RenderFillRect(ren, wr);
    draw_brown_border_rect(ren, wr, true, false);

    uint8_t hr, hg, hb;
    get_theme_surface_rgb(32, 0.22f, &hr, &hg, &hb);
    SDL_Rect hdr = { wx, wy, ww, 26 };
    SDL_SetRenderDrawColor(ren, hr, hg, hb, 255);
    SDL_RenderFillRect(ren, &hdr);
    draw_brown_border_rect(ren, &hdr, false, false);
    draw_text(ren, "Range Timings", wx + 10, wy + 7, c_white);
    draw_text(ren, "X", wx + ww - 18, wy + 7, c_white);

    // Row 1: Start Time (editable)
    int r1_y = wy + 34;
    draw_text(ren, "Start:", wx + 12, r1_y + 7, c_white);
    SDL_Rect box_s = { wx + 92, r1_y, ww - 104, 24 };
    bool ed_s = (g_range_popup.active_field == 1);
    SDL_SetRenderDrawColor(ren, 15, 14, 13, 255);
    SDL_RenderFillRect(ren, &box_s);
    draw_brown_border_rect(ren, &box_s, ed_s, false);
    if (ed_s) {
        daw_draw_text_edit_overlay(ren, g_range_popup.start_buf, box_s.x + 8, box_s.y, box_s.h, box_s.x + box_s.w - 4);
    }
    draw_text(ren, g_range_popup.start_buf, box_s.x + 8, r1_y + 6, ed_s ? c_accent : c_white);

    // Row 2: End Time (editable)
    int r2_y = wy + 64;
    draw_text(ren, "End:", wx + 12, r2_y + 7, c_white);
    SDL_Rect box_e = { wx + 92, r2_y, ww - 104, 24 };
    bool ed_e = (g_range_popup.active_field == 2);
    SDL_SetRenderDrawColor(ren, 15, 14, 13, 255);
    SDL_RenderFillRect(ren, &box_e);
    draw_brown_border_rect(ren, &box_e, ed_e, false);
    if (ed_e) {
        daw_draw_text_edit_overlay(ren, g_range_popup.end_buf, box_e.x + 8, box_e.y, box_e.h, box_e.x + box_e.w - 4);
    }
    draw_text(ren, g_range_popup.end_buf, box_e.x + 8, r2_y + 6, ed_e ? c_accent : c_white);

    // Row 3: Duration (live readout) + [Apply] button
    int r3_y = wy + 96;
    double dur_sec = fmax(0.0, get_range_end() - get_range_start());
    char dur_str[32];
    format_timecode(dur_sec, dur_str, sizeof(dur_str));
    draw_text(ren, "Duration:", wx + 12, r3_y + 7, c_muted);
    draw_text(ren, dur_str, wx + 92, r3_y + 7, c_accent);

    SDL_Rect b_set = { wx + ww - 68, r3_y, 56, 24 };
    SDL_SetRenderDrawColor(ren, hr + 20, hg + 12, hb + 8, 255);
    SDL_RenderFillRect(ren, &b_set);
    draw_brown_border_rect(ren, &b_set, true, false);
    draw_text(ren, "Apply", b_set.x + 8, r3_y + 6, c_white);
}

// ================= REQUIREMENT 4: APP HEARING VOLUME POPUP WINDOW =================
static void render_app_volume_popup_window(SDL_Renderer *ren, ProjectTab *p) {
    if (!g_vol_popup.is_open || !ren || !p) return;
    ensure_app_vol_defaults_initialized();
    int tab = get_valid_tab_idx();

    SDL_Rect *wr = &g_vol_popup.win_rect;
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
    SDL_Rect hdr = { wx, wy, ww, 28 };
    SDL_SetRenderDrawColor(ren, hr, hg, hb, 255);
    SDL_RenderFillRect(ren, &hdr);
    draw_brown_border_rect(ren, &hdr, false, false);
    draw_text(ren, "App Hearing Volume (Monitor Level Only)", wx + 12, wy + 8, c_white);
    draw_text(ren, "X", wx + ww - 20, wy + 8, c_white);

    // 1. Top Section: Master Volume (0 to 100 slider + Reset)
    int m_y = wy + 36;
    SDL_Rect m_box = { wx + 12, m_y, ww - 24, 38 };
    SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
    SDL_RenderFillRect(ren, &m_box);
    draw_brown_border_rect(ren, &m_box, true, false);

    draw_text(ren, "Master Volume", wx + 22, m_y + 13, c_white);

    float m_val = g_app_master_vol[tab];
    if (m_val < 0.0f) m_val = 0.0f;
    if (m_val > 100.0f) m_val = 100.0f;

    int sl_x = wx + 156;
    int sl_w = 168;
    SDL_Rect m_sl_bg = { sl_x, m_y + 12, sl_w, 14 };
    SDL_SetRenderDrawColor(ren, 15, 14, 13, 255);
    SDL_RenderFillRect(ren, &m_sl_bg);
    draw_brown_border_rect(ren, &m_sl_bg, false, false);

    int m_fill_w = (int)((m_val / 100.0f) * (float)(sl_w - 4) + 0.5f);
    if (m_fill_w > 0) {
        SDL_Rect m_sl_fill = { sl_x + 2, m_y + 14, m_fill_w, 10 };
        SDL_SetRenderDrawColor(ren, c_accent.r, c_accent.g, c_accent.b, 255);
        SDL_RenderFillRect(ren, &m_sl_fill);
    }
    int m_knob_x = sl_x + 2 + m_fill_w - 4;
    if (m_knob_x < sl_x) m_knob_x = sl_x;
    if (m_knob_x > sl_x + sl_w - 8) m_knob_x = sl_x + sl_w - 8;
    SDL_Rect m_knob = { m_knob_x, m_y + 10, 8, 18 };
    SDL_SetRenderDrawColor(ren, 235, 235, 235, 255);
    SDL_RenderFillRect(ren, &m_knob);

    char m_val_txt[24];
    if (m_val <= 0.01f) snprintf(m_val_txt, sizeof(m_val_txt), "0 (Mute)");
    else snprintf(m_val_txt, sizeof(m_val_txt), "%d%%", (int)(m_val + 0.5f));
    draw_text(ren, m_val_txt, sl_x + sl_w + 10, m_y + 13, (m_val <= 0.01f) ? c_muted : c_accent);

    SDL_Rect m_rst = { wx + ww - 76, m_y + 7, 56, 24 };
    SDL_SetRenderDrawColor(ren, 52, 42, 34, 255);
    SDL_RenderFillRect(ren, &m_rst);
    draw_brown_border_rect(ren, &m_rst, false, false);
    draw_text(ren, "Reset", m_rst.x + 8, m_y + 13, c_white);

    // 2. Bottom Section: Track Name List (each has 0..100 horizontal slider + Reset)
    int list_y = wy + 82;
    int list_h = wh - 122;
    SDL_Rect list_box = { wx + 12, list_y, ww - 24, list_h };
    SDL_SetRenderDrawColor(ren, 18, 17, 16, 255);
    SDL_RenderFillRect(ren, &list_box);
    draw_brown_border_rect(ren, &list_box, false, true);

    int row_h = 32;
    int content_h = p->num_tracks * row_h + 8;
    int max_sc = (content_h > list_h) ? (content_h - list_h) : 0;
    if (g_vol_popup.scroll_y > max_sc) g_vol_popup.scroll_y = max_sc;
    if (g_vol_popup.scroll_y < 0) g_vol_popup.scroll_y = 0;

    SDL_Rect rows_clip = { list_box.x + 2, list_box.y + 2, list_box.w - 18, list_box.h - 4 };
    SDL_RenderSetClipRect(ren, &rows_clip);
    for (int t = 0; t < p->num_tracks; t++) {
        int ry = list_box.y + 4 + t * row_h - g_vol_popup.scroll_y;
        if (ry + row_h < list_box.y || ry > list_box.y + list_box.h) continue;

        SDL_Rect row_r = { list_box.x + 4, ry, list_box.w - 22, row_h - 2 };
        SDL_SetRenderDrawColor(ren, (t & 1) ? 26 : 22, (t & 1) ? 24 : 20, (t & 1) ? 22 : 18, 255);
        SDL_RenderFillRect(ren, &row_r);
        draw_brown_border_rect(ren, &row_r, false, true);

        char trk_lbl[96];
        snprintf(trk_lbl, sizeof(trk_lbl), "%d. %s", t + 1, p->track_names[t]);
        SDL_Rect name_clip = { row_r.x + 6, ry + 2, 134, row_h - 6 };
        draw_text_clipped(ren, trk_lbl, row_r.x + 8, ry + 9, c_white, &name_clip, &rows_clip);

        float t_val = g_app_track_vol[tab][t];
        if (t_val < 0.0f) t_val = 0.0f;
        if (t_val > 100.0f) t_val = 100.0f;

        SDL_Rect t_sl_bg = { sl_x, ry + 8, sl_w, 14 };
        SDL_SetRenderDrawColor(ren, 14, 13, 12, 255);
        SDL_RenderFillRect(ren, &t_sl_bg);
        draw_brown_border_rect(ren, &t_sl_bg, false, false);

        int t_fill_w = (int)((t_val / 100.0f) * (float)(sl_w - 4) + 0.5f);
        if (t_fill_w > 0) {
            SDL_Rect t_sl_fill = { sl_x + 2, ry + 10, t_fill_w, 10 };
            SDL_SetRenderDrawColor(ren, c_accent.r, c_accent.g, c_accent.b, 255);
            SDL_RenderFillRect(ren, &t_sl_fill);
        }
        int t_knob_x = sl_x + 2 + t_fill_w - 4;
        if (t_knob_x < sl_x) t_knob_x = sl_x;
        if (t_knob_x > sl_x + sl_w - 8) t_knob_x = sl_x + sl_w - 8;
        SDL_Rect t_knob = { t_knob_x, ry + 6, 8, 18 };
        SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
        SDL_RenderFillRect(ren, &t_knob);

        char t_val_txt[24];
        if (t_val <= 0.01f) snprintf(t_val_txt, sizeof(t_val_txt), "0 (Mute)");
        else snprintf(t_val_txt, sizeof(t_val_txt), "%d%%", (int)(t_val + 0.5f));
        draw_text(ren, t_val_txt, sl_x + sl_w + 10, ry + 9, (t_val <= 0.01f) ? c_muted : c_white);

        SDL_Rect t_rst = { wx + ww - 84, ry + 4, 52, 22 };
        SDL_SetRenderDrawColor(ren, 48, 38, 30, 255);
        SDL_RenderFillRect(ren, &t_rst);
        draw_brown_border_rect(ren, &t_rst, false, false);
        draw_text(ren, "Reset", t_rst.x + 6, ry + 9, c_white);
    }
    SDL_RenderSetClipRect(ren, NULL);

    // Vertical Scrollbar for Track Volume List
    SDL_Rect sb_track = { list_box.x + list_box.w - 14, list_box.y + 2, 12, list_box.h - 4 };
    SDL_SetRenderDrawColor(ren, 24, 22, 20, 255);
    SDL_RenderFillRect(ren, &sb_track);
    draw_brown_border_rect(ren, &sb_track, false, true);

    int thumb_h = (content_h > list_h && content_h > 0)
                  ? fmax(24, (sb_track.h * list_h) / content_h)
                  : sb_track.h;
    int thumb_y = sb_track.y + ((max_sc > 0) ? ((g_vol_popup.scroll_y * (sb_track.h - thumb_h)) / max_sc) : 0);
    SDL_Rect sb_thumb = { sb_track.x + 1, thumb_y, sb_track.w - 2, thumb_h };
    SDL_SetRenderDrawColor(ren, 92, 64, 42, 255);
    SDL_RenderFillRect(ren, &sb_thumb);
    draw_brown_border_rect(ren, &sb_thumb, false, false);

    // Bottom info & Close button
    int by = wy + wh - 32;
    draw_text(ren, "Hearing level only (no effect on clip/track dB or export)", wx + 14, by + 6, c_muted);
}

// ================= VIDEO POPUP GLOBAL PIN OUTSIDE APP (REQUIREMENT 7) =================
static bool g_video_ext_dragging = false;
static int  g_video_ext_drag_off_x = 0;
static int  g_video_ext_drag_off_y = 0;
static bool g_video_ext_maximized = false;

static void destroy_external_pinned_video_window(void) {
    g_video_ext_dragging = false;
    g_video_ext_maximized = false;
    if (g_video_ext_tex) {
        SDL_DestroyTexture(g_video_ext_tex);
        g_video_ext_tex = NULL;
    }
    if (g_video_ext_ren) {
        SDL_DestroyRenderer(g_video_ext_ren);
        g_video_ext_ren = NULL;
    }
    if (g_video_ext_win) {
        SDL_DestroyWindow(g_video_ext_win);
        g_video_ext_win = NULL;
    }
    g_video_ext_tex_w = 0;
    g_video_ext_tex_h = 0;
}

static void toggle_video_global_pin_state(ProjectTab *p, SDL_Window *main_win) {
    g_video_pinned_global = !g_video_pinned_global;
    if (!g_video_pinned_global) {
        if (p && g_video_ext_win && !g_video_ext_maximized) {
            int ew = 0, eh = 0;
            SDL_GetWindowSize(g_video_ext_win, &ew, &eh);
            if (ew >= 220 && eh >= 148) {
                p->video.rect.w = ew;
                p->video.rect.h = eh;
            }
        }
        destroy_external_pinned_video_window();
        record_project_change("Video Monitor Unpinned (Returned Inside App)", false);
    } else {
        if (p) {
            p->video.is_dragging = false;
            p->video.is_resizing = false;
        }
        record_project_change("Video Monitor Pinned (Popped Out of App View)", false);
    }
    (void)main_win;
}

static void sync_and_render_external_pinned_video_window(ProjectTab *p, SDL_Window *main_win) {
    // Show the standalone always-on-top OS window immediately whenever Pin is active so it can move freely outside the app view!
    if (!p || !p->video.is_open || !g_video_pinned_global) {
        if (g_video_ext_win) destroy_external_pinned_video_window();
        return;
    }

    if (!g_video_ext_win) {
        int w = (p->video.rect.w >= 240) ? p->video.rect.w : 480;
        int h = (p->video.rect.h >= 160) ? p->video.rect.h : 294;
        int spawn_x = SDL_WINDOWPOS_CENTERED;
        int spawn_y = SDL_WINDOWPOS_CENTERED;
        if (main_win) {
            int mwx = 0, mwy = 0;
            SDL_GetWindowPosition(main_win, &mwx, &mwy);
            spawn_x = mwx + p->video.rect.x;
            spawn_y = mwy + p->video.rect.y;
        }
        g_video_ext_win = SDL_CreateWindow(
            "",
            spawn_x, spawn_y,
            w, h,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALWAYS_ON_TOP
        );
        if (g_video_ext_win) {
            set_daw_window_thumbnail_icon(g_video_ext_win);
            SDL_SetWindowAlwaysOnTop(g_video_ext_win, SDL_TRUE);
#ifdef _WIN32
            SDL_SysWMinfo wmInfo;
            SDL_VERSION(&wmInfo.version);
            if (SDL_GetWindowWMInfo(g_video_ext_win, &wmInfo)) {
                HWND hwnd = wmInfo.info.win.window;
                BOOL darkMode = TRUE;
                DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            }
#endif
            g_video_ext_ren = SDL_CreateRenderer(g_video_ext_win, -1, SDL_RENDERER_ACCELERATED);
        }
    }
    if (!g_video_ext_win || !g_video_ext_ren) return;

    if (g_video_ext_dragging) {
        int gmx = 0, gmy = 0;
        Uint32 mbuttons = SDL_GetGlobalMouseState(&gmx, &gmy);
        if ((mbuttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0) {
            SDL_SetWindowPosition(g_video_ext_win, gmx - g_video_ext_drag_off_x, gmy - g_video_ext_drag_off_y);
        } else {
            g_video_ext_dragging = false;
        }
    }

    int ew = 480, eh = 294;
    SDL_GetWindowSize(g_video_ext_win, &ew, &eh);
    SDL_Color c_white  = { 224, 224, 224, 255 };
    SDL_Color c_muted  = { 140, 140, 140, 255 };
    SDL_Color c_accent = get_theme_accent_sdl_color();

    SDL_SetRenderDrawColor(g_video_ext_ren, 12, 12, 12, 255);
    SDL_RenderClear(g_video_ext_ren);

    // Top control bar inside global pinned window
    SDL_Rect v_bar = { 0, 0, ew, 24 };
    SDL_SetRenderDrawColor(g_video_ext_ren, 36, 36, 36, 255);
    SDL_RenderFillRect(g_video_ext_ren, &v_bar);
    draw_brown_border_rect(g_video_ext_ren, &v_bar, false, false);

    // Pin button (active — click to unpin back into app)
    int pin_btn_x = ew - 68;
    SDL_Rect pin_btn = { pin_btn_x, 3, 18, 18 };
    SDL_SetRenderDrawColor(g_video_ext_ren, 78, 54, 30, 255);
    SDL_RenderFillRect(g_video_ext_ren, &pin_btn);
    draw_brown_border_rect(g_video_ext_ren, &pin_btn, true, false);
    draw_pin_symbol_icon(g_video_ext_ren, pin_btn_x + 4, 5, true, c_accent);

    // Full Screen / Restore button
    int fs_btn_x = ew - 46;
    SDL_Rect fs_btn = { fs_btn_x, 3, 18, 18 };
    SDL_SetRenderDrawColor(g_video_ext_ren, 48, 40, 34, 255);
    SDL_RenderFillRect(g_video_ext_ren, &fs_btn);
    draw_brown_border_rect(g_video_ext_ren, &fs_btn, false, false);
    SDL_SetRenderDrawColor(g_video_ext_ren, 224, 224, 224, 255);
    if (!g_video_ext_maximized) {
        SDL_Rect ico_max = { fs_btn_x + 4, 7, 10, 10 };
        SDL_RenderDrawRect(g_video_ext_ren, &ico_max);
        SDL_RenderDrawLine(g_video_ext_ren, fs_btn_x + 4, 8, fs_btn_x + 13, 8);
    } else {
        SDL_Rect ico_back = { fs_btn_x + 6, 6, 8, 8 };
        SDL_Rect ico_front = { fs_btn_x + 3, 9, 8, 8 };
        SDL_RenderDrawRect(g_video_ext_ren, &ico_back);
        SDL_SetRenderDrawColor(g_video_ext_ren, 48, 40, 34, 255);
        SDL_RenderFillRect(g_video_ext_ren, &ico_front);
        SDL_SetRenderDrawColor(g_video_ext_ren, 224, 224, 224, 255);
        SDL_RenderDrawRect(g_video_ext_ren, &ico_front);
    }

    // Close [X] button
    draw_text(g_video_ext_ren, "X", ew - 18, 7, c_white);

    SDL_Rect v_canvas = { 0, 24, ew, eh - 24 };
    SDL_SetRenderDrawColor(g_video_ext_ren, 0, 0, 0, 255);
    SDL_RenderFillRect(g_video_ext_ren, &v_canvas);

    if (p->video.has_active_frame && p->video.frame &&
        p->video.width > 0 && p->video.height > 0) {
        #ifdef AV_PIX_FMT_YUV420P
        bool ext_yuv = (p->video.frame->format == AV_PIX_FMT_YUV420P
#ifdef AV_PIX_FMT_YUVJ420P
                        || p->video.frame->format == AV_PIX_FMT_YUVJ420P
#endif
                       );
#else
        bool ext_yuv = false;
#endif
        if (!g_video_ext_tex || g_video_ext_tex_w != p->video.width || g_video_ext_tex_h != p->video.height) {
            if (g_video_ext_tex) SDL_DestroyTexture(g_video_ext_tex);
            g_video_ext_tex = SDL_CreateTexture(
                g_video_ext_ren, ext_yuv ? SDL_PIXELFORMAT_IYUV : SDL_PIXELFORMAT_RGB24,
                SDL_TEXTUREACCESS_STREAMING, p->video.width, p->video.height
            );
            g_video_ext_tex_w = p->video.width;
            g_video_ext_tex_h = p->video.height;
        }
        if (g_video_ext_tex) {
            if (ext_yuv && p->video.frame->data[0] && p->video.frame->data[1] && p->video.frame->data[2]) {
                SDL_UpdateYUVTexture(
                    g_video_ext_tex, NULL,
                    p->video.frame->data[0], p->video.frame->linesize[0],
                    p->video.frame->data[1], p->video.frame->linesize[1],
                    p->video.frame->data[2], p->video.frame->linesize[2]
                );
            } else if (p->video.frame_rgb && p->video.frame_rgb->data[0]) {
                SDL_UpdateTexture(g_video_ext_tex, NULL, p->video.frame_rgb->data[0], p->video.frame_rgb->linesize[0]);
            }
            double v_asp = (double)p->video.width / (double)p->video.height;
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
            SDL_RenderCopy(g_video_ext_ren, g_video_ext_tex, NULL, &dst_rect);
        }
    }
    (void)c_muted;
    draw_brown_border_rect(g_video_ext_ren, &v_canvas, false, false);
    SDL_RenderPresent(g_video_ext_ren);
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
            if (is_foc) {
                daw_draw_text_edit_overlay(ren, ctrl->str_val, ibox.x + 8, ibox.y, ibox.h, ibox.x + ibox.w - 4);
            }
            draw_text(ren, ctrl->str_val, ibox.x + 8, cy + 6, c_white);
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

#include <errno.h>
#ifndef AVERROR
#define AVERROR(e) (-(e))
#endif
#ifndef AVERROR_EOF
#define AVERROR_EOF (-541478725)
#endif
static void render_export_popup_window(SDL_Renderer *ren, ProjectTab *p);
static void ensure_recording_directory_exists(const char *dir_path);
static int daw_strcasecmp_custom(const char *a, const char *b);

// Draws a crisp Green Right Mark (checkmark ✓) inside a checkbox rectangle
static inline void draw_green_checkmark_icon(SDL_Renderer *ren, int box_x, int box_y, int box_w, int box_h) {
    int x1 = box_x + 4;
    int y1 = box_y + box_h / 2 + 1;
    int x2 = box_x + box_w / 2 - 1;
    int y2 = box_y + box_h - 5;
    int x3 = box_x + box_w - 4;
    int y3 = box_y + 4;
    SDL_SetRenderDrawColor(ren, 46, 224, 88, 255);
    SDL_RenderDrawLine(ren, x1, y1, x2, y2);
    SDL_RenderDrawLine(ren, x2, y2, x3, y3);
    SDL_RenderDrawLine(ren, x1, y1 - 1, x2, y2 - 1);
    SDL_RenderDrawLine(ren, x2, y2 - 1, x3, y3 - 1);
    SDL_RenderDrawLine(ren, x1 + 1, y1, x2 + 1, y2);
    SDL_RenderDrawLine(ren, x2 + 1, y2, x3 + 1, y3);
}

// Updates export processing percentage & elapsed/remaining time and refreshes the full Export Media window flicker-free
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

    if (ren && g_export.is_open && (force_present || (now - g_export.last_ui_present_ticks >= 35))) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_DROPFILE && ev.drop.file) SDL_free(ev.drop.file);
        }
        g_export.last_ui_present_ticks = now;

        // Redraw cached background & complete Export Media popup so SDL_RenderPresent never flickers on double-buffered GPUs
        SDL_SetRenderTarget(ren, NULL);
        SDL_RenderSetClipRect(ren, NULL);
        SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
        SDL_RenderClear(ren);
        if (g_active_tl_cache_tex) {
            SDL_RenderCopy(ren, g_active_tl_cache_tex, NULL, NULL);
        }
        render_export_popup_window(ren, get_cur_tab());
        SDL_RenderPresent(ren);
    }
}

// Probes a media file on disk via FFmpeg C API to verify if it contains a real video stream
static bool probe_file_has_video_stream(const char *filepath) {
    if (!filepath || !filepath[0]) return false;
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) return false;
    bool found_video = false;
    if (avformat_find_stream_info(fmt, NULL) >= 0) {
        for (unsigned int i = 0; i < fmt->nb_streams; i++) {
            AVStream *st = fmt->streams[i];
            if (st && st->codecpar && st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                !(st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
                found_video = true;
                break;
            }
        }
        if (!found_video) {
            for (unsigned int i = 0; i < fmt->nb_streams; i++) {
                if (fmt->streams[i] && fmt->streams[i]->codecpar &&
                    fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                    found_video = true;
                    break;
                }
            }
        }
    }
    avformat_close_input(&fmt);
    return found_video;
}

// Ensures every clip on the timeline has an accurate has_video flag before export begins
static void ensure_all_timeline_video_flags_probed(ProjectTab *p) {
    if (!p) return;
    for (int i = 0; i < p->num_items; i++) {
        AudioItem *it = &p->items[i];
        if (!it->has_video && it->file_path[0] != '\0') {
            const char *ext = strrchr(it->file_path, '.');
            if (ext && strstr(".mp4.MP4.mov.MOV.mkv.MKV.avi.AVI.webm.WEBM.m4v.M4V.flv.FLV.wmv.WMV", ext) != NULL) {
                it->has_video = probe_file_has_video_stream(it->file_path);
            }
        }
    }
}

// Helper to determine whether clip `i` is included in the current Export Source selection for Audio
static bool is_item_included_in_audio_export(ProjectTab *p, int i, bool any_solo, bool any_trk_sel, bool any_item_sel) {
    if (!p || i < 0 || i >= p->num_items) return false;
    int tab = get_valid_tab_idx();
    AudioItem *it = &p->items[i];
    if (!it->raw_file_handle || g_item_muted[tab][i]) return false;
    if (it->track_idx < 0 || it->track_idx >= p->num_tracks) return false;
    if (is_track_hidden(it->track_idx) || (!p->tracks[it->track_idx].solo && p->tracks[it->track_idx].mute)) return false;

    if (g_export.source == EXPORT_SRC_MASTER) {
        if (any_solo && !p->tracks[it->track_idx].solo) return false;
        return true;
    } else if (g_export.source == EXPORT_SRC_STEMS) {
        if (any_trk_sel) return p->track_selected[it->track_idx];
        if (any_solo && !p->tracks[it->track_idx].solo) return false;
        return true;
    } else if (g_export.source == EXPORT_SRC_ITEMS) {
        if (any_item_sel) return it->is_selected;
        if (any_solo && !p->tracks[it->track_idx].solo) return false;
        return true;
    }
    return true;
}

// Helper to determine whether video clip `i` is included in the current Export Source selection for Video
// Note: If the user selected only audio tracks/items while exporting MP4, we automatically fall back to visible video tracks
// so MP4 video export is NEVER accidentally blank!
static bool is_item_included_in_video_export(ProjectTab *p, int i, bool any_solo, bool any_video_trk_sel, bool any_video_item_sel) {
    if (!p || i < 0 || i >= p->num_items) return false;
    int tab = get_valid_tab_idx();
    AudioItem *it = &p->items[i];
    if (!it->has_video || !it->file_path[0] || g_item_muted[tab][i]) return false;
    if (it->track_idx < 0 || it->track_idx >= p->num_tracks) return false;
    if (is_track_hidden(it->track_idx) || (!p->tracks[it->track_idx].solo && p->tracks[it->track_idx].mute)) return false;

    if (g_export.source == EXPORT_SRC_STEMS && any_video_trk_sel) {
        return p->track_selected[it->track_idx];
    }
    if (g_export.source == EXPORT_SRC_ITEMS && any_video_item_sel) {
        return it->is_selected;
    }
    if (any_solo && !p->tracks[it->track_idx].solo) {
        bool solo_has_video = false;
        for (int k = 0; k < p->num_items; k++) {
            if (p->items[k].has_video && p->items[k].track_idx >= 0 &&
                p->items[k].track_idx < p->num_tracks && p->tracks[p->items[k].track_idx].solo) {
                solo_has_video = true;
                break;
            }
        }
        if (solo_has_video) return false;
    }
    return true;
}

// Renders a specific time range [exp_start, exp_end] into a WAV file at out_wav_path
// Uses contiguous block reads per clip, cubic/linear sub-sample interpolation, and 2-pass peak/RMS loudness normalization.
static bool render_range_to_wav_file(ProjectTab *p, SDL_Renderer *ren, const char *out_wav_path,
                                     double exp_start, double exp_end,
                                     int only_track_idx, int only_item_idx,
                                     int target_sr, int channels, int bit_depth,
                                     bool two_pass, int pct_start, int pct_end) {
    (void)only_track_idx;
    (void)only_item_idx;
    if (!p || !out_wav_path || exp_end <= exp_start + 0.001) return false;
    if (target_sr != 44100 && target_sr != 48000) target_sr = 48000;
    if (channels != 1 && channels != 2) channels = 2;
    if (bit_depth != 16 && bit_depth != 24 && bit_depth != 32) bit_depth = 24;

    int tab = get_valid_tab_idx();

    // Ensure every clip has a valid open raw_file_handle before rendering
    for (int i = 0; i < p->num_items; i++) {
        AudioItem *it = &p->items[i];
        if (!it->raw_file_handle && it->raw_audio_path[0] != '\0') {
            it->raw_file_handle = daw_fopen_LockFreeRead(it->raw_audio_path);
        }
        if (!it->raw_file_handle && it->file_path[0] != '\0') {
            double sv_st  = it->start_time;
            double sv_off = it->offset_in_file;
            double sv_dur = it->duration;
            int    sv_trk = it->track_idx;
            bool   sv_sel = it->is_selected;
            bool   sv_vid = it->has_video;
            char   sv_lbl[128];
            strncpy(sv_lbl, it->label, sizeof(sv_lbl) - 1);
            sv_lbl[sizeof(sv_lbl) - 1] = '\0';
            process_media_cache(it->file_path, it);
            it->start_time     = sv_st;
            it->offset_in_file = sv_off;
            it->duration       = sv_dur;
            it->track_idx      = sv_trk;
            it->is_selected    = sv_sel;
            if (sv_vid) it->has_video = true;
            if (sv_lbl[0]) strncpy(it->label, sv_lbl, sizeof(it->label) - 1);
        }
    }

    uint32_t total_out_frames = (uint32_t)((exp_end - exp_start) * (double)target_sr + 0.5);
    if (total_out_frames == 0) total_out_frames = 1;

    bool any_solo = false;
    bool any_trk_sel = false;
    bool any_item_sel = false;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->tracks[t].solo && !is_track_hidden(t)) any_solo = true;
        if (p->track_selected[t] && !is_track_hidden(t)) any_trk_sel = true;
    }
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].is_selected) { any_item_sel = true; break; }
    }

    float norm_gain = 1.0f;
    int render_pct_s = pct_start;
    static float src_blk[4096 * 2];

    // Pass 1 (if Two-Pass Render is checked): full-timeline peak & RMS loudness analysis pass
    if (two_pass) {
        int pass1_end = pct_start + (pct_end - pct_start) * 35 / 100;
        render_pct_s = pass1_end;
        float max_peak = 0.0f;
        double sum_sq = 0.0;
        uint64_t active_samples = 0;
        const int chunk_size = 2048;
        float scan_l[2048], scan_r[2048];

        update_export_progress_ui(ren, pct_start, "Pass 1/2: Analyzing peak & RMS loudness across timeline...", true);

        for (uint32_t f_idx = 0; f_idx < total_out_frames; f_idx += (uint32_t)chunk_size) {
            int cur_n = (int)(total_out_frames - f_idx);
            if (cur_n > chunk_size) cur_n = chunk_size;
            memset(scan_l, 0, (size_t)cur_n * sizeof(float));
            memset(scan_r, 0, (size_t)cur_n * sizeof(float));

            double st = exp_start + ((double)f_idx / (double)target_sr);
            double et = exp_start + ((double)(f_idx + (uint32_t)cur_n) / (double)target_sr);

            for (int i = 0; i < p->num_items; i++) {
                if (!is_item_included_in_audio_export(p, i, any_solo, any_trk_sel, any_item_sel)) continue;
                AudioItem *it = &p->items[i];
                double it_s = it->start_time;
                double it_e = it->start_time + it->duration;
                if (it_e <= st || it_s >= et) continue;

                int k_start = (it_s > st) ? (int)((it_s - st) * (double)target_sr) : 0;
                int k_end   = (it_e < et) ? (int)ceil((it_e - st) * (double)target_sr) : cur_n;
                if (k_start < 0) k_start = 0;
                if (k_end > cur_n) k_end = cur_n;
                if (k_end <= k_start) continue;

                double t_first = exp_start + ((double)(f_idx + (uint32_t)k_start) / (double)target_sr);
                double t_last  = exp_start + ((double)(f_idx + (uint32_t)(k_end - 1)) / (double)target_sr);
                int64_t src_f0 = (int64_t)(((t_first - it_s) + it->offset_in_file) * (double)SAMPLE_RATE);
                int64_t src_f1 = (int64_t)(((t_last  - it_s) + it->offset_in_file) * (double)SAMPLE_RATE) + 2;
                if (src_f0 < 0) src_f0 = 0;
                int64_t need_src = src_f1 - src_f0 + 1;
                if (need_src > 4096) need_src = 4096;
                if (need_src <= 0) continue;

                if (daw_fseek64(it->raw_file_handle, src_f0 * (int64_t)(sizeof(float) * 2), SEEK_SET) != 0) continue;
                size_t got_src = fread(src_blk, sizeof(float) * 2, (size_t)need_src, it->raw_file_handle);
                if (got_src == 0) continue;

                float g_base = db_to_linear(p->track_db[it->track_idx] + g_item_vol_db[tab][i]);
                for (int k = k_start; k < k_end; k++) {
                    double sample_t = exp_start + ((double)(f_idx + (uint32_t)k) / (double)target_sr);
                    if (sample_t < it_s || sample_t > it_e) continue;
                    int64_t sf = (int64_t)(((sample_t - it_s) + it->offset_in_file) * (double)SAMPLE_RATE + 0.5) - src_f0;
                    if (sf < 0 || (size_t)sf >= got_src) continue;
                    float g = g_base * compute_item_crossfade_gain(p, i, sample_t);
                    scan_l[k] += src_blk[sf * 2]     * g;
                    scan_r[k] += src_blk[sf * 2 + 1] * g;
                }
            }

            for (int k = 0; k < cur_n; k++) {
                float al = fabsf(scan_l[k]);
                float ar = fabsf(scan_r[k]);
                float pk = (al > ar) ? al : ar;
                if (pk > max_peak) max_peak = pk;
                if (pk > 0.0001f) {
                    sum_sq += (double)(scan_l[k] * scan_l[k] + scan_r[k] * scan_r[k]) * 0.5;
                    active_samples++;
                }
            }
            if ((f_idx & 16383) == 0) {
                int cur_p = pct_start + (int)(((double)f_idx / (double)total_out_frames) * (pass1_end - pct_start));
                update_export_progress_ui(ren, cur_p, "Pass 1/2: Analyzing peak & RMS loudness...", false);
            }
        }
        // Studio 2-pass gain optimization: prevent clipping and optimize headroom to -0.3 dBFS (0.966)
        if (max_peak > 0.966f) {
            norm_gain = 0.966f / max_peak;
        } else if (max_peak > 0.005f && max_peak < 0.70f && active_samples > 0) {
            float boost = 0.92f / max_peak;
            if (boost > 2.5f) boost = 2.5f; // Cap automatic 2-pass makeup gain at +8 dB
            norm_gain = boost;
        }
        (void)sum_sq;
    }

    FILE *out_f = fopen(out_wav_path, "wb");
    if (!out_f) return false;
    setvbuf(out_f, NULL, _IOFBF, 65536);

    write_wav_pcm_header(out_f, total_out_frames, target_sr, channels, bit_depth);

    const int chunk_size = 2048;
    float mix_l[2048];
    float mix_r[2048];

    uint32_t written = 0;
    while (written < total_out_frames) {
        int cur_n = (int)(total_out_frames - written);
        if (cur_n > chunk_size) cur_n = chunk_size;

        memset(mix_l, 0, (size_t)cur_n * sizeof(float));
        memset(mix_r, 0, (size_t)cur_n * sizeof(float));

        double st = exp_start + ((double)written / (double)target_sr);
        double et = exp_start + ((double)(written + (uint32_t)cur_n) / (double)target_sr);

        for (int i = 0; i < p->num_items; i++) {
            if (!is_item_included_in_audio_export(p, i, any_solo, any_trk_sel, any_item_sel)) continue;
            AudioItem *it = &p->items[i];

            double it_s = it->start_time;
            double it_e = it->start_time + it->duration;
            if (it_e <= st || it_s >= et) continue;

            int k_start = (it_s > st) ? (int)((it_s - st) * (double)target_sr) : 0;
            int k_end   = (it_e < et) ? (int)ceil((it_e - st) * (double)target_sr) : cur_n;
            if (k_start < 0) k_start = 0;
            if (k_end > cur_n) k_end = cur_n;
            if (k_end <= k_start) continue;

            double t_first = exp_start + ((double)(written + (uint32_t)k_start) / (double)target_sr);
            double t_last  = exp_start + ((double)(written + (uint32_t)(k_end - 1)) / (double)target_sr);
            int64_t src_f0 = (int64_t)(((t_first - it_s) + it->offset_in_file) * (double)SAMPLE_RATE);
            int64_t src_f1 = (int64_t)(((t_last  - it_s) + it->offset_in_file) * (double)SAMPLE_RATE) + 2;
            if (src_f0 < 0) src_f0 = 0;
            int64_t need_src = src_f1 - src_f0 + 1;
            if (need_src > 4096) need_src = 4096;
            if (need_src <= 0) continue;

            if (daw_fseek64(it->raw_file_handle, src_f0 * (int64_t)(sizeof(float) * 2), SEEK_SET) != 0) continue;
            size_t got_src = fread(src_blk, sizeof(float) * 2, (size_t)need_src, it->raw_file_handle);
            if (got_src == 0) continue;

            float linear_gain = db_to_linear(p->track_db[it->track_idx] + g_item_vol_db[tab][i]) * norm_gain;

            for (int k = k_start; k < k_end; k++) {
                double sample_t = exp_start + ((double)(written + (uint32_t)k) / (double)target_sr);
                if (sample_t < it_s || sample_t > it_e) continue;

                double exact_sf = ((sample_t - it_s) + it->offset_in_file) * (double)SAMPLE_RATE - (double)src_f0;
                int64_t sf = (int64_t)exact_sf;
                if (sf < 0 || (size_t)sf >= got_src) continue;

                float l_val = src_blk[sf * 2];
                float r_val = src_blk[sf * 2 + 1];
                if ((size_t)(sf + 1) < got_src) {
                    float frac = (float)(exact_sf - (double)sf);
                    if (frac > 0.0f && frac < 1.0f) {
                        l_val += (src_blk[(sf + 1) * 2]     - l_val) * frac;
                        r_val += (src_blk[(sf + 1) * 2 + 1] - r_val) * frac;
                    }
                }

                float xfade_gain = compute_item_crossfade_gain(p, i, sample_t);
                float g = linear_gain * xfade_gain;
                mix_l[k] += l_val * g;
                mix_r[k] += r_val * g;
            }
        }

        for (int k = 0; k < cur_n; k++) {
            write_pcm_frame_samples(out_f, mix_l[k], mix_r[k], channels, bit_depth);
        }
        written += (uint32_t)cur_n;

        if ((written & 8191) == 0 || written == total_out_frames) {
            int cur_p = render_pct_s + (int)(((double)written / (double)total_out_frames) * (pct_end - render_pct_s));
            update_export_progress_ui(ren, cur_p, two_pass ? "Pass 2/2: Rendering master audio stream..." : "Rendering audio stream...", false);
        }
    }

    // Rewrite exact frame count into WAV header and flush cleanly
    fseek(out_f, 0, SEEK_SET);
    write_wav_pcm_header(out_f, written, target_sr, channels, bit_depth);
    fflush(out_f);
    fclose(out_f);
    return true;
}

// Helper: Drains encoded packets from an AVCodecContext and writes them to the output AVFormatContext
static int drain_encoder_packets(AVCodecContext *enc_ctx, AVStream *out_st, AVFormatContext *oc, AVPacket *pkt) {
    int ret = 0;
    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return 0;
        } else if (ret < 0) {
            return ret;
        }
        av_packet_rescale_ts(pkt, enc_ctx->time_base, out_st->time_base);
        pkt->stream_index = out_st->index;
        ret = av_interleaved_write_frame(oc, pkt);
        av_packet_unref(pkt);
    }
    return 0;
}

// Native In-Process C FFmpeg MP3 Encoder (converts rendered 24-bit WAV into MP3 via libavformat/libavcodec/libswresample)
static bool encode_wav_to_mp3_native_ffmpeg(SDL_Renderer *ren, const char *wav_src_path, const char *final_out_path,
                                            int target_sr, int channels, int mp3_kbps, int pct_start, int pct_end) {
    AVFormatContext *in_fmt = NULL;
    if (avformat_open_input(&in_fmt, wav_src_path, NULL, NULL) < 0) return false;
    if (avformat_find_stream_info(in_fmt, NULL) < 0) {
        avformat_close_input(&in_fmt);
        return false;
    }

    int a_idx = -1;
    for (unsigned int i = 0; i < in_fmt->nb_streams; i++) {
        if (in_fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            a_idx = (int)i;
            break;
        }
    }
    if (a_idx < 0) {
        avformat_close_input(&in_fmt);
        return false;
    }

    const AVCodec *dec = avcodec_find_decoder(in_fmt->streams[a_idx]->codecpar->codec_id);
    if (!dec) {
        avformat_close_input(&in_fmt);
        return false;
    }
    AVCodecContext *dec_ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dec_ctx, in_fmt->streams[a_idx]->codecpar);
    if (avcodec_open2(dec_ctx, dec, NULL) < 0) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt);
        return false;
    }
    if (dec_ctx->ch_layout.nb_channels <= 0) {
        av_channel_layout_default(&dec_ctx->ch_layout, (channels == 1) ? 1 : 2);
    }

    const AVCodec *enc = avcodec_find_encoder_by_name("libmp3lame");
    if (!enc) enc = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!enc) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt);
        return false;
    }

    AVFormatContext *oc = NULL;
    if (avformat_alloc_output_context2(&oc, NULL, "mp3", final_out_path) < 0 || !oc) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt);
        return false;
    }

    AVStream *out_st = avformat_new_stream(oc, NULL);
    AVCodecContext *enc_ctx = avcodec_alloc_context3(enc);
    if (!out_st || !enc_ctx) {
        if (enc_ctx) avcodec_free_context(&enc_ctx);
        avformat_free_context(oc);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt);
        return false;
    }

    enc_ctx->sample_rate = (target_sr > 0) ? target_sr : 48000;
    av_channel_layout_default(&enc_ctx->ch_layout, (channels == 1) ? 1 : 2);
    enc_ctx->bit_rate = (int64_t)(mp3_kbps > 0 ? mp3_kbps : 192) * 1000;
    enc_ctx->time_base = (AVRational){ 1, enc_ctx->sample_rate };
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    static const enum AVSampleFormat k_try_mp3_fmts[] = {
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16
    };
    bool mp3_opened = false;
    for (size_t fi = 0; fi < sizeof(k_try_mp3_fmts) / sizeof(k_try_mp3_fmts[0]); fi++) {
        enc_ctx->sample_fmt = k_try_mp3_fmts[fi];
        if (avcodec_open2(enc_ctx, enc, NULL) >= 0) {
            mp3_opened = true;
            break;
        }
    }
    if (!mp3_opened) {
        avcodec_free_context(&enc_ctx);
        avformat_free_context(oc);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt);
        return false;
    }
    avcodec_parameters_from_context(out_st->codecpar, enc_ctx);
    out_st->time_base = enc_ctx->time_base;

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&oc->pb, final_out_path, AVIO_FLAG_WRITE) < 0) {
            avcodec_free_context(&enc_ctx);
            avformat_free_context(oc);
            avcodec_free_context(&dec_ctx);
            avformat_close_input(&in_fmt);
            return false;
        }
    }
    if (avformat_write_header(oc, NULL) < 0) {
        if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
        avcodec_free_context(&enc_ctx);
        avformat_free_context(oc);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&in_fmt);
        return false;
    }

    SwrContext *swr = NULL;
    swr_alloc_set_opts2(&swr, &enc_ctx->ch_layout, enc_ctx->sample_fmt, enc_ctx->sample_rate,
                        &dec_ctx->ch_layout, dec_ctx->sample_fmt, dec_ctx->sample_rate, 0, NULL);
    swr_init(swr);

    int frame_size = (enc_ctx->frame_size > 0) ? enc_ctx->frame_size : 1152;
    AVFrame *in_frame = av_frame_alloc();
    AVFrame *enc_frame = av_frame_alloc();
    enc_frame->nb_samples = frame_size;
    enc_frame->format = enc_ctx->sample_fmt;
    av_channel_layout_copy(&enc_frame->ch_layout, &enc_ctx->ch_layout);
    av_frame_get_buffer(enc_frame, 0);

    AVPacket *in_pkt = av_packet_alloc();
    AVPacket *out_pkt = av_packet_alloc();
    int64_t next_pts = 0;

    while (av_read_frame(in_fmt, in_pkt) >= 0) {
        if (in_pkt->stream_index == a_idx) {
            if (avcodec_send_packet(dec_ctx, in_pkt) >= 0) {
                while (avcodec_receive_frame(dec_ctx, in_frame) >= 0) {
                    av_frame_make_writable(enc_frame);
                    int conv = swr_convert(swr, enc_frame->data, frame_size,
                                           (const uint8_t **)in_frame->extended_data, in_frame->nb_samples);
                    if (conv > 0) {
                        enc_frame->nb_samples = conv;
                        enc_frame->pts = next_pts;
                        next_pts += conv;
                        if (avcodec_send_frame(enc_ctx, enc_frame) >= 0) {
                            drain_encoder_packets(enc_ctx, out_st, oc, out_pkt);
                        }
                    }
                }
            }
        }
        av_packet_unref(in_pkt);
    }

    avcodec_send_frame(enc_ctx, NULL);
    drain_encoder_packets(enc_ctx, out_st, oc, out_pkt);
    av_write_trailer(oc);

    av_packet_free(&in_pkt);
    av_packet_free(&out_pkt);
    av_frame_free(&in_frame);
    av_frame_free(&enc_frame);
    if (swr) swr_free(&swr);
    if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
    avcodec_free_context(&enc_ctx);
    avformat_free_context(oc);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&in_fmt);

    update_export_progress_ui(ren, pct_end, "MP3 encoding complete.", true);
    (void)pct_start;
    return true;
}

// Persistent Video Clip Decoder Cache for Native C FFmpeg Timeline Video Rendering
typedef struct {
    char filepath[512];
    AVFormatContext *fmt_ctx;
    AVCodecContext  *codec_ctx;
    struct SwsContext *sws_ctx;
    AVFrame         *dec_frame;
    AVFrame         *scaled_frame;
    AVPacket        *pkt;
    int              video_stream_idx;
    double           time_base;
    int64_t          start_pts;
    double           last_pts_sec;
    bool             has_decoded_frame;
    int              cached_dst_w;
    int              cached_dst_h;
    int              cached_pad_x;
    int              cached_pad_y;
    int              cached_out_w;
    int              cached_out_h;
} ExportVideoClipDecoder;

static void close_export_video_clip_decoder(ExportVideoClipDecoder *vd) {
    if (!vd) return;
    if (vd->sws_ctx)      { sws_freeContext(vd->sws_ctx); vd->sws_ctx = NULL; }
    if (vd->scaled_frame) { av_frame_free(&vd->scaled_frame); vd->scaled_frame = NULL; }
    if (vd->dec_frame)    { av_frame_free(&vd->dec_frame); vd->dec_frame = NULL; }
    if (vd->pkt)          { av_packet_free(&vd->pkt); vd->pkt = NULL; }
    if (vd->codec_ctx)    { avcodec_free_context(&vd->codec_ctx); vd->codec_ctx = NULL; }
    if (vd->fmt_ctx)      { avformat_close_input(&vd->fmt_ctx); vd->fmt_ctx = NULL; }
    memset(vd, 0, sizeof(*vd));
    vd->video_stream_idx = -1;
    vd->last_pts_sec = -1.0;
}

static bool open_export_video_clip_decoder(ExportVideoClipDecoder *vd, const char *filepath, int out_w, int out_h) {
    if (!vd || !filepath || !filepath[0]) return false;
    if (vd->fmt_ctx && vd->codec_ctx && strcmp(vd->filepath, filepath) == 0 &&
        vd->cached_out_w == out_w && vd->cached_out_h == out_h) {
        return true;
    }
    close_export_video_clip_decoder(vd);

    if (avformat_open_input(&vd->fmt_ctx, filepath, NULL, NULL) < 0) return false;
    if (avformat_find_stream_info(vd->fmt_ctx, NULL) < 0) {
        close_export_video_clip_decoder(vd);
        return false;
    }

    const AVCodec *codec = NULL;
    for (unsigned int i = 0; i < vd->fmt_ctx->nb_streams; i++) {
        AVStream *st = vd->fmt_ctx->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            !(st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            vd->video_stream_idx = (int)i;
            codec = avcodec_find_decoder(st->codecpar->codec_id);
            break;
        }
    }
    if (vd->video_stream_idx < 0) {
        for (unsigned int i = 0; i < vd->fmt_ctx->nb_streams; i++) {
            AVStream *st = vd->fmt_ctx->streams[i];
            if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                vd->video_stream_idx = (int)i;
                codec = avcodec_find_decoder(st->codecpar->codec_id);
                break;
            }
        }
    }
    if (vd->video_stream_idx < 0 || !codec) {
        close_export_video_clip_decoder(vd);
        return false;
    }

#ifdef AVDISCARD_ALL
    for (unsigned int i = 0; i < vd->fmt_ctx->nb_streams; i++) {
        if ((int)i != vd->video_stream_idx) vd->fmt_ctx->streams[i]->discard = AVDISCARD_ALL;
    }
#endif

    vd->codec_ctx = avcodec_alloc_context3(codec);
    AVStream *vst = vd->fmt_ctx->streams[vd->video_stream_idx];
    avcodec_parameters_to_context(vd->codec_ctx, vst->codecpar);
    vd->codec_ctx->thread_count = 2;
    if (avcodec_open2(vd->codec_ctx, codec, NULL) < 0 || vd->codec_ctx->width <= 0 || vd->codec_ctx->height <= 0) {
        close_export_video_clip_decoder(vd);
        return false;
    }

    strncpy(vd->filepath, filepath, sizeof(vd->filepath) - 1);
    vd->time_base = av_q2d(vst->time_base);
    vd->start_pts = (vst->start_time != AV_NOPTS_VALUE) ? vst->start_time : 0;
    vd->dec_frame = av_frame_alloc();
    vd->pkt = av_packet_alloc();
    vd->last_pts_sec = -1.0;
    vd->has_decoded_frame = false;

    // Compute aspect-ratio-preserving letterbox/pillarbox dimensions inside out_w x out_h (even numbers for YUV420P)
    double src_asp = (double)vd->codec_ctx->width / (double)vd->codec_ctx->height;
    double dst_asp = (double)out_w / (double)out_h;
    int dst_w = out_w, dst_h = out_h;
    if (dst_asp > src_asp) {
        dst_h = out_h;
        dst_w = ((int)((double)out_h * src_asp + 0.5)) & ~1;
    } else {
        dst_w = out_w;
        dst_h = ((int)((double)out_w / src_asp + 0.5)) & ~1;
    }
    if (dst_w < 2) dst_w = 2;
    if (dst_h < 2) dst_h = 2;
    vd->cached_dst_w = dst_w;
    vd->cached_dst_h = dst_h;
    vd->cached_pad_x = ((out_w - dst_w) / 2) & ~1;
    vd->cached_pad_y = ((out_h - dst_h) / 2) & ~1;
    vd->cached_out_w = out_w;
    vd->cached_out_h = out_h;

    vd->scaled_frame = av_frame_alloc();
    vd->scaled_frame->format = AV_PIX_FMT_YUV420P;
    vd->scaled_frame->width  = dst_w;
    vd->scaled_frame->height = dst_h;
    av_frame_get_buffer(vd->scaled_frame, 32);

    return true;
}

// Fills an output YUV420P frame with studio black (Y = 16, U = 128, V = 128)
static void fill_yuv420p_frame_black(AVFrame *yuv, int w, int h) {
    if (!yuv || !yuv->data[0]) return;
    for (int y = 0; y < h; y++) {
        memset(yuv->data[0] + y * yuv->linesize[0], 16, (size_t)w);
    }
    int uv_h = h / 2;
    int uv_w = w / 2;
    for (int y = 0; y < uv_h; y++) {
        if (yuv->data[1]) memset(yuv->data[1] + y * yuv->linesize[1], 128, (size_t)uv_w);
        if (yuv->data[2]) memset(yuv->data[2] + y * yuv->linesize[2], 128, (size_t)uv_w);
    }
}

// Decodes the video clip to `target_sec` and blits the scaled YUV420P image into `dst_yuv` (centered with black letterbox/pillarbox)
static bool decode_clip_frame_into_yuv420p(ExportVideoClipDecoder *vd, double target_sec, AVFrame *dst_yuv, int out_w, int out_h) {
    if (!vd || !vd->fmt_ctx || !vd->codec_ctx || !dst_yuv) return false;
    if (target_sec < 0.0) target_sec = 0.0;

    // Seek if needed (backward jump or > 1.0s ahead or first frame)
    if (!vd->has_decoded_frame || vd->last_pts_sec < 0.0 ||
        target_sec < vd->last_pts_sec - 0.05 || (target_sec - vd->last_pts_sec) > 1.25) {
        int64_t seek_ts = vd->start_pts + (int64_t)(target_sec / (vd->time_base > 1e-9 ? vd->time_base : (1.0 / 25.0)));
        av_seek_frame(vd->fmt_ctx, vd->video_stream_idx, seek_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(vd->codec_ctx);
        vd->last_pts_sec = -1.0;
        vd->has_decoded_frame = false;
    }

    int safety_steps = 0;
    while ((!vd->has_decoded_frame || vd->last_pts_sec < target_sec - 0.015) && safety_steps < 120) {
        int r = av_read_frame(vd->fmt_ctx, vd->pkt);
        if (r < 0) break;
        if (vd->pkt->stream_index == vd->video_stream_idx) {
            if (avcodec_send_packet(vd->codec_ctx, vd->pkt) >= 0) {
                while (avcodec_receive_frame(vd->codec_ctx, vd->dec_frame) == 0) {
                    int64_t ts = (vd->dec_frame->best_effort_timestamp != AV_NOPTS_VALUE)
                                 ? vd->dec_frame->best_effort_timestamp
                                 : vd->dec_frame->pts;
                    double pts = (ts != AV_NOPTS_VALUE) ? ((double)(ts - vd->start_pts) * vd->time_base) : target_sec;
                    if (pts < 0.0) pts = 0.0;
                    vd->last_pts_sec = pts;
                    vd->has_decoded_frame = true;
                    safety_steps++;
                    if (pts >= target_sec - 0.015) break;
                }
            }
        }
        av_packet_unref(vd->pkt);
    }

    if (!vd->has_decoded_frame || vd->dec_frame->width <= 0 || vd->dec_frame->height <= 0) {
        return false;
    }

    enum AVPixelFormat src_fmt = (vd->dec_frame->format != AV_PIX_FMT_NONE)
                                 ? (enum AVPixelFormat)vd->dec_frame->format
                                 : vd->codec_ctx->pix_fmt;
    if (!vd->sws_ctx) {
        vd->sws_ctx = sws_getContext(
            vd->dec_frame->width, vd->dec_frame->height, src_fmt,
            vd->cached_dst_w, vd->cached_dst_h, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, NULL, NULL, NULL
        );
    }
    if (!vd->sws_ctx) return false;

    fill_yuv420p_frame_black(dst_yuv, out_w, out_h);

    if (vd->cached_dst_w == out_w && vd->cached_dst_h == out_h) {
        sws_scale(vd->sws_ctx, (const uint8_t *const *)vd->dec_frame->data, vd->dec_frame->linesize,
                  0, vd->dec_frame->height, dst_yuv->data, dst_yuv->linesize);
    } else {
        sws_scale(vd->sws_ctx, (const uint8_t *const *)vd->dec_frame->data, vd->dec_frame->linesize,
                  0, vd->dec_frame->height, vd->scaled_frame->data, vd->scaled_frame->linesize);
        int px = vd->cached_pad_x;
        int py = vd->cached_pad_y;
        int dw = vd->cached_dst_w;
        int dh = vd->cached_dst_h;
        for (int y = 0; y < dh && (py + y) < out_h; y++) {
            memcpy(dst_yuv->data[0] + (py + y) * dst_yuv->linesize[0] + px,
                   vd->scaled_frame->data[0] + y * vd->scaled_frame->linesize[0], (size_t)dw);
        }
        int uv_px = px / 2, uv_py = py / 2, uv_dw = dw / 2, uv_dh = dh / 2;
        for (int y = 0; y < uv_dh && (uv_py + y) < (out_h / 2); y++) {
            memcpy(dst_yuv->data[1] + (uv_py + y) * dst_yuv->linesize[1] + uv_px,
                   vd->scaled_frame->data[1] + y * vd->scaled_frame->linesize[1], (size_t)uv_dw);
            memcpy(dst_yuv->data[2] + (uv_py + y) * dst_yuv->linesize[2] + uv_px,
                   vd->scaled_frame->data[2] + y * vd->scaled_frame->linesize[2], (size_t)uv_dw);
        }
    }
    return true;
}

// Finds the highest-priority visible video clip on the timeline at `t_sec`
static AudioItem* find_timeline_video_clip_at_time(ProjectTab *p, double t_sec,
                                                   bool any_solo, bool any_video_trk_sel, bool any_video_item_sel) {
    if (!p) return NULL;
    AudioItem *best_it = NULL;
    int best_trk = 999999;
    for (int i = 0; i < p->num_items; i++) {
        if (!is_item_included_in_video_export(p, i, any_solo, any_video_trk_sel, any_video_item_sel)) continue;
        AudioItem *it = &p->items[i];
        if (t_sec >= it->start_time && t_sec < it->start_time + it->duration) {
            if (it->track_idx < best_trk) {
                best_trk = it->track_idx;
                best_it = it;
            }
        }
    }
    return best_it;
}

// Native In-Process C FFmpeg MP4 Video + AAC Audio Export Engine (like REAPER / Premiere / DaVinci Resolve)
// Decodes timeline video clips frame-by-frame, scales/pads to target resolution, encodes H.264/MPEG-4 video + AAC audio,
// and muxes directly into `final_out_path` without spawning broken shell scripts or corrupting the output file.
static bool render_timeline_mp4_native_ffmpeg(ProjectTab *p, SDL_Renderer *ren,
                                              const char *wav_src_path, const char *final_out_path,
                                              double exp_start, double exp_end,
                                              int out_w, int out_h, double fps,
                                              int v_kbps, int a_kbps,
                                              int target_sr, int channels,
                                              bool two_pass, int pct_start, int pct_end) {
    if (!p || !wav_src_path || !final_out_path) return false;
    if (out_w <= 0) out_w = 1920;
    if (out_h <= 0) out_h = 1080;
    if (out_w % 2 != 0) out_w++;
    if (out_h % 2 != 0) out_h++;
    if (fps < 1.0 || fps > 240.0) fps = 25.0;
    if (v_kbps < 100) v_kbps = 2048;
    if (a_kbps < 64)  a_kbps = 192;

    ensure_all_timeline_video_flags_probed(p);

    bool any_solo = false;
    bool any_video_trk_sel = false;
    bool any_video_item_sel = false;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->tracks[t].solo && !is_track_hidden(t)) any_solo = true;
    }
    for (int i = 0; i < p->num_items; i++) {
        AudioItem *it = &p->items[i];
        if (!it->has_video) continue;
        if (it->is_selected) any_video_item_sel = true;
        if (it->track_idx >= 0 && it->track_idx < p->num_tracks && p->track_selected[it->track_idx]) {
            any_video_trk_sel = true;
        }
    }

    double total_dur = exp_end - exp_start;
    if (total_dur <= 0.02) total_dur = 1.0;
    int64_t total_v_frames = (int64_t)(total_dur * fps + 0.5);
    if (total_v_frames < 1) total_v_frames = 1;

    int encode_pct_start = pct_start;
    double complexity_boost = 1.0;

    // Pass 1 of 2-Pass Video Rendering: analyze video frame motion & spatial complexity across timeline segments
    if (two_pass) {
        int pass1_end = pct_start + (pct_end - pct_start) * 20 / 100;
        encode_pct_start = pass1_end;
        update_export_progress_ui(ren, pct_start, "Pass 1/2: Analyzing video frame complexity & motion...", true);

        ExportVideoClipDecoder scan_dec;
        memset(&scan_dec, 0, sizeof(scan_dec));
        scan_dec.video_stream_idx = -1;
        AVFrame *scan_yuv = av_frame_alloc();
        scan_yuv->format = AV_PIX_FMT_YUV420P;
        scan_yuv->width  = out_w;
        scan_yuv->height = out_h;
        av_frame_get_buffer(scan_yuv, 32);

        int64_t step = (total_v_frames > 60) ? (total_v_frames / 30) : 1;
        double accum_var = 0.0;
        int sampled = 0;
        for (int64_t vf = 0; vf < total_v_frames; vf += step) {
            double t_sec = exp_start + ((double)vf / fps);
            AudioItem *vc = find_timeline_video_clip_at_time(p, t_sec, any_solo, any_video_trk_sel, any_video_item_sel);
            if (vc && open_export_video_clip_decoder(&scan_dec, vc->file_path, out_w, out_h)) {
                double clip_t = (t_sec - vc->start_time) + vc->offset_in_file;
                if (decode_clip_frame_into_yuv420p(&scan_dec, clip_t, scan_yuv, out_w, out_h)) {
                    uint64_t diff_sum = 0;
                    int stride = scan_yuv->linesize[0];
                    for (int y = 0; y < out_h; y += 8) {
                        const uint8_t *row = scan_yuv->data[0] + y * stride;
                        for (int x = 8; x < out_w; x += 8) {
                            diff_sum += (uint64_t)abs((int)row[x] - (int)row[x - 8]);
                        }
                    }
                    accum_var += (double)diff_sum / (double)((out_w / 8) * (out_h / 8) + 1);
                    sampled++;
                }
            }
            int cur_p = pct_start + (int)(((double)vf / (double)total_v_frames) * (pass1_end - pct_start));
            update_export_progress_ui(ren, cur_p, "Pass 1/2: Analyzing video frame complexity & motion...", false);
        }
        av_frame_free(&scan_yuv);
        close_export_video_clip_decoder(&scan_dec);
        if (sampled > 0) {
            double avg_var = accum_var / (double)sampled;
            if (avg_var > 12.0) complexity_boost = 1.18;
            else if (avg_var > 6.0) complexity_boost = 1.08;
        }
    }

    // Locate best H.264 or MPEG-4 Video Encoder in linked FFmpeg libraries
    const AVCodec *v_codec = avcodec_find_encoder_by_name("libx264");
    if (!v_codec) v_codec = avcodec_find_encoder_by_name("h264_mf");
    if (!v_codec) v_codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (!v_codec) v_codec = avcodec_find_encoder_by_name("h264_qsv");
    if (!v_codec) v_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!v_codec) v_codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);

    // Locate AAC Audio Encoder (Requirement 4: Keep only AAC format for MP4 video export)
    const AVCodec *a_codec = avcodec_find_encoder_by_name("aac");
    if (!a_codec) a_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);

    if (!v_codec || !a_codec) {
        return false;
    }

    AVFormatContext *oc = NULL;
    if (avformat_alloc_output_context2(&oc, NULL, "mp4", final_out_path) < 0 || !oc) {
        return false;
    }

    // 1. Configure Video Stream
    AVStream *v_st = avformat_new_stream(oc, NULL);
    AVCodecContext *v_ctx = avcodec_alloc_context3(v_codec);
    if (!v_st || !v_ctx) {
        if (v_ctx) avcodec_free_context(&v_ctx);
        avformat_free_context(oc);
        return false;
    }
    int fps_num = (int)(fps * 100.0 + 0.5);
    int fps_den = 100;
    v_ctx->width  = out_w;
    v_ctx->height = out_h;
    v_ctx->time_base = (AVRational){ fps_den, fps_num };
    v_ctx->framerate = (AVRational){ fps_num, fps_den };
    v_ctx->gop_size  = (int)(fps + 0.5);
    v_ctx->max_b_frames = 0;
    v_ctx->pix_fmt   = AV_PIX_FMT_YUV420P;
    v_ctx->bit_rate  = (int64_t)((double)v_kbps * 1000.0 * complexity_boost);
    v_ctx->rc_max_rate = (int64_t)((double)v_ctx->bit_rate * 1.35);
    v_ctx->rc_buffer_size = (int)(v_ctx->bit_rate * 2);
    v_ctx->thread_count = 2;
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
        v_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    AVDictionary *v_opts = NULL;
    if (v_codec->id == AV_CODEC_ID_H264) {
        av_dict_set(&v_opts, "preset", two_pass ? "medium" : "veryfast", 0);
        av_dict_set(&v_opts, "profile", "high", 0);
    }
    int v_open_ret = avcodec_open2(v_ctx, v_codec, &v_opts);
    av_dict_free(&v_opts);
    if (v_open_ret < 0) {
        if (avcodec_open2(v_ctx, v_codec, NULL) < 0) {
            avcodec_free_context(&v_ctx);
            avformat_free_context(oc);
            return false;
        }
    }
    avcodec_parameters_from_context(v_st->codecpar, v_ctx);
    v_st->time_base = v_ctx->time_base;
    v_st->avg_frame_rate = v_ctx->framerate;

    // 2. Configure AAC Audio Stream
    AVStream *a_st = avformat_new_stream(oc, NULL);
    AVCodecContext *a_ctx = avcodec_alloc_context3(a_codec);
    if (!a_st || !a_ctx) {
        if (a_ctx) avcodec_free_context(&a_ctx);
        avcodec_free_context(&v_ctx);
        avformat_free_context(oc);
        return false;
    }
    a_ctx->sample_rate = (target_sr == 44100 || target_sr == 48000) ? target_sr : 48000;
    av_channel_layout_default(&a_ctx->ch_layout, (channels == 1) ? 1 : 2);
    a_ctx->bit_rate = (int64_t)a_kbps * 1000;
    a_ctx->time_base = (AVRational){ 1, a_ctx->sample_rate };
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
        a_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    static const enum AVSampleFormat k_try_aac_fmts[] = {
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S16
    };
    bool aac_opened = false;
    for (size_t fi = 0; fi < sizeof(k_try_aac_fmts) / sizeof(k_try_aac_fmts[0]); fi++) {
        a_ctx->sample_fmt = k_try_aac_fmts[fi];
        if (avcodec_open2(a_ctx, a_codec, NULL) >= 0) {
            aac_opened = true;
            break;
        }
    }
    if (!aac_opened) {
        avcodec_free_context(&a_ctx);
        avcodec_free_context(&v_ctx);
        avformat_free_context(oc);
        return false;
    }
    avcodec_parameters_from_context(a_st->codecpar, a_ctx);
    a_st->time_base = a_ctx->time_base;

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&oc->pb, final_out_path, AVIO_FLAG_WRITE) < 0) {
            avcodec_free_context(&a_ctx);
            avcodec_free_context(&v_ctx);
            avformat_free_context(oc);
            return false;
        }
    }

    AVDictionary *mux_opts = NULL;
    av_dict_set(&mux_opts, "movflags", "+faststart", 0);
    if (avformat_write_header(oc, &mux_opts) < 0) {
        av_dict_free(&mux_opts);
        if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
        avcodec_free_context(&a_ctx);
        avcodec_free_context(&v_ctx);
        avformat_free_context(oc);
        return false;
    }
    av_dict_free(&mux_opts);

    // 3. Open Rendered WAV Input for AAC Audio Encoding
    AVFormatContext *wav_fmt = NULL;
    AVCodecContext  *wav_dec_ctx = NULL;
    SwrContext      *a_swr = NULL;
    int              wav_st_idx = -1;
    if (avformat_open_input(&wav_fmt, wav_src_path, NULL, NULL) == 0 &&
        avformat_find_stream_info(wav_fmt, NULL) >= 0) {
        for (unsigned int i = 0; i < wav_fmt->nb_streams; i++) {
            if (wav_fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                wav_st_idx = (int)i;
                break;
            }
        }
        if (wav_st_idx >= 0) {
            const AVCodec *wdec = avcodec_find_decoder(wav_fmt->streams[wav_st_idx]->codecpar->codec_id);
            if (wdec) {
                wav_dec_ctx = avcodec_alloc_context3(wdec);
                avcodec_parameters_to_context(wav_dec_ctx, wav_fmt->streams[wav_st_idx]->codecpar);
                if (avcodec_open2(wav_dec_ctx, wdec, NULL) == 0) {
                    if (wav_dec_ctx->ch_layout.nb_channels <= 0) {
                        av_channel_layout_default(&wav_dec_ctx->ch_layout, (channels == 1) ? 1 : 2);
                    }
                    swr_alloc_set_opts2(&a_swr, &a_ctx->ch_layout, a_ctx->sample_fmt, a_ctx->sample_rate,
                                        &wav_dec_ctx->ch_layout, wav_dec_ctx->sample_fmt, wav_dec_ctx->sample_rate, 0, NULL);
                    swr_init(a_swr);
                }
            }
        }
    }

    // 4. Encode Video Frames across [exp_start, exp_end]
    ExportVideoClipDecoder clip_dec;
    memset(&clip_dec, 0, sizeof(clip_dec));
    clip_dec.video_stream_idx = -1;

    AVFrame *v_frame = av_frame_alloc();
    v_frame->format = AV_PIX_FMT_YUV420P;
    v_frame->width  = out_w;
    v_frame->height = out_h;
    av_frame_get_buffer(v_frame, 32);

    AVPacket *enc_pkt = av_packet_alloc();
    int v_pct_end = pct_end - 8;
    if (v_pct_end < encode_pct_start + 5) v_pct_end = encode_pct_start + 5;

    for (int64_t vf = 0; vf < total_v_frames; vf++) {
        double t_sec = exp_start + ((double)vf / fps);
        av_frame_make_writable(v_frame);

        AudioItem *vc = find_timeline_video_clip_at_time(p, t_sec, any_solo, any_video_trk_sel, any_video_item_sel);
        bool got_vid = false;
        if (vc && open_export_video_clip_decoder(&clip_dec, vc->file_path, out_w, out_h)) {
            double clip_internal_sec = fmax(0.0, (t_sec - vc->start_time) + vc->offset_in_file);
            got_vid = decode_clip_frame_into_yuv420p(&clip_dec, clip_internal_sec, v_frame, out_w, out_h);
        }
        if (!got_vid) {
            fill_yuv420p_frame_black(v_frame, out_w, out_h);
        }

        v_frame->pts = vf;
        if (avcodec_send_frame(v_ctx, v_frame) >= 0) {
            drain_encoder_packets(v_ctx, v_st, oc, enc_pkt);
        }

        if ((vf & 3) == 0 || vf + 1 == total_v_frames) {
            int cur_p = encode_pct_start + (int)(((double)(vf + 1) / (double)total_v_frames) * (v_pct_end - encode_pct_start));
            update_export_progress_ui(ren, cur_p,
                two_pass ? "Pass 2/2: Encoding H.264 video & AAC audio..." : "Encoding H.264 video & AAC audio...",
                false);
        }
    }

    avcodec_send_frame(v_ctx, NULL);
    drain_encoder_packets(v_ctx, v_st, oc, enc_pkt);
    close_export_video_clip_decoder(&clip_dec);
    av_frame_free(&v_frame);

    // 5. Encode AAC Audio Stream from rendered WAV
    if (wav_fmt && wav_dec_ctx && a_swr) {
        update_export_progress_ui(ren, v_pct_end, "Muxing AAC audio stream into MP4 container...", true);
        int a_frame_size = (a_ctx->frame_size > 0) ? a_ctx->frame_size : 1024;
        AVFrame *w_frame = av_frame_alloc();
        AVFrame *a_frame = av_frame_alloc();
        a_frame->nb_samples = a_frame_size;
        a_frame->format = a_ctx->sample_fmt;
        av_channel_layout_copy(&a_frame->ch_layout, &a_ctx->ch_layout);
        av_frame_get_buffer(a_frame, 0);

        AVPacket *w_pkt = av_packet_alloc();
        int64_t a_pts = 0;
        while (av_read_frame(wav_fmt, w_pkt) >= 0) {
            if (w_pkt->stream_index == wav_st_idx) {
                if (avcodec_send_packet(wav_dec_ctx, w_pkt) >= 0) {
                    while (avcodec_receive_frame(wav_dec_ctx, w_frame) >= 0) {
                        const uint8_t **in_d = (const uint8_t **)w_frame->extended_data;
                        int in_n = w_frame->nb_samples;
                        while (in_n > 0 || swr_get_delay(a_swr, a_ctx->sample_rate) >= a_frame_size) {
                            av_frame_make_writable(a_frame);
                            int conv = swr_convert(a_swr, a_frame->data, a_frame_size, in_d, in_n);
                            in_d = NULL;
                            in_n = 0;
                            if (conv <= 0) break;
                            a_frame->nb_samples = conv;
                            a_frame->pts = a_pts;
                            a_pts += conv;
                            if (avcodec_send_frame(a_ctx, a_frame) >= 0) {
                                drain_encoder_packets(a_ctx, a_st, oc, enc_pkt);
                            }
                        }
                    }
                }
            }
            av_packet_unref(w_pkt);
        }
        avcodec_send_frame(a_ctx, NULL);
        drain_encoder_packets(a_ctx, a_st, oc, enc_pkt);
        av_packet_free(&w_pkt);
        av_frame_free(&w_frame);
        av_frame_free(&a_frame);
    }

    av_write_trailer(oc);

    av_packet_free(&enc_pkt);
    if (a_swr) swr_free(&a_swr);
    if (wav_dec_ctx) avcodec_free_context(&wav_dec_ctx);
    if (wav_fmt) avformat_close_input(&wav_fmt);
    if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
    avcodec_free_context(&a_ctx);
    avcodec_free_context(&v_ctx);
    avformat_free_context(oc);

    // Verify the output MP4 file is valid and non-empty
    FILE *chk = fopen(final_out_path, "rb");
    if (!chk) return false;
    fseek(chk, 0, SEEK_END);
    long out_sz = ftell(chk);
    fclose(chk);
    return (out_sz > 256);
}

// Fallback CLI FFmpeg Video + AAC Renderer (only invoked if the user's FFmpeg shared DLLs were built without an H.264 encoder)
// Never overwrites `.mp4` with raw `.wav` data!
static bool render_timeline_mp4_cli_fallback(ProjectTab *p, SDL_Renderer *ren,
                                             const char *wav_src_path, const char *final_out_path,
                                             double exp_start, double exp_end,
                                             int w, int h, double fps, int v_kbps, int a_kbps,
                                             int pct_encode) {
    update_export_progress_ui(ren, pct_encode, "Encoding MP4 video & AAC audio via FFmpeg...", true);
    bool any_solo = false, any_video_trk_sel = false, any_video_item_sel = false;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->tracks[t].solo && !is_track_hidden(t)) any_solo = true;
    }
    for (int i = 0; i < p->num_items; i++) {
        if (!p->items[i].has_video) continue;
        if (p->items[i].is_selected) any_video_item_sel = true;
        if (p->items[i].track_idx >= 0 && p->items[i].track_idx < p->num_tracks && p->track_selected[p->items[i].track_idx]) {
            any_video_trk_sel = true;
        }
    }

    // Decode frames via our native C FFmpeg decoder and pipe raw YUV420P frames into ffmpeg CLI if available
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -y -loglevel error -f rawvideo -pix_fmt yuv420p -s %dx%d -r %.2f -i - "
             "-i \"%s\" -c:v libx264 -pix_fmt yuv420p -b:v %dk -c:a aac -b:a %dk -ar %d -ac %d -movflags +faststart -shortest \"%s\"",
             w, h, fps, wav_src_path, v_kbps, a_kbps, g_export.sample_rate, g_export.channels, final_out_path);
#ifdef _WIN32
    FILE *pipe = _popen(cmd, "wb");
#else
    FILE *pipe = popen(cmd, "w");
#endif
    if (!pipe) return false;

    double total_dur = fmax(0.04, exp_end - exp_start);
    int64_t total_v_frames = (int64_t)(total_dur * fps + 0.5);
    if (total_v_frames < 1) total_v_frames = 1;

    ExportVideoClipDecoder clip_dec;
    memset(&clip_dec, 0, sizeof(clip_dec));
    clip_dec.video_stream_idx = -1;

    AVFrame *v_frame = av_frame_alloc();
    v_frame->format = AV_PIX_FMT_YUV420P;
    v_frame->width  = w;
    v_frame->height = h;
    av_frame_get_buffer(v_frame, 32);

    for (int64_t vf = 0; vf < total_v_frames; vf++) {
        double t_sec = exp_start + ((double)vf / fps);
        AudioItem *vc = find_timeline_video_clip_at_time(p, t_sec, any_solo, any_video_trk_sel, any_video_item_sel);
        bool got_vid = false;
        if (vc && open_export_video_clip_decoder(&clip_dec, vc->file_path, w, h)) {
            double clip_internal_sec = fmax(0.0, (t_sec - vc->start_time) + vc->offset_in_file);
            got_vid = decode_clip_frame_into_yuv420p(&clip_dec, clip_internal_sec, v_frame, w, h);
        }
        if (!got_vid) fill_yuv420p_frame_black(v_frame, w, h);

        for (int y = 0; y < h; y++) fwrite(v_frame->data[0] + y * v_frame->linesize[0], 1, (size_t)w, pipe);
        for (int y = 0; y < h / 2; y++) fwrite(v_frame->data[1] + y * v_frame->linesize[1], 1, (size_t)(w / 2), pipe);
        for (int y = 0; y < h / 2; y++) fwrite(v_frame->data[2] + y * v_frame->linesize[2], 1, (size_t)(w / 2), pipe);

        if ((vf & 3) == 0 || vf + 1 == total_v_frames) {
            int cur_p = pct_encode + (int)(((double)(vf + 1) / (double)total_v_frames) * (99 - pct_encode));
            update_export_progress_ui(ren, cur_p, "Encoding MP4 video & AAC audio...", false);
        }
    }

    av_frame_free(&v_frame);
    close_export_video_clip_decoder(&clip_dec);
#ifdef _WIN32
    int rc = _pclose(pipe);
#else
    int rc = pclose(pipe);
#endif
    return (rc == 0);
}

static const char* get_export_ext(void) {
    if (g_export.format == EXPORT_FMT_MP3) return "mp3";
    if (g_export.format == EXPORT_FMT_MP4) return "mp4";
    return "wav";
}

// Sanitizes the user's File Name (strips any duplicate .wav/.mp3/.mp4 extension) and builds `<dir>/<name>.<ext>`
// Never appends `_Item_19` or `_Track_1` suffixes!
static void build_clean_export_filepath(char *out_path, size_t max_len, char *out_clean_base, size_t base_max_len) {
    const char *ext = get_export_ext();
    const char *dir = (g_export.directory[0] != '\0') ? g_export.directory : ".";
    char clean_base[128];
    snprintf(clean_base, sizeof(clean_base), "%s", (g_export.filename[0] != '\0') ? g_export.filename : "output");

    // Trim trailing spaces
    size_t blen = strlen(clean_base);
    while (blen > 0 && (clean_base[blen - 1] == ' ' || clean_base[blen - 1] == '\t')) {
        clean_base[--blen] = '\0';
    }
    // Strip trailing .wav / .mp3 / .mp4 if the user typed an extension in the File Name box
    char *dot = strrchr(clean_base, '.');
    if (dot && (daw_strcasecmp_custom(dot, ".wav") == 0 ||
                daw_strcasecmp_custom(dot, ".mp3") == 0 ||
                daw_strcasecmp_custom(dot, ".mp4") == 0)) {
        *dot = '\0';
    }
    if (!clean_base[0]) strcpy(clean_base, "output");
    strncpy(g_export.filename, clean_base, sizeof(g_export.filename) - 1);
    g_export.filename[sizeof(g_export.filename) - 1] = '\0';

    if (out_clean_base && base_max_len > 0) {
        snprintf(out_clean_base, base_max_len, "%s", clean_base);
    }

    size_t dlen = strlen(dir);
    bool has_Trailing_sep = (dlen > 0 && (dir[dlen - 1] == '\\' || dir[dlen - 1] == '/'));
#ifdef _WIN32
    snprintf(out_path, max_len, "%s%s%s.%s", dir, has_Trailing_sep ? "" : "\\", clean_base, ext);
#else
    snprintf(out_path, max_len, "%s%s%s.%s", dir, has_Trailing_sep ? "" : "/", clean_base, ext);
#endif
}

static bool export_file_already_exists(const char *filepath) {
    if (!filepath || !filepath[0]) return false;
    FILE *f = fopen(filepath, "rb");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

// Generates a non-conflicting filename suggestion (e.g. "output_1") when the user clicks [Rename File]
static void suggest_renamed_export_filename(void) {
    char base[128];
    snprintf(base, sizeof(base), "%s", g_export.filename[0] ? g_export.filename : "output");
    // Strip existing _1, _2 suffix if present
    size_t len = strlen(base);
    while (len > 1 && isdigit((unsigned char)base[len - 1])) len--;
    if (len > 1 && base[len - 1] == '_') base[len - 1] = '\0';

    const char *dir = (g_export.directory[0] != '\0') ? g_export.directory : ".";
    const char *ext = get_export_ext();
    for (int n = 1; n < 1000; n++) {
        char cand_name[128];
        char cand_path[1024];
        snprintf(cand_name, sizeof(cand_name), "%.110s_%d", base, n);
#ifdef _WIN32
        snprintf(cand_path, sizeof(cand_path), "%s\\%s.%s", dir, cand_name, ext);
#else
        snprintf(cand_path, sizeof(cand_path), "%s/%s.%s", dir, cand_name, ext);
#endif
        if (!export_file_already_exists(cand_path)) {
            strncpy(g_export.filename, cand_name, sizeof(g_export.filename) - 1);
            g_export.filename[sizeof(g_export.filename) - 1] = '\0';
            return;
        }
    }
}

// Computes active export Start, End, and Duration timings for the Export Media header and export engine
static void get_export_time_bounds(ProjectTab *p, double *out_start, double *out_end) {
    double exp_start = 0.0;
    double exp_end   = 0.0;
    if (is_range_active()) {
        exp_start = get_range_start();
        exp_end   = get_range_end();
    } else if (p && g_export.source == EXPORT_SRC_ITEMS) {
        double min_s = 1e18, max_e = 0.0;
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].is_selected) {
                if (p->items[i].start_time < min_s) min_s = p->items[i].start_time;
                double ie = p->items[i].start_time + p->items[i].duration;
                if (ie > max_e) max_e = ie;
            }
        }
        if (max_e > min_s + 0.001) {
            exp_start = min_s;
            exp_end   = max_e;
        }
    }
    if (p && exp_end <= exp_start + 0.001) {
        exp_start = 0.0;
        for (int i = 0; i < p->num_items; i++) {
            double ie = p->items[i].start_time + p->items[i].duration;
            if (ie > exp_end) exp_end = ie;
        }
    }
    if (out_start) *out_start = exp_start;
    if (out_end)   *out_end   = exp_end;
}

static void daw_ensure_all_project_raw_audio_ready(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win);

// Executes the unified Export Media workflow based on all settings in g_export with live progress %, green 100% bar & full stability
static void execute_unified_export(ProjectTab *p, SDL_Renderer *ren) {
    if (!p) return;

    (void)daw_get_engine_cache_dir();
    daw_ensure_all_project_raw_audio_ready(p, ren, g_main_win);

    // Pause live playback and pause SDL audio device during export so file handles and mixer never conflict
    p->is_playing = false;
    g_engine_processing = true;
    if (g_daw.audio_device > 0) {
        SDL_LockAudioDevice(g_daw.audio_device);
        SDL_PauseAudioDevice(g_daw.audio_device, 1);
        SDL_UnlockAudioDevice(g_daw.audio_device);
    }

    ensure_all_timeline_video_flags_probed(p);

    const char *dir = (g_export.directory[0] != '\0') ? g_export.directory : ".";
    ensure_recording_directory_exists(dir);

    char final_path[1024];
    char clean_base[128];
    build_clean_export_filepath(final_path, sizeof(final_path), clean_base, sizeof(clean_base));

    // Compute exact export time bounds [exp_start, exp_end]
    double exp_start = 0.0;
    double exp_end = 0.0;
    get_export_time_bounds(p, &exp_start, &exp_end);
    if (exp_end <= exp_start + 0.001) exp_end = exp_start + 1.0;

    g_export.overwrite_prompt_open = false;
    g_export.is_rendering = true;
    g_export.render_start_ticks = SDL_GetTicks();
    g_export.last_ui_present_ticks = 0;
    update_export_progress_ui(ren, 0, "Starting export...", true);

    bool export_ok = false;

    if (g_export.format == EXPORT_FMT_WAV) {
        export_ok = render_range_to_wav_file(
            p, ren, final_path, exp_start, exp_end, -1, -1,
            g_export.sample_rate, g_export.channels, g_export.wav_bit_depth,
            g_export.two_pass_render, 0, 100
        );
    } else if (g_export.format == EXPORT_FMT_MP3) {
        char tmp_wav[512]; snprintf(tmp_wav, sizeof(tmp_wav), "%s/export_tmp_audio.wav", daw_get_engine_cache_dir());
        if (render_range_to_wav_file(
                p, ren, tmp_wav, exp_start, exp_end, -1, -1,
                g_export.sample_rate, g_export.channels, 24,
                g_export.two_pass_render, 0, 72)) {
            update_export_progress_ui(ren, 75, "Encoding MP3 audio...", true);
            export_ok = encode_wav_to_mp3_native_ffmpeg(
                ren, tmp_wav, final_path,
                g_export.sample_rate, g_export.channels, g_export.mp3_bitrate, 75, 100
            );
            if (!export_ok) {
                char cmd[2048];
                snprintf(cmd, sizeof(cmd),
                         "ffmpeg -y -loglevel error -i \"%s\" -vn -ar %d -ac %d -b:a %dk \"%s\"",
                         tmp_wav, g_export.sample_rate, g_export.channels, g_export.mp3_bitrate, final_path);
                export_ok = (system(cmd) == 0);
            }
            remove(tmp_wav);
        }
    } else if (g_export.format == EXPORT_FMT_MP4) {
        char tmp_wav[512]; snprintf(tmp_wav, sizeof(tmp_wav), "%s/export_tmp_video_audio.wav", daw_get_engine_cache_dir());
        int w = atoi(g_export.mp4_width_str);
        int h = atoi(g_export.mp4_height_str);
        if (w <= 0 || h <= 0) {
            int p_idx = get_current_mp4_res_preset_idx();
            if (p_idx < 0 || p_idx >= NUM_MP4_RES_PRESETS) p_idx = 3;
            w = g_mp4_res_presets[p_idx].width;
            h = g_mp4_res_presets[p_idx].height;
        }
        if (w < 64) w = 64;
        if (h < 64) h = 64;
        if (w > 7680) w = 7680;
        if (h > 4320) h = 4320;
        if (w & 1) w++;
        if (h & 1) h++;
        double fps = atof(g_export.mp4_fps_str);
        int v_kbps = atoi(g_export.mp4_video_kbps_str);
        int a_kbps = atoi(g_export.mp4_audio_kbps_str);
        if (fps <= 0.0) fps = 25.0;
        if (v_kbps <= 0) v_kbps = 2048;
        if (a_kbps <= 0) a_kbps = 192;

        if (render_range_to_wav_file(
                p, ren, tmp_wav, exp_start, exp_end, -1, -1,
                g_export.sample_rate, g_export.channels, 24,
                g_export.two_pass_render, 0, 30)) {
            export_ok = render_timeline_mp4_native_ffmpeg(
                p, ren, tmp_wav, final_path, exp_start, exp_end,
                w, h, fps, v_kbps, a_kbps,
                g_export.sample_rate, g_export.channels,
                g_export.two_pass_render, 30, 100
            );
            if (!export_ok) {
                export_ok = render_timeline_mp4_cli_fallback(
                    p, ren, tmp_wav, final_path, exp_start, exp_end,
                    w, h, fps, v_kbps, a_kbps, 40
                );
            }
            remove(tmp_wav);
        }
    }

    // Resume SDL audio device cleanly after export finishes
    if (g_daw.audio_device > 0) {
        SDL_LockAudioDevice(g_daw.audio_device);
        SDL_PauseAudioDevice(g_daw.audio_device, 0);
        SDL_UnlockAudioDevice(g_daw.audio_device);
    }
    g_engine_processing = false;
    g_export.is_rendering = false;

    char done_msg[256];
    double final_time = (double)(SDL_GetTicks() - g_export.render_start_ticks) / 1000.0;
    if (export_ok) {
        snprintf(done_msg, sizeof(done_msg),
                 "Export completed: %s (100%% in %.2fs)",
                 final_path, final_time);
        record_project_change("Exported Media File", false);
    } else {
        snprintf(done_msg, sizeof(done_msg),
                 "Export failed: Could not write %s", final_path);
    }
    update_export_progress_ui(ren, export_ok ? 100 : 0, done_msg, true);
}

// Checks if the target export file already exists in the destination folder:
// If it exists, opens the "File Already Exists" modal prompt (Replace Existing File / Rename File / Cancel).
// Otherwise starts `execute_unified_export` immediately.
static void request_start_unified_export(ProjectTab *p, SDL_Renderer *ren) {
    if (!p) return;
    char target_path[1024];
    build_clean_export_filepath(target_path, sizeof(target_path), NULL, 0);
    if (export_file_already_exists(target_path)) {
        strncpy(g_export.pending_conflict_path, target_path, sizeof(g_export.pending_conflict_path) - 1);
        g_export.pending_conflict_path[sizeof(g_export.pending_conflict_path) - 1] = '\0';
        g_export.overwrite_prompt_open = true;
        return;
    }
    execute_unified_export(p, ren);
}

// Opens Windows save/browse dialog to pick output directory & filename for g_export
static void browse_export_directory_and_filename(void) {
    char chosen[MAX_PATH] = {0};
    const char *ext = get_export_ext();
    const char *fn_init = g_export.filename[0] ? g_export.filename : "output";
    if (g_export.directory[0] && strcmp(g_export.directory, ".") != 0) {
        snprintf(chosen, sizeof(chosen), "%s\\%s.%s", g_export.directory, fn_init, ext);
    } else {
        snprintf(chosen, sizeof(chosen), "%s.%s", fn_init, ext);
    }
    if (open_generic_dialog(chosen, sizeof(chosen), "Export Media File\0*.*\0", true)) {
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
                char *dot = strrchr(g_export.filename, '.');
                if (dot) *dot = '\0';
            }
        } else {
            strncpy(g_export.filename, chosen, sizeof(g_export.filename) - 1);
            g_export.filename[sizeof(g_export.filename) - 1] = '\0';
            char *dot = strrchr(g_export.filename, '.');
            if (dot) *dot = '\0';
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

// ================= REAPER-STYLE INSTANT METADATA & PEAK LOADING + BACKGROUND STREAM WORKER =================
#define MAX_BG_MEDIA_QUEUE 256
typedef struct {
    char file_path[512];
    char raw_audio_path[512];
    char proj_pk_path[512];
    char cache_pk_path[512];
} BgMediaPrepTask;

static BgMediaPrepTask g_bg_media_queue[MAX_BG_MEDIA_QUEUE];
static int             g_bg_media_q_head = 0;
static int             g_bg_media_q_tail = 0;
static SDL_mutex      *g_bg_media_mutex  = NULL;
static SDL_Thread     *g_bg_media_thread = NULL;
static volatile int    g_bg_media_running = 0;
static volatile int    g_bg_media_completed_serial = 0;
static int             g_bg_media_observed_serial  = 0;

static void daw_init_bg_media_system(void) {
    if (!g_bg_media_mutex) {
        g_bg_media_mutex = SDL_CreateMutex();
    }
}

// Decodes a single media file into <raw_audio_path>.part and writes .pk sidecar files on the background worker thread
static void daw_bg_decode_single_media_task(const BgMediaPrepTask *task) {
    if (!task || !task->file_path[0] || !task->raw_audio_path[0]) return;

    FILE *chk_raw = daw_fopen_LockFreeRead(task->raw_audio_path);
    if (chk_raw) {
        fclose(chk_raw);
        FILE *chk_pk = fopen(task->proj_pk_path[0] ? task->proj_pk_path : task->cache_pk_path, "rb");
        if (chk_pk) {
            fclose(chk_pk);
            return;
        }
    }

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, task->file_path, NULL, NULL) < 0) return;
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        avformat_close_input(&fmt);
        return;
    }

    int a_idx = -1, v_idx = -1;
    for (unsigned int i = 0; i < fmt->nb_streams; i++) {
        enum AVMediaType ctype = fmt->streams[i]->codecpar->codec_type;
        if (ctype == AVMEDIA_TYPE_AUDIO && a_idx == -1) a_idx = (int)i;
        else if (ctype == AVMEDIA_TYPE_VIDEO && v_idx == -1) {
            if (!(fmt->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)) v_idx = (int)i;
        }
    }
    if (v_idx == -1) {
        for (unsigned int i = 0; i < fmt->nb_streams; i++) {
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { v_idx = (int)i; break; }
        }
    }
    if (a_idx == -1) {
        avformat_close_input(&fmt);
        return;
    }

    bool has_video = (v_idx != -1);
    double total_sec = 0.0;
    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) {
        total_sec = (double)fmt->duration / AV_TIME_BASE;
    } else if (v_idx != -1 && fmt->streams[v_idx]->duration != AV_NOPTS_VALUE && fmt->streams[v_idx]->duration > 0) {
        total_sec = (double)fmt->streams[v_idx]->duration * av_q2d(fmt->streams[v_idx]->time_base);
    } else if (fmt->streams[a_idx]->duration != AV_NOPTS_VALUE && fmt->streams[a_idx]->duration > 0) {
        total_sec = (double)fmt->streams[a_idx]->duration * av_q2d(fmt->streams[a_idx]->time_base);
    }
    if (total_sec <= 0.0) total_sec = 10.0;

    AVCodecParameters *cpar = fmt->streams[a_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(cpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        return;
    }
    for (unsigned int s_i = 0; s_i < fmt->nb_streams; s_i++) {
#ifdef AVDISCARD_ALL
        if ((int)s_i != a_idx) fmt->streams[s_i]->discard = AVDISCARD_ALL;
#endif
    }
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, cpar);
    ctx->thread_count = 2;
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return;
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
        return;
    }

    char tmp_raw_path[540];
    snprintf(tmp_raw_path, sizeof(tmp_raw_path), "%s.part", task->raw_audio_path);
    FILE *f_raw = fopen(tmp_raw_path, "wb");
    if (!f_raw) {
        swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return;
    }
    setvbuf(f_raw, NULL, _IOFBF, 65536);

    int allocated_bins = (int)((total_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN) + 4096;
    if (allocated_bins < 4096) allocated_bins = 4096;
    PeakBin *peak_cache = (PeakBin *)calloc((size_t)allocated_bins, sizeof(PeakBin));
    int current_bin = 0;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int pcm_cap_frames = 16384;
    float *temp_pcm = (float *)malloc((size_t)pcm_cap_frames * 2 * sizeof(float));
    if (!temp_pcm) {
        fclose(f_raw);
        remove(tmp_raw_path);
        if (peak_cache) free(peak_cache);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return;
    }

    float cur_min = 0.0f, cur_max = 0.0f;
    int samples_in_bin = 0;
    int64_t total_written_frames = 0;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == a_idx && avcodec_send_packet(ctx, pkt) == 0) {
            while (avcodec_receive_frame(ctx, frame) == 0) {
                int needed_out = (int)((double)frame->nb_samples * (double)SAMPLE_RATE / (double)(ctx->sample_rate > 0 ? ctx->sample_rate : SAMPLE_RATE)) + 1024;
                if (needed_out > pcm_cap_frames) {
                    int new_cap = needed_out * 2;
                    float *grown_pcm = (float *)realloc(temp_pcm, (size_t)new_cap * 2 * sizeof(float));
                    if (grown_pcm) { temp_pcm = grown_pcm; pcm_cap_frames = new_cap; }
                }
                const uint8_t **in_data = (const uint8_t **)frame->extended_data;
                int in_samples = frame->nb_samples;
                for (;;) {
                    uint8_t *out_d[2] = { (uint8_t *)temp_pcm, NULL };
                    int converted = swr_convert(swr, out_d, pcm_cap_frames, in_data, in_samples);
                    in_data = NULL; in_samples = 0;
                    if (converted <= 0) break;
                    fwrite(temp_pcm, sizeof(float) * 2, (size_t)converted, f_raw);
                    total_written_frames += converted;
                    for (int s = 0; s < converted; s++) {
                        float mono = (temp_pcm[s * 2] + temp_pcm[s * 2 + 1]) * 0.5f;
                        if (samples_in_bin == 0) { cur_min = mono; cur_max = mono; }
                        else { if (mono < cur_min) cur_min = mono; if (mono > cur_max) cur_max = mono; }
                        if (++samples_in_bin >= PEAK_SAMPLES_PER_BIN) {
                            if (current_bin >= allocated_bins && peak_cache) {
                                int new_cap = allocated_bins * 2;
                                PeakBin *grown = (PeakBin *)realloc(peak_cache, (size_t)new_cap * sizeof(PeakBin));
                                if (grown) {
                                    memset(grown + allocated_bins, 0, (size_t)(new_cap - allocated_bins) * sizeof(PeakBin));
                                    peak_cache = grown; allocated_bins = new_cap;
                                }
                            }
                            if (peak_cache && current_bin < allocated_bins) {
                                peak_cache[current_bin].min_val = cur_min;
                                peak_cache[current_bin].max_val = cur_max;
                                current_bin++;
                            }
                            cur_min = 0.0f; cur_max = 0.0f; samples_in_bin = 0;
                        }
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }

    avcodec_send_packet(ctx, NULL);
    while (avcodec_receive_frame(ctx, frame) == 0) {
        const uint8_t **in_data = (const uint8_t **)frame->extended_data;
        int in_samples = frame->nb_samples;
        for (;;) {
            uint8_t *out_d[2] = { (uint8_t *)temp_pcm, NULL };
            int converted = swr_convert(swr, out_d, pcm_cap_frames, in_data, in_samples);
            in_data = NULL; in_samples = 0;
            if (converted <= 0) break;
            fwrite(temp_pcm, sizeof(float) * 2, (size_t)converted, f_raw);
            total_written_frames += converted;
            for (int s = 0; s < converted; s++) {
                float mono = (temp_pcm[s * 2] + temp_pcm[s * 2 + 1]) * 0.5f;
                if (mono < cur_min) cur_min = mono;
                if (mono > cur_max) cur_max = mono;
                if (++samples_in_bin >= PEAK_SAMPLES_PER_BIN) {
                    if (current_bin >= allocated_bins && peak_cache) {
                        int new_cap = allocated_bins * 2;
                        PeakBin *grown = (PeakBin *)realloc(peak_cache, (size_t)new_cap * sizeof(PeakBin));
                        if (grown) {
                            memset(grown + allocated_bins, 0, (size_t)(new_cap - allocated_bins) * sizeof(PeakBin));
                            peak_cache = grown; allocated_bins = new_cap;
                        }
                    }
                    if (peak_cache && current_bin < allocated_bins) {
                        peak_cache[current_bin].min_val = cur_min;
                        peak_cache[current_bin].max_val = cur_max;
                        current_bin++;
                    }
                    cur_min = 0.0f; cur_max = 0.0f; samples_in_bin = 0;
                }
            }
        }
    }
    for (;;) {
        uint8_t *out_d[2] = { (uint8_t *)temp_pcm, NULL };
        int converted = swr_convert(swr, out_d, pcm_cap_frames, NULL, 0);
        if (converted <= 0) break;
        fwrite(temp_pcm, sizeof(float) * 2, (size_t)converted, f_raw);
        total_written_frames += converted;
    }
    if (samples_in_bin > 0 && peak_cache && current_bin < allocated_bins) {
        peak_cache[current_bin].min_val = cur_min;
        peak_cache[current_bin].max_val = cur_max;
        current_bin++;
    }

    free(temp_pcm);
    fflush(f_raw);
    fclose(f_raw);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    double actual_dur = (total_written_frames > 0) ? ((double)total_written_frames / SAMPLE_RATE) : total_sec;
    if (peak_cache && current_bin > 0) {
        if (task->cache_pk_path[0]) daw_save_pk_file(task->cache_pk_path, peak_cache, current_bin, actual_dur, has_video);
        if (task->proj_pk_path[0])  daw_save_pk_file(task->proj_pk_path,  peak_cache, current_bin, actual_dur, has_video);
    }
    if (peak_cache) free(peak_cache);

#ifdef _WIN32
    if (!MoveFileExA(tmp_raw_path, task->raw_audio_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        remove(task->raw_audio_path);
        rename(tmp_raw_path, task->raw_audio_path);
    }
#else
    rename(tmp_raw_path, task->raw_audio_path);
#endif
    g_bg_media_completed_serial++;
}

static int SDLCALL daw_bg_media_worker_thread_fn(void *data) {
    (void)data;
    for (;;) {
        BgMediaPrepTask task;
        bool has_task = false;
        if (g_bg_media_mutex) SDL_LockMutex(g_bg_media_mutex);
        if (g_bg_media_q_head != g_bg_media_q_tail) {
            task = g_bg_media_queue[g_bg_media_q_head];
            g_bg_media_q_head = (g_bg_media_q_head + 1) % MAX_BG_MEDIA_QUEUE;
            has_task = true;
        } else {
            g_bg_media_running = 0;
        }
        if (g_bg_media_mutex) SDL_UnlockMutex(g_bg_media_mutex);

        if (!has_task) break;
        daw_bg_decode_single_media_task(&task);
    }
    return 0;
}

static void daw_enqueue_bg_media_prep(const char *file_path, const char *raw_audio_path,
                                      const char *proj_pk_path, const char *cache_pk_path) {
    if (!file_path || !file_path[0] || !raw_audio_path || !raw_audio_path[0]) return;
    daw_init_bg_media_system();
    if (g_bg_media_mutex) SDL_LockMutex(g_bg_media_mutex);

    // Avoid duplicate queue entries for the same raw_audio_path
    int idx = g_bg_media_q_head;
    while (idx != g_bg_media_q_tail) {
        if (strcmp(g_bg_media_queue[idx].raw_audio_path, raw_audio_path) == 0) {
            if (g_bg_media_mutex) SDL_UnlockMutex(g_bg_media_mutex);
            return;
        }
        idx = (idx + 1) % MAX_BG_MEDIA_QUEUE;
    }

    int next_tail = (g_bg_media_q_tail + 1) % MAX_BG_MEDIA_QUEUE;
    if (next_tail != g_bg_media_q_head) {
        BgMediaPrepTask *t = &g_bg_media_queue[g_bg_media_q_tail];
        strncpy(t->file_path, file_path, sizeof(t->file_path) - 1);
        t->file_path[sizeof(t->file_path) - 1] = '\0';
        strncpy(t->raw_audio_path, raw_audio_path, sizeof(t->raw_audio_path) - 1);
        t->raw_audio_path[sizeof(t->raw_audio_path) - 1] = '\0';
        if (proj_pk_path) {
            strncpy(t->proj_pk_path, proj_pk_path, sizeof(t->proj_pk_path) - 1);
            t->proj_pk_path[sizeof(t->proj_pk_path) - 1] = '\0';
        } else {
            t->proj_pk_path[0] = '\0';
        }
        if (cache_pk_path) {
            strncpy(t->cache_pk_path, cache_pk_path, sizeof(t->cache_pk_path) - 1);
            t->cache_pk_path[sizeof(t->cache_pk_path) - 1] = '\0';
        } else {
            t->cache_pk_path[0] = '\0';
        }
        g_bg_media_q_tail = next_tail;
    }

    bool need_spawn = (g_bg_media_running == 0);
    if (need_spawn) g_bg_media_running = 1;
    if (g_bg_media_mutex) SDL_UnlockMutex(g_bg_media_mutex);

    if (need_spawn) {
        if (g_bg_media_thread) {
            SDL_WaitThread(g_bg_media_thread, NULL);
            g_bg_media_thread = NULL;
        }
        g_bg_media_thread = SDL_CreateThread(daw_bg_media_worker_thread_fn, "DawBgMediaWorker", NULL);
    }
}

// Polls on the main UI thread to attach any background-prepared .raw handles and .pk waveform peaks
static void daw_poll_bg_prepared_media_on_main_thread(void) {
    int cur_ser = g_bg_media_completed_serial;
    if (cur_ser == g_bg_media_observed_serial) return;
    g_bg_media_observed_serial = cur_ser;

    for (int t = 0; t < g_daw.num_tabs; t++) {
        ProjectTab *tp = &g_daw.tabs[t];
        for (int i = 0; i < tp->num_items; i++) {
            AudioItem *it = &tp->items[i];
            if (!it->raw_audio_path[0]) continue;
            if (!it->peaks) {
                int p_cnt = 0; double p_dur = 0.0; bool p_vid = false;
                PeakBin *sh = daw_find_shared_peaks(it->raw_audio_path, &p_cnt, &p_dur, &p_vid);
                if (sh && p_dur > 0.0) {
                    it->peaks = sh;
                    it->peak_count = p_cnt;
                    if (it->file_total_duration <= 0.0) it->file_total_duration = p_dur;
                } else {
                    char proj_pk[512] = {0};
                    daw_build_project_pk_path(it->file_path, proj_pk, sizeof(proj_pk));
                    unsigned int h = hash_path(it->file_path);
                    char cache_pk[512] = {0};
                    daw_build_pk_cache_path(h, cache_pk, sizeof(cache_pk));
                    PeakBin *loaded_pk = NULL;
                    if (daw_load_pk_file(proj_pk, &loaded_pk, &p_cnt, &p_dur, &p_vid) ||
                        daw_load_pk_file(cache_pk, &loaded_pk, &p_cnt, &p_dur, &p_vid)) {
                        it->peaks = daw_register_shared_peaks(it->raw_audio_path, loaded_pk, p_cnt, p_dur, p_vid);
                        it->peak_count = p_cnt;
                        if (it->file_total_duration <= 0.0) it->file_total_duration = p_dur;
                        if (p_vid) it->has_video = true;
                    }
                }
            }
            if (!it->raw_file_handle) {
                FILE *fp = daw_fopen_LockFreeRead(it->raw_audio_path);
                if (fp) {
                    if (g_daw.audio_device > 0) SDL_LockAudioDevice(g_daw.audio_device);
                    it->raw_file_handle = fp;
                    if (g_daw.audio_device > 0) SDL_UnlockAudioDevice(g_daw.audio_device);
                }
            }
        }
    }
}

// Ensures all clips in ProjectTab `p` have their .raw audio handles ready before offline Export / Glue / Reverse / Normalize
static void daw_ensure_all_project_raw_audio_ready(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win) {
    if (!p) return;
    if (g_bg_media_running && g_bg_media_thread) {
        SDL_WaitThread(g_bg_media_thread, NULL);
        g_bg_media_thread = NULL;
    }
    daw_poll_bg_prepared_media_on_main_thread();
    for (int i = 0; i < p->num_items; i++) {
        AudioItem *it = &p->items[i];
        if (!it->raw_file_handle && it->raw_audio_path[0]) {
            it->raw_file_handle = daw_fopen_LockFreeRead(it->raw_audio_path);
            if (!it->raw_file_handle && it->file_path[0]) {
                BgMediaPrepTask task;
                memset(&task, 0, sizeof(task));
                strncpy(task.file_path, it->file_path, sizeof(task.file_path) - 1);
                strncpy(task.raw_audio_path, it->raw_audio_path, sizeof(task.raw_audio_path) - 1);
                daw_build_project_pk_path(it->file_path, task.proj_pk_path, sizeof(task.proj_pk_path));
                daw_build_pk_cache_path(hash_path(it->file_path), task.cache_pk_path, sizeof(task.cache_pk_path));
                (void)ren; (void)win;
                daw_bg_decode_single_media_task(&task);
                it->raw_file_handle = daw_fopen_LockFreeRead(it->raw_audio_path);
            }
        }
    }
}

// REAPER-Style Instant Metadata & Sidecar Peak Loader for Project Open (0 ms UI wait, NEVER shows "Importing Media...")
static int daw_load_clip_metadata_fast_reaper(AudioItem *item, const char *filepath,
                                              double saved_file_tot_dur, double saved_clip_dur,
                                              bool saved_has_video) {
    if (!item || !filepath || !filepath[0]) return -1;
    (void)daw_get_engine_cache_dir();

    const char *name = strrchr(filepath, '\\');
    if (!name) name = strrchr(filepath, '/');
    const char *short_name = name ? name + 1 : filepath;

    strncpy(item->file_path, filepath, sizeof(item->file_path) - 1);
    item->file_path[sizeof(item->file_path) - 1] = '\0';
    snprintf(item->label, sizeof(item->label), "%s", short_name);

    unsigned int h = hash_path(filepath);
    daw_build_raw_cache_path(h, item->raw_audio_path, sizeof(item->raw_audio_path));

    char pk_cache_path[512] = {0};
    char proj_pk_path[512]  = {0};
    daw_build_pk_cache_path(h, pk_cache_path, sizeof(pk_cache_path));
    daw_build_project_pk_path(filepath, proj_pk_path, sizeof(proj_pk_path));

    bool has_vid_flag = saved_has_video;
    if (!has_vid_flag) {
        const char *ext_chk = strrchr(filepath, '.');
        if (ext_chk && strstr(".mp4.MP4.mov.MOV.mkv.MKV.avi.AVI.webm.WEBM", ext_chk) != NULL) {
            has_vid_flag = true;
        }
    }
    item->has_video = has_vid_flag;
    double fallback_dur = (saved_file_tot_dur > 0.0) ? saved_file_tot_dur : ((saved_clip_dur > 0.0) ? saved_clip_dur : 10.0);
    item->file_total_duration = fallback_dur;
    item->duration = (saved_clip_dur > 0.0) ? saved_clip_dur : fallback_dur;
    item->offset_in_file = 0.0;
    item->is_selected = false;

    // 1. Check in-RAM shared peak pool first (0 ms)
    int pooled_cnt = 0;
    double pooled_dur = 0.0;
    bool pooled_vid = false;
    PeakBin *pooled_pk = daw_find_shared_peaks(item->raw_audio_path, &pooled_cnt, &pooled_dur, &pooled_vid);
    if (pooled_pk && pooled_dur > 0.0) {
        item->peaks = pooled_pk;
        item->peak_count = pooled_cnt;
        item->file_total_duration = pooled_dur;
        if (pooled_vid) item->has_video = true;
    } else {
        // 2. Load REAPER-style sidecar .pk peak file from <Project>/media/<clip>.pk or engine cache (< 1 ms)
        PeakBin *loaded_pk = NULL;
        int loaded_cnt = 0;
        double loaded_dur = 0.0;
        bool loaded_vid = false;
        bool from_proj_pk = daw_load_pk_file(proj_pk_path, &loaded_pk, &loaded_cnt, &loaded_dur, &loaded_vid);
        bool from_cache_pk = false;
        if (!from_proj_pk) {
            from_cache_pk = daw_load_pk_file(pk_cache_path, &loaded_pk, &loaded_cnt, &loaded_dur, &loaded_vid);
        }
        if ((from_proj_pk || from_cache_pk) && loaded_dur > 0.0) {
            if (!from_proj_pk && proj_pk_path[0] && loaded_pk && loaded_cnt > 0) {
                daw_save_pk_file(proj_pk_path, loaded_pk, loaded_cnt, loaded_dur, loaded_vid || has_vid_flag);
            }
            if (loaded_vid) item->has_video = true;
            item->peaks = daw_register_shared_peaks(item->raw_audio_path, loaded_pk, loaded_cnt, loaded_dur, item->has_video);
            item->peak_count = loaded_cnt;
            item->file_total_duration = loaded_dur;
        }
    }

    // 3. Open .raw audio stream handle if already in engine cache; otherwise queue silent background preparation
    item->raw_file_handle = daw_fopen_LockFreeRead(item->raw_audio_path);
    if (item->raw_file_handle && !item->peaks) {
        int rb_cnt = 0;
        double rb_dur = 0.0;
        PeakBin *rb_pk = daw_rebuild_peaks_from_raw_file(item->raw_audio_path, pk_cache_path, &rb_cnt, &rb_dur, item->has_video);
        if (rb_pk && rb_dur > 0.0) {
            if (proj_pk_path[0]) daw_save_pk_file(proj_pk_path, rb_pk, rb_cnt, rb_dur, item->has_video);
            item->peaks = rb_pk;
            item->peak_count = rb_cnt;
            item->file_total_duration = rb_dur;
        }
    } else if (!item->raw_file_handle || !item->peaks) {
        daw_enqueue_bg_media_prep(filepath, item->raw_audio_path, proj_pk_path, pk_cache_path);
    }
    return 0;
}

// Stable, UI-responsive media importer with disk peak-cache reuse and live "Importing Media..." popup window
static int import_media_with_progress(SDL_Renderer *ren, SDL_Window *win,
                                      const char *filepath, AudioItem *item,
                                      int item_num, int total_items) {
    if (!filepath || !filepath[0] || !item) return -1;

    (void)daw_get_engine_cache_dir();

    Uint32 start_ticks = SDL_GetTicks();
    Uint32 last_ui_ticks = 0;

    const char *name = strrchr(filepath, '\\');
    if (!name) name = strrchr(filepath, '/');
    const char *short_name = name ? name + 1 : filepath;

    strncpy(item->file_path, filepath, sizeof(item->file_path) - 1);
    item->file_path[sizeof(item->file_path) - 1] = '\0';
    snprintf(item->label, sizeof(item->label), "%s", short_name);

    unsigned int h = hash_path(filepath);
    daw_build_raw_cache_path(h, item->raw_audio_path, sizeof(item->raw_audio_path));
    char pk_cache_path[512] = {0};
    char proj_pk_path[512]  = {0};
    daw_build_pk_cache_path(h, pk_cache_path, sizeof(pk_cache_path));
    daw_build_project_pk_path(filepath, proj_pk_path, sizeof(proj_pk_path));

    // Ultra-fast path 0: check in-RAM shared peak pool first (0 RAM allocation, 0 disk re-decode, 0 popup)
    {
        int pooled_cnt = 0;
        double pooled_dur = 0.0;
        bool pooled_vid = false;
        PeakBin *pooled_pk = daw_find_shared_peaks(item->raw_audio_path, &pooled_cnt, &pooled_dur, &pooled_vid);
        if (pooled_pk && pooled_dur > 0.0) {
            FILE *f_existing = daw_fopen_LockFreeRead(item->raw_audio_path);
            if (f_existing) {
                item->peaks = pooled_pk;
                item->peak_count = pooled_cnt;
                item->file_total_duration = pooled_dur;
                item->duration = pooled_dur;
                item->offset_in_file = 0.0;
                item->is_selected = false;
                item->has_video = pooled_vid;
                item->raw_file_handle = f_existing;
                if (proj_pk_path[0] && pooled_cnt > 0) {
                    FILE *chk_ppk = fopen(proj_pk_path, "rb");
                    if (chk_ppk) fclose(chk_ppk);
                    else daw_save_pk_file(proj_pk_path, pooled_pk, pooled_cnt, pooled_dur, pooled_vid);
                }
                return 0;
            }
        }
    }

    // Fast path 1: check if both .raw and .pk cache files already exist on disk from an earlier import
    FILE *chk_raw = daw_fopen_LockFreeRead(item->raw_audio_path);
    if (chk_raw) {
        PeakBin *pk_buf = NULL;
        int pk_cnt = 0;
        double cached_dur = 0.0;
        bool cached_vid = false;
        bool got_proj_pk = daw_load_pk_file(proj_pk_path, &pk_buf, &pk_cnt, &cached_dur, &cached_vid);
        bool got_cache_pk = false;
        if (!got_proj_pk) {
            got_cache_pk = daw_load_pk_file(pk_cache_path, &pk_buf, &pk_cnt, &cached_dur, &cached_vid);
        }
        if ((got_proj_pk || got_cache_pk) && cached_dur > 0.0) {
            if (!got_proj_pk && proj_pk_path[0] && pk_buf && pk_cnt > 0) {
                daw_save_pk_file(proj_pk_path, pk_buf, pk_cnt, cached_dur, cached_vid);
            }
            item->has_video = cached_vid;
            item->peaks = daw_register_shared_peaks(item->raw_audio_path, pk_buf, pk_cnt, cached_dur, item->has_video);
            item->peak_count = pk_cnt;
            item->file_total_duration = cached_dur;
            item->duration = cached_dur;
            item->offset_in_file = 0.0;
            item->is_selected = false;
            item->raw_file_handle = chk_raw;
            return 0;
        }
        // Existing .raw file is already on disk, rebuild .pk directly
        fclose(chk_raw);
        bool has_vid_metadata_flag = false;
        const char *ext_chk = strrchr(filepath, '.');
        if (ext_chk && (strstr(".mp4.MP4.mov.MOV.mkv.MKV.avi.AVI.webm.WEBM", ext_chk) != NULL)) {
            has_vid_metadata_flag = true;
        }
        int rb_cnt = 0;
        double rb_dur = 0.0;
        PeakBin *rb_pk = daw_rebuild_peaks_from_raw_file(item->raw_audio_path, pk_cache_path, &rb_cnt, &rb_dur, has_vid_metadata_flag);
        if (rb_pk && rb_dur > 0.0) {
            if (proj_pk_path[0]) daw_save_pk_file(proj_pk_path, rb_pk, rb_cnt, rb_dur, has_vid_metadata_flag);
            item->has_video = has_vid_metadata_flag;
            item->peaks = rb_pk;
            item->peak_count = rb_cnt;
            item->file_total_duration = rb_dur;
            item->duration = rb_dur;
            item->offset_in_file = 0.0;
            item->is_selected = false;
            item->raw_file_handle = daw_fopen_LockFreeRead(item->raw_audio_path);
            return 0;
        }
    }

    render_import_progress_popup(ren, win, short_name, item_num, total_items, 0, start_ticks);

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

    for (unsigned int s_i = 0; s_i < fmt->nb_streams; s_i++) {
        #ifdef AVDISCARD_ALL
        if ((int)s_i != a_idx) fmt->streams[s_i]->discard = AVDISCARD_ALL;
#endif
    }
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, cpar);
    ctx->thread_count = 2;
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
    int pcm_cap_frames = 16384;
    float *temp_pcm = (float *)malloc((size_t)pcm_cap_frames * 2 * sizeof(float));
    if (!temp_pcm) {
        fclose(f_raw);
        if (peak_cache) free(peak_cache);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return -1;
    }
    float cur_min = 0.0f, cur_max = 0.0f;
    int samples_in_bin = 0;
    int64_t total_written_frames = 0;
    double expected_frames = total_sec * (double)SAMPLE_RATE;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == a_idx) {
            if (avcodec_send_packet(ctx, pkt) == 0) {
                while (avcodec_receive_frame(ctx, frame) == 0) {
                    int needed_out = (int)((double)frame->nb_samples * (double)SAMPLE_RATE / (double)(ctx->sample_rate > 0 ? ctx->sample_rate : SAMPLE_RATE)) + 1024;
                    if (needed_out < frame->nb_samples + 256) needed_out = frame->nb_samples + 256;
                    if (needed_out > pcm_cap_frames) {
                        int new_cap = needed_out * 2;
                        float *grown_pcm = (float *)realloc(temp_pcm, (size_t)new_cap * 2 * sizeof(float));
                        if (grown_pcm) {
                            temp_pcm = grown_pcm;
                            pcm_cap_frames = new_cap;
                        }
                    }
                    const uint8_t **in_data = (const uint8_t **)frame->extended_data;
                    int in_samples = frame->nb_samples;
                    for (;;) {
                        uint8_t *out_d[2] = { (uint8_t *)temp_pcm, NULL };
                        int converted = swr_convert(swr, out_d, pcm_cap_frames, in_data, in_samples);
                        in_data = NULL;
                        in_samples = 0;
                        if (converted <= 0) break;
                        fwrite(temp_pcm, sizeof(float) * 2, (size_t)converted, f_raw);
                        total_written_frames += converted;

                        for (int s = 0; s < converted; s++) {
                            float mono = (temp_pcm[s * 2] + temp_pcm[s * 2 + 1]) * 0.5f;
                            if (samples_in_bin == 0) {
                                cur_min = mono;
                                cur_max = mono;
                            } else {
                                if (mono < cur_min) cur_min = mono;
                                if (mono > cur_max) cur_max = mono;
                            }
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

    // Flush decoder and resampler so 100% of audio frames and final peak bin are written
    avcodec_send_packet(ctx, NULL);
    while (avcodec_receive_frame(ctx, frame) == 0) {
        const uint8_t **in_data = (const uint8_t **)frame->extended_data;
        int in_samples = frame->nb_samples;
        for (;;) {
            uint8_t *out_d[2] = { (uint8_t *)temp_pcm, NULL };
            int converted = swr_convert(swr, out_d, pcm_cap_frames, in_data, in_samples);
            in_data = NULL;
            in_samples = 0;
            if (converted <= 0) break;
            fwrite(temp_pcm, sizeof(float) * 2, (size_t)converted, f_raw);
            total_written_frames += converted;
            for (int s = 0; s < converted; s++) {
                float mono = (temp_pcm[s * 2] + temp_pcm[s * 2 + 1]) * 0.5f;
                if (samples_in_bin == 0) {
                    cur_min = mono;
                    cur_max = mono;
                } else {
                    if (mono < cur_min) cur_min = mono;
                    if (mono > cur_max) cur_max = mono;
                }
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
    for (;;) {
        uint8_t *out_d[2] = { (uint8_t *)temp_pcm, NULL };
        int converted = swr_convert(swr, out_d, pcm_cap_frames, NULL, 0);
        if (converted <= 0) break;
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
    if (samples_in_bin > 0 && peak_cache && current_bin < allocated_bins) {
        peak_cache[current_bin].min_val = cur_min;
        peak_cache[current_bin].max_val = cur_max;
        current_bin++;
    }
    if (peak_cache && current_bin > 0 && current_bin < allocated_bins) {
        PeakBin *shrunk = (PeakBin *)realloc(peak_cache, (size_t)current_bin * sizeof(PeakBin));
        if (shrunk) peak_cache = shrunk;
    }

    free(temp_pcm);
    fclose(f_raw);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    double actual_dur = (total_written_frames > 0) ? ((double)total_written_frames / SAMPLE_RATE) : total_sec;
    item->peaks = daw_register_shared_peaks(item->raw_audio_path, peak_cache, current_bin, actual_dur, item->has_video);
    item->peak_count = current_bin;
    item->file_total_duration = actual_dur;
    item->duration = actual_dur;
    item->offset_in_file = 0.0;
    item->is_selected = false;

    // Save peak cache (.pk) to both engine cache and <Project>/media/<clip>.pk (REAPER-style sidecar peaks)
    daw_save_pk_file(pk_cache_path, item->peaks, current_bin, actual_dur, item->has_video);
    if (proj_pk_path[0]) {
        daw_save_pk_file(proj_pk_path, item->peaks, current_bin, actual_dur, item->has_video);
    }

    item->raw_file_handle = daw_fopen_LockFreeRead(item->raw_audio_path);
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

static bool daw_paths_equal_norm(const char *a, const char *b);
static void ensure_recording_directory_exists(const char *dir_path);

// Resolves and creates the current project's <ProjectFolder>/media directory
static void get_project_media_folder(ProjectTab *p, char *out_media_dir, size_t max_len) {
    if (!out_media_dir || max_len == 0) return;
    out_media_dir[0] = '\0';

    if (p && p->project_path[0] != '\0') {
        char proj_dir[512] = {0};
        const char *ls1 = strrchr(p->project_path, '\\');
        const char *ls2 = strrchr(p->project_path, '/');
        if (ls2 && (!ls1 || ls2 > ls1)) ls1 = ls2;
        if (ls1) {
            size_t dl = (size_t)(ls1 - p->project_path);
            if (dl >= sizeof(proj_dir)) dl = sizeof(proj_dir) - 1;
            memcpy(proj_dir, p->project_path, dl);
            proj_dir[dl] = '\0';
        } else {
            strcpy(proj_dir, ".");
        }
#ifdef _WIN32
        snprintf(out_media_dir, max_len, "%s\\media", proj_dir);
#else
        snprintf(out_media_dir, max_len, "%s/media", proj_dir);
#endif
        ensure_recording_directory_exists(proj_dir);
        ensure_recording_directory_exists(out_media_dir);
        strncpy(g_rec.folder_path, out_media_dir, sizeof(g_rec.folder_path) - 1);
        g_rec.folder_path[sizeof(g_rec.folder_path) - 1] = '\0';
        return;
    }

    if (g_rec.folder_path[0] != '\0') {
        snprintf(out_media_dir, max_len, "%s", g_rec.folder_path);
    } else {
        snprintf(out_media_dir, max_len, "media");
    }
    ensure_recording_directory_exists(out_media_dir);
}

// Checks if filepath is already directly inside target_dir (so files imported from <Project>/media are not re-copied)
static bool is_file_already_in_media_dir(const char *filepath, const char *media_dir) {
    if (!filepath || !filepath[0] || !media_dir || !media_dir[0]) return false;
    char file_parent[512] = {0};
    const char *ls1 = strrchr(filepath, '\\');
    const char *ls2 = strrchr(filepath, '/');
    if (ls2 && (!ls1 || ls2 > ls1)) ls1 = ls2;
    if (!ls1) return false;
    size_t dl = (size_t)(ls1 - filepath);
    if (dl >= sizeof(file_parent)) dl = sizeof(file_parent) - 1;
    memcpy(file_parent, filepath, dl);
    file_parent[dl] = '\0';
    return daw_paths_equal_norm(file_parent, media_dir);
}

static int64_t get_file_byte_size_helper(const char *path) {
    if (!path || !path[0]) return -1;
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    if (daw_fseek64(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    int64_t sz = daw_ftell64(fp);
    fclose(fp);
    return sz;
}

static bool copy_binary_file_to_dest(const char *src_path, const char *dst_path) {
    if (!src_path || !src_path[0] || !dst_path || !dst_path[0]) return false;
    if (daw_paths_equal_norm(src_path, dst_path)) return true;
#ifdef _WIN32
    if (CopyFileA(src_path, dst_path, FALSE)) return true;
#endif
    FILE *in = fopen(src_path, "rb");
    if (!in) return false;
    FILE *out = fopen(dst_path, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    fflush(out);
    fclose(in);
    fclose(out);
    return ok;
}

// Requirement 5: Any media item dropped or imported into the app from outside <Project>/media is copied into <Project>/media.
// If the file is already inside <Project>/media, no need to copy.
static void copy_media_to_project_media_folder_if_needed(ProjectTab *p, const char *src_path,
                                                         char *out_path, size_t max_len) {
    if (!out_path || max_len == 0) return;
    if (!src_path || !src_path[0]) {
        out_path[0] = '\0';
        return;
    }

    char media_dir[512] = {0};
    get_project_media_folder(p, media_dir, sizeof(media_dir));

    // If file is already inside <Project>/media, do not copy
    if (is_file_already_in_media_dir(src_path, media_dir)) {
        snprintf(out_path, max_len, "%s", src_path);
        return;
    }

    const char *fn = strrchr(src_path, '\\');
    const char *fn2 = strrchr(src_path, '/');
    if (fn2 && (!fn || fn2 > fn)) fn = fn2;
    const char *short_name = fn ? (fn + 1) : src_path;
    if (!short_name[0]) {
        snprintf(out_path, max_len, "%s", src_path);
        return;
    }

    char dst_cand[512] = {0};
#ifdef _WIN32
    snprintf(dst_cand, sizeof(dst_cand), "%s\\""%s", media_dir, short_name);
#else
    snprintf(dst_cand, sizeof(dst_cand), "%s/%s", media_dir, short_name);
#endif

    if (daw_paths_equal_norm(src_path, dst_cand)) {
        snprintf(out_path, max_len, "%s", src_path);
        return;
    }

    int64_t src_sz = get_file_byte_size_helper(src_path);
    int64_t dst_sz = get_file_byte_size_helper(dst_cand);
    if (dst_sz >= 0) {
        if (src_sz > 0 && dst_sz == src_sz) {
            // Identical-sized file already exists in <Project>/media
            snprintf(out_path, max_len, "%s", dst_cand);
            return;
        }
        // Different file with same name exists: pick <stem>_1.<ext>, <stem>_2.<ext>, ...
        char stem[128] = {0};
        char ext[32]   = {0};
        const char *dot = strrchr(short_name, '.');
        if (dot && dot > short_name) {
            size_t sl = (size_t)(dot - short_name);
            if (sl >= sizeof(stem)) sl = sizeof(stem) - 1;
            memcpy(stem, short_name, sl);
            stem[sl] = '\0';
            snprintf(ext, sizeof(ext), "%s", dot);
        } else {
            snprintf(stem, sizeof(stem), "%s", short_name);
        }
        for (int k = 1; k < 1000; k++) {
#ifdef _WIN32
            snprintf(dst_cand, sizeof(dst_cand), "%s\\%s_%d%s", media_dir, stem, k, ext);
#else
            snprintf(dst_cand, sizeof(dst_cand), "%s/%s_%d%s", media_dir, stem, k, ext);
#endif
            int64_t c_sz = get_file_byte_size_helper(dst_cand);
            if (c_sz < 0) break;
            if (src_sz > 0 && c_sz == src_sz) {
                snprintf(out_path, max_len, "%s", dst_cand);
                return;
            }
        }
    }

    if (copy_binary_file_to_dest(src_path, dst_cand)) {
        snprintf(out_path, max_len, "%s", dst_cand);
    } else {
        snprintf(out_path, max_len, "%s", src_path);
    }
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

    char media_filepath[512] = {0};
    copy_media_to_project_media_folder_if_needed(p, filepath, media_filepath, sizeof(media_filepath));

    push_undo_state();
    int new_idx = p->num_items;
    int tab_i = get_valid_tab_idx();
    AudioItem *it = &p->items[new_idx];
    memset(it, 0, sizeof(AudioItem));
    it->track_idx = target_trk;
    it->start_time = target_time;

    if (import_media_with_progress(ren, win, media_filepath, it, 1, 1) == 0) {
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
        if (it->has_video && p->video.is_open) {
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

        char media_filepath[512] = {0};
        copy_media_to_project_media_folder_if_needed(p, g_multi_import.files[k], media_filepath, sizeof(media_filepath));
        if (import_media_with_progress(ren, win, media_filepath, it, k + 1, total) == 0) {
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
            if (it->has_video && p->video.is_open) {
                open_video_by_path(&p->video, it->file_path, ren);
            }
            p->is_dirty = true;
        }
    }
    record_project_change("Imported Multiple Media Items", true);
    g_multi_import.num_files = 0;
}

// ================= CLEAN PROJECT EXPORT & IMPORT SYSTEM (.ehs) =================
#define EHS_EXT_MAGIC  0x45485333U // "EHS3" Extended Project Metadata Signature
#define EHS_EXT4_MAGIC 0x45485334U // "EHS4" Complete Session, Color, Peak Color & History Signature
#define EHS_EXT5_MAGIC 0x45485335U // "EHS5" Track Lock & Lock Mode Options Signature
#define EHS_EXT6_MAGIC 0x45485336U // "EHS6" App Hearing Volume (Master & Per-Track Monitor Level) Signature

static void ensure_recording_directory_exists(const char *dir_path);
static void start_or_stop_recording(ProjectTab *p, SDL_Renderer *ren, SDL_Window *win);

// Compares two file paths case-insensitively while treating '/' and '\' as equivalent
static int daw_strcasecmp_custom(const char *a, const char *b) {
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static int daw_strncasecmp_custom(const char *a, const char *b, size_t n) {
    if (n == 0) return 0;
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    while (n-- > 0 && *a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    if (n == (size_t)-1) return 0;
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static bool daw_paths_equal_norm(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = (*a == '/') ? '\\' : (char)tolower((unsigned char)*a);
        char cb = (*b == '/') ? '\\' : (char)tolower((unsigned char)*b);
        if (ca != cb) return false;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

// Returns true if fpath is inside the software's Temporary Media (or legacy Recordings) folder
static bool is_temp_recordings_clip_path(const char *fpath) {
    if (!fpath || !fpath[0]) return false;
    init_default_recording_folder();

    // 1. Check if fpath starts with g_rec.folder_path + separator
    size_t rlen = strlen(g_rec.folder_path);
    if (rlen > 0 && strlen(fpath) > rlen) {
        bool prefix_ok = true;
        for (size_t i = 0; i < rlen; i++) {
            char ca = (fpath[i] == '/') ? '\\' : (char)tolower((unsigned char)fpath[i]);
            char cb = (g_rec.folder_path[i] == '/') ? '\\' : (char)tolower((unsigned char)g_rec.folder_path[i]);
            if (ca != cb) { prefix_ok = false; break; }
        }
        if (prefix_ok && (fpath[rlen] == '\\' || fpath[rlen] == '/')) {
            return true;
        }
    }

    // 2. Check relative Temporary Media/... or Recordings/...
    const char *p = fpath;
    if (p[0] == '.' && (p[1] == '/' || p[1] == '\\')) p += 2;
    if (daw_strncasecmp_custom(p, "Temporary Media/", 16) == 0 ||
        daw_strncasecmp_custom(p, "Temporary Media\\", 16) == 0 ||
        daw_strncasecmp_custom(p, "Recordings/", 11) == 0 ||
        daw_strncasecmp_custom(p, "Recordings\\", 11) == 0) {
        return true;
    }

    // 3. Check if path contains /temporary media/ or /recordings/
    char norm[512];
    size_t n = strlen(fpath);
    if (n >= sizeof(norm)) n = sizeof(norm) - 1;
    for (size_t i = 0; i < n; i++) {
        norm[i] = (fpath[i] == '/') ? '\\' : (char)tolower((unsigned char)fpath[i]);
    }
    norm[n] = '\0';
    if (strstr(norm, "\\temporary media\\") != NULL || strstr(norm, "\\recordings\\") != NULL) {
        return true;
    }
    return false;
}

// Moves a temporary file from the software Temporary Media folder to the permanent project folder (safe across drives)
static bool move_temp_file_to_project_folder(const char *src_path, const char *dst_path) {
    if (!src_path || !src_path[0] || !dst_path || !dst_path[0]) return false;
    if (daw_paths_equal_norm(src_path, dst_path)) return true;

    FILE *chk_src = fopen(src_path, "rb");
    if (!chk_src) {
        FILE *chk_dst = fopen(dst_path, "rb");
        if (chk_dst) {
            fclose(chk_dst);
            return true;
        }
        return false;
    }
    fclose(chk_src);

#ifdef _WIN32
    if (MoveFileExA(src_path, dst_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        return true;
    }
#endif
    if (rename(src_path, dst_path) == 0) {
        return true;
    }

    // Cross-volume / fallback copy + remove
    FILE *in = fopen(src_path, "rb");
    if (!in) return false;
    FILE *out = fopen(dst_path, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[65536];
    size_t bytes_read;
    bool copy_ok = true;
    while ((bytes_read = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, bytes_read, out) != bytes_read) {
            copy_ok = false;
            break;
        }
    }
    fclose(in);
    fclose(out);
    if (copy_ok) {
        remove(src_path);
        return true;
    }
    return false;
}

// Updates all in-memory AudioItem file_path references (tabs, undo stacks, clipboards) after moving a clip
static void update_clip_filepath_references(const char *old_path, const char *new_path) {
    if (!old_path || !old_path[0] || !new_path || !new_path[0]) return;

    for (int t = 0; t < g_daw.num_tabs; t++) {
        ProjectTab *tp = &g_daw.tabs[t];
        for (int i = 0; i < tp->num_items; i++) {
            if (daw_paths_equal_norm(tp->items[i].file_path, old_path)) {
                strncpy(tp->items[i].file_path, new_path, sizeof(tp->items[i].file_path) - 1);
                tp->items[i].file_path[sizeof(tp->items[i].file_path) - 1] = '\0';
            }
        }
        for (int h = 0; h < tp->undo_mgr.count && h < MAX_UNDO_STACK; h++) {
            DawSnapshot *snap = &tp->undo_mgr.history[h];
            for (int i = 0; i < snap->num_items; i++) {
                if (daw_paths_equal_norm(snap->items[i].file_path, old_path)) {
                    strncpy(snap->items[i].file_path, new_path, sizeof(snap->items[i].file_path) - 1);
                    snap->items[i].file_path[sizeof(snap->items[i].file_path) - 1] = '\0';
                }
            }
        }
        if (g_deep_undo_stack[t]) {
            for (int h = 0; h < MAX_DEEP_UNDO; h++) {
                DeepUndoEntry *ent = &g_deep_undo_stack[t][h];
                for (int i = 0; i < ent->num_items; i++) {
                    if (daw_paths_equal_norm(ent->items[i].file_path, old_path)) {
                        strncpy(ent->items[i].file_path, new_path, sizeof(ent->items[i].file_path) - 1);
                        ent->items[i].file_path[sizeof(ent->items[i].file_path) - 1] = '\0';
                    }
                }
            }
        }
    }

    for (int c = 0; c < g_clipboard_count; c++) {
        if (daw_paths_equal_norm(g_clipboard[c].item.file_path, old_path)) {
            strncpy(g_clipboard[c].item.file_path, new_path, sizeof(g_clipboard[c].item.file_path) - 1);
            g_clipboard[c].item.file_path[sizeof(g_clipboard[c].item.file_path) - 1] = '\0';
        }
    }
    for (int tc = 0; tc < g_track_clipboard_count; tc++) {
        for (int c = 0; c < g_track_clipboard[tc].num_clips; c++) {
            if (daw_paths_equal_norm(g_track_clipboard[tc].clips[c].item.file_path, old_path)) {
                strncpy(g_track_clipboard[tc].clips[c].item.file_path, new_path,
                        sizeof(g_track_clipboard[tc].clips[c].item.file_path) - 1);
                g_track_clipboard[tc].clips[c].item.file_path[sizeof(g_track_clipboard[tc].clips[c].item.file_path) - 1] = '\0';
            }
        }
    }
    if (daw_paths_equal_norm(g_rec.current_wav_path, old_path)) {
        strncpy(g_rec.current_wav_path, new_path, sizeof(g_rec.current_wav_path) - 1);
        g_rec.current_wav_path[sizeof(g_rec.current_wav_path) - 1] = '\0';
    }
}

// Sweeps a Temporary Media directory, moving all remaining files into proj_folder so Temporary Media becomes empty
static void sweep_and_empty_temp_recordings_dir(const char *rec_dir, const char *proj_folder) {
    if (!rec_dir || !rec_dir[0] || !proj_folder || !proj_folder[0]) return;
    if (daw_paths_equal_norm(rec_dir, proj_folder)) return;

#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*.*", rec_dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            const char *fname = fd.cFileName;
            if (fname[0] == '.') continue;
            char src_f[1024], dst_f[1024], rel_f1[1024], rel_f2[1024];
            snprintf(src_f, sizeof(src_f), "%s\\%s", rec_dir, fname);
            snprintf(dst_f, sizeof(dst_f), "%s\\%s", proj_folder, fname);
            snprintf(rel_f1, sizeof(rel_f1), "Temporary Media\\%s", fname);
            snprintf(rel_f2, sizeof(rel_f2), "Recordings\\%s", fname);
            move_temp_file_to_project_folder(src_f, dst_f);
            remove(src_f);
            update_clip_filepath_references(src_f, dst_f);
            update_clip_filepath_references(rel_f1, dst_f);
            update_clip_filepath_references(rel_f2, dst_f);
        }
    } while (FindNextFileA(hFind, &fd) != 0);
    FindClose(hFind);
#else
    DIR *d = opendir(rec_dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *fname = ent->d_name;
        if (fname[0] == '.') continue;
        char src_f[1024], dst_f[1024], rel_f1[1024], rel_f2[1024];
        snprintf(src_f, sizeof(src_f), "%s/%s", rec_dir, fname);
        snprintf(dst_f, sizeof(dst_f), "%s/%s", proj_folder, fname);
        snprintf(rel_f1, sizeof(rel_f1), "Temporary Media/%s", fname);
        snprintf(rel_f2, sizeof(rel_f2), "Recordings/%s", fname);
        move_temp_file_to_project_folder(src_f, dst_f);
        remove(src_f);
        update_clip_filepath_references(src_f, dst_f);
        update_clip_filepath_references(rel_f1, dst_f);
        update_clip_filepath_references(rel_f2, dst_f);
    }
    closedir(d);
#endif
}

// Deletes all files inside a temporary folder on disk
static void delete_all_files_in_temp_dir(const char *dir_path) {
    if (!dir_path || !dir_path[0]) return;
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*.*", dir_path);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (fd.cFileName[0] == '.') continue;
            char fpath[1024];
            snprintf(fpath, sizeof(fpath), "%s\\%s", dir_path, fd.cFileName);
            remove(fpath);
        }
    } while (FindNextFileA(hFind, &fd) != 0);
    FindClose(hFind);
#else
    DIR *d = opendir(dir_path);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char fpath[1024];
        snprintf(fpath, sizeof(fpath), "%s/%s", dir_path, ent->d_name);
        remove(fpath);
    }
    closedir(d);
#endif
}

// Checks whether a .cache/<hash>.raw file is still referenced by any open tab, undo snapshot, or clipboard
static bool daw_is_raw_cache_referenced_anywhere(const char *raw_path) {
    if (!raw_path || !raw_path[0]) return false;
    for (int t = 0; t < g_daw.num_tabs; t++) {
        ProjectTab *tp = &g_daw.tabs[t];
        for (int i = 0; i < tp->num_items; i++) {
            if (daw_paths_equal_norm(tp->items[i].raw_audio_path, raw_path)) return true;
        }
        for (int h = 0; h < tp->undo_mgr.count && h < MAX_UNDO_STACK; h++) {
            DawSnapshot *snap = &tp->undo_mgr.history[h];
            for (int i = 0; i < snap->num_items; i++) {
                if (daw_paths_equal_norm(snap->items[i].raw_audio_path, raw_path)) return true;
            }
        }
        if (g_deep_undo_stack[t]) {
            for (int h = 0; h < MAX_DEEP_UNDO; h++) {
                DeepUndoEntry *ent = &g_deep_undo_stack[t][h];
                for (int i = 0; i < ent->num_items; i++) {
                    if (ent->items && daw_paths_equal_norm(ent->items[i].raw_audio_path, raw_path)) return true;
                }
            }
        }
    }
    for (int c = 0; c < g_clipboard_count; c++) {
        if (daw_paths_equal_norm(g_clipboard[c].item.raw_audio_path, raw_path)) return true;
    }
    for (int tc = 0; tc < g_track_clipboard_count; tc++) {
        for (int c = 0; c < g_track_clipboard[tc].num_clips; c++) {
            if (daw_paths_equal_norm(g_track_clipboard[tc].clips[c].item.raw_audio_path, raw_path)) return true;
        }
    }
    if (g_rec.is_recording && daw_paths_equal_norm(g_rec.current_raw_path, raw_path)) return true;
    return false;
}

// Garbage-collects unreferenced .raw and .pk files inside .cache/ when a tab closes so .cache never bloats
static void daw_garbage_collect_unused_cache_files(void) {
    for (int sp = 0; sp < MAX_SHARED_PEAK_ENTRIES; sp++) {
        if (g_shared_peak_pool[sp].in_use) {
            if (!daw_is_raw_cache_referenced_anywhere(g_shared_peak_pool[sp].raw_audio_path)) {
                if (g_shared_peak_pool[sp].peaks) {
                    free(g_shared_peak_pool[sp].peaks);
                    g_shared_peak_pool[sp].peaks = NULL;
                }
                g_shared_peak_pool[sp].in_use = false;
                g_shared_peak_pool[sp].raw_audio_path[0] = '\0';
            }
        }
    }

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(".cache\\*.*", &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            const char *fname = fd.cFileName;
            if (fname[0] == '.') continue;
            const char *ext = strrchr(fname, '.');
            char rel_slash[512];
            snprintf(rel_slash, sizeof(rel_slash), ".cache/%s", fname);
            if (ext && daw_strcasecmp_custom(ext, ".raw") == 0) {
                if (!daw_is_raw_cache_referenced_anywhere(rel_slash)) {
                    remove(rel_slash);
                    char pk_path[512];
                    size_t base_len = (size_t)(ext - fname);
                    if (base_len < 480) {
                        snprintf(pk_path, sizeof(pk_path), ".cache/%.*s.pk", (int)base_len, fname);
                        remove(pk_path);
                    }
                }
            } else if (ext && daw_strcasecmp_custom(ext, ".pk") == 0) {
                char raw_match[512];
                size_t base_len = (size_t)(ext - fname);
                if (base_len < 480) {
                    snprintf(raw_match, sizeof(raw_match), ".cache/%.*s.raw", (int)base_len, fname);
                    if (!daw_is_raw_cache_referenced_anywhere(raw_match)) {
                        remove(rel_slash);
                    }
                }
            }
        }
    } while (FindNextFileA(hFind, &fd) != 0);
    FindClose(hFind);
#else
    DIR *d = opendir(".cache");
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *fname = ent->d_name;
        if (fname[0] == '.') continue;
        const char *ext = strrchr(fname, '.');
        char rel_slash[512];
        snprintf(rel_slash, sizeof(rel_slash), ".cache/%s", fname);
        if (ext && daw_strcasecmp_custom(ext, ".raw") == 0) {
            if (!daw_is_raw_cache_referenced_anywhere(rel_slash)) {
                remove(rel_slash);
                char pk_path[512];
                size_t base_len = (size_t)(ext - fname);
                if (base_len < 480) {
                    snprintf(pk_path, sizeof(pk_path), ".cache/%.*s.pk", (int)base_len, fname);
                    remove(pk_path);
                }
            }
        } else if (ext && daw_strcasecmp_custom(ext, ".pk") == 0) {
            char raw_match[512];
            size_t base_len = (size_t)(ext - fname);
            if (base_len < 480) {
                snprintf(raw_match, sizeof(raw_match), ".cache/%.*s.raw", (int)base_len, fname);
                if (!daw_is_raw_cache_referenced_anywhere(raw_match)) {
                    remove(rel_slash);
                }
            }
        }
    }
    closedir(d);
#endif
}

// Removes any legacy .cache or Temporary Media folders inside the software installation directory
// while preserving the persistent engine cache in %TEMP%/AudioTrack_EngineCache for instant project opening!
static void daw_purge_all_cache_folder(void) {
    (void)daw_garbage_collect_unused_cache_files;
    delete_all_files_in_temp_dir(".cache");
    delete_all_files_in_temp_dir("Temporary Media");
#ifdef _WIN32
    RemoveDirectoryA(".cache");
    RemoveDirectoryA("Temporary Media");
#else
    rmdir(".cache");
    rmdir("Temporary Media");
#endif
}

// Shifts all 25 per-tab arrays in main.c in lockstep when a tab is closed in daw_tabs.c
void daw_shift_per_tab_arrays_on_close(int idx, bool was_last_single_tab) {
    if (idx < 0 || idx >= MAX_TABS) return;

    g_daw.input_focus = INPUT_FOCUS_NONE;
    g_daw.editing_track_name_idx = -1;
    g_daw.editing_marker_idx = -1;
    g_selected_marker_idx = -1;
    g_last_clicked_item_idx = -1;
    g_last_clicked_track_idx = 0;
    g_clip_props.is_open = false;
    g_text_edit.active_buf = NULL;
    g_text_edit.cursor_pos = 0;
    g_text_edit.select_all = false;
    SDL_StopTextInput();

    if (was_last_single_tab) {
        reset_tab_undo_history(0);
        g_tab_needs_project_setup[0] = true;
        g_tab_crossfade[0] = true;
        g_tab_range_active[0] = false;
        g_tab_range_start[0] = 0.0;
        g_tab_range_end[0] = 0.0;
        memset(g_item_fade_in[0], 0, sizeof(g_item_fade_in[0]));
        memset(g_item_fade_out[0], 0, sizeof(g_item_fade_out[0]));
        memset(g_item_vol_db[0], 0, sizeof(g_item_vol_db[0]));
        memset(g_item_muted[0], 0, sizeof(g_item_muted[0]));
        memset(g_item_color_idx[0], 0, sizeof(g_item_color_idx[0]));
        memset(g_item_peak_color_idx[0], 0, sizeof(g_item_peak_color_idx[0]));
        memset(g_item_group_id[0], 0, sizeof(g_item_group_id[0]));
        memset(g_track_color_idx[0], 0, sizeof(g_track_color_idx[0]));
        memset(g_track_peak_color_idx[0], 0, sizeof(g_track_peak_color_idx[0]));
        g_tab_peak_color_idx[0] = 0;
        memset(g_track_hidden[0], 0, sizeof(g_track_hidden[0]));
        memset(g_track_locked[0], 0, sizeof(g_track_locked[0]));
        memset(g_track_lock_lr_only[0], 0, sizeof(g_track_lock_lr_only[0]));
        memset(g_track_lock_ud_only[0], 0, sizeof(g_track_lock_ud_only[0]));
        g_lock_opt_full[0] = false;
        g_lock_opt_complete_unlock[0] = false;
        g_lock_opt_prevent_lr[0] = false;
        g_lock_opt_prevent_ud[0] = false;
        memset(g_track_inf_db[0], 0, sizeof(g_track_inf_db[0]));
        memset(g_track_pre_inf_db[0], 0, sizeof(g_track_pre_inf_db[0]));
        init_tab_app_volume_defaults(0);
        g_video_fullscreen[0] = false;
        memset(&g_video_saved_rect[0], 0, sizeof(SDL_Rect));
        g_playhead_seek_serial[0] = 0;
        g_last_observed_playhead[0] = -1.0;
        memset(g_marker_full_text[0], 0, sizeof(g_marker_full_text[0]));
        memset(g_tab_history[0], 0, sizeof(g_tab_history[0]));
        g_tab_history_count[0] = 0;
        snprintf(g_tab_notification[0], sizeof(g_tab_notification[0]), "Select Open or Create Project");
        return;
    }

    // Free the closed tab's deep undo stack before shifting pointers down
    reset_tab_undo_history(idx);
    if (g_deep_undo_stack[idx]) {
        free(g_deep_undo_stack[idx]);
        g_deep_undo_stack[idx] = NULL;
    }

    // Note: g_daw.num_tabs was already decremented in force_close_tab(idx),
    // so tabs originally at (idx + 1 .. g_daw.num_tabs) shift into (idx .. g_daw.num_tabs - 1).
    int new_count = g_daw.num_tabs;
    if (new_count < 1) new_count = 1;
    if (new_count >= MAX_TABS) new_count = MAX_TABS - 1;

    for (int t = idx; t < new_count; t++) {
        g_deep_undo_stack[t]        = g_deep_undo_stack[t + 1];
        g_deep_undo_head[t]         = g_deep_undo_head[t + 1];
        g_deep_undo_count[t]        = g_deep_undo_count[t + 1];
        g_deep_undo_has_redo_tip[t] = g_deep_undo_has_redo_tip[t + 1];

        g_tab_needs_project_setup[t] = g_tab_needs_project_setup[t + 1];
        g_tab_crossfade[t]           = g_tab_crossfade[t + 1];
        g_tab_range_active[t]        = g_tab_range_active[t + 1];
        g_tab_range_start[t]         = g_tab_range_start[t + 1];
        g_tab_range_end[t]           = g_tab_range_end[t + 1];

        memcpy(g_item_fade_in[t],        g_item_fade_in[t + 1],        sizeof(g_item_fade_in[0]));
        memcpy(g_item_fade_out[t],       g_item_fade_out[t + 1],       sizeof(g_item_fade_out[0]));
        memcpy(g_item_vol_db[t],         g_item_vol_db[t + 1],         sizeof(g_item_vol_db[0]));
        memcpy(g_item_muted[t],          g_item_muted[t + 1],          sizeof(g_item_muted[0]));
        memcpy(g_item_color_idx[t],      g_item_color_idx[t + 1],      sizeof(g_item_color_idx[0]));
        memcpy(g_item_peak_color_idx[t], g_item_peak_color_idx[t + 1], sizeof(g_item_peak_color_idx[0]));
        memcpy(g_item_group_id[t],       g_item_group_id[t + 1],       sizeof(g_item_group_id[0]));

        memcpy(g_track_color_idx[t],      g_track_color_idx[t + 1],      sizeof(g_track_color_idx[0]));
        memcpy(g_track_peak_color_idx[t], g_track_peak_color_idx[t + 1], sizeof(g_track_peak_color_idx[0]));
        g_tab_peak_color_idx[t] = g_tab_peak_color_idx[t + 1];

        memcpy(g_track_hidden[t],       g_track_hidden[t + 1],       sizeof(g_track_hidden[0]));
        memcpy(g_track_locked[t],       g_track_locked[t + 1],       sizeof(g_track_locked[0]));
        memcpy(g_track_lock_lr_only[t], g_track_lock_lr_only[t + 1], sizeof(g_track_lock_lr_only[0]));
        memcpy(g_track_lock_ud_only[t], g_track_lock_ud_only[t + 1], sizeof(g_track_lock_ud_only[0]));
        g_lock_opt_full[t]            = g_lock_opt_full[t + 1];
        g_lock_opt_complete_unlock[t] = g_lock_opt_complete_unlock[t + 1];
        g_lock_opt_prevent_lr[t]      = g_lock_opt_prevent_lr[t + 1];
        g_lock_opt_prevent_ud[t]      = g_lock_opt_prevent_ud[t + 1];

        memcpy(g_track_inf_db[t],     g_track_inf_db[t + 1],     sizeof(g_track_inf_db[0]));
        memcpy(g_track_pre_inf_db[t], g_track_pre_inf_db[t + 1], sizeof(g_track_pre_inf_db[0]));

        g_app_master_vol[t] = g_app_master_vol[t + 1];
        memcpy(g_app_track_vol[t], g_app_track_vol[t + 1], sizeof(g_app_track_vol[0]));

        g_video_fullscreen[t]  = g_video_fullscreen[t + 1];
        g_video_saved_rect[t]  = g_video_saved_rect[t + 1];
        g_playhead_seek_serial[t]   = g_playhead_seek_serial[t + 1];
        g_last_observed_playhead[t] = g_last_observed_playhead[t + 1];

        memcpy(g_marker_full_text[t], g_marker_full_text[t + 1], sizeof(g_marker_full_text[0]));
        memcpy(g_tab_history[t],      g_tab_history[t + 1],      sizeof(g_tab_history[0]));
        g_tab_history_count[t] = g_tab_history_count[t + 1];
        memcpy(g_tab_notification[t], g_tab_notification[t + 1], sizeof(g_tab_notification[0]));
    }

    // Reset the vacated slot at new_count
    g_deep_undo_stack[new_count]        = NULL;
    g_deep_undo_head[new_count]         = -1;
    g_deep_undo_count[new_count]        = 0;
    g_deep_undo_has_redo_tip[new_count] = false;
    g_tab_needs_project_setup[new_count] = true;
    g_tab_crossfade[new_count]          = true;
    g_tab_range_active[new_count]       = false;
    g_tab_range_start[new_count]        = 0.0;
    g_tab_range_end[new_count]          = 0.0;
    memset(g_item_fade_in[new_count], 0, sizeof(g_item_fade_in[0]));
    memset(g_item_fade_out[new_count], 0, sizeof(g_item_fade_out[0]));
    memset(g_item_vol_db[new_count], 0, sizeof(g_item_vol_db[0]));
    memset(g_item_muted[new_count], 0, sizeof(g_item_muted[0]));
    memset(g_item_color_idx[new_count], 0, sizeof(g_item_color_idx[0]));
    memset(g_item_peak_color_idx[new_count], 0, sizeof(g_item_peak_color_idx[0]));
    memset(g_item_group_id[new_count], 0, sizeof(g_item_group_id[0]));
    memset(g_track_color_idx[new_count], 0, sizeof(g_track_color_idx[0]));
    memset(g_track_peak_color_idx[new_count], 0, sizeof(g_track_peak_color_idx[0]));
    g_tab_peak_color_idx[new_count] = 0;
    memset(g_track_hidden[new_count], 0, sizeof(g_track_hidden[0]));
    memset(g_track_locked[new_count], 0, sizeof(g_track_locked[0]));
    memset(g_track_lock_lr_only[new_count], 0, sizeof(g_track_lock_lr_only[0]));
    memset(g_track_lock_ud_only[new_count], 0, sizeof(g_track_lock_ud_only[0]));
    g_lock_opt_full[new_count] = false;
    g_lock_opt_complete_unlock[new_count] = false;
    g_lock_opt_prevent_lr[new_count] = false;
    g_lock_opt_prevent_ud[new_count] = false;
    memset(g_track_inf_db[new_count], 0, sizeof(g_track_inf_db[0]));
    memset(g_track_pre_inf_db[new_count], 0, sizeof(g_track_pre_inf_db[0]));
    init_tab_app_volume_defaults(new_count);
    g_video_fullscreen[new_count] = false;
    memset(&g_video_saved_rect[new_count], 0, sizeof(SDL_Rect));
    g_playhead_seek_serial[new_count] = 0;
    g_last_observed_playhead[new_count] = -1.0;
    memset(g_marker_full_text[new_count], 0, sizeof(g_marker_full_text[0]));
    memset(g_tab_history[new_count], 0, sizeof(g_tab_history[0]));
    g_tab_history_count[new_count] = 0;
    g_tab_notification[new_count][0] = '\0';
}

static bool daw_is_peak_ptr_referenced_in_active_tabs(const PeakBin *pk) {
    if (!pk) return false;
    for (int t = 0; t < g_daw.num_tabs && t < MAX_TABS; t++) {
        ProjectTab *tp = &g_daw.tabs[t];
        for (int i = 0; i < tp->num_items && i < MAX_ITEMS; i++) {
            if (tp->items[i].peaks == pk) return true;
        }
        if (g_deep_undo_stack[t]) {
            for (int k = 0; k < MAX_DEEP_UNDO; k++) {
                DeepUndoEntry *ent = &g_deep_undo_stack[t][k];
                if (ent->items) {
                    for (int i = 0; i < ent->num_items; i++) {
                        if (ent->items[i].peaks == pk) return true;
                    }
                }
            }
        }
    }
    for (int c = 0; c < g_clipboard_count; c++) {
        if (g_clipboard[c].item.peaks == pk) return true;
    }
    for (int tc = 0; tc < g_track_clipboard_count; tc++) {
        for (int c = 0; c < g_track_clipboard[tc].num_clips; c++) {
            if (g_track_clipboard[tc].clips[c].item.peaks == pk) return true;
        }
    }
    return false;
}

void daw_evict_unused_shared_peaks_after_tab_close(void) {
    for (int sp = 0; sp < MAX_SHARED_PEAK_ENTRIES; sp++) {
        if (g_shared_peak_pool[sp].in_use) {
            PeakBin *pk = g_shared_peak_pool[sp].peaks;
            if (!pk || !daw_is_peak_ptr_referenced_in_active_tabs(pk)) {
                if (pk) free(pk);
                g_shared_peak_pool[sp].peaks = NULL;
                g_shared_peak_pool[sp].in_use = false;
                g_shared_peak_pool[sp].raw_audio_path[0] = '\0';
                g_shared_peak_pool[sp].peak_count = 0;
                g_shared_peak_pool[sp].total_duration = 0.0;
                g_shared_peak_pool[sp].has_video = false;
            }
        }
    }
    g_engine_processing = false;
}

void daw_prepare_tab_close_state(int idx) {
    if (idx < 0 || idx >= g_daw.num_tabs || idx >= MAX_TABS) return;
    if (g_rec.is_recording && (idx == g_daw.active_tab || g_daw.num_tabs <= 1)) {
        g_rec.is_recording = false;
        if (g_rec.capture_dev > 0) {
            SDL_CloseAudioDevice(g_rec.capture_dev);
            g_rec.capture_dev = 0;
        }
        if (g_rec.wav_file) {
            fclose(g_rec.wav_file);
            g_rec.wav_file = NULL;
        }
        if (g_rec.raw_cache_file) {
            fclose(g_rec.raw_cache_file);
            g_rec.raw_cache_file = NULL;
        }
    }
    g_engine_processing = true;
    g_daw.tabs[idx].is_playing = false;
    if (g_video_pinned_global && (idx == g_daw.active_tab || g_daw.num_tabs <= 1)) {
        g_video_pinned_global = false;
        destroy_external_pinned_video_window();
    }
}

bool daw_is_tab_unmodified_setup(int idx) {
    if (idx < 0 || idx >= g_daw.num_tabs || idx >= MAX_TABS) return false;
    return ((g_tab_needs_project_setup[idx] || g_daw.tabs[idx].project_path[0] == '\0') &&
            g_daw.tabs[idx].num_items == 0);
}

void daw_clear_open_project_after_save_flag(void) {
    g_save_prompt_open_project_after = false;
}

// Requirement 1 & 2: Cleans up temporary and .cache files when a tab or the complete application closes
void daw_cleanup_temp_media_for_tab(int tab_idx, bool delete_all_temp_files) {
    (void)is_temp_recordings_clip_path;
    (void)sweep_and_empty_temp_recordings_dir;
    if (g_rec.is_recording && (delete_all_temp_files || tab_idx < 0 || tab_idx == g_daw.active_tab)) {
        g_rec.is_recording = false;
        if (g_rec.capture_dev > 0) {
            SDL_CloseAudioDevice(g_rec.capture_dev);
            g_rec.capture_dev = 0;
        }
        if (g_rec.wav_file) {
            fclose(g_rec.wav_file);
            g_rec.wav_file = NULL;
        }
        if (g_rec.raw_cache_file) {
            fclose(g_rec.raw_cache_file);
            g_rec.raw_cache_file = NULL;
        }
    }

    if (tab_idx >= 0 && tab_idx < g_daw.num_tabs) {
        ProjectTab *tp = &g_daw.tabs[tab_idx];
        tp->is_playing = false;
        close_video(&tp->video);
        if (g_daw.audio_device > 0) SDL_LockAudioDevice(g_daw.audio_device);
        for (int i = 0; i < tp->num_items; i++) {
            AudioItem *it = &tp->items[i];
            if (it->raw_file_handle) {
                fclose(it->raw_file_handle);
                it->raw_file_handle = NULL;
            }
        }
        if (g_daw.audio_device > 0) SDL_UnlockAudioDevice(g_daw.audio_device);
        reset_tab_undo_history(tab_idx);
    }

    if (delete_all_temp_files) {
        daw_purge_all_cache_folder();
    }
}

// Requests complete application exit (triggered by top-right 'Exit' button above 'Save'):
// Checks all open tabs for unsaved changes first; prompts Save / Don't Save / Cancel for each dirty tab.
static void request_exit_application(bool *running) {
    for (int t = 0; t < g_daw.num_tabs; t++) {
        if (g_daw.tabs[t].project_path[0] == '\0' && g_daw.tabs[t].num_items == 0) {
            g_daw.tabs[t].is_dirty = false;
        }
        if (g_daw.tabs[t].is_dirty) {
            g_daw.active_tab = t;
            g_daw.save_prompt_open = true;
            g_daw.save_prompt_tab_idx = t;
            g_daw.save_prompt_quit_app = true;
            g_save_prompt_open_project_after = false;
            return;
        }
    }
    daw_cleanup_temp_media_for_tab(-1, true);
    if (running) *running = false;
}

// Moves all temporary Recorded and Glued clips from software Temporary Media folder into proj_folder permanently
static void migrate_temp_recordings_to_project_folder(ProjectTab *p, const char *proj_folder) {
    if (!p || !proj_folder || !proj_folder[0]) return;
    init_default_recording_folder();

    if (g_rec.is_recording) {
        start_or_stop_recording(p, NULL, NULL);
    }

    ensure_recording_directory_exists(proj_folder);

    // 1. Move any clip referenced by the project from software Temporary Media folder to proj_folder
    for (int i = 0; i < p->num_items; i++) {
        AudioItem *it = &p->items[i];
        if (!is_temp_recordings_clip_path(it->file_path)) continue;

        const char *fn = strrchr(it->file_path, '\\');
        const char *fn2 = strrchr(it->file_path, '/');
        if (fn2 && (!fn || fn2 > fn)) fn = fn2;
        const char *short_name = fn ? (fn + 1) : it->file_path;
        if (!short_name[0]) continue;

        char old_path[512];
        strncpy(old_path, it->file_path, sizeof(old_path) - 1);
        old_path[sizeof(old_path) - 1] = '\0';

        char dst_path[512];
#ifdef _WIN32
        snprintf(dst_path, sizeof(dst_path), "%s\\%s", proj_folder, short_name);
#else
        snprintf(dst_path, sizeof(dst_path), "%s/%s", proj_folder, short_name);
#endif

        if (!move_temp_file_to_project_folder(old_path, dst_path)) {
            char alt_src[512];
#ifdef _WIN32
            snprintf(alt_src, sizeof(alt_src), "%s\\%s", g_rec.folder_path, short_name);
#else
            snprintf(alt_src, sizeof(alt_src), "%s/%s", g_rec.folder_path, short_name);
#endif
            if (!move_temp_file_to_project_folder(alt_src, dst_path)) {
                snprintf(alt_src, sizeof(alt_src), "Temporary Media/%s", short_name);
                if (!move_temp_file_to_project_folder(alt_src, dst_path)) {
                    snprintf(alt_src, sizeof(alt_src), "Recordings/%s", short_name);
                    move_temp_file_to_project_folder(alt_src, dst_path);
                }
            }
        }
        update_clip_filepath_references(old_path, dst_path);
    }

    // 2. Sweep software Temporary Media folder so all temporary files are moved to proj_folder and Temporary Media becomes empty
    sweep_and_empty_temp_recordings_dir(g_rec.folder_path, proj_folder);
    sweep_and_empty_temp_recordings_dir("Temporary Media", proj_folder);
    sweep_and_empty_temp_recordings_dir("Recordings", proj_folder);
}

static bool daw_save_project_to_path(ProjectTab *p, const char *path) {
    if (!p || !path || !path[0]) return false;

    const char *save_modal_title = g_autosave_in_progress ? "Project Auto Saving..." : "Project Saving...";
    show_processing_wait_modal(
        g_main_ren, g_main_win,
        save_modal_title,
        "Saving project file (.ehs) and syncing media folder...",
        35
    );

    // Resolve designated directory, project base name, and same-named project folder
    char parent_dir[512] = {0};
    char proj_base_name[128] = {0};

    const char *last_sep = strrchr(path, '\\');
    const char *last_sep2 = strrchr(path, '/');
    if (last_sep2 && (!last_sep || last_sep2 > last_sep)) last_sep = last_sep2;

    if (last_sep) {
        size_t dlen = (size_t)(last_sep - path);
        if (dlen >= sizeof(parent_dir)) dlen = sizeof(parent_dir) - 1;
        memcpy(parent_dir, path, dlen);
        parent_dir[dlen] = '\0';
        snprintf(proj_base_name, sizeof(proj_base_name), "%s", last_sep + 1);
    } else {
        strcpy(parent_dir, ".");
        snprintf(proj_base_name, sizeof(proj_base_name), "%s", path);
    }

    strip_ehs_extension(proj_base_name);
    if (!proj_base_name[0]) {
        strcpy(proj_base_name, "Project");
    }

    // Check if parent_dir already ends with proj_base_name (e.g., saving an already-folderized project)
    const char *parent_tail = strrchr(parent_dir, '\\');
    const char *parent_tail2 = strrchr(parent_dir, '/');
    if (parent_tail2 && (!parent_tail || parent_tail2 > parent_tail)) parent_tail = parent_tail2;
    const char *parent_folder_name = parent_tail ? (parent_tail + 1) : parent_dir;

    char proj_folder[512] = {0};
    char final_ehs_path[512] = {0};

    if (daw_strcasecmp_custom(parent_folder_name, proj_base_name) == 0) {
        snprintf(proj_folder, sizeof(proj_folder), "%s", parent_dir);
#ifdef _WIN32
        snprintf(final_ehs_path, sizeof(final_ehs_path), "%s\\%s.ehs", proj_folder, proj_base_name);
#else
        snprintf(final_ehs_path, sizeof(final_ehs_path), "%s/%s.ehs", proj_folder, proj_base_name);
#endif
    } else {
#ifdef _WIN32
        snprintf(proj_folder, sizeof(proj_folder), "%s\\%s", parent_dir, proj_base_name);
        snprintf(final_ehs_path, sizeof(final_ehs_path), "%s\\%s.ehs", proj_folder, proj_base_name);
#else
        snprintf(proj_folder, sizeof(proj_folder), "%s/%s", parent_dir, proj_base_name);
        snprintf(final_ehs_path, sizeof(final_ehs_path), "%s/%s.ehs", proj_folder, proj_base_name);
#endif
    }

    // Auto-create the Project folder (<ProjectName>/) and its internal "media" subfolder (<ProjectName>/media/)
    char media_folder[512] = {0};
#ifdef _WIN32
    snprintf(media_folder, sizeof(media_folder), "%s\\media", proj_folder);
#else
    snprintf(media_folder, sizeof(media_folder), "%s/media", proj_folder);
#endif
    ensure_recording_directory_exists(proj_folder);
    ensure_recording_directory_exists(media_folder);
    strncpy(g_rec.folder_path, media_folder, sizeof(g_rec.folder_path) - 1);
    g_rec.folder_path[sizeof(g_rec.folder_path) - 1] = '\0';
    (void)migrate_temp_recordings_to_project_folder;

    // Ensure every clip in the project is copied into <ProjectFolder>/media if it came from outside
    strncpy(p->project_path, final_ehs_path, sizeof(p->project_path) - 1);
    p->project_path[sizeof(p->project_path) - 1] = '\0';
    for (int i = 0; i < p->num_items; i++) {
        AudioItem *it = &p->items[i];
        if (it->file_path[0] != '\0' && !is_file_already_in_media_dir(it->file_path, media_folder)) {
            char old_p[512];
            strncpy(old_p, it->file_path, sizeof(old_p) - 1);
            old_p[sizeof(old_p) - 1] = '\0';
            char new_p[512] = {0};
            copy_media_to_project_media_folder_if_needed(p, old_p, new_p, sizeof(new_p));
            if (new_p[0] != '\0' && !daw_paths_equal_norm(old_p, new_p)) {
                update_clip_filepath_references(old_p, new_p);
            }
        }
        // Ensure REAPER-style sidecar .pk peak file exists in <Project>/media/<clip>.pk for instant project reload
        if (it->file_path[0] != '\0' && it->peaks && it->peak_count > 0 && it->file_total_duration > 0.0) {
            char proj_pk[512] = {0};
            daw_build_project_pk_path(it->file_path, proj_pk, sizeof(proj_pk));
            if (proj_pk[0]) {
                FILE *chk_pk = fopen(proj_pk, "rb");
                if (chk_pk) fclose(chk_pk);
                else daw_save_pk_file(proj_pk, it->peaks, it->peak_count, it->file_total_duration, it->has_video);
            }
        }
    }

    FILE *f = fopen(final_ehs_path, "wb");
    if (!f) {
        f = fopen(path, "wb");
        if (!f) return false;
        strncpy(final_ehs_path, path, sizeof(final_ehs_path) - 1);
        final_ehs_path[sizeof(final_ehs_path) - 1] = '\0';
    } else if (!daw_paths_equal_norm(path, final_ehs_path)) {
        remove(path);
    }

    int tab_i = get_valid_tab_idx();
    for (int ti = 0; ti < g_daw.num_tabs; ti++) {
        if (&g_daw.tabs[ti] == p) {
            tab_i = ti;
            break;
        }
    }

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

    // 2. Write Extended Metadata Block ("EHS3")
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

    // 3. Write Complete Session, Color, Peak Color, Clip Gain/Mute/Group, Video & History Block ("EHS4")
    uint32_t magic4 = EHS_EXT4_MAGIC;
    fwrite(&magic4, sizeof(uint32_t), 1, f);
    fwrite(g_track_color_idx[tab_i], sizeof(int), MAX_TRACKS, f);
    fwrite(g_track_peak_color_idx[tab_i], sizeof(int), MAX_TRACKS, f);
    fwrite(&g_tab_peak_color_idx[tab_i], sizeof(int), 1, f);
    fwrite(&g_daw.tcp_width, sizeof(int), 1, f);
    fwrite(&p->video.is_open, sizeof(bool), 1, f);
    fwrite(&p->video.rect, sizeof(SDL_Rect), 1, f);
    fwrite(&g_video_fullscreen[tab_i], sizeof(bool), 1, f);
    fwrite(&g_video_saved_rect[tab_i], sizeof(SDL_Rect), 1, f);
    fwrite(&g_selected_marker_idx, sizeof(int), 1, f);
    fwrite(&g_last_clicked_track_idx, sizeof(int), 1, f);
    fwrite(&g_last_clicked_item_idx, sizeof(int), 1, f);
    fwrite(&g_next_item_group_id, sizeof(int), 1, f);
    fwrite(&g_tab_history_count[tab_i], sizeof(int), 1, f);
    fwrite(g_tab_history[tab_i], sizeof(char) * HISTORY_ENTRY_LEN, MAX_HISTORY_ENTRIES, f);

    for (int i = 0; i < p->num_items; i++) {
        fwrite(&g_item_vol_db[tab_i][i], sizeof(float), 1, f);
        fwrite(&g_item_muted[tab_i][i], sizeof(bool), 1, f);
        fwrite(&g_item_color_idx[tab_i][i], sizeof(int), 1, f);
        fwrite(&g_item_peak_color_idx[tab_i][i], sizeof(int), 1, f);
        fwrite(&g_item_group_id[tab_i][i], sizeof(int), 1, f);
    }

    // 4. Write Track Lock & Lock Mode Options Block ("EHS5")
    uint32_t magic5 = EHS_EXT5_MAGIC;
    fwrite(&magic5, sizeof(uint32_t), 1, f);
    fwrite(g_track_locked[tab_i], sizeof(bool), MAX_TRACKS, f);
    fwrite(&g_lock_opt_full[tab_i], sizeof(bool), 1, f);
    fwrite(&g_lock_opt_prevent_lr[tab_i], sizeof(bool), 1, f);
    fwrite(&g_lock_opt_prevent_ud[tab_i], sizeof(bool), 1, f);
    fwrite(g_track_lock_lr_only[tab_i], sizeof(bool), MAX_TRACKS, f);
    fwrite(g_track_lock_ud_only[tab_i], sizeof(bool), MAX_TRACKS, f);
    fwrite(&g_lock_opt_complete_unlock[tab_i], sizeof(bool), 1, f);

    // 5. Write App Hearing Volume Block ("EHS6": Master & Per-Track Monitor Volume 0..100)
    ensure_app_vol_defaults_initialized();
    uint32_t magic6 = EHS_EXT6_MAGIC;
    fwrite(&magic6, sizeof(uint32_t), 1, f);
    fwrite(&g_app_master_vol[tab_i], sizeof(float), 1, f);
    fwrite(g_app_track_vol[tab_i], sizeof(float), MAX_TRACKS, f);

    fflush(f);
    fclose(f);

    for (int tr = 0; tr < MAX_TRACKS; tr++) {
        g_track_inf_db[tab_i][tr] = (p->track_db[tr] <= -60.0f);
    }

    strncpy(p->project_path, final_ehs_path, sizeof(p->project_path) - 1);
    p->project_path[sizeof(p->project_path) - 1] = '\0';

    snprintf(p->project_name, sizeof(p->project_name), "%s", proj_base_name);
    strip_ehs_extension(p->project_name);
    p->is_dirty = false;
    g_tab_needs_project_setup[tab_i] = false;
    g_autosave.last_save_ticks = SDL_GetTicks();
    g_autosave.pending_autosave = false;

    show_processing_wait_modal(
        g_main_ren, g_main_win,
        save_modal_title,
        "Project saved.",
        100
    );
    if (g_main_ren && g_main_win) {
        SDL_Delay(90);
    }
    finish_processing_wait_modal(0);
    record_project_change(g_autosave_in_progress ? "Project Auto Saved" : "Project Saved (Media Folder Synced)", false);
    return true;
}

// Opens Browse/Save dialog for "Create Project File" option on New Project Tab modal:
// Creates <ParentDir>/<ProjectName>/, <ParentDir>/<ProjectName>/<ProjectName>.ehs, and <ParentDir>/<ProjectName>/media/
static bool daw_create_new_project_file_dialog(ProjectTab *p) {
    if (!p) return false;
    char path[MAX_PATH] = {0};
    snprintf(path, sizeof(path), "%s.ehs", p->project_name[0] ? p->project_name : "Project_1");
    if (!open_generic_dialog(path, sizeof(path), "Create Project File (*.ehs)\0*.ehs\0", true)) {
        return false;
    }
    if (!strstr(path, ".ehs") && !strstr(path, ".EHS")) {
        strncat(path, ".ehs", sizeof(path) - strlen(path) - 1);
    }
    // Requirement 1: By default when opening/creating a fresh project, Lock is OFF
    int cur_t = get_valid_tab_idx();
    memset(g_track_locked[cur_t], 0, sizeof(g_track_locked[0]));
    memset(g_track_lock_lr_only[cur_t], 0, sizeof(g_track_lock_lr_only[0]));
    memset(g_track_lock_ud_only[cur_t], 0, sizeof(g_track_lock_ud_only[0]));
    g_lock_opt_full[cur_t] = false;
    g_lock_opt_complete_unlock[cur_t] = false;
    g_lock_opt_prevent_lr[cur_t] = false;
    g_lock_opt_prevent_ud[cur_t] = false;
    if (daw_save_project_to_path(p, path)) {
        g_tab_needs_project_setup[get_valid_tab_idx()] = false;
        record_project_change("Created Project Folder & Media Subfolder", false);
        return true;
    }
    return false;
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
    p->is_playing = false;

    // Free existing clip handles & peaks safely under audio lock before loading project
    close_video(&p->video);
    if (g_daw.audio_device > 0) SDL_LockAudioDevice(g_daw.audio_device);
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].raw_file_handle) {
            fclose(p->items[i].raw_file_handle);
            p->items[i].raw_file_handle = NULL;
        }
        if (p->items[i].peaks) {
            daw_free_peaks_if_unpooled(p->items[i].peaks);
            p->items[i].peaks = NULL;
        }
    }
    p->num_items = 0;
    if (g_daw.audio_device > 0) SDL_UnlockAudioDevice(g_daw.audio_device);

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

    // Default extended state before checking for EHS3 / EHS4 footers
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
    memset(g_track_locked[tab_i], 0, sizeof(g_track_locked[0]));
    g_lock_opt_full[tab_i] = false;
    g_lock_opt_prevent_lr[tab_i] = false;
    g_lock_opt_prevent_ud[tab_i] = false;
    memset(g_track_color_idx[tab_i], 0, sizeof(g_track_color_idx[0]));
    memset(g_track_peak_color_idx[tab_i], 0, sizeof(g_track_peak_color_idx[0]));
    g_tab_peak_color_idx[tab_i] = 0;
    memset(g_item_fade_in[tab_i], 0, sizeof(g_item_fade_in[0]));
    memset(g_item_fade_out[tab_i], 0, sizeof(g_item_fade_out[0]));
    memset(g_item_vol_db[tab_i], 0, sizeof(g_item_vol_db[0]));
    memset(g_item_muted[tab_i], 0, sizeof(g_item_muted[0]));
    memset(g_item_color_idx[tab_i], 0, sizeof(g_item_color_idx[0]));
    memset(g_item_peak_color_idx[tab_i], 0, sizeof(g_item_peak_color_idx[0]));
    memset(g_item_group_id[tab_i], 0, sizeof(g_item_group_id[0]));
    for (int m = 0; m < p->num_markers; m++) {
        set_marker_full_str(p, m, p->markers[m].text);
    }

    bool   has_ext = false;
    bool   has_ext4 = false;
    double saved_playhead = 0.0;
    double saved_scroll_x = 0.0;
    int    saved_scroll_y = 0;
    bool   saved_vid_open = false;
    SDL_Rect saved_vid_rect = { 680, 110, 480, 294 };

    double ext_file_tot_dur[MAX_ITEMS] = {0.0};
    static char ext_label[MAX_ITEMS][128];
    bool   ext_selected[MAX_ITEMS] = {false};
    bool   ext_has_video[MAX_ITEMS] = {false};
    double ext_fin[MAX_ITEMS] = {0.0};
    double ext_fout[MAX_ITEMS] = {0.0};

    float  ext4_vol_db[MAX_ITEMS] = {0.0f};
    bool   ext4_muted[MAX_ITEMS] = {false};
    int    ext4_color_idx[MAX_ITEMS] = {0};
    int    ext4_peak_color_idx[MAX_ITEMS] = {0};
    int    ext4_group_id[MAX_ITEMS] = {0};

    uint32_t magic = 0;
    if (fread(&magic, sizeof(uint32_t), 1, f) == 1 && magic == EHS_EXT_MAGIC) {
        has_ext = true;
        fread(p->tracks, sizeof(Track), MAX_TRACKS, f);
        fread(p->track_selected, sizeof(bool), MAX_TRACKS, f);
        fread(g_track_hidden[tab_i], sizeof(bool), MAX_TRACKS, f);
        fread(&saved_playhead, sizeof(double), 1, f);
        fread(&saved_scroll_x, sizeof(double), 1, f);
        fread(&saved_scroll_y, sizeof(int), 1, f);
        fread(&p->pixels_per_second, sizeof(float), 1, f);
        fread(&p->project_duration_sec, sizeof(double), 1, f);
        fread(&p->show_marker_lane, sizeof(bool), 1, f);
        fread(&g_tab_crossfade[tab_i], sizeof(bool), 1, f);
        fread(&g_tab_range_active[tab_i], sizeof(bool), 1, f);
        fread(&g_tab_range_start[tab_i], sizeof(double), 1, f);
        fread(&g_tab_range_end[tab_i], sizeof(double), 1, f);
        fread(g_marker_full_text[tab_i], sizeof(char) * MARKER_TEXT_MAX, MAX_MARKERS, f);

        if (p->pixels_per_second < 0.002f || p->pixels_per_second > 3000.0f) p->pixels_per_second = 80.0f;
        if (saved_scroll_x < 0.0) saved_scroll_x = 0.0;
        if (saved_scroll_y < 0) saved_scroll_y = 0;
        if (saved_playhead < 0.0) saved_playhead = 0.0;

        for (int i = 0; i < loaded_items; i++) {
            fread(&ext_file_tot_dur[i], sizeof(double), 1, f);
            fread(ext_label[i], sizeof(char), 128, f);
            ext_label[i][127] = '\0';
            fread(&ext_selected[i], sizeof(bool), 1, f);
            fread(&ext_has_video[i], sizeof(bool), 1, f);
            fread(&ext_fin[i], sizeof(double), 1, f);
            fread(&ext_fout[i], sizeof(double), 1, f);
        }

        uint32_t magic4 = 0;
        if (fread(&magic4, sizeof(uint32_t), 1, f) == 1 && magic4 == EHS_EXT4_MAGIC) {
            has_ext4 = true;
            fread(g_track_color_idx[tab_i], sizeof(int), MAX_TRACKS, f);
            fread(g_track_peak_color_idx[tab_i], sizeof(int), MAX_TRACKS, f);
            fread(&g_tab_peak_color_idx[tab_i], sizeof(int), 1, f);
            int saved_tcp_w = 200;
            fread(&saved_tcp_w, sizeof(int), 1, f);
            if (saved_tcp_w >= 140 && saved_tcp_w <= 380) g_daw.tcp_width = saved_tcp_w;
            fread(&saved_vid_open, sizeof(bool), 1, f);
            fread(&saved_vid_rect, sizeof(SDL_Rect), 1, f);
            fread(&g_video_fullscreen[tab_i], sizeof(bool), 1, f);
            fread(&g_video_saved_rect[tab_i], sizeof(SDL_Rect), 1, f);
            fread(&g_selected_marker_idx, sizeof(int), 1, f);
            fread(&g_last_clicked_track_idx, sizeof(int), 1, f);
            fread(&g_last_clicked_item_idx, sizeof(int), 1, f);
            fread(&g_next_item_group_id, sizeof(int), 1, f);
            if (g_next_item_group_id < 1) g_next_item_group_id = 1;
            int hist_cnt = 0;
            fread(&hist_cnt, sizeof(int), 1, f);
            if (hist_cnt < 0) hist_cnt = 0;
            if (hist_cnt > MAX_HISTORY_ENTRIES) hist_cnt = MAX_HISTORY_ENTRIES;
            g_tab_history_count[tab_i] = hist_cnt;
            fread(g_tab_history[tab_i], sizeof(char) * HISTORY_ENTRY_LEN, MAX_HISTORY_ENTRIES, f);

            for (int i = 0; i < loaded_items; i++) {
                fread(&ext4_vol_db[i], sizeof(float), 1, f);
                fread(&ext4_muted[i], sizeof(bool), 1, f);
                fread(&ext4_color_idx[i], sizeof(int), 1, f);
                fread(&ext4_peak_color_idx[i], sizeof(int), 1, f);
                fread(&ext4_group_id[i], sizeof(int), 1, f);
            }

            init_tab_app_volume_defaults(tab_i);
            uint32_t magic5 = 0;
            if (fread(&magic5, sizeof(uint32_t), 1, f) == 1 && magic5 == EHS_EXT5_MAGIC) {
                fread(g_track_locked[tab_i], sizeof(bool), MAX_TRACKS, f);
                fread(&g_lock_opt_full[tab_i], sizeof(bool), 1, f);
                fread(&g_lock_opt_prevent_lr[tab_i], sizeof(bool), 1, f);
                fread(&g_lock_opt_prevent_ud[tab_i], sizeof(bool), 1, f);
                fread(g_track_lock_lr_only[tab_i], sizeof(bool), MAX_TRACKS, f);
                fread(g_track_lock_ud_only[tab_i], sizeof(bool), MAX_TRACKS, f);
                fread(&g_lock_opt_complete_unlock[tab_i], sizeof(bool), 1, f);

                uint32_t magic6 = 0;
                if (fread(&magic6, sizeof(uint32_t), 1, f) == 1 && magic6 == EHS_EXT6_MAGIC) {
                    fread(&g_app_master_vol[tab_i], sizeof(float), 1, f);
                    fread(g_app_track_vol[tab_i], sizeof(float), MAX_TRACKS, f);
                    if (g_app_master_vol[tab_i] < 0.0f) g_app_master_vol[tab_i] = 0.0f;
                    if (g_app_master_vol[tab_i] > 100.0f) g_app_master_vol[tab_i] = 100.0f;
                    for (int tv = 0; tv < MAX_TRACKS; tv++) {
                        if (g_app_track_vol[tab_i][tv] < 0.0f) g_app_track_vol[tab_i][tv] = 0.0f;
                        if (g_app_track_vol[tab_i][tv] > 100.0f) g_app_track_vol[tab_i][tv] = 100.0f;
                    }
                }
            }
        }
    }
    fclose(f);

    // Resolve project directory from path so moved project folders always find their audio clips
    char ehs_dir[512] = {0};
    char ehs_base[128] = {0};
    {
        const char *ls1 = strrchr(path, '\\');
        const char *ls2 = strrchr(path, '/');
        if (ls2 && (!ls1 || ls2 > ls1)) ls1 = ls2;
        if (ls1) {
            size_t dl = (size_t)(ls1 - path);
            if (dl >= sizeof(ehs_dir)) dl = sizeof(ehs_dir) - 1;
            memcpy(ehs_dir, path, dl);
            ehs_dir[dl] = '\0';
            snprintf(ehs_base, sizeof(ehs_base), "%s", ls1 + 1);
        } else {
            strcpy(ehs_dir, ".");
            snprintf(ehs_base, sizeof(ehs_base), "%s", path);
        }
        strip_ehs_extension(ehs_base);
    }

    // Reconstruct every clip item and strictly restore saved split/trimmed offset, duration, start_time, fades & colors
    for (int i = 0; i < loaded_items; i++) {
        AudioItem *it = &p->items[i];
        memset(it, 0, sizeof(AudioItem));

        if (saved_fpath[i][0] != '\0') {
            FILE *chk_f = fopen(saved_fpath[i], "rb");
            if (chk_f) {
                fclose(chk_f);
            } else {
                const char *cfn = strrchr(saved_fpath[i], '\\');
                const char *cfn2 = strrchr(saved_fpath[i], '/');
                if (cfn2 && (!cfn || cfn2 > cfn)) cfn = cfn2;
                const char *clip_short = cfn ? (cfn + 1) : saved_fpath[i];
                char cand0[512], cand1[512], cand2[512];
#ifdef _WIN32
                snprintf(cand0, sizeof(cand0), "%s\\media\\%s", ehs_dir, clip_short);
                snprintf(cand1, sizeof(cand1), "%s\\%s", ehs_dir, clip_short);
                snprintf(cand2, sizeof(cand2), "%s\\%s\\%s", ehs_dir, ehs_base, clip_short);
#else
                snprintf(cand0, sizeof(cand0), "%s/media/%s", ehs_dir, clip_short);
                snprintf(cand1, sizeof(cand1), "%s/%s", ehs_dir, clip_short);
                snprintf(cand2, sizeof(cand2), "%s/%s/%s", ehs_dir, ehs_base, clip_short);
#endif
                FILE *c0 = fopen(cand0, "rb");
                if (c0) {
                    fclose(c0);
                    strncpy(saved_fpath[i], cand0, sizeof(saved_fpath[i]) - 1);
                    saved_fpath[i][sizeof(saved_fpath[i]) - 1] = '\0';
                } else {
                    FILE *c1 = fopen(cand1, "rb");
                    if (c1) {
                        fclose(c1);
                        strncpy(saved_fpath[i], cand1, sizeof(saved_fpath[i]) - 1);
                        saved_fpath[i][sizeof(saved_fpath[i]) - 1] = '\0';
                    } else {
                        FILE *c2 = fopen(cand2, "rb");
                        if (c2) {
                            fclose(c2);
                            strncpy(saved_fpath[i], cand2, sizeof(saved_fpath[i]) - 1);
                            saved_fpath[i][sizeof(saved_fpath[i]) - 1] = '\0';
                        }
                    }
                }
            }
        }

        int shared_idx = -1;
        for (int j = 0; j < i; j++) {
            if (saved_fpath[i][0] != '\0' && strcmp(saved_fpath[i], p->items[j].file_path) == 0 &&
                p->items[j].file_total_duration > 0.0) {
                shared_idx = j;
                break;
            }
        }

        (void)win;
        if (shared_idx >= 0) {
            AudioItem *src = &p->items[shared_idx];
            strncpy(it->file_path, src->file_path, sizeof(it->file_path) - 1);
            strncpy(it->raw_audio_path, src->raw_audio_path, sizeof(it->raw_audio_path) - 1);
            strncpy(it->label, src->label, sizeof(it->label) - 1);
            it->file_total_duration = src->file_total_duration;
            it->has_video = src->has_video;
            it->peak_count = src->peak_count;
            it->peaks = src->peaks;
            it->raw_file_handle = daw_fopen_LockFreeRead(it->raw_audio_path);
        } else {
            daw_load_clip_metadata_fast_reaper(
                it, saved_fpath[i],
                has_ext ? ext_file_tot_dur[i] : saved_dur[i],
                saved_dur[i],
                has_ext ? ext_has_video[i] : false
            );
        }

        // Restore exact saved clip position, trim offset, split duration, and metadata
        it->track_idx = (saved_trk[i] >= 0 && saved_trk[i] < p->num_tracks) ? saved_trk[i] : 0;
        it->start_time = (saved_start[i] >= 0.0) ? saved_start[i] : 0.0;
        it->offset_in_file = (saved_offset[i] >= 0.0) ? saved_offset[i] : 0.0;
        if (saved_dur[i] > 0.001) {
            it->duration = saved_dur[i];
        }
        if (has_ext) {
            if (ext_file_tot_dur[i] > 0.0 && it->file_total_duration < ext_file_tot_dur[i]) {
                it->file_total_duration = ext_file_tot_dur[i];
            }
            if (ext_label[i][0] != '\0') {
                strncpy(it->label, ext_label[i], sizeof(it->label) - 1);
                it->label[sizeof(it->label) - 1] = '\0';
            }
            if (ext_has_video[i]) it->has_video = true;
            it->is_selected = ext_selected[i];
            set_item_fade_in(p, i, ext_fin[i]);
            set_item_fade_out(p, i, ext_fout[i]);
        }
        if (has_ext4) {
            g_item_vol_db[tab_i][i]         = ext4_vol_db[i];
            g_item_muted[tab_i][i]          = ext4_muted[i];
            g_item_color_idx[tab_i][i]      = ext4_color_idx[i];
            g_item_peak_color_idx[tab_i][i] = ext4_peak_color_idx[i];
            g_item_group_id[tab_i][i]       = ext4_group_id[i];
        }
    }

    if (has_ext4) {
        if (saved_vid_rect.w >= 200 && saved_vid_rect.h >= 120) {
            p->video.rect = saved_vid_rect;
        }
        p->video.is_open = saved_vid_open;
    }
    if (p->video.is_open) {
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].has_video) {
                open_video_by_path(&p->video, p->items[i].file_path, ren);
                break;
            }
        }
    }

    // Restore exact saved playhead (cursor position) and viewport scroll position without auto-scroll override!
    if (has_ext) {
        daw_set_playhead_seconds(p, saved_playhead);
        p->scroll_x_sec = saved_scroll_x;
        p->scroll_y_px  = saved_scroll_y;
        g_last_observed_playhead[tab_i] = saved_playhead;
    }

    strncpy(p->project_path, path, sizeof(p->project_path) - 1);
    p->project_path[sizeof(p->project_path) - 1] = '\0';

    const char *fn = strrchr(path, '\\');
    if (!fn) fn = strrchr(path, '/');
    snprintf(p->project_name, sizeof(p->project_name), "%s", fn ? fn + 1 : path);
    strip_ehs_extension(p->project_name);

    // Ensure <ProjectFolder>/media exists for this loaded project
    char loaded_media_dir[512] = {0};
    get_project_media_folder(p, loaded_media_dir, sizeof(loaded_media_dir));

    // Reset undo stack to clean loaded state and clear dirty flag
    p->undo_mgr.head = -1;
    p->undo_mgr.count = 0;
    reset_tab_undo_history(tab_i);
    p->is_dirty = false;
    g_tab_needs_project_setup[tab_i] = false;
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

    bool has_sel = false;
    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].is_selected && !is_track_hidden(p->items[i].track_idx)) {
            has_sel = true;
            break;
        }
    }
    if (!has_sel) return;

    char proj_media_dir[512] = {0};
    get_project_media_folder(p, proj_media_dir, sizeof(proj_media_dir));
    (void)daw_get_engine_cache_dir();
    daw_ensure_all_project_raw_audio_ready(p, ren, win);

    p->is_playing = false;
    g_engine_processing = true;
    if (g_daw.audio_device > 0) {
        SDL_LockAudioDevice(g_daw.audio_device);
        SDL_UnlockAudioDevice(g_daw.audio_device);
    }
    show_processing_wait_modal(ren, win, "Join (Glue) Clips - Processing...", "Rendering joined audio clip & waveform peaks...", 15);
    push_undo_state();
    bool joined_any = false;
    static float glue_src_blk[4096 * 2];

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
        uint32_t total_frames = (uint32_t)(total_dur * (double)SAMPLE_RATE + 0.5);
        if (total_frames == 0) continue;

        unsigned int ticks = (unsigned int)SDL_GetTicks();
        char raw_path[512];
        char wav_path[512];
        snprintf(raw_path, sizeof(raw_path), "%s/glued_%u_t%d.raw", daw_get_engine_cache_dir(), ticks, t + 1);
#ifdef _WIN32
        snprintf(wav_path, sizeof(wav_path), "%s\\Glued_Track%d_%u.wav", proj_media_dir, t + 1, ticks);
#else
        snprintf(wav_path, sizeof(wav_path), "%s/Glued_Track%d_%u.wav", proj_media_dir, t + 1, ticks);
#endif

        FILE *f_raw = fopen(raw_path, "wb");
        if (!f_raw) continue;
        setvbuf(f_raw, NULL, _IOFBF, 65536);
        FILE *f_wav = fopen(wav_path, "wb");
        if (f_wav) {
            setvbuf(f_wav, NULL, _IOFBF, 65536);
            write_wav_pcm_header(f_wav, total_frames, SAMPLE_RATE, 2, 24);
        }

        int alloc_bins = (int)(total_frames / PEAK_SAMPLES_PER_BIN) + 16;
        PeakBin *new_peaks = (PeakBin *)calloc((size_t)alloc_bins, sizeof(PeakBin));
        int pk_cnt = 0;
        float bin_min = 0.0f, bin_max = 0.0f;
        int samp_in_bin = 0;

        const int chunk = 2048;
        float mix_lr[4096];
        uint32_t written = 0;

        while (written < total_frames) {
            int n = (int)(total_frames - written);
            if (n > chunk) n = chunk;
            memset(mix_lr, 0, (size_t)n * 2 * sizeof(float));

            double blk_s = min_s + (double)written / (double)SAMPLE_RATE;
            double blk_e = min_s + (double)(written + (uint32_t)n) / (double)SAMPLE_RATE;

            for (int si = 0; si < sel_cnt; si++) {
                int idx = sel_indices[si];
                AudioItem *it = &p->items[idx];
                if (!it->raw_file_handle && it->raw_audio_path[0]) {
                    it->raw_file_handle = daw_fopen_LockFreeRead(it->raw_audio_path);
                }
                if (!it->raw_file_handle || g_item_muted[tab][idx]) continue;

                double it_s = it->start_time;
                double it_e = it->start_time + it->duration;
                if (it_e <= blk_s || it_s >= blk_e) continue;

                int k_start = (it_s > blk_s) ? (int)((it_s - blk_s) * (double)SAMPLE_RATE) : 0;
                int k_end   = (it_e < blk_e) ? (int)ceil((it_e - blk_s) * (double)SAMPLE_RATE) : n;
                if (k_start < 0) k_start = 0;
                if (k_end > n) k_end = n;
                if (k_end <= k_start) continue;

                double t_first = min_s + (double)(written + (uint32_t)k_start) / (double)SAMPLE_RATE;
                int64_t src_f0 = (int64_t)(((t_first - it_s) + it->offset_in_file) * (double)SAMPLE_RATE + 0.5);
                if (src_f0 < 0) src_f0 = 0;
                int need_frames = (k_end - k_start) + 4;
                if (need_frames > 4096) need_frames = 4096;

                if (daw_fseek64(it->raw_file_handle, src_f0 * (int64_t)(sizeof(float) * 2), SEEK_SET) != 0) continue;
                size_t got_f = fread(glue_src_blk, sizeof(float) * 2, (size_t)need_frames, it->raw_file_handle);
                if (got_f == 0) continue;

                float g_lin = db_to_linear(g_item_vol_db[tab][idx]);
                for (int k = k_start; k < k_end; k++) {
                    int sf = k - k_start;
                    if (sf < 0 || (size_t)sf >= got_f) break;
                    double t_sec = min_s + (double)(written + (uint32_t)k) / (double)SAMPLE_RATE;
                    if (t_sec < it_s || t_sec > it_e) continue;
                    float xf = compute_item_crossfade_gain(p, idx, t_sec);
                    mix_lr[k * 2]     += glue_src_blk[sf * 2]     * g_lin * xf;
                    mix_lr[k * 2 + 1] += glue_src_blk[sf * 2 + 1] * g_lin * xf;
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

        if (samp_in_bin > 0 && new_peaks && pk_cnt < alloc_bins) {
            new_peaks[pk_cnt].min_val = bin_min;
            new_peaks[pk_cnt].max_val = bin_max;
            pk_cnt++;
        }

        fflush(f_raw);
        fclose(f_raw);
        if (f_wav) {
            fseek(f_wav, 0, SEEK_SET);
            write_wav_pcm_header(f_wav, written, SAMPLE_RATE, 2, 24);
            fflush(f_wav);
            fclose(f_wav);
        }
        {
            char glued_pk[512] = {0};
            daw_build_project_pk_path(wav_path, glued_pk, sizeof(glued_pk));
            if (glued_pk[0] && new_peaks && pk_cnt > 0) {
                daw_save_pk_file(glued_pk, new_peaks, pk_cnt, total_dur, false);
            }
        }

        int keep_color      = g_item_color_idx[tab][sel_indices[0]];
        int keep_peak_color = g_item_peak_color_idx[tab][sel_indices[0]];
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
            dst->peaks = daw_register_shared_peaks(dst->raw_audio_path, new_peaks, pk_cnt, total_dur, false);
            dst->peak_count = pk_cnt;
            dst->raw_file_handle = daw_fopen_LockFreeRead(dst->raw_audio_path);
            g_item_fade_in[tab][ni]        = 0.0;
            g_item_fade_out[tab][ni]       = 0.0;
            g_item_vol_db[tab][ni]         = 0.0f;
            g_item_muted[tab][ni]          = false;
            g_item_color_idx[tab][ni]      = keep_color;
            g_item_peak_color_idx[tab][ni] = keep_peak_color;
            g_item_group_id[tab][ni]       = 0;
            g_last_clicked_item_idx        = ni;
            joined_any = true;
        } else {
            if (new_peaks) free(new_peaks);
        }
    }

    g_engine_processing = false;
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
        // Stop Recording and finalize the WAV header in Temporary Media
        g_rec.is_recording = false;
        p->is_playing = false;
        if (g_rec.capture_dev > 0) {
            SDL_CloseAudioDevice(g_rec.capture_dev);
            g_rec.capture_dev = 0;
        }
        if (g_rec.wav_file) {
            fseek(g_rec.wav_file, 0, SEEK_SET);
            write_wav_pcm_header(g_rec.wav_file, g_rec.rec_frames_written, SAMPLE_RATE, 2, 24);
            fflush(g_rec.wav_file);
            fclose(g_rec.wav_file);
            g_rec.wav_file = NULL;
        }
        if (g_rec.raw_cache_file) {
            fflush(g_rec.raw_cache_file);
            fclose(g_rec.raw_cache_file);
            g_rec.raw_cache_file = NULL;
        }
        if (g_rec.rec_item_idx >= 0 && g_rec.rec_item_idx < p->num_items) {
            AudioItem *it = &p->items[g_rec.rec_item_idx];
            if (g_rec.samples_in_bin > 0 && it->peaks && it->peak_count < g_rec.allocated_bins) {
                it->peaks[it->peak_count].min_val = g_rec.cur_bin_min;
                it->peaks[it->peak_count].max_val = g_rec.cur_bin_max;
                it->peak_count++;
                g_rec.samples_in_bin = 0;
            }
            if (it->peaks && it->peak_count > 0) {
                it->peaks = daw_register_shared_peaks(it->raw_audio_path, it->peaks, it->peak_count, it->file_total_duration, false);
                char rec_pk[512] = {0};
                daw_build_project_pk_path(it->file_path, rec_pk, sizeof(rec_pk));
                if (rec_pk[0]) daw_save_pk_file(rec_pk, it->peaks, it->peak_count, it->file_total_duration, false);
            }
            it->raw_file_handle = daw_fopen_LockFreeRead(it->raw_audio_path);
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

    char proj_media_dir[512] = {0};
    get_project_media_folder(p, proj_media_dir, sizeof(proj_media_dir));
    (void)daw_get_engine_cache_dir();

    time_t now_t = time(NULL);
    struct tm *lt = localtime(&now_t);
    char ts[32] = "20261021_083000";
    if (lt) strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", lt);

#ifdef _WIN32
    snprintf(g_rec.current_wav_path, sizeof(g_rec.current_wav_path),
             "%s\\Rec_Track%d_%s.wav", proj_media_dir, target_trk + 1, ts);
#else
    snprintf(g_rec.current_wav_path, sizeof(g_rec.current_wav_path),
             "%s/Rec_Track%d_%s.wav", proj_media_dir, target_trk + 1, ts);
#endif
    snprintf(g_rec.current_raw_path, sizeof(g_rec.current_raw_path),
             "%s/rec_%u_%s.raw", daw_get_engine_cache_dir(), (unsigned int)SDL_GetTicks(), ts);

    g_rec.wav_file = fopen(g_rec.current_wav_path, "wb");
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
    int tab = eng->active_tab;
    if (tab < 0 || tab >= MAX_TABS) tab = 0;
    ProjectTab *p = &eng->tabs[tab];
    float *out = (float *)stream;
    int frames_needed = len / (int)(sizeof(float) * 2);
    memset(stream, 0, (size_t)len);

    if (g_engine_processing || !p->is_playing || frames_needed <= 0) return;

    uint32_t seek_serial_before = g_playhead_seek_serial[tab];
    double st = *(volatile double *)&p->playhead_seconds;

    // Logic Pro style Cycle Range loop during playback
    if (is_range_active()) {
        double rs = get_range_start();
        double re = get_range_end();
        if (st >= re || st < rs) {
            st = rs;
            *(volatile double *)&p->playhead_seconds = rs;
        }
    }

    bool any_solo = false;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->tracks[t].solo) { any_solo = true; break; }
    }

    double et = st + ((double)frames_needed / (double)SAMPLE_RATE);
    float chunk[1024 * 2];

    ensure_app_vol_defaults_initialized();
    float app_master_pct = g_app_master_vol[tab];
    float app_master_gain = (app_master_pct <= 0.001f) ? 0.0f : (app_master_pct / 100.0f);

    for (int i = 0; i < p->num_items; i++) {
        AudioItem *it = &p->items[i];
        if (!it->raw_file_handle) continue;
        if (it->track_idx < 0 || it->track_idx >= p->num_tracks) continue;

        Track *tr = &p->tracks[it->track_idx];
        // Requirement 3: Solo takes first preference over Mute when both are active on a track header
        if (!tr->solo && (tr->mute || any_solo)) continue;
        if (g_item_muted[tab][i]) continue;

        // Requirement 4: App Hearing Volume (0 = infinity mute, 0..100% monitoring level only)
        float app_trk_pct = g_app_track_vol[tab][it->track_idx];
        if (app_master_gain <= 0.0f || app_trk_pct <= 0.001f) continue;
        float app_hearing_gain = app_master_gain * (app_trk_pct / 100.0f);

        float linear_gain = db_to_linear(p->track_db[it->track_idx] + g_item_vol_db[tab][i]) * app_hearing_gain;
        double it_s = it->start_time;
        double it_e = it->start_time + it->duration;

        if (it_e > st && it_s < et) {
            int f_start = 0;
            if (it_s > st) {
                f_start = (int)((it_s - st) * (double)SAMPLE_RATE);
                if (f_start < 0) f_start = 0;
                if (f_start >= frames_needed) continue;
            }
            int f_end = frames_needed;
            if (it_e < et) {
                f_end = (int)((it_e - st) * (double)SAMPLE_RATE);
                if (f_end > frames_needed) f_end = frames_needed;
                if (f_end <= f_start) continue;
            }

            int total_to_read = f_end - f_start;
            double clip_start_t = st + ((double)f_start / (double)SAMPLE_RATE);
            int64_t sample_offset = (int64_t)(((clip_start_t - it_s) + it->offset_in_file) * (double)SAMPLE_RATE + 0.5);
            if (sample_offset < 0) sample_offset = 0;

            int64_t target_byte_off = sample_offset * (int64_t)(sizeof(float) * 2);
            int64_t cur_byte_off = daw_ftell64(it->raw_file_handle);
            if (cur_byte_off < 0 || llabs(cur_byte_off - target_byte_off) > (int64_t)(sizeof(float) * 4)) {
                if (daw_fseek64(it->raw_file_handle, target_byte_off, SEEK_SET) != 0) {
                    continue;
                }
            }

            // Precheck once per buffer if this 21ms slice intersects any fade-in, fade-out, 2ms edge ramp, or crossfade overlap
            double man_fin  = get_item_fade_in(p, i);
            double man_fout = get_item_fade_out(p, i);
            bool needs_env = (st < it_s + man_fin + 0.004) || (et > it_e - man_fout - 0.004);
            if (!needs_env) {
                for (int o = 0; o < p->num_items; o++) {
                    if (o == i) continue;
                    AudioItem *ot = &p->items[o];
                    if (ot->track_idx != it->track_idx) continue;
                    double ov_s = fmax(it_s, ot->start_time);
                    double ov_e = fmin(it_e, ot->start_time + ot->duration);
                    if (ov_e > ov_s + 0.0005 && et >= ov_s && st <= ov_e) {
                        needs_env = true;
                        break;
                    }
                }
            }

            int done = 0;
            while (done < total_to_read) {
                int blk = total_to_read - done;
                if (blk > 1024) blk = 1024;
                size_t read_frames = fread(chunk, sizeof(float) * 2, (size_t)blk, it->raw_file_handle);
                if (read_frames == 0) break;
                if (!needs_env) {
                    for (size_t f = 0; f < read_frames; f++) {
                        int out_idx = (f_start + done + (int)f) * 2;
                        out[out_idx]     += chunk[f * 2]     * linear_gain;
                        out[out_idx + 1] += chunk[f * 2 + 1] * linear_gain;
                    }
                } else {
                    for (size_t f = 0; f < read_frames; f++) {
                        int out_idx = f_start + done + (int)f;
                        double sample_t = st + ((double)out_idx / (double)SAMPLE_RATE);
                        float xfade_gain = compute_item_crossfade_gain(p, i, sample_t);
                        float total_gain = linear_gain * xfade_gain;
                        out[out_idx * 2]     += chunk[f * 2]     * total_gain;
                        out[out_idx * 2 + 1] += chunk[f * 2 + 1] * total_gain;
                    }
                }
                done += (int)read_frames;
                if ((int)read_frames < blk) break;
            }
        }
    }

    if (g_rec.is_recording && g_rec.capture_dev == 0) {
        float synth_buf[2048];
        int n_f = (frames_needed <= 1024) ? frames_needed : 1024;
        for (int f = 0; f < n_f; f++) {
            double t = st + (double)f / (double)SAMPLE_RATE;
            float sig = 0.25f * sinf((float)(t * 2.0 * 3.14159265 * 220.0)) * (0.6f + 0.4f * sinf((float)(t * 6.0)));
            synth_buf[f * 2]     = sig;
            synth_buf[f * 2 + 1] = sig;
        }
        append_recorded_stereo_frames(p, synth_buf, n_f);
    }

    // Only advance playhead if the UI thread did not issue a new seek during this callback
    if (g_playhead_seek_serial[tab] == seek_serial_before) {
        double next_t = st + ((double)frames_needed / (double)SAMPLE_RATE);
        if (is_range_active() && next_t >= get_range_end()) {
            next_t = get_range_start();
        }
        *(volatile double *)&p->playhead_seconds = next_t;
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
            Track *tr = &p->tracks[it->track_idx];
            if (!tr->solo && (tr->mute || any_solo)) continue;
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
    int cur_tab_hit = get_valid_tab_idx();
    for (int i = p->num_items - 1; i >= 0; i--) {
        AudioItem *it = &p->items[i];
        if (is_track_hidden(it->track_idx) || is_track_full_locked_tab(cur_tab_hit, it->track_idx)) continue;
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

#include "keyboard_actions.h"

// Returns true if any modal popup is open OR if the mouse cursor (mx, my) is over any open popup window or dropdown menu.
// Used to keep mouse cursor and focus strictly on the active popup/menu and never on the background panel.
// Renders the Unified Export Media Popup Window (including MP4 Resolution Size dropdown, AAC-only MP4 audio,
// Green Checkmark on Two-pass render, Green 100% completion progress bar, and "File Already Exists" modal prompt)
static void render_export_popup_window(SDL_Renderer *ren, ProjectTab *p) {
    if (!g_export.is_open || !ren) return;
    SDL_Rect *er = &g_export.win_rect;
    if (er->w < 720) er->w = 720;
    if (er->h < 620) er->h = 620;
    int ex = er->x, ey = er->y, ew = er->w, eh = er->h;
    SDL_Color c_white  = { 228, 228, 228, 255 };
    SDL_Color c_muted  = { 145, 140, 135, 255 };
    SDL_Color c_accent = get_theme_accent_sdl_color();

    // Subtle drop shadow
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_Rect shadow = { ex + 6, ey + 6, ew, eh };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 165);
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

    const char *bounds_mode = is_range_active()
                              ? "Bounds: Cycle Range"
                              : (g_export.source == EXPORT_SRC_ITEMS ? "Bounds: Selected Items" : "Bounds: Entire Project");
    draw_text(ren, bounds_mode, ex + ew - 230, ey + 9, c_muted);

    // ---- TOP EXPORT TIMINGS BAR: Start, End, Duration (Requirement 1) ----
    double exp_s = 0.0, exp_e = 0.0;
    get_export_time_bounds(p, &exp_s, &exp_e);
    double exp_dur = fmax(0.0, exp_e - exp_s);
    char s_tc[32], e_tc[32], d_tc[32];
    format_timecode(exp_s, s_tc, sizeof(s_tc));
    format_timecode(exp_e, e_tc, sizeof(e_tc));
    format_timecode(exp_dur, d_tc, sizeof(d_tc));

    SDL_Rect t_bar = { ex + 18, ey + 36, ew - 36, 28 };
    SDL_SetRenderDrawColor(ren, 18, 17, 16, 255);
    SDL_RenderFillRect(ren, &t_bar);
    draw_brown_border_rect(ren, &t_bar, false, true);

    char t_start_lbl[64], t_end_lbl[64], t_dur_lbl[64];
    snprintf(t_start_lbl, sizeof(t_start_lbl), "Start: %s", s_tc);
    snprintf(t_end_lbl,   sizeof(t_end_lbl),   "End: %s",   e_tc);
    snprintf(t_dur_lbl,   sizeof(t_dur_lbl),   "Duration: %s", d_tc);
    draw_text(ren, t_start_lbl, ex + 32,  ey + 44, c_white);
    draw_text(ren, t_end_lbl,   ex + 258, ey + 44, c_white);
    draw_text(ren, t_dur_lbl,   ex + 474, ey + 44, c_accent);

    // ---- 1. SOURCE SECTION ----
    int sy_src = ey + 74;
    draw_text(ren, "Source:", ex + 18, sy_src + 8, c_white);

    SDL_Rect b_src_master = { ex + 130, sy_src, 150, 28 };
    SDL_SetRenderDrawColor(ren, g_export.source == EXPORT_SRC_MASTER ? 68 : 38,
                                g_export.source == EXPORT_SRC_MASTER ? 48 : 36,
                                g_export.source == EXPORT_SRC_MASTER ? 32 : 34, 255);
    SDL_RenderFillRect(ren, &b_src_master);
    draw_brown_border_rect(ren, &b_src_master, g_export.source == EXPORT_SRC_MASTER, false);
    draw_text(ren, "Master mix", b_src_master.x + 35, sy_src + 8, c_white);

    SDL_Rect b_src_stems = { ex + 290, sy_src, 210, 28 };
    SDL_SetRenderDrawColor(ren, g_export.source == EXPORT_SRC_STEMS ? 68 : 38,
                                g_export.source == EXPORT_SRC_STEMS ? 48 : 36,
                                g_export.source == EXPORT_SRC_STEMS ? 32 : 34, 255);
    SDL_RenderFillRect(ren, &b_src_stems);
    draw_brown_border_rect(ren, &b_src_stems, g_export.source == EXPORT_SRC_STEMS, false);
    draw_text(ren, "Selected tracks (stems)", b_src_stems.x + 13, sy_src + 8, c_white);

    SDL_Rect b_src_items = { ex + 510, sy_src, 192, 28 };
    SDL_SetRenderDrawColor(ren, g_export.source == EXPORT_SRC_ITEMS ? 68 : 38,
                                g_export.source == EXPORT_SRC_ITEMS ? 48 : 36,
                                g_export.source == EXPORT_SRC_ITEMS ? 32 : 34, 255);
    SDL_RenderFillRect(ren, &b_src_items);
    draw_brown_border_rect(ren, &b_src_items, g_export.source == EXPORT_SRC_ITEMS, false);
    draw_text(ren, "Selected media items", b_src_items.x + 16, sy_src + 8, c_white);

    // ---- 2. OUTPUT SECTION (Directory + Browse, File Name) ----
    int sy_dir = ey + 114;
    draw_text(ren, "Directory:", ex + 18, sy_dir + 8, c_white);

    SDL_Rect box_dir = { ex + 130, sy_dir, 452, 28 };
    bool ed_dir = (g_export.active_field == EXP_FIELD_DIR);
    SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
    SDL_RenderFillRect(ren, &box_dir);
    draw_brown_border_rect(ren, &box_dir, ed_dir, false);
    SDL_Rect box_dir_clip = { box_dir.x + 4, box_dir.y + 2, box_dir.w - 8, box_dir.h - 4 };
    if (ed_dir) {
        daw_draw_text_edit_overlay(ren, g_export.directory, box_dir.x + 6, box_dir.y, box_dir.h, box_dir.x + box_dir.w - 4);
    }
    draw_text_clipped(ren, g_export.directory, box_dir.x + 6, sy_dir + 8, c_white, &box_dir_clip, NULL);

    SDL_Rect btn_browse = { ex + 592, sy_dir, 110, 28 };
    SDL_SetRenderDrawColor(ren, 52, 40, 30, 255);
    SDL_RenderFillRect(ren, &btn_browse);
    draw_brown_border_rect(ren, &btn_browse, false, false);
    draw_text(ren, "Browse...", btn_browse.x + 19, sy_dir + 8, c_white);

    int sy_fn = ey + 150;
    draw_text(ren, "File Name:", ex + 18, sy_fn + 8, c_white);

    SDL_Rect box_fn = { ex + 130, sy_fn, 452, 28 };
    bool ed_fn = (g_export.active_field == EXP_FIELD_FILENAME);
    SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
    SDL_RenderFillRect(ren, &box_fn);
    draw_brown_border_rect(ren, &box_fn, ed_fn, false);
    SDL_Rect box_fn_clip = { box_fn.x + 4, box_fn.y + 2, box_fn.w - 8, box_fn.h - 4 };
    if (ed_fn) {
        daw_draw_text_edit_overlay(ren, g_export.filename, box_fn.x + 6, box_fn.y, box_fn.h, box_fn.x + box_fn.w - 4);
    }
    draw_text_clipped(ren, g_export.filename, box_fn.x + 6, sy_fn + 8, c_white, &box_fn_clip, NULL);
    char ext_preview[16];
    snprintf(ext_preview, sizeof(ext_preview), ".%s", get_export_ext());
    draw_text(ren, ext_preview, ex + 594, sy_fn + 8, c_muted);

    // ---- 3. SAMPLE RATE & CHANNELS SECTION ----
    int sy_sr = ey + 190;
    draw_text(ren, "Sample Rate:", ex + 18, sy_sr + 8, c_white);

    SDL_Rect b_sr_48 = { ex + 130, sy_sr, 110, 28 };
    SDL_SetRenderDrawColor(ren, g_export.sample_rate == 48000 ? 68 : 38,
                                g_export.sample_rate == 48000 ? 48 : 36,
                                g_export.sample_rate == 48000 ? 32 : 34, 255);
    SDL_RenderFillRect(ren, &b_sr_48);
    draw_brown_border_rect(ren, &b_sr_48, g_export.sample_rate == 48000, false);
    draw_text(ren, "48,000 Hz", b_sr_48.x + 19, sy_sr + 8, c_white);

    SDL_Rect b_sr_44 = { ex + 248, sy_sr, 110, 28 };
    SDL_SetRenderDrawColor(ren, g_export.sample_rate == 44100 ? 68 : 38,
                                g_export.sample_rate == 44100 ? 48 : 36,
                                g_export.sample_rate == 44100 ? 32 : 34, 255);
    SDL_RenderFillRect(ren, &b_sr_44);
    draw_brown_border_rect(ren, &b_sr_44, g_export.sample_rate == 44100, false);
    draw_text(ren, "44,100 Hz", b_sr_44.x + 19, sy_sr + 8, c_white);

    draw_text(ren, "Channels:", ex + 382, sy_sr + 8, c_white);

    SDL_Rect b_ch_mono = { ex + 466, sy_sr, 112, 28 };
    SDL_SetRenderDrawColor(ren, g_export.channels == 1 ? 68 : 38,
                                g_export.channels == 1 ? 48 : 36,
                                g_export.channels == 1 ? 32 : 34, 255);
    SDL_RenderFillRect(ren, &b_ch_mono);
    draw_brown_border_rect(ren, &b_ch_mono, g_export.channels == 1, false);
    draw_text(ren, "Mono", b_ch_mono.x + 40, sy_sr + 8, c_white);

    SDL_Rect b_ch_stereo = { ex + 586, sy_sr, 116, 28 };
    SDL_SetRenderDrawColor(ren, g_export.channels == 2 ? 68 : 38,
                                g_export.channels == 2 ? 48 : 36,
                                g_export.channels == 2 ? 32 : 34, 255);
    SDL_RenderFillRect(ren, &b_ch_stereo);
    draw_brown_border_rect(ren, &b_ch_stereo, g_export.channels == 2, false);
    draw_text(ren, "Stereo", b_ch_stereo.x + 34, sy_sr + 8, c_white);

    // ---- 4. FORMAT SECTION ----
    int sy_fmt = ey + 230;
    draw_text(ren, "Format:", ex + 18, sy_fmt + 8, c_white);

    SDL_Rect b_fmt_wav = { ex + 130, sy_fmt, 160, 28 };
    SDL_SetRenderDrawColor(ren, g_export.format == EXPORT_FMT_WAV ? 68 : 38,
                                g_export.format == EXPORT_FMT_WAV ? 48 : 36,
                                g_export.format == EXPORT_FMT_WAV ? 32 : 34, 255);
    SDL_RenderFillRect(ren, &b_fmt_wav);
    draw_brown_border_rect(ren, &b_fmt_wav, g_export.format == EXPORT_FMT_WAV, false);
    draw_text(ren, "WAV (PCM)", b_fmt_wav.x + 44, sy_fmt + 8, c_white);

    SDL_Rect b_fmt_mp3 = { ex + 300, sy_fmt, 160, 28 };
    SDL_SetRenderDrawColor(ren, g_export.format == EXPORT_FMT_MP3 ? 68 : 38,
                                g_export.format == EXPORT_FMT_MP3 ? 48 : 36,
                                g_export.format == EXPORT_FMT_MP3 ? 32 : 34, 255);
    SDL_RenderFillRect(ren, &b_fmt_mp3);
    draw_brown_border_rect(ren, &b_fmt_mp3, g_export.format == EXPORT_FMT_MP3, false);
    draw_text(ren, "MP3 Audio", b_fmt_mp3.x + 44, sy_fmt + 8, c_white);

    SDL_Rect b_fmt_mp4 = { ex + 470, sy_fmt, 232, 28 };
    SDL_SetRenderDrawColor(ren, g_export.format == EXPORT_FMT_MP4 ? 68 : 38,
                                g_export.format == EXPORT_FMT_MP4 ? 48 : 36,
                                g_export.format == EXPORT_FMT_MP4 ? 32 : 34, 255);
    SDL_RenderFillRect(ren, &b_fmt_mp4);
    draw_brown_border_rect(ren, &b_fmt_mp4, g_export.format == EXPORT_FMT_MP4, false);
    draw_text(ren, "MP4 (MPEG-4 Video)", b_fmt_mp4.x + 44, sy_fmt + 8, c_white);

    // Format Details Sub-Panel (ey + 268 .. ey + 442)
    SDL_Rect sub_panel = { ex + 18, ey + 268, ew - 36, 174 };
    SDL_SetRenderDrawColor(ren, 20, 19, 18, 255);
    SDL_RenderFillRect(ren, &sub_panel);
    draw_brown_border_rect(ren, &sub_panel, false, true);

    int sy_sub = ey + 274;
    if (g_export.format == EXPORT_FMT_WAV) {
        draw_text(ren, "WAV Bit Depth:", ex + 34, sy_sub + 22, c_white);

        SDL_Rect b_w16 = { ex + 160, sy_sub + 14, 150, 28 };
        SDL_SetRenderDrawColor(ren, g_export.wav_bit_depth == 16 ? 68 : 38,
                                    g_export.wav_bit_depth == 16 ? 48 : 36,
                                    g_export.wav_bit_depth == 16 ? 32 : 34, 255);
        SDL_RenderFillRect(ren, &b_w16);
        draw_brown_border_rect(ren, &b_w16, g_export.wav_bit_depth == 16, false);
        draw_text(ren, "16 bit PCM", b_w16.x + 35, sy_sub + 22, c_white);

        SDL_Rect b_w24 = { ex + 322, sy_sub + 14, 150, 28 };
        SDL_SetRenderDrawColor(ren, g_export.wav_bit_depth == 24 ? 68 : 38,
                                    g_export.wav_bit_depth == 24 ? 48 : 36,
                                    g_export.wav_bit_depth == 24 ? 32 : 34, 255);
        SDL_RenderFillRect(ren, &b_w24);
        draw_brown_border_rect(ren, &b_w24, g_export.wav_bit_depth == 24, false);
        draw_text(ren, "24 bit PCM", b_w24.x + 35, sy_sub + 22, c_white);

        SDL_Rect b_w32 = { ex + 484, sy_sub + 14, 150, 28 };
        SDL_SetRenderDrawColor(ren, g_export.wav_bit_depth == 32 ? 68 : 38,
                                    g_export.wav_bit_depth == 32 ? 48 : 36,
                                    g_export.wav_bit_depth == 32 ? 32 : 34, 255);
        SDL_RenderFillRect(ren, &b_w32);
        draw_brown_border_rect(ren, &b_w32, g_export.wav_bit_depth == 32, false);
        draw_text(ren, "32 bit PCM", b_w32.x + 35, sy_sub + 22, c_white);

        draw_text(ren, "Uncompressed studio-quality linear PCM WAVE format.", ex + 34, sy_sub + 70, c_muted);
    } else if (g_export.format == EXPORT_FMT_MP3) {
        draw_text(ren, "MP3 Bitrate:", ex + 34, sy_sub + 22, c_white);

        SDL_Rect b_m128 = { ex + 160, sy_sub + 14, 150, 28 };
        SDL_SetRenderDrawColor(ren, g_export.mp3_bitrate == 128 ? 68 : 38,
                                    g_export.mp3_bitrate == 128 ? 48 : 36,
                                    g_export.mp3_bitrate == 128 ? 32 : 34, 255);
        SDL_RenderFillRect(ren, &b_m128);
        draw_brown_border_rect(ren, &b_m128, g_export.mp3_bitrate == 128, false);
        draw_text(ren, "128 kbps", b_m128.x + 43, sy_sub + 22, c_white);

        SDL_Rect b_m192 = { ex + 322, sy_sub + 14, 150, 28 };
        SDL_SetRenderDrawColor(ren, g_export.mp3_bitrate == 192 ? 68 : 38,
                                    g_export.mp3_bitrate == 192 ? 48 : 36,
                                    g_export.mp3_bitrate == 192 ? 32 : 34, 255);
        SDL_RenderFillRect(ren, &b_m192);
        draw_brown_border_rect(ren, &b_m192, g_export.mp3_bitrate == 192, false);
        draw_text(ren, "192 kbps", b_m192.x + 43, sy_sub + 22, c_white);

        SDL_Rect b_m320 = { ex + 484, sy_sub + 14, 150, 28 };
        SDL_SetRenderDrawColor(ren, g_export.mp3_bitrate == 320 ? 68 : 38,
                                    g_export.mp3_bitrate == 320 ? 48 : 36,
                                    g_export.mp3_bitrate == 320 ? 32 : 34, 255);
        SDL_RenderFillRect(ren, &b_m320);
        draw_brown_border_rect(ren, &b_m320, g_export.mp3_bitrate == 320, false);
        draw_text(ren, "320 kbps", b_m320.x + 43, sy_sub + 22, c_white);

        draw_text(ren, "MPEG-1/2 Audio Layer III constant bitrate encoding.", ex + 34, sy_sub + 70, c_muted);
    } else if (g_export.format == EXPORT_FMT_MP4) {
        // Row 1 (Requirement 4): File: [ MPEG-4 ]   Size: [ Dropdown with "Manual" at end ]   FPS: [ 25.00 ] fps
        int r1_y = sy_sub + 14;
        draw_text(ren, "File:", ex + 34, r1_y + 7, c_white);
        SDL_Rect box_mpeg4 = { ex + 96, r1_y, 108, 26 };
        SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
        SDL_RenderFillRect(ren, &box_mpeg4);
        draw_brown_border_rect(ren, &box_mpeg4, false, false);
        draw_text(ren, "MPEG-4", box_mpeg4.x + 30, r1_y + 7, c_white);

        draw_text(ren, "Size:", ex + 218, r1_y + 7, c_white);
        int cur_res_idx = get_current_mp4_res_preset_idx();
        if (!g_export.mp4_size_manual_mode && cur_res_idx >= 0 && cur_res_idx < NUM_MP4_RES_PRESETS) {
            SDL_Rect box_size_dd = { ex + 266, r1_y, 226, 26 };
            SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
            SDL_RenderFillRect(ren, &box_size_dd);
            draw_brown_border_rect(ren, &box_size_dd, g_export.mp4_size_dropdown_open, false);
            draw_text(ren, g_mp4_res_presets[cur_res_idx].label, box_size_dd.x + 8, r1_y + 7, c_white);
            draw_text(ren, "v", box_size_dd.x + box_size_dd.w - 16, r1_y + 7, c_muted);
        } else {
            // Inline Manual Width x Height input right in the Size slot + [v] dropdown toggle button
            SDL_Rect box_mp4_w = { ex + 266, r1_y, 84, 26 };
            bool ed_w = (g_export.active_field == EXP_FIELD_MP4_WIDTH);
            SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
            SDL_RenderFillRect(ren, &box_mp4_w);
            draw_brown_border_rect(ren, &box_mp4_w, ed_w, false);
            if (ed_w) {
                daw_draw_text_edit_overlay(ren, g_export.mp4_width_str, box_mp4_w.x + 8, box_mp4_w.y, box_mp4_w.h, box_mp4_w.x + box_mp4_w.w - 4);
            }
            draw_text(ren, g_export.mp4_width_str, box_mp4_w.x + 8, r1_y + 7, ed_w ? c_accent : c_white);

            draw_text(ren, "x", ex + 356, r1_y + 7, c_white);

            SDL_Rect box_mp4_h = { ex + 370, r1_y, 84, 26 };
            bool ed_h = (g_export.active_field == EXP_FIELD_MP4_HEIGHT);
            SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
            SDL_RenderFillRect(ren, &box_mp4_h);
            draw_brown_border_rect(ren, &box_mp4_h, ed_h, false);
            if (ed_h) {
                daw_draw_text_edit_overlay(ren, g_export.mp4_height_str, box_mp4_h.x + 8, box_mp4_h.y, box_mp4_h.h, box_mp4_h.x + box_mp4_h.w - 4);
            }
            draw_text(ren, g_export.mp4_height_str, box_mp4_h.x + 8, r1_y + 7, ed_h ? c_accent : c_white);

            SDL_Rect btn_sdd = { ex + 458, r1_y, 34, 26 };
            SDL_SetRenderDrawColor(ren, 44, 36, 30, 255);
            SDL_RenderFillRect(ren, &btn_sdd);
            draw_brown_border_rect(ren, &btn_sdd, g_export.mp4_size_dropdown_open, false);
            draw_text(ren, "v", btn_sdd.x + 13, r1_y + 7, c_white);
        }

        draw_text(ren, "FPS:", ex + 504, r1_y + 7, c_white);
        SDL_Rect box_fps = { ex + 544, r1_y, 92, 26 };
        bool ed_fps = (g_export.active_field == EXP_FIELD_MP4_FPS);
        SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
        SDL_RenderFillRect(ren, &box_fps);
        draw_brown_border_rect(ren, &box_fps, ed_fps, false);
        if (ed_fps) {
            daw_draw_text_edit_overlay(ren, g_export.mp4_fps_str, box_fps.x + 8, box_fps.y, box_fps.h, box_fps.x + box_fps.w - 4);
        }
        draw_text(ren, g_export.mp4_fps_str, box_fps.x + 8, r1_y + 7, c_white);
        draw_text(ren, "fps", ex + 644, r1_y + 7, c_muted);

        // Row 2 (Requirement 5): Video: [ H.264 ]   Bitrate: [ Dropdown with "Manual" at end ]
        int r2_y = sy_sub + 58;
        draw_text(ren, "Video:", ex + 34, r2_y + 7, c_white);
        SDL_Rect box_vcodec = { ex + 96, r2_y, 108, 26 };
        SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
        SDL_RenderFillRect(ren, &box_vcodec);
        draw_brown_border_rect(ren, &box_vcodec, false, false);
        draw_text(ren, "H.264", box_vcodec.x + 34, r2_y + 7, c_white);

        draw_text(ren, "Bitrate:", ex + 218, r2_y + 7, c_white);
        int cur_vk_idx = get_current_mp4_v_kbps_preset_idx();
        if (!g_export.mp4_v_kbps_manual_mode && cur_vk_idx >= 0 && cur_vk_idx < NUM_MP4_V_KBPS_PRESETS) {
            SDL_Rect box_vk_dd = { ex + 286, r2_y, 206, 26 };
            SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
            SDL_RenderFillRect(ren, &box_vk_dd);
            draw_brown_border_rect(ren, &box_vk_dd, g_export.mp4_v_kbps_dropdown_open, false);
            char vk_lbl[32];
            snprintf(vk_lbl, sizeof(vk_lbl), "%d kbps", g_mp4_v_kbps_presets[cur_vk_idx]);
            draw_text(ren, vk_lbl, box_vk_dd.x + 10, r2_y + 7, c_white);
            draw_text(ren, "v", box_vk_dd.x + box_vk_dd.w - 16, r2_y + 7, c_muted);
        } else {
            // Inline Manual Video Bitrate input + kbps + [v] dropdown toggle button
            SDL_Rect box_vkbps = { ex + 286, r2_y, 114, 26 };
            bool ed_vk = (g_export.active_field == EXP_FIELD_MP4_V_KBPS);
            SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
            SDL_RenderFillRect(ren, &box_vkbps);
            draw_brown_border_rect(ren, &box_vkbps, ed_vk, false);
            if (ed_vk) {
                daw_draw_text_edit_overlay(ren, g_export.mp4_video_kbps_str, box_vkbps.x + 8, box_vkbps.y, box_vkbps.h, box_vkbps.x + box_vkbps.w - 4);
            }
            draw_text(ren, g_export.mp4_video_kbps_str, box_vkbps.x + 8, r2_y + 7, ed_vk ? c_accent : c_white);
            draw_text(ren, "kbps", ex + 408, r2_y + 7, c_muted);

            SDL_Rect btn_vkdd = { ex + 458, r2_y, 34, 26 };
            SDL_SetRenderDrawColor(ren, 44, 36, 30, 255);
            SDL_RenderFillRect(ren, &btn_vkdd);
            draw_brown_border_rect(ren, &btn_vkdd, g_export.mp4_v_kbps_dropdown_open, false);
            draw_text(ren, "v", btn_vkdd.x + 13, r2_y + 7, c_white);
        }

        // Row 3: Audio: [ AAC ]   Bitrate: [ 192 kbps v ]
        int r3_y = sy_sub + 102;
        draw_text(ren, "Audio:", ex + 34, r3_y + 7, c_white);

        SDL_Rect box_ac_aac = { ex + 96, r3_y, 108, 26 };
        SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
        SDL_RenderFillRect(ren, &box_ac_aac);
        draw_brown_border_rect(ren, &box_ac_aac, true, false);
        draw_text(ren, "AAC", box_ac_aac.x + 42, r3_y + 7, c_white);

        draw_text(ren, "Bitrate:", ex + 218, r3_y + 7, c_white);
        SDL_Rect box_akbps = { ex + 286, r3_y, 206, 26 };
        SDL_SetRenderDrawColor(ren, 34, 30, 26, 255);
        SDL_RenderFillRect(ren, &box_akbps);
        draw_brown_border_rect(ren, &box_akbps, g_export.mp4_aac_dropdown_open, false);
        char aac_rate_lbl[32];
        snprintf(aac_rate_lbl, sizeof(aac_rate_lbl), "%s kbps", g_export.mp4_audio_kbps_str);
        draw_text(ren, aac_rate_lbl, box_akbps.x + 10, r3_y + 7, c_white);
        draw_text(ren, "v", box_akbps.x + box_akbps.w - 16, r3_y + 7, c_muted);
    }

    // ---- 5. OPTION: TWO-PASS RENDER CHECKBOX (Green Right Mark) ----
    int sy_opt = ey + 454;
    SDL_Rect chk_box = { ex + 22, sy_opt + 2, 20, 20 };
    SDL_SetRenderDrawColor(ren, g_export.two_pass_render ? 22 : 20,
                                g_export.two_pass_render ? 44 : 20,
                                g_export.two_pass_render ? 26 : 20, 255);
    SDL_RenderFillRect(ren, &chk_box);
    draw_brown_border_rect(ren, &chk_box, g_export.two_pass_render, false);
    if (g_export.two_pass_render) {
        draw_green_checkmark_icon(ren, chk_box.x, chk_box.y, chk_box.w, chk_box.h);
    }
    draw_text(ren, "Option: Two-pass render", ex + 50, sy_opt + 6, c_white);

    // ---- 6. PROCESSING PROGRESS BAR, PERCENTAGE (%) & PROCESSING TIME ----
    SDL_Rect bar_bg = { ex + 22, ey + 486, ew - 44, 26 };
    SDL_SetRenderDrawColor(ren, 16, 15, 14, 255);
    SDL_RenderFillRect(ren, &bar_bg);

    bool is_done_green = (!g_export.is_rendering && g_export.progress_pct >= 100);
    int fill_w = ((bar_bg.w - 4) * g_export.progress_pct) / 100;
    if (fill_w > 0) {
        SDL_Rect bar_fill = { bar_bg.x + 2, bar_bg.y + 2, fill_w, bar_bg.h - 4 };
        if (is_done_green) {
            SDL_SetRenderDrawColor(ren, 38, 178, 72, 255);
        } else {
            SDL_SetRenderDrawColor(ren, 195, 128, 54, 255);
        }
        SDL_RenderFillRect(ren, &bar_fill);
    }
    if (is_done_green) {
        SDL_SetRenderDrawColor(ren, 62, 224, 98, 255);
        SDL_RenderDrawRect(ren, &bar_bg);
        SDL_Rect inner_g = { bar_bg.x + 1, bar_bg.y + 1, bar_bg.w - 2, bar_bg.h - 2 };
        SDL_RenderDrawRect(ren, &inner_g);
    } else {
        draw_brown_border_rect(ren, &bar_bg, g_export.progress_pct > 0, false);
    }

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
    draw_text(ren, pct_time_buf, bar_bg.x + 12, bar_bg.y + 7, c_white);

    if (g_export.status_msg[0] != 0) {
        SDL_Rect st_clip = { ex + 22, ey + 518, ew - 44, 22 };
        SDL_Color st_col = is_done_green ? (SDL_Color){ 88, 232, 118, 255 } : c_muted;
        draw_text_clipped(ren, g_export.status_msg, ex + 22, ey + 520, st_col, &st_clip, NULL);
    }

    // ---- 7. BOTTOM ACTION BUTTONS ----
    int btn_y = ey + eh - 44;
    SDL_Rect btn_render = { ex + ew - 270, btn_y, 145, 32 };
    SDL_SetRenderDrawColor(ren, 78, 52, 32, 255);
    SDL_RenderFillRect(ren, &btn_render);
    draw_brown_border_rect(ren, &btn_render, true, false);
    draw_text(ren, "Render / Export", btn_render.x + 13, btn_y + 10, c_white);

    SDL_Rect btn_close = { ex + ew - 115, btn_y, 95, 32 };
    SDL_SetRenderDrawColor(ren, 44, 40, 38, 255);
    SDL_RenderFillRect(ren, &btn_close);
    draw_brown_border_rect(ren, &btn_close, false, false);
    draw_text(ren, "Cancel", btn_close.x + 23, btn_y + 10, c_white);

    // ---- 8. DROPDOWN OVERLAYS (Drawn after main controls so they appear on top of everything) ----
    if (g_export.format == EXPORT_FMT_MP4 && g_export.mp4_aac_dropdown_open) {
        int dd_x = ex + 286;
        int dd_y = sy_sub + 102 + 26;
        int dd_w = 206;
        SDL_Rect dd_box = { dd_x, dd_y, dd_w, 80 };
        SDL_SetRenderDrawColor(ren, 24, 22, 20, 255);
        SDL_RenderFillRect(ren, &dd_box);
        draw_brown_border_rect(ren, &dd_box, true, false);

        const char *rates[3] = { "120", "192", "320" };
        const char *rate_labels[3] = { "120 kbps", "192 kbps", "320 kbps" };
        for (int r_i = 0; r_i < 3; r_i++) {
            bool is_cur = (strcmp(g_export.mp4_audio_kbps_str, rates[r_i]) == 0);
            SDL_Rect r_item = { dd_x + 2, dd_y + r_i * 26 + 2, dd_w - 4, 24 };
            SDL_SetRenderDrawColor(ren, is_cur ? 68 : 30, is_cur ? 48 : 28, is_cur ? 32 : 26, 255);
            SDL_RenderFillRect(ren, &r_item);
            draw_brown_border_rect(ren, &r_item, is_cur, !is_cur);
            draw_text(ren, rate_labels[r_i], dd_x + 10, dd_y + r_i * 26 + 7, c_white);
        }
    }

    if (g_export.format == EXPORT_FMT_MP4 && g_export.mp4_v_kbps_dropdown_open) {
        int vdd_x = ex + 286;
        int vdd_y = sy_sub + 58 + 26;
        int vdd_w = 206;
        int vdd_h = (NUM_MP4_V_KBPS_PRESETS + 1) * 26 + 4;
        SDL_Rect vdd_box = { vdd_x, vdd_y, vdd_w, vdd_h };
        SDL_SetRenderDrawColor(ren, 24, 22, 20, 255);
        SDL_RenderFillRect(ren, &vdd_box);
        draw_brown_border_rect(ren, &vdd_box, true, false);

        int cur_vk = get_current_mp4_v_kbps_preset_idx();
        for (int vi = 0; vi <= NUM_MP4_V_KBPS_PRESETS; vi++) {
            bool is_cur = (vi < NUM_MP4_V_KBPS_PRESETS) ? (vi == cur_vk && !g_export.mp4_v_kbps_manual_mode)
                                                        : (g_export.mp4_v_kbps_manual_mode || cur_vk < 0);
            SDL_Rect r_item = { vdd_x + 2, vdd_y + 2 + vi * 26, vdd_w - 4, 24 };
            SDL_SetRenderDrawColor(ren, is_cur ? 68 : 30, is_cur ? 48 : 28, is_cur ? 32 : 26, 255);
            SDL_RenderFillRect(ren, &r_item);
            draw_brown_border_rect(ren, &r_item, is_cur, !is_cur);
            if (vi < NUM_MP4_V_KBPS_PRESETS) {
                char vlbl[32];
                snprintf(vlbl, sizeof(vlbl), "%d kbps", g_mp4_v_kbps_presets[vi]);
                draw_text(ren, vlbl, vdd_x + 10, vdd_y + 8 + vi * 26, c_white);
            } else {
                draw_text(ren, "Manual", vdd_x + 10, vdd_y + 8 + vi * 26, is_cur ? c_accent : c_white);
            }
        }
    }

    if (g_export.format == EXPORT_FMT_MP4 && g_export.mp4_size_dropdown_open) {
        int sdd_x = ex + 266;
        int sdd_y = sy_sub + 14 + 26;
        int sdd_w = 226;
        int sdd_h = (NUM_MP4_RES_PRESETS + 1) * 26 + 4;
        SDL_Rect sdd_box = { sdd_x, sdd_y, sdd_w, sdd_h };
        SDL_SetRenderDrawColor(ren, 24, 22, 20, 255);
        SDL_RenderFillRect(ren, &sdd_box);
        draw_brown_border_rect(ren, &sdd_box, true, false);

        int cur_res = get_current_mp4_res_preset_idx();
        for (int ri = 0; ri <= NUM_MP4_RES_PRESETS; ri++) {
            bool is_cur = (ri < NUM_MP4_RES_PRESETS) ? (ri == cur_res && !g_export.mp4_size_manual_mode)
                                                     : (g_export.mp4_size_manual_mode || cur_res < 0);
            SDL_Rect r_item = { sdd_x + 2, sdd_y + 2 + ri * 26, sdd_w - 4, 24 };
            SDL_SetRenderDrawColor(ren, is_cur ? 68 : 30, is_cur ? 48 : 28, is_cur ? 32 : 26, 255);
            SDL_RenderFillRect(ren, &r_item);
            draw_brown_border_rect(ren, &r_item, is_cur, !is_cur);
            if (ri < NUM_MP4_RES_PRESETS) {
                draw_text(ren, g_mp4_res_presets[ri].label, sdd_x + 10, sdd_y + 8 + ri * 26, c_white);
            } else {
                draw_text(ren, "Manual", sdd_x + 10, sdd_y + 8 + ri * 26, is_cur ? c_accent : c_white);
            }
        }
    }

    // ---- 9. "FILE ALREADY EXISTS" MODAL PROMPT (Replace Existing File / Rename File / Cancel) ----
    if (g_export.overwrite_prompt_open) {
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_Rect dim = { ex, ey, ew, eh };
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 175);
        SDL_RenderFillRect(ren, &dim);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

        int mw = 500, mh = 166;
        int mx_box = ex + (ew - mw) / 2;
        int my_box = ey + (eh - mh) / 2;

        SDL_Rect mbox = { mx_box, my_box, mw, mh };
        SDL_SetRenderDrawColor(ren, 30, 26, 24, 255);
        SDL_RenderFillRect(ren, &mbox);
        draw_brown_border_rect(ren, &mbox, true, false);

        SDL_Rect mhdr = { mx_box, my_box, mw, 28 };
        SDL_SetRenderDrawColor(ren, 52, 38, 28, 255);
        SDL_RenderFillRect(ren, &mhdr);
        draw_brown_border_rect(ren, &mhdr, false, false);
        draw_text(ren, "File Already Exists", mx_box + 14, my_box + 8, c_white);
        draw_text(ren, "X", mx_box + mw - 20, my_box + 8, c_white);

        char msg1[256];
        snprintf(msg1, sizeof(msg1), "\"%s.%s\" already exists in this folder.",
                 g_export.filename[0] ? g_export.filename : "output", get_export_ext());
        SDL_Rect m1_clip = { mx_box + 16, my_box + 40, mw - 32, 20 };
        draw_text_clipped(ren, msg1, mx_box + 16, my_box + 42, c_white, &m1_clip, NULL);
        draw_text(ren, "Would you like to replace the existing file or rename it?", mx_box + 16, my_box + 68, c_muted);

        int mby = my_box + mh - 44;
        // [Replace Existing File]
        SDL_Rect b_rep = { mx_box + 16, mby, 186, 30 };
        SDL_SetRenderDrawColor(ren, 88, 54, 30, 255);
        SDL_RenderFillRect(ren, &b_rep);
        draw_brown_border_rect(ren, &b_rep, true, false);
        draw_text(ren, "Replace Existing File", b_rep.x + 10, mby + 9, c_white);

        // [Rename File]
        SDL_Rect b_ren = { mx_box + 214, mby, 146, 30 };
        SDL_SetRenderDrawColor(ren, 42, 68, 48, 255);
        SDL_RenderFillRect(ren, &b_ren);
        draw_brown_border_rect(ren, &b_ren, true, false);
        draw_text(ren, "Rename File", b_ren.x + 29, mby + 9, c_white);

        // [Cancel]
        SDL_Rect b_can = { mx_box + 372, mby, 112, 30 };
        SDL_SetRenderDrawColor(ren, 46, 42, 38, 255);
        SDL_RenderFillRect(ren, &b_can);
        draw_brown_border_rect(ren, &b_can, false, false);
        draw_text(ren, "Cancel", b_can.x + 32, mby + 9, c_white);
    }
}

// Requirement 10: Commit manual Auto Save cycle interval (minutes) and render Auto Save Settings window
static void commit_autosave_interval_edit(void) {
    int val = atoi(g_autosave.interval_buf);
    if (val < 1) val = 1;
    if (val > 1440) val = 1440;
    g_autosave.interval_minutes = val;
    snprintf(g_autosave.interval_buf, sizeof(g_autosave.interval_buf), "%d", val);
    g_autosave.interval_focused = false;
    SDL_StopTextInput();
    save_keyboard_shortcuts_json();
    snprintf(g_autosave.status_msg, sizeof(g_autosave.status_msg),
             "Auto save cycle set to every %d min (%s)",
             g_autosave.interval_minutes, g_autosave.enabled ? "ON" : "OFF");
}

static void render_auto_save_settings_window(SDL_Renderer *ren) {
    if (!g_autosave.is_open || !ren) return;
    SDL_Rect *wr = &g_autosave.win_rect;
    ensure_popup_fits_text_width(wr, 480, 244, g_autosave.status_msg, 44);
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
    draw_text(ren, "Auto Save Settings", wx + 14, wy + 9, c_white);
    draw_text(ren, "X", wx + ww - 20, wy + 9, c_white);

    // Row 1: Auto Save ON/OFF Tick Box (with green right checkmark)
    int r1_y = wy + 44;
    SDL_Rect row1 = { wx + 18, r1_y, ww - 36, 36 };
    SDL_SetRenderDrawColor(ren, g_autosave.enabled ? 36 : 28, g_autosave.enabled ? 44 : 26, g_autosave.enabled ? 30 : 24, 255);
    SDL_RenderFillRect(ren, &row1);
    draw_brown_border_rect(ren, &row1, g_autosave.enabled, !g_autosave.enabled);

    SDL_Rect chk = { wx + 30, r1_y + 8, 20, 20 };
    SDL_SetRenderDrawColor(ren, g_autosave.enabled ? 22 : 16, g_autosave.enabled ? 46 : 15, g_autosave.enabled ? 26 : 14, 255);
    SDL_RenderFillRect(ren, &chk);
    draw_brown_border_rect(ren, &chk, g_autosave.enabled, false);
    if (g_autosave.enabled) {
        draw_green_checkmark_icon(ren, chk.x, chk.y, chk.w, chk.h);
    }
    draw_text(ren, g_autosave.enabled ? "Project Auto Save: ON" : "Project Auto Save: OFF", wx + 60, r1_y + 11, c_white);

    // Row 2: Save-to-Save Cycle Timing Manual Entry (minutes)
    int r2_y = wy + 94;
    draw_text(ren, "Save-to-Save Cycle:", wx + 20, r2_y + 8, c_white);

    SDL_Rect box_min = { wx + 190, r2_y, 96, 28 };
    SDL_SetRenderDrawColor(ren, 16, 15, 14, 255);
    SDL_RenderFillRect(ren, &box_min);
    draw_brown_border_rect(ren, &box_min, g_autosave.interval_focused, false);
    if (g_autosave.interval_focused) {
        daw_draw_text_edit_overlay(ren, g_autosave.interval_buf, box_min.x + 8, box_min.y, box_min.h, box_min.x + box_min.w - 4);
    }
    draw_text(ren, g_autosave.interval_buf, box_min.x + 8, r2_y + 8, g_autosave.interval_focused ? c_accent : c_white);
    draw_text(ren, "minutes", wx + 296, r2_y + 8, c_muted);

    SDL_Rect btn_apply = { wx + ww - 98, r2_y, 80, 28 };
    SDL_SetRenderDrawColor(ren, 58, 44, 32, 255);
    SDL_RenderFillRect(ren, &btn_apply);
    draw_brown_border_rect(ren, &btn_apply, true, false);
    draw_text(ren, "Apply", btn_apply.x + 20, r2_y + 8, c_white);

    // Row 3: Next Auto Save Countdown & Status Info
    Uint32 now_t = SDL_GetTicks();
    Uint32 cyc_ms = (Uint32)(g_autosave.interval_minutes > 0 ? g_autosave.interval_minutes : 30) * 60000U;
    Uint32 el_ms = (now_t >= g_autosave.last_save_ticks) ? (now_t - g_autosave.last_save_ticks) : 0;
    Uint32 rem_ms = (el_ms < cyc_ms) ? (cyc_ms - el_ms) : 0;
    int rem_min = (int)(rem_ms / 60000U);
    int rem_sec = (int)((rem_ms % 60000U) / 1000U);

    char info_line[128];
    if (!g_autosave.enabled) {
        snprintf(info_line, sizeof(info_line), "Auto save is currently disabled.");
    } else if (g_autosave.pending_autosave) {
        snprintf(info_line, sizeof(info_line), "Auto save ready (waiting for active process to finish)...");
    } else {
        snprintf(info_line, sizeof(info_line), "Next auto save in: %02dm %02ds (Cycle: %d min)",
                 rem_min, rem_sec, g_autosave.interval_minutes);
    }
    draw_text(ren, info_line, wx + 20, wy + 138, c_accent);
    draw_text(ren, "Waits until Export / Import / Dynamic Split finishes.", wx + 20, wy + 162, c_muted);

    // Bottom Buttons: [Save Now] and [OK]
    int by = wy + wh - 42;
    SDL_Rect b_savenow = { wx + 20, by, 118, 28 };
    SDL_SetRenderDrawColor(ren, 48, 64, 46, 255);
    SDL_RenderFillRect(ren, &b_savenow);
    draw_brown_border_rect(ren, &b_savenow, true, false);
    draw_text(ren, "Save Now", b_savenow.x + 25, by + 8, c_white);

    SDL_Rect b_ok = { wx + ww - 102, by, 84, 28 };
    SDL_SetRenderDrawColor(ren, hr + 22, hg + 14, hb + 8, 255);
    SDL_RenderFillRect(ren, &b_ok);
    draw_brown_border_rect(ren, &b_ok, true, false);
    draw_text(ren, "OK", b_ok.x + 34, by + 8, c_white);
}

static bool is_processing_blocking_input(void) {
    return (g_engine_processing || g_processing_popup_open || g_export.is_rendering ||
            g_daw.save_prompt_open || g_track_delete_prompt_open);
}

static bool is_any_popup_or_dropdown_open(void) {
    return is_processing_blocking_input();
}

// Requirement 4: Focus on a popup window ONLY when the mouse cursor is hovering over that popup window!
// When the mouse cursor is outside the popup window, the mouse cursor and clicks/wheel are active on the panel.
// During any kind of processing time, the mouse cursor and clicks do not work on the panel.
static bool is_cursor_captured_by_popup_or_menu(ProjectTab *p, int mx, int my, SDL_Cursor **out_cursor) {
    if (out_cursor) *out_cursor = g_cursor_arrow;

    if (is_processing_blocking_input()) {
        return true;
    }

    if (p && (g_tab_needs_project_setup[get_valid_tab_idx()] || p->project_path[0] == '\0')) return true;
    if (g_lock_popup_open && point_in_rect(mx, my, &g_lock_win_rect)) return true;
    if (g_multi_import.is_open && point_in_rect(mx, my, &g_multi_import.win_rect)) return true;
    if (g_vol_popup.is_open && point_in_rect(mx, my, &g_vol_popup.win_rect)) return true;
    if (g_range_popup.is_open && is_range_active() && point_in_rect(mx, my, &g_range_popup.win_rect)) {
        if (out_cursor) {
            int wx = g_range_popup.win_rect.x, wy = g_range_popup.win_rect.y, ww = g_range_popup.win_rect.w;
            if (mx >= wx + 92 && mx <= wx + ww - 12 &&
                ((my >= wy + 34 && my <= wy + 58) || (my >= wy + 64 && my <= wy + 88))) {
                *out_cursor = g_cursor_ibeam;
            }
        }
        return true;
    }

    if (g_time_jump_open) {
        if (out_cursor) {
            int wx = g_time_jump_rect.x, wy = g_time_jump_rect.y, ww = g_time_jump_rect.w;
            if (mx >= wx + 62 && mx <= wx + ww - 14 && my >= wy + 38 && my <= wy + 64) {
                *out_cursor = g_cursor_ibeam;
            }
        }
        return true;
    }
    if (g_mouse_settings_open && point_in_rect(mx, my, &g_mouse_win_rect)) return true;
    if (g_autosave.is_open && point_in_rect(mx, my, &g_autosave.win_rect)) {
        if (out_cursor) {
            int wx = g_autosave.win_rect.x, wy = g_autosave.win_rect.y;
            if (mx >= wx + 190 && mx <= wx + 286 && my >= wy + 94 && my <= wy + 122) {
                *out_cursor = g_cursor_ibeam;
            }
        }
        return true;
    }
    if (g_dyn_split.is_open && point_in_rect(mx, my, &g_dyn_split.win_rect)) return true;
    if (g_reposition.is_open && point_in_rect(mx, my, &g_reposition.win_rect)) return true;
    if (g_color_picker.is_open && point_in_rect(mx, my, &g_color_picker.win_rect)) return true;
    if (g_clip_props.is_open && (hit_test_video_resize_edge(&g_clip_props.win_rect, mx, my) != VID_RESIZE_NONE || point_in_rect(mx, my, &g_clip_props.win_rect))) {
        VideoResizeEdge cp_edge = hit_test_video_resize_edge(&g_clip_props.win_rect, mx, my);
        if (cp_edge != VID_RESIZE_NONE) {
            if (out_cursor) *out_cursor = (cp_edge == VID_RESIZE_BOTTOM) ? g_cursor_size_ns : g_cursor_size_we;
            return true;
        }
        if (point_in_rect(mx, my, &g_clip_props.win_rect)) return true;
    }
    if (g_script_popup.is_open && point_in_rect(mx, my, &g_script_popup.win_rect)) return true;

    if (g_export.is_open && point_in_rect(mx, my, &g_export.win_rect)) {
        if (out_cursor && !g_export.overwrite_prompt_open) {
            int ex = g_export.win_rect.x, ey = g_export.win_rect.y;
            bool over_text = false;
            if (my >= ey + 98 && my <= ey + 124 && mx >= ex + 130 && mx <= ex + 545) over_text = true;
            else if (my >= ey + 132 && my <= ey + 158 && mx >= ex + 130 && mx <= ex + 545) over_text = true;
            else if (g_export.format == EXPORT_FMT_MP4 && !g_export.mp4_aac_dropdown_open && !g_export.mp4_size_dropdown_open) {
                int sy_sub = ey + 278;
                int r1_y = sy_sub + 8;
                int r2_y = sy_sub + 46;
                if (my >= r1_y && my <= r1_y + 26 && (mx >= ex + 568 && mx <= ex + 648)) {
                    over_text = true;
                } else if (my >= r2_y && my <= r2_y + 26 && (mx >= ex + 260 && mx <= ex + 360)) {
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

    if (p && p->video.is_open && !g_video_pinned_global) {
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
    if (g_daw.menu_file_open && mx >= 8 && mx <= 204 && my >= m_start && my <= m_start + 126) return true;
    if (g_daw.menu_action_open && mx >= 60 && mx <= 258 && my >= m_start && my <= m_start + 108) return true;
    if (g_menu_tools_open && mx >= 126 && mx <= 282 && my >= m_start && my <= m_start + 81) return true;
    if (g_menu_edit_open && mx >= 190 && mx <= 346 && my >= m_start && my <= m_start + 54) return true;
    if (g_menu_settings_open && mx >= 250 && mx <= 434 && my >= m_start && my <= m_start + 108) return true;
    if (g_marker_context_menu_open && mx >= g_marker_context_x && mx <= g_marker_context_x + 128 &&
        my >= g_marker_context_y && my <= g_marker_context_y + 28) return true;

    return false;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return 1;

    SDL_Window *win = SDL_CreateWindow(
        "Audio Track",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    g_main_win = win;
    g_main_ren = ren;
    g_autosave.last_save_ticks = SDL_GetTicks();
    signal(SIGSEGV, daw_fault_signal_handler);
    signal(SIGFPE,  daw_fault_signal_handler);
    signal(SIGILL,  daw_fault_signal_handler);
    signal(SIGABRT, daw_fault_signal_handler);

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

    for (int t_i = 0; t_i < MAX_TABS; t_i++) {
        char def_title[32];
        snprintf(def_title, sizeof(def_title), "Project %d", t_i + 1);
        init_tab_defaults(t_i, def_title);
    }
    memset(g_marker_full_text, 0, sizeof(g_marker_full_text));
    memset(g_editing_marker_full_buf, 0, sizeof(g_editing_marker_full_buf));
    memset(g_item_fade_in, 0, sizeof(g_item_fade_in));
    memset(g_item_fade_out, 0, sizeof(g_item_fade_out));
    memset(g_item_vol_db, 0, sizeof(g_item_vol_db));
    memset(g_item_muted, 0, sizeof(g_item_muted));
    memset(g_item_color_idx, 0, sizeof(g_item_color_idx));
    memset(g_track_color_idx, 0, sizeof(g_track_color_idx));
    memset(g_item_peak_color_idx, 0, sizeof(g_item_peak_color_idx));
    memset(g_track_peak_color_idx, 0, sizeof(g_track_peak_color_idx));
    memset(g_tab_peak_color_idx, 0, sizeof(g_tab_peak_color_idx));
    memset(g_item_group_id, 0, sizeof(g_item_group_id));
    memset(g_track_hidden, 0, sizeof(g_track_hidden));
    memset(g_track_locked, 0, sizeof(g_track_locked));
    memset(g_track_lock_lr_only, 0, sizeof(g_track_lock_lr_only));
    memset(g_track_lock_ud_only, 0, sizeof(g_track_lock_ud_only));
    for (int lt = 0; lt < MAX_TABS; lt++) {
        g_lock_opt_full[lt] = false;
        g_lock_opt_complete_unlock[lt] = false;
        g_lock_opt_prevent_lr[lt] = false;
        g_lock_opt_prevent_ud[lt] = false;
    }
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

    // Clean up any leftover .cache or Temporary Media files from previous sessions on startup
    daw_purge_all_cache_folder();

    set_daw_window_thumbnail_icon(win);
    init_default_recording_folder();
    ensure_app_vol_defaults_initialized();
    for (int st_i = 0; st_i < MAX_TABS; st_i++) {
        init_tab_app_volume_defaults(st_i);
        g_tab_needs_project_setup[st_i] = true;
    }
    init_tab_defaults(0, "Project 1");
    init_keyboard_actions_system();
    reset_tab_undo_history(0);
    g_daw.tabs[0].is_dirty = false;
    record_project_change("Project Initialized - Select Open or Create Project", false);

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
        // Requirement 8: Crash-proof recovery guard — if any unexpected signal/fault occurs, stabilize & show Wait Processing popup
        if (setjmp(g_crash_recovery_jmp) != 0) {
            g_crash_jmp_armed = 0;
            g_fault_recovered_flag = 0;
            stabilize_app_state_and_recover(get_cur_tab(), "Stabilizing application state after unexpected fault, please wait...");
        }
        g_crash_jmp_armed = 1;

        ProjectTab *p = get_cur_tab();
        int win_w, win_h;
        SDL_GetWindowSize(win, &win_w, &win_h);

        int m_lane_h = get_marker_height(p);
        int view_w = win_w - g_daw.tcp_width - SCROLLBAR_SIZE;
        int view_h = win_h - TABBAR_HEIGHT - TOPBAR_HEIGHT - RULER_HEIGHT - m_lane_h - SCROLLBAR_SIZE;
        int max_h_tracks = get_total_tracks_height();

        double full_content_dur = get_complete_project_duration_sec(p);
        p->project_duration_sec = fmax(300.0, full_content_dur * 1.02);
        double total_w_timeline_d = p->project_duration_sec * (double)p->pixels_per_second;
        int total_w_timeline = (total_w_timeline_d > 200000000.0) ? 200000000 : (int)total_w_timeline_d;

        int cur_tab_chk = get_valid_tab_idx();
        if (fabs(p->playhead_seconds - g_last_observed_playhead[cur_tab_chk]) > 1e-5) {
            ensure_playhead_visible_in_viewport(p, view_w);
            g_last_observed_playhead[cur_tab_chk] = p->playhead_seconds;
        }
        if (g_processing_popup_open && g_processing_close_at_ticks > 0 &&
            SDL_GetTicks() >= g_processing_close_at_ticks) {
            g_processing_popup_open = false;
            g_processing_close_at_ticks = 0;
        }

        daw_poll_bg_prepared_media_on_main_thread();

        while (SDL_PollEvent(&e)) {
            p = get_cur_tab();
            if (!(p->pixels_per_second >= 0.002f && p->pixels_per_second <= 10000.0f) || !isfinite(p->pixels_per_second)) {
                p->pixels_per_second = 80.0f;
            }
            if (e.type == SDL_WINDOWEVENT && g_video_ext_win &&
                e.window.windowID == SDL_GetWindowID(g_video_ext_win)) {
                if (e.window.event == SDL_WINDOWEVENT_CLOSE) {
                    g_video_pinned_global = false;
                    destroy_external_pinned_video_window();
                    p->video.is_open = false;
                }
                continue;
            }
            if (e.type == SDL_QUIT) {
                request_exit_application(&running);
            } else if (e.type == SDL_MOUSEWHEEL) {
                int mx, my; SDL_GetMouseState(&mx, &my);
                if (is_processing_blocking_input()) continue;
                if (g_tab_needs_project_setup[get_valid_tab_idx()] || p->project_path[0] == '\0') continue;
                if (g_export.is_open && point_in_rect(mx, my, &g_export.win_rect)) continue;
                if (g_multi_import.is_open && point_in_rect(mx, my, &g_multi_import.win_rect)) continue;
                if (g_range_popup.is_open && is_range_active() && point_in_rect(mx, my, &g_range_popup.win_rect)) continue;
                if (g_vol_popup.is_open && point_in_rect(mx, my, &g_vol_popup.win_rect)) {
                    int list_h = g_vol_popup.win_rect.h - 122;
                    int content_h = p->num_tracks * 32 + 8;
                    int max_sc = (content_h > list_h) ? (content_h - list_h) : 0;
                    g_vol_popup.scroll_y -= e.wheel.y * 32;
                    if (g_vol_popup.scroll_y < 0) g_vol_popup.scroll_y = 0;
                    if (g_vol_popup.scroll_y > max_sc) g_vol_popup.scroll_y = max_sc;
                    continue;
                }
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
                    int rows_h = (g_action_win.win_rect.h - 266) - 24;
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
                if (is_any_popup_or_dropdown_open() || is_cursor_captured_by_popup_or_menu(p, mx, my, NULL)) continue;
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
                    // Mousewheel over timeline: Zoom horizontally all the way out to full project duration (3h, 4h, etc.)
                    double mouse_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                    float min_pps = get_min_zoom_pps(p, view_w);
                    if (e.wheel.y > 0) {
                        p->pixels_per_second *= powf(1.25f, (float)e.wheel.y);
                    } else if (e.wheel.y < 0) {
                        p->pixels_per_second *= powf(0.80f, (float)(-e.wheel.y));
                    }
                    if (p->pixels_per_second < min_pps) p->pixels_per_second = min_pps;
                    if (p->pixels_per_second > 3000.0f) p->pixels_per_second = 3000.0f;
                    if (p->pixels_per_second <= min_pps * 1.02f) {
                        p->scroll_x_sec = 0.0;
                    } else {
                        p->scroll_x_sec = mouse_time - (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                        if (p->scroll_x_sec < 0.0) p->scroll_x_sec = 0.0;
                    }
                }
            } else if (e.type == SDL_TEXTINPUT) {
                if (g_daw.save_prompt_open || g_track_delete_prompt_open || g_processing_popup_open) continue;
                if (g_tab_needs_project_setup[get_valid_tab_idx()] || p->project_path[0] == '\0') continue;
                if (g_range_popup.is_open && is_range_active() && g_range_popup.active_field != 0) {
                    char clean_t[32] = {0};
                    size_t j = 0;
                    for (const char *c = e.text.text; *c && j + 1 < sizeof(clean_t); c++) {
                        if (isdigit((unsigned char)*c) || *c == ':' || *c == '.') clean_t[j++] = *c;
                    }
                    clean_t[j] = '\0';
                    if (clean_t[0]) {
                        char *rbuf = (g_range_popup.active_field == 1) ? g_range_popup.start_buf : g_range_popup.end_buf;
                        daw_text_edit_insert_text(rbuf, 31, clean_t);
                    }
                    continue;
                }
                if (g_time_jump_open) {
                    char clean_t[32] = {0};
                    size_t j = 0;
                    for (const char *c = e.text.text; *c && j + 1 < sizeof(clean_t); c++) {
                        if (isdigit((unsigned char)*c) || *c == ':' || *c == '.') clean_t[j++] = *c;
                    }
                    clean_t[j] = '\0';
                    if (clean_t[0]) daw_text_edit_insert_text(g_time_jump_buf, sizeof(g_time_jump_buf) - 1, clean_t);
                    continue;
                }
                if (g_script_popup.is_open && g_script_popup.focused_ctrl_idx >= 0 &&
                    g_script_popup.focused_ctrl_idx < g_script_popup.num_controls) {
                    ScriptPopupControl *ctrl = &g_script_popup.controls[g_script_popup.focused_ctrl_idx];
                    daw_text_edit_insert_text(ctrl->str_val, sizeof(ctrl->str_val) - 1, e.text.text);
                    ctrl->num_val = atof(ctrl->str_val);
                    continue;
                }
                if (g_autosave.is_open && g_autosave.interval_focused) {
                    char clean_d[16] = {0};
                    size_t jd = 0;
                    for (const char *c = e.text.text; *c && jd + 1 < sizeof(clean_d); c++) {
                        if (isdigit((unsigned char)*c)) clean_d[jd++] = *c;
                    }
                    clean_d[jd] = '\0';
                    if (clean_d[0]) {
                        daw_text_edit_insert_text(g_autosave.interval_buf, sizeof(g_autosave.interval_buf) - 1, clean_d);
                    }
                    continue;
                }
                if (g_reposition.is_open && g_reposition.interval_focused) {
                    daw_text_edit_insert_text(g_reposition.interval_str, sizeof(g_reposition.interval_str) - 1, e.text.text);
                    continue;
                }
                if (g_dyn_split.is_open && g_dyn_split.editing_field_id > 0) {
                    daw_text_edit_insert_text(g_dyn_split.edit_buf, sizeof(g_dyn_split.edit_buf) - 1, e.text.text);
                    continue;
                }
                if (g_daw.script_manager_open && g_script_filter_focused) {
                    daw_text_edit_insert_text(g_script_filter_text, sizeof(g_script_filter_text) - 1, e.text.text);
                    g_script_scroll_y = 0;
                    refresh_filtered_scripts();
                    if (g_script_num_filtered > 0) {
                        g_daw.script_selected_idx = g_script_filtered_indices[0];
                    }
                    continue;
                }
                if (g_theme.is_open && g_theme.code_focused) {
                    daw_text_edit_insert_text(g_theme.code_buf, sizeof(g_theme.code_buf) - 1, e.text.text);
                    if (parse_theme_code_input(g_theme.code_buf)) {
                        save_themes_settings_json();
                    }
                    continue;
                }
                if (g_action_win.is_open && g_action_win.filter_focused &&
                    !g_action_win.capturing_add_shortcut && !g_action_win.capturing_find_shortcut) {
                    daw_text_edit_insert_text(g_action_win.filter_text, sizeof(g_action_win.filter_text) - 1, e.text.text);
                    g_action_win.scroll_y = 0;
                    refresh_filtered_actions();
                    if (g_action_win.num_filtered > 0) {
                        g_action_win.selected_action_idx = g_action_win.filtered_indices[0];
                        g_action_win.selected_shortcut_sub = 0;
                    }
                    continue;
                }
                if (g_export.is_open && g_export.active_field != EXP_FIELD_NONE) {
                    size_t max_len = 0;
                    char *buf = get_active_export_field_buf(&max_len);
                    if (buf) daw_text_edit_insert_text(buf, max_len, e.text.text);
                    continue;
                }
                if (is_any_popup_or_dropdown_open()) continue;
                if (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME) {
                    daw_text_edit_insert_text(g_daw.editing_name_buf, 63, e.text.text);
                } else if (g_daw.input_focus == INPUT_FOCUS_MARKER) {
                    daw_text_edit_insert_text(g_editing_marker_full_buf, MARKER_TEXT_MAX - 1, e.text.text);
                    g_marker_cursor_pos = g_text_edit.cursor_pos;
                    strncpy(g_daw.editing_marker_buf, g_editing_marker_full_buf, 63);
                    g_daw.editing_marker_buf[63] = '\0';
                }
            } else if (e.type == SDL_DROPFILE) {
                if (g_daw.save_prompt_open || g_export.is_open || g_multi_import.is_open ||
                    g_tab_needs_project_setup[get_valid_tab_idx()] || p->project_path[0] == '\0') {
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
                if (g_engine_processing || g_processing_popup_open || g_export.is_rendering) {
                    continue;
                }
                int kmx = 0, kmy = 0;
                SDL_GetMouseState(&kmx, &kmy);
                if (g_lock_popup_open && point_in_rect(kmx, kmy, &g_lock_win_rect)) {
                    if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        g_lock_popup_open = false;
                    }
                    continue;
                }
                SDL_Keymod mod = SDL_GetModState();
                if (g_daw.save_prompt_open) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_save_prompt_open_project_after = false;
                        handle_save_modal_choice(3, &running);
                    }
                    continue;
                }
                if (g_tab_needs_project_setup[get_valid_tab_idx()] || p->project_path[0] == '\0') {
                    if (e.key.keysym.sym == SDLK_ESCAPE && g_daw.num_tabs > 1) {
                        g_tab_needs_project_setup[g_daw.active_tab] = false;
                        request_close_tab(g_daw.active_tab);
                    }
                    continue;
                }
                if (g_range_popup.is_open && is_range_active() &&
                    (g_range_popup.active_field != 0 || point_in_rect(kmx, kmy, &g_range_popup.win_rect))) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        if (g_range_popup.active_field != 0) {
                            g_range_popup.active_field = 0;
                            SDL_StopTextInput();
                            sync_range_popup_buffers_from_tab();
                        } else {
                            g_range_popup.is_open = false;
                        }
                    } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        commit_range_popup_edits();
                    } else if (g_range_popup.active_field != 0) {
                        char *rbuf = (g_range_popup.active_field == 1) ? g_range_popup.start_buf : g_range_popup.end_buf;
                        daw_text_edit_handle_keydown(rbuf, 31, e.key.keysym.sym, mod);
                    }
                    continue;
                }
                if (g_vol_popup.is_open && point_in_rect(kmx, kmy, &g_vol_popup.win_rect)) {
                    if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        g_vol_popup.is_open = false;
                    }
                    continue;
                }
                if (g_time_jump_open) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_time_jump_open = false;
                        SDL_StopTextInput();
                    } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        commit_time_jump_popup(p, view_w);
                    } else {
                        daw_text_edit_handle_keydown(g_time_jump_buf, sizeof(g_time_jump_buf) - 1, e.key.keysym.sym, mod);
                    }
                    continue;
                }
                if (g_mouse_settings_open && point_in_rect(kmx, kmy, &g_mouse_win_rect)) {
                    if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        g_mouse_settings_open = false;
                    }
                    continue;
                }
                if (g_autosave.is_open && (g_autosave.interval_focused || point_in_rect(kmx, kmy, &g_autosave.win_rect))) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        if (g_autosave.interval_focused) {
                            g_autosave.interval_focused = false;
                            snprintf(g_autosave.interval_buf, sizeof(g_autosave.interval_buf), "%d", g_autosave.interval_minutes);
                            SDL_StopTextInput();
                        } else {
                            g_autosave.is_open = false;
                        }
                    } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        if (g_autosave.interval_focused) {
                            commit_autosave_interval_edit();
                        } else {
                            g_autosave.is_open = false;
                        }
                    } else if (g_autosave.interval_focused) {
                        daw_text_edit_handle_keydown(g_autosave.interval_buf, sizeof(g_autosave.interval_buf) - 1, e.key.keysym.sym, mod);
                    }
                    continue;
                }
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
                if (g_script_popup.is_open && (g_script_popup.focused_ctrl_idx >= 0 || point_in_rect(kmx, kmy, &g_script_popup.win_rect))) {
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
                    if (g_script_popup.focused_ctrl_idx >= 0 &&
                        g_script_popup.focused_ctrl_idx < g_script_popup.num_controls) {
                        char *sb = g_script_popup.controls[g_script_popup.focused_ctrl_idx].str_val;
                        daw_text_edit_handle_keydown(sb, sizeof(g_script_popup.controls[0].str_val) - 1, e.key.keysym.sym, mod);
                        g_script_popup.controls[g_script_popup.focused_ctrl_idx].num_val = atof(sb);
                    }
                    continue;
                }
                if (g_reposition.is_open && (g_reposition.interval_focused || point_in_rect(kmx, kmy, &g_reposition.win_rect))) {
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
                    if (g_reposition.interval_focused) {
                        daw_text_edit_handle_keydown(g_reposition.interval_str, sizeof(g_reposition.interval_str) - 1, e.key.keysym.sym, mod);
                    }
                    continue;
                }
                if (g_dyn_split.is_open && (g_dyn_split.editing_field_id > 0 || point_in_rect(kmx, kmy, &g_dyn_split.win_rect))) {
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
                    if (g_dyn_split.editing_field_id > 0) {
                        daw_text_edit_handle_keydown(g_dyn_split.edit_buf, sizeof(g_dyn_split.edit_buf) - 1, e.key.keysym.sym, mod);
                    }
                    continue;
                }
                if (g_daw.script_manager_open && g_script_filter_focused) {
                    if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        g_script_filter_focused = false;
                        SDL_StopTextInput();
                    } else if (daw_text_edit_handle_keydown(g_script_filter_text, sizeof(g_script_filter_text) - 1, e.key.keysym.sym, mod)) {
                        g_script_scroll_y = 0;
                        refresh_filtered_scripts();
                        if (g_script_num_filtered > 0) {
                            g_daw.script_selected_idx = g_script_filtered_indices[0];
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
                    } else {
                        daw_text_edit_handle_keydown(g_theme.code_buf, sizeof(g_theme.code_buf) - 1, e.key.keysym.sym, mod);
                    }
                    continue;
                }
                if (g_multi_import.is_open && point_in_rect(kmx, kmy, &g_multi_import.win_rect)) {
                    if (e.key.keysym.sym == SDLK_RETURN) {
                        execute_multi_media_import(p, ren, win);
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_multi_import.is_open = false;
                        g_multi_import.num_files = 0;
                    }
                    continue;
                }
                if (g_export.is_open && (g_export.overwrite_prompt_open || g_export.active_field != EXP_FIELD_NONE || point_in_rect(kmx, kmy, &g_export.win_rect))) {
                    if (g_export.overwrite_prompt_open) {
                        if (e.key.keysym.sym == SDLK_ESCAPE) {
                            g_export.overwrite_prompt_open = false;
                        } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                            g_export.overwrite_prompt_open = false;
                            execute_unified_export(p, ren);
                        }
                        continue;
                    }
                    if (g_export.active_field != EXP_FIELD_NONE) {
                        size_t max_len = 0;
                        char *buf = get_active_export_field_buf(&max_len);
                        if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER || e.key.keysym.sym == SDLK_ESCAPE) {
                            g_export.active_field = EXP_FIELD_NONE;
                            SDL_StopTextInput();
                        } else if (buf) {
                            daw_text_edit_handle_keydown(buf, max_len, e.key.keysym.sym, mod);
                        }
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                        if (g_export.mp4_size_dropdown_open || g_export.mp4_v_kbps_dropdown_open || g_export.mp4_aac_dropdown_open) {
                            g_export.mp4_size_dropdown_open = false;
                            g_export.mp4_v_kbps_dropdown_open = false;
                            g_export.mp4_aac_dropdown_open = false;
                        } else {
                            g_export.is_open = false;
                        }
                    }
                    continue;
                }
                if (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME) {
                    if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        if (strcmp(p->track_names[g_daw.editing_track_name_idx], g_daw.editing_name_buf) != 0) push_undo_state();
                        strncpy(p->track_names[g_daw.editing_track_name_idx], g_daw.editing_name_buf, 63);
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_track_name_idx = -1;
                        SDL_StopTextInput();
                        record_project_change("Renamed Track", true);
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_track_name_idx = -1;
                        SDL_StopTextInput();
                    } else {
                        daw_text_edit_handle_keydown(g_daw.editing_name_buf, 63, e.key.keysym.sym, mod);
                    }
                    continue;
                }
                if (g_daw.input_focus == INPUT_FOCUS_MARKER) {
                    if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        if (strcmp(get_marker_full_str(p, g_daw.editing_marker_idx), g_editing_marker_full_buf) != 0) push_undo_state();
                        set_marker_full_str(p, g_daw.editing_marker_idx, g_editing_marker_full_buf);
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_marker_idx = -1;
                        SDL_StopTextInput();
                        record_project_change("Edited Marker Text", true);
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_marker_idx = -1;
                        SDL_StopTextInput();
                    } else {
                        daw_text_edit_handle_keydown(g_editing_marker_full_buf, MARKER_TEXT_MAX - 1, e.key.keysym.sym, mod);
                        g_marker_cursor_pos = g_text_edit.cursor_pos;
                        strncpy(g_daw.editing_marker_buf, g_editing_marker_full_buf, 63);
                        g_daw.editing_marker_buf[63] = '\0';
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
                // Requirement 4: Focus keyboard on a popup window ONLY when mouse cursor is hovering over that popup window!
                if (g_color_picker.is_open && point_in_rect(kmx, kmy, &g_color_picker.win_rect)) {
                    if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN) g_color_picker.is_open = false;
                    continue;
                }
                if (g_clip_props.is_open && point_in_rect(kmx, kmy, &g_clip_props.win_rect)) {
                    if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN) g_clip_props.is_open = false;
                    continue;
                }
                if (g_hide_popup_open && point_in_rect(kmx, kmy, &g_hide_win_rect)) {
                    if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN) g_hide_popup_open = false;
                    continue;
                }
                if (g_history_popup_open && point_in_rect(kmx, kmy, &g_history_win_rect)) {
                    if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN) g_history_popup_open = false;
                    continue;
                }
                if (g_theme.is_open && point_in_rect(kmx, kmy, &g_theme.win_rect)) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) g_theme.is_open = false;
                    else if (e.key.keysym.sym == SDLK_DELETE || e.key.keysym.sym == SDLK_BACKSPACE) {
                        delete_saved_theme_at_index(g_theme.selected_saved_idx);
                    }
                    continue;
                }
                if (g_daw.script_manager_open && point_in_rect(kmx, kmy, &g_daw.script_win_rect)) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) g_daw.script_manager_open = false;
                    continue;
                }
                if (g_action_win.is_open && (g_action_win.capturing_add_shortcut || g_action_win.capturing_find_shortcut ||
                    g_action_win.filter_focused || point_in_rect(kmx, kmy, &g_action_win.win_rect))) {
                    handle_action_window_keydown(p, ren, win, e.key.keysym.sym, mod);
                    continue;
                }
                if ((mod & KMOD_CTRL) && !(mod & KMOD_ALT) && !(mod & KMOD_SHIFT) &&
                    (e.key.keysym.sym == SDLK_i || e.key.keysym.sym == SDLK_EQUALS)) {
                    open_clip_properties_window(p, win_w, win_h);
                    continue;
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

                // Requirement 4: When any kind of processing is active, mouse cursor/clicks must not work on panel
                if (g_engine_processing || g_processing_popup_open || g_export.is_rendering) {
                    continue;
                }

                // Handle Save Confirmation Dialog Modal first (highest modal priority for tab close / app Exit)
                if (g_daw.save_prompt_open) {
                    int dia_w = 380, dia_h = 140;
                    int dia_x = (win_w - dia_w) / 2;
                    int dia_y = (win_h - dia_h) / 2;
                    int btn_y = dia_y + dia_h - 44;
                    bool open_proj_after = g_save_prompt_open_project_after;
                    bool quit_after = g_daw.save_prompt_quit_app;

                    // [Save]
                    if (mx >= dia_x + 18 && mx <= dia_x + 118 && my >= btn_y && my <= btn_y + 30) {
                        int t_idx = g_daw.save_prompt_tab_idx;
                        g_daw.save_prompt_open = false;
                        g_daw.save_prompt_tab_idx = -1;
                        g_daw.save_prompt_quit_app = false;
                        g_save_prompt_open_project_after = false;
                        if (t_idx >= 0 && t_idx < g_daw.num_tabs) {
                            daw_save_project_dialog(&g_daw.tabs[t_idx], false);
                            // Proceed with closing tab, exiting app, or opening project only if user completed the save
                            if (!g_daw.tabs[t_idx].is_dirty) {
                                if (open_proj_after) {
                                    daw_load_project_dialog(p, ren, win);
                                } else if (quit_after) {
                                    request_exit_application(&running);
                                } else {
                                    force_close_tab(t_idx);
                                }
                            }
                        }
                        continue;
                    }
                    // [Don't Save]
                    if (mx >= dia_x + 130 && mx <= dia_x + 250 && my >= btn_y && my <= btn_y + 30) {
                        g_save_prompt_open_project_after = false;
                        if (open_proj_after) {
                            daw_cleanup_temp_media_for_tab(g_daw.active_tab, false);
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

                // Requirement 5: New Project Tab 2-Option Modal ("Open Project File" / "Create Project File")
                if (g_tab_needs_project_setup[get_valid_tab_idx()] || p->project_path[0] == '\0') {
                    // Allow clicking top tab bar to switch between open tabs, close a tab, or add '+' tab
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
                                    p = get_cur_tab();
                                } else {
                                    g_daw.active_tab = t;
                                    g_selected_marker_idx = -1;
                                    p = get_cur_tab();
                                }
                                break;
                            }
                            cur_tab_x += cur_tab_w;
                        }
                        if (!clicked_tab && mx >= cur_tab_x && mx <= cur_tab_x + add_btn_w && g_daw.num_tabs < MAX_TABS) {
                            int nt = g_daw.num_tabs;
                            char name[64]; snprintf(name, sizeof(name), "Project %d", nt + 1);
                            init_tab_defaults(nt, name);
                            g_tab_crossfade[nt] = true;
                            g_tab_range_active[nt] = false;
                            g_tab_range_start[nt] = 0.0;
                            g_tab_range_end[nt] = 0.0;
                            memset(g_marker_full_text[nt], 0, sizeof(g_marker_full_text[0]));
                            memset(g_item_fade_in[nt], 0, sizeof(g_item_fade_in[0]));
                            memset(g_item_fade_out[nt], 0, sizeof(g_item_fade_out[0]));
                            memset(g_track_hidden[nt], 0, sizeof(g_track_hidden[0]));
                            memset(g_track_locked[nt], 0, sizeof(g_track_locked[0]));
                            memset(g_track_lock_lr_only[nt], 0, sizeof(g_track_lock_lr_only[0]));
                            memset(g_track_lock_ud_only[nt], 0, sizeof(g_track_lock_ud_only[0]));
                            g_lock_opt_full[nt] = false;
                            g_lock_opt_complete_unlock[nt] = false;
                            g_lock_opt_prevent_lr[nt] = false;
                            g_lock_opt_prevent_ud[nt] = false;
                            memset(g_track_color_idx[nt], 0, sizeof(g_track_color_idx[0]));
                            memset(g_track_peak_color_idx[nt], 0, sizeof(g_track_peak_color_idx[0]));
                            memset(g_item_peak_color_idx[nt], 0, sizeof(g_item_peak_color_idx[0]));
                            g_tab_peak_color_idx[nt] = 0;
                            reset_tab_undo_history(nt);
                            memset(g_tab_history[nt], 0, sizeof(g_tab_history[0]));
                            g_tab_history_count[nt] = 0;
                            g_tab_notification[nt][0] = '\0';
                            init_tab_app_volume_defaults(nt);
                            g_tab_needs_project_setup[nt] = true;
                            g_daw.active_tab = g_daw.num_tabs++;
                            g_selected_marker_idx = -1;
                            record_project_change("New Project Tab Opened", false);
                        }
                        continue;
                    }

                    int mw = 620, mh = 210;
                    int mx_box = (win_w - mw) / 2;
                    int my_box = (win_h - mh) / 2;
                    // Optional [X] close on extra tab (when num_tabs > 1)
                    if (g_daw.num_tabs > 1 && my >= my_box && my <= my_box + 30 && mx >= mx_box + mw - 30 && mx <= mx_box + mw) {
                        g_tab_needs_project_setup[g_daw.active_tab] = false;
                        request_close_tab(g_daw.active_tab);
                        continue;
                    }
                    int btn_y = my_box + 118;
                    // Option 1: [ Open Project File ]
                    if (mx >= mx_box + 56 && mx <= mx_box + 296 && my >= btn_y && my <= btn_y + 42) {
                        daw_load_project_dialog(p, ren, win);
                        if (p->project_path[0] != '\0') {
                            g_tab_needs_project_setup[get_valid_tab_idx()] = false;
                        }
                        continue;
                    }
                    // Option 2: [ Create Project File ]
                    if (mx >= mx_box + 324 && mx <= mx_box + 564 && my >= btn_y && my <= btn_y + 42) {
                        daw_create_new_project_file_dialog(p);
                        continue;
                    }
                    continue;
                }

                // Requirement 2: Tiny Range Popup Window near Range Button
                if (g_range_popup.is_open && is_range_active() && point_in_rect(mx, my, &g_range_popup.win_rect)) {
                    SDL_Rect *wr = &g_range_popup.win_rect;
                    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
                    if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + 26) {
                        if (mx >= wx + ww - 24) {
                            // Close popup window only; keep yellow Range active on screen until 2nd click on Range button
                            if (g_range_popup.active_field != 0) {
                                commit_range_popup_edits();
                            }
                            g_range_popup.is_open = false;
                            g_range_popup.active_field = 0;
                            SDL_StopTextInput();
                        } else {
                            g_range_popup.is_dragging = true;
                            g_range_popup.drag_off_x = mx - wx;
                            g_range_popup.drag_off_y = my - wy;
                        }
                        continue;
                    }
                    if (mx >= wx && mx <= wx + ww && my >= wy + 26 && my <= wy + wh) {
                        int r1_y = wy + 34;
                        int r2_y = wy + 64;
                        int r3_y = wy + 96;
                        if (my >= r1_y && my <= r1_y + 24 && mx >= wx + 92 && mx <= wx + ww - 12) {
                            if (g_range_popup.active_field == 2) commit_range_popup_edits();
                            g_range_popup.active_field = 1;
                            daw_text_edit_click_cursor(g_range_popup.start_buf, mx, wx + 100);
                            SDL_StartTextInput();
                        } else if (my >= r2_y && my <= r2_y + 24 && mx >= wx + 92 && mx <= wx + ww - 12) {
                            if (g_range_popup.active_field == 1) commit_range_popup_edits();
                            g_range_popup.active_field = 2;
                            daw_text_edit_click_cursor(g_range_popup.end_buf, mx, wx + 100);
                            SDL_StartTextInput();
                        } else if (my >= r3_y && my <= r3_y + 24 && mx >= wx + ww - 68 && mx <= wx + ww - 12) {
                            commit_range_popup_edits();
                        } else {
                            if (g_range_popup.active_field != 0) {
                                commit_range_popup_edits();
                            }
                        }
                        continue;
                    }
                    continue;
                } else if (g_range_popup.active_field != 0) {
                    commit_range_popup_edits();
                }

                // Requirement 4: App Hearing Volume Popup Window
                if (g_vol_popup.is_open && point_in_rect(mx, my, &g_vol_popup.win_rect)) {
                    ensure_app_vol_defaults_initialized();
                    SDL_Rect *wr = &g_vol_popup.win_rect;
                    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
                    int vtab = get_valid_tab_idx();
                    if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + 28) {
                        if (mx >= wx + ww - 26) {
                            g_vol_popup.is_open = false;
                            g_vol_popup.dragging_slider_idx = -1;
                        } else {
                            g_vol_popup.is_dragging = true;
                            g_vol_popup.drag_off_x = mx - wx;
                            g_vol_popup.drag_off_y = my - wy;
                        }
                        continue;
                    }
                    int m_y = wy + 36;
                    int sl_x = wx + 156;
                    int sl_w = 168;
                    // Master Volume slider or [Reset]
                    if (my >= m_y && my <= m_y + 38) {
                        if (mx >= wx + ww - 76 && mx <= wx + ww - 20) {
                            g_app_master_vol[vtab] = 100.0f;
                            p->is_dirty = true;
                            record_project_change("Reset App Master Hearing Volume to 100%", true);
                        } else if (mx >= sl_x - 6 && mx <= sl_x + sl_w + 6) {
                            g_vol_popup.dragging_slider_idx = 0;
                            float u = (float)(mx - sl_x) / (float)sl_w;
                            if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
                            g_app_master_vol[vtab] = floorf(u * 100.0f + 0.5f);
                            p->is_dirty = true;
                        }
                        continue;
                    }
                    // Track list sliders, [Reset] buttons, or scrollbar
                    int list_y = wy + 82;
                    int list_h = wh - 122;
                    int sb_x = wx + ww - 26;
                    if (my >= list_y && my <= list_y + list_h) {
                        if (mx >= sb_x && mx <= wx + ww - 12) {
                            g_vol_popup.sb_dragging = true;
                            g_vol_popup.sb_start_my = my;
                            g_vol_popup.sb_orig_scroll = g_vol_popup.scroll_y;
                            continue;
                        }
                        int r = (my - (list_y + 4) + g_vol_popup.scroll_y) / 32;
                        if (r >= 0 && r < p->num_tracks) {
                            if (mx >= wx + ww - 84 && mx <= wx + ww - 32) {
                                g_app_track_vol[vtab][r] = 100.0f;
                                p->is_dirty = true;
                                record_project_change("Reset Track App Hearing Volume to 100%", true);
                            } else if (mx >= sl_x - 6 && mx <= sl_x + sl_w + 6) {
                                g_vol_popup.dragging_slider_idx = r + 1;
                                float u = (float)(mx - sl_x) / (float)sl_w;
                                if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
                                g_app_track_vol[vtab][r] = floorf(u * 100.0f + 0.5f);
                                p->is_dirty = true;
                            }
                        }
                        continue;
                    }
                    continue;
                }

                // Main Lock Two-Section Popup Window (All Track | Selected track)
                if (g_lock_popup_open && point_in_rect(mx, my, &g_lock_win_rect)) {
                    SDL_Rect *wr = &g_lock_win_rect;
                    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
                    int ltab = get_valid_tab_idx();
                    if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + 30) {
                        if (mx >= wx + ww - 28) {
                            g_lock_popup_open = false;
                        } else {
                            g_lock_win_dragging = true;
                            g_lock_drag_off_x = mx - wx;
                            g_lock_drag_off_y = my - wy;
                        }
                        continue;
                    }
                    int col_w = (ww - 44) / 2;
                    int left_x = wx + 14;
                    int right_x = wx + 30 + col_w;
                    int box_y = wy + 40;

                    // Left Section clicks: All Track (3 tick boxes)
                    if (mx >= left_x + 8 && mx <= left_x + col_w - 8) {
                        if (my >= box_y + 30 && my <= box_y + 60) {
                            // 1. All Track Complete Lock
                            push_undo_state();
                            g_lock_opt_full[ltab] = !g_lock_opt_full[ltab];
                            if (g_lock_opt_full[ltab]) {
                                g_lock_opt_complete_unlock[ltab] = false;
                                g_lock_opt_prevent_lr[ltab] = false;
                                g_lock_opt_prevent_ud[ltab] = false;
                                for (int t = 0; t < p->num_tracks; t++) {
                                    g_track_locked[ltab][t] = true;
                                    g_track_lock_lr_only[ltab][t] = false;
                                    g_track_lock_ud_only[ltab][t] = false;
                                }
                                record_project_change("All Track Complete Lock: ON", true);
                            } else {
                                for (int t = 0; t < p->num_tracks; t++) {
                                    g_track_locked[ltab][t] = false;
                                    g_track_lock_lr_only[ltab][t] = false;
                                    g_track_lock_ud_only[ltab][t] = false;
                                }
                                record_project_change("All Track Complete Lock: OFF", true);
                            }
                        } else if (my >= box_y + 66 && my <= box_y + 96) {
                            // 2. Lock only clip item Left Right move (All Track)
                            push_undo_state();
                            g_lock_opt_prevent_lr[ltab] = !g_lock_opt_prevent_lr[ltab];
                            g_lock_opt_full[ltab] = false;
                            g_lock_opt_complete_unlock[ltab] = false;
                            for (int t = 0; t < p->num_tracks; t++) {
                                g_track_lock_lr_only[ltab][t] = g_lock_opt_prevent_lr[ltab];
                                g_track_lock_ud_only[ltab][t] = g_lock_opt_prevent_ud[ltab];
                                g_track_locked[ltab][t] = (g_track_lock_lr_only[ltab][t] || g_track_lock_ud_only[ltab][t]);
                            }
                            record_project_change("All Track: Lock Left/Right Clip Move", true);
                        } else if (my >= box_y + 102 && my <= box_y + 132) {
                            // 3. Lock only clip item Up Down move (All Track)
                            push_undo_state();
                            g_lock_opt_prevent_ud[ltab] = !g_lock_opt_prevent_ud[ltab];
                            g_lock_opt_full[ltab] = false;
                            g_lock_opt_complete_unlock[ltab] = false;
                            for (int t = 0; t < p->num_tracks; t++) {
                                g_track_lock_lr_only[ltab][t] = g_lock_opt_prevent_lr[ltab];
                                g_track_lock_ud_only[ltab][t] = g_lock_opt_prevent_ud[ltab];
                                g_track_locked[ltab][t] = (g_track_lock_lr_only[ltab][t] || g_track_lock_ud_only[ltab][t]);
                            }
                            record_project_change("All Track: Lock Up/Down Clip Move", true);
                        }
                    }

                    // Right Section clicks: Selected Track (3 tick boxes)
                    if (mx >= right_x + 8 && mx <= right_x + col_w - 8) {
                        bool any_sel_t = false;
                        int first_sel_t = -1;
                        for (int t = 0; t < p->num_tracks; t++) {
                            if (p->track_selected[t]) {
                                any_sel_t = true;
                                if (first_sel_t < 0) first_sel_t = t;
                            }
                        }
                        if (!any_sel_t && p->num_tracks > 0) {
                            int fb_t = (g_last_clicked_track_idx >= 0 && g_last_clicked_track_idx < p->num_tracks) ? g_last_clicked_track_idx : 0;
                            p->track_selected[fb_t] = true;
                            first_sel_t = fb_t;
                        }
                        if (my >= box_y + 30 && my <= box_y + 60) {
                            // 1. All Clips Complete Lock (Selected Track)
                            push_undo_state();
                            bool cur_full = (first_sel_t >= 0) ? is_track_full_locked_tab(ltab, first_sel_t) : false;
                            bool new_full = !cur_full;
                            for (int t = 0; t < p->num_tracks; t++) {
                                if (!p->track_selected[t]) continue;
                                if (new_full) {
                                    g_track_locked[ltab][t] = true;
                                    g_track_lock_lr_only[ltab][t] = false;
                                    g_track_lock_ud_only[ltab][t] = false;
                                } else {
                                    g_track_locked[ltab][t] = false;
                                    g_track_lock_lr_only[ltab][t] = false;
                                    g_track_lock_ud_only[ltab][t] = false;
                                    g_lock_opt_full[ltab] = false;
                                }
                            }
                            record_project_change(new_full ? "Selected Track: All Clips Complete Lock ON" : "Selected Track: All Clips Complete Lock OFF", true);
                        } else if (my >= box_y + 66 && my <= box_y + 96) {
                            // 2. Lock clip Left Right move (Selected Track)
                            push_undo_state();
                            for (int t = 0; t < p->num_tracks; t++) {
                                if (!p->track_selected[t]) continue;
                                g_track_lock_lr_only[ltab][t] = !g_track_lock_lr_only[ltab][t];
                                g_track_locked[ltab][t] = (g_track_lock_lr_only[ltab][t] || g_track_lock_ud_only[ltab][t]);
                            }
                            record_project_change("Selected Track: Lock clip Left Right move", true);
                        } else if (my >= box_y + 102 && my <= box_y + 132) {
                            // 3. Lock clip Up Down move (Selected Track)
                            push_undo_state();
                            for (int t = 0; t < p->num_tracks; t++) {
                                if (!p->track_selected[t]) continue;
                                g_track_lock_ud_only[ltab][t] = !g_track_lock_ud_only[ltab][t];
                                g_track_locked[ltab][t] = (g_track_lock_lr_only[ltab][t] || g_track_lock_ud_only[ltab][t]);
                            }
                            record_project_change("Selected Track: Lock clip Up Down move", true);
                        }
                    }

                    int by = wy + wh - 38;
                    if (my >= by && my <= by + 26 && mx >= wx + ww - 98 && mx <= wx + ww - 16) {
                        g_lock_popup_open = false;
                    }
                    continue;
                }

                // Handle clicks inside the globally pinned external Video Monitor window
                if (g_video_ext_win && e.button.windowID == SDL_GetWindowID(g_video_ext_win)) {
                    int ew = 480, eh = 294;
                    SDL_GetWindowSize(g_video_ext_win, &ew, &eh);
                    if (my <= 24) {
                        if (mx >= ew - 24) {
                            g_video_pinned_global = false;
                            destroy_external_pinned_video_window();
                            p->video.is_open = false;
                        } else if (mx >= ew - 48 && mx < ew - 24) {
                            g_video_ext_maximized = !g_video_ext_maximized;
                            if (g_video_ext_maximized) SDL_MaximizeWindow(g_video_ext_win);
                            else SDL_RestoreWindow(g_video_ext_win);
                        } else if (mx >= ew - 72 && mx < ew - 48) {
                            toggle_video_global_pin_state(p, win);
                        } else if (e.button.clicks >= 2) {
                            g_video_ext_maximized = !g_video_ext_maximized;
                            if (g_video_ext_maximized) SDL_MaximizeWindow(g_video_ext_win);
                            else SDL_RestoreWindow(g_video_ext_win);
                        } else if (!g_video_ext_maximized) {
                            int gmx = 0, gmy = 0, wx_p = 0, wy_p = 0;
                            SDL_GetGlobalMouseState(&gmx, &gmy);
                            SDL_GetWindowPosition(g_video_ext_win, &wx_p, &wy_p);
                            g_video_ext_dragging = true;
                            g_video_ext_drag_off_x = gmx - wx_p;
                            g_video_ext_drag_off_y = gmy - wy_p;
                        }
                    }
                    continue;
                }

                // Tiny Time Clock Manual Entry Popup (Requirement 14)
                if (g_time_jump_open && point_in_rect(mx, my, &g_time_jump_rect)) {
                    SDL_Rect *wr = &g_time_jump_rect;
                    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
                    if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + 26) {
                        if (mx >= wx + ww - 24) {
                            g_time_jump_open = false;
                            SDL_StopTextInput();
                        } else {
                            g_time_jump_dragging = true;
                            g_time_jump_drag_off_x = mx - wx;
                            g_time_jump_drag_off_y = my - wy;
                        }
                        continue;
                    }
                    if (mx >= wx && mx <= wx + ww && my >= wy + 26 && my <= wy + wh) {
                        int by = wy + wh - 36;
                        if (my >= by && my <= by + 26) {
                            if (mx >= wx + ww - 166 && mx <= wx + ww - 94) {
                                commit_time_jump_popup(p, view_w);
                            } else if (mx >= wx + ww - 86 && mx <= wx + ww - 14) {
                                g_time_jump_open = false;
                                SDL_StopTextInput();
                            }
                        } else {
                            daw_text_edit_click_cursor(g_time_jump_buf, mx, wx + 70);
                            SDL_StartTextInput();
                        }
                        continue;
                    }
                    continue;
                }

                // Settings -> Mouse Popup Window (Requirement 9)
                if (g_mouse_settings_open && point_in_rect(mx, my, &g_mouse_win_rect)) {
                    SDL_Rect *wr = &g_mouse_win_rect;
                    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
                    if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + 30) {
                        if (mx >= wx + ww - 28) {
                            g_mouse_settings_open = false;
                        } else {
                            g_mouse_win_dragging = true;
                            g_mouse_drag_off_x = mx - wx;
                            g_mouse_drag_off_y = my - wy;
                        }
                        continue;
                    }
                    if (mx >= wx && mx <= wx + ww && my >= wy + 30 && my <= wy + wh) {
                        if (my >= wy + 88 && my <= wy + 120 && mx >= wx + 18 && mx <= wx + ww - 18) {
                            g_mouse_click_empty_moves_playhead = !g_mouse_click_empty_moves_playhead;
                            save_themes_settings_json();
                            record_project_change(g_mouse_click_empty_moves_playhead
                                ? "Mouse: Click on empty space moves playhead [ON]"
                                : "Mouse: Click on empty space moves playhead [OFF]", false);
                        } else if (my >= wy + 130 && my <= wy + 162 && mx >= wx + 18 && mx <= wx + ww - 18) {
                            g_mouse_click_universal_moves_playhead = !g_mouse_click_universal_moves_playhead;
                            save_themes_settings_json();
                            record_project_change(g_mouse_click_universal_moves_playhead
                                ? "Mouse: Click universal moves playhead [ON]"
                                : "Mouse: Click universal moves playhead [OFF]", false);
                        }
                        int by = wy + wh - 40;
                        if (my >= by && my <= by + 28 && mx >= wx + ww - 102 && mx <= wx + ww - 18) {
                            g_mouse_settings_open = false;
                        }
                        continue;
                    }
                    continue;
                }

                // Settings -> Auto save Popup Window (Requirement 10)
                if (g_autosave.is_open && point_in_rect(mx, my, &g_autosave.win_rect)) {
                    SDL_Rect *wr = &g_autosave.win_rect;
                    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
                    if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + 30) {
                        if (mx >= wx + ww - 28) {
                            if (g_autosave.interval_focused) commit_autosave_interval_edit();
                            g_autosave.is_open = false;
                        } else {
                            g_autosave.is_dragging = true;
                            g_autosave.drag_off_x = mx - wx;
                            g_autosave.drag_off_y = my - wy;
                        }
                        continue;
                    }
                    if (mx >= wx && mx <= wx + ww && my >= wy + 30 && my <= wy + wh) {
                        int r1_y = wy + 44;
                        int r2_y = wy + 94;
                        int by   = wy + wh - 42;
                        if (my >= r1_y && my <= r1_y + 36 && mx >= wx + 18 && mx <= wx + ww - 18) {
                            if (g_autosave.interval_focused) commit_autosave_interval_edit();
                            g_autosave.enabled = !g_autosave.enabled;
                            if (g_autosave.enabled) {
                                g_autosave.last_save_ticks = SDL_GetTicks();
                                g_autosave.pending_autosave = false;
                            }
                            save_keyboard_shortcuts_json();
                            record_project_change(g_autosave.enabled ? "Auto Save: ON" : "Auto Save: OFF", false);
                        } else if (my >= r2_y && my <= r2_y + 28 && mx >= wx + 190 && mx <= wx + 286) {
                            g_autosave.interval_focused = true;
                            daw_text_edit_click_cursor(g_autosave.interval_buf, mx, wx + 198);
                            SDL_StartTextInput();
                        } else if (my >= r2_y && my <= r2_y + 28 && mx >= wx + ww - 98 && mx <= wx + ww - 18) {
                            commit_autosave_interval_edit();
                        } else if (my >= by && my <= by + 28 && mx >= wx + 20 && mx <= wx + 138) {
                            if (g_autosave.interval_focused) commit_autosave_interval_edit();
                            daw_save_project_dialog(p, false);
                        } else if (my >= by && my <= by + 28 && mx >= wx + ww - 102 && mx <= wx + ww - 18) {
                            if (g_autosave.interval_focused) commit_autosave_interval_edit();
                            g_autosave.is_open = false;
                        } else if (g_autosave.interval_focused) {
                            commit_autosave_interval_edit();
                        }
                        continue;
                    }
                    continue;
                } else if (g_autosave.interval_focused) {
                    commit_autosave_interval_edit();
                }

                if (g_track_delete_prompt_open) {
                    int dia_w = 430, dia_h = 140;
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



                // ================= LUA & PYTHON SCRIPT POPUP WINDOW INTERACTIONS =================
                if (g_script_popup.is_open && point_in_rect(mx, my, &g_script_popup.win_rect)) {
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
                                    daw_text_edit_click_cursor(ctrl->str_val, mx, wx + 188);
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

                // ================= TRACK COLOR & CLIP COLOR PICKER POPUP INTERACTIONS =================
                if (g_color_picker.is_open && point_in_rect(mx, my, &g_color_picker.win_rect)) {
                    SDL_Rect *wr = &g_color_picker.win_rect;
                    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
                    if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + 30) {
                        if (mx >= wx + ww - 28) {
                            g_color_picker.is_open = false;
                        } else {
                            g_color_picker.is_dragging = true;
                            g_color_picker.drag_off_x = mx - wx;
                            g_color_picker.drag_off_y = my - wy;
                        }
                        continue;
                    }
                    if (mx >= wx && mx <= wx + ww && my >= wy + 30 && my <= wy + wh) {
                        if (my >= wy + 38 && my <= wy + 64) {
                            if (mx >= wx + 16 && mx <= wx + 148) g_color_picker.target_mode = 0;
                            else if (mx >= wx + 154 && mx <= wx + 286) g_color_picker.target_mode = 1;
                            else if (mx >= wx + 292 && mx <= wx + 424) g_color_picker.target_mode = 2;
                            continue;
                        }
                        int grid_x = wx + 16, grid_y = wy + 74;
                        int cell_w = 98, cell_h = 38, gap_x = 6, gap_y = 6;
                        for (int c = 0; c < NUM_STUDIO_COLORS; c++) {
                            int col = c % 4, row = c / 4;
                            int cx = grid_x + col * (cell_w + gap_x);
                            int cy = grid_y + row * (cell_h + gap_y);
                            if (mx >= cx && mx <= cx + cell_w && my >= cy && my <= cy + cell_h) {
                                apply_studio_color_choice(p, c);
                                break;
                            }
                        }
                        int by = wy + wh - 36;
                        if (my >= by && my <= by + 26 && mx >= wx + ww - 104 && mx <= wx + ww - 16) {
                            g_color_picker.is_open = false;
                        }
                        continue;
                    }
                }

                // ================= SELECTED CLIP PROPERTY DETAILS WINDOW (CTRL+I, EXPANDABLE) INTERACTIONS =================
                if (g_clip_props.is_open && (hit_test_video_resize_edge(&g_clip_props.win_rect, mx, my) != VID_RESIZE_NONE || point_in_rect(mx, my, &g_clip_props.win_rect))) {
                    SDL_Rect *wr = &g_clip_props.win_rect;
                    int wx = wr->x, wy = wr->y, ww = wr->w, wh = wr->h;
                    VideoResizeEdge cp_edge = hit_test_video_resize_edge(wr, mx, my);
                    if (cp_edge != VID_RESIZE_NONE) {
                        g_clip_props.resize_edge = (int)cp_edge;
                        g_clip_props.resize_start_mx = mx;
                        g_clip_props.resize_start_my = my;
                        g_clip_props.resize_orig_rect = *wr;
                        continue;
                    }
                    if (mx >= wx && mx <= wx + ww && my >= wy && my <= wy + 30) {
                        if (mx >= wx + ww - 28) {
                            g_clip_props.is_open = false;
                        } else if (mx >= wx + ww - 56 && mx < wx + ww - 28) {
                            // [+] / [-] Expand / Restore window width & height
                            if (!g_clip_props.is_expanded) {
                                g_clip_props.saved_norm_rect = *wr;
                                g_clip_props.is_expanded = true;
                                int nw = (win_w - 60 > 960) ? 960 : (win_w - 40);
                                int nh = (win_h - 60 > 520) ? 520 : (win_h - 40);
                                if (nw < 650) nw = 650;
                                if (nh < 448) nh = 448;
                                wr->w = nw;
                                wr->h = nh;
                                wr->x = (win_w - nw) / 2;
                                wr->y = (win_h - nh) / 2;
                            } else {
                                g_clip_props.is_expanded = false;
                                if (g_clip_props.saved_norm_rect.w >= 520) {
                                    *wr = g_clip_props.saved_norm_rect;
                                } else {
                                    *wr = (SDL_Rect){ (win_w - 650) / 2, (win_h - 448) / 2, 650, 448 };
                                }
                            }
                        } else if (e.button.clicks >= 2) {
                            g_clip_props.is_expanded = !g_clip_props.is_expanded;
                            if (g_clip_props.is_expanded) {
                                g_clip_props.saved_norm_rect = *wr;
                                wr->w = (win_w - 60 > 960) ? 960 : (win_w - 40);
                                wr->h = (win_h - 60 > 520) ? 520 : (win_h - 40);
                                wr->x = (win_w - wr->w) / 2;
                                wr->y = (win_h - wr->h) / 2;
                            } else if (g_clip_props.saved_norm_rect.w >= 520) {
                                *wr = g_clip_props.saved_norm_rect;
                            }
                        } else {
                            g_clip_props.is_dragging = true;
                            g_clip_props.drag_off_x = mx - wx;
                            g_clip_props.drag_off_y = my - wy;
                        }
                        continue;
                    }
                    if (mx >= wx && mx <= wx + ww && my >= wy + 30 && my <= wy + wh) {
                        continue;
                    }
                    continue;
                }

                // ================= REPOSITION ITEMS POPUP INTERACTIONS =================
                if (g_reposition.is_open && point_in_rect(mx, my, &g_reposition.win_rect)) {
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
                            daw_text_edit_click_cursor(g_reposition.interval_str, mx, wx + 140);
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
                if (g_dyn_split.is_open && point_in_rect(mx, my, &g_dyn_split.win_rect)) {
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
                if (g_export.is_open && point_in_rect(mx, my, &g_export.win_rect)) {
                    SDL_Rect *er = &g_export.win_rect;
                    int ex = er->x, ey = er->y, ew = er->w, eh = er->h;

                    // Handle "File Already Exists" modal prompt first if open
                    if (g_export.overwrite_prompt_open) {
                        int mw = 500, mh = 166;
                        int mx_box = ex + (ew - mw) / 2;
                        int my_box = ey + (eh - mh) / 2;
                        int mby = my_box + mh - 44;

                        // Close [X] on modal header
                        if (my >= my_box && my <= my_box + 28 && mx >= mx_box + mw - 28 && mx <= mx_box + mw) {
                            g_export.overwrite_prompt_open = false;
                            continue;
                        }
                        if (my >= mby && my <= mby + 30) {
                            if (mx >= mx_box + 16 && mx <= mx_box + 202) {
                                // [Replace Existing File]
                                g_export.overwrite_prompt_open = false;
                                save_themes_settings_json();
                                execute_unified_export(p, ren);
                            } else if (mx >= mx_box + 214 && mx <= mx_box + 360) {
                                // [Rename File] -> Suggest non-conflicting name and focus File Name input box
                                g_export.overwrite_prompt_open = false;
                                suggest_renamed_export_filename();
                                g_export.active_field = EXP_FIELD_FILENAME;
                                daw_text_edit_focus_buf(g_export.filename, true);
                                SDL_StartTextInput();
                            } else if (mx >= mx_box + 372 && mx <= mx_box + 484) {
                                // [Cancel]
                                g_export.overwrite_prompt_open = false;
                            }
                        }
                        continue;
                    }

                    // Stop any previous text input unless a field is clicked below
                    g_export.active_field = EXP_FIELD_NONE;
                    SDL_StopTextInput();

                    // Title bar drag or [X] close
                    if (mx >= ex && mx <= ex + ew && my >= ey && my <= ey + 30) {
                        if (mx >= ex + ew - 34) {
                            g_export.mp4_aac_dropdown_open = false;
                            g_export.mp4_size_dropdown_open = false;
                            g_export.mp4_v_kbps_dropdown_open = false;
                            g_export.overwrite_prompt_open = false;
                            g_export.is_open = false;
                            save_themes_settings_json();
                        } else {
                            g_export.is_dragging = true;
                            g_export.drag_off_x = mx - ex;
                            g_export.drag_off_y = my - ey;
                        }
                        continue;
                    }

                    if (mx >= ex && mx <= ex + ew && my >= ey + 30 && my <= ey + eh) {
                        int sy_sub = ey + 274;
                        int r1_y = sy_sub + 14;
                        int r2_y = sy_sub + 58;
                        int r3_y = sy_sub + 102;

                        // 1. Check open MP4 Size Resolution Dropdown Menu first (6 presets + Manual at end)
                        if (g_export.format == EXPORT_FMT_MP4 && g_export.mp4_size_dropdown_open) {
                            int sdd_x = ex + 266;
                            int sdd_y = r1_y + 26;
                            int sdd_w = 226;
                            int sdd_h = (NUM_MP4_RES_PRESETS + 1) * 26 + 4;
                            if (mx >= sdd_x && mx <= sdd_x + sdd_w && my >= sdd_y && my <= sdd_y + sdd_h) {
                                int sel_idx = (my - (sdd_y + 2)) / 26;
                                if (sel_idx >= 0 && sel_idx < NUM_MP4_RES_PRESETS) {
                                    apply_mp4_res_preset(sel_idx);
                                } else if (sel_idx == NUM_MP4_RES_PRESETS) {
                                    // Selected "Manual" at end of Size dropdown
                                    g_export.mp4_size_manual_mode = true;
                                    g_export.active_field = EXP_FIELD_MP4_WIDTH;
                                    daw_text_edit_focus_buf(g_export.mp4_width_str, true);
                                    SDL_StartTextInput();
                                }
                                g_export.mp4_size_dropdown_open = false;
                                continue;
                            }
                            g_export.mp4_size_dropdown_open = false;
                        }

                        // 2. Check open MP4 Video Bitrate Dropdown Menu (6 presets + Manual at end)
                        if (g_export.format == EXPORT_FMT_MP4 && g_export.mp4_v_kbps_dropdown_open) {
                            int vdd_x = ex + 286;
                            int vdd_y = r2_y + 26;
                            int vdd_w = 206;
                            int vdd_h = (NUM_MP4_V_KBPS_PRESETS + 1) * 26 + 4;
                            if (mx >= vdd_x && mx <= vdd_x + vdd_w && my >= vdd_y && my <= vdd_y + vdd_h) {
                                int sel_idx = (my - (vdd_y + 2)) / 26;
                                if (sel_idx >= 0 && sel_idx < NUM_MP4_V_KBPS_PRESETS) {
                                    g_export.mp4_v_kbps_manual_mode = false;
                                    snprintf(g_export.mp4_video_kbps_str, sizeof(g_export.mp4_video_kbps_str),
                                             "%d", g_mp4_v_kbps_presets[sel_idx]);
                                } else if (sel_idx == NUM_MP4_V_KBPS_PRESETS) {
                                    // Selected "Manual" at end of Video Bitrate dropdown
                                    g_export.mp4_v_kbps_manual_mode = true;
                                    g_export.active_field = EXP_FIELD_MP4_V_KBPS;
                                    daw_text_edit_focus_buf(g_export.mp4_video_kbps_str, true);
                                    SDL_StartTextInput();
                                }
                                g_export.mp4_v_kbps_dropdown_open = false;
                                continue;
                            }
                            g_export.mp4_v_kbps_dropdown_open = false;
                        }

                        // 3. Check open MP4 AAC Audio Bitrate Dropdown Menu
                        if (g_export.format == EXPORT_FMT_MP4 && g_export.mp4_aac_dropdown_open) {
                            int dd_x = ex + 286;
                            int dd_y = r3_y + 26;
                            int dd_w = 206;
                            if (mx >= dd_x && mx <= dd_x + dd_w && my >= dd_y && my <= dd_y + 80) {
                                int sel_row = (my - dd_y) / 26;
                                if (sel_row == 0) strcpy(g_export.mp4_audio_kbps_str, "120");
                                else if (sel_row == 1) strcpy(g_export.mp4_audio_kbps_str, "192");
                                else if (sel_row == 2) strcpy(g_export.mp4_audio_kbps_str, "320");
                                g_export.mp4_aac_dropdown_open = false;
                                continue;
                            }
                            g_export.mp4_aac_dropdown_open = false;
                        }

                        // 1. Source row (ey + 74)
                        int sy_src = ey + 74;
                        if (my >= sy_src - 2 && my <= sy_src + 30) {
                            if (mx >= ex + 130 && mx <= ex + 280) g_export.source = EXPORT_SRC_MASTER;
                            else if (mx >= ex + 290 && mx <= ex + 500) g_export.source = EXPORT_SRC_STEMS;
                            else if (mx >= ex + 510 && mx <= ex + 702) g_export.source = EXPORT_SRC_ITEMS;
                            continue;
                        }

                        // 2. Output Directory row (ey + 114) & [Browse...] button
                        int sy_dir = ey + 114;
                        if (my >= sy_dir - 2 && my <= sy_dir + 30) {
                            if (mx >= ex + 130 && mx <= ex + 582) {
                                g_export.active_field = EXP_FIELD_DIR;
                                daw_text_edit_click_cursor(g_export.directory, mx, ex + 136);
                                SDL_StartTextInput();
                            } else if (mx >= ex + 592 && mx <= ex + 702) {
                                browse_export_directory_and_filename();
                            }
                            continue;
                        }

                        // 3. Output File Name row (ey + 150)
                        int sy_fn = ey + 150;
                        if (my >= sy_fn - 2 && my <= sy_fn + 30) {
                            if (mx >= ex + 130 && mx <= ex + 582) {
                                g_export.active_field = EXP_FIELD_FILENAME;
                                daw_text_edit_click_cursor(g_export.filename, mx, ex + 136);
                                SDL_StartTextInput();
                            }
                            continue;
                        }

                        // 4. Sample Rate (48,000 / 44,100) & Channels (Mono / Stereo) row (ey + 190)
                        int sy_sr = ey + 190;
                        if (my >= sy_sr - 2 && my <= sy_sr + 30) {
                            if (mx >= ex + 130 && mx <= ex + 240) g_export.sample_rate = 48000;
                            else if (mx >= ex + 248 && mx <= ex + 358) g_export.sample_rate = 44100;
                            else if (mx >= ex + 466 && mx <= ex + 578) g_export.channels = 1;
                            else if (mx >= ex + 586 && mx <= ex + 702) g_export.channels = 2;
                            continue;
                        }

                        // 5. Format Tabs: WAV / MP3 / MP4 (ey + 230)
                        int sy_fmt = ey + 230;
                        if (my >= sy_fmt - 2 && my <= sy_fmt + 30) {
                            if (mx >= ex + 130 && mx <= ex + 290) {
                                g_export.format = EXPORT_FMT_WAV;
                                g_export.mp4_aac_dropdown_open = false;
                                g_export.mp4_size_dropdown_open = false;
                            } else if (mx >= ex + 300 && mx <= ex + 460) {
                                g_export.format = EXPORT_FMT_MP3;
                                g_export.mp4_aac_dropdown_open = false;
                                g_export.mp4_size_dropdown_open = false;
                            } else if (mx >= ex + 470 && mx <= ex + 702) {
                                g_export.format = EXPORT_FMT_MP4;
                                g_export.mp4_audio_codec = EXPORT_MP4_AUDIO_AAC;
                            }
                            continue;
                        }

                        // 6. Format-specific sub-options (ey + 268 .. ey + 442)
                        if (g_export.format == EXPORT_FMT_WAV) {
                            if (my >= sy_sub + 10 && my <= sy_sub + 46) {
                                if (mx >= ex + 160 && mx <= ex + 310) g_export.wav_bit_depth = 16;
                                else if (mx >= ex + 322 && mx <= ex + 472) g_export.wav_bit_depth = 24;
                                else if (mx >= ex + 484 && mx <= ex + 634) g_export.wav_bit_depth = 32;
                                continue;
                            }
                        } else if (g_export.format == EXPORT_FMT_MP3) {
                            if (my >= sy_sub + 10 && my <= sy_sub + 46) {
                                if (mx >= ex + 160 && mx <= ex + 310) g_export.mp3_bitrate = 128;
                                else if (mx >= ex + 322 && mx <= ex + 472) g_export.mp3_bitrate = 192;
                                else if (mx >= ex + 484 && mx <= ex + 634) g_export.mp3_bitrate = 320;
                                continue;
                            }
                        } else if (g_export.format == EXPORT_FMT_MP4) {
                            // Row 1 (Requirement 4): File: [MPEG-4]  Size: [ Dropdown with Manual at end ]  FPS: [25.00]
                            if (my >= r1_y - 2 && my <= r1_y + 28) {
                                int cur_res_idx = get_current_mp4_res_preset_idx();
                                if (!g_export.mp4_size_manual_mode && cur_res_idx >= 0 && cur_res_idx < NUM_MP4_RES_PRESETS) {
                                    if (mx >= ex + 266 && mx <= ex + 492) {
                                        g_export.mp4_size_dropdown_open = !g_export.mp4_size_dropdown_open;
                                        g_export.mp4_v_kbps_dropdown_open = false;
                                        g_export.mp4_aac_dropdown_open = false;
                                    }
                                } else {
                                    if (mx >= ex + 266 && mx <= ex + 350) {
                                        g_export.active_field = EXP_FIELD_MP4_WIDTH;
                                        daw_text_edit_click_cursor(g_export.mp4_width_str, mx, ex + 274);
                                        SDL_StartTextInput();
                                    } else if (mx >= ex + 370 && mx <= ex + 454) {
                                        g_export.active_field = EXP_FIELD_MP4_HEIGHT;
                                        daw_text_edit_click_cursor(g_export.mp4_height_str, mx, ex + 378);
                                        SDL_StartTextInput();
                                    } else if (mx >= ex + 458 && mx <= ex + 492) {
                                        g_export.mp4_size_dropdown_open = !g_export.mp4_size_dropdown_open;
                                        g_export.mp4_v_kbps_dropdown_open = false;
                                        g_export.mp4_aac_dropdown_open = false;
                                    }
                                }
                                if (mx >= ex + 544 && mx <= ex + 636) {
                                    g_export.active_field = EXP_FIELD_MP4_FPS;
                                    daw_text_edit_click_cursor(g_export.mp4_fps_str, mx, ex + 552);
                                    SDL_StartTextInput();
                                }
                                continue;
                            }
                            // Row 2 (Requirement 5): Video: [H.264]  Bitrate: [ Dropdown with Manual at end ]
                            if (my >= r2_y - 2 && my <= r2_y + 28) {
                                int cur_vk_idx = get_current_mp4_v_kbps_preset_idx();
                                if (!g_export.mp4_v_kbps_manual_mode && cur_vk_idx >= 0 && cur_vk_idx < NUM_MP4_V_KBPS_PRESETS) {
                                    if (mx >= ex + 286 && mx <= ex + 492) {
                                        g_export.mp4_v_kbps_dropdown_open = !g_export.mp4_v_kbps_dropdown_open;
                                        g_export.mp4_size_dropdown_open = false;
                                        g_export.mp4_aac_dropdown_open = false;
                                    }
                                } else {
                                    if (mx >= ex + 286 && mx <= ex + 400) {
                                        g_export.active_field = EXP_FIELD_MP4_V_KBPS;
                                        daw_text_edit_click_cursor(g_export.mp4_video_kbps_str, mx, ex + 294);
                                        SDL_StartTextInput();
                                    } else if (mx >= ex + 458 && mx <= ex + 492) {
                                        g_export.mp4_v_kbps_dropdown_open = !g_export.mp4_v_kbps_dropdown_open;
                                        g_export.mp4_size_dropdown_open = false;
                                        g_export.mp4_aac_dropdown_open = false;
                                    }
                                }
                                continue;
                            }
                            // Row 3: Audio: [ AAC ]  Bitrate: [ 120 / 192 / 320 kbps v ] dropdown menu
                            if (my >= r3_y - 2 && my <= r3_y + 28) {
                                g_export.mp4_audio_codec = EXPORT_MP4_AUDIO_AAC;
                                if (mx >= ex + 286 && mx <= ex + 492) {
                                    g_export.mp4_aac_dropdown_open = !g_export.mp4_aac_dropdown_open;
                                    g_export.mp4_size_dropdown_open = false;
                                    g_export.mp4_v_kbps_dropdown_open = false;
                                    continue;
                                }
                            }
                        }

                        // 7. Option: Two-pass render checkbox (ey + 454)
                        int sy_opt = ey + 454;
                        if (my >= sy_opt - 2 && my <= sy_opt + 26 && mx >= ex + 20 && mx <= ex + 280) {
                            g_export.two_pass_render = !g_export.two_pass_render;
                            continue;
                        }

                        // 8. Bottom buttons: [Render / Export] and [Cancel]
                        int btn_y = ey + eh - 44;
                        if (my >= btn_y - 2 && my <= btn_y + 34) {
                            if (mx >= ex + ew - 272 && mx <= ex + ew - 123) {
                                save_themes_settings_json();
                                request_start_unified_export(p, ren);
                            } else if (mx >= ex + ew - 117 && mx <= ex + ew - 18) {
                                g_export.mp4_aac_dropdown_open = false;
                                g_export.mp4_size_dropdown_open = false;
                                g_export.overwrite_prompt_open = false;
                                g_export.is_open = false;
                                save_themes_settings_json();
                            }
                            continue;
                        }
                        continue;
                    }
                    continue;
                }

                // ================= INSERT MULTIPLE MEDIA ITEMS POPUP INTERACTIONS =================
                if (g_multi_import.is_open && point_in_rect(mx, my, &g_multi_import.win_rect)) {
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
                if (g_theme.is_open && point_in_rect(mx, my, &g_theme.win_rect)) {
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
                            daw_text_edit_click_cursor(g_theme.code_buf, mx, p_x + 8);
                            SDL_StartTextInput();
                            continue;
                        }

                        // 5. [Save Theme] button -> Saves current ARGB theme into Saved Themes list
                        if (mx >= p_x && mx <= p_x + 84 && my >= b_y && my <= b_y + 30) {
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
                                snprintf(st->name, sizeof(st->name), "Custom Theme %d", slot + 1);
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

                        // 5B. [Delete] button -> Deletes selected Saved Theme from list & JSON
                        if (mx >= p_x + 88 && mx <= p_x + 160 && my >= b_y && my <= b_y + 30) {
                            delete_saved_theme_at_index(g_theme.selected_saved_idx);
                            continue;
                        }

                        // 6. [Default] button -> Restores default Orange theme
                        if (mx >= p_x + 164 && mx <= p_x + 256 && my >= b_y && my <= b_y + 30) {
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

                        // 7. Right-side Saved Themes list & scrollbar (with per-row [x] delete button)
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
                                int row_right = rx + 4 + (rw - 22);
                                if (clicked_row >= NUM_INBUILT_THEMES && mx >= row_right - 24 && mx <= row_right) {
                                    // Clicked per-row [x] delete button (only on manually added themes)
                                    delete_saved_theme_at_index(clicked_row);
                                } else {
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
                            }
                            continue;
                        }
                        continue;
                    }
                }

                // ================= HIDE TRACKS POPUP WINDOW INTERACTIONS (WITH SCROLLBAR) =================
                if (g_hide_popup_open && point_in_rect(mx, my, &g_hide_win_rect)) {
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
                                push_undo_state();
                                set_track_hidden(row, !is_track_hidden(row));
                                record_project_change(is_track_hidden(row) ? "Hidden Track" : "Shown Track", true);
                            }
                        }

                        int b_y = hy + hh - 38;
                        if (my >= b_y && my <= b_y + 28) {
                            if (mx >= hx + 14 && mx <= hx + 114) {
                                push_undo_state();
                                for (int t = 0; t < p->num_tracks; t++) set_track_hidden(t, false);
                                record_project_change("Shown All Tracks", true);
                            } else if (mx >= hx + 124 && mx <= hx + 224) {
                                push_undo_state();
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
                if (g_action_win.is_open && handle_action_window_mouse_down(p, ren, win, mx, my, e.button.clicks)) {
                    continue;
                }

                // ================= PROJECT HISTORY POPUP WINDOW INTERACTIONS (100 ITEMS) =================
                if (g_history_popup_open && point_in_rect(mx, my, &g_history_win_rect)) {
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
                        int et = g_daw.editing_track_name_idx;
                        int ty_et = (et >= 0 && et < p->num_tracks) ? get_track_y_pos(et) : -999;
                        if (et >= 0 && mx >= 8 && mx <= g_daw.tcp_width - 10 && my >= ty_et + 6 && my <= ty_et + 28 &&
                            e.button.button == SDL_BUTTON_LEFT) {
                            daw_text_edit_click_cursor(g_daw.editing_name_buf, mx, 12);
                            continue;
                        }
                        if (strcmp(p->track_names[g_daw.editing_track_name_idx], g_daw.editing_name_buf) != 0) push_undo_state();
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
                                daw_text_edit_click_cursor(g_editing_marker_full_buf, mx, mk_x + 6);
                            }
                        }
                        if (clicked_inside_editing_marker) {
                            continue;
                        }
                        if (em >= 0 && em < p->num_markers) {
                            if (strcmp(get_marker_full_str(p, em), g_editing_marker_full_buf) != 0) push_undo_state();
                            set_marker_full_str(p, em, g_editing_marker_full_buf);
                            p->is_dirty = true;
                        }
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_marker_idx = -1;
                        SDL_StopTextInput();
                    }
                }

                if (p->video.is_open && !g_video_pinned_global) {
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
                            if (g_video_pinned_global) {
                                g_video_pinned_global = false;
                                destroy_external_pinned_video_window();
                            }
                            p->video.is_open = false;
                        } else if (mx >= p->video.rect.x + p->video.rect.w - 72 &&
                                   mx <  p->video.rect.x + p->video.rect.w - 48 &&
                                   my <= p->video.rect.y + 24) {
                            // Requirement 7: Pin Symbol button on Video Monitor (Pin Active = Globally visible outside app)
                            toggle_video_global_pin_state(p, win);
                        } else if (mx >= p->video.rect.x + p->video.rect.w - 48 &&
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
                                p = get_cur_tab();
                            } else {
                                g_daw.active_tab = t;
                                g_selected_marker_idx = -1;
                                p = get_cur_tab();
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
                        memset(g_track_locked[g_daw.num_tabs], 0, sizeof(g_track_locked[0]));
                        memset(g_track_lock_lr_only[g_daw.num_tabs], 0, sizeof(g_track_lock_lr_only[0]));
                        memset(g_track_lock_ud_only[g_daw.num_tabs], 0, sizeof(g_track_lock_ud_only[0]));
                        g_lock_opt_full[g_daw.num_tabs] = false;
                        g_lock_opt_complete_unlock[g_daw.num_tabs] = false;
                        g_lock_opt_prevent_lr[g_daw.num_tabs] = false;
                        g_lock_opt_prevent_ud[g_daw.num_tabs] = false;
                        memset(g_track_color_idx[g_daw.num_tabs], 0, sizeof(g_track_color_idx[0]));
                        memset(g_track_peak_color_idx[g_daw.num_tabs], 0, sizeof(g_track_peak_color_idx[0]));
                        memset(g_item_peak_color_idx[g_daw.num_tabs], 0, sizeof(g_item_peak_color_idx[0]));
                        g_tab_peak_color_idx[g_daw.num_tabs] = 0;
                        reset_tab_undo_history(g_daw.num_tabs);
                        memset(g_tab_history[g_daw.num_tabs], 0, sizeof(g_tab_history[0]));
                        g_tab_history_count[g_daw.num_tabs] = 0;
                        g_tab_notification[g_daw.num_tabs][0] = '\0';
                        init_tab_app_volume_defaults(g_daw.num_tabs);
                        g_tab_needs_project_setup[g_daw.num_tabs] = true;
                        g_daw.active_tab = g_daw.num_tabs++;
                        g_selected_marker_idx = -1;
                        record_project_change("New Project Tab Opened", false);
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

                        // Top Filter & Pin row (sr->y + 34 .. sr->y + 60) - Up/Down options removed
                        int top_y = sr->y + 34;
                        if (my >= top_y && my <= top_y + 26) {
                            int flt_w = sr->w - 204;
                            if (flt_w < 120) flt_w = 120;
                            int flt_x = sr->x + 56;
                            int clr_x = flt_x + flt_w + 8;
                            int pin_x = clr_x + 60;
                            if (mx >= flt_x && mx <= flt_x + flt_w) {
                                g_script_filter_focused = true;
                                daw_text_edit_click_cursor(g_script_filter_text, mx, flt_x + 6);
                                SDL_StartTextInput();
                            } else if (mx >= clr_x && mx <= clr_x + 54) {
                                g_script_filter_text[0] = '\0';
                                g_script_scroll_y = 0;
                                refresh_filtered_scripts();
                            } else if (mx >= pin_x && mx <= pin_x + 64) {
                                toggle_pin_selected_script();
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
                    if (mx >= 60 && mx <= 258 && my >= m_start && my <= m_start + 108) {
                        if (my >= m_start && my < m_start + 27) {
                            // Action Submenu 1: Scripts -> Opens unified Lua & Python Script Manager popup window
                            scan_scripts_directory_to_actions(&g_daw);
                            save_keyboard_shortcuts_json();
                            refresh_filtered_scripts();
                            g_daw.script_manager_open = true;
                            g_daw.menu_action_open = false;
                            continue;
                        } else if (my >= m_start + 27 && my < m_start + 54) {
                            // Action Submenu 2: Dynamic split -> Opens simple box-based Dynamic split popup window
                            g_dyn_split.is_open = true;
                            g_dyn_split.win_rect.x = (win_w - g_dyn_split.win_rect.w) / 2;
                            g_dyn_split.win_rect.y = (win_h - g_dyn_split.win_rect.h) / 2;
                            g_dyn_split.editing_field_id = 0;
                            g_dyn_split.dragging_slider_id = 0;
                            g_daw.menu_action_open = false;
                            continue;
                        } else if (my >= m_start + 54 && my < m_start + 81) {
                            // Action Submenu 3: Reposition -> Opens Reposition Items popup window
                            g_reposition.is_open = true;
                            g_reposition.win_rect.x = (win_w - g_reposition.win_rect.w) / 2;
                            g_reposition.win_rect.y = (win_h - g_reposition.win_rect.h) / 2;
                            g_reposition.interval_focused = true;
                            SDL_StartTextInput();
                            g_daw.menu_action_open = false;
                            continue;
                        } else if (my >= m_start + 81 && my <= m_start + 108) {
                            // Action Submenu 4 (Requirement 6): Infinity -dB ON/OFF for selected track
                            toggle_selected_track_infinity_db(p);
                            g_daw.menu_action_open = false;
                            continue;
                        }
                    }
                    g_daw.menu_action_open = false;
                    continue;
                }

                if (g_menu_tools_open) {
                    int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                    if (mx >= 126 && mx <= 282 && my >= m_start && my <= m_start + 81) {
                        if (my >= m_start && my < m_start + 27) {
                            // Tools Submenu 1: Marker
                            push_undo_state();
                            p->show_marker_lane = !p->show_marker_lane;
                            g_menu_tools_open = false;
                            g_submenu_tools_record_open = false;
                            continue;
                        } else if (my >= m_start + 27 && my < m_start + 54) {
                            // Tools Submenu 2: History
                            g_history_popup_open = true;
                            g_history_win_rect.x = (win_w - g_history_win_rect.w) / 2;
                            g_history_win_rect.y = (win_h - g_history_win_rect.h) / 2;
                            g_menu_tools_open = false;
                            g_submenu_tools_record_open = false;
                            continue;
                        } else if (my >= m_start + 54 && my <= m_start + 81) {
                            // Tools Submenu 3: Clip properties
                            g_menu_tools_open = false;
                            g_submenu_tools_record_open = false;
                            open_clip_properties_window(p, win_w, win_h);
                            continue;
                        }
                    }
                    g_menu_tools_open = false;
                    g_submenu_tools_record_open = false;
                    continue;
                }

                if (g_menu_edit_open) {
                    int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                    if (mx >= 190 && mx <= 346 && my >= m_start && my <= m_start + 54) {
                        if (my >= m_start && my < m_start + 27) {
                            // Edit Submenu 1: Color (Unified Track / Clip / Peaks Color window)
                            g_menu_edit_open = false;
                            g_color_picker.win_rect.x = (win_w - g_color_picker.win_rect.w) / 2;
                            g_color_picker.win_rect.y = (win_h - g_color_picker.win_rect.h) / 2;
                            open_color_picker_dialog(g_color_picker.target_mode);
                            continue;
                        } else if (my >= m_start + 27 && my <= m_start + 54) {
                            // Edit Submenu 2: Join (Glue)
                            g_menu_edit_open = false;
                            execute_join_glue_selected_items(p, ren, win);
                            continue;
                        }
                    }
                    g_menu_edit_open = false;
                    continue;
                }

                if (g_menu_settings_open) {
                    int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                    if (mx >= 250 && mx <= 434 && my >= m_start && my <= m_start + 108) {
                        if (my >= m_start && my < m_start + 27) {
                            // Settings Submenu 1: Keyboard Shortcut
                            scan_scripts_directory_to_actions(&g_daw);
                            save_keyboard_shortcuts_json();
                            g_action_win.is_open = true;
                            g_action_win.win_rect.x = (win_w - g_action_win.win_rect.w) / 2;
                            g_action_win.win_rect.y = (win_h - g_action_win.win_rect.h) / 2;
                            g_menu_settings_open = false;
                            continue;
                        } else if (my >= m_start + 27 && my < m_start + 54) {
                            // Settings Submenu 2: Themes
                            init_default_saved_themes();
                            sync_theme_code_string();
                            g_theme.is_open = true;
                            g_theme.win_rect.x = (win_w - g_theme.win_rect.w) / 2;
                            g_theme.win_rect.y = (win_h - g_theme.win_rect.h) / 2;
                            g_menu_settings_open = false;
                            continue;
                        } else if (my >= m_start + 54 && my < m_start + 81) {
                            // Settings Submenu 3: Mouse
                            g_mouse_settings_open = true;
                            g_mouse_win_rect.x = (win_w - g_mouse_win_rect.w) / 2;
                            g_mouse_win_rect.y = (win_h - g_mouse_win_rect.h) / 2;
                            g_menu_settings_open = false;
                            continue;
                        } else if (my >= m_start + 81 && my <= m_start + 108) {
                            // Settings Submenu 4 (Requirement 10): Auto save
                            g_autosave.is_open = true;
                            g_autosave.win_rect.x = (win_w - g_autosave.win_rect.w) / 2;
                            g_autosave.win_rect.y = (win_h - g_autosave.win_rect.h) / 2;
                            snprintf(g_autosave.interval_buf, sizeof(g_autosave.interval_buf), "%d", g_autosave.interval_minutes);
                            g_autosave.interval_focused = false;
                            g_menu_settings_open = false;
                            continue;
                        }
                    }
                    g_menu_settings_open = false;
                    continue;
                }

                // Requirement 3: If any popup window is open, keep mouse clicks strictly focused on that popup window
                if (is_any_popup_or_dropdown_open()) {
                    continue;
                }

                if (my >= TABBAR_HEIGHT && my <= TABBAR_HEIGHT + TOPBAR_HEIGHT) {
                    int left_menu_end = 400;
                    int right_menu_start = win_w - 380;
                    int clock_x = left_menu_end + ((right_menu_start - left_menu_end) / 2) - 68;
                    if (clock_x < left_menu_end + 82) clock_x = left_menu_end + 82;
                    int play_btn_x  = clock_x - 46;
                    int vol_btn_x   = play_btn_x - 38;
                    int clock_box_x = clock_x - 10;
                    int rec_btn_x   = clock_box_x + 128 + 6;
                    int rec_onoff_x = rec_btn_x + 52;

                    // Left-side top menu: File | Action | Tools | Edit | Settings | Video
                    if (mx >= 8 && mx <= 54) {
                        g_daw.menu_file_open = !g_daw.menu_file_open;
                        g_daw.menu_action_open = false;
                        g_menu_tools_open = false;
                        g_menu_edit_open = false;
                        g_menu_settings_open = false;
                    }
                    else if (mx >= 60 && mx <= 120) {
                        g_daw.menu_action_open = !g_daw.menu_action_open;
                        g_daw.menu_file_open = false;
                        g_menu_tools_open = false;
                        g_menu_edit_open = false;
                        g_menu_settings_open = false;
                    }
                    else if (mx >= 126 && mx <= 184) {
                        g_menu_tools_open = !g_menu_tools_open;
                        g_daw.menu_file_open = false;
                        g_daw.menu_action_open = false;
                        g_menu_edit_open = false;
                        g_menu_settings_open = false;
                    }
                    else if (mx >= 190 && mx <= 244) {
                        g_menu_edit_open = !g_menu_edit_open;
                        g_daw.menu_file_open = false;
                        g_daw.menu_action_open = false;
                        g_menu_tools_open = false;
                        g_menu_settings_open = false;
                    }
                    else if (mx >= 250 && mx <= 326) {
                        g_menu_settings_open = !g_menu_settings_open;
                        g_daw.menu_file_open = false;
                        g_daw.menu_action_open = false;
                        g_menu_tools_open = false;
                        g_menu_edit_open = false;
                    }
                    else if (mx >= 332 && mx <= 392) {
                        p->video.is_open = !p->video.is_open;
                        if (!p->video.is_open && g_video_pinned_global) {
                            g_video_pinned_global = false;
                            destroy_external_pinned_video_window();
                        }
                        g_daw.menu_file_open = false;
                        g_daw.menu_action_open = false;
                        g_menu_tools_open = false;
                        g_menu_edit_open = false;
                        g_menu_settings_open = false;
                    }
                    // Requirement 4: Volume symbol button near top-left side of Play/Pause button
                    else if (mx >= vol_btn_x && mx <= vol_btn_x + 32) {
                        g_vol_popup.is_open = !g_vol_popup.is_open;
                        if (g_vol_popup.is_open) {
                            g_vol_popup.win_rect.x = vol_btn_x - 40;
                            if (g_vol_popup.win_rect.x < 12) g_vol_popup.win_rect.x = 12;
                            if (g_vol_popup.win_rect.x + g_vol_popup.win_rect.w > win_w - 12) {
                                g_vol_popup.win_rect.x = win_w - g_vol_popup.win_rect.w - 12;
                            }
                            g_vol_popup.win_rect.y = TABBAR_HEIGHT + TOPBAR_HEIGHT + 4;
                        }
                    }
                    // Requirement 12: Play/Pause symbol button near left side of Time Clock
                    else if (mx >= play_btn_x && mx <= play_btn_x + 32) {
                        if (g_rec.is_recording) start_or_stop_recording(p, ren, win);
                        else p->is_playing = !p->is_playing;
                    }
                    // Requirement 14: Double-click on Time Clock opens tiny manual timings entry popup
                    else if (mx >= clock_box_x && mx <= clock_box_x + 128) {
                        if (e.button.clicks >= 2) {
                            open_time_jump_popup(p, win_w, win_h);
                        }
                    }
                    // Requirement 6: 'Rec' button near next to Time Clock (only records when ON/OFF switch is ON)
                    else if (mx >= rec_btn_x && mx <= rec_btn_x + 52) {
                        if (g_rec_master_enabled) {
                            start_or_stop_recording(p, ren, win);
                        } else {
                            record_project_change("Rec is OFF (Turn ON switch next to Rec)", false);
                        }
                    }
                    // Requirement 4: ON/OFF button next to Record menu (default OFF)
                    else if (mx >= rec_onoff_x && mx <= rec_onoff_x + 42) {
                        g_rec_master_enabled = !g_rec_master_enabled;
                        if (!g_rec_master_enabled && g_rec.is_recording) {
                            start_or_stop_recording(p, ren, win);
                        }
                        record_project_change(g_rec_master_enabled ? "Record Switch: ON" : "Record Switch: OFF", false);
                    }
                    // Right-side top menu: [Undo Symbol] | [Redo Symbol] | Range | Crossfade | Save
                    else if (mx >= win_w - 314 && mx <= win_w - 274) { perform_undo(); record_project_change("Undo Action", true); }
                    else if (mx >= win_w - 268 && mx <= win_w - 228) { perform_redo(); record_project_change("Redo Action", true); }
                    else if (mx >= win_w - 222 && mx <= win_w - 162) {
                        push_undo_state();
                        handle_top_menu_range_button(p);
                        if (g_range_popup.is_open) {
                            g_range_popup.win_rect.x = win_w - g_range_popup.win_rect.w - 14;
                            if (g_range_popup.win_rect.x < 10) g_range_popup.win_rect.x = 10;
                            g_range_popup.win_rect.y = TABBAR_HEIGHT + TOPBAR_HEIGHT + 4;
                        }
                    }
                    else if (mx >= win_w - 156 && mx <= win_w - 66)  { push_undo_state(); set_crossfade_active(!is_crossfade_active()); }
                    else if (mx >= win_w - 60 && mx <= win_w - 8) {
                        daw_save_project_dialog(p, false);
                    }
                    continue;
                }

                if (abs(mx - g_daw.tcp_width) <= 5 && my > TABBAR_HEIGHT + TOPBAR_HEIGHT) {
                    resizing_tcp_width = true;
                    continue;
                }

                // Corner Header (+ Track, All-Track Lock Symbol, 'H' Hide Tracks, Master M, Master S)
                int corner_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                if (mx < g_daw.tcp_width && my > corner_y && my <= corner_y + RULER_HEIGHT + m_lane_h) {
                    if (mx >= 8 && mx <= 76 && p->num_tracks < MAX_TRACKS && e.button.button == SDL_BUTTON_LEFT) {
                        insert_new_track_below_selected(p);
                    } else if (mx >= 80 && mx <= 106 && e.button.button == SDL_BUTTON_LEFT) {
                        g_lock_popup_open = !g_lock_popup_open;
                        if (g_lock_popup_open) {
                            g_lock_win_rect.x = (win_w - g_lock_win_rect.w) / 2;
                            g_lock_win_rect.y = (win_h - g_lock_win_rect.h) / 2;
                        }
                    } else if (mx >= 110 && mx <= 136 && e.button.button == SDL_BUTTON_LEFT) {
                        g_hide_popup_open = !g_hide_popup_open;
                        if (g_hide_popup_open && point_in_rect(mx, my, &g_hide_win_rect)) {
                            g_hide_win_rect.x = (win_w - g_hide_win_rect.w) / 2;
                            g_hide_win_rect.y = (win_h - g_hide_win_rect.h) / 2;
                        }
                    } else if (mx >= 140 && mx <= 166 && e.button.button == SDL_BUTTON_LEFT) {
                        toggle_master_mute_all_tracks(p);
                    } else if (mx >= 170 && mx <= 196 && e.button.button == SDL_BUTTON_LEFT) {
                        toggle_master_solo_all_tracks(p);
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
                            push_undo_state();
                            dragging_range_left = true;
                            range_orig_start_sec = get_range_start();
                            range_orig_end_sec   = get_range_end();
                            continue;
                        } else if (abs(mx - rx2) <= 8) {
                            push_undo_state();
                            dragging_range_right = true;
                            range_orig_start_sec = get_range_start();
                            range_orig_end_sec   = get_range_end();
                            continue;
                        } else if (mx > rx1 + 8 && mx < rx2 - 8 &&
                                   ((my >= ruler_y + RULER_HEIGHT - 10 && my <= ruler_y + RULER_HEIGHT) || in_top_strip)) {
                            push_undo_state();
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
                        daw_set_playhead_seconds(p, fmax(0.0, p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second));
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
                        int tab_tcp = get_valid_tab_idx();
                        bool trk_full_locked = is_track_full_locked_tab(tab_tcp, t);
                        int rel_y_edge_chk = my - cur_y;
                        bool over_bot_controls = (rel_y_edge_chk >= 32 && rel_y_edge_chk <= 55 && mx >= 8 && mx <= 172);
                        if (!over_bot_controls && abs(my - (cur_y + th)) <= 4) {
                            if (!trk_full_locked) {
                                push_undo_state();
                                resizing_track_idx = t;
                                track_orig_height = th;
                                mouse_drag_start_y = my;
                            }
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

                            if (rel_y >= 32 && rel_y <= 56) {
                                // Individual Track Lock button is at left side of 'M' (mx: 96 .. 118)
                                if (mx >= 96 && mx <= 118) {
                                    if (e.button.button == SDL_BUTTON_LEFT) {
                                        push_undo_state();
                                        g_track_locked[tab_tcp][t] = !g_track_locked[tab_tcp][t];
                                        if (g_track_locked[tab_tcp][t]) {
                                            // Individual track lock defaults to Complete Lock unless partial LR/UD was set
                                            if (!g_track_lock_lr_only[tab_tcp][t] && !g_track_lock_ud_only[tab_tcp][t]) {
                                                g_track_lock_lr_only[tab_tcp][t] = false;
                                                g_track_lock_ud_only[tab_tcp][t] = false;
                                            }
                                        } else {
                                            g_track_lock_lr_only[tab_tcp][t] = false;
                                            g_track_lock_ud_only[tab_tcp][t] = false;
                                            g_lock_opt_full[tab_tcp] = false;
                                        }
                                        record_project_change(g_track_locked[tab_tcp][t] ? "Locked Individual Track" : "Unlocked Individual Track", true);
                                    }
                                    handled_btn = true;
                                } else if (!trk_full_locked && e.button.button == SDL_BUTTON_LEFT) {
                                    if (mx >= 8 && mx <= 26) {
                                        push_undo_state();
                                        if (!g_track_inf_db[tab_tcp][t] && p->track_db[t] > -60.0f) {
                                            p->track_db[t] -= 1.0f;
                                            if (p->track_db[t] <= -60.0f) {
                                                g_track_inf_db[tab_tcp][t] = true;
                                                p->track_db[t] = -120.0f;
                                            }
                                        }
                                        record_project_change("Adjusted Track Volume", true);
                                        handled_btn = true;
                                    } else if (mx >= 72 && mx <= 90) {
                                        push_undo_state();
                                        if (g_track_inf_db[tab_tcp][t] || p->track_db[t] <= -60.0f) {
                                            g_track_inf_db[tab_tcp][t] = false;
                                            float rdb = g_track_pre_inf_db[tab_tcp][t];
                                            if (rdb <= -59.0f || rdb > 24.0f) rdb = 0.0f;
                                            p->track_db[t] = rdb;
                                        } else {
                                            p->track_db[t] += 1.0f;
                                        }
                                        record_project_change("Adjusted Track Volume", true);
                                        handled_btn = true;
                                    }
                                    else if (mx >= 122 && mx <= 144) { push_undo_state(); p->tracks[t].mute = !p->tracks[t].mute; record_project_change("Toggled Track Mute", true); handled_btn = true; }
                                    else if (mx >= 148 && mx <= 170) { push_undo_state(); p->tracks[t].solo = !p->tracks[t].solo; record_project_change("Toggled Track Solo", true); handled_btn = true; }
                                } else if (mx >= 8 && mx <= 172) {
                                    handled_btn = true;
                                }
                            } else if (rel_y >= 6 && rel_y <= 28 && mx >= 8 && mx <= g_daw.tcp_width - 10) {
                                if (e.button.clicks >= 2 && !trk_full_locked) {
                                    g_daw.input_focus = INPUT_FOCUS_TRACK_NAME;
                                    g_daw.editing_track_name_idx = t;
                                    strncpy(g_daw.editing_name_buf, p->track_names[t], 63);
                                    daw_text_edit_focus_buf(g_daw.editing_name_buf, true);
                                    SDL_StartTextInput();
                                    handled_btn = true;
                                }
                            }

                            if (!handled_btn && e.button.button == SDL_BUTTON_LEFT && !trk_full_locked) {
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
                        if (g_mouse_click_universal_moves_playhead && e.button.button == SDL_BUTTON_LEFT) {
                            daw_set_playhead_seconds(p, fmax(0.0, click_time));
                        }
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
                            g_drag_item_orig_track[gi]  = p->items[gi].track_idx;
                        }
                        dragging_trim_left  = hit_tl;
                        dragging_trim_right = hit_tr;
                        dragging_fade_in    = hit_fi;
                        dragging_fade_out   = hit_fo;
                    } else {
                        if ((g_mouse_click_empty_moves_playhead || g_mouse_click_universal_moves_playhead) &&
                            e.button.button == SDL_BUTTON_LEFT) {
                            daw_set_playhead_seconds(p, fmax(0.0, click_time));
                        }
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
                g_color_picker.is_dragging = false;
                g_clip_props.is_dragging = false;
                g_clip_props.resize_edge = 0;
                g_mouse_win_dragging = false;
                g_autosave.is_dragging = false;
                g_lock_win_dragging = false;
                g_time_jump_dragging = false;
                g_range_popup.is_dragging = false;
                g_vol_popup.is_dragging = false;
                g_vol_popup.sb_dragging = false;
                g_vol_popup.dragging_slider_idx = -1;
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
                if (g_color_picker.is_open && g_color_picker.is_dragging) {
                    g_color_picker.win_rect.x = mx - g_color_picker.drag_off_x;
                    g_color_picker.win_rect.y = my - g_color_picker.drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_range_popup.is_open && g_range_popup.is_dragging) {
                    g_range_popup.win_rect.x = mx - g_range_popup.drag_off_x;
                    g_range_popup.win_rect.y = my - g_range_popup.drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_vol_popup.is_open && g_vol_popup.is_dragging) {
                    g_vol_popup.win_rect.x = mx - g_vol_popup.drag_off_x;
                    g_vol_popup.win_rect.y = my - g_vol_popup.drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_vol_popup.is_open && g_vol_popup.dragging_slider_idx >= 0) {
                    ensure_app_vol_defaults_initialized();
                    int vtab = get_valid_tab_idx();
                    int sl_x = g_vol_popup.win_rect.x + 156;
                    int sl_w = 168;
                    float u = (float)(mx - sl_x) / (float)sl_w;
                    if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
                    float val = floorf(u * 100.0f + 0.5f);
                    if (g_vol_popup.dragging_slider_idx == 0) {
                        g_app_master_vol[vtab] = val;
                    } else {
                        int trk_i = g_vol_popup.dragging_slider_idx - 1;
                        if (trk_i >= 0 && trk_i < MAX_TRACKS) {
                            g_app_track_vol[vtab][trk_i] = val;
                        }
                    }
                    p->is_dirty = true;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_vol_popup.is_open && g_vol_popup.sb_dragging) {
                    int list_h = g_vol_popup.win_rect.h - 122;
                    int content_h = p->num_tracks * 32 + 8;
                    int max_sc = (content_h > list_h) ? (content_h - list_h) : 0;
                    if (max_sc > 0 && list_h > 0) {
                        int delta_y = my - g_vol_popup.sb_start_my;
                        g_vol_popup.scroll_y = g_vol_popup.sb_orig_scroll + (delta_y * content_h) / list_h;
                        if (g_vol_popup.scroll_y < 0) g_vol_popup.scroll_y = 0;
                        if (g_vol_popup.scroll_y > max_sc) g_vol_popup.scroll_y = max_sc;
                    }
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_time_jump_open && g_time_jump_dragging) {
                    g_time_jump_rect.x = mx - g_time_jump_drag_off_x;
                    g_time_jump_rect.y = my - g_time_jump_drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_lock_popup_open && g_lock_win_dragging) {
                    g_lock_win_rect.x = mx - g_lock_drag_off_x;
                    g_lock_win_rect.y = my - g_lock_drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_mouse_settings_open && g_mouse_win_dragging) {
                    g_mouse_win_rect.x = mx - g_mouse_drag_off_x;
                    g_mouse_win_rect.y = my - g_mouse_drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_autosave.is_open && g_autosave.is_dragging) {
                    g_autosave.win_rect.x = mx - g_autosave.drag_off_x;
                    g_autosave.win_rect.y = my - g_autosave.drag_off_y;
                    SDL_SetCursor(g_cursor_arrow);
                    continue;
                }
                if (g_clip_props.is_open && g_clip_props.resize_edge != 0) {
                    int dx = mx - g_clip_props.resize_start_mx;
                    int dy = my - g_clip_props.resize_start_my;
                    SDL_Rect nr = g_clip_props.resize_orig_rect;
                    if (g_clip_props.resize_edge == VID_RESIZE_RIGHT || g_clip_props.resize_edge == VID_RESIZE_BOTTOM_RIGHT) {
                        nr.w = g_clip_props.resize_orig_rect.w + dx;
                        if (nr.w < 520) nr.w = 520;
                        if (nr.w > win_w - 20) nr.w = win_w - 20;
                    }
                    if (g_clip_props.resize_edge == VID_RESIZE_LEFT || g_clip_props.resize_edge == VID_RESIZE_BOTTOM_LEFT) {
                        int nw = g_clip_props.resize_orig_rect.w - dx;
                        if (nw < 520) nw = 520;
                        if (nw > win_w - 20) nw = win_w - 20;
                        nr.x = (g_clip_props.resize_orig_rect.x + g_clip_props.resize_orig_rect.w) - nw;
                        nr.w = nw;
                    }
                    if (g_clip_props.resize_edge == VID_RESIZE_BOTTOM ||
                        g_clip_props.resize_edge == VID_RESIZE_BOTTOM_LEFT ||
                        g_clip_props.resize_edge == VID_RESIZE_BOTTOM_RIGHT) {
                        nr.h = g_clip_props.resize_orig_rect.h + dy;
                        if (nr.h < 400) nr.h = 400;
                        if (nr.h > win_h - 20) nr.h = win_h - 20;
                    }
                    g_clip_props.win_rect = nr;
                    SDL_SetCursor((g_clip_props.resize_edge == VID_RESIZE_BOTTOM) ? g_cursor_size_ns : g_cursor_size_we);
                    continue;
                }
                if (g_clip_props.is_open && g_clip_props.is_dragging) {
                    g_clip_props.win_rect.x = mx - g_clip_props.drag_off_x;
                    g_clip_props.win_rect.y = my - g_clip_props.drag_off_y;
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
                    int rows_h = (g_action_win.win_rect.h - 266) - 24;
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
                        int rel_y = my - cur_y;
                        bool trk_full_lck = is_track_full_locked_tab(get_valid_tab_idx(), t);
                        // Never show vertical resize cursor over the Lock / M / S / Volume buttons row
                        bool over_buttons = (rel_y >= 30 && rel_y <= 56 && mx >= 6 && mx <= 174);
                        if (!over_buttons && !trk_full_lck && abs(my - (cur_y + th)) <= 3) {
                            SDL_SetCursor(g_cursor_size_ns);
                            hovered_edge = true;
                            break;
                        }
                        if (!trk_full_lck && rel_y >= 6 && rel_y <= 28 && mx >= 8 && mx <= g_daw.tcp_width - 10) {
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
                        int sel_trk_cnt = 0;
                        for (int t = 0; t < p->num_tracks; t++) {
                            if (p->track_selected[t]) sel_trk_cnt++;
                        }
                        if (sel_trk_cnt > 1 && p->track_selected[dragging_track_order_idx]) {
                            // Multi-track drag: shift all selected tracks up or down together maintaining relative order
                            while (dragging_track_order_idx > target_trk) {
                                int top_sel = -1;
                                for (int t = 0; t < p->num_tracks; t++) {
                                    if (p->track_selected[t]) { top_sel = t; break; }
                                }
                                if (top_sel <= 0) break;
                                for (int t = 1; t < p->num_tracks; t++) {
                                    if (p->track_selected[t] && !p->track_selected[t - 1]) {
                                        swap_tracks(p, t, t - 1);
                                    }
                                }
                                dragging_track_order_idx--;
                            }
                            while (dragging_track_order_idx < target_trk) {
                                int bot_sel = -1;
                                for (int t = p->num_tracks - 1; t >= 0; t--) {
                                    if (p->track_selected[t]) { bot_sel = t; break; }
                                }
                                if (bot_sel < 0 || bot_sel >= p->num_tracks - 1) break;
                                for (int t = p->num_tracks - 2; t >= 0; t--) {
                                    if (p->track_selected[t] && !p->track_selected[t + 1]) {
                                        swap_tracks(p, t, t + 1);
                                    }
                                }
                                dragging_track_order_idx++;
                            }
                        } else {
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
                    daw_set_playhead_seconds(p, fmax(0.0, p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second));
                }
                if (dragging_item_idx != -1) {
                    AudioItem *it = &p->items[dragging_item_idx];
                    int tab_lck = get_valid_tab_idx();
                    int orig_trk_lck = g_drag_item_orig_track[dragging_item_idx];
                    bool trk_full_lck = is_track_full_locked_tab(tab_lck, orig_trk_lck);
                    bool trk_lr_lck   = is_track_lr_locked_tab(tab_lck, orig_trk_lck);
                    bool trk_ud_lck   = is_track_ud_locked_tab(tab_lck, orig_trk_lck);

                    double cur_mouse_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                    double delta_t = cur_mouse_time - drag_start_mouse_time;
                    bool xfade_on = is_crossfade_active();

                    if (trk_full_lck && (dragging_fade_in || dragging_fade_out || dragging_trim_left || dragging_trim_right)) {
                        // Full lock prevents fade and edge-trim edits on locked track
                    } else if (dragging_fade_in) {
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
                        int target_trk = g_drag_item_orig_track[dragging_item_idx];
                        int cur_y = header_top_y - p->scroll_y_px;
                        for (int t = 0; t < p->num_tracks; t++) {
                            if (is_track_hidden(t)) continue;
                            if (my >= cur_y && my < cur_y + p->track_heights[t]) { target_trk = t; break; }
                            cur_y += p->track_heights[t];
                        }
                        if (trk_ud_lck || is_track_ud_locked_tab(tab_lck, target_trk)) {
                            target_trk = orig_trk_lck;
                        }
                        double eff_delta_t = trk_lr_lck ? 0.0 : delta_t;

                        int tab_d = get_valid_tab_idx();
                        int my_grp = g_item_group_id[tab_d][dragging_item_idx];
                        int sel_clip_cnt = 0;
                        for (int gi = 0; gi < p->num_items; gi++) {
                            if (p->items[gi].is_selected || (my_grp > 0 && g_item_group_id[tab_d][gi] == my_grp)) {
                                sel_clip_cnt++;
                            }
                        }

                        if (sel_clip_cnt <= 1) {
                            double desired_start = fmax(0.0, item_orig_start + eff_delta_t);
                            it->track_idx = target_trk;
                            if (!xfade_on) {
                                it->start_time = clamp_non_overlapping_start(p, dragging_item_idx, target_trk, desired_start, it->duration);
                            } else {
                                it->start_time = desired_start;
                            }
                        } else {
                            // Multi-clip drag: move all selected & grouped clips together while respecting per-track lock modes
                            double clamped_delta_t = eff_delta_t;
                            int clamped_delta_trk  = target_trk - g_drag_item_orig_track[dragging_item_idx];
                            for (int gi = 0; gi < p->num_items; gi++) {
                                if (p->items[gi].is_selected || (my_grp > 0 && g_item_group_id[tab_d][gi] == my_grp)) {
                                    int g_trk = g_drag_item_orig_track[gi];
                                    if (!is_track_lr_locked_tab(tab_d, g_trk) && g_drag_group_orig_start[gi] + clamped_delta_t < 0.0) {
                                        clamped_delta_t = -g_drag_group_orig_start[gi];
                                    }
                                    if (!is_track_ud_locked_tab(tab_d, g_trk)) {
                                        if (g_trk + clamped_delta_trk < 0) {
                                            clamped_delta_trk = -g_trk;
                                        }
                                        if (g_trk + clamped_delta_trk >= p->num_tracks) {
                                            clamped_delta_trk = (p->num_tracks - 1) - g_trk;
                                        }
                                    }
                                }
                            }
                            for (int gi = 0; gi < p->num_items; gi++) {
                                if (p->items[gi].is_selected || (my_grp > 0 && g_item_group_id[tab_d][gi] == my_grp)) {
                                    int g_trk = g_drag_item_orig_track[gi];
                                    if (!is_track_lr_locked_tab(tab_d, g_trk)) {
                                        p->items[gi].start_time = fmax(0.0, g_drag_group_orig_start[gi] + clamped_delta_t);
                                    }
                                    if (!is_track_ud_locked_tab(tab_d, g_trk)) {
                                        int nt = g_trk + clamped_delta_trk;
                                        if (nt < 0) nt = 0;
                                        if (nt >= p->num_tracks) nt = p->num_tracks - 1;
                                        if (!is_track_ud_locked_tab(tab_d, nt)) {
                                            p->items[gi].track_idx = nt;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!running) break;

        // Refresh active ProjectTab pointer and viewport metrics after event processing
        // (prevents stale `p` pointer and division-by-zero when closing the 3rd/last tab!)
        p = get_cur_tab();
        if (!(p->pixels_per_second >= 0.002f && p->pixels_per_second <= 10000.0f) || !isfinite(p->pixels_per_second)) {
            p->pixels_per_second = 80.0f;
        }
        if (p->num_tracks <= 0 || p->num_tracks > MAX_TRACKS) {
            p->num_tracks = 4;
        }
        m_lane_h = get_marker_height(p);
        view_w = win_w - g_daw.tcp_width - SCROLLBAR_SIZE;
        if (view_w < 64) view_w = 64;
        view_h = win_h - TABBAR_HEIGHT - TOPBAR_HEIGHT - RULER_HEIGHT - m_lane_h - SCROLLBAR_SIZE;
        if (view_h < 64) view_h = 64;
        max_h_tracks = get_total_tracks_height();
        full_content_dur = get_complete_project_duration_sec(p);
        p->project_duration_sec = fmax(300.0, full_content_dur * 1.02);
        total_w_timeline_d = p->project_duration_sec * (double)p->pixels_per_second;
        total_w_timeline = (total_w_timeline_d > 200000000.0) ? 200000000 : (int)total_w_timeline_d;

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

        // ================= REQUIREMENT 10: PROJECT AUTO SAVE (DEFAULT 30-MINUTE CYCLE) =================
        // If project is not saved until cycle time (default 30 minutes), show "Project Auto Saving..." popup and auto-save.
        // When major actions (Dynamic Split, Export, Import, Recording, Script Popup, or active mouse dragging) are in progress,
        // auto-saving waits without disturbing the user and runs immediately after the major process completes!
        if (g_autosave.enabled) {
            Uint32 now_as = SDL_GetTicks();
            Uint32 cyc_ms = (Uint32)(g_autosave.interval_minutes > 0 ? g_autosave.interval_minutes : 30) * 60000U;
            if (now_as >= g_autosave.last_save_ticks && (now_as - g_autosave.last_save_ticks) >= cyc_ms) {
                g_autosave.pending_autosave = true;
            }
            if (g_autosave.pending_autosave) {
                bool major_action_busy = (
                    g_engine_processing || g_processing_popup_open ||
                    g_export.is_open || g_export.is_rendering ||
                    g_multi_import.is_open || g_drop_batch_count > 0 ||
                    g_dyn_split.is_open || g_reposition.is_open || g_script_popup.is_open ||
                    g_rec.is_recording || g_daw.save_prompt_open || g_track_delete_prompt_open ||
                    dragging_item_idx != -1 || marquee_selecting || scrubbing_ruler ||
                    dragging_range_left || dragging_range_right || dragging_range_body ||
                    resizing_track_idx != -1 || dragging_track_order_idx != -1 ||
                    g_tab_needs_project_setup[get_valid_tab_idx()] || !p || p->project_path[0] == '\0'
                );
                if (!major_action_busy) {
                    g_autosave_in_progress = true;
                    for (int ti = 0; ti < g_daw.num_tabs; ti++) {
                        if (g_daw.tabs[ti].project_path[0] != '\0' && !g_tab_needs_project_setup[ti]) {
                            daw_save_project_to_path(&g_daw.tabs[ti], g_daw.tabs[ti].project_path);
                        }
                    }
                    g_autosave_in_progress = false;
                    g_autosave.pending_autosave = false;
                    g_autosave.last_save_ticks = SDL_GetTicks();
                }
            }
        }

        // ================= TOP-TRACK VIDEO SYNCHRONIZATION =================
        if (p->video.is_open) {
            AudioItem *v_clip = get_active_video_clip_at_playhead(p);

            if (v_clip != NULL) {
                double media_internal_time = (p->playhead_seconds - v_clip->start_time) + v_clip->offset_in_file;

                // Load the original media container (file_path), NOT the raw PCM audio cache (raw_audio_path)
                if (strncmp(p->video.current_loaded_path, v_clip->file_path, sizeof(p->video.current_loaded_path)) != 0) {
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
        } else if (p->video.fmt_ctx != NULL || p->video.texture != NULL) {
            close_video(&p->video);
            p->video.is_open = false;
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
        int notif_w = (win_w - 14) - notif_x;
        if (notif_w > 100) {
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

        // Left-side top menu: File | Action | Tools | Edit | Settings | Video
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

        bool tools_active = g_menu_tools_open || g_history_popup_open || g_clip_props.is_open || p->show_marker_lane;
        SDL_Rect btn_tools = { 126, TABBAR_HEIGHT + 5, 58, 26 };
        SDL_SetRenderDrawColor(ren, tools_active ? 62 : 40, tools_active ? 46 : 40, tools_active ? 32 : 40, 255);
        SDL_RenderFillRect(ren, &btn_tools);
        draw_brown_border_rect(ren, &btn_tools, g_menu_tools_open, false);
        draw_text(ren, "Tools", 135, TABBAR_HEIGHT + 13, c_white);

        bool edit_active = g_menu_edit_open || g_color_picker.is_open;
        SDL_Rect btn_edit = { 190, TABBAR_HEIGHT + 5, 54, 26 };
        SDL_SetRenderDrawColor(ren, edit_active ? 62 : 40, edit_active ? 46 : 40, edit_active ? 32 : 40, 255);
        SDL_RenderFillRect(ren, &btn_edit);
        draw_brown_border_rect(ren, &btn_edit, g_menu_edit_open, false);
        draw_text(ren, "Edit", 201, TABBAR_HEIGHT + 13, c_white);

        bool settings_active = g_menu_settings_open || g_action_win.is_open || g_theme.is_open || g_mouse_settings_open || g_autosave.is_open;
        SDL_Rect btn_settings = { 250, TABBAR_HEIGHT + 5, 76, 26 };
        SDL_SetRenderDrawColor(ren, settings_active ? 62 : 40, settings_active ? 46 : 40, settings_active ? 32 : 40, 255);
        SDL_RenderFillRect(ren, &btn_settings);
        draw_brown_border_rect(ren, &btn_settings, g_menu_settings_open, false);
        draw_text(ren, "Settings", 256, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect v_menu = { 332, TABBAR_HEIGHT + 5, 60, 26 };
        SDL_SetRenderDrawColor(ren, p->video.is_open ? 62 : 40, p->video.is_open ? 46 : 40, p->video.is_open ? 32 : 40, 255);
        SDL_RenderFillRect(ren, &v_menu);
        draw_brown_border_rect(ren, &v_menu, p->video.is_open, false);
        draw_text(ren, "Video", 342, TABBAR_HEIGHT + 13, c_white);

        // Center Transport & Time Clock Group:
        // [Play/Pause Symbol] [ Time Clock (Double-click to Jump) ] [ Record ] [ ON/OFF ]
        char clock_buf[32];
        format_timecode(p->playhead_seconds, clock_buf, sizeof(clock_buf));
        int left_menu_end = 400;
        int right_menu_start = win_w - 380;
        int clock_x = left_menu_end + ((right_menu_start - left_menu_end) / 2) - 68;
        if (clock_x < left_menu_end + 82) clock_x = left_menu_end + 82;
        int play_btn_x  = clock_x - 46;
        int vol_btn_x   = play_btn_x - 38;
        int clock_box_x = clock_x - 10;
        int rec_btn_x   = clock_box_x + 128 + 6;
        int rec_onoff_x = rec_btn_x + 52;

        // Requirement 4: Volume Symbol Button near top-left side of Play/Pause button
        ensure_app_vol_defaults_initialized();
        bool app_vol_muted = (g_app_master_vol[get_valid_tab_idx()] <= 0.01f);
        SDL_Rect btn_vol_sym = { vol_btn_x, TABBAR_HEIGHT + 5, 32, 26 };
        SDL_SetRenderDrawColor(ren, g_vol_popup.is_open ? 68 : 42,
                                    g_vol_popup.is_open ? 50 : 40,
                                    g_vol_popup.is_open ? 34 : 38, 255);
        SDL_RenderFillRect(ren, &btn_vol_sym);
        draw_brown_border_rect(ren, &btn_vol_sym, g_vol_popup.is_open, false);
        draw_volume_speaker_symbol_icon(ren, vol_btn_x + 8, TABBAR_HEIGHT + 10, app_vol_muted,
                                        g_vol_popup.is_open ? get_theme_accent_sdl_color() : c_white);

        // Requirement 12: Play/Pause symbol-only button near left side of Time Clock
        SDL_Rect btn_play_sym = { play_btn_x, TABBAR_HEIGHT + 5, 32, 26 };
        SDL_SetRenderDrawColor(ren, p->is_playing ? 68 : 42, p->is_playing ? 50 : 40, p->is_playing ? 34 : 38, 255);
        SDL_RenderFillRect(ren, &btn_play_sym);
        draw_brown_border_rect(ren, &btn_play_sym, p->is_playing, false);
        draw_play_pause_symbol_icon(ren, play_btn_x + 8, TABBAR_HEIGHT + 10, p->is_playing, c_white);

        // Center Time Clock (Requirement 14: Double-click opens manual timings entry popup)
        SDL_Rect clock_bg = { clock_box_x, TABBAR_HEIGHT + 4, 128, 28 };
        SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
        SDL_RenderFillRect(ren, &clock_bg);
        draw_brown_border_rect(ren, &clock_bg, g_rec.is_recording || g_time_jump_open, false);
        draw_text(ren, clock_buf, clock_x - 4, TABBAR_HEIGHT + 12, c_white);

        // Requirement 6: 'Rec' button near next to Time Clock + ON/OFF toggle button (default OFF)
        SDL_Rect btn_rec_top = { rec_btn_x, TABBAR_HEIGHT + 5, 52, 26 };
        SDL_SetRenderDrawColor(ren,
            g_rec.is_recording ? 120 : (g_rec_master_enabled ? 58 : 34),
            g_rec.is_recording ? 26  : (g_rec_master_enabled ? 36 : 32),
            g_rec.is_recording ? 26  : 30, 255);
        SDL_RenderFillRect(ren, &btn_rec_top);
        draw_brown_border_rect(ren, &btn_rec_top, g_rec.is_recording || g_rec_master_enabled, !g_rec_master_enabled);
        draw_filled_circle_sdl(ren, rec_btn_x + 10, TABBAR_HEIGHT + 18, 4,
            g_rec.is_recording ? 255 : (g_rec_master_enabled ? 230 : 120),
            g_rec.is_recording ? 45  : (g_rec_master_enabled ? 45  : 60),
            g_rec.is_recording ? 45  : 60, 255);
        draw_text(ren, "Rec", rec_btn_x + 20, TABBAR_HEIGHT + 13, g_rec_master_enabled ? c_white : c_muted);

        SDL_Rect btn_rec_onoff = { rec_onoff_x, TABBAR_HEIGHT + 5, 42, 26 };
        SDL_SetRenderDrawColor(ren,
            g_rec_master_enabled ? 36  : 48,
            g_rec_master_enabled ? 118 : 34,
            g_rec_master_enabled ? 48  : 32, 255);
        SDL_RenderFillRect(ren, &btn_rec_onoff);
        draw_brown_border_rect(ren, &btn_rec_onoff, g_rec_master_enabled, false);
        draw_text(ren, g_rec_master_enabled ? "ON" : "OFF",
                  rec_onoff_x + (g_rec_master_enabled ? 13 : 9), TABBAR_HEIGHT + 13, c_white);

        // Right-side top menu: [Undo Symbol] | [Redo Symbol] | Range | Crossfade | Save
        SDL_Rect btn_undo = { win_w - 314, TABBAR_HEIGHT + 5, 40, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_undo);
        draw_brown_border_rect(ren, &btn_undo, false, false);
        draw_undo_symbol_icon(ren, btn_undo.x + 11, btn_undo.y + 5, c_white);

        SDL_Rect btn_redo = { win_w - 268, TABBAR_HEIGHT + 5, 40, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_redo);
        draw_brown_border_rect(ren, &btn_redo, false, false);
        draw_redo_symbol_icon(ren, btn_redo.x + 11, btn_redo.y + 5, c_white);

        bool range_on = is_range_active();
        SDL_Rect range_btn = { win_w - 222, TABBAR_HEIGHT + 5, 60, 26 };
        SDL_SetRenderDrawColor(ren, range_on ? 95 : 40, range_on ? 78 : 40, range_on ? 22 : 40, 255);
        SDL_RenderFillRect(ren, &range_btn);
        draw_brown_border_rect(ren, &range_btn, range_on, false);
        draw_text(ren, "Range", win_w - 212, TABBAR_HEIGHT + 13, c_white);

        bool xfade_active = is_crossfade_active();
        SDL_Rect xfade_btn = { win_w - 156, TABBAR_HEIGHT + 5, 90, 26 };
        SDL_SetRenderDrawColor(ren, xfade_active ? 62 : 40, xfade_active ? 46 : 40, xfade_active ? 32 : 40, 255);
        SDL_RenderFillRect(ren, &xfade_btn);
        draw_brown_border_rect(ren, &xfade_btn, xfade_active, false);
        draw_text(ren, "Crossfade", win_w - 147, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect save_btn = { win_w - 60, TABBAR_HEIGHT + 5, 52, 26 };
        SDL_SetRenderDrawColor(ren, p->is_dirty ? 68 : 42, p->is_dirty ? 48 : 42, p->is_dirty ? 32 : 42, 255);
        SDL_RenderFillRect(ren, &save_btn);
        draw_brown_border_rect(ren, &save_btn, p->is_dirty, false);
        draw_text(ren, "Save", win_w - 50, TABBAR_HEIGHT + 13, c_white);

        // 3. Corner Header (+ Track, Main Lock, 'H' Hide Tracks, Master M, Master S — deep dark blue to match TCP)
        int corner_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
        SDL_Rect corner_area = { 0, corner_y, g_daw.tcp_width, RULER_HEIGHT + m_lane_h };
        SDL_SetRenderDrawColor(ren, 7, 13, 28, 255);
        SDL_RenderFillRect(ren, &corner_area);
        draw_brown_border_rect(ren, &corner_area, false, true);

        SDL_Rect btn_corner_add = { 8, corner_y + 2, 68, 24 };
        SDL_SetRenderDrawColor(ren, 48, 48, 48, 255);
        SDL_RenderFillRect(ren, &btn_corner_add);
        draw_brown_border_rect(ren, &btn_corner_add, false, false);
        draw_text(ren, "+ Track", 14, corner_y + 9, c_white);

        int c_tab = get_valid_tab_idx();
        bool all_trks_locked = (p->num_tracks > 0);
        bool any_trk_locked  = false;
        bool all_trks_muted  = (p->num_tracks > 0);
        bool all_trks_soloed = (p->num_tracks > 0);
        for (int t = 0; t < p->num_tracks; t++) {
            if (!g_track_locked[c_tab][t]) all_trks_locked = false;
            else any_trk_locked = true;
            if (!p->tracks[t].mute) all_trks_muted = false;
            if (!p->tracks[t].solo) all_trks_soloed = false;
        }

        // Main Lock symbol button right next to '+ Track'
        (void)any_trk_locked;
        bool main_lock_active = all_trks_locked || g_lock_opt_full[c_tab] || g_lock_opt_prevent_lr[c_tab] || g_lock_opt_prevent_ud[c_tab];
        SDL_Rect btn_master_lock = { 80, corner_y + 2, 26, 24 };
        SDL_SetRenderDrawColor(ren, main_lock_active ? 155 : 44,
                                    main_lock_active ? 86  : 44,
                                    32, 255);
        SDL_RenderFillRect(ren, &btn_master_lock);
        draw_brown_border_rect(ren, &btn_master_lock, main_lock_active || g_lock_popup_open, false);
        draw_lock_symbol_icon(ren, 87, corner_y + 7, main_lock_active,
                              main_lock_active ? get_theme_accent_sdl_color() : c_white);

        // 'H' (Hide Tracks) button right next to Main Lock symbol
        bool hide_active = g_hide_popup_open || has_any_hidden_track(p);
        SDL_Rect btn_corner_hide = { 110, corner_y + 2, 26, 24 };
        SDL_SetRenderDrawColor(ren, hide_active ? 155 : 44, hide_active ? 86 : 44, 32, 255);
        SDL_RenderFillRect(ren, &btn_corner_hide);
        draw_brown_border_rect(ren, &btn_corner_hide, hide_active, false);
        draw_text(ren, "H", 119, corner_y + 9, hide_active ? get_theme_accent_sdl_color() : c_white);

        SDL_Rect btn_master_m = { 140, corner_y + 2, 26, 24 };
        SDL_SetRenderDrawColor(ren, all_trks_muted ? 185 : 44, all_trks_muted ? 42 : 44, 44, 255);
        SDL_RenderFillRect(ren, &btn_master_m);
        draw_brown_border_rect(ren, &btn_master_m, all_trks_muted, false);
        draw_text(ren, "M", 149, corner_y + 9, c_white);

        SDL_Rect btn_master_s = { 170, corner_y + 2, 26, 24 };
        SDL_SetRenderDrawColor(ren, all_trks_soloed ? 185 : 44, all_trks_soloed ? 142 : 44, 44, 255);
        SDL_RenderFillRect(ren, &btn_master_s);
        draw_brown_border_rect(ren, &btn_master_s, all_trks_soloed, false);
        draw_text(ren, "S", 179, corner_y + 9, c_white);

        // 4. Time Ruler & Logic Pro Yellow Cycle Range Strip (Requirement 10: Bright & Center-Aligned Over Vertical Tick Mark)
        SDL_Rect ruler = { g_daw.tcp_width, corner_y, view_w, RULER_HEIGHT };
        SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
        SDL_RenderFillRect(ren, &ruler);
        draw_brown_border_rect(ren, &ruler, false, true);

        if (!(p->pixels_per_second >= 0.002f && p->pixels_per_second <= 10000.0f) || !isfinite(p->pixels_per_second)) {
            p->pixels_per_second = 80.0f;
        }
        if (!isfinite(p->scroll_x_sec) || p->scroll_x_sec < 0.0) {
            p->scroll_x_sec = 0.0;
        }
        double min_px_gap = 90.0;
        double sec_per_tick = min_px_gap / (double)p->pixels_per_second;
        double nice_steps[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0, 7200.0 };
        int num_steps = sizeof(nice_steps) / sizeof(nice_steps[0]);
        double chosen_step = nice_steps[num_steps - 1];
        for (int s = 0; s < num_steps; s++) {
            if (nice_steps[s] >= sec_per_tick) { chosen_step = nice_steps[s]; break; }
        }
        if (!(chosen_step > 0.01) || !isfinite(chosen_step)) chosen_step = 2.0;

        double start_tick = floor(p->scroll_x_sec / chosen_step) * chosen_step;
        double end_tick = p->scroll_x_sec + (double)view_w / (double)p->pixels_per_second + chosen_step;
        if (!isfinite(start_tick) || start_tick < 0.0) start_tick = 0.0;
        if (!isfinite(end_tick) || end_tick < start_tick) end_tick = start_tick + chosen_step * 20.0;

        SDL_RenderSetClipRect(ren, &ruler);
        int ruler_tick_guard = 0;
        for (double t = start_tick; t <= end_tick && ruler_tick_guard < 512; t += chosen_step, ruler_tick_guard++) {
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

            // Center-align time number horizontally over vertical tick mark rx, bright as standard button text (c_white)
            int tw = (int)strlen(t_str) * 8;
            int tx = rx - (tw / 2);
            if (tx < g_daw.tcp_width + 2) tx = g_daw.tcp_width + 2;
            draw_text(ren, t_str, tx, corner_y + 3, c_white);

            // Bright vertical tick mark centered right below the time number
            SDL_SetRenderDrawColor(ren, c_white.r, c_white.g, c_white.b, 255);
            SDL_RenderDrawLine(ren, rx, corner_y + 16, rx, corner_y + RULER_HEIGHT - 1);
        }
        SDL_RenderSetClipRect(ren, NULL);

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
                    daw_draw_text_edit_overlay(ren, g_editing_marker_full_buf, mk_x + 6, m_tag.y, m_tag.h, m_tag.x + m_tag.w - 3);
                    draw_text_clipped(ren, disp_txt, mk_x + 6, marker_y + 6, c_white, &txt_bounds, &marker_clip);
                }
            }

            SDL_RenderSetClipRect(ren, NULL);
        }

        int header_top_y = marker_y + m_lane_h;
        SDL_Rect timeline_clip = { g_daw.tcp_width, header_top_y, view_w, view_h };

        // Compute 64-bit state hash of all static timeline + waveform parameters for GPU texture caching (REAPER-style 0-1% CPU playback)
        static SDL_Texture *g_timeline_cache_tex = NULL;
        static int          g_timeline_cache_w = 0;
        static int          g_timeline_cache_h = 0;
        static uint64_t     g_timeline_cache_hash = 0;

        uint64_t tl_hash = 1469598103934665603ULL;
        #define TL_HASH_MIX(val) do { uint64_t _v = (uint64_t)(val); tl_hash ^= _v; tl_hash *= 1099511628211ULL; } while (0)
        TL_HASH_MIX(g_daw.active_tab);
        TL_HASH_MIX(win_w);
        TL_HASH_MIX(win_h);
        TL_HASH_MIX(view_w);
        TL_HASH_MIX(view_h);
        TL_HASH_MIX(g_daw.tcp_width);
        TL_HASH_MIX(header_top_y);
        TL_HASH_MIX((int64_t)(p->scroll_x_sec * 10000.0));
        TL_HASH_MIX(p->scroll_y_px);
        TL_HASH_MIX((int64_t)(p->pixels_per_second * 1000.0f));
        TL_HASH_MIX(p->num_tracks);
        TL_HASH_MIX(p->num_items);
        TL_HASH_MIX(g_universal_peak_color_idx);
        TL_HASH_MIX(g_theme.r); TL_HASH_MIX(g_theme.g); TL_HASH_MIX(g_theme.b); TL_HASH_MIX(g_theme.a);
        TL_HASH_MIX(g_dyn_split.is_open ? 1 : 0);
        TL_HASH_MIX(g_dyn_split.display_gate_threshold ? 1 : 0);
        TL_HASH_MIX((int)(g_dyn_split.gate_threshold_db * 10.0f));
        TL_HASH_MIX(dragging_item_idx);
        TL_HASH_MIX(dragging_fade_in ? 1 : 0);
        TL_HASH_MIX(dragging_fade_out ? 1 : 0);
        TL_HASH_MIX(g_rec.is_recording ? 1 : 0);
        TL_HASH_MIX(g_rec.rec_frames_written / 2048);
        TL_HASH_MIX(range_on ? 1 : 0);
        if (range_on) {
            TL_HASH_MIX((int64_t)(get_range_start() * 1000.0));
            TL_HASH_MIX((int64_t)(get_range_end() * 1000.0));
        }
        int cur_tab_tl = get_valid_tab_idx();
        for (int t_h = 0; t_h < p->num_tracks; t_h++) {
            TL_HASH_MIX(p->track_heights[t_h]);
            TL_HASH_MIX((int)(p->track_db[t_h] * 20.0f));
            TL_HASH_MIX(p->track_selected[t_h] ? 1 : 0);
            TL_HASH_MIX(g_track_hidden[cur_tab_tl][t_h] ? 1 : 0);
            TL_HASH_MIX(g_track_color_idx[cur_tab_tl][t_h]);
        }
        for (int i_h = 0; i_h < p->num_items; i_h++) {
            AudioItem *it_h = &p->items[i_h];
            TL_HASH_MIX(it_h->track_idx);
            TL_HASH_MIX((int64_t)(it_h->start_time * 1000.0));
            TL_HASH_MIX((int64_t)(it_h->offset_in_file * 1000.0));
            TL_HASH_MIX((int64_t)(it_h->duration * 1000.0));
            TL_HASH_MIX(it_h->peak_count);
            TL_HASH_MIX(it_h->is_selected ? 1 : 0);
            TL_HASH_MIX((int64_t)(g_item_fade_in[cur_tab_tl][i_h] * 1000.0));
            TL_HASH_MIX((int64_t)(g_item_fade_out[cur_tab_tl][i_h] * 1000.0));
            TL_HASH_MIX((int)(g_item_vol_db[cur_tab_tl][i_h] * 20.0f));
            TL_HASH_MIX(g_item_muted[cur_tab_tl][i_h] ? 1 : 0);
            TL_HASH_MIX(g_item_color_idx[cur_tab_tl][i_h]);
            TL_HASH_MIX(g_item_group_id[cur_tab_tl][i_h]);
        }
        #undef TL_HASH_MIX

        if (win_w > 0 && win_h > 0 && (!g_timeline_cache_tex || g_timeline_cache_w != win_w || g_timeline_cache_h != win_h)) {
            if (g_timeline_cache_tex) SDL_DestroyTexture(g_timeline_cache_tex);
            g_timeline_cache_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, win_w, win_h);
            g_active_tl_cache_tex = g_timeline_cache_tex;
            g_timeline_cache_w = win_w;
            g_timeline_cache_h = win_h;
            g_timeline_cache_hash = 0;
        }

        bool use_tl_target = false;
        bool need_tl_redraw = true;
        if (g_timeline_cache_tex) {
            if (tl_hash == g_timeline_cache_hash && g_timeline_cache_hash != 0) {
                need_tl_redraw = false;
            } else if (SDL_SetRenderTarget(ren, g_timeline_cache_tex) == 0) {
                use_tl_target = true;
            }
        }

        if (need_tl_redraw) {
        SDL_RenderSetClipRect(ren, &timeline_clip);
        SDL_SetRenderDrawColor(ren, 31, 31, 33, 255);
        SDL_RenderFillRect(ren, &timeline_clip);

        // Subtle vertical timeline grid lines in faded brown
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        uint8_t th_r, th_g, th_b;
        get_theme_effective_rgb(&th_r, &th_g, &th_b);
        int grid_tick_guard = 0;
        for (double t = start_tick; t <= end_tick && grid_tick_guard < 512; t += chosen_step, grid_tick_guard++) {
            int gx = g_daw.tcp_width + (int)((t - p->scroll_x_sec) * p->pixels_per_second);
            if (gx < g_daw.tcp_width || gx > g_daw.tcp_width + view_w) continue;
            SDL_SetRenderDrawColor(ren, (uint8_t)((th_r * 58) / 100), (uint8_t)((th_g * 57) / 100), (uint8_t)((th_b * 63) / 100), 65);
            SDL_RenderDrawLine(ren, gx, header_top_y, gx, header_top_y + view_h);
        }

        // Timeline tracks: Render custom Track Color lane tint + theme border
        int track_cur_y = header_top_y - p->scroll_y_px;
        int cur_tab_lane = get_valid_tab_idx();
        for (int i = 0; i < p->num_tracks; i++) {
            if (is_track_hidden(i)) continue;
            int th = p->track_heights[i];
            SDL_Rect lane_border = { g_daw.tcp_width, track_cur_y, view_w, th };
            int trk_c = g_track_color_idx[cur_tab_lane][i] % NUM_STUDIO_COLORS;
            if (trk_c > 0) {
                SDL_SetRenderDrawColor(ren, g_studio_pal_bg[trk_c][0], g_studio_pal_bg[trk_c][1], g_studio_pal_bg[trk_c][2], 55);
                SDL_RenderFillRect(ren, &lane_border);
            }
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

        // 5. Render Clips (REAPER-Style 2-Section Layout: Top Dark Name Bar + Border + Bottom Peaks Section)
        for (int i = 0; i < p->num_items; i++) {
            AudioItem *it = &p->items[i];
            if (is_track_hidden(it->track_idx)) continue;
            int iy = get_track_y_pos(it->track_idx) + 2;
            int ih = p->track_heights[it->track_idx] - 4;
            double ix_d = (double)g_daw.tcp_width + (it->start_time - p->scroll_x_sec) * (double)p->pixels_per_second;
            double iw_d = it->duration * (double)p->pixels_per_second;
            if (iw_d < 2.0) iw_d = 2.0;
            if (ix_d + iw_d < (double)g_daw.tcp_width || ix_d > (double)(g_daw.tcp_width + view_w)) continue;

            int vis_x1 = (ix_d < (double)(g_daw.tcp_width - 4)) ? (g_daw.tcp_width - 4) : (int)ix_d;
            int vis_x2 = (ix_d + iw_d > (double)(g_daw.tcp_width + view_w + 4)) ? (g_daw.tcp_width + view_w + 4) : (int)(ix_d + iw_d);
            int vis_w  = vis_x2 - vis_x1;
            if (vis_w <= 0) continue;

            int hdr_h = (ih > 26) ? 18 : 0;
            int peaks_y = iy + hdr_h;
            int peaks_h = ih - hdr_h;

            int cur_t_i = get_valid_tab_idx();
            int c_idx = g_item_color_idx[cur_t_i][i] % NUM_STUDIO_COLORS;
            if (c_idx == 0 && it->track_idx >= 0 && it->track_idx < MAX_TRACKS && g_track_color_idx[cur_t_i][it->track_idx] > 0) {
                c_idx = g_track_color_idx[cur_t_i][it->track_idx] % NUM_STUDIO_COLORS;
            }
            uint8_t br   = g_studio_pal_bg[c_idx][0] + (it->is_selected ? 16 : 0);
            uint8_t bg_c = g_studio_pal_bg[c_idx][1] + (it->is_selected ? 12 : 0);
            uint8_t bb   = g_studio_pal_bg[c_idx][2] + (it->is_selected ? 10 : 0);

            if (hdr_h > 0) {
                // Section 1: Dark background for top clip name strip
                SDL_Rect hdr_r = { vis_x1, iy, vis_w, hdr_h };
                SDL_SetRenderDrawColor(ren, it->is_selected ? 30 : 20, it->is_selected ? 28 : 19, it->is_selected ? 26 : 18, 255);
                SDL_RenderFillRect(ren, &hdr_r);
            }

            // Section 2: Peaks section background below the name strip
            SDL_Rect peaks_r = { vis_x1, peaks_y, vis_w, peaks_h };
            SDL_SetRenderDrawColor(ren, br, bg_c, bb, 255);
            SDL_RenderFillRect(ren, &peaks_r);
        }

        // Pass 1B: Render Waveforms strictly inside Section 2 (Peaks section) so peaks never overlap the top name section
        for (int i = 0; i < p->num_items; i++) {
            AudioItem *it = &p->items[i];
            if (is_track_hidden(it->track_idx)) continue;
            int iy = get_track_y_pos(it->track_idx) + 2;
            int ih = p->track_heights[it->track_idx] - 4;
            double ix_d = (double)g_daw.tcp_width + (it->start_time - p->scroll_x_sec) * (double)p->pixels_per_second;
            double iw_d = it->duration * (double)p->pixels_per_second;
            if (iw_d < 2.0) iw_d = 2.0;
            if (ix_d + iw_d < (double)g_daw.tcp_width || ix_d > (double)(g_daw.tcp_width + view_w)) continue;

            int ix = (ix_d < -1000000.0) ? -1000000 : (int)ix_d;
            int iw = (iw_d > 2000000.0) ? 2000000 : (int)iw_d;
            int vis_x1 = (ix < g_daw.tcp_width) ? g_daw.tcp_width : ix;
            int vis_x2 = (ix + iw > g_daw.tcp_width + view_w) ? (g_daw.tcp_width + view_w) : (ix + iw);

            int hdr_h = (ih > 26) ? 18 : 0;
            int peaks_y = iy + hdr_h;
            int peaks_h = ih - hdr_h;

            int cur_tab_w = get_valid_tab_idx();
            float gain_multiplier = g_item_muted[cur_tab_w][i]
                ? 0.15f
                : db_to_linear(p->track_db[it->track_idx] + g_item_vol_db[cur_tab_w][i]);
            int mid_y = peaks_y + peaks_h / 2;

            SDL_SetRenderDrawColor(ren, 96, 66, 42, 255);
            SDL_RenderDrawLine(ren, vis_x1, mid_y, vis_x2, mid_y);

            if (it->peaks && it->peak_count > 0 && vis_x2 > vis_x1) {
                double bins_per_sec = (double)SAMPLE_RATE / (double)PEAK_SAMPLES_PER_BIN;
                int pk_c = g_universal_peak_color_idx % NUM_STUDIO_COLORS;
                if (pk_c < 0) pk_c = 0;
                uint8_t pkr = g_studio_pal_peak[pk_c][0];
                uint8_t pkg = g_studio_pal_peak[pk_c][1];
                uint8_t pkb = g_studio_pal_peak[pk_c][2];

                float half_amp = (float)(peaks_h / 2 - 2);
                if (half_amp < 2.0f) half_amp = 2.0f;
                int min_wave_y = peaks_y + 1;

                bool clip_has_xfade_or_fade = (get_item_fade_in(p, i) > 0.001) || (get_item_fade_out(p, i) > 0.001);
                if (!clip_has_xfade_or_fade) {
                    for (int o = 0; o < p->num_items; o++) {
                        if (o == i) continue;
                        AudioItem *ot = &p->items[o];
                        if (ot->track_idx != it->track_idx) continue;
                        double ov_s = fmax(it->start_time, ot->start_time);
                        double ov_e = fmin(it->start_time + it->duration, ot->start_time + ot->duration);
                        if (ov_e > ov_s + 0.0005) { clip_has_xfade_or_fade = true; break; }
                    }
                }

                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                for (int px = vis_x1; px < vis_x2; px++) {
                    double rel_sec_0 = ((double)px - ix_d) / (double)p->pixels_per_second;
                    double rel_sec_1 = ((double)(px + 1) - ix_d) / (double)p->pixels_per_second;
                    if (rel_sec_0 < 0.0) rel_sec_0 = 0.0;
                    double timeline_sec = it->start_time + rel_sec_0;

                    double exact_b0 = (it->offset_in_file + rel_sec_0) * bins_per_sec;
                    double exact_b1 = (it->offset_in_file + rel_sec_1) * bins_per_sec;
                    int b0 = (int)exact_b0;
                    int b1 = (int)exact_b1;
                    if (b0 < 0) b0 = 0;
                    if (b0 >= it->peak_count) continue;
                    if (b1 < b0) b1 = b0;
                    if (b1 >= it->peak_count) b1 = it->peak_count - 1;

                    float raw_mn = it->peaks[b0].min_val;
                    float raw_mx = it->peaks[b0].max_val;
                    float sum_mn = 0.0f;
                    float sum_mx = 0.0f;
                    int cnt_b = 0;

                    int span = b1 - b0;
                    int step = (span > 64) ? (span / 64) : 1;
                    for (int b = b0; b <= b1; b += step) {
                        float v_mn = it->peaks[b].min_val;
                        float v_mx = it->peaks[b].max_val;
                        if (v_mn < raw_mn) raw_mn = v_mn;
                        if (v_mx > raw_mx) raw_mx = v_mx;
                        sum_mn += (v_mn < 0.0f) ? v_mn : 0.0f;
                        sum_mx += (v_mx > 0.0f) ? v_mx : 0.0f;
                        cnt_b++;
                    }

                    // When multiple bins fall in one pixel column, blend extrema with mean energy
                    // so glottal valleys and up/down modulation remain distinct instead of merging into a heavy block
                    float avg_mn = (cnt_b > 0) ? (sum_mn / (float)cnt_b) : raw_mn;
                    float avg_mx = (cnt_b > 0) ? (sum_mx / (float)cnt_b) : raw_mx;
                    float draw_top_v = (raw_mx > 0.0f) ? raw_mx : 0.0f;
                    float draw_bot_v = (raw_mn < 0.0f) ? raw_mn : 0.0f;
                    if (span >= 2) {
                        draw_top_v = 0.55f * draw_top_v + 0.45f * avg_mx;
                        draw_bot_v = 0.55f * draw_bot_v + 0.45f * avg_mn;
                    }

                    float xfade_env = clip_has_xfade_or_fade ? compute_item_crossfade_gain(p, i, timeline_sec) : 1.0f;
                    float env_gain = gain_multiplier * xfade_env;

                    int y_top = mid_y - (int)(draw_top_v * env_gain * half_amp + 0.5f);
                    int y_bot = mid_y - (int)(draw_bot_v * env_gain * half_amp - 0.5f);

                    if (y_top < min_wave_y) y_top = min_wave_y;
                    if (y_bot > iy + ih - 3) y_bot = iy + ih - 3;
                    if (y_top > mid_y) y_top = mid_y;
                    if (y_bot < mid_y) y_bot = mid_y;

                    // REAPER-style crisp vertical peak bar (no diagonal valley bridging, no heavy double-fill)
                    Uint8 bar_alpha = (xfade_env < 0.999f) ? 165 : 210;
                    SDL_SetRenderDrawColor(ren, pkr, pkg, pkb, bar_alpha);
                    if (y_bot > y_top) {
                        SDL_RenderDrawLine(ren, px, y_top, px, y_bot);
                    } else {
                        SDL_RenderDrawPoint(ren, px, mid_y);
                    }

                    // Crisp 1px peak tip highlight at upper & lower extrema for effortless up/down reading
                    if (y_bot - y_top >= 3) {
                        SDL_SetRenderDrawColor(ren, pkr, pkg, pkb, 255);
                        SDL_RenderDrawPoint(ren, px, y_top);
                        SDL_RenderDrawPoint(ren, px, y_bot);
                    }
                }
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            }

            // Render manual Clip Fade-In and Fade-Out curves & shaded upper exterior regions
            double fin_sec  = get_item_fade_in(p, i);
            double fout_sec = get_item_fade_out(p, i);
            int fin_px  = (int)(fin_sec  * p->pixels_per_second);
            int fout_px = (int)(fout_sec * p->pixels_per_second);
            if (fin_px > iw) fin_px = iw;
            if (fout_px > iw) fout_px = iw;

            int top_pad = peaks_y + 1;
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
                    if (cur_x >= g_daw.tcp_width && cur_x <= g_daw.tcp_width + view_w && y_in > peaks_y) {
                        SDL_SetRenderDrawColor(ren, 10, 12, 16, 130);
                        SDL_RenderDrawLine(ren, cur_x, peaks_y + 1, cur_x, y_in);
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
                    if (cur_x >= g_daw.tcp_width && cur_x <= g_daw.tcp_width + view_w && y_out > peaks_y) {
                        SDL_SetRenderDrawColor(ren, 10, 12, 16, 130);
                        SDL_RenderDrawLine(ren, cur_x, peaks_y + 1, cur_x, y_out);
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
                int dy_thr = (int)(thr_lin * (peaks_h / 2.0f - 2));
                SDL_SetRenderDrawColor(ren, 80, 235, 120, 220);
                SDL_RenderDrawLine(ren, ix, mid_y - dy_thr, ix + iw, mid_y - dy_thr);
                SDL_RenderDrawLine(ren, ix, mid_y + dy_thr, ix + iw, mid_y + dy_thr);
            }

            // Border for the 2 clip sections (Top Name Section & Bottom Peaks Section)
            if (hdr_h > 0 && vis_x2 > vis_x1) {
                uint8_t bdr_r, bdr_g, bdr_b;
                get_theme_effective_rgb(&bdr_r, &bdr_g, &bdr_b);
                if (!it->is_selected) {
                    bdr_r = (uint8_t)((bdr_r * 74) / 100);
                    bdr_g = (uint8_t)((bdr_g * 71) / 100);
                    bdr_b = (uint8_t)((bdr_b * 75) / 100);
                }
                SDL_SetRenderDrawColor(ren, bdr_r, bdr_g, bdr_b, 255);
                SDL_RenderDrawLine(ren, vis_x1, peaks_y, vis_x2 - 1, peaks_y);
            }

            // Outer clip border (thicker when clip is selected)
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
                SDL_Rect clip_text_bounds = { ix + 8, iy + 1, iw - 16, (hdr_h > 0) ? (hdr_h - 1) : (ih - 2) };
                draw_text_clipped(ren, full_clip_lbl, ix + 10, iy + 3, c_white, &clip_text_bounds, &timeline_clip);
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
                int hdr_h = (ih > 26) ? 18 : 0;
                int peaks_y = iy + hdr_h;
                int ox = g_daw.tcp_width + (int)((overlap_s - p->scroll_x_sec) * p->pixels_per_second);
                int ox_end = g_daw.tcp_width + (int)((overlap_e - p->scroll_x_sec) * p->pixels_per_second);
                int ow = ox_end - ox;
                if (ow < 2) ow = 2;

                if (ox + ow < g_daw.tcp_width || ox > g_daw.tcp_width + view_w) continue;

                SDL_Rect xf_box = { ox, peaks_y, ow, ih - hdr_h };
                SDL_SetRenderDrawColor(ren, 185, 120, 55, 32);
                SDL_RenderFillRect(ren, &xf_box);

                int top_pad = peaks_y + 2;
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
                    if (cur_x >= g_daw.tcp_width && cur_x <= g_daw.tcp_width + view_w && y_top_env > peaks_y) {
                        SDL_SetRenderDrawColor(ren, 12, 14, 18, 115);
                        SDL_RenderDrawLine(ren, cur_x, peaks_y + 1, cur_x, y_top_env);
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

                SDL_Rect h_tl = { ox, peaks_y, 5, 5 };
                SDL_Rect h_tr = { ox + ow - 5, peaks_y, 5, 5 };
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
        if (use_tl_target) {
            SDL_RenderSetClipRect(ren, NULL);
            SDL_SetRenderTarget(ren, NULL);
            g_timeline_cache_hash = tl_hash;
        }
        } // end if (need_tl_redraw)

        if (g_timeline_cache_tex && g_timeline_cache_hash == tl_hash) {
            SDL_RenderSetClipRect(ren, NULL);
            SDL_RenderCopy(ren, g_timeline_cache_tex, &timeline_clip, &timeline_clip);
        }
        SDL_RenderSetClipRect(ren, &timeline_clip);

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

        // 6. Track Header (TCP) — Deep Dark Blue Studio Background
        SDL_Rect tcp_clip = { 0, header_top_y, g_daw.tcp_width, view_h };
        SDL_RenderSetClipRect(ren, &tcp_clip);
        SDL_SetRenderDrawColor(ren, 5, 10, 24, 255);
        SDL_RenderFillRect(ren, &tcp_clip);
        for (int i = 0; i < p->num_tracks; i++) {
            if (is_track_hidden(i)) continue;
            int ty = get_track_y_pos(i);
            int th = p->track_heights[i];
            bool is_sel = p->track_selected[i];
            bool is_dragging_this_trk = (dragging_track_order_idx == i);

            int trk_col = g_track_color_idx[get_valid_tab_idx()][i] % NUM_STUDIO_COLORS;
            if (trk_col < 0) trk_col = 0;
            uint8_t tcp_r = (trk_col > 0) ? ((g_studio_pal_bg[trk_col][0] * 70) / 100 + (is_sel ? 14 : 0)) : (is_dragging_this_trk ? 18 : (is_sel ? 14 : 8));
            uint8_t tcp_g = (trk_col > 0) ? ((g_studio_pal_bg[trk_col][1] * 70) / 100 + (is_sel ? 10 : 0)) : (is_dragging_this_trk ? 34 : (is_sel ? 26 : 16));
            uint8_t tcp_b = (trk_col > 0) ? ((g_studio_pal_bg[trk_col][2] * 70) / 100 + (is_sel ? 8 : 0))  : (is_dragging_this_trk ? 74 : (is_sel ? 58 : 38));
            SDL_Rect tp = { 1, ty + 1, g_daw.tcp_width - 2, th - 2 };
            SDL_SetRenderDrawColor(ren, tcp_r, tcp_g, tcp_b, 255);
            SDL_RenderFillRect(ren, &tp);
            if (trk_col > 0) {
                SDL_Rect col_strip = { 2, ty + 2, 5, th - 4 };
                SDL_SetRenderDrawColor(ren, g_studio_pal_hdr[trk_col][0], g_studio_pal_hdr[trk_col][1], g_studio_pal_hdr[trk_col][2], 255);
                SDL_RenderFillRect(ren, &col_strip);
            }

            // Every track has a simple identical faded brown edge; selected/dragged track edge is slightly thicker
            if (is_dragging_this_trk || is_sel) {
                draw_brown_border_rect(ren, &tp, true, false);
            } else {
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 118, 78, 46, 145);
                SDL_RenderDrawRect(ren, &tp);
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            }

            // Top Part: Track Name Box (expands cleanly without right-side drag symbol)
            SDL_Rect name_box = { 8, ty + 6, g_daw.tcp_width - 18, 22 };
            bool is_editing = (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME && g_daw.editing_track_name_idx == i);

            SDL_SetRenderDrawColor(ren, 10, 10, 12, 255);
            SDL_RenderFillRect(ren, &name_box);
            draw_brown_border_rect(ren, &name_box, is_editing, false);

            SDL_Rect name_txt_bounds = { name_box.x + 2, name_box.y + 1, name_box.w - 4, name_box.h - 2 };
            if (is_editing) {
                daw_draw_text_edit_overlay(ren, g_daw.editing_name_buf, 12, name_box.y, name_box.h, name_box.x + name_box.w - 4);
                draw_text_clipped(ren, g_daw.editing_name_buf, 12, ty + 10, c_white, &name_txt_bounds, &tcp_clip);
            } else {
                draw_text_clipped(ren, p->track_names[i], 12, ty + 10, c_white, &name_txt_bounds, &tcp_clip);
            }

            // Bottom Part: Controls ([-][dB][+]  [Lock][M][S])
            int bot_y = ty + 34;

            // DB Controls
            SDL_Rect btn_minus = { 8, bot_y, 18, 20 };
            SDL_SetRenderDrawColor(ren, 32, 32, 34, 255);
            SDL_RenderFillRect(ren, &btn_minus);
            draw_brown_border_rect(ren, &btn_minus, false, false);
            draw_text(ren, "-", 14, bot_y + 4, c_white);

            char db_txt[16];
            bool trk_is_inf = (g_track_inf_db[get_valid_tab_idx()][i] || p->track_db[i] <= -60.0f);
            if (trk_is_inf) {
                snprintf(db_txt, sizeof(db_txt), "-inf");
            } else {
                snprintf(db_txt, sizeof(db_txt), "%+.1f", p->track_db[i]);
            }
            draw_text(ren, db_txt, trk_is_inf ? 32 : 29, bot_y + 5, trk_is_inf ? get_theme_accent_sdl_color() : c_white);

            SDL_Rect btn_plus = { 72, bot_y, 18, 20 };
            SDL_SetRenderDrawColor(ren, 32, 32, 34, 255);
            SDL_RenderFillRect(ren, &btn_plus);
            draw_brown_border_rect(ren, &btn_plus, false, false);
            draw_text(ren, "+", 77, bot_y + 4, c_white);

            // Individual Track Lock Symbol Button on the LEFT side of 'M'
            bool trk_is_locked = g_track_locked[get_valid_tab_idx()][i];
            SDL_Rect btn_trk_lock = { 96, bot_y, 22, 20 };
            SDL_SetRenderDrawColor(ren, trk_is_locked ? 155 : 32, trk_is_locked ? 86 : 32, 34, 255);
            SDL_RenderFillRect(ren, &btn_trk_lock);
            draw_brown_border_rect(ren, &btn_trk_lock, trk_is_locked, false);
            draw_lock_symbol_icon(ren, 96 + 5, bot_y + 3, trk_is_locked,
                                  trk_is_locked ? get_theme_accent_sdl_color() : c_white);

            // M & S Buttons
            SDL_Rect m_btn = { 122, bot_y, 22, 20 };
            SDL_SetRenderDrawColor(ren, p->tracks[i].mute ? 180 : 32, p->tracks[i].mute ? 40 : 32, 34, 255);
            SDL_RenderFillRect(ren, &m_btn);
            draw_brown_border_rect(ren, &m_btn, p->tracks[i].mute, false);
            draw_text(ren, "M", 129, bot_y + 4, c_white);

            SDL_Rect s_btn = { 148, bot_y, 22, 20 };
            SDL_SetRenderDrawColor(ren, p->tracks[i].solo ? 180 : 32, p->tracks[i].solo ? 140 : 32, 34, 255);
            SDL_RenderFillRect(ren, &s_btn);
            draw_brown_border_rect(ren, &s_btn, p->tracks[i].solo, false);
            draw_text(ren, "S", 155, bot_y + 4, c_white);
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

        // 7. Action Menu (Submenus: Scripts, Dynamic split, Reposition, Infinity -dB)
        if (g_daw.menu_action_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            SDL_Rect a_box = { 60, m_start, 198, 108 };
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, &a_box);
            draw_brown_border_rect(ren, &a_box, true, false);
            draw_text(ren, "Scripts", 68, m_start + 7, c_white);
            draw_text(ren, "Dynamic split", 68, m_start + 34, c_white);
            draw_text(ren, "Reposition", 68, m_start + 61, c_white);
            bool inf_on = is_selected_track_infinity_db_on(p);
            draw_text(ren, inf_on ? "Infinity -dB [ON]" : "Infinity -dB [OFF]",
                      68, m_start + 88, inf_on ? get_theme_accent_sdl_color() : c_white);
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

            // Top Filter Search Box + [Clear] + [Pin] (Up/Down removed)
            int top_y = sr->y + 34;
            int flt_w = sr->w - 204;
            if (flt_w < 120) flt_w = 120;
            int flt_x = sr->x + 56;
            int clr_x = flt_x + flt_w + 8;
            int pin_x = clr_x + 60;

            draw_text(ren, "Filter", sr->x + 12, top_y + 6, c_white);
            SDL_Rect s_flt_box = { flt_x, top_y, flt_w, 24 };
            SDL_SetRenderDrawColor(ren, 15, 14, 13, 255);
            SDL_RenderFillRect(ren, &s_flt_box);
            draw_brown_border_rect(ren, &s_flt_box, g_script_filter_focused, false);

            SDL_Rect s_flt_clip = { s_flt_box.x + 4, s_flt_box.y + 2, s_flt_box.w - 8, s_flt_box.h - 4 };
            if (g_script_filter_focused) {
                daw_draw_text_edit_overlay(ren, g_script_filter_text, s_flt_box.x + 6, s_flt_box.y, s_flt_box.h, s_flt_box.x + s_flt_box.w - 4);
            }
            draw_text_clipped(ren, g_script_filter_text, s_flt_box.x + 6, top_y + 5, c_white, &s_flt_clip, NULL);

            SDL_Rect b_s_clr = { clr_x, top_y, 54, 24 };
            SDL_SetRenderDrawColor(ren, 44, 38, 34, 255);
            SDL_RenderFillRect(ren, &b_s_clr);
            draw_brown_border_rect(ren, &b_s_clr, false, false);
            draw_text(ren, "Clear", clr_x + 7, top_y + 5, c_white);

            bool sel_sc_pinned = (g_daw.script_selected_idx >= 0 && g_daw.script_selected_idx < g_daw.num_scripts &&
                                  g_script_pinned[g_daw.script_selected_idx]);
            SDL_Rect b_s_pin = { pin_x, top_y, 64, 24 };
            SDL_SetRenderDrawColor(ren, sel_sc_pinned ? 74 : 44, sel_sc_pinned ? 52 : 38, sel_sc_pinned ? 30 : 34, 255);
            SDL_RenderFillRect(ren, &b_s_pin);
            draw_brown_border_rect(ren, &b_s_pin, sel_sc_pinned, false);
            draw_pin_symbol_icon(ren, pin_x + 7, top_y + 5, sel_sc_pinned, sel_sc_pinned ? c_accent : c_white);
            draw_text(ren, "Pin", pin_x + 24, top_y + 5, c_white);

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

        // Tools Menu (Submenus: Marker, History, Clip properties)
        if (g_menu_tools_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            SDL_Rect t_box = { 126, m_start, 156, 81 };
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, &t_box);
            draw_brown_border_rect(ren, &t_box, true, false);

            draw_text(ren, p->show_marker_lane ? "Marker [ON]" : "Marker", 136, m_start + 7, c_white);
            draw_text(ren, "History", 136, m_start + 34, c_white);
            draw_text(ren, "Clip properties", 136, m_start + 61, c_white);
        }

        // Edit Menu (Submenus: Color, Join (Glue))
        if (g_menu_edit_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            SDL_Rect e_box = { 190, m_start, 156, 54 };
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, &e_box);
            draw_brown_border_rect(ren, &e_box, true, false);

            draw_text(ren, "Color", 200, m_start + 7, c_white);
            draw_text(ren, "Join (Glue)", 200, m_start + 34, c_white);
        }

        // Settings Menu (Submenus: Keyboard Shortcut, Themes, Mouse, Auto save)
        if (g_menu_settings_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            SDL_Rect s_box = { 250, m_start, 184, 108 };
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, &s_box);
            draw_brown_border_rect(ren, &s_box, true, false);

            draw_text(ren, "Keyboard Shortcut", 260, m_start + 7, c_white);
            draw_text(ren, "Themes", 260, m_start + 34, c_white);
            draw_text(ren, "Mouse", 260, m_start + 61, c_white);
            draw_text(ren, "Auto save", 260, m_start + 88, c_white);
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

        // Floating / Fullscreen Video Monitor (when unpinned inside the app; when pinned, renders in external OS window outside app)
        if (p->video.is_open && !g_video_pinned_global) {
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


            // Requirement 7: Pin Symbol Button on Video Monitor (Pin Active = visible globally outside app)
            int pin_btn_x = p->video.rect.x + p->video.rect.w - 68;
            int pin_btn_y = p->video.rect.y + 3;
            SDL_Rect pin_btn = { pin_btn_x, pin_btn_y, 18, 18 };
            SDL_SetRenderDrawColor(ren, g_video_pinned_global ? 78 : 48, g_video_pinned_global ? 54 : 40, 34, 255);
            SDL_RenderFillRect(ren, &pin_btn);
            draw_brown_border_rect(ren, &pin_btn, g_video_pinned_global, false);
            draw_pin_symbol_icon(ren, pin_btn_x + 4, pin_btn_y + 2, g_video_pinned_global,
                                 g_video_pinned_global ? get_theme_accent_sdl_color() : c_white);

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
            }
            draw_brown_border_rect(ren, &v_canvas, false, false);

        }

        // ================= UNIFIED EXPORT MEDIA POPUP WINDOW =================
        if (g_export.is_open) {
            render_export_popup_window(ren, p);
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

        // Edit -> Track color / Clip color Picker Popup Window
        if (g_color_picker.is_open) {
            render_color_picker_window(ren, p);
        }

        // Edit -> Selected Clip Property Details Popup Window (Ctrl+I)
        if (g_clip_props.is_open) {
            render_clip_properties_window(ren, p);
        }

        // Lua & Python Custom Script Popup Window
        if (g_script_popup.is_open) {
            render_script_popup_window(ren);
        }

        // Settings -> Mouse Popup Window (Requirement 9)
        if (g_mouse_settings_open) {
            render_mouse_settings_window(ren);
        }

        // Settings -> Auto Save Popup Window (Requirement 10)
        if (g_autosave.is_open) {
            render_auto_save_settings_window(ren);
        }

        // Main Lock Two-Section Popup Window (All Track | Selected track)
        if (g_lock_popup_open) {
            render_track_lock_popup_window(ren);
        }

        // Double-Click Time Clock Manual Timings Entry Popup (Requirement 14)
        if (g_time_jump_open) {
            render_time_jump_popup_window(ren);
        }

        // Tiny Range Timings Popup Window near Range Button (Requirement 2)
        if (g_range_popup.is_open) {
            render_range_popup_window(ren, win_w);
        }

        // App Hearing Volume Popup Window near Play/Pause Button (Requirement 4)
        if (g_vol_popup.is_open) {
            render_app_volume_popup_window(ren, p);
        }

        // Requirement 5: New Project Tab 2-Option Modal ("Open Project File" & "Create Project File")
        if (g_tab_needs_project_setup[get_valid_tab_idx()] || p->project_path[0] == '\0') {
            SDL_Rect dim_overlay = { 0, TABBAR_HEIGHT, win_w, win_h - TABBAR_HEIGHT };
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 185);
            SDL_RenderFillRect(ren, &dim_overlay);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);

            int mw = 620, mh = 210;
            int mx_box = (win_w - mw) / 2;
            int my_box = (win_h - mh) / 2;

            SDL_Rect mbox = { mx_box, my_box, mw, mh };
            SDL_SetRenderDrawColor(ren, 28, 25, 22, 255);
            SDL_RenderFillRect(ren, &mbox);
            draw_brown_border_rect(ren, &mbox, true, false);

            SDL_Rect mhdr = { mx_box, my_box, mw, 32 };
            SDL_SetRenderDrawColor(ren, 48, 36, 28, 255);
            SDL_RenderFillRect(ren, &mhdr);
            draw_brown_border_rect(ren, &mhdr, false, false);
            draw_text(ren, "New Project Tab - Select Project Option", mx_box + 16, my_box + 10, c_white);
            if (g_daw.num_tabs > 1) {
                draw_text(ren, "X", mx_box + mw - 20, my_box + 10, c_white);
            }

            draw_text(ren, "Choose an option to start working in this project tab:", mx_box + 24, my_box + 52, c_white);
            draw_text(ren, "Creating a project creates <Project>/, <Project>.ehs & <Project>/media/", mx_box + 24, my_box + 78, c_muted);

            int btn_y = my_box + 118;
            // Option 1: [ Open Project File ]
            SDL_Rect b_open = { mx_box + 56, btn_y, 240, 42 };
            SDL_SetRenderDrawColor(ren, 46, 58, 72, 255);
            SDL_RenderFillRect(ren, &b_open);
            draw_brown_border_rect(ren, &b_open, true, false);
            draw_text(ren, "Open Project File", b_open.x + 52, btn_y + 15, c_white);

            // Option 2: [ Create Project File ]
            SDL_Rect b_create = { mx_box + 324, btn_y, 240, 42 };
            SDL_SetRenderDrawColor(ren, 82, 54, 32, 255);
            SDL_RenderFillRect(ren, &b_create);
            draw_brown_border_rect(ren, &b_create, true, false);
            draw_text(ren, "Create Project File", b_create.x + 44, btn_y + 15, c_white);

            draw_text(ren, "All imported, recorded & glued media are stored in <Project>/media/", mx_box + 24, my_box + mh - 26, c_muted);
        }

        // Sync & render Global Pinned Video Monitor window if active (Requirement 7)
        sync_and_render_external_pinned_video_window(p, win);

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

            int dia_w = 430, dia_h = 140;
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
    if (g_bg_media_thread) {
        SDL_WaitThread(g_bg_media_thread, NULL);
        g_bg_media_thread = NULL;
    }
    if (g_bg_media_mutex) {
        SDL_DestroyMutex(g_bg_media_mutex);
        g_bg_media_mutex = NULL;
    }
    if (g_daw.audio_device > 0) SDL_CloseAudioDevice(g_daw.audio_device);
    for (int t = 0; t < g_daw.num_tabs; t++) {
        close_video(&g_daw.tabs[t].video);
        for (int i = 0; i < g_daw.tabs[t].num_items; i++) {
            if (g_daw.tabs[t].items[i].raw_file_handle) {
                fclose(g_daw.tabs[t].items[i].raw_file_handle);
                g_daw.tabs[t].items[i].raw_file_handle = NULL;
            }
            daw_free_peaks_if_unpooled(g_daw.tabs[t].items[i].peaks);
            g_daw.tabs[t].items[i].peaks = NULL;
        }
    }
    daw_cleanup_temp_media_for_tab(-1, true);
    for (int sp = 0; sp < MAX_SHARED_PEAK_ENTRIES; sp++) {
        if (g_shared_peak_pool[sp].in_use && g_shared_peak_pool[sp].peaks) {
            free(g_shared_peak_pool[sp].peaks);
            g_shared_peak_pool[sp].peaks = NULL;
            g_shared_peak_pool[sp].in_use = false;
        }
    }
    SDL_FreeCursor(g_cursor_arrow);
    SDL_FreeCursor(g_cursor_size_we);
    SDL_FreeCursor(g_cursor_size_ns);
    SDL_FreeCursor(g_cursor_ibeam);
    destroy_external_pinned_video_window();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
