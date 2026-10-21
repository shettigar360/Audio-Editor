#ifndef DAW_TABS_SELF_HEALING_GUARD
#define DAW_TABS_SELF_HEALING_GUARD

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

#ifndef DAW_CORE_STRUCTS_DEFINED
#define DAW_CORE_STRUCTS_DEFINED
#ifndef DAW_TYPES_H
#define DAW_TYPES_H
#endif

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
    bool is_dirty;

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

    bool save_prompt_open;
    int save_prompt_tab_idx;
    bool save_prompt_quit_app;

    DawScript scripts[MAX_SCRIPTS];
    int num_scripts;
    bool script_manager_open;
    bool script_manager_is_python;
    int script_selected_idx;
    SDL_Rect script_win_rect;
    bool script_win_dragging;
    int script_drag_offset_x;
    int script_drag_offset_y;

    bool menu_file_open;
    bool menu_action_open;
    bool submenu_export_audio_open;
    bool submenu_export_video_open;

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
#endif // DAW_CORE_STRUCTS_DEFINED

#endif // DAW_TABS_SELF_HEALING_GUARD

#if !defined(DAW_COMPILING_MAIN_C) && !defined(DAW_TABS_IMPLEMENTATION_DONE)
#define DAW_TABS_IMPLEMENTATION_DONE
#include "daw_tabs.h"
#include "project_io.h"


void init_tab_defaults(int idx, const char *title) {
    ProjectTab *p = &g_daw.tabs[idx];
    memset(p, 0, sizeof(ProjectTab));
    snprintf(p->project_name, sizeof(p->project_name), "%s", title);
    p->num_tracks = 4;
    p->pixels_per_second = 80.0f;
    p->project_duration_sec = 300.0;
    p->show_marker_lane = false; // Disabled by default
    p->is_dirty = false;
    p->video = (SimpleVideoPlayer){ .rect = { 650, 160, 480, 310 }, .is_open = false, .video_stream_idx = -1 };

    for (int t = 0; t < MAX_TRACKS; t++) {
        p->tracks[t].id = t + 1;
        p->tracks[t].volume = 1.0f;
        p->track_db[t] = 0.0f;
        p->track_heights[t] = 84;
        snprintf(p->track_names[t], sizeof(p->track_names[t]), "Track %d", t + 1);
    }
}

void force_close_tab(int idx) {
    if (idx < 0 || idx >= g_daw.num_tabs) return;

    if (g_daw.num_tabs <= 1) {
        ProjectTab *p = &g_daw.tabs[0];
        close_video(&p->video);
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].raw_file_handle) {
                fclose(p->items[i].raw_file_handle);
                p->items[i].raw_file_handle = NULL;
            }
            daw_free_peaks_if_unpooled(p->items[i].peaks);
            p->items[i].peaks = NULL;
        }
        init_tab_defaults(0, "Project 1");
        g_daw.active_tab = 0;
        return;
    }

    ProjectTab *target = &g_daw.tabs[idx];
    close_video(&target->video);
    for (int i = 0; i < target->num_items; i++) {
        if (target->items[i].raw_file_handle) {
            fclose(target->items[i].raw_file_handle);
            target->items[i].raw_file_handle = NULL;
        }
        daw_free_peaks_if_unpooled(target->items[i].peaks);
        target->items[i].peaks = NULL;
    }

    for (int t = idx; t < g_daw.num_tabs - 1; t++) {
        g_daw.tabs[t] = g_daw.tabs[t + 1];
    }
    g_daw.num_tabs--;

    if (g_daw.active_tab > idx) {
        g_daw.active_tab--;
    } else if (g_daw.active_tab >= g_daw.num_tabs) {
        g_daw.active_tab = g_daw.num_tabs - 1;
    }
}

void request_close_tab(int idx) {
    if (idx < 0 || idx >= g_daw.num_tabs) return;

    if (g_daw.tabs[idx].is_dirty) {
        g_daw.save_prompt_open = true;
        g_daw.save_prompt_tab_idx = idx;
        g_daw.save_prompt_quit_app = false;
    } else {
        force_close_tab(idx);
    }
}

void handle_save_modal_choice(int choice, bool *running) {
    int t_idx = g_daw.save_prompt_tab_idx;
    bool quit_after = g_daw.save_prompt_quit_app;

    // Close the popup window
    g_daw.save_prompt_open = false;
    g_daw.save_prompt_tab_idx = -1;
    g_daw.save_prompt_quit_app = false;

    if (choice == 1) {
        // [Save]: Save project and return back to project (do not exit/close)
        if (t_idx >= 0 && t_idx < g_daw.num_tabs) {
            save_project_file();
            g_daw.tabs[t_idx].is_dirty = false;
        }
    } else if (choice == 2) {
        // [Don't Save]: Discard and exit/close
        if (quit_after) {
            if (running) *running = false;
        } else {
            force_close_tab(t_idx);
        }
    }
    // choice == 3 ([Cancel]): Discard modal and return back to project
}
#endif
