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

// FFmpeg headers for metadata, peak extraction, and video decoding
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#define MAX_TRACKS 32
#define MAX_ITEMS 128
#define RULER_HEIGHT 32
#define TOPBAR_HEIGHT 40
#define TCP_WIDTH 190
#define SCROLLBAR_SIZE 16
#define PEAK_SAMPLES_PER_BIN 256
#define SAMPLE_RATE 48000

// Exact min/max peak bin like REAPER .reapeaks
typedef struct {
    float min_val;
    float max_val;
} PeakBin;

typedef struct {
    int track_idx;
    double start_time;          // multitrack timeline position (seconds)
    double offset_in_file;      // in-point trim (seconds)
    double duration;            // item length (seconds)
    double file_total_duration;
    char file_path[512];        // Media referenced by path ONLY
    char label[128];
    PeakBin *peaks;             // Real peak cache (~50KB instead of 500MB PCM)
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

// Video reader state for the Floating Video Monitor
typedef struct {
    SDL_Rect rect;
    bool is_open;
    bool is_dragging;
    int drag_x;
    int drag_y;
    char file_path[512];
    AVFormatContext *fmt_ctx;
    AVCodecContext *codec_ctx;
    struct SwsContext *sws_ctx;
    int video_stream_idx;
    AVFrame *frame_yuv;
    AVFrame *frame_rgb;
    uint8_t *rgb_buffer;
    SDL_Texture *texture;
    int width;
    int height;
    double last_seek_time;
} VideoEngine;

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
    VideoEngine video;
} DawEngine;

static DawEngine g_daw;

