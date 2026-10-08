#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <windows.h>
#include <commdlg.h>

#include "font.h"
#include "video_player.h"
#include "audio_engine.h"

#define RULER_HEIGHT 32
#define TOPBAR_HEIGHT 40
#define TCP_WIDTH 190
#define SCROLLBAR_SIZE 16

typedef struct {
    Track tracks[MAX_TRACKS];
    int num_tracks;
    AudioItem items[MAX_ITEMS];
    int num_items;
    double playhead_seconds;
    bool is_playing;
    double scroll_x_sec;
    int scroll_y_px;
    float pixels_per_second;
    int track_height;
    SimpleVideoPlayer video;
    SDL_AudioDeviceID audio_device;
} DawEngine;

static DawEngine g_daw;

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
        if (!it->audio_samples) continue;

        Track *tr = &eng->tracks[it->track_idx];
        if (tr->mute) continue;
        if (any_solo && !tr->solo) continue;

        double it_s = it->start_time;
        double it_e = it->start_time + it->duration;

        if (it_e > st && it_s < et) {
            for (int f = 0; f < frames_needed; f++) {
                double cur_t = st + ((double)f / SAMPLE_RATE);
                if (cur_t >= it_s && cur_t < it_e) {
                    double rel_time = (cur_t - it_s) + it->offset_in_file;
                    int64_t sample_pos = (int64_t)(rel_time * SAMPLE_RATE);
                    if (sample_pos >= 0 && sample_pos < it->total_sample_pairs) {
                        out[f * 2]     += it->audio_samples[sample_pos * 2] * tr->volume;
                        out[f * 2 + 1] += it->audio_samples[sample_pos * 2 + 1] * tr->volume;
                    }
                }
            }
        }
    }
    eng->playhead_seconds += (double)frames_needed / SAMPLE_RATE;
}

