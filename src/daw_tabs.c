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
#include <stddef.h>
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

    InputFocusType input_focus;
    int editing_track_name_idx;
    char editing_name_buf[64];
    int editing_marker_idx;
    char editing_marker_buf[64];

    bool menu_file_open;
    bool menu_action_open;

    DawScript scripts[MAX_SCRIPTS];
    int num_scripts;
    bool script_manager_open;
    SDL_Rect script_win_rect;
    bool script_win_dragging;
    int script_drag_offset_x;
    int script_drag_offset_y;
    int script_selected_idx;
} DawEngine;

extern DawEngine g_daw;

static inline ProjectTab* get_cur_tab(void) {
    if (g_daw.num_tabs <= 0) g_daw.num_tabs = 1;
    if (g_daw.num_tabs > MAX_TABS) g_daw.num_tabs = MAX_TABS;
    if (g_daw.active_tab < 0) g_daw.active_tab = 0;
    if (g_daw.active_tab >= g_daw.num_tabs) g_daw.active_tab = g_daw.num_tabs - 1;
    return &g_daw.tabs[g_daw.active_tab];
}

static inline float db_to_linear(float db) {
    if (db <= -60.0f) return 0.0f;
    return powf(10.0f, db / 20.0f);
}

static inline int get_marker_height(ProjectTab *p) {
    return (p && p->show_marker_lane) ? MARKER_BAR_HEIGHT : 0;
}

static inline int get_track_y_offset(int target_track_idx) {
    ProjectTab *p = get_cur_tab();
    int y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT + get_marker_height(p) - p->scroll_y_px;
    for (int i = 0; i < target_track_idx && i < p->num_tracks; i++) {
        y += p->track_heights[i];
    }
    return y;
}

static inline int get_total_tracks_height(void) {
    ProjectTab *p = get_cur_tab();
    int total = 0;
    for (int i = 0; i < p->num_tracks; i++) total += p->track_heights[i];
    return total;
}

static inline int get_track_at_mouse_y(int my) {
    ProjectTab *p = get_cur_tab();
    int cur_y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT + get_marker_height(p) - p->scroll_y_px;
    for (int i = 0; i < p->num_tracks; i++) {
        if (my >= cur_y && my < cur_y + p->track_heights[i]) return i;
        cur_y += p->track_heights[i];
    }
    return -1;
}

static inline void push_undo_state(void) {
    ProjectTab *p = get_cur_tab();
    UndoRedoManager *um = &p->undo_mgr;
    um->head = (um->head + 1) % MAX_UNDO_STACK;
    if (um->count < MAX_UNDO_STACK) um->count++;

    DawSnapshot *snap = &um->history[um->head];
    snap->num_items = p->num_items;
    snap->num_tracks = p->num_tracks;
    memcpy(snap->items, p->items, sizeof(AudioItem) * p->num_items);
    p->is_dirty = true;
}

static inline void perform_undo(void) {
    ProjectTab *p = get_cur_tab();
    UndoRedoManager *um = &p->undo_mgr;
    if (um->count > 0 && um->head >= 0) {
        DawSnapshot *snap = &um->history[um->head];
        p->num_items = snap->num_items;
        p->num_tracks = snap->num_tracks;
        memcpy(p->items, snap->items, sizeof(AudioItem) * snap->num_items);
        for (int i = 0; i < p->num_items; i++) {
            if (!p->items[i].raw_file_handle) {
                p->items[i].raw_file_handle = fopen(p->items[i].raw_audio_path, "rb");
            }
        }
        um->head--;
        if (um->head < 0 && um->count == MAX_UNDO_STACK) um->head = MAX_UNDO_STACK - 1;
        um->count--;
        p->is_dirty = true;
    }
}