// ----------------- 1. REAPER-STYLE PEAK EXTRACTION -----------------
// Decodes audio in small temporary frames to compute exact min/max peaks, then frees everything
int extract_real_peaks(const char *filepath, AudioItem *item) {
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
    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_MONO; // Mono peak envelope
    swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_FLT, SAMPLE_RATE,
                        &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, NULL);
    swr_init(swr);

    double total_sec = (fmt->duration != AV_NOPTS_VALUE) ? ((double)fmt->duration / AV_TIME_BASE) : 10.0;
    int estimated_bins = (int)((total_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN) + 1000;
    PeakBin *peak_cache = (PeakBin *)malloc(estimated_bins * sizeof(PeakBin));
    int current_bin = 0;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    float temp_pcm[4096];
    float cur_min = 0.0f, cur_max = 0.0f;
    int samples_in_bin = 0;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == a_idx) {
            if (avcodec_send_packet(ctx, pkt) == 0) {
                while (avcodec_receive_frame(ctx, frame) == 0) {
                    uint8_t *out_d[1] = { (uint8_t *)temp_pcm };
                    int converted = swr_convert(swr, out_d, 4096, (const uint8_t **)frame->extended_data, frame->nb_samples);
                    for (int s = 0; s < converted; s++) {
                        float v = temp_pcm[s];
                        if (v < cur_min) cur_min = v;
                        if (v > cur_max) cur_max = v;
                        samples_in_bin++;

                        if (samples_in_bin >= PEAK_SAMPLES_PER_BIN) {
                            if (current_bin < estimated_bins) {
                                peak_cache[current_bin].min_val = cur_min;
                                peak_cache[current_bin].max_val = cur_max;
                                current_bin++;
                            }
                            cur_min = 0.0f;
                            cur_max = 0.0f;
                            samples_in_bin = 0;
                        }
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt); // Immediately closed! ZERO audio stored in RAM

    item->peaks = peak_cache;
    item->peak_count = current_bin;
    item->file_total_duration = total_sec;
    item->duration = total_sec;
    item->offset_in_file = 0.0;
    item->is_selected = false;
    strncpy(item->file_path, filepath, sizeof(item->file_path) - 1);

    const char *name = strrchr(filepath, '\\');
    if (!name) name = strrchr(filepath, '/');
    snprintf(item->label, sizeof(item->label), "%s", name ? name + 1 : filepath);
    return 0;
}

// ----------------- 2. HARDWARE VIDEO FRAME ENGINE -----------------
void close_video_stream() {
    if (g_daw.video.texture) { SDL_DestroyTexture(g_daw.video.texture); g_daw.video.texture = NULL; }
    if (g_daw.video.sws_ctx) { sws_freeContext(g_daw.video.sws_ctx); g_daw.video.sws_ctx = NULL; }
    if (g_daw.video.frame_rgb) { av_frame_free(&g_daw.video.frame_rgb); }
    if (g_daw.video.frame_yuv) { av_frame_free(&g_daw.video.frame_yuv); }
    if (g_daw.video.rgb_buffer) { av_free(g_daw.video.rgb_buffer); g_daw.video.rgb_buffer = NULL; }
    if (g_daw.video.codec_ctx) { avcodec_free_context(&g_daw.video.codec_ctx); }
    if (g_daw.video.fmt_ctx) { avformat_close_input(&g_daw.video.fmt_ctx); }
    g_daw.video.video_stream_idx = -1;
}

int open_video_stream(const char *filepath, SDL_Renderer *ren) {
    close_video_stream();

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) return -1;
    if (avformat_find_stream_info(fmt, NULL) < 0) { avformat_close_input(&fmt); return -1; }

    int v_idx = -1;
    for (unsigned int i = 0; i < fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            v_idx = i;
            break;
        }
    }
    if (v_idx == -1) { avformat_close_input(&fmt); return -1; }

    AVCodecParameters *cpar = fmt->streams[v_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(cpar->codec_id);
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, cpar);
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return -1;
    }

    g_daw.video.fmt_ctx = fmt;
    g_daw.video.codec_ctx = ctx;
    g_daw.video.video_stream_idx = v_idx;
    g_daw.video.width = ctx->width;
    g_daw.video.height = ctx->height;
    g_daw.video.last_seek_time = -1.0;

    g_daw.video.frame_yuv = av_frame_alloc();
    g_daw.video.frame_rgb = av_frame_alloc();

    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, ctx->width, ctx->height, 1);
    g_daw.video.rgb_buffer = (uint8_t *)av_malloc(num_bytes * sizeof(uint8_t));

    av_image_fill_arrays(g_daw.video.frame_rgb->data, g_daw.video.frame_rgb->linesize,
                         g_daw.video.rgb_buffer, AV_PIX_FMT_RGB24, ctx->width, ctx->height, 1);

    g_daw.video.sws_ctx = sws_getContext(ctx->width, ctx->height, ctx->sample_fmt,
                                         ctx->width, ctx->height, AV_PIX_FMT_RGB24,
                                         SWS_BILINEAR, NULL, NULL, NULL);

    g_daw.video.texture = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, ctx->width, ctx->height);
    strncpy(g_daw.video.file_path, filepath, sizeof(g_daw.video.file_path) - 1);
    g_daw.video.is_open = true;
    return 0;
}

