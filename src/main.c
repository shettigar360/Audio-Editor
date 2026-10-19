#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "font.h"
#include "video_player.h"
#include "audio_engine.h"
#include "daw_types.h"
#include "daw_tabs.h"
#include "project_io.h"

DawEngine g_daw;

static SDL_Cursor *g_cursor_arrow = NULL;
static SDL_Cursor *g_cursor_size_we = NULL;
static SDL_Cursor *g_cursor_size_ns = NULL;
static SDL_Cursor *g_cursor_ibeam = NULL;

// Per-tab Crossfade mode state (true = Crossfade ON, false = Crossfade OFF / edge-touch only)
static bool g_tab_crossfade[MAX_TABS] = { true, true, true, true, true, true, true, true };

static inline bool is_crossfade_active(void) {
    int idx = g_daw.active_tab;
    if (idx < 0 || idx >= MAX_TABS) idx = 0;
    return g_tab_crossfade[idx];
}

static inline void set_crossfade_active(bool active) {
    int idx = g_daw.active_tab;
    if (idx < 0 || idx >= MAX_TABS) idx = 0;
    g_tab_crossfade[idx] = active;
}

// Computes equal-power crossfade gain (sin/cos S-law) and micro de-click ramps at sample time t_sec
static float compute_item_crossfade_gain(ProjectTab *p, int item_idx, double t_sec, bool crossfade_on) {
    AudioItem *it = &p->items[item_idx];
    double it_s = it->start_time;
    double it_e = it->start_time + it->duration;
    if (t_sec < it_s || t_sec > it_e) return 0.0f;

    float env = 1.0f;
    bool has_left_xfade = false;
    bool has_right_xfade = false;

    if (crossfade_on) {
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

// When Crossfade is OFF, clamps a dragged/placed clip's start_time so it touches edges without overlapping
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

// Resolves any existing overlaps on all tracks by snapping overlapping clips edge-to-edge
static void resolve_all_track_overlaps_edge_to_edge(ProjectTab *p) {
    if (!p) return;
    for (int t = 0; t < p->num_tracks; t++) {
        int idxs[MAX_ITEMS];
        int cnt = 0;
        for (int i = 0; i < p->num_items; i++) {
            if (p->items[i].track_idx == t) {
                idxs[cnt++] = i;
            }
        }
        // Sort clips on track t by start_time
        for (int a = 0; a < cnt - 1; a++) {
            for (int b = a + 1; b < cnt; b++) {
                if (p->items[idxs[b]].start_time < p->items[idxs[a]].start_time) {
                    int tmp = idxs[a];
                    idxs[a] = idxs[b];
                    idxs[b] = tmp;
                }
            }
        }
        // Push any overlapping clip so its left edge touches the preceding clip's right edge
        for (int k = 1; k < cnt; k++) {
            AudioItem *prev = &p->items[idxs[k - 1]];
            AudioItem *curr = &p->items[idxs[k]];
            double prev_end = prev->start_time + prev->duration;
            if (curr->start_time < prev_end) {
                curr->start_time = prev_end;
                p->is_dirty = true;
            }
        }
    }
}

void audio_mixer_callback(void *userdata, Uint8 *stream, int len) {
    DawEngine *eng = (DawEngine *)userdata;
    if (eng->num_tabs <= 0) return;
    ProjectTab *p = &eng->tabs[eng->active_tab];
    float *out = (float *)stream;
    int frames_needed = len / (sizeof(float) * 2);
    memset(stream, 0, len);

    if (!p->is_playing) return;

    bool any_solo = false;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->tracks[t].solo) { any_solo = true; break; }
    }

    bool xfade_on = g_tab_crossfade[eng->active_tab >= 0 && eng->active_tab < MAX_TABS ? eng->active_tab : 0];
    double st = p->playhead_seconds;
    double et = st + ((double)frames_needed / SAMPLE_RATE);

    for (int i = 0; i < p->num_items; i++) {
        AudioItem *it = &p->items[i];
        if (!it->raw_file_handle) continue;

        Track *tr = &p->tracks[it->track_idx];
        if (tr->mute) continue;
        if (any_solo && !tr->solo) continue;

        float linear_gain = db_to_linear(p->track_db[it->track_idx]);
        double it_s = it->start_time;
        double it_e = it->start_time + it->duration;

        if (it_e > st && it_s < et) {
            int f_start = 0;
            if (it_s > st) {
                f_start = (int)((it_s - st) * SAMPLE_RATE);
                if (f_start < 0) f_start = 0;
                if (f_start >= frames_needed) continue;
            }
            int f_end = frames_needed;
            if (it_e < et) {
                f_end = (int)((it_e - st) * SAMPLE_RATE);
                if (f_end > frames_needed) f_end = frames_needed;
                if (f_end <= f_start) continue;
            }

            int frames_to_read = f_end - f_start;
            double clip_start_t = st + ((double)f_start / SAMPLE_RATE);
            int64_t sample_offset = (int64_t)(((clip_start_t - it_s) + it->offset_in_file) * SAMPLE_RATE);
            if (sample_offset < 0) sample_offset = 0;

            fseek(it->raw_file_handle, (long)(sample_offset * sizeof(float) * 2), SEEK_SET);
            float chunk[1024 * 2];
            if (frames_to_read > 1024) frames_to_read = 1024;
            size_t read_frames = fread(chunk, sizeof(float) * 2, (size_t)frames_to_read, it->raw_file_handle);
            for (size_t f = 0; f < read_frames; f++) {
                int out_idx = f_start + (int)f;
                double sample_t = st + ((double)out_idx / SAMPLE_RATE);
                float xfade_gain = compute_item_crossfade_gain(p, i, sample_t, xfade_on);
                float total_gain = linear_gain * xfade_gain;
                out[out_idx * 2]     += chunk[f * 2]     * total_gain;
                out[out_idx * 2 + 1] += chunk[f * 2 + 1] * total_gain;
            }
        }
    }
    p->playhead_seconds += (double)frames_needed / SAMPLE_RATE;
}

static inline int get_marker_height(ProjectTab *p) {
    return p->show_marker_lane ? MARKER_BAR_HEIGHT : 0;
}

static inline int get_track_y_pos(int trk_idx) {
    ProjectTab *p = get_cur_tab();
    if (!p) return 0;
    int y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT + get_marker_height(p) - p->scroll_y_px;
    for (int t = 0; t < trk_idx; t++) y += p->track_heights[t];
    return y;
}