static inline void perform_redo(void) {
    ProjectTab *p = get_cur_tab();
    UndoRedoManager *um = &p->undo_mgr;
    int next_head = (um->head + 1) % MAX_UNDO_STACK;
    if (um->count < MAX_UNDO_STACK && um->history[next_head].num_tracks > 0) {
        um->head = next_head;
        um->count++;
        DawSnapshot *snap = &um->history[um->head];
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

extern void daw_cleanup_temp_media_for_tab(int tab_idx, bool delete_all_temp_files);
extern void daw_shift_per_tab_arrays_on_close(int closed_tab_idx, bool was_last_single_tab);

void init_tab_defaults(int idx, const char *title) {
    if (idx < 0 || idx >= MAX_TABS) return;
    ProjectTab *p = &g_daw.tabs[idx];
    memset(p, 0, offsetof(ProjectTab, undo_mgr));
    p->undo_mgr.head = -1;
    p->undo_mgr.count = 0;
    snprintf(p->project_name, sizeof(p->project_name), "%s", title ? title : "Project");
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

    // Close video outside audio lock so FFmpeg cleanup never delays the real-time audio callback
    close_video(&g_daw.tabs[idx].video);

    // Lock audio device before closing file handles or shifting tabs so the real-time
    // audio callback thread never reads a freed FILE* or mid-shift ProjectTab!
    if (g_daw.audio_device > 0) SDL_LockAudioDevice(g_daw.audio_device);

    if (g_daw.num_tabs <= 1) {
        ProjectTab *p = &g_daw.tabs[0];
        p->is_playing = false;
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
        if (g_daw.audio_device > 0) SDL_UnlockAudioDevice(g_daw.audio_device);

        daw_shift_per_tab_arrays_on_close(0, true);
        return;
    }

    ProjectTab *target = &g_daw.tabs[idx];
    target->is_playing = false;
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
    int vacated_slot = g_daw.num_tabs - 1;
    // Initialize vacated_slot with safe defaults (pixels_per_second = 80.0f, num_tracks = 4)
    // so no slot in g_daw.tabs[] ever has pixels_per_second == 0.0f!
    init_tab_defaults(vacated_slot, "Project");
    g_daw.num_tabs--;

    if (g_daw.active_tab > idx) {
        g_daw.active_tab--;
    } else if (g_daw.active_tab >= g_daw.num_tabs) {
        g_daw.active_tab = g_daw.num_tabs - 1;
    }
    if (g_daw.active_tab < 0) g_daw.active_tab = 0;

    if (g_daw.audio_device > 0) SDL_UnlockAudioDevice(g_daw.audio_device);

    // Shift all per-tab arrays in main.c in lockstep with g_daw.tabs[]
    daw_shift_per_tab_arrays_on_close(idx, false);
}

void request_close_tab(int idx) {
    if (idx < 0 || idx >= g_daw.num_tabs) return;

    // An unconfigured tab with no project path and 0 items has no unsaved edits
    if (g_daw.tabs[idx].project_path[0] == '\0' && g_daw.tabs[idx].num_items == 0) {
        g_daw.tabs[idx].is_dirty = false;
    }

    if (g_daw.tabs[idx].is_dirty) {
        g_daw.active_tab = idx;
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
        // [Save]: Handled directly by main.c save dialog + post-save continuation
        if (t_idx >= 0 && t_idx < g_daw.num_tabs) {
            save_project_file();
            g_daw.tabs[t_idx].is_dirty = false;
        }
    } else if (choice == 2) {
        // [Don't Save]: Discard unsaved changes for t_idx
        if (quit_after) {
            if (t_idx >= 0 && t_idx < g_daw.num_tabs) {
                g_daw.tabs[t_idx].is_dirty = false;
            }
            // Check if any other open tab still has unsaved changes before exiting the app
            for (int t = 0; t < g_daw.num_tabs; t++) {
                if (g_daw.tabs[t].project_path[0] == '\0' && g_daw.tabs[t].num_items == 0) {
                    g_daw.tabs[t].is_dirty = false;
                }
                if (g_daw.tabs[t].is_dirty) {
                    g_daw.active_tab = t;
                    g_daw.save_prompt_open = true;
                    g_daw.save_prompt_tab_idx = t;
                    g_daw.save_prompt_quit_app = true;
                    return;
                }
            }
            daw_cleanup_temp_media_for_tab(-1, true);
            if (running) *running = false;
        } else {
            force_close_tab(t_idx);
        }
    }
    // choice == 3 ([Cancel]): Discard modal and return back to project
}
#endif