// Seeks and grabs a video frame synchronized with the multitrack timeline playhead
void render_video_frame(double target_seconds) {
    if (!g_daw.video.fmt_ctx || g_daw.video.video_stream_idx == -1) return;

    // Only seek if playhead moved noticeably
    if (fabs(target_seconds - g_daw.video.last_seek_time) > 0.03) {
        int64_t target_ts = (int64_t)(target_seconds * AV_TIME_BASE);
        av_seek_frame(g_daw.video.fmt_ctx, -1, target_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(g_daw.video.codec_ctx);

        AVPacket *pkt = av_packet_alloc();
        bool frame_found = false;

        while (av_read_frame(g_daw.video.fmt_ctx, pkt) >= 0) {
            if (pkt->stream_index == g_daw.video.video_stream_idx) {
                if (avcodec_send_packet(g_daw.video.codec_ctx, pkt) == 0) {
                    if (avcodec_receive_frame(g_daw.video.codec_ctx, g_daw.video.frame_yuv) == 0) {
                        sws_scale(g_daw.video.sws_ctx,
                                  (uint8_t const *const *)g_daw.video.frame_yuv->data,
                                  g_daw.video.frame_yuv->linesize, 0, g_daw.video.height,
                                  g_daw.video.frame_rgb->data, g_daw.video.frame_rgb->linesize);

                        SDL_UpdateTexture(g_daw.video.texture, NULL,
                                          g_daw.video.frame_rgb->data[0],
                                          g_daw.video.frame_rgb->linesize[0]);
                        frame_found = true;
                    }
                }
            }
            av_packet_unref(pkt);
            if (frame_found) break;
        }
        av_packet_free(&pkt);
        g_daw.video.last_seek_time = target_seconds;
    }
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
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;

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
    g_daw.video = (VideoEngine){ .rect = { 650, 160, 480, 310 }, .is_open = false, .video_stream_idx = -1 };
    strcpy(g_daw.video.file_path, "No active video");

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
            // Drag-and-drop media
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
                    if (extract_real_peaks(file, it) == 0) {
                        g_daw.num_items++;
                    }
                }
                // Also initialize video stream if container has video
                open_video_stream(file, ren);
                SDL_free(file);
            }
            // Keyboard Shortcuts
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_SPACE) {
                    g_daw.is_playing = !g_daw.is_playing;
                } else if (e.key.keysym.sym == SDLK_v) {
                    g_daw.video.is_open = !g_daw.video.is_open;
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

                // 1. Floating Video Window Header Dragging & Close
                if (g_daw.video.is_open && mx >= g_daw.video.rect.x && mx <= g_daw.video.rect.x + g_daw.video.rect.w &&
                    my >= g_daw.video.rect.y && my <= g_daw.video.rect.y + g_daw.video.rect.h) {
                    if (mx >= g_daw.video.rect.x + g_daw.video.rect.w - 24 && my <= g_daw.video.rect.y + 24) {
                        g_daw.video.is_open = false;
                    } else if (my <= g_daw.video.rect.y + 24) {
                        g_daw.video.is_dragging = true;
                        g_daw.video.drag_x = mx - g_daw.video.rect.x;
                        g_daw.video.drag_y = my - g_daw.video.rect.y;
                    }
                    continue;
                }

                // 2. Topbar Actions
                if (my <= TOPBAR_HEIGHT) {
                    if (mx >= 115 && mx <= 200) {
                        g_daw.video.is_open = !g_daw.video.is_open;
                    } else if (mx >= 440 && mx <= 550) {
                        char p[MAX_PATH]; open_file_dialog(p, sizeof(p));
                        if (strlen(p) > 0 && g_daw.num_items < MAX_ITEMS) {
                            AudioItem *it = &g_daw.items[g_daw.num_items];
                            it->track_idx = 0; it->start_time = g_daw.playhead_seconds;
                            if (extract_real_peaks(p, it) == 0) g_daw.num_items++;
                            open_video_stream(p, ren);
                        }
                    } else if (mx >= 560 && mx <= 650 && g_daw.num_tracks < MAX_TRACKS) {
                        g_daw.num_tracks++;
                    } else if (mx >= 660 && mx <= 720) {
                        g_daw.is_playing = !g_daw.is_playing;
                    }
                    continue;
                }

                // 3. Compact Horizontal Scrollbar Click
                if (my >= win_h - SCROLLBAR_SIZE && mx >= TCP_WIDTH && mx < win_w - SCROLLBAR_SIZE) {
                    float thumb_w = fminf(120.0f, fmaxf(48.0f, (float)view_w * (40.0f / g_daw.pixels_per_second)));
                    float thumb_x = TCP_WIDTH + (float)(g_daw.scroll_x_sec / 300.0f) * (view_w - thumb_w);

                    if (mx >= thumb_x && mx <= thumb_x + thumb_w) {
                        dragging_scrollbar_h = true;
                        scrollbar_h_start_mx = mx;
                        orig_scroll_x = g_daw.scroll_x_sec;
                    } else {
                        float rel = (float)(mx - TCP_WIDTH) / (float)view_w;
                        g_daw.scroll_x_sec = fmax(0.0, rel * 300.0f);
                    }
                    continue;
                }

                // 4. Vertical Scrollbar Click
                if (mx >= win_w - SCROLLBAR_SIZE && my >= TOPBAR_HEIGHT + RULER_HEIGHT && my < win_h - SCROLLBAR_SIZE) {
                    dragging_scrollbar_v = true;
                    scrollbar_v_start_my = my;
                    orig_scroll_y = g_daw.scroll_y_px;
                    continue;
                }

                // 5. TCP Volume & Mute/Solo
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

                // 6. Multitrack Items Selection & Dragging
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

                // 7. Ruler Click
                if (mx >= TCP_WIDTH && my >= TOPBAR_HEIGHT && my <= TOPBAR_HEIGHT + RULER_HEIGHT) {
                    scrubbing = true;
                    g_daw.playhead_seconds = fmax(0.0, g_daw.scroll_x_sec + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second);
                }
            }
            else if (e.type == SDL_MOUSEBUTTONUP) {
                g_daw.video.is_dragging = false;
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
                if (g_daw.video.is_dragging) {
                    g_daw.video.rect.x = mx - g_daw.video.drag_x;
                    g_daw.video.rect.y = my - g_daw.video.drag_y;
                }
                if (dragging_scrollbar_h) {
                    float delta = (float)(mx - scrollbar_h_start_mx);
                    g_daw.scroll_x_sec = fmax(0.0, orig_scroll_x + (delta / (float)view_w) * 300.0f);
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

        // Keep hardware video frame in sync with playhead
        if (g_daw.video.is_open && g_daw.video.video_stream_idx != -1) {
            render_video_frame(g_daw.playhead_seconds);
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

        draw_text(ren, "[Real Peaks Active | Hardware Video Renderer]", win_w - 390, 12, c_muted);

        // Timeline Clip Region
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

        // Horizontal Track Separators
        for (int i = 0; i <= g_daw.num_tracks; i++) {
            int ty = TOPBAR_HEIGHT + RULER_HEIGHT + (i * g_daw.track_height) - g_daw.scroll_y_px;
            SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
            SDL_RenderDrawLine(ren, TCP_WIDTH, ty, TCP_WIDTH + view_w, ty);
        }

        // 3. Render Real Audio Waveforms (REAPER-Style from Peak Cache)
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

            // True min/max sample envelope rendering
            int mid_y = iy + ih / 2;
            SDL_SetRenderDrawColor(ren, 100, 220, 175, 255);

            for (int col = 0; col < iw; col++) {
                int px = ix + col;
                if (px < TCP_WIDTH || px > TCP_WIDTH + view_w) continue;

                double cur_item_sec = it->offset_in_file + ((double)col / g_daw.pixels_per_second);
                int bin_idx = (int)((cur_item_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN);

                if (bin_idx >= 0 && bin_idx < it->peak_count) {
                    float mn = it->peaks[bin_idx].min_val;
                    float mx = it->peaks[bin_idx].max_val;

                    int y1 = mid_y - (int)(mx * (ih / 2.0f - 2));
                    int y2 = mid_y - (int)(mn * (ih / 2.0f - 2));
                    if (y1 == y2) y2 += 1;
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

        // 6. Track Control Panels (Sidebar)
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

        // 7. Compact Horizontal Scrollbar
        SDL_Rect bar_h = { TCP_WIDTH, win_h - SCROLLBAR_SIZE, view_w, SCROLLBAR_SIZE };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &bar_h);

        float thumb_w = fminf(120.0f, fmaxf(48.0f, (float)view_w * (40.0f / g_daw.pixels_per_second)));
        float thumb_x = TCP_WIDTH + (float)(g_daw.scroll_x_sec / 300.0f) * (view_w - thumb_w);
        SDL_Rect thumb_h = { (int)thumb_x, win_h - SCROLLBAR_SIZE + 2, (int)thumb_w, SCROLLBAR_SIZE - 4 };
        SDL_SetRenderDrawColor(ren, 85, 85, 85, 255);
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

        // 9. Floating Video Window (Hardware-Accelerated Frame Rendering)
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

            // Render live decoded video frame
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

    close_video_stream();
    for (int i = 0; i < g_daw.num_items; i++) {
        if (g_daw.items[i].peaks) free(g_daw.items[i].peaks);
    }
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
