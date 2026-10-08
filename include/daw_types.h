#ifndef DAW_TYPES_H
#define DAW_TYPES_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "video_player.h"
#include "audio_engine.h"

#define RULER_HEIGHT 34
#define TOPBAR_HEIGHT 38
#define TCP_WIDTH 190
#define SCROLLBAR_SIZE 16
#define MAX_UNDO_STACK 32

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
    Track tracks[MAX_TRACKS];
    float track_db[MAX_TRACKS];
    bool track_selected[MAX_TRACKS];
    int num_tracks;
    AudioItem items[MAX_ITEMS];
    int num_items;
    double playhead_seconds;
    bool is_playing;
    double scroll_x_sec;
    int scroll_y_px;
    float pixels_per_second;
    int track_height;
    double project_duration_sec;
    SimpleVideoPlayer video;
    SDL_AudioDeviceID audio_device;
    UndoRedoManager undo_mgr;

    // Menu States
    bool menu_file_open;
    bool submenu_export_audio_open;
    bool submenu_export_video_open;
} DawEngine;

extern DawEngine g_daw;

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
    if (g_daw.undo_mgr.head < MAX_UNDO_STACK - 1) {
        g_daw.undo_mgr.head++;
    } else {
        for (int i = 0; i < MAX_UNDO_STACK - 1; i++) {
            g_daw.undo_mgr.history[i] = g_daw.undo_mgr.history[i + 1];
        }
    }
    DawSnapshot *snap = &g_daw.undo_mgr.history[g_daw.undo_mgr.head];
    snap->num_items = g_daw.num_items;
    snap->num_tracks = g_daw.num_tracks;
    memcpy(snap->items, g_daw.items, sizeof(AudioItem) * g_daw.num_items);
    g_daw.undo_mgr.count = g_daw.undo_mgr.head + 1;
}

static inline void perform_undo(void) {
    if (g_daw.undo_mgr.head > 0) {
        g_daw.undo_mgr.head--;
        DawSnapshot *snap = &g_daw.undo_mgr.history[g_daw.undo_mgr.head];
        g_daw.num_items = snap->num_items;
        g_daw.num_tracks = snap->num_tracks;
        memcpy(g_daw.items, snap->items, sizeof(AudioItem) * snap->num_items);
        for (int i = 0; i < g_daw.num_items; i++) {
            if (!g_daw.items[i].raw_file_handle) {
                g_daw.items[i].raw_file_handle = fopen(g_daw.items[i].raw_audio_path, "rb");
            }
        }
    }
}

static inline void perform_redo(void) {
    if (g_daw.undo_mgr.head + 1 < g_daw.undo_mgr.count) {
        g_daw.undo_mgr.head++;
        DawSnapshot *snap = &g_daw.undo_mgr.history[g_daw.undo_mgr.head];
        g_daw.num_items = snap->num_items;
        g_daw.num_tracks = snap->num_tracks;
        memcpy(g_daw.items, snap->items, sizeof(AudioItem) * snap->num_items);
        for (int i = 0; i < g_daw.num_items; i++) {
            if (!g_daw.items[i].raw_file_handle) {
                g_daw.items[i].raw_file_handle = fopen(g_daw.items[i].raw_audio_path, "rb");
            }
        }
    }
}

#endif // DAW_TYPES_H