static inline int get_total_tracks_height() {
    ProjectTab *p = get_cur_tab();
    if (!p) return 0;
    int total = 0;
    for (int t = 0; t < p->num_tracks; t++) total += p->track_heights[t];
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
            if (p->tracks[it->track_idx].mute) continue;
            if (any_solo && !p->tracks[it->track_idx].solo) continue;
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

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return 1;

    SDL_Window *win = SDL_CreateWindow(
        "Professional Multitrack DAW",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

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

    memset(&g_daw, 0, sizeof(DawEngine));
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
    g_daw.script_win_rect = (SDL_Rect){ 420, 180, 440, 270 };

    init_tab_defaults(0, "Project 1");
    push_undo_state();
    g_daw.tabs[0].is_dirty = false;

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

    bool marquee_selecting = false;
    int marquee_start_x = 0, marquee_start_y = 0;
    int marquee_cur_x = 0, marquee_cur_y = 0;

    int dragging_item_idx = -1;
    bool dragging_trim_left = false, dragging_trim_right = false;
    double drag_start_mouse_time = 0.0, item_orig_start = 0.0, item_orig_dur = 0.0, item_orig_offset = 0.0;

    bool resizing_tcp_width = false;
    int resizing_track_idx = -1;
    int track_orig_height = 0;
    int mouse_drag_start_y = 0;

    SDL_Event e;
    while (running) {
        ProjectTab *p = get_cur_tab();
        int win_w, win_h;
        SDL_GetWindowSize(win, &win_w, &win_h);

        int m_lane_h = get_marker_height(p);
        int view_w = win_w - g_daw.tcp_width - SCROLLBAR_SIZE;
        int view_h = win_h - TABBAR_HEIGHT - TOPBAR_HEIGHT - RULER_HEIGHT - m_lane_h - SCROLLBAR_SIZE;
        int max_h_tracks = get_total_tracks_height();

        for (int i = 0; i < p->num_items; i++) {
            double end_t = p->items[i].start_time + p->items[i].duration + 60.0;
            if (end_t > p->project_duration_sec) p->project_duration_sec = end_t;
        }
        int total_w_timeline = (int)(p->project_duration_sec * p->pixels_per_second);

        if (p->is_playing) {
            double visible_end_sec = p->scroll_x_sec + ((double)view_w / p->pixels_per_second);
            if (p->playhead_seconds >= visible_end_sec) {
                p->scroll_x_sec = p->playhead_seconds;
            }
        }

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                bool has_any_dirty = false;
                for (int t = 0; t < g_daw.num_tabs; t++) {
                    if (g_daw.tabs[t].is_dirty) {
                        has_any_dirty = true;
                        g_daw.save_prompt_tab_idx = t;
                        break;
                    }
                }
                if (has_any_dirty) {
                    g_daw.save_prompt_open = true;
                    g_daw.save_prompt_quit_app = true;
                } else {
                    running = false;
                }
            } else if (e.type == SDL_MOUSEWHEEL) {
                if (g_daw.save_prompt_open) continue;
                SDL_Keymod mod = SDL_GetModState();
                int mx, my; SDL_GetMouseState(&mx, &my);
                if (mod & KMOD_CTRL) {
                    double mouse_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                    p->pixels_per_second += e.wheel.y * 12.0f;
                    if (p->pixels_per_second < 15.0f) p->pixels_per_second = 15.0f;
                    if (p->pixels_per_second > 400.0f) p->pixels_per_second = 400.0f;
                    p->scroll_x_sec = mouse_time - (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                    if (p->scroll_x_sec < 0) p->scroll_x_sec = 0;
                } else if (mod & KMOD_SHIFT) {
                    p->scroll_x_sec -= e.wheel.y * (30.0f / p->pixels_per_second);
                    if (p->scroll_x_sec < 0) p->scroll_x_sec = 0;
                } else {
                    p->scroll_y_px -= e.wheel.y * 36;
                    int max_y_scroll = max_h_tracks - view_h;
                    if (max_y_scroll < 0) max_y_scroll = 0;
                    if (p->scroll_y_px < 0) p->scroll_y_px = 0;
                    if (p->scroll_y_px > max_y_scroll) p->scroll_y_px = max_y_scroll;
                }
            } else if (e.type == SDL_TEXTINPUT) {
                if (g_daw.save_prompt_open) continue;
                if (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME) {
                    if (strlen(g_daw.editing_name_buf) + strlen(e.text.text) < 63) strcat(g_daw.editing_name_buf, e.text.text);
                } else if (g_daw.input_focus == INPUT_FOCUS_MARKER) {
                    if (strlen(g_daw.editing_marker_buf) + strlen(e.text.text) < 63) strcat(g_daw.editing_marker_buf, e.text.text);
                }
            } else if (e.type == SDL_DROPFILE) {
                if (g_daw.save_prompt_open) { SDL_free(e.drop.file); continue; }
                char *file = e.drop.file;
                int my = 0; SDL_GetMouseState(NULL, &my);
                int trk = 0;
                int cur_y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT + m_lane_h - p->scroll_y_px;
                for (int t = 0; t < p->num_tracks; t++) {
                    if (my >= cur_y && my < cur_y + p->track_heights[t]) { trk = t; break; }
                    cur_y += p->track_heights[t];
                }

                if (p->num_items < MAX_ITEMS) {
                    push_undo_state();
                    int new_idx = p->num_items;
                    AudioItem *it = &p->items[new_idx];
                    memset(it, 0, sizeof(AudioItem));
                    it->track_idx = trk;
                    it->start_time = p->playhead_seconds;
                    if (process_media_cache(file, it) == 0) {
                        if (!is_crossfade_active()) {
                            it->start_time = clamp_non_overlapping_start(p, new_idx, trk, it->start_time, it->duration);
                        }
                        p->num_items++;
                        if (it->has_video) {
                            open_video_by_path(&p->video, it->file_path, ren);
                        }
                    }
                }
                SDL_free(file);
            } else if (e.type == SDL_KEYDOWN) {
                if (g_daw.save_prompt_open) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        handle_save_modal_choice(3, &running);
                    }
                    continue;
                }
                SDL_Keymod mod = SDL_GetModState();
                if (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME) {
                    if (e.key.keysym.sym == SDLK_RETURN) {
                        strncpy(p->track_names[g_daw.editing_track_name_idx], g_daw.editing_name_buf, 63);
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_track_name_idx = -1;
                        SDL_StopTextInput();
                        p->is_dirty = true;
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_track_name_idx = -1;
                        SDL_StopTextInput();
                    } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                        size_t l = strlen(g_daw.editing_name_buf);
                        if (l > 0) g_daw.editing_name_buf[l - 1] = '\0';
                    }
                    continue;
                }
                if (g_daw.input_focus == INPUT_FOCUS_MARKER) {
                    if (e.key.keysym.sym == SDLK_RETURN) {
                        strncpy(p->markers[g_daw.editing_marker_idx].text, g_daw.editing_marker_buf, 63);
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_marker_idx = -1;
                        SDL_StopTextInput();
                        p->is_dirty = true;
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                        g_daw.input_focus = INPUT_FOCUS_NONE;
                        g_daw.editing_marker_idx = -1;
                        SDL_StopTextInput();
                    } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                        size_t l = strlen(g_daw.editing_marker_buf);
                        if (l > 0) g_daw.editing_marker_buf[l - 1] = '\0';
                    }
                    continue;
                }

                if ((mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_z) perform_undo();
                else if ((mod & KMOD_CTRL) && (e.key.keysym.sym == SDLK_y || ((mod & KMOD_SHIFT) && e.key.keysym.sym == SDLK_z))) perform_redo();
                else if ((mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_s) {
                    save_project_file();
                    p->is_dirty = false;
                }
                else if (e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_PLUS || e.key.keysym.sym == SDLK_KP_PLUS)
                    p->pixels_per_second = fminf(400.0f, p->pixels_per_second * 1.25f);
                else if (e.key.keysym.sym == SDLK_MINUS || e.key.keysym.sym == SDLK_KP_MINUS)
                    p->pixels_per_second = fmaxf(15.0f, p->pixels_per_second * 0.8f);
                else if (e.key.keysym.sym == SDLK_m) {
                    if (p->num_markers < MAX_MARKERS) {
                        DawMarker *mk = &p->markers[p->num_markers++];
                        mk->time_sec = p->playhead_seconds;
                        snprintf(mk->text, sizeof(mk->text), "Marker %d", p->num_markers);
                        g_daw.input_focus = INPUT_FOCUS_MARKER;
                        g_daw.editing_marker_idx = p->num_markers - 1;
                        strncpy(g_daw.editing_marker_buf, mk->text, 63);
                        SDL_StartTextInput();
                        p->is_dirty = true;
                    }
                }
                else if (e.key.keysym.sym == SDLK_SPACE) p->is_playing = !p->is_playing;
                else if (e.key.keysym.sym == SDLK_v) p->video.is_open = !p->video.is_open;
                else if (e.key.keysym.sym == SDLK_HOME) { p->playhead_seconds = 0.0; }
                else if (e.key.keysym.sym == SDLK_s && !(mod & KMOD_CTRL)) {
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
                                r_item->raw_file_handle = r_item->raw_audio_path[0] ? fopen(r_item->raw_audio_path, "rb") : NULL;
                                p->num_items++;
                                break;
                            }
                        }
                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x, my = e.button.y;
                SDL_Keymod mod = SDL_GetModState();
                bool shift_pressed = (mod & KMOD_SHIFT);
                bool ctrl_pressed = (mod & KMOD_CTRL);

                if (g_daw.save_prompt_open) {
                    int dia_w = 380, dia_h = 140;
                    int dia_x = (win_w - dia_w) / 2;
                    int dia_y = (win_h - dia_h) / 2;
                    int btn_y = dia_y + dia_h - 44;

                    // [Save]
                    if (mx >= dia_x + 18 && mx <= dia_x + 118 && my >= btn_y && my <= btn_y + 30) {
                        handle_save_modal_choice(1, &running);
                        continue;
                    }
                    // [Don't Save]
                    if (mx >= dia_x + 130 && mx <= dia_x + 250 && my >= btn_y && my <= btn_y + 30) {
                        handle_save_modal_choice(2, &running);
                        continue;
                    }
                    // [Cancel]
                    if (mx >= dia_x + 262 && mx <= dia_x + 362 && my >= btn_y && my <= btn_y + 30) {
                        handle_save_modal_choice(3, &running);
                        continue;
                    }
                    continue;
                }

                if (g_daw.input_focus != INPUT_FOCUS_NONE) {
                    if (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME) {
                        strncpy(p->track_names[g_daw.editing_track_name_idx], g_daw.editing_name_buf, 63);
                        p->is_dirty = true;
                    } else if (g_daw.input_focus == INPUT_FOCUS_MARKER) {
                        strncpy(p->markers[g_daw.editing_marker_idx].text, g_daw.editing_marker_buf, 63);
                        p->is_dirty = true;
                    }
                    g_daw.input_focus = INPUT_FOCUS_NONE;
                    g_daw.editing_track_name_idx = -1;
                    g_daw.editing_marker_idx = -1;
                    SDL_StopTextInput();
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
                            } else {
                                g_daw.active_tab = t;
                            }
                            break;
                        }
                        cur_tab_x += cur_tab_w;
                    }

                    if (!clicked_tab && mx >= cur_tab_x && mx <= cur_tab_x + add_btn_w && g_daw.num_tabs < MAX_TABS) {
                        char name[64]; snprintf(name, sizeof(name), "Project %d", g_daw.num_tabs + 1);
                        init_tab_defaults(g_daw.num_tabs, name);
                        g_tab_crossfade[g_daw.num_tabs] = true;
                        g_daw.active_tab = g_daw.num_tabs++;
                    }
                    continue;
                }

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
                        continue;
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
                        continue;
                    }
                }

                if ((g_daw.menu_file_open || g_daw.menu_action_open) && (mx > 320 || my > 270)) {
                    g_daw.menu_file_open = false;
                    g_daw.menu_action_open = false;
                    g_daw.submenu_export_audio_open = false;
                    g_daw.submenu_export_video_open = false;
                    continue;
                }

                if (g_daw.menu_file_open) {
                    if (mx >= 8 && mx <= 200) {
                        int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                        if (my >= m_start && my < m_start + 24) { save_project_file(); p->is_dirty = false; g_daw.menu_file_open = false; continue; }
                        else if (my >= m_start + 24 && my < m_start + 48) { load_project_file(); p->is_dirty = false; g_daw.menu_file_open = false; continue; }
                        else if (my >= m_start + 48 && my < m_start + 72) {
                            char pth[MAX_PATH];
                            if (open_generic_dialog(pth, sizeof(pth), "Media Files\0*.wav;*.mp3;*.flac;*.mp4;*.mkv;*.mov;*.avi;*.webm\0All Files (*.*)\0*.*\0", false)) {
                                if (p->num_items < MAX_ITEMS) {
                                    push_undo_state();
                                    int new_idx = p->num_items;
                                    AudioItem *it = &p->items[new_idx];
                                    memset(it, 0, sizeof(AudioItem));
                                    it->track_idx = 0;
                                    it->start_time = p->playhead_seconds;
                                    if (process_media_cache(pth, it) == 0) {
                                        if (!is_crossfade_active()) {
                                            it->start_time = clamp_non_overlapping_start(p, new_idx, 0, it->start_time, it->duration);
                                        }
                                        p->num_items++;
                                        if (it->has_video) {
                                            open_video_by_path(&p->video, it->file_path, ren);
                                        }
                                    }
                                }
                            }
                            g_daw.menu_file_open = false;
                            continue;
                        }
                        else if (my >= m_start + 72 && my < m_start + 96) { g_daw.submenu_export_audio_open = !g_daw.submenu_export_audio_open; g_daw.submenu_export_video_open = false; continue; }
                        else if (my >= m_start + 96 && my < m_start + 120) { g_daw.submenu_export_video_open = !g_daw.submenu_export_video_open; g_daw.submenu_export_audio_open = false; continue; }
                        else if (my >= m_start + 120 && my < m_start + 144) { export_batch_tracks(); g_daw.menu_file_open = false; continue; }
                    }
                    if (g_daw.submenu_export_audio_open && mx >= 200 && mx <= 310) {
                        int s_start = TABBAR_HEIGHT + TOPBAR_HEIGHT + 72;
                        if (my >= s_start && my < s_start + 22) { export_rendered_audio("wav"); g_daw.menu_file_open = false; continue; }
                        if (my >= s_start + 22 && my < s_start + 44) { export_rendered_audio("mp3"); g_daw.menu_file_open = false; continue; }
                        if (my >= s_start + 44 && my < s_start + 66) { export_rendered_audio("flac"); g_daw.menu_file_open = false; continue; }
                        if (my >= s_start + 66 && my < s_start + 88) { export_rendered_audio("ogg"); g_daw.menu_file_open = false; continue; }
                    }
                    if (g_daw.submenu_export_video_open && mx >= 200 && mx <= 310) {
                        int s_start = TABBAR_HEIGHT + TOPBAR_HEIGHT + 100;
                        if (my >= s_start && my < s_start + 22) { export_rendered_audio("mp4"); g_daw.menu_file_open = false; continue; }
                        if (my >= s_start + 22 && my < s_start + 44) { export_rendered_audio("mkv"); g_daw.menu_file_open = false; continue; }
                        if (my >= s_start + 44 && my < s_start + 66) { export_rendered_audio("mov"); g_daw.menu_file_open = false; continue; }
                        if (my >= s_start + 66 && my < s_start + 88) { export_rendered_audio("avi"); g_daw.menu_file_open = false; continue; }
                    }
                }

                if (g_daw.menu_action_open) {
                    if (mx >= 56 && mx <= 190) {
                        int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                        if (my >= m_start && my < m_start + 26) {
                            g_daw.script_manager_open = true;
                            g_daw.script_manager_is_python = false;
                            g_daw.menu_action_open = false;
                            continue;
                        } else if (my >= m_start + 26 && my < m_start + 52) {
                            g_daw.script_manager_open = true;
                            g_daw.script_manager_is_python = true;
                            g_daw.menu_action_open = false;
                            continue;
                        }
                    }
                }

                if (my >= TABBAR_HEIGHT && my <= TABBAR_HEIGHT + TOPBAR_HEIGHT) {
                    if (mx >= 8 && mx <= 50) { g_daw.menu_file_open = !g_daw.menu_file_open; g_daw.menu_action_open = false; }
                    else if (mx >= 56 && mx <= 116) { g_daw.menu_action_open = !g_daw.menu_action_open; g_daw.menu_file_open = false; }
                    else if (mx >= 122 && mx <= 186) {
                        p->show_marker_lane = !p->show_marker_lane;
                    }
                    else if (mx >= 192 && mx <= 284) p->video.is_open = !p->video.is_open;
                    else if (mx >= 288 && mx <= 412) {
                        bool next_xfade = !is_crossfade_active();
                        set_crossfade_active(next_xfade);
                        if (!next_xfade) {
                            push_undo_state();
                            resolve_all_track_overlaps_edge_to_edge(p);
                        }
                    }
                    else if (mx >= win_w - 228 && mx < win_w - 176) perform_undo();
                    else if (mx >= win_w - 172 && mx < win_w - 120) perform_redo();
                    else if (mx >= win_w - 116 && mx < win_w - 60) p->pixels_per_second = fmaxf(15.0f, p->pixels_per_second * 0.8f);
                    else if (mx >= win_w - 56 && mx < win_w - 6) p->pixels_per_second = fminf(400.0f, p->pixels_per_second * 1.25f);
                    continue;
                }

                if (abs(mx - g_daw.tcp_width) <= 5 && my > TABBAR_HEIGHT + TOPBAR_HEIGHT) {
                    resizing_tcp_width = true;
                    continue;
                }

                int corner_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                if (mx < g_daw.tcp_width && my > corner_y && my <= corner_y + RULER_HEIGHT + m_lane_h) {
                    if (mx >= 8 && mx <= 88) {
                        push_undo_state();
                        p->track_heights[p->num_tracks] = 84;
                        snprintf(p->track_names[p->num_tracks], 64, "Track %d", p->num_tracks + 1);
                        p->num_tracks++;
                    } else if (mx >= 94 && mx <= 180) p->is_playing = !p->is_playing;
                    continue;
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

                int marker_y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT;
                if (p->show_marker_lane && mx >= g_daw.tcp_width && my > marker_y && my <= marker_y + MARKER_BAR_HEIGHT) {
                    double m_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                    if (e.button.button == SDL_BUTTON_RIGHT && p->num_markers < MAX_MARKERS) {
                        DawMarker *mk = &p->markers[p->num_markers++];
                        mk->time_sec = m_time;
                        snprintf(mk->text, sizeof(mk->text), "Marker %d", p->num_markers);
                        g_daw.input_focus = INPUT_FOCUS_MARKER;
                        g_daw.editing_marker_idx = p->num_markers - 1;
                        strncpy(g_daw.editing_marker_buf, mk->text, 63);
                        SDL_StartTextInput();
                        p->is_dirty = true;
                    } else {
                        for (int m = 0; m < p->num_markers; m++) {
                            int mk_x = g_daw.tcp_width + (int)((p->markers[m].time_sec - p->scroll_x_sec) * p->pixels_per_second);
                            if (abs(mx - mk_x) <= 28) {
                                if (e.button.clicks == 2) {
                                    g_daw.input_focus = INPUT_FOCUS_MARKER;
                                    g_daw.editing_marker_idx = m;
                                    strncpy(g_daw.editing_marker_buf, p->markers[m].text, 63);
                                    SDL_StartTextInput();
                                }
                                break;
                            }
                        }
                    }
                    continue;
                }

                int ruler_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
                if (mx >= g_daw.tcp_width && my > ruler_y && my <= ruler_y + RULER_HEIGHT) {
                    scrubbing_ruler = true;
                    p->playhead_seconds = fmax(0.0, p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second);
                    continue;
                }

                // Track Controls & Header Selection
                int header_top_y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT + m_lane_h;
                if (mx < g_daw.tcp_width && my > header_top_y) {
                    int cur_y = header_top_y - p->scroll_y_px;
                    for (int t = 0; t < p->num_tracks; t++) {
                        int th = p->track_heights[t];
                        if (abs(my - (cur_y + th)) <= 5) {
                            resizing_track_idx = t;
                            track_orig_height = th;
                            mouse_drag_start_y = my;
                            break;
                        }
                        if (my >= cur_y && my < cur_y + th) {
                            int rel_y = my - cur_y;

                            if (ctrl_pressed) {
                                p->track_selected[t] = !p->track_selected[t];
                            } else if (shift_pressed) {
                                for (int tr = 0; tr <= t; tr++) p->track_selected[tr] = true;
                            } else {
                                for (int tr = 0; tr < p->num_tracks; tr++) p->track_selected[tr] = false;
                                p->track_selected[t] = true;
                            }

                            if (rel_y >= 30 && rel_y <= 56) {
                                if (mx >= 8 && mx <= 26) { p->track_db[t] -= 1.0f; p->is_dirty = true; }
                                else if (mx >= 76 && mx <= 94) { p->track_db[t] += 1.0f; p->is_dirty = true; }
                                else if (mx >= 110 && mx <= 132) { p->tracks[t].mute = !p->tracks[t].mute; p->is_dirty = true; }
                                else if (mx >= 138 && mx <= 160) { p->tracks[t].solo = !p->tracks[t].solo; p->is_dirty = true; }
                            } else if (rel_y >= 6 && rel_y <= 28 && mx >= 8 && mx <= g_daw.tcp_width - 16) {
                                if (e.button.clicks == 2) {
                                    g_daw.input_focus = INPUT_FOCUS_TRACK_NAME;
                                    g_daw.editing_track_name_idx = t;
                                    strncpy(g_daw.editing_name_buf, p->track_names[t], 63);
                                    SDL_StartTextInput();
                                }
                            }
                            break;
                        }
                        cur_y += th;
                    }
                    continue;
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
                            push_undo_state();
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
                        marquee_selecting = true;
                        marquee_start_x = mx; marquee_start_y = my;
                        marquee_cur_x = mx; marquee_cur_y = my;
                    }
                    continue;
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                p->video.is_dragging = false; p->video.is_resizing = false;
                dragging_scrollbar_h = false; dragging_scrollbar_v = false;
                scrubbing_ruler = false;
                resizing_tcp_width = false;
                resizing_track_idx = -1;
                g_daw.script_win_dragging = false;
                if (dragging_item_idx != -1) p->is_dirty = true;
                dragging_item_idx = -1; dragging_trim_left = false; dragging_trim_right = false;

                if (marquee_selecting) {
                    marquee_selecting = false;
                    int x1 = fmin(marquee_start_x, marquee_cur_x);
                    int x2 = fmax(marquee_start_x, marquee_cur_x);
                    int y1 = fmin(marquee_start_y, marquee_cur_y);
                    int y2 = fmax(marquee_start_y, marquee_cur_y);

                    for (int i = 0; i < p->num_items; i++) {
                        AudioItem *it = &p->items[i];
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

                if (g_daw.script_win_dragging) {
                    g_daw.script_win_rect.x = mx - g_daw.script_drag_offset_x;
                    g_daw.script_win_rect.y = my - g_daw.script_drag_offset_y;
                }

                int header_top_y = TABBAR_HEIGHT + TOPBAR_HEIGHT + RULER_HEIGHT + m_lane_h;
                bool hovered_edge = false;
                bool hovered_ibeam = false;

                if (abs(mx - g_daw.tcp_width) <= 5 && my > TABBAR_HEIGHT + TOPBAR_HEIGHT) {
                    SDL_SetCursor(g_cursor_size_we);
                    hovered_edge = true;
                } else if (mx < g_daw.tcp_width && my > header_top_y) {
                    int cur_y = header_top_y - p->scroll_y_px;
                    for (int t = 0; t < p->num_tracks; t++) {
                        int th = p->track_heights[t];
                        if (abs(my - (cur_y + th)) <= 5) {
                            SDL_SetCursor(g_cursor_size_ns);
                            hovered_edge = true;
                            break;
                        }
                        if (my >= cur_y + 6 && my <= cur_y + 28 && mx >= 8 && mx <= g_daw.tcp_width - 16) {
                            SDL_SetCursor(g_cursor_ibeam);
                            hovered_ibeam = true;
                            break;
                        }
                        cur_y += th;
                    }
                } else if (mx >= g_daw.tcp_width && my > header_top_y) {
                    for (int i = 0; i < p->num_items; i++) {
                        AudioItem *it = &p->items[i];
                        int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
                        int iw = (int)(it->duration * p->pixels_per_second);
                        int iy = get_track_y_pos(it->track_idx);
                        if (my >= iy && my <= iy + p->track_heights[it->track_idx]) {
                            if (abs(mx - ix) <= 8 || abs(mx - (ix + iw)) <= 8) {
                                SDL_SetCursor(g_cursor_size_we);
                                hovered_edge = true;
                                break;
                            }
                        }
                    }
                }
                if (!hovered_edge && !hovered_ibeam && !resizing_tcp_width && resizing_track_idx < 0) {
                    SDL_SetCursor(g_cursor_arrow);
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
                    p->scroll_y_px = (int)(orig_scroll_y + (delta / view_h) * max_h_tracks);
                    if (p->scroll_y_px < 0) p->scroll_y_px = 0;
                    if (p->scroll_y_px > max_y) p->scroll_y_px = max_y;
                }
                if (scrubbing_ruler && mx >= g_daw.tcp_width && mx < win_w - SCROLLBAR_SIZE) {
                    p->playhead_seconds = fmax(0.0, p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second);
                }
                if (dragging_item_idx != -1) {
                    AudioItem *it = &p->items[dragging_item_idx];
                    double cur_mouse_time = p->scroll_x_sec + (double)(mx - g_daw.tcp_width) / p->pixels_per_second;
                    double delta_t = cur_mouse_time - drag_start_mouse_time;
                    bool xfade_on = is_crossfade_active();

                    if (dragging_trim_left) {
                        double new_start = item_orig_start + delta_t;
                        if (new_start < 0.0) new_start = 0.0;
                        if (item_orig_offset + (new_start - item_orig_start) < 0.0) {
                            new_start = item_orig_start - item_orig_offset;
                        }
                        if (!xfade_on) {
                            // Clamp left trim so it stops at the right edge of any preceding clip on the same track
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
                        }
                    } else if (dragging_trim_right) {
                        double new_dur = item_orig_dur + delta_t;
                        if (it->file_total_duration > 0.0 && it->offset_in_file + new_dur > it->file_total_duration) {
                            new_dur = it->file_total_duration - it->offset_in_file;
                        }
                        if (!xfade_on) {
                            // Clamp right trim so it stops at the left edge of any following clip on the same track
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
                        if (new_dur > 0.1) it->duration = new_dur;
                    } else {
                        int target_trk = it->track_idx;
                        int cur_y = header_top_y - p->scroll_y_px;
                        for (int t = 0; t < p->num_tracks; t++) {
                            if (my >= cur_y && my < cur_y + p->track_heights[t]) { target_trk = t; break; }
                            cur_y += p->track_heights[t];
                        }
                        double desired_start = fmax(0.0, item_orig_start + delta_t);
                        it->track_idx = target_trk;
                        if (!xfade_on) {
                            it->start_time = clamp_non_overlapping_start(p, dragging_item_idx, target_trk, desired_start, it->duration);
                        } else {
                            it->start_time = desired_start;
                        }
                    }
                }
            }
        }

        // ================= TOP-TRACK VIDEO SYNCHRONIZATION =================
        if (p->video.is_open) {
            AudioItem *v_clip = get_active_video_clip_at_playhead(p);

            if (v_clip != NULL) {
                double media_internal_time = (p->playhead_seconds - v_clip->start_time) + v_clip->offset_in_file;
                
                // Load the original media container (file_path), NOT the raw PCM audio cache (raw_audio_path)
                if (strncmp(p->video.current_loaded_path, v_clip->file_path, sizeof(p->video.current_loaded_path)) != 0 || !p->video.texture) {
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
        }

        // ================= RENDERING =================
        SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
        SDL_RenderClear(ren);

        SDL_Color c_white = { 224, 224, 224, 255 };
        SDL_Color c_muted = { 130, 130, 130, 255 };
        SDL_Color c_blue  = { 26, 86, 219, 255 };
        (void)c_blue;

        // 1. Google Chrome Style Tab Bar
        SDL_Rect tab_bar = { 0, 0, win_w, TABBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 18, 18, 18, 255);
        SDL_RenderFillRect(ren, &tab_bar);

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
            SDL_SetRenderDrawColor(ren, is_active ? 36 : 22, is_active ? 36 : 22, is_active ? 36 : 22, 255);
            SDL_RenderFillRect(ren, &tr);

            char label_buf[70];
            if (g_daw.tabs[t].is_dirty) {
                snprintf(label_buf, sizeof(label_buf), "*%s", g_daw.tabs[t].project_name);
            } else {
                snprintf(label_buf, sizeof(label_buf), "%s", g_daw.tabs[t].project_name);
            }
            draw_text(ren, label_buf, cur_tab_x + 10, 9, is_active ? c_white : c_muted);

            draw_text(ren, "x", cur_tab_x + cur_tab_w - 18, 9, is_active ? c_white : c_muted);
            cur_tab_x += cur_tab_w;
        }

        SDL_Rect btn_add_tab = { cur_tab_x, 2, add_btn_w, TABBAR_HEIGHT - 2 };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &btn_add_tab);
        draw_text(ren, "+", cur_tab_x + 12, 9, c_white);

        // 2. Top Bar
        SDL_Rect topbar = { 0, TABBAR_HEIGHT, win_w, TOPBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 28, 28, 28, 255);
        SDL_RenderFillRect(ren, &topbar);

        SDL_Rect btn_file = { 8, TABBAR_HEIGHT + 5, 46, 26 };
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderFillRect(ren, &btn_file);
        draw_text(ren, "File", 18, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect btn_act = { 56, TABBAR_HEIGHT + 5, 60, 26 };
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderFillRect(ren, &btn_act);
        draw_text(ren, "Action", 64, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect btn_mk_toggle = { 122, TABBAR_HEIGHT + 5, 66, 26 };
        SDL_SetRenderDrawColor(ren, p->show_marker_lane ? 55 : 40, p->show_marker_lane ? 55 : 40, p->show_marker_lane ? 55 : 40, 255);
        SDL_RenderFillRect(ren, &btn_mk_toggle);
        draw_text(ren, "Marker", 130, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect v_menu = { 194, TABBAR_HEIGHT + 5, 88, 26 };
        SDL_SetRenderDrawColor(ren, p->video.is_open ? 55 : 40, p->video.is_open ? 55 : 40, p->video.is_open ? 55 : 40, 255);
        SDL_RenderFillRect(ren, &v_menu);
        draw_text(ren, "View:Video", 200, TABBAR_HEIGHT + 13, c_white);

        bool xfade_active = is_crossfade_active();
        SDL_Rect xfade_btn = { 288, TABBAR_HEIGHT + 5, 124, 26 };
        SDL_SetRenderDrawColor(ren, xfade_active ? 28 : 40, xfade_active ? 82 : 40, xfade_active ? 62 : 40, 255);
        SDL_RenderFillRect(ren, &xfade_btn);
        SDL_SetRenderDrawColor(ren, xfade_active ? 64 : 60, xfade_active ? 160 : 60, xfade_active ? 115 : 60, 255);
        SDL_RenderDrawRect(ren, &xfade_btn);
        draw_text(ren, xfade_active ? "Crossfade: ON" : "Crossfade: OFF", 294, TABBAR_HEIGHT + 13, c_white);

        char clock_buf[32];
        format_timecode(p->playhead_seconds, clock_buf, sizeof(clock_buf));
        int clock_x = (win_w / 2) - 52;
        SDL_Rect clock_bg = { clock_x - 10, TABBAR_HEIGHT + 4, 128, 28 };
        SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
        SDL_RenderFillRect(ren, &clock_bg);
        draw_text(ren, clock_buf, clock_x - 4, TABBAR_HEIGHT + 12, c_white);

        SDL_Rect btn_undo = { win_w - 228, TABBAR_HEIGHT + 5, 52, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_undo);
        draw_text(ren, "Undo", win_w - 218, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect btn_redo = { win_w - 172, TABBAR_HEIGHT + 5, 52, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_redo);
        draw_text(ren, "Redo", win_w - 162, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect btn_zm_out = { win_w - 116, TABBAR_HEIGHT + 5, 56, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_zm_out);
        draw_text(ren, "Zoom-", win_w - 110, TABBAR_HEIGHT + 13, c_white);

        SDL_Rect btn_zm_in = { win_w - 56, TABBAR_HEIGHT + 5, 50, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_zm_in);
        draw_text(ren, "Zoom+", win_w - 50, TABBAR_HEIGHT + 13, c_white);

        // 3. Corner Header
        int corner_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
        SDL_Rect corner_area = { 0, corner_y, g_daw.tcp_width, RULER_HEIGHT + m_lane_h };
        SDL_SetRenderDrawColor(ren, 32, 32, 32, 255);
        SDL_RenderFillRect(ren, &corner_area);

        SDL_Rect btn_corner_add = { 8, corner_y + 10, 72, 26 };
        SDL_SetRenderDrawColor(ren, 48, 48, 48, 255);
        SDL_RenderFillRect(ren, &btn_corner_add);
        draw_text(ren, "+ Track", 16, corner_y + 18, c_white);

        SDL_Rect btn_corner_play = { 86, corner_y + 10, 72, 26 };
        SDL_SetRenderDrawColor(ren, p->is_playing ? 65 : 48, 48, 48, 255);
        SDL_RenderFillRect(ren, &btn_corner_play);
        draw_text(ren, p->is_playing ? "Pause" : "Play", 102, corner_y + 18, c_white);

        // 4. Time Ruler & Marker Bar
        SDL_Rect ruler = { g_daw.tcp_width, corner_y, view_w, RULER_HEIGHT };
        SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
        SDL_RenderFillRect(ren, &ruler);

        double min_px_gap = 90.0;
        double sec_per_tick = min_px_gap / p->pixels_per_second;
        double nice_steps[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0 };
        int num_steps = sizeof(nice_steps) / sizeof(nice_steps[0]);
        double chosen_step = nice_steps[num_steps - 1];
        for (int s = 0; s < num_steps; s++) {
            if (nice_steps[s] >= sec_per_tick) { chosen_step = nice_steps[s]; break; }
        }

        double start_tick = floor(p->scroll_x_sec / chosen_step) * chosen_step;
        double end_tick = p->scroll_x_sec + (double)view_w / p->pixels_per_second + chosen_step;

        for (double t = start_tick; t <= end_tick; t += chosen_step) {
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
            draw_text(ren, t_str, rx + 4, corner_y + 8, c_muted);
            SDL_SetRenderDrawColor(ren, 55, 55, 55, 255);
            SDL_RenderDrawLine(ren, rx, corner_y + 16, rx, corner_y + RULER_HEIGHT);
        }

        int marker_y = corner_y + RULER_HEIGHT;
        if (p->show_marker_lane) {
            SDL_Rect marker_bg = { g_daw.tcp_width, marker_y, view_w, MARKER_BAR_HEIGHT };
            SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
            SDL_RenderFillRect(ren, &marker_bg);
            SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
            SDL_RenderDrawLine(ren, g_daw.tcp_width, marker_y + MARKER_BAR_HEIGHT - 1, g_daw.tcp_width + view_w, marker_y + MARKER_BAR_HEIGHT - 1);

            for (int m = 0; m < p->num_markers; m++) {
                int mk_x = g_daw.tcp_width + (int)((p->markers[m].time_sec - p->scroll_x_sec) * p->pixels_per_second);
                if (mk_x >= g_daw.tcp_width && mk_x <= g_daw.tcp_width + view_w) {
                    SDL_Rect m_tag = { mk_x, marker_y + 2, 74, MARKER_BAR_HEIGHT - 4 };
                    bool is_editing_this = (g_daw.input_focus == INPUT_FOCUS_MARKER && g_daw.editing_marker_idx == m);
                    SDL_SetRenderDrawColor(ren, is_editing_this ? 26 : 48, is_editing_this ? 86 : 48, is_editing_this ? 219 : 48, 255);
                    SDL_RenderFillRect(ren, &m_tag);
                    if (is_editing_this) {
                        draw_text(ren, g_daw.editing_marker_buf, mk_x + 4, marker_y + 6, c_white);
                    } else {
                        draw_text(ren, p->markers[m].text, mk_x + 4, marker_y + 6, c_white);
                    }
                }
            }
        }

        int header_top_y = marker_y + m_lane_h;
        SDL_Rect timeline_clip = { g_daw.tcp_width, header_top_y, view_w, view_h };
        SDL_RenderSetClipRect(ren, &timeline_clip);

        for (double t = start_tick; t <= end_tick; t += chosen_step) {
            int gx = g_daw.tcp_width + (int)((t - p->scroll_x_sec) * p->pixels_per_second);
            if (gx < g_daw.tcp_width || gx > g_daw.tcp_width + view_w) continue;
            SDL_SetRenderDrawColor(ren, 38, 38, 38, 255);
            SDL_RenderDrawLine(ren, gx, header_top_y, gx, header_top_y + view_h);
        }

        // Timeline tracks background and light selection borders
        int track_cur_y = header_top_y - p->scroll_y_px;
        for (int i = 0; i < p->num_tracks; i++) {
            int th = p->track_heights[i];
            if (p->track_selected[i]) {
                SDL_Rect sel_lane_border = { g_daw.tcp_width, track_cur_y, view_w, th };
                SDL_SetRenderDrawColor(ren, 40, 75, 50, 255);
                SDL_RenderDrawRect(ren, &sel_lane_border);
            }
            track_cur_y += th;
            SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
            SDL_RenderDrawLine(ren, g_daw.tcp_width, track_cur_y, g_daw.tcp_width + view_w, track_cur_y);
        }

        // 5. Render Clips (Pass 1A: Clip Backgrounds, Pass 1B: Faded Waveforms & Labels, Pass 2: Standard Studio Crossfade Overlays)
        for (int i = 0; i < p->num_items; i++) {
            AudioItem *it = &p->items[i];
            int iy = get_track_y_pos(it->track_idx) + 2;
            int ih = p->track_heights[it->track_idx] - 4;
            int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
            int iw = (int)(it->duration * p->pixels_per_second);

            if (ix + iw < g_daw.tcp_width || ix > g_daw.tcp_width + view_w) continue;

            SDL_Rect clip_r = { ix, iy, iw, ih };
            SDL_SetRenderDrawColor(ren, it->is_selected ? 46 : 34, it->is_selected ? 50 : 36, it->is_selected ? 56 : 40, 255);
            SDL_RenderFillRect(ren, &clip_r);

            // Subtle header strip at the top of each clip
            SDL_Rect hdr_r = { ix, iy, iw, 18 };
            SDL_SetRenderDrawColor(ren, it->is_selected ? 60 : 42, it->is_selected ? 68 : 45, it->is_selected ? 78 : 52, 255);
            SDL_RenderFillRect(ren, &hdr_r);
        }

        // Pass 1B: Render Waveforms (attenuated by equal-power crossfade envelope inside overlap zones), Borders & Labels
        for (int i = 0; i < p->num_items; i++) {
            AudioItem *it = &p->items[i];
            int iy = get_track_y_pos(it->track_idx) + 2;
            int ih = p->track_heights[it->track_idx] - 4;
            int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
            int iw = (int)(it->duration * p->pixels_per_second);

            if (ix + iw < g_daw.tcp_width || ix > g_daw.tcp_width + view_w) continue;

            float gain_multiplier = db_to_linear(p->track_db[it->track_idx]);
            int mid_y = iy + ih / 2;

            // Zero-line center axis
            SDL_SetRenderDrawColor(ren, 52, 55, 60, 255);
            SDL_RenderDrawLine(ren, ix, mid_y, ix + iw, mid_y);

            if (it->peaks && it->peak_count > 0) {
                for (int col = 0; col < iw; col++) {
                    int px = ix + col;
                    if (px < g_daw.tcp_width || px > g_daw.tcp_width + view_w) continue;

                    double rel_sec = (double)col / p->pixels_per_second;
                    double timeline_sec = it->start_time + rel_sec;
                    double cur_item_sec = it->offset_in_file + rel_sec;
                    int bin_idx = (int)((cur_item_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN);

                    if (bin_idx >= 0 && bin_idx < it->peak_count) {
                        float xfade_env = compute_item_crossfade_gain(p, i, timeline_sec, xfade_active);
                        float env_gain = gain_multiplier * xfade_env;
                        float mn = it->peaks[bin_idx].min_val * env_gain;
                        float mx = it->peaks[bin_idx].max_val * env_gain;

                        int y1 = mid_y - (int)(mx * (ih / 2.0f - 4));
                        int y2 = mid_y - (int)(mn * (ih / 2.0f - 4));
                        if (y1 < iy + 2) y1 = iy + 2;
                        if (y2 > iy + ih - 2) y2 = iy + ih - 2;
                        if (y1 == y2) y2 += 1;

                        if (xfade_env < 0.999f) {
                            SDL_SetRenderDrawColor(ren, 165, 205, 225, 255);
                        } else {
                            SDL_SetRenderDrawColor(ren, 196, 202, 210, 255);
                        }
                        SDL_RenderDrawLine(ren, px, y1, px, y2);
                    }
                }
            }

            SDL_Rect clip_r = { ix, iy, iw, ih };
            SDL_SetRenderDrawColor(ren, it->is_selected ? 190 : 82, it->is_selected ? 200 : 86, it->is_selected ? 215 : 94, 255);
            SDL_RenderDrawRect(ren, &clip_r);

            draw_text(ren, it->label, ix + 6, iy + 4, c_white);
        }

        // Pass 2: Standard Audio Editing Crossfade Box & Equal-Power X-Curves on Overlapping Clips
        if (xfade_active) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            for (int i = 0; i < p->num_items; i++) {
                AudioItem *it = &p->items[i];
                double it_s = it->start_time;
                double it_e = it->start_time + it->duration;

                for (int o = 0; o < p->num_items; o++) {
                    if (o == i) continue;
                    AudioItem *ot = &p->items[o];
                    if (ot->track_idx != it->track_idx) continue;

                    double ot_s = ot->start_time;
                    double ot_e = ot->start_time + ot->duration;

                    // Process each ordered pair once (ot = outgoing earlier clip, it = incoming later clip)
                    bool ot_is_earlier = (ot_s < it_s - 1e-6) || (fabs(ot_s - it_s) <= 1e-6 && o < i);
                    if (!ot_is_earlier) continue;

                    double overlap_s = it_s;
                    double overlap_e = fmin(it_e, ot_e);
                    if (overlap_e <= overlap_s + 0.001) continue;

                    int iy = get_track_y_pos(it->track_idx) + 2;
                    int ih = p->track_heights[it->track_idx] - 4;
                    int ox = g_daw.tcp_width + (int)((overlap_s - p->scroll_x_sec) * p->pixels_per_second);
                    int ox_end = g_daw.tcp_width + (int)((overlap_e - p->scroll_x_sec) * p->pixels_per_second);
                    int ow = ox_end - ox;
                    if (ow < 2) ow = 2;

                    if (ox + ow < g_daw.tcp_width || ox > g_daw.tcp_width + view_w) continue;

                    // 1. Translucent crossfade zone highlight box
                    SDL_Rect xf_box = { ox, iy, ow, ih };
                    SDL_SetRenderDrawColor(ren, 245, 175, 55, 28);
                    SDL_RenderFillRect(ren, &xf_box);

                    int top_pad = iy + 3;
                    int bot_pad = iy + ih - 4;
                    int curve_h = bot_pad - top_pad;
                    if (curve_h < 8) curve_h = 8;

                    int prev_x = ox;
                    int prev_y_out = top_pad;
                    int prev_y_in  = bot_pad;

                    // 2. Shade upper exterior region and draw continuous 2px Equal-Power S/Cosine Crossfade X-Curves
                    for (int px = 0; px < ow; px++) {
                        int cur_x = ox + px;
                        float u = (ow > 1) ? ((float)px / (float)(ow - 1)) : 0.5f;

                        // Equal-power / smooth S-curve envelope values in [0..1]
                        float gain_out = cosf(u * 1.57079632679f);
                        float gain_in  = sinf(u * 1.57079632679f);

                        int y_out = bot_pad - (int)(gain_out * curve_h);
                        int y_in  = bot_pad - (int)(gain_in  * curve_h);

                        // Darken region above the active crossfade envelope (standard DAW crossfade shading)
                        int y_top_env = (y_out < y_in) ? y_out : y_in;
                        if (cur_x >= g_daw.tcp_width && cur_x <= g_daw.tcp_width + view_w && y_top_env > iy + 1) {
                            SDL_SetRenderDrawColor(ren, 12, 14, 18, 115);
                            SDL_RenderDrawLine(ren, cur_x, iy + 1, cur_x, y_top_env);
                        }

                        if (px > 0) {
                            // Outgoing Fade-Out Curve (warm coral-amber)
                            SDL_SetRenderDrawColor(ren, 245, 115, 75, 245);
                            SDL_RenderDrawLine(ren, prev_x, prev_y_out, cur_x, y_out);
                            SDL_RenderDrawLine(ren, prev_x, prev_y_out + 1, cur_x, y_out + 1);

                            // Incoming Fade-In Curve (bright gold-cyan studio accent)
                            SDL_SetRenderDrawColor(ren, 85, 215, 240, 245);
                            SDL_RenderDrawLine(ren, prev_x, prev_y_in, cur_x, y_in);
                            SDL_RenderDrawLine(ren, prev_x, prev_y_in + 1, cur_x, y_in + 1);
                        }

                        prev_x = cur_x;
                        prev_y_out = y_out;
                        prev_y_in  = y_in;
                    }

                    // 3. Crisp Crossfade Boundary Frame & Corner Handles
                    SDL_SetRenderDrawColor(ren, 235, 175, 65, 215);
                    SDL_RenderDrawRect(ren, &xf_box);

                    // 4. Center Equal-Power Crossing Handle (-3dB intersection point at u = 0.5)
                    int cross_x = ox + ow / 2;
                    int cross_y = bot_pad - (int)(0.70710678f * curve_h);
                    SDL_Rect cross_handle = { cross_x - 3, cross_y - 3, 6, 6 };
                    SDL_SetRenderDrawColor(ren, 255, 235, 180, 255);
                    SDL_RenderFillRect(ren, &cross_handle);

                    // Top/bottom corner fade handles
                    SDL_Rect h_tl = { ox, iy, 5, 5 };
                    SDL_Rect h_tr = { ox + ow - 5, iy, 5, 5 };
                    SDL_Rect h_bl = { ox, iy + ih - 5, 5, 5 };
                    SDL_Rect h_br = { ox + ow - 5, iy + ih - 5, 5, 5 };
                    SDL_SetRenderDrawColor(ren, 235, 175, 65, 255);
                    SDL_RenderFillRect(ren, &h_tl);
                    SDL_RenderFillRect(ren, &h_tr);
                    SDL_RenderFillRect(ren, &h_bl);
                    SDL_RenderFillRect(ren, &h_br);
                }
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        }

        if (marquee_selecting) {
            int mx1 = fmin(marquee_start_x, marquee_cur_x);
            int my1 = fmin(marquee_start_y, marquee_cur_y);
            int mw = abs(marquee_cur_x - marquee_start_x);
            int mh = abs(marquee_cur_y - marquee_start_y);
            SDL_Rect mr = { mx1, my1, mw, mh };
            SDL_SetRenderDrawColor(ren, 100, 100, 100, 255);
            SDL_RenderDrawRect(ren, &mr);
        }

        int cur_px = g_daw.tcp_width + (int)((p->playhead_seconds - p->scroll_x_sec) * p->pixels_per_second);
        if (cur_px >= g_daw.tcp_width && cur_px <= g_daw.tcp_width + view_w) {
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            SDL_RenderDrawLine(ren, cur_px, corner_y, cur_px, header_top_y + view_h);
        }
        SDL_RenderSetClipRect(ren, NULL);

        // 6. Track Header (TCP)
        SDL_Rect tcp_clip = { 0, header_top_y, g_daw.tcp_width, view_h };
        SDL_RenderSetClipRect(ren, &tcp_clip);
        for (int i = 0; i < p->num_tracks; i++) {
            int ty = get_track_y_pos(i);
            int th = p->track_heights[i];
            bool is_sel = p->track_selected[i];

            SDL_Rect tp = { 2, ty + 2, g_daw.tcp_width - 4, th - 4 };
            SDL_SetRenderDrawColor(ren, is_sel ? 32 : 26, is_sel ? 40 : 26, is_sel ? 32 : 26, 255);
            SDL_RenderFillRect(ren, &tp);

            if (is_sel) {
                SDL_SetRenderDrawColor(ren, 22, 101, 52, 255);
                SDL_RenderDrawRect(ren, &tp);
                SDL_Rect inner_green = { tp.x + 1, tp.y + 1, tp.w - 2, tp.h - 2 };
                SDL_RenderDrawRect(ren, &inner_green);
            }

            // Top Part: Track Name Box with clean border
            SDL_Rect name_box = { 8, ty + 6, g_daw.tcp_width - 16, 22 };
            bool is_editing = (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME && g_daw.editing_track_name_idx == i);

            SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
            SDL_RenderFillRect(ren, &name_box);
            SDL_SetRenderDrawColor(ren, is_editing ? 220 : 65, is_editing ? 220 : 65, is_editing ? 220 : 65, 255);
            SDL_RenderDrawRect(ren, &name_box);

            if (is_editing) {
                draw_text(ren, g_daw.editing_name_buf, 12, ty + 10, c_white);
                int text_w = (int)strlen(g_daw.editing_name_buf) * 8;
                if ((SDL_GetTicks() / 500) % 2 == 0) {
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_RenderDrawLine(ren, 12 + text_w, ty + 8, 12 + text_w, ty + 24);
                }
            } else {
                draw_text(ren, p->track_names[i], 12, ty + 10, c_white);
            }

            // Bottom Part: Controls
            int bot_y = ty + 34;

            // DB Controls
            SDL_Rect btn_minus = { 8, bot_y, 18, 20 };
            SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
            SDL_RenderFillRect(ren, &btn_minus);
            draw_text(ren, "-", 14, bot_y + 4, c_white);

            char db_txt[16];
            snprintf(db_txt, sizeof(db_txt), "%+.1f", p->track_db[i]);
            draw_text(ren, db_txt, 30, bot_y + 5, c_white);

            SDL_Rect btn_plus = { 76, bot_y, 18, 20 };
            SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
            SDL_RenderFillRect(ren, &btn_plus);
            draw_text(ren, "+", 81, bot_y + 4, c_white);

            // M & S Buttons
            SDL_Rect m_btn = { 110, bot_y, 22, 20 };
            SDL_SetRenderDrawColor(ren, p->tracks[i].mute ? 180 : 44, p->tracks[i].mute ? 40 : 44, 44, 255);
            SDL_RenderFillRect(ren, &m_btn);
            draw_text(ren, "M", 117, bot_y + 4, c_white);

            SDL_Rect s_btn = { 138, bot_y, 22, 20 };
            SDL_SetRenderDrawColor(ren, p->tracks[i].solo ? 180 : 44, p->tracks[i].solo ? 140 : 44, 44, 255);
            SDL_RenderFillRect(ren, &s_btn);
            draw_text(ren, "S", 145, bot_y + 4, c_white);

            SDL_SetRenderDrawColor(ren, 18, 18, 18, 255);
            SDL_RenderDrawLine(ren, 0, ty + th, g_daw.tcp_width, ty + th);
        }
        SDL_RenderSetClipRect(ren, NULL);

        SDL_SetRenderDrawColor(ren, 45, 45, 45, 255);
        SDL_RenderDrawLine(ren, g_daw.tcp_width - 1, TABBAR_HEIGHT + TOPBAR_HEIGHT, g_daw.tcp_width - 1, win_h);

        // Scrollbars
        SDL_Rect bar_h = { g_daw.tcp_width, win_h - SCROLLBAR_SIZE, view_w, SCROLLBAR_SIZE };
        SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
        SDL_RenderFillRect(ren, &bar_h);

        float thumb_w_sz = (total_w_timeline > 0) ? fmaxf(30.0f, (float)view_w * ((float)view_w / total_w_timeline)) : view_w;
        double max_scroll_sec = (total_w_timeline > view_w) ? (double)(total_w_timeline - view_w) / p->pixels_per_second : 0.0001;
        float thumb_x = g_daw.tcp_width + (max_scroll_sec > 0 ? (float)(p->scroll_x_sec / max_scroll_sec) * (view_w - thumb_w_sz) : 0);
        SDL_Rect thumb_h = { (int)thumb_x, win_h - SCROLLBAR_SIZE + 2, (int)thumb_w_sz, SCROLLBAR_SIZE - 4 };
        SDL_SetRenderDrawColor(ren, 65, 65, 65, 255);
        SDL_RenderFillRect(ren, &thumb_h);

        SDL_Rect bar_v = { win_w - SCROLLBAR_SIZE, header_top_y, SCROLLBAR_SIZE, view_h };
        SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
        SDL_RenderFillRect(ren, &bar_v);

        float thumb_h_sz = (max_h_tracks > 0) ? fmaxf(30.0f, (float)view_h * ((float)view_h / max_h_tracks)) : view_h;
        float thumb_y = header_top_y + (max_h_tracks > view_h ? ((float)p->scroll_y_px / (max_h_tracks - view_h)) * (view_h - thumb_h_sz) : 0);
        SDL_Rect thumb_v = { win_w - SCROLLBAR_SIZE + 2, (int)thumb_y, SCROLLBAR_SIZE - 4, (int)thumb_h_sz };
        SDL_SetRenderDrawColor(ren, 65, 65, 65, 255);
        SDL_RenderFillRect(ren, &thumb_v);

        // 7. Action Menu
        if (g_daw.menu_action_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            SDL_Rect a_box = { 56, m_start, 134, 56 };
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, &a_box);
            SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
            SDL_RenderDrawRect(ren, &a_box);
            draw_text(ren, "Lua Scripts...", 64, m_start + 8, c_white);
            draw_text(ren, "Python Scripts...", 64, m_start + 32, c_white);
        }

        // Script Manager
        if (g_daw.script_manager_open) {
            SDL_Rect *sr = &g_daw.script_win_rect;
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, sr);
            SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
            SDL_RenderDrawRect(ren, sr);

            SDL_Rect s_hdr = { sr->x, sr->y, sr->w, 26 };
            SDL_SetRenderDrawColor(ren, 36, 36, 36, 255);
            SDL_RenderFillRect(ren, &s_hdr);

            char win_title[64];
            snprintf(win_title, sizeof(win_title), "%s Script Manager", g_daw.script_manager_is_python ? "Python" : "Lua");
            draw_text(ren, win_title, sr->x + 8, sr->y + 8, c_white);
            draw_text(ren, "X", sr->x + sr->w - 18, sr->y + 8, c_white);

            SDL_Rect list_box = { sr->x + 12, sr->y + 34, sr->w - 24, 180 };
            SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
            SDL_RenderFillRect(ren, &list_box);

            for (int s = 0; s < g_daw.num_scripts; s++) {
                int sy = sr->y + 38 + (s * 22);
                if (s == g_daw.script_selected_idx) {
                    SDL_Rect s_row = { sr->x + 14, sy - 2, sr->w - 28, 20 };
                    SDL_SetRenderDrawColor(ren, 48, 48, 48, 255);
                    SDL_RenderFillRect(ren, &s_row);
                }
                draw_text(ren, g_daw.scripts[s].name, sr->x + 18, sy + 2, c_white);
                draw_text(ren, g_daw.scripts[s].shortcut, sr->x + sr->w - 70, sy + 2, c_white);
            }

            int by = sr->y + sr->h - 38;
            SDL_Rect btn_add_sc = { sr->x + 12, by, 92, 28 };
            SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
            SDL_RenderFillRect(ren, &btn_add_sc);
            draw_text(ren, "Add Script", sr->x + 18, by + 8, c_white);

            SDL_Rect btn_run_sc = { sr->x + 114, by, 92, 28 };
            SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
            SDL_RenderFillRect(ren, &btn_run_sc);
            draw_text(ren, "Run Script", sr->x + 120, by + 8, c_white);

            SDL_Rect btn_del_sc = { sr->x + 216, by, 92, 28 };
            SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
            SDL_RenderFillRect(ren, &btn_del_sc);
            draw_text(ren, "Delete", sr->x + 236, by + 8, c_white);
        }

        // File Menu
        if (g_daw.menu_file_open) {
            int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
            SDL_Rect m_box = { 8, m_start, 192, 146 };
            SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
            SDL_RenderFillRect(ren, &m_box);
            SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
            SDL_RenderDrawRect(ren, &m_box);

            draw_text(ren, "Import Project (.ehs)", 16, m_start + 6, c_white);
            draw_text(ren, "Export Project (.ehs)", 16, m_start + 30, c_white);
            draw_text(ren, "Import Media", 16, m_start + 54, c_white);
            draw_text(ren, "Export Media -> Audio >", 16, m_start + 78, c_white);
            draw_text(ren, "Export Media -> Video >", 16, m_start + 102, c_white);
            draw_text(ren, "Batch Export (Stems)", 16, m_start + 126, c_white);

            if (g_daw.submenu_export_audio_open) {
                SDL_Rect sub_a = { 200, m_start + 76, 110, 94 };
                SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
                SDL_RenderFillRect(ren, &sub_a);
                SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
                SDL_RenderDrawRect(ren, &sub_a);
                draw_text(ren, ".WAV Audio", 208, m_start + 82, c_white);
                draw_text(ren, ".MP3 Audio", 208, m_start + 104, c_white);
                draw_text(ren, ".FLAC Audio", 208, m_start + 126, c_white);
                draw_text(ren, ".OGG Audio", 208, m_start + 148, c_white);
            }

            if (g_daw.submenu_export_video_open) {
                SDL_Rect sub_v = { 200, m_start + 100, 110, 94 };
                SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
                SDL_RenderFillRect(ren, &sub_v);
                SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
                SDL_RenderDrawRect(ren, &sub_v);
                draw_text(ren, ".MP4 Video", 208, m_start + 106, c_white);
                draw_text(ren, ".MKV Video", 208, m_start + 128, c_white);
                draw_text(ren, ".MOV Video", 208, m_start + 150, c_white);
                draw_text(ren, ".AVI Video", 208, m_start + 172, c_white);
            }
        }

        // Floating Video Monitor (With Black Gap Handling)
        if (p->video.is_open) {
            SDL_Rect s_rect = { p->video.rect.x + 4, p->video.rect.y + 4, p->video.rect.w, p->video.rect.h };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 140);
            SDL_RenderFillRect(ren, &s_rect);

            SDL_Rect v_bar = { p->video.rect.x, p->video.rect.y, p->video.rect.w, 24 };
            SDL_SetRenderDrawColor(ren, 36, 36, 36, 255);
            SDL_RenderFillRect(ren, &v_bar);

            draw_text(ren, "Video Monitor", p->video.rect.x + 8, p->video.rect.y + 7, c_white);
            draw_text(ren, "X", p->video.rect.x + p->video.rect.w - 18, p->video.rect.y + 7, c_white);

            SDL_Rect v_canvas = { p->video.rect.x, p->video.rect.y + 24, p->video.rect.w, p->video.rect.h - 24 };
            
            // Background is always solid black
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderFillRect(ren, &v_canvas);

            if (p->video.has_active_frame && p->video.texture) {
                SDL_RenderCopy(ren, p->video.texture, NULL, &v_canvas);
            } else if (!p->video.texture) {
                draw_text(ren, "No Video Stream Loaded", p->video.rect.x + 120, p->video.rect.y + 140, c_muted);
            }

            SDL_Rect resize_handle = { p->video.rect.x + p->video.rect.w - 12, p->video.rect.y + p->video.rect.h - 12, 10, 10 };
            SDL_SetRenderDrawColor(ren, 100, 100, 100, 255);
            SDL_RenderFillRect(ren, &resize_handle);
        }

        // 8. Save Confirmation Dialog Modal
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
            SDL_SetRenderDrawColor(ren, 65, 65, 65, 255);
            SDL_RenderDrawRect(ren, &dia_rect);

            draw_text(ren, "Save changes before closing?", dia_x + 18, dia_y + 18, c_white);

            char prompt_sub[128];
            int t_idx = g_daw.save_prompt_tab_idx;
            const char *pname = (t_idx >= 0 && t_idx < g_daw.num_tabs) ? g_daw.tabs[t_idx].project_name : "Project";
            snprintf(prompt_sub, sizeof(prompt_sub), "Project \"%s\" has unsaved changes.", pname);
            draw_text(ren, prompt_sub, dia_x + 18, dia_y + 44, c_muted);

            int btn_y = dia_y + dia_h - 44;

            // [ Save ]
            SDL_Rect b_save = { dia_x + 18, btn_y, 100, 30 };
            SDL_SetRenderDrawColor(ren, 26, 86, 219, 255);
            SDL_RenderFillRect(ren, &b_save);
            draw_text(ren, "Save", dia_x + 52, btn_y + 8, c_white);

            // [ Don't Save ]
            SDL_Rect b_discard = { dia_x + 130, btn_y, 120, 30 };
            SDL_SetRenderDrawColor(ren, 55, 55, 55, 255);
            SDL_RenderFillRect(ren, &b_discard);
            draw_text(ren, "Don't Save", dia_x + 148, btn_y + 8, c_white);

            // [ Cancel ]
            SDL_Rect b_cancel = { dia_x + 262, btn_y, 100, 30 };
            SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
            SDL_RenderFillRect(ren, &b_cancel);
            draw_text(ren, "Cancel", dia_x + 292, btn_y + 8, c_white);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (g_daw.audio_device > 0) SDL_CloseAudioDevice(g_daw.audio_device);
    for (int t = 0; t < g_daw.num_tabs; t++) {
        close_video(&g_daw.tabs[t].video);
        for (int i = 0; i < g_daw.tabs[t].num_items; i++) {
            if (g_daw.tabs[t].items[i].raw_file_handle) fclose(g_daw.tabs[t].items[i].raw_file_handle);
            if (g_daw.tabs[t].items[i].peaks) free(g_daw.tabs[t].items[i].peaks);
        }
    }
    SDL_FreeCursor(g_cursor_arrow);
    SDL_FreeCursor(g_cursor_size_we);
    SDL_FreeCursor(g_cursor_size_ns);
    SDL_FreeCursor(g_cursor_ibeam);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
