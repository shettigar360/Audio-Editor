#ifndef DAW_TYPES_H
#define DAW_TYPES_H
#define DAW_CORE_STRUCTS_DEFINED

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "video_player.h"
#include "audio_engine.h"

#define TABBAR_HEIGHT 30
#define TOPBAR_HEIGHT 36
#define RULER_HEIGHT 28
#define MARKER_BAR_HEIGHT 24
#define SCROLLBAR_SIZE 16
#define MAX_UNDO_STACK 32
#define MAX_SCRIPTS 64
#define MAX_TABS 8
#define MAX_MARKERS 64

typedef enum {
    INPUT_FOCUS_NONE = 0,
    INPUT_FOCUS_TRACK_NAME,
    INPUT_FOCUS_MARKER
} InputFocusType;

typedef struct {
    double time_sec;
    char text[64];
} DawMarker;

typedef struct {
    char name[64];
    char filepath[260];
    char shortcut[16];
    bool is_python;
} DawScript;

typedef struct {
    AudioItem items[MAX_ITEMS];
    int num_items;
    int num_tracks;
} DawSnapshot;

typedef struct {
    DawSnapshot history[MAX_UNDO_STACK];
    int head;
    int count;
} UndoRedoManager;

typedef struct {
    char project_name[64];
    char project_path[512];
    bool is_dirty;                      // True if unsaved changes exist

    Track tracks[MAX_TRACKS];
    char track_names[MAX_TRACKS][64];
    float track_db[MAX_TRACKS];
    int track_heights[MAX_TRACKS];
    bool track_selected[MAX_TRACKS];
    int num_tracks;

    AudioItem items[MAX_ITEMS];
    int num_items;
    double playhead_seconds;
    bool is_playing;
    double scroll_x_sec;
    int scroll_y_px;
    float pixels_per_second;
    double project_duration_sec;

    DawMarker markers[MAX_MARKERS];
    int num_markers;
    bool show_marker_lane;

    SimpleVideoPlayer video;
    UndoRedoManager undo_mgr;
} ProjectTab;

typedef struct {
    ProjectTab tabs[MAX_TABS];
    int num_tabs;
    int active_tab;
    int tcp_width;

    SDL_AudioDeviceID audio_device;

    // Save Confirmation Modal
    bool save_prompt_open;              // Modal visibility
    int save_prompt_tab_idx;            // Tab index pending close confirmation (-1 if none)
    bool save_prompt_quit_app;          // True if user clicked window 'X' (quit) instead of tab 'x'

    // Scripts
    DawScript scripts[MAX_SCRIPTS];
    int num_scripts;
    bool script_manager_open;
    bool script_manager_is_python;
    int script_selected_idx;
    SDL_Rect script_win_rect;
    bool script_win_dragging;
    int script_drag_offset_x;
    int script_drag_offset_y;

    // Menus
    bool menu_file_open;
    bool menu_action_open;
    bool submenu_export_audio_open;
    bool submenu_export_video_open;

    // Exclusive Text Box Input State
    InputFocusType input_focus;
    int editing_track_name_idx;
    char editing_name_buf[64];
    int editing_marker_idx;
    char editing_marker_buf[64];
} DawEngine;

extern DawEngine g_daw;

static inline ProjectTab* get_cur_tab(void) {
    if (g_daw.num_tabs <= 0) return NULL;
    if (g_daw.active_tab < 0) g_daw.active_tab = 0;
    if (g_daw.active_tab >= g_daw.num_tabs) g_daw.active_tab = g_daw.num_tabs - 1;
    return &g_daw.tabs[g_daw.active_tab];
}

static inline float db_to_linear(float db) {
    if (db <= -60.0f) return 0.0f;
    return powf(10.0f, db / 20.0f);
}

static inline void format_timecode(double seconds, char *out, size_t max_len) {
    if (seconds < 0.0) seconds = 0.0;
    int h = (int)(seconds / 3600);
    int m = (int)((seconds - h * 3600) / 60);
    int s = (int)(seconds - h * 3600 - m * 60);
    int ms = (int)((seconds - (int)seconds) * 1000);
    snprintf(out, max_len, "%02d:%02d:%02d.%03d", h, m, s, ms);
}

static inline void push_undo_state(void) {
    ProjectTab *p = get_cur_tab();
    if (!p) return;

    if (p->undo_mgr.head < MAX_UNDO_STACK - 1) {
        p->undo_mgr.head++;
    } else {
        for (int i = 0; i < MAX_UNDO_STACK - 1; i++) {
            p->undo_mgr.history[i] = p->undo_mgr.history[i + 1];
        }
    }
    DawSnapshot *snap = &p->undo_mgr.history[p->undo_mgr.head];
    snap->num_items = p->num_items;
    snap->num_tracks = p->num_tracks;
    memcpy(snap->items, p->items, sizeof(AudioItem) * p->num_items);
    p->undo_mgr.count = p->undo_mgr.head + 1;
    p->is_dirty = true;
}

static inline void perform_undo(void) {
    ProjectTab *p = get_cur_tab();
    if (!p) return;

    if (p->undo_mgr.head > 0) {
        p->undo_mgr.head--;
        DawSnapshot *snap = &p->undo_mgr.history[p->undo_mgr.head];
        p->num_items = snap->num_items;
        p->num_tracks = snap->num_tracks;
        memcpy(p->items, snap->items, sizeof(AudioItem) * snap->num_items);
        for (int i = 0; i < p->num_items; i++) {
            if (!p->items[i].raw_file_handle) {
                p->items[i].raw_file_handle = fopen(p->items[i].raw_audio_path, "rb");
            }
        }
        p->is_dirty = true;
    }
}

static inline void perform_redo(void) {
    ProjectTab *p = get_cur_tab();
    if (!p) return;

    if (p->undo_mgr.head + 1 < p->undo_mgr.count) {
        p->undo_mgr.head++;
        DawSnapshot *snap = &p->undo_mgr.history[p->undo_mgr.head];
        p->num_items = snap->num_items;
        p->num_tracks = snap->num_tracks;
        memcpy(p->items, snap->items, sizeof(AudioItem) * snap->num_items);
        for (int i = 0; i < p->num_items; i++) {
            if (!p->items[i].raw_file_handle) {
                p->items[i].raw_file_handle = fopen(p->items[i].raw_audio_path, "rb");
            }
        }
        p->is_dirty = true;
    }
}

#endif // DAW_TYPES_H
