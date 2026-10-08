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

// FFmpeg headers for reading metadata only
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

#define MAX_TRACKS 32
#define MAX_ITEMS 128
#define RULER_HEIGHT 32
#define TOPBAR_HEIGHT 40
#define TCP_WIDTH 190
#define SCROLLBAR_SIZE 16

typedef struct {
    int track_idx;
    double start_time;      // Multitrack timeline start (sec)
    double offset_in_file;  // In-point offset (sec)
    double duration;        // Visible length (sec)
    double file_total_duration; // Total file duration (sec)
    char file_path[512];    // Path reference only (no audio data in RAM)
    char label[128];
    bool is_selected;
} AudioItem;

typedef struct {
    int id;
    bool armed;
    bool mute;
    bool solo;
    float volume;
} Track;

typedef struct {
    SDL_Rect rect;
    bool is_open;
    bool is_dragging;
    int drag_x;
    int drag_y;
} VideoWindow;

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
    VideoWindow video_win;
    char active_video_path[512];
} DawEngine;

static DawEngine g_daw;

// Reads ONLY media header info (duration & name) — NO decoding into memory
int register_media_path(const char *filepath, AudioItem *item) {
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) return -1;
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        avformat_close_input(&fmt);
        return -1;
    }

    double dur = 10.0; // fallback 10s
    if (fmt->duration != AV_NOPTS_VALUE) {
        dur = (double)fmt->duration / AV_TIME_BASE;
    }

    bool has_video = false;
    for (unsigned int i = 0; i < fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            has_video = true;
            break;
        }
    }

    avformat_close_input(&fmt); // Closes immediately — zero audio/video stored in RAM

    item->file_total_duration = dur;
    item->duration = dur;
    item->offset_in_file = 0.0;
    item->is_selected = false;
    strncpy(item->file_path, filepath, sizeof(item->file_path) - 1);

    const char *name = strrchr(filepath, '\\');
    if (!name) name = strrchr(filepath, '/');
    snprintf(item->label, sizeof(item->label), "%s", name ? name + 1 : filepath);

    if (has_video) {
        strncpy(g_daw.active_video_path, filepath, sizeof(g_daw.active_video_path) - 1);
        g_daw.video_win.is_open = true;
    }

    return 0;
}

