#ifndef DAW_INPUT_H
#define DAW_INPUT_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <math.h>
#include "daw_types.h"
#include "project_io.h"

static inline void swap_tracks(int t1, int t2) {
    ProjectTab *p = get_cur_tab();
    if (t1 < 0 || t1 >= p->num_tracks || t2 < 0 || t2 >= p->num_tracks || t1 == t2) return;

    Track tmp_tr = p->tracks[t1]; p->tracks[t1] = p->tracks[t2]; p->tracks[t2] = tmp_tr;
    float tmp_db = p->track_db[t1]; p->track_db[t1] = p->track_db[t2]; p->track_db[t2] = tmp_db;
    int tmp_h = p->track_heights[t1]; p->track_heights[t1] = p->track_heights[t2]; p->track_heights[t2] = tmp_h;
    bool tmp_sel = p->track_selected[t1]; p->track_selected[t1] = p->track_selected[t2]; p->track_selected[t2] = tmp_sel;
    char tmp_nm[64];
    strncpy(tmp_nm, p->track_names[t1], 63);
    strncpy(p->track_names[t1], p->track_names[t2], 63);
    strncpy(p->track_names[t2], tmp_nm, 63);

    for (int i = 0; i < p->num_items; i++) {
        if (p->items[i].track_idx == t1) p->items[i].track_idx = t2;
        else if (p->items[i].track_idx == t2) p->items[i].track_idx = t1;
    }
}

