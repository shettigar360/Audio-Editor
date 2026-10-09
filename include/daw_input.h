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
        SDL_Keymod mod = SDL_GetModState();
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

        if ((e->key.keysym.sym == SDLK_DELETE || e->key.keysym.sym == SDLK_BACKSPACE) && g_daw.selected_marker_idx >= 0) {
            for (int m = g_daw.selected_marker_idx; m < p->num_markers - 1; m++) {
                p->markers[m] = p->markers[m + 1];
            }
            p->num_markers--;
            g_daw.selected_marker_idx = -1;
            return;
        }

        if ((mod & KMOD_CTRL) && e->key.keysym.sym == SDLK_z) perform_undo();
        else if ((mod & KMOD_CTRL) && (e->key.keysym.sym == SDLK_y || ((mod & KMOD_SHIFT) && e->key.keysym.sym == SDLK_z))) perform_redo();
        else if (e->key.keysym.sym == SDLK_EQUALS || e->key.keysym.sym == SDLK_PLUS || e->key.keysym.sym == SDLK_KP_PLUS)
            p->pixels_per_second = fminf(400.0f, p->pixels_per_second * 1.25f);
        else if (e->key.keysym.sym == SDLK_MINUS || e->key.keysym.sym == SDLK_KP_MINUS)
            p->pixels_per_second = fmaxf(15.0f, p->pixels_per_second * 0.8f);
        else if (e->key.keysym.sym == SDLK_m) {
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
        }
        else if (e->key.keysym.sym == SDLK_SPACE) p->is_playing = !p->is_playing;
        else if (e->key.keysym.sym == SDLK_v) p->video.is_open = !p->video.is_open;
        else if (e->key.keysym.sym == SDLK_HOME) { p->playhead_seconds = 0.0; seek_video(&p->video, 0.0); }
        else if (e->key.keysym.sym == SDLK_s) {
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
        }
    } else if (e->type == SDL_MOUSEBUTTONDOWN) {
        int mx = e->button.x, my = e->button.y;
        SDL_Keymod mod = SDL_GetModState();
        bool shift_pressed = (mod & KMOD_SHIFT);
        bool ctrl_pressed = (mod & KMOD_CTRL);

        if (g_daw.input_focus != INPUT_FOCUS_NONE) {
            if (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME) {
                strncpy(p->track_names[g_daw.editing_track_name_idx], g_daw.editing_name_buf, 63);
            } else if (g_daw.input_focus == INPUT_FOCUS_MARKER) {
                strncpy(p->markers[g_daw.editing_marker_idx].text, g_daw.editing_marker_buf, 63);
            }
            g_daw.input_focus = INPUT_FOCUS_NONE;
            g_daw.editing_track_name_idx = -1;
            g_daw.editing_marker_idx = -1;
            SDL_StopTextInput();
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

        // Script Manager
        if (g_daw.script_manager_open) {
            SDL_Rect *sr = &g_daw.script_win_rect;
            if (mx >= sr->x && mx <= sr->x + sr->w && my >= sr->y && my <= sr->y + 26) {
                if (mx >= sr->x + sr->w - 24) {
                    g_daw.script_manager_open = false;
                } else {
                    g_daw.script_win_dragging = true;
                    g_daw.script_drag_offset_x = mx - sr->x;
                    g_daw.script_drag_offset_y = my - sr->y;
                }
                return;
            }
            if (mx >= sr->x && mx <= sr->x + sr->w && my >= sr->y + 26 && my <= sr->y + sr->h) {
                int by = sr->y + sr->h - 38;
                if (my >= by && my <= by + 28) {
                    if (mx >= sr->x + 12 && mx <= sr->x + 104) {
                        char spath[MAX_PATH];
                        const char *flt = g_daw.script_manager_is_python ? "Python (*.py)\0*.py\0" : "Lua (*.lua)\0*.lua\0";
                        if (open_generic_dialog(spath, sizeof(spath), flt, false) && g_daw.num_scripts < MAX_SCRIPTS) {
                            DawScript *sc = &g_daw.scripts[g_daw.num_scripts++];
                            sc->is_python = g_daw.script_manager_is_python;
                            strncpy(sc->filepath, spath, sizeof(sc->filepath) - 1);
                            const char *fn = strrchr(spath, '\\');
                            if (!fn) fn = strrchr(spath, '/');
                            strncpy(sc->name, fn ? fn + 1 : spath, sizeof(sc->name) - 1);
                            strcpy(sc->shortcut, "F8");
                        }
                    } else if (mx >= sr->x + 114 && mx <= sr->x + 206) {
                        if (g_daw.script_selected_idx >= 0 && g_daw.script_selected_idx < g_daw.num_scripts) {
                            printf("Executed script: %s\n", g_daw.scripts[g_daw.script_selected_idx].filepath);
                        }
                    } else if (mx >= sr->x + 216 && mx <= sr->x + 308) {
                        if (g_daw.script_selected_idx >= 0 && g_daw.script_selected_idx < g_daw.num_scripts) {
                            for (int s = g_daw.script_selected_idx; s < g_daw.num_scripts - 1; s++) {
                                g_daw.scripts[s] = g_daw.scripts[s + 1];
                            }
                            g_daw.num_scripts--;
                            g_daw.script_selected_idx = -1;
                        }
                    }
                }
                int ly = sr->y + 34;
                if (my >= ly && my <= ly + 160 && mx >= sr->x + 12 && mx <= sr->x + sr->w - 12) {
                    int r = (my - ly) / 22;
                    if (r >= 0 && r < g_daw.num_scripts) g_daw.script_selected_idx = r;
                }
                return;
            }
        }

        if ((g_daw.menu_file_open || g_daw.menu_action_open) && (mx > 260 || my > 260)) {
            g_daw.menu_file_open = false;
            g_daw.menu_action_open = false;
            g_daw.submenu_export_audio_open = false;
            g_daw.submenu_export_video_open = false;
            return;
        }

        if (g_daw.menu_file_open) {
            if (mx >= 8 && mx <= 200) {
                int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                if (my >= m_start && my < m_start + 24) { save_project_file(); g_daw.menu_file_open = false; return; }
                else if (my >= m_start + 24 && my < m_start + 48) { load_project_file(); g_daw.menu_file_open = false; return; }
                else if (my >= m_start + 48 && my < m_start + 72) {
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
                else if (my >= m_start + 72 && my < m_start + 96) { g_daw.submenu_export_audio_open = !g_daw.submenu_export_audio_open; return; }
                else if (my >= m_start + 96 && my < m_start + 120) { g_daw.submenu_export_video_open = !g_daw.submenu_export_video_open; return; }
                else if (my >= m_start + 120 && my < m_start + 144) { export_batch_tracks(); g_daw.menu_file_open = false; return; }
            }
            if (g_daw.submenu_export_audio_open && mx >= 200 && mx <= 310) {
                int s_start = TABBAR_HEIGHT + TOPBAR_HEIGHT + 72;
                if (my >= s_start && my < s_start + 22) { export_rendered_audio("wav"); g_daw.menu_file_open = false; return; }
                if (my >= s_start + 22 && my < s_start + 44) { export_rendered_audio("mp3"); g_daw.menu_file_open = false; return; }
                if (my >= s_start + 44 && my < s_start + 66) { export_rendered_audio("flac"); g_daw.menu_file_open = false; return; }
                if (my >= s_start + 66 && my < s_start + 88) { export_rendered_audio("ogg"); g_daw.menu_file_open = false; return; }
            }
        }

        // Action Menu Dropdown Handling
        if (g_daw.menu_action_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            if (mx >= 56 && mx <= 230) {
                // Row 1: Key Shortcuts...
                if (my >= m_start && my < m_start + 26) {
                    g_daw.shortcut_manager_open = true;
                    g_daw.script_manager_open = false;
                    g_daw.menu_action_open = false;
                    return;
                }
                // Row 2: Lua Scripts...
                else if (my >= m_start + 26 && my < m_start + 50) {
                    g_daw.script_manager_open = true;
                    g_daw.script_manager_is_python = false;
                    g_daw.shortcut_manager_open = false;
                    g_daw.menu_action_open = false;
                    return;
                }
                // Row 3: Python Scripts...
                else if (my >= m_start + 50 && my < m_start + 76) {
                    g_daw.script_manager_open = true;
                    g_daw.script_manager_is_python = true;
                    g_daw.shortcut_manager_open = false;
                    g_daw.menu_action_open = false;
                    return;
                }
            }
        }

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
        g_daw.script_win_dragging = false;
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

        if (g_daw.script_win_dragging) {
            g_daw.script_win_rect.x = mx - g_daw.script_drag_offset_x;
            g_daw.script_win_rect.y = my - g_daw.script_drag_offset_y;
        }

        if (moving_track_idx >= 0) {
            int cur_y = header_top_y - p->scroll_y_px;
            for (int t = 0; t < p->num_tracks; t++) {
                if (my >= cur_y && my < cur_y + p->track_heights[t]) {
                    if (t != moving_track_idx) {
                        swap_tracks(moving_track_idx, t);
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
        } else if (mx >= g_daw.tcp_width && my > header_top_y) {
            for (int i = 0; i < p->num_items; i++) {
                AudioItem *it = &p->items[i];
                int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
                int iw = (int)(it->duration * p->pixels_per_second);
                int iy = header_top_y - p->scroll_y_px;
                for (int t = 0; t < it->track_idx; t++) iy += p->track_heights[t];
                if (my >= iy && my <= iy + p->track_heights[it->track_idx]) {
                    if (abs(mx - ix) <= 8 || abs(mx - (ix + iw)) <= 8) {
                        SDL_SetCursor(g_cursor_size_we);
                        hovered_edge = true;
                        break;
                    }
                }
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
