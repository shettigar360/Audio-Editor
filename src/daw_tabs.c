#include "daw_tabs.h"
#include "video_player.h"
#include "project_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_tab_defaults(int idx, const char *title) {
    if (idx < 0 || idx >= MAX_TABS) return;

    ProjectTab *p = &g_daw.tabs[idx];
    memset(p, 0, sizeof(ProjectTab));
    
    if (title && title[0] != '\0') {
        snprintf(p->project_name, sizeof(p->project_name), "%s", title);
    } else {
        snprintf(p->project_name, sizeof(p->project_name), "Project %d", idx + 1);
    }

    p->num_tracks = 4;
    p->pixels_per_second = 80.0f;
    p->project_duration_sec = 300.0;
    p->show_marker_lane = false;
    p->is_dirty = false;
    p->playhead_sec = 0.0;
    p->is_playing = false;

    // Initialize video player state with clean defaults
    memset(&p->video, 0, sizeof(SimpleVideoPlayer));
    p->video.video_stream_idx = -1;
    p->video.is_open = false;
    p->video.has_active_frame = false;
    p->video.current_pts_sec = 0.0;
    p->video.rect = (SDL_Rect){ .x = 650, .y = 160, .w = 480, .h = 310 };

    for (int t = 0; t < MAX_TRACKS; t++) {
        p->tracks[t].id = t + 1;
        p->tracks[t].volume = 1.0f;
        p->track_db[t] = 0.0f;
        p->track_heights[t] = 84;
        snprintf(p->track_names[t], sizeof(p->track_names[t]), "Track %d", t + 1);
    }
}

void switch_active_tab(int target_idx) {
    if (target_idx < 0 || target_idx >= g_daw.num_tabs || target_idx == g_daw.active_tab) {
        return;
    }

    g_daw.active_tab = target_idx;
    ProjectTab *active = &g_daw.tabs[g_daw.active_tab];

    // Resynchronize video position to current tab playhead if a video track exists
    if (active->video.is_open) {
        seek_video(&active->video, active->playhead_sec);
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
            if (p->items[i].peaks) {
                free(p->items[i].peaks);
                p->items[i].peaks = NULL;
            }
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
        if (target->items[i].peaks) {
            free(target->items[i].peaks);
            target->items[i].peaks = NULL;
        }
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

    // Refresh video context for the newly focused tab
    ProjectTab *current = &g_daw.tabs[g_daw.active_tab];
    if (current->video.is_open) {
        seek_video(&current->video, current->playhead_sec);
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

    g_daw.save_prompt_open = false;
    g_daw.save_prompt_tab_idx = -1;
    g_daw.save_prompt_quit_app = false;

    if (choice == 1) {
        // [Save]: Write project file and preserve tab
        if (t_idx >= 0 && t_idx < g_daw.num_tabs) {
            save_project_file();
            g_daw.tabs[t_idx].is_dirty = false;
        }
    } else if (choice == 2) {
        // [Don't Save]: Discard changes
        if (quit_after) {
            if (running) *running = false;
        } else {
            force_close_tab(t_idx);
        }
    }
    // choice == 3 ([Cancel]): Dismiss dialog
}