static inline void handle_daw_input_events(SDL_Event *e, SDL_Renderer *ren, SDL_Cursor *g_cursor_arrow, SDL_Cursor *g_cursor_size_we, SDL_Cursor *g_cursor_size_ns, bool *running) {
    ProjectTab *p = get_cur_tab();
    int m_lane_h = p->show_marker_lane ? MARKER_BAR_HEIGHT : 0;
    int header_top_y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT + m_lane_h;

    int win_w = 1280, win_h = 720;
    SDL_GetRendererOutputSize(ren, &win_w, &win_h);
    int view_w = win_w - g_daw.tcp_width - SCROLLBAR_SIZE;
    int view_h = win_h - header_top_y - SCROLLBAR_SIZE;
    int max_h_tracks = 0;
    for (int t = 0; t < p->num_tracks; t++) max_h_tracks += p->track_heights[t];
    int total_w_timeline = (int)(p->project_duration_sec * p->pixels_per_second);

    static bool dragging_scrollbar_h = false, dragging_scrollbar_v = false;
    static int scrollbar_h_start_mx = 0, scrollbar_v_start_my = 0;
    static double orig_scroll_x = 0; static int orig_scroll_y = 0;
    static bool scrubbing_ruler = false;

    static int dragging_item_idx = -1;
    static bool dragging_trim_left = false, dragging_trim_right = false;
    static double drag_start_mouse_time = 0.0, item_orig_start = 0.0, item_orig_dur = 0.0, item_orig_offset = 0.0;

    static bool resizing_tcp_width = false;
    static int resizing_track_idx = -1;
    static int moving_track_idx = -1;
    static int track_orig_height = 0;
    static int mouse_drag_start_y = 0;

    if (e->type == SDL_QUIT) {
        *running = false;
    } else if (e->type == SDL_MOUSEWHEEL) {
        SDL_Keymod mod = SDL_GetModState();
        int mx, my; SDL_GetMouseState(&mx, &my);
        if (mod & KMOD_CTRL) {
            double mouse_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
            p->pixels_per_second += e->wheel.y * 12.0f;
            if (p->pixels_per_second < 15.0f) p->pixels_per_second = 15.0f;
            if (p->pixels_per_second > 400.0f) p->pixels_per_second = 400.0f;
            p->scroll_x_sec = mouse_time - (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
            if (p->scroll_x_sec < 0) p->scroll_x_sec = 0;
        } else if (mod & KMOD_SHIFT) {
            p->scroll_x_sec -= e->wheel.y * (30.0f / p->pixels_per_second);
            if (p->scroll_x_sec < 0) p->scroll_x_sec = 0;
        } else {
            p->scroll_y_px -= e->wheel.y * 36;
            int max_y_scroll = max_h_tracks - view_h;
            if (max_y_scroll < 0) max_y_scroll = 0;
            if (p->scroll_y_px < 0) p->scroll_y_px = 0;
            if (p->scroll_y_px > max_y_scroll) p->scroll_y_px = max_y_scroll;
        }
    } else if (e->type == SDL_TEXTINPUT) {
        if (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME) {
            if (strlen(g_daw.editing_name_buf) + strlen(e->text.text) < 63) strcat(g_daw.editing_name_buf, e->text.text);
        } else if (g_daw.input_focus == INPUT_FOCUS_MARKER) {
            if (strlen(g_daw.editing_marker_buf) + strlen(e->text.text) < 63) strcat(g_daw.editing_marker_buf, e->text.text);
        }
    } else if (e->type == SDL_DROPFILE) {
        char *file = e->drop.file;
        int my = 0; SDL_GetMouseState(NULL, &my);
        int trk = 0;
        int cur_y = header_top_y - p->scroll_y_px;
        for (int t = 0; t < p->num_tracks; t++) {
            if (my >= cur_y && my < cur_y + p->track_heights[t]) { trk = t; break; }
            cur_y += p->track_heights[t];
        }

        if (p->num_items < MAX_ITEMS) {
            push_undo_state();
            AudioItem *it = &p->items[p->num_items];
            it->track_idx = trk;
            it->start_time = p->playhead_seconds;
            if (process_media_cache(file, it) == 0) p->num_items++;
        }
        open_video_by_path(&p->video, file, ren);
        SDL_free(file);
    } else if (e->type == SDL_KEYDOWN) {
        // 1. Key Rebinding Listener
        if (g_daw.input_focus == INPUT_FOCUS_KEY_BIND && g_daw.rebinding_action_idx >= 0) {
            if (e->key.keysym.sym != SDLK_ESCAPE) {
                g_daw.actions[g_daw.rebinding_action_idx].key = e->key.keysym.sym;
                g_daw.actions[g_daw.rebinding_action_idx].mod = (e->key.keysym.mod & KMOD_CTRL) ? KMOD_CTRL : 0;
            }
            g_daw.input_focus = INPUT_FOCUS_NONE;
            g_daw.rebinding_action_idx = -1;
            return;
        }

        // 2. Track Name Text Input
        if (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME) {
            if (e->key.keysym.sym == SDLK_RETURN) {
                strncpy(p->track_names[g_daw.editing_track_name_idx], g_daw.editing_name_buf, 63);
                g_daw.input_focus = INPUT_FOCUS_NONE;
                g_daw.editing_track_name_idx = -1;
                SDL_StopTextInput();
            } else if (e->key.keysym.sym == SDLK_ESCAPE) {
                g_daw.input_focus = INPUT_FOCUS_NONE;
                g_daw.editing_track_name_idx = -1;
                SDL_StopTextInput();
            } else if (e->key.keysym.sym == SDLK_BACKSPACE) {
                size_t l = strlen(g_daw.editing_name_buf);
                if (l > 0) g_daw.editing_name_buf[l - 1] = '\0';
            }
            return;
        }

        // 3. Marker Text Input
        if (g_daw.input_focus == INPUT_FOCUS_MARKER) {
            if (e->key.keysym.sym == SDLK_RETURN) {
                strncpy(p->markers[g_daw.editing_marker_idx].text, g_daw.editing_marker_buf, 63);
                g_daw.input_focus = INPUT_FOCUS_NONE;
                g_daw.editing_marker_idx = -1;
                SDL_StopTextInput();
            } else if (e->key.keysym.sym == SDLK_ESCAPE) {
                g_daw.input_focus = INPUT_FOCUS_NONE;
                g_daw.editing_marker_idx = -1;
                SDL_StopTextInput();
            } else if (e->key.keysym.sym == SDLK_BACKSPACE) {
                size_t l = strlen(g_daw.editing_marker_buf);
                if (l > 0) g_daw.editing_marker_buf[l - 1] = '\0';
            }
            return;
        }

        // 4. Delete Selected Marker
        if ((e->key.keysym.sym == SDLK_DELETE || e->key.keysym.sym == SDLK_BACKSPACE) && g_daw.selected_marker_idx >= 0) {
            for (int m = g_daw.selected_marker_idx; m < p->num_markers - 1; m++) {
                p->markers[m] = p->markers[m + 1];
            }
            p->num_markers--;
            g_daw.selected_marker_idx = -1;
            return;
        }

        // 5. Open Shortcuts Dialog Shortcut '?'
        if (e->key.keysym.sym == SDLK_SLASH && (e->key.keysym.mod & KMOD_SHIFT)) {
            g_daw.shortcut_manager_open = !g_daw.shortcut_manager_open;
            return;
        }

        // 6. Action-Triggered Shortcuts
        SDL_Keycode k = e->key.keysym.sym;
        Uint16 m = (e->key.keysym.mod & KMOD_CTRL) ? KMOD_CTRL : 0;

        for (int a = 0; a < g_daw.num_actions; a++) {
            if (g_daw.actions[a].key == k && (g_daw.actions[a].mod == m || g_daw.actions[a].mod == 0)) {
                switch (g_daw.actions[a].action_id) {
                    case 1: // Play / Pause
                        p->is_playing = !p->is_playing;
                        break;
                    case 2: { // Split Item
                        double cur_t = p->playhead_seconds;
                        for (int i = 0; i < p->num_items; i++) {
                            AudioItem *it = &p->items[i];
                            if (it->is_selected) {
                                double it_s = it->start_time, it_e = it->start_time + it->duration;
                                if (cur_t > it_s + 0.05 && cur_t < it_e - 0.05 && p->num_items < MAX_ITEMS) {
                                    push_undo_state();
                                    AudioItem *r_item = &p->items[p->num_items];
                                    memcpy(r_item, it, sizeof(AudioItem));
                                    double delta = cur_t - it_s;
                                    it->duration = delta;
                                    r_item->start_time = cur_t;
                                    r_item->offset_in_file = it->offset_in_file + delta;
                                    r_item->duration = it_e - cur_t;
                                    r_item->is_selected = true;
                                    it->is_selected = false;
                                    r_item->raw_file_handle = fopen(r_item->raw_audio_path, "rb");
                                    p->num_items++;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                    case 3: // Insert Marker
                        if (p->num_markers < MAX_MARKERS) {
                            DawMarker *mk = &p->markers[p->num_markers++];
                            mk->time_sec = p->playhead_seconds;
                            snprintf(mk->text, sizeof(mk->text), "Marker %d", p->num_markers);
                            g_daw.input_focus = INPUT_FOCUS_MARKER;
                            g_daw.editing_marker_idx = p->num_markers - 1;
                            strncpy(g_daw.editing_marker_buf, mk->text, 63);
                            g_daw.selected_marker_idx = p->num_markers - 1;
                            SDL_StartTextInput();
                        }
                        break;
                    case 4: // Zoom In
                        p->pixels_per_second = fminf(400.0f, p->pixels_per_second * 1.25f);
                        break;
                    case 5: // Zoom Out
                        p->pixels_per_second = fmaxf(15.0f, p->pixels_per_second * 0.8f);
                        break;
                    case 6: // Undo
                        perform_undo();
                        break;
                    case 7: // Redo
                        perform_redo();
                        break;
                    case 8: // Jump To Start
                        p->playhead_seconds = 0.0;
                        seek_video(&p->video, 0.0);
                        break;
                    case 9: // Toggle Video
                        p->video.is_open = !p->video.is_open;
                        break;
                    case 10: // Delete Selected Items
                        for (int i = 0; i < p->num_items; i++) {
                            if (p->items[i].is_selected) {
                                if (p->items[i].raw_file_handle) fclose(p->items[i].raw_file_handle);
                                if (p->items[i].peaks) free(p->items[i].peaks);
                                for (int j = i; j < p->num_items - 1; j++) p->items[j] = p->items[j + 1];
                                p->num_items--;
                                i--;
                            }
                        }
                        break;
                    default:
                        break;
                }
                return;
            }
        }
    } else if (e->type == SDL_MOUSEBUTTONDOWN) {
        int mx = e->button.x, my = e->button.y;
        SDL_Keymod mod = SDL_GetModState();
        bool shift_pressed = (mod & KMOD_SHIFT);
        bool
