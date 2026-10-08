#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <windows.h>
#include <commdlg.h>

// Font & text rendering header
#include "font.h"

// FFmpeg headers
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>

#define MAX_TRACKS 32
#define MAX_ITEMS 128
#define RULER_HEIGHT 32
#define TOPBAR_HEIGHT 40
#define TCP_WIDTH 190
#define SCROLLBAR_SIZE 16
#define SAMPLE_RATE 48000
#define PEAK_BIN_SIZE 256

typedef struct {
    float min_val;
    float max_val;
} PeakBin;

typedef struct {
    int track_idx;
    double start_time;
    double offset_in_file;
    double duration;
    char file_path[512];
    char label[128];
    float *pcm_data;
    int64_t total_samples;
    PeakBin *peaks;
    int peak_count;
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
    SDL_AudioDeviceID audio_device;
} DawEngine;

static DawEngine g_daw;

int decode_media_reference(const char *filepath, AudioItem *item) {
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) return -1;
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        avformat_close_input(&fmt);
        return -1;
    }

    int a_idx = -1;
    for (unsigned int i = 0; i < fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            a_idx = i;
            break;
        }
    }
    if (a_idx == -1) {
        avformat_close_input(&fmt);
        return -1;
    }

    AVCodecParameters *cpar = fmt->streams[a_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(cpar->codec_id);
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, cpar);
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return -1;
    }

    SwrContext *swr = swr_alloc();
    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_FLT, SAMPLE_RATE,
                        &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, NULL);
    swr_init(swr);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    int cap = SAMPLE_RATE * 2 * 60;
    float *buf = (float *)malloc(cap * sizeof(float));
    int64_t total = 0;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == a_idx) {
            if (avcodec_send_packet(ctx, pkt) == 0) {
                while (avcodec_receive_frame(ctx, frame) == 0) {
                    int dst_samples = av_rescale_rnd(swr_get_delay(swr, ctx->sample_rate) + frame->nb_samples,
                                                     SAMPLE_RATE, ctx->sample_rate, AV_ROUND_UP);
                    if ((total + dst_samples) * 2 > cap) {
                        cap = (total + dst_samples) * 4;
                        buf = (float *)realloc(buf, cap * sizeof(float));
                    }
                    uint8_t *out_d[2] = { (uint8_t *)(buf + total * 2), NULL };
                    int conv = swr_convert(swr, out_d, dst_samples, (const uint8_t **)frame->extended_data, frame->nb_samples);
                    if (conv > 0) total += conv;
                }
            }
        }
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    item->pcm_data = buf;
    item->total_samples = total;
    item->duration = (double)total / SAMPLE_RATE;
    item->offset_in_file = 0.0;
    item->is_selected = false;
    strncpy(item->file_path, filepath, sizeof(item->file_path) - 1);

    int bins = (int)(total / PEAK_BIN_SIZE) + 1;
    item->peaks = (PeakBin *)malloc(bins * sizeof(PeakBin));
    item->peak_count = bins;

    for (int b = 0; b < bins; b++) {
        float min_v = 0.0f, max_v = 0.0f;
        int64_t s_idx = (int64_t)b * PEAK_BIN_SIZE;
        for (int s = 0; s < PEAK_BIN_SIZE; s++) {
            if (s_idx + s >= total) break;
            float mono = (buf[(s_idx + s) * 2] + buf[(s_idx + s) * 2 + 1]) * 0.5f;
            if (mono < min_v) min_v = mono;
            if (mono > max_v) max_v = mono;
        }
        item->peaks[b].min_val = min_v;
        item->peaks[b].max_val = max_v;
    }

    const char *name = strrchr(filepath, '\\');
    if (!name) name = strrchr(filepath, '/');
    snprintf(item->label, sizeof(item->label), "%s", name ? name + 1 : filepath);
    return 0;
}

