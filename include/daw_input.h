#ifndef DAW_INPUT_H
#define DAW_INPUT_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <math.h>
#include "daw_types.h"
#include "project_io.h"

static inline void execute_action_by_id(int action_id) {
    ProjectTab *p = get_cur_tab();
    switch (action_id) {
        case 1: p->is_playing = !p->is_playing; break;
        case 2: {
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
        case 3: {
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
        }
        case 4: p->pixels_per_second = fminf(400.0f, p->pixels_per_second * 1.25f); break;
        case 5: p->pixels_per_second = fmaxf(15.0f, p->pixels_per_second * 0.8f); break;
        case 6: perform_undo(); break;
        case 7: perform_redo(); break;
        case 8: p->video.is_open = !p->video.is_open; break;
        case 9: p->playhead_seconds = 0.0; seek_video(&p->video, 0.0); break;
        case 10: {
            if (g_daw.selected_marker_idx >= 0) {
                for (int m = g_daw.selected_marker_idx; m < p->num_markers - 1; m++) {
                    p->markers[m] = p->markers[m + 1];
                }
                p->num_markers--;
                g_daw.selected_marker_idx = -1;
            }
            break;
        }
        case 11: {
            push_undo_state();
            p->track_heights[p->num_tracks] = 76;
            snprintf(p->track_names[p->num_tracks], 64, "Track %d", p->num_tracks + 1);
            p->num_tracks++;
            break;
        }
        case 12: save_project_file(); break;
        case 13: load_project_file(); break;
        case 14: g_daw.action_window_open = !g_daw.action_window_open; break;
        case 15: g_daw.render_window_open = !g_daw.render_window_open; break;
        default: break;
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
        if (g_daw.action_window_open) {
            g_daw.action_scroll_row -= e->wheel.y * 2;
            if (g_daw.action_scroll_row < 0) g_daw.action_scroll_row = 0;
            return;
        }

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
        } else if (g_daw.input_focus == INPUT_FOCUS_ACTION_FILTER) {
            if (strlen(g_daw.filter_buf) + strlen(e->text.text) < 63) {
                strcat(g_daw.filter_buf, e->text.text);
                filter_actions();
            }
        } else if (g_daw.input_focus == INPUT_FOCUS_RENDER_DIR) {
            if (strlen(g_daw.render_settings.directory) + strlen(e->text.text) < 259) strcat(g_daw.render_settings.directory, e->text.text);
        } else if (g_daw.input_focus == INPUT_FOCUS_RENDER_FILE) {
            if (strlen(g_daw.render_settings.filename) + strlen(e->text.text) < 127) strcat(g_daw.render_settings.filename, e->text.text);
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
        if (g_daw.input_focus == INPUT_FOCUS_ACTION_FILTER) {
            if (e->key.keysym.sym == SDLK_RETURN || e->key.keysym.sym == SDLK_ESCAPE) {
                g_daw.input_focus = INPUT_FOCUS_NONE;
                SDL_StopTextInput();
            } else if (e->key.keysym.sym == SDLK_BACKSPACE) {
                size_t l = strlen(g_daw.filter_buf);
                if (l > 0) { g_daw.filter_buf[l - 1] = '\0'; filter_actions(); }
            }
            return;
        }

        if (g_daw.input_focus == INPUT_FOCUS_RENDER_DIR) {
            if (e->key.keysym.sym == SDLK_RETURN || e->key.keysym.sym == SDLK_ESCAPE) {
                g_daw.input_focus = INPUT_FOCUS_NONE;
                SDL_StopTextInput();
            } else if (e->key.keysym.sym == SDLK_BACKSPACE) {
                size_t l = strlen(g_daw.render_settings.directory);
                if (l > 0) g_daw.render_settings.directory[l - 1] = '\0';
            }
            return;
        }

        if (g_daw.input_focus == INPUT_FOCUS_RENDER_FILE) {
            if (e->key.keysym.sym == SDLK_RETURN || e->key.keysym.sym == SDLK_ESCAPE) {
                g_daw.input_focus = INPUT_FOCUS_NONE;
                SDL_StopTextInput();
            } else if (e->key.keysym.sym == SDLK_BACKSPACE) {
                size_t l = strlen(g_daw.render_settings.filename);
                if (l > 0) g_daw.render_settings.filename[l - 1] = '\0';
            }
            return;
        }

        if (g_daw.input_focus == INPUT_FOCUS_SHORTCUT_ASSIGN && g_daw.selected_action_idx >= 0) {
            ActionItem *a = &g_daw.actions[g_daw.selected_action_idx];
            a->key = e->key.keysym.sym;
            a->mod = e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT);

            char sc_str[32] = {0};
            if (a->mod & KMOD_CTRL) strcat(sc_str, "Ctrl+");
            if (a->mod & KMOD_ALT) strcat(sc_str, "Alt+");
            if (a->mod & KMOD_SHIFT) strcat(sc_str, "Shift+");
            strcat(sc_str, SDL_GetKeyName(a->key));
            strncpy(a->shortcut, sc_str, sizeof(a->shortcut) - 1);

            g_daw.input_focus = INPUT_FOCUS_NONE;
            return;
        }

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

        // Global Shortcut Matching
        Uint16 cur_mod = e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_SHIFT);
        for (int a = 0; a < g_daw.num_actions; a++) {
            if (g_daw.actions[a].key == e->key.keysym.sym) {
                bool mod_match = false;
                if (g_daw.actions[a].mod == 0 && cur_mod == 0) mod_match = true;
                else if (g_daw.actions[a].mod != 0 && (cur_mod & g_daw.actions[a].mod)) mod_match = true;

                if (mod_match) {
                    execute_action_by_id(g_daw.actions[a].id);
                    return;
                }
            }
        }
    } else if (e->type == SDL_MOUSEBUTTONDOWN) {
        int mx = e->button.x, my = e->button.y;
        SDL_Keymod mod = SDL_GetModState();
        bool shift_pressed = (mod & KMOD_SHIFT);
        bool ctrl_pressed = (mod & KMOD_CTRL);

        if (g_daw.input_focus != INPUT_FOCUS_NONE && g_daw.input_focus != INPUT_FOCUS_ACTION_FILTER) {
            g_daw.input_focus = INPUT_FOCUS_NONE;
            SDL_StopTextInput();
        }

        // Render to File Window Interaction
        if (g_daw.render_window_open) {
            SDL_Rect *rw = &g_daw.render_win_rect;
            RenderSettings *rs = &g_daw.render_settings;

            // Header Drag and Close [X]
            if (mx >= rw->x && mx <= rw->x + rw->w && my >= rw->y && my <= rw->y + 26) {
                if (mx >= rw->x + rw->w - 26) {
                    g_daw.render_window_open = false;
                } else {
                    g_daw.render_win_dragging = true;
                    g_daw.render_drag_offset_x = mx - rw->x;
                    g_daw.render_drag_offset_y = my - rw->y;
                }
                return;
            }

            int cy = rw->y + 36;
            // Source Dropdown Toggle
            if (mx >= rw->x + 70 && mx <= rw->x + 220 && my >= cy && my <= cy + 22) {
                rs->source_idx = !rs->source_idx;
                return;
            }
            // Bounds Dropdown Toggle
            if (mx >= rw->x + 236 && mx <= rw->x + 376 && my >= cy && my <= cy + 22) {
                rs->bounds_idx = !rs->bounds_idx;
                return;
            }

            cy += 32;
            // Directory Input
            if (mx >= rw->x + 88 && mx <= rw->x + rw->w - 122 && my >= cy + 10 && my <= cy + 32) {
                g_daw.input_focus = INPUT_FOCUS_RENDER_DIR;
                SDL_StartTextInput();
                return;
            }
            // Browse Directory Button
            if (mx >= rw->x + rw->w - 110 && mx <= rw->x + rw->w - 26 && my >= cy + 10 && my <= cy + 32) {
                char chosen_file[MAX_PATH];
                if (open_generic_dialog(chosen_file, sizeof(chosen_file), "Audio File (*.wav)\0*.wav\0", true)) {
                    char *last_slash = strrchr(chosen_file, '\\');
                    if (!last_slash) last_slash = strrchr(chosen_file, '/');
                    if (last_slash) {
                        *last_slash = '\0';
                        strncpy(rs->directory, chosen_file, sizeof(rs->directory) - 1);
                        strncpy(rs->filename, last_slash + 1, sizeof(rs->filename) - 1);
                        char *dot = strrchr(rs->filename, '.');
                        if (dot) *dot = '\0';
                    }
                }
                return;
            }

            // File Name Input
            if (mx >= rw->x + 88 && mx <= rw->x + rw->w - 122 && my >= cy + 38 && my <= cy + 60) {
                g_daw.input_focus = INPUT_FOCUS_RENDER_FILE;
                SDL_StartTextInput();
                return;
            }

            cy += 114;
            // Sample rate toggle
            if (mx >= rw->x + 96 && mx <= rw->x + 176 && my >= cy && my <= cy + 22) {
                if (rs->sample_rate == 48000) rs->sample_rate = 96000;
                else if (rs->sample_rate == 96000) rs->sample_rate = 44100;
                else rs->sample_rate = 48000;
                return;
            }
            // Channels toggle
            if (mx >= rw->x + 286 && mx <= rw->x + 366 && my >= cy && my <= cy + 22) {
                rs->channels = (rs->channels == 1) ? 2 : 1;
                return;
            }

            cy += 34;
            // Format toggle
            if (mx >= rw->x + 76 && mx <= rw->x + 196 && my >= cy + 12 && my <= cy + 34) {
                rs->format_idx = (rs->format_idx + 1) % 4;
                return;
            }
            // Bit depth toggle
            if (rs->format_idx == 0 && mx >= rw->x + 316 && mx <= rw->x + 426 && my >= cy + 12 && my <= cy + 34) {
                rs->bit_depth_idx = !rs->bit_depth_idx;
                return;
            }

            cy += 90;
            // Checkbox 1: Auto-increment
            if (mx >= rw->x + 16 && mx <= rw->x + 380 && my >= cy && my <= cy + 18) {
                rs->auto_increment = !rs->auto_increment;
                return;
            }

            cy += 22;
            // Checkbox 2: Skip silent
            if (mx >= rw->x + 16 && mx <= rw->x + 380 && my >= cy && my <= cy + 18) {
                rs->skip_silent = !rs->skip_silent;
                return;
            }

            cy += 34;
            // Render 1 file button
            if (mx >= rw->x + rw->w - 220 && mx <= rw->x + rw->w - 110 && my >= cy && my <= cy + 28) {
                execute_render_to_file();
                g_daw.render_window_open = false;
                return;
            }

            // Cancel button
            if (mx >= rw->x + rw->w - 100 && mx <= rw->x + rw->w - 16 && my >= cy && my <= cy + 28) {
                g_daw.render_window_open = false;
                return;
            }

            // Block clicks from leaking behind the window
            if (mx >= rw->x && mx <= rw->x + rw->w && my >= rw->y && my <= rw->y + rw->h) return;
        }

        // Action Window Interactions
        if (g_daw.action_window_open) {
            SDL_Rect *aw = &g_daw.action_win_rect;

            if (mx >= aw->x && mx <= aw->x + aw->w && my >= aw->y && my <= aw->y + 24) {
                if (mx >= aw->x + aw->w - 24) {
                    g_daw.action_window_open = false;
                } else {
                    g_daw.action_win_dragging = true;
                    g_daw.action_drag_offset_x = mx - aw->x;
                    g_daw.action_drag_offset_y = my - aw->y;
                }
                return;
            }

            int fy = aw->y + 32;
            if (mx >= aw->x + 60 && mx <= aw->x + 250 && my >= fy && my <= fy + 22) {
                g_daw.input_focus = INPUT_FOCUS_ACTION_FILTER;
                SDL_StartTextInput();
                return;
            }

            if (mx >= aw->x + 256 && mx <= aw->x + 304 && my >= fy && my <= fy + 22) {
                g_daw.filter_buf[0] = '\0';
                filter_actions();
                return;
            }

            int table_y = aw->y + 80;
            int table_h = aw->h - 220;
            if (mx >= aw->x + 12 && mx <= aw->x + aw->w - 12 && my >= table_y && my <= table_y + table_h) {
                int row = (my - table_y) / 20;
                int filter_idx = g_daw.action_scroll_row + row;
                if (filter_idx >= 0 && filter_idx < g_daw.num_filtered) {
                    g_daw.selected_action_idx = g_daw.filtered_indices[filter_idx];
                    if (e->button.clicks == 2) {
                        execute_action_by_id(g_daw.actions[g_daw.selected_action_idx].id);
                    }
                }
                return;
            }

            int lower_y = aw->y + aw->h - 130;
            if (mx >= aw->x + 210 && mx <= aw->x + 290 && my >= lower_y + 14 && my <= lower_y + 38) {
                if (g_daw.selected_action_idx >= 0) g_daw.input_focus = INPUT_FOCUS_SHORTCUT_ASSIGN;
                return;
            }

            if (mx >= aw->x + 210 && mx <= aw->x + 290 && my >= lower_y + 44 && my <= lower_y + 68) {
                if (g_daw.selected_action_idx >= 0) {
                    g_daw.actions[g_daw.selected_action_idx].shortcut[0] = '\0';
                    g_daw.actions[g_daw.selected_action_idx].key = 0;
                    g_daw.actions[g_daw.selected_action_idx].mod = 0;
                }
                return;
            }

            int btn_col_x = aw->x + 330;
            if (mx >= btn_col_x && mx <= btn_col_x + 96 && my >= lower_y + 14 && my <= lower_y + 38) {
                char spath[MAX_PATH];
                if (open_generic_dialog(spath, sizeof(spath), "Script Files (*.lua;*.py)\0*.lua;*.py\0", false)) {
                    const char *fn = strrchr(spath, '\\');
                    if (!fn) fn = strrchr(spath, '/');
                    char desc[128];
                    snprintf(desc, sizeof(desc), "Script: %s", fn ? fn + 1 : spath);
                    bool is_py = (strstr(spath, ".py") != NULL);
                    register_action(100 + g_daw.num_actions, desc, "", 0, 0, spath, is_py);
                    filter_actions();
                }
                return;
            }

            if (mx >= btn_col_x && mx <= btn_col_x + 96 && my >= lower_y + 46 && my <= lower_y + 70) {
                if (g_daw.selected_action_idx >= 0) execute_action_by_id(g_daw.actions[g_daw.selected_action_idx].id);
                return;
            }

            if (mx >= btn_col_x + 104 && mx <= btn_col_x + 200 && my >= lower_y + 46 && my <= lower_y + 70) {
                if (g_daw.selected_action_idx >= 0) execute_action_by_id(g_daw.actions[g_daw.selected_action_idx].id);
                g_daw.action_window_open = false;
                return;
            }

            if (mx >= btn_col_x + 104 && mx <= btn_col_x + 200 && my >= lower_y + 76 && my <= lower_y + 100) {
                g_daw.action_window_open = false;
                return;
            }

            if (mx >= aw->x && mx <= aw->x + aw->w && my >= aw->y && my <= aw->y + aw->h) return;
        }

        // Tab Bar Interaction
        if (my < TABBAR_HEIGHT) {
            const int tab_w = 140;
            int total_tabs = g_daw.num_tabs;
            for (int t = 0; t < total_tabs; t++) {
                int tx = t * tab_w;
                if (mx >= tx && mx < tx + tab_w - 2) {
                    if (mx >= tx + 6 && mx <= tx + 22 && my >= 5 && my <= 23) {
                        close_tab(t);
                    } else {
                        g_daw.active_tab = t;
                    }
                    return;
                }
            }

            int plus_x = total_tabs * tab_w + 4;
            if (g_daw.num_tabs < MAX_TABS && mx >= plus_x && mx <= plus_x + 26 && my >= 4 && my <= TABBAR_HEIGHT - 2) {
                int new_idx = g_daw.num_tabs;
                char name[64];
                snprintf(name, sizeof(name), "Project %d", new_idx + 1);
                init_tab_defaults(new_idx, name);
                g_daw.num_tabs++;
                g_daw.active_tab = new_idx;
                return;
            }
            return;
        }

        // Action Menu Dropdown
        if (g_daw.menu_action_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            if (mx >= 56 && mx <= 232) {
                if (my >= m_start && my < m_start + 26) {
                    g_daw.action_window_open = true;
                    g_daw.menu_action_open = false;
                    return;
                } else if (my >= m_start + 26 && my < m_start + 52) {
                    char spath[MAX_PATH];
                    if (open_generic_dialog(spath, sizeof(spath), "Script Files (*.lua;*.py)\0*.lua;*.py\0", false)) {
                        const char *fn = strrchr(spath, '\\');
                        if (!fn) fn = strrchr(spath, '/');
                        char desc[128];
                        snprintf(desc, sizeof(desc), "Script: %s", fn ? fn + 1 : spath);
                        bool is_py = (strstr(spath, ".py") != NULL);
                        register_action(100 + g_daw.num_actions, desc, "", 0, 0, spath, is_py);
                        filter_actions();
                        g_daw.action_window_open = true;
                    }
                    g_daw.menu_action_open = false;
                    return;
                }
            }
        }

        // File Menu Dropdown
        if (g_daw.menu_file_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            if (mx >= 8 && mx <= 200) {
                if (my >= m_start && my < m_start + 22) {
                    save_project_file();
                    g_daw.menu_file_open = false;
                    return;
                } else if (my >= m_start + 22 && my < m_start + 44) {
                    load_project_file();
                    g_daw.menu_file_open = false;
                    return;
                } else if (my >= m_start + 44 && my < m_start + 66) {
                    g_daw.render_window_open = true;
                    g_daw.menu_file_open = false;
                    return;
                } else if (my >= m_start + 66 && my < m_start + 90) {
                    char pth[MAX_PATH];
                    if (open_generic_dialog(pth, sizeof(pth), "Media Files\0*.wav;*.mp3;*.flac;*.mp4;*.mkv;*.mov\0", false)) {
                        push_undo_state();
                        AudioItem *it = &p->items[p->num_items];
                        it->track_idx = 0; it->start_time = p->playhead_seconds;
                        if (process_media_cache(pth, it) == 0) p->num_items++;
                        open_video_by_path(&p->video, pth, ren);
                    }
                    g_daw.menu_file_open = false;
                    return;
                }
            }
        }

        if ((g_daw.menu_file_open || g_daw.menu_action_open) && (mx > 260 || my > 260)) {
            g_daw.menu_file_open = false;
            g_daw.menu_action_open = false;
            return;
        }

        // Top Bar
        if (my >= TABBAR_HEIGHT && my <= TABBAR_HEIGHT + TOPBAR_HEIGHT) {
            if (mx >= 8 && mx <= 50) { g_daw.menu_file_open = !g_daw.menu_file_open; g_daw.menu_action_open = false; }
            else if (mx >= 56 && mx <= 116) { g_daw.menu_action_open = !g_daw.menu_action_open; g_daw.menu_file_open = false; }
            else if (mx >= 122 && mx <= 186) { p->show_marker_lane = !p->show_marker_lane; }
            else if (mx >= 194 && mx <= 278) p->video.is_open = !p->video.is_open;
            else if (mx >= win_w - 228 && mx < win_w - 176) perform_undo();
            else if (mx >= win_w - 172 && mx < win_w - 120) perform_redo();
            else if (mx >= win_w - 116 && mx < win_w - 60) p->pixels_per_second = fmaxf(15.0f, p->pixels_per_second * 0.8f);
            else if (mx >= win_w - 56 && mx < win_w - 6) p->pixels_per_second = fminf(400.0f, p->pixels_per_second * 1.25f);
            return;
        }

        if (abs(mx - g_daw.tcp_width) <= 5 && my > TABBAR_HEIGHT + TOPBAR_HEIGHT) {
            resizing_tcp_width = true;
            return;
        }

        int corner_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
        if (mx < g_daw.tcp_width && my > corner_y && my <= corner_y + RULER_HEIGHT + m_lane_h) {
            if (mx >= 8 && mx <= 88) {
                push_undo_state();
                p->track_heights[p->num_tracks] = 76;
                snprintf(p->track_names[p->num_tracks], 64, "Track %d", p->num_tracks + 1);
                p->num_tracks++;
            } else if (mx >= 94 && mx <= 180) p->is_playing = !p->is_playing;
            return;
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
            return;
        }

        if (my >= win_h - SCROLLBAR_SIZE && mx >= g_daw.tcp_width && mx < win_w - SCROLLBAR_SIZE) {
            dragging_scrollbar_h = true; scrollbar_h_start_mx = mx; orig_scroll_x = p->scroll_x_sec;
            return;
        }
        if (mx >= win_w - SCROLLBAR_SIZE && my >= header_top_y && my < win_h - SCROLLBAR_SIZE) {
            dragging_scrollbar_v = true; scrollbar_v_start_my = my; orig_scroll_y = p->scroll_y_px;
            return;
        }

        int marker_y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT;
        if (p->show_marker_lane && mx >= g_daw.tcp_width && my > marker_y && my <= marker_y + MARKER_BAR_HEIGHT) {
            double m_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
            if (e->button.button == SDL_BUTTON_RIGHT && p->num_markers < MAX_MARKERS) {
                DawMarker *mk = &p->markers[p->num_markers++];
                mk->time_sec = m_time;
                snprintf(mk->text, sizeof(mk->text), "Marker %d", p->num_markers);
                g_daw.input_focus = INPUT_FOCUS_MARKER;
                g_daw.editing_marker_idx = p->num_markers - 1;
                strncpy(g_daw.editing_marker_buf, mk->text, 63);
                g_daw.selected_marker_idx = p->num_markers - 1;
                SDL_StartTextInput();
            } else {
                g_daw.selected_marker_idx = -1;
                for (int m = 0; m < p->num_markers; m++) {
                    int mk_x = g_daw.tcp_width + (int)((p->markers[m].time_sec - p->scroll_x_sec) * p->pixels_per_second);
                    if (abs(mx - mk_x) <= 36) {
                        g_daw.selected_marker_idx = m;
                        if (e->button.clicks == 2) {
                            g_daw.input_focus = INPUT_FOCUS_MARKER;
                            g_daw.editing_marker_idx = m;
                            strncpy(g_daw.editing_marker_buf, p->markers[m].text, 63);
                            SDL_StartTextInput();
                        }
                        break;
                    }
                }
            }
            return;
        }

        int ruler_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
        if (mx >= g_daw.tcp_width && my > ruler_y && my <= ruler_y + RULER_HEIGHT) {
            scrubbing_ruler = true;
            p->playhead_seconds = fmax(0.0, p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second);
            seek_video(&p->video, p->playhead_seconds);
            return;
        }

        if (mx < g_daw.tcp_width && my > header_top_y) {
            int cur_y = header_top_y - p->scroll_y_px;
            for (int t = 0; t < p->num_tracks; t++) {
                int th = p->track_heights[t];
                if (abs(my - (cur_y + th)) <= 4) {
                    resizing_track_idx = t;
                    track_orig_height = th;
                    mouse_drag_start_y = my;
                    break;
                }
                if (my >= cur_y && my < cur_y + th) {
                    int rel_y = my - cur_y;
                    if (rel_y <= 24) {
                        if (e->button.clicks == 2) {
                            g_daw.input_focus = INPUT_FOCUS_TRACK_NAME;
                            g_daw.editing_track_name_idx = t;
                            strncpy(g_daw.editing_name_buf, p->track_names[t], 63);
                            SDL_StartTextInput();
                        } else {
                            moving_track_idx = t;
                            mouse_drag_start_y = my;
                        }
                    } else {
                        if (mx >= 6 && mx <= 24) p->track_db[t] -= 1.0f;
                        else if (mx >= 78 && mx <= 96) p->track_db[t] += 1.0f;
                        else if (mx >= g_daw.tcp_width - 56 && mx <= g_daw.tcp_width - 32) p->tracks[t].mute = !p->tracks[t].mute;
                        else if (mx >= g_daw.tcp_width - 28 && mx <= g_daw.tcp_width - 6) p->tracks[t].solo = !p->tracks[t].solo;
                        else {
                            moving_track_idx = t;
                            mouse_drag_start_y = my;
                        }
                    }
                    break;
                }
                cur_y += th;
            }
            return;
        }

        if (mx >= g_daw.tcp_width && my > header_top_y && my < win_h - SCROLLBAR_SIZE) {
            double click_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
            int cur_y = header_top_y - p->scroll_y_px;
            int click_track = -1;
            for (int t = 0; t < p->num_tracks; t++) {
                if (my >= cur_y && my < cur_y + p->track_heights[t]) { click_track = t; break; }
                cur_y += p->track_heights[t];
            }

            bool clicked_clip = false;
            for (int i = p->num_items - 1; i >= 0; i--) {
                AudioItem *it = &p->items[i];
                if (it->track_idx == click_track && click_time >= it->start_time && click_time <= it->start_time + it->duration) {
                    clicked_clip = true;
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
                    if (mx <= ix + 8) dragging_trim_left = true;
                    else if (mx >= ix + iw - 8) dragging_trim_right = true;
                    break;
                }
            }

            if (!clicked_clip) {
                if (!ctrl_pressed && !shift_pressed) {
                    for (int o = 0; o < p->num_items; o++) p->items[o].is_selected = false;
                }
                g_daw.marquee_selecting = true;
                g_daw.marquee_start_x = mx; g_daw.marquee_start_y = my;
                g_daw.marquee_cur_x = mx; g_daw.marquee_cur_y = my;
            }
            return;
        }
    } else if (e->type == SDL_MOUSEBUTTONUP) {
        p->video.is_dragging = false; p->video.is_resizing = false;
        dragging_scrollbar_h = false; dragging_scrollbar_v = false;
        scrubbing_ruler = false;
        resizing_tcp_width = false;
        resizing_track_idx = -1;
        moving_track_idx = -1;
        g_daw.action_win_dragging = false;
        g_daw.render_win_dragging = false;
        dragging_item_idx = -1; dragging_trim_left = false; dragging_trim_right = false;

        if (g_daw.marquee_selecting) {
            g_daw.marquee_selecting = false;
            int x1 = fmin(g_daw.marquee_start_x, g_daw.marquee_cur_x);
            int x2 = fmax(g_daw.marquee_start_x, g_daw.marquee_cur_x);
            int y1 = fmin(g_daw.marquee_start_y, g_daw.marquee_cur_y);
            int y2 = fmax(g_daw.marquee_start_y, g_daw.marquee_cur_y);

            for (int i = 0; i < p->num_items; i++) {
                AudioItem *it = &p->items[i];
                int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
                int iw = (int)(it->duration * p->pixels_per_second);
                int iy = header_top_y - p->scroll_y_px;
                for (int t = 0; t < it->track_idx; t++) iy += p->track_heights[t];
                int ih = p->track_heights[it->track_idx];
                if (ix < x2 && ix + iw > x1 && iy < y2 && iy + ih > y1) {
                    it->is_selected = true;
                }
            }
        }
    } else if (e->type == SDL_MOUSEMOTION) {
        int mx = e->motion.x, my = e->motion.y;

        if (g_daw.render_win_dragging) {
            g_daw.render_win_rect.x = mx - g_daw.render_drag_offset_x;
            g_daw.render_win_rect.y = my - g_daw.render_drag_offset_y;
        }

        if (g_daw.action_win_dragging) {
            g_daw.action_win_rect.x = mx - g_daw.action_drag_offset_x;
            g_daw.action_win_rect.y = my - g_daw.action_drag_offset_y;
        }

        if (moving_track_idx >= 0) {
            int cur_y = header_top_y - p->scroll_y_px;
            for (int t = 0; t < p->num_tracks; t++) {
                if (my >= cur_y && my < cur_y + p->track_heights[t]) {
                    if (t != moving_track_idx) {
                        Track tmp_tr = p->tracks[moving_track_idx]; p->tracks[moving_track_idx] = p->tracks[t]; p->tracks[t] = tmp_tr;
                        float tmp_db = p->track_db[moving_track_idx]; p->track_db[moving_track_idx] = p->track_db[t]; p->track_db[t] = tmp_db;
                        int tmp_h = p->track_heights[moving_track_idx]; p->track_heights[moving_track_idx] = p->track_heights[t]; p->track_heights[t] = tmp_h;
                        char tmp_nm[64];
                        strncpy(tmp_nm, p->track_names[moving_track_idx], 63);
                        strncpy(p->track_names[moving_track_idx], p->track_names[t], 63);
                        strncpy(p->track_names[t], tmp_nm, 63);
                        for (int i = 0; i < p->num_items; i++) {
                            if (p->items[i].track_idx == moving_track_idx) p->items[i].track_idx = t;
                            else if (p->items[i].track_idx == t) p->items[i].track_idx = moving_track_idx;
                        }
                        moving_track_idx = t;
                    }
                    break;
                }
                cur_y += p->track_heights[t];
            }
        }

        bool hovered_edge = false;
        if (abs(mx - g_daw.tcp_width) <= 5 && my > TABBAR_HEIGHT + TOPBAR_HEIGHT) {
            SDL_SetCursor(g_cursor_size_we);
            hovered_edge = true;
        } else if (mx < g_daw.tcp_width && my > header_top_y) {
            int cur_y = header_top_y - p->scroll_y_px;
            for (int t = 0; t < p->num_tracks; t++) {
                if (abs(my - (cur_y + p->track_heights[t])) <= 4) {
                    SDL_SetCursor(g_cursor_size_ns);
                    hovered_edge = true;
                    break;
                }
                cur_y += p->track_heights[t];
            }
        }
        if (!hovered_edge && !resizing_tcp_width && resizing_track_idx < 0) {
            SDL_SetCursor(g_cursor_arrow);
        }

        if (g_daw.marquee_selecting) {
            g_daw.marquee_cur_x = mx; g_daw.marquee_cur_y = my;
        }

        if (resizing_tcp_width && mx >= 150 && mx <= 380) {
            g_daw.tcp_width = mx;
        }
        if (resizing_track_idx >= 0) {
            int delta = my - mouse_drag_start_y;
            int new_h = track_orig_height + delta;
            if (new_h >= 54 && new_h <= 260) p->track_heights[resizing_track_idx] = new_h;
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
            p->scroll_y_px = (int)(orig_scroll_y + ((float)(my - scrollbar_v_start_my) / view_h) * max_h_tracks);
            if (p->scroll_y_px < 0) p->scroll_y_px = 0;
            if (p->scroll_y_px > max_y) p->scroll_y_px = max_y;
        }
        if (scrubbing_ruler && mx >= g_daw.tcp_width && mx < win_w - SCROLLBAR_SIZE) {
            p->playhead_seconds = fmax(0.0, p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second);
            seek_video(&p->video, p->playhead_seconds);
        }
        if (dragging_item_idx != -1) {
            AudioItem *it = &p->items[dragging_item_idx];
            double cur_mouse_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
            double delta_t = cur_mouse_time - drag_start_mouse_time;

            if (dragging_trim_left) {
                if (item_orig_dur - delta_t > 0.1) {
                    it->start_time = item_orig_start + delta_t;
                    it->duration = item_orig_dur - delta_t;
                    it->offset_in_file = item_orig_offset + delta_t;
                }
            } else if (dragging_trim_right) {
                if (item_orig_dur + delta_t > 0.1) it->duration = item_orig_dur + delta_t;
            } else {
                it->start_time = fmax(0.0, item_orig_start + delta_t);
                int cur_y = header_top_y - p->scroll_y_px;
                for (int t = 0; t < p->num_tracks; t++) {
                    if (my >= cur_y && my < cur_y + p->track_heights[t]) { it->track_idx = t; break; }
                    cur_y += p->track_heights[t];
                }
            }
        }
    }
}

#endif // DAW_INPUT_H
