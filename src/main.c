#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "font.h"
#include "video_player.h"
#include "audio_engine.h"
#include "daw_types.h"
#include "project_io.h"

DawEngine g_daw;

void audio_mixer_callback(void *userdata, Uint8 *stream, int len) {
    DawEngine *eng = (DawEngine *)userdata;
    float *out = (float *)stream;
    int frames_needed = len / (sizeof(float) * 2);
    memset(stream, 0, len);

    if (!eng->is_playing) return;

    bool any_solo = false;
    for (int t = 0; t < eng->num_tracks; t++) {
        if (eng->tracks[t].solo) { any_solo = true; break; }
    }

    double st = eng->playhead_seconds;
    double et = st + ((double)frames_needed / SAMPLE_RATE);

    for (int i = 0; i < eng->num_items; i++) {
        AudioItem *it = &eng->items[i];
        if (!it->raw_file_handle) continue;

        Track *tr = &eng->tracks[it->track_idx];
        if (tr->mute) continue;
        if (any_solo && !tr->solo) continue;

        float linear_gain = db_to_linear(eng->track_db[it->track_idx]);
        double it_s = it->start_time;
        double it_e = it->start_time + it->duration;

        if (it_e > st && it_s < et) {
            int64_t sample_offset = (int64_t)(((st - it_s) + it->offset_in_file) * SAMPLE_RATE);
            if (sample_offset >= 0) {
                fseek(it->raw_file_handle, sample_offset * sizeof(float) * 2, SEEK_SET);
                float chunk[1024 * 2];
                size_t read_frames = fread(chunk, sizeof(float) * 2, frames_needed, it->raw_file_handle);
                for (size_t f = 0; f < read_frames; f++) {
                    out[f * 2]     += chunk[f * 2] * linear_gain;
                    out[f * 2 + 1] += chunk[f * 2 + 1] * linear_gain;
                }
            }
        }
    }
    eng->playhead_seconds += (double)frames_needed / SAMPLE_RATE;
}

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return 1;

    SDL_Window *win = SDL_CreateWindow(
        "[unsaved project] - Professional Multitrack DAW",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    memset(&g_daw, 0, sizeof(DawEngine));
    g_daw.num_tracks = 4;
    g_daw.pixels_per_second = 80.0f;
    g_daw.track_height = 80;
    g_daw.project_duration_sec = 300.0;
    g_daw.video = (SimpleVideoPlayer){ .rect = { 650, 160, 480, 310 }, .is_open = false, .video_stream_idx = -1 };

    for (int i = 0; i < MAX_TRACKS; i++) {
        g_daw.tracks[i].id = i + 1;
        g_daw.tracks[i].volume = 1.0f;
        g_daw.track_db[i] = 0.0f;
    }

    push_undo_state();

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
    int dragging_item_idx = -1;
    bool dragging_trim_left = false, dragging_trim_right = false;
    double drag_start_mouse_time = 0.0, item_orig_start = 0.0, item_orig_dur = 0.0, item_orig_offset = 0.0;

    SDL_Event e;
    while (running) {
        int win_w, win_h;
        SDL_GetWindowSize(win, &win_w, &win_h);
        int view_w = win_w - TCP_WIDTH - SCROLLBAR_SIZE;
        int view_h = win_h - TOPBAR_HEIGHT - RULER_HEIGHT - SCROLLBAR_SIZE;

        for (int i = 0; i < g_daw.num_items; i++) {
            double end_t = g_daw.items[i].start_time + g_daw.items[i].duration + 60.0;
            if (end_t > g_daw.project_duration_sec) g_daw.project_duration_sec = end_t;
        }
        int total_w_timeline = (int)(g_daw.project_duration_sec * g_daw.pixels_per_second);
        int max_h_tracks = g_daw.num_tracks * g_daw.track_height;

        if (g_daw.is_playing) {
            double visible_end_sec = g_daw.scroll_x_sec + ((double)view_w / g_daw.pixels_per_second);
            if (g_daw.playhead_seconds >= visible_end_sec) {
                g_daw.scroll_x_sec = g_daw.playhead_seconds;
            }
        }

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_MOUSEWHEEL) {
                SDL_Keymod mod = SDL_GetModState();
                int mx, my; SDL_GetMouseState(&mx, &my);
                if (mod & KMOD_CTRL) {
                    if (mod & KMOD_SHIFT) {
                        g_daw.track_height += e.wheel.y * 8;
                        if (g_daw.track_height < 45) g_daw.track_height = 45;
                        if (g_daw.track_height > 180) g_daw.track_height = 180;
                    } else {
                        double mouse_time = g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second;
                        g_daw.pixels_per_second += e.wheel.y * 12.0f;
                        if (g_daw.pixels_per_second < 15.0f) g_daw.pixels_per_second = 15.0f;
                        if (g_daw.pixels_per_second > 400.0f) g_daw.pixels_per_second = 400.0f;
                        g_daw.scroll_x_sec = mouse_time - (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second;
                        if (g_daw.scroll_x_sec < 0) g_daw.scroll_x_sec = 0;
                    }
                } else if (mod & KMOD_SHIFT) {
                    g_daw.scroll_x_sec -= e.wheel.y * (30.0f / g_daw.pixels_per_second);
                    if (g_daw.scroll_x_sec < 0) g_daw.scroll_x_sec = 0;
                } else {
                    g_daw.scroll_y_px -= e.wheel.y * 36;
                    int max_y_scroll = max_h_tracks - view_h;
                    if (max_y_scroll < 0) max_y_scroll = 0;
                    if (g_daw.scroll_y_px < 0) g_daw.scroll_y_px = 0;
                    if (g_daw.scroll_y_px > max_y_scroll) g_daw.scroll_y_px = max_y_scroll;
                }
            } else if (e.type == SDL_DROPFILE) {
                char *file = e.drop.file;
                int my = 0; SDL_GetMouseState(NULL, &my);
                int trk = (my - TOPBAR_HEIGHT - RULER_HEIGHT + g_daw.scroll_y_px) / g_daw.track_height;
                if (trk < 0) trk = 0;
                if (trk >= g_daw.num_tracks) trk = g_daw.num_tracks - 1;

                if (g_daw.num_items < MAX_ITEMS) {
                    push_undo_state();
                    AudioItem *it = &g_daw.items[g_daw.num_items];
                    it->track_idx = trk;
                    it->start_time = g_daw.playhead_seconds;
                    if (process_media_cache(file, it) == 0) g_daw.num_items++;
                }
                open_video_by_path(&g_daw.video, file, ren);
                SDL_free(file);
            } else if (e.type == SDL_KEYDOWN) {
                SDL_Keymod mod = SDL_GetModState();
                if ((mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_z) perform_undo();
                else if ((mod & KMOD_CTRL) && (e.key.keysym.sym == SDLK_y || ((mod & KMOD_SHIFT) && e.key.keysym.sym == SDLK_z))) perform_redo();
                else if (e.key.keysym.sym == SDLK_EQUALS || e.key.keysym.sym == SDLK_PLUS || e.key.keysym.sym == SDLK_KP_PLUS)
                    g_daw.pixels_per_second = fminf(400.0f, g_daw.pixels_per_second * 1.25f);
                else if (e.key.keysym.sym == SDLK_MINUS || e.key.keysym.sym == SDLK_KP_MINUS)
                    g_daw.pixels_per_second = fmaxf(15.0f, g_daw.pixels_per_second * 0.8f);
                else if (e.key.keysym.sym == SDLK_SPACE) g_daw.is_playing = !g_daw.is_playing;
                else if (e.key.keysym.sym == SDLK_v) g_daw.video.is_open = !g_daw.video.is_open;
                else if (e.key.keysym.sym == SDLK_HOME) { g_daw.playhead_seconds = 0.0; seek_video(&g_daw.video, 0.0); }
                else if (e.key.keysym.sym == SDLK_s) {
                    double cur_t = g_daw.playhead_seconds;
                    for (int i = 0; i < g_daw.num_items; i++) {
                        AudioItem *it = &g_daw.items[i];
                        if (it->is_selected) {
                            double it_s = it->start_time, it_e = it->start_time + it->duration;
                            if (cur_t > it_s + 0.05 && cur_t < it_e - 0.05 && g_daw.num_items < MAX_ITEMS) {
                                push_undo_state();
                                AudioItem *r_item = &g_daw.items[g_daw.num_items];
                                memcpy(r_item, it, sizeof(AudioItem));
                                double delta = cur_t - it_s;
                                it->duration = delta;
                                r_item->start_time = cur_t;
                                r_item->offset_in_file = it->offset_in_file + delta;
                                r_item->duration = it_e - cur_t;
                                r_item->is_selected = true;
                                it->is_selected = false;
                                r_item->raw_file_handle = fopen(r_item->raw_audio_path, "rb");
                                g_daw.num_items++;
                                break;
                            }
                        }
                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x, my = e.button.y;
                SDL_Keymod mod = SDL_GetModState();
                bool shift_pressed = (mod & KMOD_SHIFT);

                if (g_daw.menu_file_open && (mx > 260 || my > 260)) {
                    g_daw.menu_file_open = false;
                    g_daw.submenu_export_audio_open = false;
                    g_daw.submenu_export_video_open = false;
                    continue;
                }

                if (g_daw.menu_file_open) {
                    if (mx >= 8 && mx <= 200) {
                        if (my >= 38 && my < 62) { save_project_file(); g_daw.menu_file_open = false; continue; }
                        else if (my >= 62 && my < 86) { load_project_file(); g_daw.menu_file_open = false; continue; }
                        else if (my >= 86 && my < 110) {
                            char p[MAX_PATH];
                            if (open_generic_dialog(p, sizeof(p), "Media Files\0*.wav;*.mp3;*.flac;*.mp4;*.mkv;*.mov\0", false)) {
                                push_undo_state();
                                AudioItem *it = &g_daw.items[g_daw.num_items];
                                it->track_idx = 0; it->start_time = g_daw.playhead_seconds;
                                if (process_media_cache(p, it) == 0) g_daw.num_items++;
                                open_video_by_path(&g_daw.video, p, ren);
                            }
                            g_daw.menu_file_open = false;
                            continue;
                        }
                        else if (my >= 110 && my < 134) { g_daw.submenu_export_audio_open = !g_daw.submenu_export_audio_open; continue; }
                        else if (my >= 134 && my < 158) { g_daw.submenu_export_video_open = !g_daw.submenu_export_video_open; continue; }
                        else if (my >= 158 && my < 182) { export_batch_tracks(); g_daw.menu_file_open = false; continue; }
                    }
                    if (g_daw.submenu_export_audio_open && mx >= 200 && mx <= 310) {
                        if (my >= 110 && my < 132) { export_rendered_audio("wav"); g_daw.menu_file_open = false; continue; }
                        if (my >= 132 && my < 154) { export_rendered_audio("mp3"); g_daw.menu_file_open = false; continue; }
                        if (my >= 154 && my < 176) { export_rendered_audio("flac"); g_daw.menu_file_open = false; continue; }
                        if (my >= 176 && my < 198) { export_rendered_audio("ogg"); g_daw.menu_file_open = false; continue; }
                    }
                }

                if (my <= TOPBAR_HEIGHT) {
                    if (mx >= 8 && mx <= 50) g_daw.menu_file_open = !g_daw.menu_file_open;
                    else if (mx >= 56 && mx <= 146) g_daw.video.is_open = !g_daw.video.is_open;
                    else if (mx >= win_w - 228 && mx < win_w - 176) perform_undo();
                    else if (mx >= win_w - 172 && mx < win_w - 120) perform_redo();
                    else if (mx >= win_w - 116 && mx < win_w - 60) g_daw.pixels_per_second = fmaxf(15.0f, g_daw.pixels_per_second * 0.8f);
                    else if (mx >= win_w - 56 && mx < win_w - 6) g_daw.pixels_per_second = fminf(400.0f, g_daw.pixels_per_second * 1.25f);
                    continue;
                }

                if (mx < TCP_WIDTH && my > TOPBAR_HEIGHT && my <= TOPBAR_HEIGHT + RULER_HEIGHT) {
                    if (mx >= 8 && mx <= 88 && g_daw.num_tracks < MAX_TRACKS) {
                        push_undo_state();
                        g_daw.num_tracks++;
                    } else if (mx >= 94 && mx <= 180) g_daw.is_playing = !g_daw.is_playing;
                    continue;
                }

                if (g_daw.video.is_open && mx >= g_daw.video.rect.x && mx <= g_daw.video.rect.x + g_daw.video.rect.w &&
                    my >= g_daw.video.rect.y && my <= g_daw.video.rect.y + g_daw.video.rect.h) {
                    if (mx >= g_daw.video.rect.x + g_daw.video.rect.w - 18 && my >= g_daw.video.rect.y + g_daw.video.rect.h - 18) {
                        g_daw.video.is_resizing = true;
                    } else if (mx >= g_daw.video.rect.x + g_daw.video.rect.w - 24 && my <= g_daw.video.rect.y + 24) {
                        g_daw.video.is_open = false;
                    } else if (my <= g_daw.video.rect.y + 24) {
                        g_daw.video.is_dragging = true;
                        g_daw.video.drag_x = mx - g_daw.video.rect.x;
                        g_daw.video.drag_y = my - g_daw.video.rect.y;
                    }
                    continue;
                }

                if (my >= win_h - SCROLLBAR_SIZE && mx >= TCP_WIDTH && mx < win_w - SCROLLBAR_SIZE) {
                    dragging_scrollbar_h = true; scrollbar_h_start_mx = mx; orig_scroll_x = g_daw.scroll_x_sec;
                    continue;
                }
                if (mx >= win_w - SCROLLBAR_SIZE && my >= TOPBAR_HEIGHT + RULER_HEIGHT && my < win_h - SCROLLBAR_SIZE) {
                    dragging_scrollbar_v = true; scrollbar_v_start_my = my; orig_scroll_y = g_daw.scroll_y_px;
                    continue;
                }

                if (mx < TCP_WIDTH && my > TOPBAR_HEIGHT + RULER_HEIGHT) {
                    int trk = (my - TOPBAR_HEIGHT - RULER_HEIGHT + g_daw.scroll_y_px) / g_daw.track_height;
                    if (trk >= 0 && trk < g_daw.num_tracks) {
                        int rel_y = (my - TOPBAR_HEIGHT - RULER_HEIGHT + g_daw.scroll_y_px) % g_daw.track_height;
                        if (rel_y >= 8 && rel_y <= 28) {
                            if (mx >= 38 && mx <= 54) g_daw.track_db[trk] = fmaxf(-60.0f, g_daw.track_db[trk] - 1.0f);
                            else if (mx >= 118 && mx <= 134) g_daw.track_db[trk] = fminf(12.0f, g_daw.track_db[trk] + 1.0f);
                            else if (mx >= 142 && mx <= 162) g_daw.tracks[trk].mute = !g_daw.tracks[trk].mute;
                            else if (mx >= 166 && mx <= 186) g_daw.tracks[trk].solo = !g_daw.tracks[trk].solo;
                        } else {
                            if (!shift_pressed) {
                                for (int t = 0; t < g_daw.num_tracks; t++) g_daw.track_selected[t] = false;
                            }
                            g_daw.track_selected[trk] = !g_daw.track_selected[trk];
                        }
                    }
                    continue;
                }

                if (mx >= TCP_WIDTH && my > TOPBAR_HEIGHT && my <= TOPBAR_HEIGHT + RULER_HEIGHT) {
                    scrubbing_ruler = true;
                    g_daw.playhead_seconds = fmax(0.0, g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second);
                    seek_video(&g_daw.video, g_daw.playhead_seconds);
                    continue;
                }

                if (mx >= TCP_WIDTH && my > TOPBAR_HEIGHT + RULER_HEIGHT && my < win_h - SCROLLBAR_SIZE) {
                    double click_time = g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second;
                    int click_track = (my - TOPBAR_HEIGHT - RULER_HEIGHT + g_daw.scroll_y_px) / g_daw.track_height;

                    if (!shift_pressed) {
                        for (int i = 0; i < g_daw.num_items; i++) g_daw.items[i].is_selected = false;
                    }

                    for (int i = g_daw.num_items - 1; i >= 0; i--) {
                        AudioItem *it = &g_daw.items[i];
                        if (it->track_idx == click_track && click_time >= it->start_time && click_time <= it->start_time + it->duration) {
                            it->is_selected = true;
                            dragging_item_idx = i;
                            drag_start_mouse_time = click_time;
                            item_orig_start = it->start_time;
                            item_orig_dur = it->duration;
                            item_orig_offset = it->offset_in_file;

                            int ix = TCP_WIDTH + (int)((it->start_time - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
                            int iw = (int)(it->duration * g_daw.pixels_per_second);
                            if (mx <= ix + 8) dragging_trim_left = true;
                            else if (mx >= ix + iw - 8) dragging_trim_right = true;
                            break;
                        }
                    }
                    continue;
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                g_daw.video.is_dragging = false; g_daw.video.is_resizing = false;
                dragging_scrollbar_h = false; dragging_scrollbar_v = false;
                scrubbing_ruler = false;
                dragging_item_idx = -1; dragging_trim_left = false; dragging_trim_right = false;
            } else if (e.type == SDL_MOUSEMOTION) {
                int mx = e.motion.x, my = e.motion.y;
                if (g_daw.video.is_dragging) {
                    g_daw.video.rect.x = mx - g_daw.video.drag_x;
                    g_daw.video.rect.y = my - g_daw.video.drag_y;
                }
                if (g_daw.video.is_resizing) {
                    int new_w = mx - g_daw.video.rect.x, new_h = my - g_daw.video.rect.y;
                    if (new_w >= 200) g_daw.video.rect.w = new_w;
                    if (new_h >= 140) g_daw.video.rect.h = new_h;
                }
                if (dragging_scrollbar_h) {
                    float delta = (float)(mx - scrollbar_h_start_mx);
                    double max_x_scroll = (total_w_timeline > view_w) ? (double)(total_w_timeline - view_w) / g_daw.pixels_per_second : 0.0;
                    g_daw.scroll_x_sec = orig_scroll_x + (delta / (float)view_w) * (g_daw.project_duration_sec);
                    if (g_daw.scroll_x_sec < 0) g_daw.scroll_x_sec = 0;
                    if (g_daw.scroll_x_sec > max_x_scroll) g_daw.scroll_x_sec = max_x_scroll;
                }
                if (dragging_scrollbar_v) {
                    float delta = (float)(my - scrollbar_v_start_my);
                    int max_y = max_h_tracks - view_h; if (max_y < 0) max_y = 0;
                    g_daw.scroll_y_px = (int)(orig_scroll_y + ((float)(my - scrollbar_v_start_my) / view_h) * max_h_tracks);
                    if (g_daw.scroll_y_px < 0) g_daw.scroll_y_px = 0;
                    if (g_daw.scroll_y_px > max_y) g_daw.scroll_y_px = max_y;
                }
                if (scrubbing_ruler && mx >= TCP_WIDTH && mx < win_w - SCROLLBAR_SIZE) {
                    g_daw.playhead_seconds = fmax(0.0, g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second);
                    seek_video(&g_daw.video, g_daw.playhead_seconds);
                }
                if (dragging_item_idx != -1) {
                    AudioItem *it = &g_daw.items[dragging_item_idx];
                    double cur_mouse_time = g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second;
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
                        int target_t = (my - TOPBAR_HEIGHT - RULER_HEIGHT + g_daw.scroll_y_px) / g_daw.track_height;
                        if (target_t >= 0 && target_t < g_daw.num_tracks) it->track_idx = target_t;
                    }
                }
            }
        }

        if (g_daw.video.is_open && g_daw.video.video_stream_idx != -1) {
            sync_video_to_audio_clock(&g_daw.video, g_daw.playhead_seconds, scrubbing_ruler || !g_daw.is_playing);
        }

        // --- GUI RENDER ---
        SDL_SetRenderDrawColor(ren, 38, 38, 38, 255);
        SDL_RenderClear(ren);

        SDL_Color c_white = { 220, 220, 220, 255 }, c_muted = { 130, 130, 130, 255 }, c_teal = { 0, 210, 170, 255 }, c_dark = { 40, 40, 40, 255 };

        // 1. Top Bar
        SDL_Rect bar = { 0, 0, win_w, TOPBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
        SDL_RenderFillRect(ren, &bar);

        SDL_Rect btn_file = { 8, 6, 46, 26 };
        SDL_SetRenderDrawColor(ren, g_daw.menu_file_open ? 55 : 40, g_daw.menu_file_open ? 55 : 40, g_daw.menu_file_open ? 55 : 40, 255);
        SDL_RenderFillRect(ren, &btn_file);
        draw_text(ren, "File", 18, 14, c_white);

        SDL_Rect v_menu = { 60, 6, 90, 26 };
        SDL_SetRenderDrawColor(ren, g_daw.video.is_open ? 0 : 40, g_daw.video.is_open ? 140 : 40, g_daw.video.is_open ? 120 : 40, 255);
        SDL_RenderFillRect(ren, &v_menu);
        draw_text(ren, "View:Video", 66, 14, c_white);

        char clock_buf[32];
        format_timecode(g_daw.playhead_seconds, clock_buf, sizeof(clock_buf));
        int clock_x = (win_w / 2) - 52;
        SDL_Rect clock_bg = { clock_x - 10, 5, 128, 28 };
        SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
        SDL_RenderFillRect(ren, &clock_bg);
        SDL_SetRenderDrawColor(ren, 50, 50, 50, 255);
        SDL_RenderDrawRect(ren, &clock_bg);
        draw_text(ren, clock_buf, clock_x - 4, 14, c_teal);

        SDL_Rect btn_undo = { win_w - 228, 6, 52, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_undo);
        draw_text(ren, "Undo", win_w - 218, 14, c_white);

        SDL_Rect btn_redo = { win_w - 172, 6, 52, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_redo);
        draw_text(ren, "Redo", win_w - 162, 14, c_white);

        SDL_Rect btn_zm_out = { win_w - 116, 6, 56, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_zm_out);
        draw_text(ren, "Zoom-", win_w - 110, 14, c_white);

        SDL_Rect btn_zm_in = { win_w - 56, 6, 50, 26 };
        SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
        SDL_RenderFillRect(ren, &btn_zm_in);
        draw_text(ren, "Zoom+", win_w - 50, 14, c_teal);

        // 2. Corner Area Above Track Header
        SDL_Rect corner_area = { 0, TOPBAR_HEIGHT, TCP_WIDTH, RULER_HEIGHT };
        SDL_SetRenderDrawColor(ren, 32, 32, 32, 255);
        SDL_RenderFillRect(ren, &corner_area);

        SDL_Rect btn_corner_add = { 8, TOPBAR_HEIGHT + 4, 80, 26 };
        SDL_SetRenderDrawColor(ren, 48, 48, 48, 255);
        SDL_RenderFillRect(ren, &btn_corner_add);
        draw_text(ren, "+ Track", 18, TOPBAR_HEIGHT + 12, c_teal);

        SDL_Rect btn_corner_play = { 94, TOPBAR_HEIGHT + 4, 86, 26 };
        SDL_SetRenderDrawColor(ren, g_daw.is_playing ? 160 : 48, g_daw.is_playing ? 60 : 48, 48, 255);
        SDL_RenderFillRect(ren, &btn_corner_play);
        draw_text(ren, g_daw.is_playing ? "Pause" : "Play", 118, TOPBAR_HEIGHT + 12, c_white);

        // 3. Dynamic Adaptive Time Ruler
        SDL_Rect ruler = { TCP_WIDTH, TOPBAR_HEIGHT, view_w, RULER_HEIGHT };
        SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
        SDL_RenderFillRect(ren, &ruler);

        double sec_per_tick = 1.0;
        if (g_daw.pixels_per_second >= 200.0f) sec_per_tick = 0.1;
        else if (g_daw.pixels_per_second >= 100.0f) sec_per_tick = 0.25;
        else if (g_daw.pixels_per_second >= 50.0f) sec_per_tick = 0.5;
        else if (g_daw.pixels_per_second >= 25.0f) sec_per_tick = 1.0;
        else sec_per_tick = 5.0;

        double start_tick = floor(g_daw.scroll_x_sec / sec_per_tick) * sec_per_tick;
        double end_tick = g_daw.scroll_x_sec + (double)view_w / g_daw.pixels_per_second + sec_per_tick;

        for (double t = start_tick; t <= end_tick; t += sec_per_tick) {
            int rx = TCP_WIDTH + (int)((t - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
            if (rx < TCP_WIDTH || rx > TCP_WIDTH + view_w) continue;

            char t_str[32];
            if (sec_per_tick < 1.0) snprintf(t_str, sizeof(t_str), "%.2fs", t);
            else if (t < 60.0) snprintf(t_str, sizeof(t_str), "%02ds", (int)t);
            else if (t < 3600.0) {
                int m = (int)(t / 60); int s = (int)(t - m * 60);
                snprintf(t_str, sizeof(t_str), "%02d:%02d", m, s);
            } else {
                int h = (int)(t / 3600); int m = (int)((t - h * 3600) / 60);
                snprintf(t_str, sizeof(t_str), "%02d:%02d:00", h, m);
            }

            draw_text(ren, t_str, rx + 4, TOPBAR_HEIGHT + 12, c_muted);
            SDL_SetRenderDrawColor(ren, 55, 55, 55, 255);
            SDL_RenderDrawLine(ren, rx, TOPBAR_HEIGHT + 20, rx, TOPBAR_HEIGHT + RULER_HEIGHT);
        }

        SDL_Rect timeline_clip = { TCP_WIDTH, TOPBAR_HEIGHT + RULER_HEIGHT, view_w, view_h };
        SDL_RenderSetClipRect(ren, &timeline_clip);

        for (double t = start_tick; t <= end_tick; t += sec_per_tick) {
            int gx = TCP_WIDTH + (int)((t - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
            if (gx < TCP_WIDTH || gx > TCP_WIDTH + view_w) continue;
            SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
            SDL_RenderDrawLine(ren, gx, TOPBAR_HEIGHT + RULER_HEIGHT, gx, TOPBAR_HEIGHT + RULER_HEIGHT + view_h);
        }

        for (int i = 0; i <= g_daw.num_tracks; i++) {
            int ty = TOPBAR_HEIGHT + RULER_HEIGHT + (i * g_daw.track_height) - g_daw.scroll_y_px;
            SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
            SDL_RenderDrawLine(ren, TCP_WIDTH, ty, TCP_WIDTH + view_w, ty);
        }

        // 4. Render Items
        for (int i = 0; i < g_daw.num_items; i++) {
            AudioItem *it = &g_daw.items[i];
            int ix = TCP_WIDTH + (int)((it->start_time - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
            int iy = TOPBAR_HEIGHT + RULER_HEIGHT + (it->track_idx * g_daw.track_height) - g_daw.scroll_y_px + 2;
            int iw = (int)(it->duration * g_daw.pixels_per_second);
            int ih = g_daw.track_height - 4;

            if (ix + iw < TCP_WIDTH || ix > TCP_WIDTH + view_w) continue;

            float gain_multiplier = db_to_linear(g_daw.track_db[it->track_idx]);

            SDL_Rect clip_r = { ix, iy, iw, ih };
            SDL_SetRenderDrawColor(ren, it->is_selected ? 44 : 32, it->is_selected ? 70 : 50, it->is_selected ? 94 : 68, 255);
            SDL_RenderFillRect(ren, &clip_r);
            SDL_SetRenderDrawColor(ren, it->is_selected ? 0 : 75, it->is_selected ? 220 : 140, it->is_selected ? 190 : 150, 255);
            SDL_RenderDrawRect(ren, &clip_r);

            for (int o = 0; o < g_daw.num_items; o++) {
                if (o == i) continue;
                AudioItem *ot = &g_daw.items[o];
                if (ot->track_idx == it->track_idx) {
                    double overlap_s = fmax(it->start_time, ot->start_time);
                    double overlap_e = fmin(it->start_time + it->duration, ot->start_time + ot->duration);
                    if (overlap_e > overlap_s) {
                        int ox = TCP_WIDTH + (int)((overlap_s - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
                        int ow = (int)((overlap_e - overlap_s) * g_daw.pixels_per_second);
                        SDL_Rect xfade_r = { ox, iy, ow, ih };
                        SDL_SetRenderDrawColor(ren, 210, 140, 0, 70);
                        SDL_RenderFillRect(ren, &xfade_r);
                    }
                }
            }

            int mid_y = iy + ih / 2;
            SDL_SetRenderDrawColor(ren, 100, 220, 175, 255);

            for (int col = 0; col < iw; col++) {
                int px = ix + col;
                if (px < TCP_WIDTH || px > TCP_WIDTH + view_w) continue;

                double cur_item_sec = it->offset_in_file + ((double)col / g_daw.pixels_per_second);
                int bin_idx = (int)((cur_item_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN);

                if (bin_idx >= 0 && bin_idx < it->peak_count) {
                    float mn = it->peaks[bin_idx].min_val * gain_multiplier;
                    float mx = it->peaks[bin_idx].max_val * gain_multiplier;
                    int y1 = mid_y - (int)(mx * (ih / 2.0f - 2));
                    int y2 = mid_y - (int)(mn * (ih / 2.0f - 2));
                    if (y1 == y2) y2 += 1;
                    SDL_RenderDrawLine(ren, px, y1, px, y2);
                }
            }
            draw_text(ren, it->label, ix + 6, iy + 6, c_white);
        }

        int cur_px = TCP_WIDTH + (int)((g_daw.playhead_seconds - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
        if (cur_px >= TCP_WIDTH && cur_px <= TCP_WIDTH + view_w) {
            SDL_SetRenderDrawColor(ren, 0, 220, 170, 255);
            SDL_RenderDrawLine(ren, cur_px, TOPBAR_HEIGHT, cur_px, TOPBAR_HEIGHT + RULER_HEIGHT + view_h);
        }
        SDL_RenderSetClipRect(ren, NULL);

        // 5. TCP Sidebar
        SDL_Rect tcp_clip = { 0, TOPBAR_HEIGHT + RULER_HEIGHT, TCP_WIDTH, view_h };
        SDL_RenderSetClipRect(ren, &tcp_clip);
        for (int i = 0; i < g_daw.num_tracks; i++) {
            int ty = TOPBAR_HEIGHT + RULER_HEIGHT + (i * g_daw.track_height) - g_daw.scroll_y_px;
            SDL_Rect tp = { 2, ty + 2, TCP_WIDTH - 4, g_daw.track_height - 4 };
            SDL_SetRenderDrawColor(ren, g_daw.track_selected[i] ? 64 : 48, g_daw.track_selected[i] ? 64 : 48, g_daw.track_selected[i] ? 64 : 48, 255);
            SDL_RenderFillRect(ren, &tp);

            char n_str[8]; snprintf(n_str, sizeof(n_str), "%d", g_daw.tracks[i].id);
            draw_text(ren, n_str, 8, ty + 12, c_white);

            SDL_Rect btn_minus = { 38, ty + 10, 16, 18 };
            SDL_SetRenderDrawColor(ren, 36, 36, 36, 255);
            SDL_RenderFillRect(ren, &btn_minus);
            draw_text(ren, "-", 43, ty + 13, c_white);

            char db_txt[16];
            if (g_daw.track_db[i] <= -60.0f) snprintf(db_txt, sizeof(db_txt), "-inf dB");
            else snprintf(db_txt, sizeof(db_txt), "%+.1f dB", g_daw.track_db[i]);
            draw_text(ren, db_txt, 58, ty + 14, c_teal);

            SDL_Rect btn_plus = { 118, ty + 10, 16, 18 };
            SDL_SetRenderDrawColor(ren, 36, 36, 36, 255);
            SDL_RenderFillRect(ren, &btn_plus);
            draw_text(ren, "+", 123, ty + 13, c_white);

            SDL_Rect m_btn = { 142, ty + 10, 20, 20 };
            SDL_SetRenderDrawColor(ren, g_daw.tracks[i].mute ? 200 : 65, g_daw.tracks[i].mute ? 40 : 65, 40, 255);
            SDL_RenderFillRect(ren, &m_btn);
            draw_text(ren, "M", 148, ty + 14, c_white);

            SDL_Rect s_btn = { 166, ty + 10, 20, 20 };
            SDL_SetRenderDrawColor(ren, g_daw.tracks[i].solo ? 210 : 65, g_daw.tracks[i].solo ? 180 : 65, 40, 255);
            SDL_RenderFillRect(ren, &s_btn);
            draw_text(ren, "S", 172, ty + 14, c_white);

            SDL_SetRenderDrawColor(ren, 25, 25, 25, 255);
            SDL_RenderDrawLine(ren, 0, ty + g_daw.track_height, TCP_WIDTH, ty + g_daw.track_height);
        }
        SDL_RenderSetClipRect(ren, NULL);

        // 6. Scrollbars
        SDL_Rect bar_h = { TCP_WIDTH, win_h - SCROLLBAR_SIZE, view_w, SCROLLBAR_SIZE };
        SDL_SetRenderDrawColor(ren, 28, 28, 28, 255);
        SDL_RenderFillRect(ren, &bar_h);

        float thumb_w_sz = (total_w_timeline > 0) ? fmaxf(30.0f, (float)view_w * ((float)view_w / total_w_timeline)) : view_w;
        double max_scroll_sec = (total_w_timeline > view_w) ? (double)(total_w_timeline - view_w) / g_daw.pixels_per_second : 0.0001;
        float thumb_x = TCP_WIDTH + (max_scroll_sec > 0 ? (float)(g_daw.scroll_x_sec / max_scroll_sec) * (view_w - thumb_w_sz) : 0);
        SDL_Rect thumb_h = { (int)thumb_x, win_h - SCROLLBAR_SIZE + 2, (int)thumb_w_sz, SCROLLBAR_SIZE - 4 };
        SDL_SetRenderDrawColor(ren, 75, 75, 75, 255);
        SDL_RenderFillRect(ren, &thumb_h);

        SDL_Rect bar_v = { win_w - SCROLLBAR_SIZE, TOPBAR_HEIGHT + RULER_HEIGHT, SCROLLBAR_SIZE, view_h };
        SDL_SetRenderDrawColor(ren, 28, 28, 28, 255);
        SDL_RenderFillRect(ren, &bar_v);

        float thumb_h_sz = (max_h_tracks > 0) ? fmaxf(30.0f, (float)view_h * ((float)view_h / max_h_tracks)) : view_h;
        float thumb_y = TOPBAR_HEIGHT + RULER_HEIGHT + (max_h_tracks > view_h ? ((float)g_daw.scroll_y_px / (max_h_tracks - view_h)) * (view_h - thumb_h_sz) : 0);
        SDL_Rect thumb_v = { win_w - SCROLLBAR_SIZE + 2, (int)thumb_y, SCROLLBAR_SIZE - 4, (int)thumb_h_sz };
        SDL_SetRenderDrawColor(ren, 75, 75, 75, 255);
        SDL_RenderFillRect(ren, &thumb_v);

        // 7. Dropdown Menu
        if (g_daw.menu_file_open) {
            SDL_Rect m_box = { 8, TOPBAR_HEIGHT, 192, 146 };
            SDL_SetRenderDrawColor(ren, 28, 28, 28, 255);
            SDL_RenderFillRect(ren, &m_box);
            SDL_SetRenderDrawColor(ren, 65, 65, 65, 255);
            SDL_RenderDrawRect(ren, &m_box);

            draw_text(ren, "Import Project File", 16, TOPBAR_HEIGHT + 6, c_white);
            draw_text(ren, "Export Project File", 16, TOPBAR_HEIGHT + 30, c_white);
            draw_text(ren, "Import Media", 16, TOPBAR_HEIGHT + 54, c_white);
            draw_text(ren, "Export Media -> Audio >", 16, TOPBAR_HEIGHT + 78, c_teal);
            draw_text(ren, "Export Media -> Video >", 16, TOPBAR_HEIGHT + 102, c_teal);
            draw_text(ren, "Batch Export (Stems)", 16, TOPBAR_HEIGHT + 126, c_white);

            if (g_daw.submenu_export_audio_open) {
                SDL_Rect sub_a = { 200, TOPBAR_HEIGHT + 76, 110, 94 };
                SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
                SDL_RenderFillRect(ren, &sub_a);
                SDL_SetRenderDrawColor(ren, 65, 65, 65, 255);
                SDL_RenderDrawRect(ren, &sub_a);
                draw_text(ren, ".WAV Audio", 208, TOPBAR_HEIGHT + 82, c_white);
                draw_text(ren, ".MP3 Audio", 208, TOPBAR_HEIGHT + 104, c_white);
                draw_text(ren, ".FLAC Audio", 208, TOPBAR_HEIGHT + 126, c_white);
                draw_text(ren, ".OGG Audio", 208, TOPBAR_HEIGHT + 148, c_white);
            }

            if (g_daw.submenu_export_video_open) {
                SDL_Rect sub_v = { 200, TOPBAR_HEIGHT + 100, 110, 94 };
                SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
                SDL_RenderFillRect(ren, &sub_v);
                SDL_SetRenderDrawColor(ren, 65, 65, 65, 255);
                SDL_RenderDrawRect(ren, &sub_v);
                draw_text(ren, ".MP4 Video", 208, TOPBAR_HEIGHT + 106, c_white);
                draw_text(ren, ".MKV Video", 208, TOPBAR_HEIGHT + 128, c_white);
                draw_text(ren, ".MOV Video", 208, TOPBAR_HEIGHT + 150, c_white);
                draw_text(ren, ".AVI Video", 208, TOPBAR_HEIGHT + 172, c_white);
            }
        }

        // 8. Video Window
        if (g_daw.video.is_open) {
            SDL_Rect s_rect = { g_daw.video.rect.x + 4, g_daw.video.rect.y + 4, g_daw.video.rect.w, g_daw.video.rect.h };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 140);
            SDL_RenderFillRect(ren, &s_rect);

            SDL_Rect v_bar = { g_daw.video.rect.x, g_daw.video.rect.y, g_daw.video.rect.w, 24 };
            SDL_SetRenderDrawColor(ren, 240, 240, 240, 255);
            SDL_RenderFillRect(ren, &v_bar);

            draw_text(ren, "Video Monitor", g_daw.video.rect.x + 8, g_daw.video.rect.y + 7, c_dark);
            draw_text(ren, "X", g_daw.video.rect.x + g_daw.video.rect.w - 18, g_daw.video.rect.y + 7, c_dark);

            SDL_Rect v_canvas = { g_daw.video.rect.x, g_daw.video.rect.y + 24, g_daw.video.rect.w, g_daw.video.rect.h - 24 };
            if (g_daw.video.texture) {
                SDL_RenderCopy(ren, g_daw.video.texture, NULL, &v_canvas);
            } else {
                SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
                SDL_RenderFillRect(ren, &v_canvas);
                draw_text(ren, "No Video Stream Loaded", g_daw.video.rect.x + 120, g_daw.video.rect.y + 140, c_muted);
            }

            SDL_Rect resize_handle = { g_daw.video.rect.x + g_daw.video.rect.w - 12, g_daw.video.rect.y + g_daw.video.rect.h - 12, 10, 10 };
            SDL_SetRenderDrawColor(ren, 0, 210, 170, 255);
            SDL_RenderFillRect(ren, &resize_handle);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (g_daw.audio_device > 0) SDL_CloseAudioDevice(g_daw.audio_device);
    close_video(&g_daw.video);
    for (int i = 0; i < g_daw.num_items; i++) {
        if (g_daw.items[i].raw_file_handle) fclose(g_daw.items[i].raw_file_handle);
        if (g_daw.items[i].peaks) free(g_daw.items[i].peaks);
    }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