void audio_mixer_callback(void *userdata, Uint8 *stream, int len) {
    DawEngine *eng = (DawEngine *)userdata;
    float *out = (float *)stream;
    int frames = len / (sizeof(float) * 2);
    memset(stream, 0, len);

    if (!eng->is_playing) return;

    bool any_solo = false;
    for (int t = 0; t < eng->num_tracks; t++) {
        if (eng->tracks[t].solo) { any_solo = true; break; }
    }

    double st = eng->playhead_seconds;
    double et = st + ((double)frames / SAMPLE_RATE);

    for (int i = 0; i < eng->num_items; i++) {
        AudioItem *it = &eng->items[i];
        Track *tr = &eng->tracks[it->track_idx];
        if (tr->mute) continue;
        if (any_solo && !tr->solo) continue;

        double it_s = it->start_time;
        double it_e = it->start_time + it->duration;

        if (it_e > st && it_s < et) {
            for (int f = 0; f < frames; f++) {
                double cur = st + ((double)f / SAMPLE_RATE);
                if (cur >= it_s && cur < it_e) {
                    int64_t s_pos = (int64_t)((cur - it_s + it->offset_in_file) * SAMPLE_RATE);
                    if (s_pos >= 0 && s_pos < it->total_samples) {
                        out[f * 2]     += it->pcm_data[s_pos * 2] * tr->volume;
                        out[f * 2 + 1] += it->pcm_data[s_pos * 2 + 1] * tr->volume;
                    }
                }
            }
        }
    }
    eng->playhead_seconds += (double)frames / SAMPLE_RATE;
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
    g_daw.scroll_x_sec = 0.0;
    g_daw.scroll_y_px = 0;
    g_daw.video_win = (VideoWindow){ .rect = { 650, 160, 480, 320 }, .is_open = false };

    for (int i = 0; i < MAX_TRACKS; i++) {
        g_daw.tracks[i].id = i + 1;
        g_daw.tracks[i].volume = 0.8f;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_mixer_callback;
    want.userdata = &g_daw;
    g_daw.audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_daw.audio_device > 0) SDL_PauseAudioDevice(g_daw.audio_device, 0);

    bool running = true;
    bool dragging_scrollbar_h = false;
    bool dragging_scrollbar_v = false;
    int scrollbar_h_start_mx = 0;
    int scrollbar_v_start_my = 0;
    double orig_scroll_x = 0;
    int orig_scroll_y = 0;
    bool scrubbing = false;
    int active_vol_track = -1;

    SDL_Event e;
    while (running) {
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

                // Ctrl + Mouse Wheel = Horizontal Timeline Zoom
                if (mod & KMOD_CTRL) {
                    if (mod & KMOD_SHIFT) {
                        // Ctrl + Shift + Wheel = Vertical Track Height Zoom
                        g_daw.track_height += e.wheel.y * 8;
                        if (g_daw.track_height < 45) g_daw.track_height = 45;
                        if (g_daw.track_height > 180) g_daw.track_height = 180;
                    } else {
                        double mouse_time = g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second;
                        g_daw.pixels_per_second += e.wheel.y * 12.0f;
                        if (g_daw.pixels_per_second < 15.0f) g_daw.pixels_per_second = 15.0f;
                        if (g_daw.pixels_per_second > 400.0f) g_daw.pixels_per_second = 400.0f;
                        // Keep time anchored at cursor
                        g_daw.scroll_x_sec = mouse_time - (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second;
                        if (g_daw.scroll_x_sec < 0) g_daw.scroll_x_sec = 0;
                    }
                }
                // Shift + Wheel = Horizontal Scroll
                else if (mod & KMOD_SHIFT) {
                    g_daw.scroll_x_sec -= e.wheel.y * (30.0f / g_daw.pixels_per_second);
                    if (g_daw.scroll_x_sec < 0) g_daw.scroll_x_sec = 0;
                }
                // Normal Wheel = Vertical Track Scroll
                else {
                    g_daw.scroll_y_px -= e.wheel.y * 36;
                    int max_y_scroll = max_h_tracks - view_h;
                    if (max_y_scroll < 0) max_y_scroll = 0;
                    if (g_daw.scroll_y_px < 0) g_daw.scroll_y_px = 0;
                    if (g_daw.scroll_y_px > max_y_scroll) g_daw.scroll_y_px = max_y_scroll;
                }
            }
            // Drag and drop files
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
                    if (decode_media_reference(file, it) == 0) g_daw.num_items++;
                }
                SDL_free(file);
            }
            // Keyboard shortcuts
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_SPACE) {
                    g_daw.is_playing = !g_daw.is_playing;
                } else if (e.key.keysym.sym == SDLK_v) {
                    g_daw.video_win.is_open = !g_daw.video_win.is_open;
                } else if (e.key.keysym.sym == SDLK_HOME) {
                    g_daw.playhead_seconds = 0.0;
                }
            }
            // Mouse interactions
            else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x, my = e.button.y;

                // 1. Floating Video Window Dragging & Close
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

                // 2. Topbar Buttons
                if (my <= TOPBAR_HEIGHT) {
                    if (mx >= 115 && mx <= 200) {
                        g_daw.video_win.is_open = !g_daw.video_win.is_open;
                    } else if (mx >= 440 && mx <= 550) {
                        char p[MAX_PATH]; open_file_dialog(p, sizeof(p));
                        if (strlen(p) > 0 && g_daw.num_items < MAX_ITEMS) {
                            AudioItem *it = &g_daw.items[g_daw.num_items];
                            it->track_idx = 0; it->start_time = g_daw.playhead_seconds;
                            if (decode_media_reference(p, it) == 0) g_daw.num_items++;
                        }
                    } else if (mx >= 560 && mx <= 650 && g_daw.num_tracks < MAX_TRACKS) {
                        g_daw.num_tracks++;
                    } else if (mx >= 660 && mx <= 720) {
                        g_daw.is_playing = !g_daw.is_playing;
                    }
                    continue;
                }

                // 3. Horizontal Scrollbar Click
                if (my >= win_h - SCROLLBAR_SIZE && mx >= TCP_WIDTH && mx < win_w - SCROLLBAR_SIZE) {
                    dragging_scrollbar_h = true;
                    scrollbar_h_start_mx = mx;
                    orig_scroll_x = g_daw.scroll_x_sec;
                    continue;
                }

                // 4. Vertical Scrollbar Click
                if (mx >= win_w - SCROLLBAR_SIZE && my >= TOPBAR_HEIGHT + RULER_HEIGHT && my < win_h - SCROLLBAR_SIZE) {
                    dragging_scrollbar_v = true;
                    scrollbar_v_start_my = my;
                    orig_scroll_y = g_daw.scroll_y_px;
                    continue;
                }

                // 5. Track Control Panel (TCP)
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

                // 6. Timeline Scrubbing
                if (mx >= TCP_WIDTH && mx < win_w - SCROLLBAR_SIZE) {
                    scrubbing = true;
                    g_daw.playhead_seconds = fmax(0.0, g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second);
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                g_daw.video_win.is_dragging = false;
                dragging_scrollbar_h = false;
                dragging_scrollbar_v = false;
                scrubbing = false;
                active_vol_track = -1;
            } else if (e.type == SDL_MOUSEMOTION) {
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

        draw_text(ren, "Ctrl+Wheel: H-Zoom | Wheel: V-Scroll | Shift+Wheel: H-Scroll", 740, 12, c_muted);

        // Timeline Clip Region
        SDL_Rect timeline_clip = { TCP_WIDTH, TOPBAR_HEIGHT + RULER_HEIGHT, view_w, view_h };
        SDL_RenderSetClipRect(ren, &timeline_clip);

        // 2. Timeline Grid Lines
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

        // 3. Audio Clips & Peaks
        for (int i = 0; i < g_daw.num_items; i++) {
            AudioItem *it = &g_daw.items[i];
            int ix = TCP_WIDTH + (int)((it->start_time - g_daw.scroll_x_sec) * g_daw.pixels_per_second);
            int iy = TOPBAR_HEIGHT + RULER_HEIGHT + (it->track_idx * g_daw.track_height) - g_daw.scroll_y_px + 2;
            int iw = (int)(it->duration * g_daw.pixels_per_second);
            int ih = g_daw.track_height - 4;

            if (ix + iw < TCP_WIDTH || ix > TCP_WIDTH + view_w) continue;

            SDL_Rect clip_r = { ix, iy, iw, ih };
            SDL_SetRenderDrawColor(ren, 36, 68, 88, 255);
            SDL_RenderFillRect(ren, &clip_r);
            SDL_SetRenderDrawColor(ren, 0, 200, 180, 255);
            SDL_RenderDrawRect(ren, &clip_r);

            int mid_y = iy + ih / 2;
            SDL_SetRenderDrawColor(ren, 100, 220, 175, 255);
            for (int c = 0; c < iw; c++) {
                int px = ix + c;
                if (px < TCP_WIDTH || px > TCP_WIDTH + view_w) continue;
                double c_time = it->offset_in_file + ((double)c / g_daw.pixels_per_second);
                int b_idx = (int)((c_time * SAMPLE_RATE) / PEAK_BIN_SIZE);
                if (b_idx >= 0 && b_idx < it->peak_count) {
                    int y1 = mid_y - (int)(it->peaks[b_idx].max_val * (ih / 2 - 2));
                    int y2 = mid_y - (int)(it->peaks[b_idx].min_val * (ih / 2 - 2));
                    SDL_RenderDrawLine(ren, px, y1, px, y2);
                }
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

        // 6. Track Control Panels (Left Sidebar with vertical scroll)
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

        // 7. Horizontal Scrollbar (Bottom)
        SDL_Rect bar_h = { TCP_WIDTH, win_h - SCROLLBAR_SIZE, view_w, SCROLLBAR_SIZE };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &bar_h);

        float thumb_w = fmaxf(40.0f, (float)view_w * (60.0f / g_daw.pixels_per_second));
        float thumb_x = TCP_WIDTH + (float)(g_daw.scroll_x_sec / 300.0f) * (view_w - thumb_w);
        SDL_Rect thumb_h = { (int)thumb_x, win_h - SCROLLBAR_SIZE + 2, (int)thumb_w, SCROLLBAR_SIZE - 4 };
        SDL_SetRenderDrawColor(ren, 75, 75, 75, 255);
        SDL_RenderFillRect(ren, &thumb_h);

        // 8. Vertical Scrollbar (Right)
        SDL_Rect bar_v = { win_w - SCROLLBAR_SIZE, TOPBAR_HEIGHT + RULER_HEIGHT, SCROLLBAR_SIZE, view_h };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &bar_v);

        float thumb_h_sz = (max_h_tracks > 0) ? fmaxf(30.0f, (float)view_h * ((float)view_h / max_h_tracks)) : view_h;
        float thumb_y = TOPBAR_HEIGHT + RULER_HEIGHT + (max_h_tracks > view_h ? ((float)g_daw.scroll_y_px / (max_h_tracks - view_h)) * (view_h - thumb_h_sz) : 0);
        SDL_Rect thumb_v = { win_w - SCROLLBAR_SIZE + 2, (int)thumb_y, SCROLLBAR_SIZE - 4, (int)thumb_h_sz };
        SDL_SetRenderDrawColor(ren, 75, 75, 75, 255);
        SDL_RenderFillRect(ren, &thumb_v);

        // 9. Floating Video Window (REAPER-style Monitor)
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

            draw_text(ren, "No Active Video Stream", g_daw.video_win.rect.x + 130, g_daw.video_win.rect.y + 140, c_muted);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (g_daw.audio_device > 0) SDL_CloseAudioDevice(g_daw.audio_device);
    for (int i = 0; i < g_daw.num_items; i++) {
        free(g_daw.items[i].pcm_data);
        free(g_daw.items[i].peaks);
    }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