void open_file_dialog(char *out_path, int max_len) {
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Media Files\0*.wav;*.mp3;*.flac;*.mp4;*.mkv;*.mov\0All Files\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) strncpy(out_path, szFile, max_len);
    else out_path[0] = '\0';
}

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;

    SDL_Window *win = SDL_CreateWindow(
        "[unsaved project] - Lite Visual Multitrack DAW",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    memset(&g_daw, 0, sizeof(DawEngine));
    g_daw.num_tracks = 4;
    g_daw.pixels_per_second = 80.0f;
    g_daw.track_height = 80;
    g_daw.scroll_x_sec = 0.0;
    g_daw.scroll_y_px = 0;
    g_daw.video_win = (VideoWindow){ .rect = { 650, 160, 480, 300 }, .is_open = false };
    strcpy(g_daw.active_video_path, "No video loaded");

    for (int i = 0; i < MAX_TRACKS; i++) {
        g_daw.tracks[i].id = i + 1;
        g_daw.tracks[i].volume = 0.8f;
    }

    bool running = true;
    bool dragging_scrollbar_h = false;
    bool dragging_scrollbar_v = false;
    int scrollbar_h_start_mx = 0;
    int scrollbar_v_start_my = 0;
    double orig_scroll_x = 0;
    int orig_scroll_y = 0;
    bool scrubbing = false;
    int active_vol_track = -1;

    int dragging_item_idx = -1;
    bool dragging_trim_left = false;
    bool dragging_trim_right = false;
    double drag_start_mouse_time = 0.0;
    double item_orig_start = 0.0;
    double item_orig_dur = 0.0;
    double item_orig_offset = 0.0;

    SDL_Event e;
    Uint32 last_tick = SDL_GetTicks();

    while (running) {
        Uint32 now = SDL_GetTicks();
        float delta_time = (now - last_tick) / 1000.0f;
        last_tick = now;

        if (g_daw.is_playing) {
            g_daw.playhead_seconds += delta_time;
        }

        int win_w, win_h;
        SDL_GetWindowSize(win, &win_w, &win_h);
        int view_w = win_w - TCP_WIDTH - SCROLLBAR_SIZE;
        int view_h = win_h - TOPBAR_HEIGHT - RULER_HEIGHT - SCROLLBAR_SIZE;
        int max_h_tracks = g_daw.num_tracks * g_daw.track_height;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }
            // Mouse Wheel (Zoom & Scroll)
            else if (e.type == SDL_MOUSEWHEEL) {
                SDL_Keymod mod = SDL_GetModState();
                int mx, my;
                SDL_GetMouseState(&mx, &my);

                // Ctrl + Mouse Wheel = Zoom
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
                }
                else if (mod & KMOD_SHIFT) {
                    g_daw.scroll_x_sec -= e.wheel.y * (30.0f / g_daw.pixels_per_second);
                    if (g_daw.scroll_x_sec < 0) g_daw.scroll_x_sec = 0;
                }
                else {
                    g_daw.scroll_y_px -= e.wheel.y * 36;
                    int max_y_scroll = max_h_tracks - view_h;
                    if (max_y_scroll < 0) max_y_scroll = 0;
                    if (g_daw.scroll_y_px < 0) g_daw.scroll_y_px = 0;
                    if (g_daw.scroll_y_px > max_y_scroll) g_daw.scroll_y_px = max_y_scroll;
                }
            }
            // Drag-and-drop media (Registers Path Reference Only)
            else if (e.type == SDL_DROPFILE) {
                char *file = e.drop.file;
                int my = 0; SDL_GetMouseState(NULL, &my);
                int trk = (my - TOPBAR_HEIGHT - RULER_HEIGHT + g_daw.scroll_y_px) / g_daw.track_height;
                if (trk < 0) trk = 0;
                if (trk >= g_daw.num_tracks) trk = g_daw.num_tracks - 1;

                if (g_daw.num_items < MAX_ITEMS) {
                    AudioItem *it = &g_daw.items[g_daw.num_items];
                    it->track_idx = trk;
                    it->start_time = g_daw.playhead_seconds;
                    if (register_media_path(file, it) == 0) g_daw.num_items++;
                }
                SDL_free(file);
            }
            // Hotkeys
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_SPACE) {
                    g_daw.is_playing = !g_daw.is_playing;
                } else if (e.key.keysym.sym == SDLK_v) {
                    g_daw.video_win.is_open = !g_daw.video_win.is_open;
                } else if (e.key.keysym.sym == SDLK_HOME) {
                    g_daw.playhead_seconds = 0.0;
                } else if (e.key.keysym.sym == SDLK_s) {
                    // Split item at playhead
                    double cur_t = g_daw.playhead_seconds;
                    for (int i = 0; i < g_daw.num_items; i++) {
                        AudioItem *it = &g_daw.items[i];
                        if (it->is_selected) {
                            double it_s = it->start_time;
                            double it_e = it->start_time + it->duration;
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
            }
            // Mouse Down
            else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x, my = e.button.y;

                // 1. Video Window
                if (g_daw.video_win.is_open && mx >= g_daw.video_win.rect.x && mx <= g_daw.video_win.rect.x + g_daw.video_win.rect.w &&
                    my >= g_daw.video_win.rect.y && my <= g_daw.video_win.rect.y + g_daw.video_win.rect.h) {
                    if (mx >= g_daw.video_win.rect.x + g_daw.video_win.rect.w - 24 && my <= g_daw.video_win.rect.y + 24) {
                        g_daw.video_win.is_open = false;
                    } else if (my <= g_daw.video_win.rect.y + 24) {
                        g_daw.video_win.is_dragging = true;
                        g_daw.video_win.drag_x = mx - g_daw.video_win.rect.x;
                        g_daw.video_win.drag_y = my - g_daw.video_win.rect.y;
                    }
                    continue;
                }

                // 2. Topbar Actions
                if (my <= TOPBAR_HEIGHT) {
                    if (mx >= 115 && mx <= 200) {
                        g_daw.video_win.is_open = !g_daw.video_win.is_open;
                    } else if (mx >= 440 && mx <= 550) {
                        char p[MAX_PATH]; open_file_dialog(p, sizeof(p));
                        if (strlen(p) > 0 && g_daw.num_items < MAX_ITEMS) {
                            AudioItem *it = &g_daw.items[g_daw.num_items];
                            it->track_idx = 0; it->start_time = g_daw.playhead_seconds;
                            if (register_media_path(p, it) == 0) g_daw.num_items++;
                        }
                    } else if (mx >= 560 && mx <= 650 && g_daw.num_tracks < MAX_TRACKS) {
                        g_daw.num_tracks++;
                    } else if (mx >= 660 && mx <= 720) {
                        g_daw.is_playing = !g_daw.is_playing;
                    }
                    continue;
                }

                // 3. Scrollbars
                if (my >= win_h - SCROLLBAR_SIZE && mx >= TCP_WIDTH && mx < win_w - SCROLLBAR_SIZE) {
                    dragging_scrollbar_h = true;
                    scrollbar_h_start_mx = mx;
                    orig_scroll_x = g_daw.scroll_x_sec;
                    continue;
                }
                if (mx >= win_w - SCROLLBAR_SIZE && my >= TOPBAR_HEIGHT + RULER_HEIGHT && my < win_h - SCROLLBAR_SIZE) {
                    dragging_scrollbar_v = true;
                    scrollbar_v_start_my = my;
                    orig_scroll_y = g_daw.scroll_y_px;
                    continue;
                }

                // 4. TCP Volume & Mute/Solo
                if (mx < TCP_WIDTH && my > TOPBAR_HEIGHT + RULER_HEIGHT) {
                    int trk = (my - TOPBAR_HEIGHT - RULER_HEIGHT + g_daw.scroll_y_px) / g_daw.track_height;
                    if (trk >= 0 && trk < g_daw.num_tracks) {
                        int rel_y = (my - TOPBAR_HEIGHT - RULER_HEIGHT + g_daw.scroll_y_px) % g_daw.track_height;
                        if (mx >= 20 && mx <= 38 && rel_y >= 10 && rel_y <= 28) g_daw.tracks[trk].armed = !g_daw.tracks[trk].armed;
                        else if (mx >= 44 && mx <= 124 && rel_y >= 12 && rel_y <= 28) {
                            active_vol_track = trk;
                            g_daw.tracks[trk].volume = (float)(mx - 44) / 80.0f;
                        } else if (mx >= 132 && mx <= 152 && rel_y >= 10 && rel_y <= 30) g_daw.tracks[trk].mute = !g_daw.tracks[trk].mute;
                        else if (mx >= 156 && mx <= 176 && rel_y >= 10 && rel_y <= 30) g_daw.tracks[trk].solo = !g_daw.tracks[trk].solo;
                    }
                    continue;
                }

                // 5. Timeline Item Selection & Dragging
                if (mx >= TCP_WIDTH && my > TOPBAR_HEIGHT + RULER_HEIGHT && my < win_h - SCROLLBAR_SIZE) {
                    double click_time = g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second;
                    int click_track = (my - TOPBAR_HEIGHT - RULER_HEIGHT + g_daw.scroll_y_px) / g_daw.track_height;

                    for (int i = 0; i < g_daw.num_items; i++) g_daw.items[i].is_selected = false;
                    bool hit = false;

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
                            hit = true;
                            break;
                        }
                    }
                    if (!hit) {
                        scrubbing = true;
                        g_daw.playhead_seconds = fmax(0.0, click_time);
                    }
                    continue;
                }

                // 6. Ruler Click
                if (mx >= TCP_WIDTH && my >= TOPBAR_HEIGHT && my <= TOPBAR_HEIGHT + RULER_HEIGHT) {
                    scrubbing = true;
                    g_daw.playhead_seconds = fmax(0.0, g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second);
                }
            }
            else if (e.type == SDL_MOUSEBUTTONUP) {
                g_daw.video_win.is_dragging = false;
                dragging_scrollbar_h = false;
                dragging_scrollbar_v = false;
                scrubbing = false;
                active_vol_track = -1;
                dragging_item_idx = -1;
                dragging_trim_left = false;
                dragging_trim_right = false;
            }
            else if (e.type == SDL_MOUSEMOTION) {
                int mx = e.motion.x, my = e.motion.y;
                if (g_daw.video_win.is_dragging) {
                    g_daw.video_win.rect.x = mx - g_daw.video_win.drag_x;
                    g_daw.video_win.rect.y = my - g_daw.video_win.drag_y;
                }
                if (dragging_scrollbar_h) {
                    float delta = (float)(mx - scrollbar_h_start_mx);
                    g_daw.scroll_x_sec = fmax(0.0, orig_scroll_x + (delta / view_w) * 300.0f);
                }
                if (dragging_scrollbar_v) {
                    float delta = (float)(my - scrollbar_v_start_my);
                    int max_y = max_h_tracks - view_h;
                    if (max_y < 0) max_y = 0;
                    g_daw.scroll_y_px = (int)(orig_scroll_y + (delta / view_h) * max_h_tracks);
                    if (g_daw.scroll_y_px < 0) g_daw.scroll_y_px = 0;
                    if (g_daw.scroll_y_px > max_y) g_daw.scroll_y_px = max_y;
                }
                if (scrubbing && mx >= TCP_WIDTH && mx < win_w - SCROLLBAR_SIZE) {
                    g_daw.playhead_seconds = fmax(0.0, g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second);
                }
                if (active_vol_track != -1) {
                    float v = (float)(mx - 44) / 80.0f;
                    g_daw.tracks[active_vol_track].volume = fmaxf(0.0f, fminf(1.0f, v));
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
                        if (item_orig_dur + delta_t > 0.1) {
                            it->duration = item_orig_dur + delta_t;
                        }
                    } else {
                        it->start_time = fmax(0.0, item_orig_start + delta_t);
                        int target_t = (my - TOPBAR_HEIGHT - RULER_HEIGHT + g_daw.scroll_y_px) / g_daw.track_height;
                        if (target_t >= 0 && target_t < g_daw.num_tracks) it->track_idx = target_t;
                    }
                }
            }
        }

        // ================= RENDERING =================
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderClear(ren);

        SDL_Color c_white = { 220, 220, 220, 255 };
        SDL_Color c_muted = { 130, 130, 130, 255 };
        SDL_Color c_teal  = { 0, 210, 170, 255 };

        // 1. Top Bar
        SDL_Rect bar = { 0, 0, win_w, TOPBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &bar);
        draw_text(ren, "File  Edit", 14, 8, c_white);

        SDL_Rect v_menu = { 110, 6, 90, 24 };
        SDL_SetRenderDrawColor(ren, g_daw.video_win.is_open ? 0 : 50, g_daw.video_win.is_open ? 140 : 50, g_daw.video_win.is_open ? 120 : 50, 255);
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

        draw_text(ren, "[Lite Visual Mode - Zero Audio in RAM]", win_w - 340, 12, c_muted);

        // Timeline Area Clip
        SDL_Rect timeline_clip = { TCP_WIDTH, TOPBAR_HEIGHT + RULER_HEIGHT, view_w, view_h };
        SDL_RenderSetClipRect(ren, &timeline_clip);

        // 2. Grid Lines
        int sec_step = (g_daw.pixels_per_second < 40) ? 2 : 1;
        for (int s = 0; s < 3600; s += sec_step) {
            int gx = TCP_WIDTH + (int)((s - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
            if (gx < TCP_WIDTH || gx > TCP_WIDTH + view_w) continue;
            SDL_SetRenderDrawColor(ren, 46, 46, 46, 255);
            SDL_RenderDrawLine(ren, gx, TOPBAR_HEIGHT + RULER_HEIGHT, gx, TOPBAR_HEIGHT + RULER_HEIGHT + view_h);
        }

        // Horizontal Track Lanes
        for (int i = 0; i <= g_daw.num_tracks; i++) {
            int ty = TOPBAR_HEIGHT + RULER_HEIGHT + (i * g_daw.track_height) - g_daw.scroll_y_px;
            SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
            SDL_RenderDrawLine(ren, TCP_WIDTH, ty, TCP_WIDTH + view_w, ty);
        }

        // 3. Render Items with Lightweight Procedural Pseudo-Waveforms
        for (int i = 0; i < g_daw.num_items; i++) {
            AudioItem *it = &g_daw.items[i];
            int ix = TCP_WIDTH + (int)((it->start_time - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
            int iy = TOPBAR_HEIGHT + RULER_HEIGHT + (it->track_idx * g_daw.track_height) - g_daw.scroll_y_px + 2;
            int iw = (int)(it->duration * g_daw.pixels_per_second);
            int ih = g_daw.track_height - 4;

            if (ix + iw < TCP_WIDTH || ix > TCP_WIDTH + view_w) continue;

            SDL_Rect clip_r = { ix, iy, iw, ih };
            SDL_SetRenderDrawColor(ren, it->is_selected ? 46 : 34, it->is_selected ? 72 : 56, it->is_selected ? 96 : 72, 255);
            SDL_RenderFillRect(ren, &clip_r);
            SDL_SetRenderDrawColor(ren, it->is_selected ? 0 : 80, it->is_selected ? 220 : 150, it->is_selected ? 190 : 160, 255);
            SDL_RenderDrawRect(ren, &clip_r);

            // Procedural waveform bars (zero sample arrays needed)
            int mid_y = iy + ih / 2;
            SDL_SetRenderDrawColor(ren, 100, 220, 175, 255);
            for (int c = 4; c < iw - 4; c += 2) {
                int px = ix + c;
                if (px < TCP_WIDTH || px > TCP_WIDTH + view_w) continue;
                double t_val = (it->offset_in_file + ((double)c / g_daw.pixels_per_second)) * 30.0;
                float amp = (float)(fabs(sin(t_val) * cos(t_val * 0.3) * sin(t_val * 1.7)));
                int h = (int)(amp * (ih / 2 - 4));
                SDL_RenderDrawLine(ren, px, mid_y - h, px, mid_y + h);
            }

            draw_text(ren, it->label, ix + 6, iy + 6, c_white);
        }

        // 4. Playhead Line
        int cur_px = TCP_WIDTH + (int)((g_daw.playhead_seconds - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
        if (cur_px >= TCP_WIDTH && cur_px <= TCP_WIDTH + view_w) {
            SDL_SetRenderDrawColor(ren, 0, 220, 170, 255);
            SDL_RenderDrawLine(ren, cur_px, TOPBAR_HEIGHT, cur_px, TOPBAR_HEIGHT + RULER_HEIGHT + view_h);
        }

        SDL_RenderSetClipRect(ren, NULL);

        // 5. Timeline Ruler
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

        // 6. Track Control Panels
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

        // 7. Horizontal Scrollbar
        SDL_Rect bar_h = { TCP_WIDTH, win_h - SCROLLBAR_SIZE, view_w, SCROLLBAR_SIZE };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &bar_h);

        float thumb_w = fmaxf(40.0f, (float)view_w * (60.0f / g_daw.pixels_per_second));
        float thumb_x = TCP_WIDTH + (float)(g_daw.scroll_x_sec / 300.0f) * (view_w - thumb_w);
        SDL_Rect thumb_h = { (int)thumb_x, win_h - SCROLLBAR_SIZE + 2, (int)thumb_w, SCROLLBAR_SIZE - 4 };
        SDL_SetRenderDrawColor(ren, 75, 75, 75, 255);
        SDL_RenderFillRect(ren, &thumb_h);

        // 8. Vertical Scrollbar
        SDL_Rect bar_v = { win_w - SCROLLBAR_SIZE, TOPBAR_HEIGHT + RULER_HEIGHT, SCROLLBAR_SIZE, view_h };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &bar_v);

        float thumb_h_sz = (max_h_tracks > 0) ? fmaxf(30.0f, (float)view_h * ((float)view_h / max_h_tracks)) : view_h;
        float thumb_y = TOPBAR_HEIGHT + RULER_HEIGHT + (max_h_tracks > view_h ? ((float)g_daw.scroll_y_px / (max_h_tracks - view_h)) * (view_h - thumb_h_sz) : 0);
        SDL_Rect thumb_v = { win_w - SCROLLBAR_SIZE + 2, (int)thumb_y, SCROLLBAR_SIZE - 4, (int)thumb_h_sz };
        SDL_SetRenderDrawColor(ren, 75, 75, 75, 255);
        SDL_RenderFillRect(ren, &thumb_v);

        // 9. Floating Video Window (Displays loaded media path reference)
        if (g_daw.video_win.is_open) {
            SDL_Rect s_rect = { g_daw.video_win.rect.x + 4, g_daw.video_win.rect.y + 4, g_daw.video_win.rect.w, g_daw.video_win.rect.h };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 140);
            SDL_RenderFillRect(ren, &s_rect);

            SDL_Rect v_bar = { g_daw.video_win.rect.x, g_daw.video_win.rect.y, g_daw.video_win.rect.w, 24 };
            SDL_SetRenderDrawColor(ren, 240, 240, 240, 255);
            SDL_RenderFillRect(ren, &v_bar);

            SDL_Color c_dark = { 40, 40, 40, 255 };
            draw_text(ren, "Video Monitor", g_daw.video_win.rect.x + 8, g_daw.video_win.rect.y + 7, c_dark);
            draw_text(ren, "X", g_daw.video_win.rect.x + g_daw.video_win.rect.w - 18, g_daw.video_win.rect.y + 7, c_dark);

            SDL_Rect v_canvas = { g_daw.video_win.rect.x, g_daw.video_win.rect.y + 24, g_daw.video_win.rect.w, g_daw.video_win.rect.h - 24 };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderFillRect(ren, &v_canvas);

            draw_text(ren, "Media Reference Path:", g_daw.video_win.rect.x + 12, g_daw.video_win.rect.y + 40, c_teal);
            draw_text(ren, g_daw.active_video_path, g_daw.video_win.rect.x + 12, g_daw.video_win.rect.y + 60, c_white);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
