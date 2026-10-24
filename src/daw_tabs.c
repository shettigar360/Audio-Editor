#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "video_player.h"
#include "audio_engine.h"
#include "daw_types.h"
#include "daw_tabs.h"
#include "project_io.h"

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
    p->show_marker_lane = false;
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
    if (idx < 0 || idx >= g_daw.num_tabs || idx >= MAX_TABS) return;

    // 1. Stop any active recording or external pinned video window for this tab
    daw_prepare_tab_close_state(idx);
    close_video(&g_daw.tabs[idx].video);

    // 2. Lock audio device while closing clip file handles and shifting tabs
    if (g_daw.audio_device > 0) SDL_LockAudioDevice(g_daw.audio_device);

    ProjectTab *target = &g_daw.tabs[idx];
    target->is_playing = false;
    for (int i = 0; i < target->num_items && i < MAX_ITEMS; i++) {
        if (target->items[i].raw_file_handle) {
            fclose(target->items[i].raw_file_handle);
            target->items[i].raw_file_handle = NULL;
        }
        if (target->items[i].peaks) {
            if (!daw_is_pooled_peaks(target->items[i].peaks)) {
                char anon_key[64];
                snprintf(anon_key, sizeof(anon_key), "__unpooled_pk_%p", (void *)target->items[i].peaks);
                daw_register_shared_peaks(
                    target->items[i].raw_audio_path[0] ? target->items[i].raw_audio_path : anon_key,
                    target->items[i].peaks,
                    target->items[i].peak_count,
                    target->items[i].file_total_duration,
                    target->items[i].has_video
                );
            }
            target->items[i].peaks = NULL;
        }
    }
    target->num_items = 0;

    if (g_daw.num_tabs <= 1) {
        init_tab_defaults(0, "Project 1");
        g_daw.num_tabs = 1;
        g_daw.active_tab = 0;
        if (g_daw.audio_device > 0) SDL_UnlockAudioDevice(g_daw.audio_device);

        daw_shift_per_tab_arrays_on_close(0, true);
        daw_evict_unused_shared_peaks_after_tab_close();
        return;
    }

    // Shift only the active ProjectTab fields (1.2 MB instead of 41.2 MB UndoRedoManager)
    for (int t = idx; t < g_daw.num_tabs - 1; t++) {
        memmove(&g_daw.tabs[t], &g_daw.tabs[t + 1], offsetof(ProjectTab, undo_mgr));
        g_daw.tabs[t].undo_mgr.head = -1;
        g_daw.tabs[t].undo_mgr.count = 0;
    }
    int vacated_slot = g_daw.num_tabs - 1;
    init_tab_defaults(vacated_slot, "Project");
    g_daw.num_tabs--;

    if (g_daw.active_tab > idx) {
        g_daw.active_tab--;
    } else if (g_daw.active_tab >= g_daw.num_tabs) {
        g_daw.active_tab = g_daw.num_tabs - 1;
    }
    if (g_daw.active_tab < 0) g_daw.active_tab = 0;

    if (g_daw.audio_device > 0) SDL_UnlockAudioDevice(g_daw.audio_device);

    daw_shift_per_tab_arrays_on_close(idx, false);
    daw_evict_unused_shared_peaks_after_tab_close();
}

void request_close_tab(int idx) {
    if (idx < 0 || idx >= g_daw.num_tabs) return;

    if (daw_is_tab_unmodified_setup(idx)) {
        g_daw.tabs[idx].is_dirty = false;
    }

    if (g_daw.tabs[idx].is_dirty) {
        g_daw.active_tab = idx;
        g_daw.save_prompt_open = true;
        g_daw.save_prompt_tab_idx = idx;
        g_daw.save_prompt_quit_app = false;
        daw_clear_open_project_after_save_flag();
    } else {
        force_close_tab(idx);
    }
}

void handle_save_modal_choice(int choice, bool *running) {
    int t_idx = g_daw.save_prompt_tab_idx;
    bool quit_after = g_daw.save_prompt_quit_app;

    g_daw.save_prompt_open = false;
    g_daw.save_prompt_tab_idx = -1;
    g_daw.save_prompt_quit_app = false;

    if (choice == 1) {
        if (t_idx >= 0 && t_idx < g_daw.num_tabs) {
            save_project_file();
            g_daw.tabs[t_idx].is_dirty = false;
        }
    } else if (choice == 2) {
        if (quit_after) {
            if (t_idx >= 0 && t_idx < g_daw.num_tabs) {
                g_daw.tabs[t_idx].is_dirty = false;
            }
            for (int t = 0; t < g_daw.num_tabs; t++) {
                if (daw_is_tab_unmodified_setup(t)) {
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
}