void open_file_dialog(char *out_path, int max_len) {
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Media Files\0*.wav;*.mp3;*.flac;*.mp4;*.mkv;*.mov;*.avi\0All Files\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) strncpy(out_path, szFile, max_len);
    else out_path[0] = '\0';
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
    g_daw.video = (SimpleVideoPlayer){ .rect = { 650, 160, 480, 310 }, .is_open = false, .video_stream_idx = -1 };

    for (int i = 0; i < MAX_TRACKS; i++) {
        g_daw.tracks[i].id = i + 1;
        g_daw.tracks[i].volume = 0.8f;
    }

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
    bool dragging_scrollbar_h = false;
    bool dragging_scrollbar_v = false;
    int scrollbar_h_start_mx = 0, scrollbar_v_start_my = 0;
    double orig_scroll_x = 0; int orig_scroll_y = 0;
    bool scrubbing = false;
    int active_vol_track = -1;
    int dragging_item_idx = -1;
    bool dragging_trim_left = false, dragging_trim_right = false;
    double drag_start_mouse_time = 0.0, item_orig_start = 0.0, item_orig_dur = 0.0, item_orig_offset = 0.0;

    SDL_Event e;
    while (running) {
        int win_w, win_h;
        SDL_GetWindowSize(win, &win_w, &win_h);
        int view_w = win_w - TCP_WIDTH - SCROLLBAR_SIZE;
        int view_h = win_h - TOPBAR_HEIGHT - RULER_HEIGHT - SCROLLBAR_SIZE;
        int max_h_tracks = g_daw.num_tracks * g_daw.track_height;

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
                    AudioItem *it = &g_daw.items[g_daw.num_items];
                    it->track_idx = trk;
                    it->start_time = g_daw.playhead_seconds;
                    if (load_media_item(file, it) == 0) g_daw.num_items++;
                }
                open_video_by_path(&g_daw.video, file, ren);
                SDL_free(file);
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_SPACE) g_daw.is_playing = !g_daw.is_playing;
                else if (e.key.keysym.sym == SDLK_v) g_daw.video.is_open = !g_daw.video.is_open;
                else if (e.key.keysym.sym == SDLK_HOME) { g_daw.playhead_seconds = 0.0; seek_video(&g_daw.video, 0.0); }
                else if (e.key.keysym.sym == SDLK_s) {
                    double cur_t = g_daw.playhead_seconds;
                    for (int i = 0; i < g_daw.num_items; i++) {
                        AudioItem *it = &g_daw.items[i];
                        if (it->is_selected) {
                            double it_s = it->start_time, it_e = it->start_time + it->duration;
                            if (cur_t > it_s + 0.05 && cur_t < it_e - 0.05 && g_daw.num_items < MAX_ITEMS) {
                                AudioItem *r_item = &g_daw.items[g_daw.num_items];
                                memcpy(r_item, it, sizeof(AudioItem));
                                double delta = cur_t - it_s;
                                it->duration = delta;
                                r_item->start_time = cur_t;
                                r_item->offset_in_file = it->offset_in_file + delta;
                                r_item->duration = it_e - cur_t;
                                r_item->is_selected = true;
                                it->is_selected = false;
                                g_daw.num_items++;
                                break;
                            }
                        }
                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x, my = e.button.y;
                if (g_daw.video.is_open && mx >= g_daw.video.rect.x && mx <= g_daw.video.rect.x + g_daw.video.rect.w &&
                    my >= g_daw.video.rect.y && my <= g_daw.video.rect.y + g_daw.video.rect.h) {
                    if (mx >= g_daw.video.rect.x + g_daw.video.rect.w - 24 && my <= g_daw.video.rect.y + 24) g_daw.video.is_open = false;
                    else if (my <= g_daw.video.rect.y + 24) {
                        g_daw.video.is_dragging = true;
                        g_daw.video.drag_x = mx - g_daw.video.rect.x;
                        g_daw.video.drag_y = my - g_daw.video.rect.y;
                    }
                    continue;
                }
                if (my <= TOPBAR_HEIGHT) {
                    if (mx >= 115 && mx <= 200) g_daw.video.is_open = !g_daw.video.is_open;
                    else if (mx >= 440 && mx <= 550) {
                        char p[MAX_PATH]; open_file_dialog(p, sizeof(p));
                        if (strlen(p) > 0 && g_daw.num_items < MAX_ITEMS) {
                            AudioItem *it = &g_daw.items[g_daw.num_items];
                            it->track_idx = 0; it->start_time = g_daw.playhead_seconds;
                            if (load_media_item(p, it) == 0) g_daw.num_items++;
                            open_video_by_path(&g_daw.video, p, ren);
                        }
                    } else if (mx >= 560 && mx <= 650 && g_daw.num_tracks < MAX_TRACKS) g_daw.num_tracks++;
                    else if (mx >= 660 && mx <= 720) g_daw.is_playing = !g_daw.is_playing;
                    continue;
                }
                if (my >= win_h - SCROLLBAR_SIZE && mx >= TCP_WIDTH && mx < win_w - SCROLLBAR_SIZE) {
                    float thumb_w = fminf(120.0f, fmaxf(48.0f, (float)view_w * (40.0f / g_daw.pixels_per_second)));
                    float thumb_x = TCP_WIDTH + (float)(g_daw.scroll_x_sec / 300.0f) * (view_w - thumb_w);
                    if (mx >= thumb_x && mx <= thumb_x + thumb_w) {
                        dragging_scrollbar_h = true;
                        scrollbar_h_start_mx = mx;
                        orig_scroll_x = g_daw.scroll_x_sec;
                    } else {
                        g_daw.scroll_x_sec = fmax(0.0, ((float)(mx - TCP_WIDTH) / (float)view_w) * 300.0f);
                    }
                    continue;
                }
                if (mx >= win_w - SCROLLBAR_SIZE && my >= TOPBAR_HEIGHT + RULER_HEIGHT && my < win_h - SCROLLBAR_SIZE) {
                    dragging_scrollbar_v = true;
                    scrollbar_v_start_my = my;
                    orig_scroll_y = g_daw.scroll_y_px;
                    continue;
                }
                if (mx < TCP_WIDTH && my > TOPBAR_HEIGHT + RULER_HEIGHT) {
                    int trk = (my - TOPBAR_HEIGHT - RULER_HEIGHT + g_daw.scroll_y_px) / g_daw.track_height;
                    if (trk >= 0 && trk < g_daw.num_tracks) {
                        int rel_y = (my - TOPBAR_HEIGHT - RULER_HEIGHT + g_daw.scroll_y_px) % g_daw.track_height;
                        if (mx >= 20 && mx <= 38 && rel_y >= 10 && rel_y <= 28) g_daw.tracks[trk].armed = !g_daw.tracks[trk].armed;
                        else if (mx >= 44 && mx <= 124 && rel_y >= 12 && rel_y <= 28) { active_vol_track = trk; g_daw.tracks[trk].volume = (float)(mx - 44) / 80.0f; }
                        else if (mx >= 132 && mx <= 152 && rel_y >= 10 && rel_y <= 30) g_daw.tracks[trk].mute = !g_daw.tracks[trk].mute;
                        else if (mx >= 156 && mx <= 176 && rel_y >= 10 && rel_y <= 30) g_daw.tracks[trk].solo = !g_daw.tracks[trk].solo;
                    }
                    continue;
                }
                if (mx >= TCP_WIDTH && my > TOPBAR_HEIGHT && my < win_h - SCROLLBAR_SIZE) {
                    scrubbing = true;
                    g_daw.playhead_seconds = fmax(0.0, g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second);
                    seek_video(&g_daw.video, g_daw.playhead_seconds);
                    continue;
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                g_daw.video.is_dragging = false;
                dragging_scrollbar_h = false; dragging_scrollbar_v = false;
                scrubbing = false; active_vol_track = -1;
                dragging_item_idx = -1; dragging_trim_left = false; dragging_trim_right = false;
            } else if (e.type == SDL_MOUSEMOTION) {
                int mx = e.motion.x, my = e.motion.y;
                if (g_daw.video.is_dragging) {
                    g_daw.video.rect.x = mx - g_daw.video.drag_x;
                    g_daw.video.rect.y = my - g_daw.video.drag_y;
                }
                if (dragging_scrollbar_h) g_daw.scroll_x_sec = fmax(0.0, orig_scroll_x + ((float)(mx - scrollbar_h_start_mx) / (float)view_w) * 300.0f);
                if (dragging_scrollbar_v) {
                    int max_y = max_h_tracks - view_h; if (max_y < 0) max_y = 0;
                    g_daw.scroll_y_px = (int)(orig_scroll_y + ((float)(my - scrollbar_v_start_my) / view_h) * max_h_tracks);
                    if (g_daw.scroll_y_px < 0) g_daw.scroll_y_px = 0;
                    if (g_daw.scroll_y_px > max_y) g_daw.scroll_y_px = max_y;
                }
                if (scrubbing && mx >= TCP_WIDTH && mx < win_w - SCROLLBAR_SIZE) {
                    g_daw.playhead_seconds = fmax(0.0, g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second);
                    seek_video(&g_daw.video, g_daw.playhead_seconds);
                }
                if (active_vol_track != -1) g_daw.tracks[active_vol_track].volume = fmaxf(0.0f, fminf(1.0f, (float)(mx - 44) / 80.0f));
            }
        }

        if (g_daw.video.is_open && g_daw.video.video_stream_idx != -1) {
            update_video_player(&g_daw.video, g_daw.playhead_seconds);
        }

        // --- RENDER GUI ---
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderClear(ren);

        SDL_Color c_white = { 220, 220, 220, 255 }, c_muted = { 130, 130, 130, 255 }, c_teal = { 0, 210, 170, 255 };

        // Top Bar
        SDL_Rect bar = { 0, 0, win_w, TOPBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &bar);
        draw_text(ren, "File  Edit", 14, 8, c_white);

        SDL_Rect v_menu = { 110, 6, 90, 24 };
        SDL_SetRenderDrawColor(ren, g_daw.video.is_open ? 0 : 50, g_daw.video.is_open ? 140 : 50, g_daw.video.is_open ? 120 : 50, 255);
        SDL_RenderFillRect(ren, &v_menu);
        draw_text(ren, "View:Video", 115, 12, c_white);

        SDL_Rect btn_imp = { 440, 6, 110, 24 };
        SDL_SetRenderDrawColor(ren, 50, 50, 50, 255);
        SDL_RenderFillRect(ren, &btn_imp);
        draw_text(ren, "Import Media", 446, 12, c_white);

        SDL_Rect btn_add = { 560, 6, 90, 24 };
        SDL_SetRenderDrawColor(ren, 50, 50, 50, 255);
        SDL_RenderFillRect(ren, &btn_add);
        draw_text(ren, "+ Track", 575, 12, c_teal);

        SDL_Rect btn_play = { 660, 6, 65, 24 };
        SDL_SetRenderDrawColor(ren, g_daw.is_playing ? 160 : 50, g_daw.is_playing ? 60 : 50, 50, 255);
        SDL_RenderFillRect(ren, &btn_play);
        draw_text(ren, g_daw.is_playing ? "Stop" : "Play", 675, 12, c_white);

        // Timeline Clip Region
        SDL_Rect timeline_clip = { TCP_WIDTH, TOPBAR_HEIGHT + RULER_HEIGHT, view_w, view_h };
        SDL_RenderSetClipRect(ren, &timeline_clip);

        int sec_step = (g_daw.pixels_per_second < 40) ? 2 : 1;
        for (int s = 0; s < 3600; s += sec_step) {
            int gx = TCP_WIDTH + (int)((s - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
            if (gx < TCP_WIDTH || gx > TCP_WIDTH + view_w) continue;
            SDL_SetRenderDrawColor(ren, 46, 46, 46, 255);
            SDL_RenderDrawLine(ren, gx, TOPBAR_HEIGHT + RULER_HEIGHT, gx, TOPBAR_HEIGHT + RULER_HEIGHT + view_h);
        }

        for (int i = 0; i <= g_daw.num_tracks; i++) {
            int ty = TOPBAR_HEIGHT + RULER_HEIGHT + (i * g_daw.track_height) - g_daw.scroll_y_px;
            SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
            SDL_RenderDrawLine(ren, TCP_WIDTH, ty, TCP_WIDTH + view_w, ty);
        }

        for (int i = 0; i < g_daw.num_items; i++) {
            AudioItem *it = &g_daw.items[i];
            int ix = TCP_WIDTH + (int)((it->start_time - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
            int iy = TOPBAR_HEIGHT + RULER_HEIGHT + (it->track_idx * g_daw.track_height) - g_daw.scroll_y_px + 2;
            int iw = (int)(it->duration * g_daw.pixels_per_second);
            int ih = g_daw.track_height - 4;

            if (ix + iw < TCP_WIDTH || ix > TCP_WIDTH + view_w) continue;

            SDL_Rect clip_r = { ix, iy, iw, ih };
            SDL_SetRenderDrawColor(ren, it->is_selected ? 44 : 34, it->is_selected ? 70 : 54, it->is_selected ? 94 : 70, 255);
            SDL_RenderFillRect(ren, &clip_r);
            SDL_SetRenderDrawColor(ren, it->is_selected ? 0 : 80, it->is_selected ? 220 : 150, it->is_selected ? 190 : 160, 255);
            SDL_RenderDrawRect(ren, &clip_r);

            int mid_y = iy + ih / 2;
            SDL_SetRenderDrawColor(ren, 100, 220, 175, 255);
            for (int col = 0; col < iw; col++) {
                int px = ix + col;
                if (px < TCP_WIDTH || px > TCP_WIDTH + view_w) continue;
                double cur_item_sec = it->offset_in_file + ((double)col / g_daw.pixels_per_second);
                int bin_idx = (int)((cur_item_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN);
                if (bin_idx >= 0 && bin_idx < it->peak_count) {
                    int y1 = mid_y - (int)(it->peaks[bin_idx].max_val * (ih / 2.0f - 2));
                    int y2 = mid_y - (int)(it->peaks[bin_idx].min_val * (ih / 2.0f - 2));
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

        // Timeline Ruler
        SDL_Rect ruler = { TCP_WIDTH, TOPBAR_HEIGHT, view_w, RULER_HEIGHT };
        SDL_SetRenderDrawColor(ren, 28, 28, 28, 255);
        SDL_RenderFillRect(ren, &ruler);

        for (int s = 0; s < 3600; s += sec_step) {
            int rx = TCP_WIDTH + (int)((s - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
            if (rx < TCP_WIDTH || rx > TCP_WIDTH + view_w) continue;
            char b_txt[16], t_txt[16];
            snprintf(b_txt, sizeof(b_txt), "%d.1", s + 1);
            snprintf(t_txt, sizeof(t_txt), "0:%02d", s);
            draw_text(ren, b_txt, rx + 4, TOPBAR_HEIGHT + 4, c_white);
            draw_text(ren, t_txt, rx + 4, TOPBAR_HEIGHT + 16, c_muted);
            SDL_SetRenderDrawColor(ren, 55, 55, 55, 255);
            SDL_RenderDrawLine(ren, rx, TOPBAR_HEIGHT, rx, TOPBAR_HEIGHT + RULER_HEIGHT);
        }

        // TCP Sidebar
        SDL_Rect tcp_clip = { 0, TOPBAR_HEIGHT + RULER_HEIGHT, TCP_WIDTH, view_h };
        SDL_RenderSetClipRect(ren, &tcp_clip);
        for (int i = 0; i < g_daw.num_tracks; i++) {
            int ty = TOPBAR_HEIGHT + RULER_HEIGHT + (i * g_daw.track_height) - g_daw.scroll_y_px;
            SDL_Rect tp = { 2, ty + 2, TCP_WIDTH - 4, g_daw.track_height - 4 };
            SDL_SetRenderDrawColor(ren, 58, 58, 58, 255);
            SDL_RenderFillRect(ren, &tp);

            char n_str[8]; snprintf(n_str, sizeof(n_str), "%d", g_daw.tracks[i].id);
            draw_text(ren, n_str, 6, ty + 10, c_white);

            SDL_Rect arm = { 20, ty + 8, 16, 16 };
            SDL_SetRenderDrawColor(ren, g_daw.tracks[i].armed ? 210 : 80, 40, 40, 255);
            SDL_RenderFillRect(ren, &arm);

            SDL_Rect v_bg = { 44, ty + 10, 80, 12 };
            SDL_SetRenderDrawColor(ren, 25, 25, 25, 255);
            SDL_RenderFillRect(ren, &v_bg);
            SDL_Rect v_fill = { 44, ty + 10, (int)(80.0f * g_daw.tracks[i].volume), 12 };
            SDL_SetRenderDrawColor(ren, 150, 150, 150, 255);
            SDL_RenderFillRect(ren, &v_fill);

            SDL_Rect m_btn = { 132, ty + 8, 18, 18 };
            SDL_SetRenderDrawColor(ren, g_daw.tracks[i].mute ? 200 : 70, g_daw.tracks[i].mute ? 40 : 70, 40, 255);
            SDL_RenderFillRect(ren, &m_btn);
            draw_text(ren, "M", 137, ty + 12, c_white);

            SDL_Rect s_btn = { 156, ty + 8, 18, 18 };
            SDL_SetRenderDrawColor(ren, g_daw.tracks[i].solo ? 210 : 70, g_daw.tracks[i].solo ? 180 : 70, 40, 255);
            SDL_RenderFillRect(ren, &s_btn);
            draw_text(ren, "S", 161, ty + 12, c_white);

            SDL_SetRenderDrawColor(ren, 25, 25, 25, 255);
            SDL_RenderDrawLine(ren, 0, ty + g_daw.track_height, TCP_WIDTH, ty + g_daw.track_height);
        }
        SDL_RenderSetClipRect(ren, NULL);

        // Scrollbars
        SDL_Rect bar_h = { TCP_WIDTH, win_h - SCROLLBAR_SIZE, view_w, SCROLLBAR_SIZE };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &bar_h);
        float thumb_w = fminf(120.0f, fmaxf(48.0f, (float)view_w * (40.0f / g_daw.pixels_per_second)));
        float thumb_x = TCP_WIDTH + (float)(g_daw.scroll_x_sec / 300.0f) * (view_w - thumb_w);
        SDL_Rect thumb_h = { (int)thumb_x, win_h - SCROLLBAR_SIZE + 2, (int)thumb_w, SCROLLBAR_SIZE - 4 };
        SDL_SetRenderDrawColor(ren, 85, 85, 85, 255);
        SDL_RenderFillRect(ren, &thumb_h);

        SDL_Rect bar_v = { win_w - SCROLLBAR_SIZE, TOPBAR_HEIGHT + RULER_HEIGHT, SCROLLBAR_SIZE, view_h };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &bar_v);
        float thumb_h_sz = (max_h_tracks > 0) ? fmaxf(30.0f, (float)view_h * ((float)view_h / max_h_tracks)) : view_h;
        float thumb_y = TOPBAR_HEIGHT + RULER_HEIGHT + (max_h_tracks > view_h ? ((float)g_daw.scroll_y_px / (max_h_tracks - view_h)) * (view_h - thumb_h_sz) : 0);
        SDL_Rect thumb_v = { win_w - SCROLLBAR_SIZE + 2, (int)thumb_y, SCROLLBAR_SIZE - 4, (int)thumb_h_sz };
        SDL_SetRenderDrawColor(ren, 75, 75, 75, 255);
        SDL_RenderFillRect(ren, &thumb_v);

        // Video Window
        if (g_daw.video.is_open) {
            SDL_Rect s_rect = { g_daw.video.rect.x + 4, g_daw.video.rect.y + 4, g_daw.video.rect.w, g_daw.video.rect.h };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 140);
            SDL_RenderFillRect(ren, &s_rect);

            SDL_Rect v_bar = { g_daw.video.rect.x, g_daw.video.rect.y, g_daw.video.rect.w, 24 };
            SDL_SetRenderDrawColor(ren, 240, 240, 240, 255);
            SDL_RenderFillRect(ren, &v_bar);

            SDL_Color c_dark = { 40, 40, 40, 255 };
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
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (g_daw.audio_device > 0) SDL_CloseAudioDevice(g_daw.audio_device);
    close_video(&g_daw.video);
    for (int i = 0; i < g_daw.num_items; i++) {
        if (g_daw.items[i].audio_samples) free(g_daw.items[i].audio_samples);
        if (g_daw.items[i].peaks) free(g_daw.items[i].peaks);
    }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
