#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>
#include <commdlg.h>

// FFmpeg Headers
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>

#define MAX_TRACKS 16
#define MAX_ITEMS 64
#define RULER_HEIGHT 38
#define TOPBAR_HEIGHT 44
#define TCP_WIDTH 190
#define TRACK_HEIGHT 80
#define SAMPLE_RATE 48000
#define PIXELS_PER_SECOND 60.0f

// 8x8 ASCII font bitmap
static const unsigned char font8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00},{0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00},
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00},{0x00,0x66,0xA6,0xD8,0x1B,0x65,0x66,0x00},
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},{0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},{0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},{0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},{0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00},
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},{0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00},{0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    {0x0C,0x1C,0x34,0x64,0x7E,0x04,0x04,0x00},{0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},{0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},{0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},{0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},{0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00},{0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00},
    {0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x3C,0x00},{0x18,0x3C,0x66,0x7E,0x66,0x66,0x66,0x00},
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},{0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},{0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00},{0x3C,0x66,0x60,0x6E,0x66,0x66,0x3A,0x00},
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},{0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x0E,0x06,0x06,0x06,0x66,0x66,0x3C,0x00},{0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},{0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},{0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},{0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00},
    {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00},{0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},{0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},{0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},{0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},{0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00},{0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00},
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00},{0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00},
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00},{0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00},
    {0x1C,0x30,0x78,0x30,0x30,0x30,0x30,0x00},{0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C},
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00},{0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    {0x06,0x00,0x0E,0x06,0x06,0x66,0x3C,0x00},{0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},{0x00,0x00,0x66,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00},{0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00},
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60},{0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06},
    {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00},{0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00},
    {0x18,0x18,0x7E,0x18,0x18,0x18,0x0C,0x00},{0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00},
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00},{0x00,0x00,0x63,0x6B,0x7F,0x3E,0x36,0x00},
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00},{0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C},
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00},{0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},{0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}
};

void draw_char(SDL_Renderer *r, char c, int x, int y, SDL_Color color) {
    if (c < 32 || c > 126) return;
    const unsigned char *glyph = font8x8[c - 32];
    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (glyph[row] & (1 << (7 - col))) {
                SDL_RenderDrawPoint(r, x + col, y + row);
            }
        }
    }
}

void draw_text(SDL_Renderer *r, const char *str, int x, int y, SDL_Color color) {
    int cur_x = x;
    while (*str) {
        draw_char(r, *str, cur_x, y, color);
        cur_x += 8;
        str++;
    }
}

// ----------------- MULTITRACK & CLIP STRUCTURES -----------------
typedef struct {
    int track_idx;
    float start_time;
    float duration;
    char filename[128];
    float *pcm_data;        // Stereo interleaved 32-bit float samples
    int64_t total_samples;  // Total stereo sample pairs
} AudioItem;

typedef struct {
    int id;
    bool armed;
    bool mute;
    bool solo;
    float volume;
} Track;

typedef struct {
    Track tracks[MAX_TRACKS];
    int num_tracks;
    AudioItem items[MAX_ITEMS];
    int num_items;
    double playhead_seconds;
    bool is_playing;
    SDL_AudioDeviceID audio_device;
} DawEngine;

static DawEngine g_daw;

// ----------------- FFMPEG AUDIO DECODER -----------------
int decode_audio_file(const char *filepath, AudioItem *item) {
    AVFormatContext *fmt_ctx = NULL;
    if (avformat_open_input(&fmt_ctx, filepath, NULL, NULL) < 0) return -1;
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    int stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            stream_idx = i;
            break;
        }
    }
    if (stream_idx == -1) {
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    AVCodecParameters *codecpar = fmt_ctx->streams[stream_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, codecpar);
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    SwrContext *swr = swr_alloc();
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&swr,
        &out_ch_layout, AV_SAMPLE_FMT_FLT, SAMPLE_RATE,
        &codec_ctx->ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,
        0, NULL);
    swr_init(swr);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    int capacity = SAMPLE_RATE * 2 * 60; // Start with 60 sec buffer
    float *buffer = (float *)malloc(capacity * sizeof(float));
    int64_t total_samples = 0;

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == stream_idx) {
            if (avcodec_send_packet(codec_ctx, pkt) == 0) {
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    int dst_nb_samples = av_rescale_rnd(
                        swr_get_delay(swr, codec_ctx->sample_rate) + frame->nb_samples,
                        SAMPLE_RATE, codec_ctx->sample_rate, AV_ROUND_UP);

                    if ((total_samples + dst_nb_samples) * 2 > capacity) {
                        capacity = (total_samples + dst_nb_samples) * 2 * 2;
                        buffer = (float *)realloc(buffer, capacity * sizeof(float));
                    }

                    uint8_t *out_data[2] = { (uint8_t *)(buffer + total_samples * 2), NULL };
                    int converted = swr_convert(swr, out_data, dst_nb_samples,
                                                (const uint8_t **)frame->extended_data, frame->nb_samples);
                    if (converted > 0) total_samples += converted;
                }
            }
        }
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    item->pcm_data = buffer;
    item->total_samples = total_samples;
    item->duration = (float)total_samples / (float)SAMPLE_RATE;

    const char *slash = strrchr(filepath, '\\');
    if (!slash) slash = strrchr(filepath, '/');
    snprintf(item->filename, sizeof(item->filename), "%s", slash ? slash + 1 : filepath);

    return 0;
}

// ----------------- SDL2 AUDIO MIXER CALLBACK -----------------
void audio_mixer_callback(void *userdata, Uint8 *stream, int len) {
    DawEngine *engine = (DawEngine *)userdata;
    float *out = (float *)stream;
    int frames_needed = len / (sizeof(float) * 2);

    memset(stream, 0, len);
    if (!engine->is_playing) return;

    bool any_solo = false;
    for (int t = 0; t < engine->num_tracks; t++) {
        if (engine->tracks[t].solo) { any_solo = true; break; }
    }

    double start_time = engine->playhead_seconds;
    double end_time = start_time + ((double)frames_needed / SAMPLE_RATE);

    for (int i = 0; i < engine->num_items; i++) {
        AudioItem *it = &engine->items[i];
        Track *tr = &engine->tracks[it->track_idx];

        if (tr->mute) continue;
        if (any_solo && !tr->solo) continue;

        double it_start = it->start_time;
        double it_end = it->start_time + it->duration;

        if (it_end > start_time && it_start < end_time) {
            for (int f = 0; f < frames_needed; f++) {
                double cur_t = start_time + ((double)f / SAMPLE_RATE);
                if (cur_t >= it_start && cur_t < it_end) {
                    int64_t sample_pos = (int64_t)((cur_t - it_start) * SAMPLE_RATE);
                    if (sample_pos < it->total_samples) {
                        out[f * 2]     += it->pcm_data[sample_pos * 2] * tr->volume;
                        out[f * 2 + 1] += it->pcm_data[sample_pos * 2 + 1] * tr->volume;
                    }
                }
            }
        }
    }

    engine->playhead_seconds += (double)frames_needed / SAMPLE_RATE;
}

// ----------------- NATIVE WINDOWS OPEN FILE DIALOG -----------------
void open_file_dialog(char *out_path, int max_len) {
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Audio Files (*.wav;*.mp3;*.flac;*.ogg)\0*.wav;*.mp3;*.flac;*.ogg\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        strncpy(out_path, szFile, max_len);
    } else {
        out_path[0] = '\0';
    }
}

// ----------------- MAIN GUI & EVENT LOOP -----------------
int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return 1;

    SDL_Window *win = SDL_CreateWindow(
        "[unsaved project] - Multitrack Audio Editor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // Initialize Multitrack Engine
    memset(&g_daw, 0, sizeof(DawEngine));
    g_daw.num_tracks = 2;
    for (int i = 0; i < MAX_TRACKS; i++) {
        g_daw.tracks[i].id = i + 1;
        g_daw.tracks[i].armed = (i == 0);
        g_daw.tracks[i].volume = 0.8f;
    }

    // Configure Realtime Audio Output
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_mixer_callback;
    want.userdata = &g_daw;

    g_daw.audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_daw.audio_device > 0) {
        SDL_PauseAudioDevice(g_daw.audio_device, 0);
    }

    char status_bar[128] = "Drag & Drop audio files onto track lanes. Space: Play/Pause.";
    int dragging_vol_track = -1;
    bool scrubbing_timeline = false;
    bool running = true;
    SDL_Event e;

    while (running) {
        int win_w, win_h;
        SDL_GetWindowSize(win, &win_w, &win_h);

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }
            // ----------------- DRAG AND DROP HANDLER -----------------
            else if (e.type == SDL_DROPFILE) {
                char *dropped_file = e.drop.file;
                int drop_y = 0;
                SDL_GetMouseState(NULL, &drop_y);

                int target_track = (drop_y - TOPBAR_HEIGHT) / TRACK_HEIGHT;
                if (target_track < 0) target_track = 0;
                if (target_track >= g_daw.num_tracks) {
                    if (g_daw.num_tracks < MAX_TRACKS) {
                        target_track = g_daw.num_tracks;
                        g_daw.num_tracks++;
                    } else {
                        target_track = g_daw.num_tracks - 1;
                    }
                }

                if (g_daw.num_items < MAX_ITEMS) {
                    AudioItem *item = &g_daw.items[g_daw.num_items];
                    item->track_idx = target_track;
                    item->start_time = (float)g_daw.playhead_seconds;
                    if (decode_audio_file(dropped_file, item) == 0) {
                        g_daw.num_items++;
                        snprintf(status_bar, sizeof(status_bar), "Imported: %s on Track %d", item->filename, target_track + 1);
                    } else {
                        snprintf(status_bar, sizeof(status_bar), "Failed to decode dropped file.");
                    }
                }
                SDL_free(dropped_file);
            }
            // ----------------- KEYBOARD SHORTCUTS -----------------
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_SPACE) {
                    g_daw.is_playing = !g_daw.is_playing;
                    snprintf(status_bar, sizeof(status_bar), g_daw.is_playing ? "Playback started." : "Playback paused.");
                } else if ((e.key.keysym.mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_i) {
                    // Open File Import Dialog
                    char open_path[MAX_PATH];
                    open_file_dialog(open_path, sizeof(open_path));
                    if (strlen(open_path) > 0 && g_daw.num_items < MAX_ITEMS) {
                        AudioItem *item = &g_daw.items[g_daw.num_items];
                        item->track_idx = 0;
                        item->start_time = (float)g_daw.playhead_seconds;
                        if (decode_audio_file(open_path, item) == 0) {
                            g_daw.num_items++;
                            snprintf(status_bar, sizeof(status_bar), "Imported: %s", item->filename);
                        }
                    }
                }
            }
            // ----------------- MOUSE INTERACTIONS -----------------
            else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x;
                int my = e.button.y;

                // Topbar Menus
                if (my <= TOPBAR_HEIGHT) {
                    // [Import Media...] Button
                    if (mx >= 440 && mx <= 555) {
                        char open_path[MAX_PATH];
                        open_file_dialog(open_path, sizeof(open_path));
                        if (strlen(open_path) > 0 && g_daw.num_items < MAX_ITEMS) {
                            AudioItem *item = &g_daw.items[g_daw.num_items];
                            item->track_idx = 0;
                            item->start_time = (float)g_daw.playhead_seconds;
                            if (decode_audio_file(open_path, item) == 0) {
                                g_daw.num_items++;
                                snprintf(status_bar, sizeof(status_bar), "Imported: %s", item->filename);
                            }
                        }
                    }
                    // [+ Add Track] Button
                    else if (mx >= 565 && mx <= 665) {
                        if (g_daw.num_tracks < MAX_TRACKS) {
                            g_daw.num_tracks++;
                            snprintf(status_bar, sizeof(status_bar), "Track %d created.", g_daw.num_tracks);
                        }
                    }
                    // [Play / Stop] Button
                    else if (mx >= 675 && mx <= 735) {
                        g_daw.is_playing = !g_daw.is_playing;
                    }
                    continue;
                }

                // Track Control Panel (TCP)
                if (mx < TCP_WIDTH && my > TOPBAR_HEIGHT) {
                    int t_idx = (my - TOPBAR_HEIGHT) / TRACK_HEIGHT;
                    if (t_idx >= 0 && t_idx < g_daw.num_tracks) {
                        int rel_y = (my - TOPBAR_HEIGHT) % TRACK_HEIGHT;
                        if (mx >= 20 && mx <= 38 && rel_y >= 10 && rel_y <= 28) {
                            g_daw.tracks[t_idx].armed = !g_daw.tracks[t_idx].armed;
                        } else if (mx >= 44 && mx <= 124 && rel_y >= 12 && rel_y <= 28) {
                            dragging_vol_track = t_idx;
                            g_daw.tracks[t_idx].volume = (float)(mx - 44) / 80.0f;
                        } else if (mx >= 132 && mx <= 152 && rel_y >= 10 && rel_y <= 30) {
                            g_daw.tracks[t_idx].mute = !g_daw.tracks[t_idx].mute;
                        } else if (mx >= 156 && mx <= 176 && rel_y >= 10 && rel_y <= 30) {
                            g_daw.tracks[t_idx].solo = !g_daw.tracks[t_idx].solo;
                        }
                    } else if (t_idx == g_daw.num_tracks && g_daw.num_tracks < MAX_TRACKS) {
                        g_daw.num_tracks++;
                    }
                    continue;
                }

                // Scrub Timeline
                if (mx >= TCP_WIDTH && my >= TOPBAR_HEIGHT) {
                    scrubbing_timeline = true;
                    g_daw.playhead_seconds = (double)(mx - TCP_WIDTH) / PIXELS_PER_SECOND;
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                dragging_vol_track = -1;
                scrubbing_timeline = false;
            } else if (e.type == SDL_MOUSEMOTION) {
                if (dragging_vol_track != -1) {
                    float v = (float)(e.motion.x - 44) / 80.0f;
                    if (v < 0.0f) v = 0.0f;
                    if (v > 1.0f) v = 1.0f;
                    g_daw.tracks[dragging_vol_track].volume = v;
                }
                if (scrubbing_timeline && e.motion.x >= TCP_WIDTH) {
                    g_daw.playhead_seconds = (double)(e.motion.x - TCP_WIDTH) / PIXELS_PER_SECOND;
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

        draw_text(ren, "File  Edit  View  Track  Actions", 14, 8, c_white);

        // Clickable [Import Media...]
        SDL_Rect btn_imp = { 435, 6, 115, 22 };
        SDL_SetRenderDrawColor(ren, 52, 52, 52, 255);
        SDL_RenderFillRect(ren, &btn_imp);
        draw_text(ren, "Import Media", 442, 13, c_white);

        // Clickable [+ Add Track]
        SDL_Rect btn_add = { 560, 6, 100, 22 };
        SDL_SetRenderDrawColor(ren, 52, 52, 52, 255);
        SDL_RenderFillRect(ren, &btn_add);
        draw_text(ren, "+ Track", 575, 13, c_teal);

        // Clickable [Play / Stop]
        SDL_Rect btn_play = { 670, 6, 65, 22 };
        SDL_SetRenderDrawColor(ren, g_daw.is_playing ? 160 : 52, g_daw.is_playing ? 60 : 52, 52, 255);
        SDL_RenderFillRect(ren, &btn_play);
        draw_text(ren, g_daw.is_playing ? "Stop" : "Play", 685, 13, c_white);

        // Status & Audio Format
        draw_text(ren, status_bar, 750, 13, c_teal);
        draw_text(ren, "[48kHz 32b Float Mixer Active]", win_w - 250, 13, c_muted);

        // 2. Timeline Grid & Track Lanes
        int timeline_w = win_w - TCP_WIDTH;
        for (int i = 0; i < timeline_w / 60 + 1; i++) {
            int gx = TCP_WIDTH + (i * 60);
            SDL_SetRenderDrawColor(ren, (i % 2 == 0) ? 45 : 40, (i % 2 == 0) ? 45 : 40, (i % 2 == 0) ? 45 : 40, 255);
            SDL_Rect col = { gx, TOPBAR_HEIGHT + RULER_HEIGHT, 60, win_h };
            SDL_RenderFillRect(ren, &col);

            SDL_SetRenderDrawColor(ren, 33, 33, 33, 255);
            SDL_RenderDrawLine(ren, gx, TOPBAR_HEIGHT + RULER_HEIGHT, gx, win_h);
        }

        for (int i = 0; i < g_daw.num_tracks; i++) {
            int ty = TOPBAR_HEIGHT + RULER_HEIGHT + (i * TRACK_HEIGHT);
            SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
            SDL_RenderDrawLine(ren, TCP_WIDTH, ty, win_w, ty);
        }

        // 3. Audio Items / Clips on Timeline
        for (int i = 0; i < g_daw.num_items; i++) {
            AudioItem *it = &g_daw.items[i];
            int ix = TCP_WIDTH + (int)(it->start_time * PIXELS_PER_SECOND);
            int iy = TOPBAR_HEIGHT + RULER_HEIGHT + (it->track_idx * TRACK_HEIGHT) + 4;
            int iw = (int)(it->duration * PIXELS_PER_SECOND);
            int ih = TRACK_HEIGHT - 8;

            // Clip Body
            SDL_Rect clip_rect = { ix, iy, iw, ih };
            SDL_SetRenderDrawColor(ren, 40, 85, 110, 255);
            SDL_RenderFillRect(ren, &clip_rect);

            // Clip Border
            SDL_SetRenderDrawColor(ren, 0, 180, 220, 255);
            SDL_RenderDrawRect(ren, &clip_rect);

            // Clip Label
            draw_text(ren, it->filename, ix + 6, iy + 6, c_white);

            // Fake waveform center line
            SDL_SetRenderDrawColor(ren, 80, 170, 210, 255);
            SDL_RenderDrawLine(ren, ix + 4, iy + ih / 2, ix + iw - 4, iy + ih / 2);
        }

        // 4. Timeline Ruler
        SDL_Rect ruler = { TCP_WIDTH, TOPBAR_HEIGHT, timeline_w, RULER_HEIGHT };
        SDL_SetRenderDrawColor(ren, 28, 28, 28, 255);
        SDL_RenderFillRect(ren, &ruler);

        for (int i = 0; i < timeline_w / 120 + 1; i++) {
            int rx = TCP_WIDTH + (i * 120);
            char bar_txt[16], time_txt[16];
            snprintf(bar_txt, sizeof(bar_txt), "%d.1", i * 2 + 1);
            snprintf(time_txt, sizeof(time_txt), "0:%02d.000", i * 2);

            draw_text(ren, bar_txt, rx + 6, TOPBAR_HEIGHT + 6, c_white);
            draw_text(ren, time_txt, rx + 6, TOPBAR_HEIGHT + 20, c_muted);

            SDL_SetRenderDrawColor(ren, 55, 55, 55, 255);
            SDL_RenderDrawLine(ren, rx, TOPBAR_HEIGHT, rx, TOPBAR_HEIGHT + RULER_HEIGHT);
        }

        // 5. Playhead Line & Pointer
        int cur_px = TCP_WIDTH + (int)(g_daw.playhead_seconds * PIXELS_PER_SECOND);
        SDL_SetRenderDrawColor(ren, 0, 210, 170, 255);
        SDL_RenderDrawLine(ren, cur_px, TOPBAR_HEIGHT, cur_px, win_h);
        for (int t = 0; t < 8; t++) {
            SDL_RenderDrawLine(ren, cur_px - t, TOPBAR_HEIGHT + t, cur_px + t, TOPBAR_HEIGHT + t);
        }

        // 6. Track Control Panels (TCP)
        SDL_Rect tcp_bg = { 0, TOPBAR_HEIGHT, TCP_WIDTH, win_h - TOPBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 48, 48, 48, 255);
        SDL_RenderFillRect(ren, &tcp_bg);

        for (int i = 0; i < g_daw.num_tracks; i++) {
            int ty = TOPBAR_HEIGHT + (i * TRACK_HEIGHT);
            SDL_Rect tp = { 2, ty + 2, TCP_WIDTH - 4, TRACK_HEIGHT - 4 };
            SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
            SDL_RenderFillRect(ren, &tp);

            char num_str[8];
            snprintf(num_str, sizeof(num_str), "%d", g_daw.tracks[i].id);
            draw_text(ren, num_str, 6, ty + 12, c_white);

            // Record Arm
            SDL_Rect arm = { 20, ty + 10, 18, 18 };
            SDL_SetRenderDrawColor(ren, g_daw.tracks[i].armed ? 210 : 80, 40, 40, 255);
            SDL_RenderFillRect(ren, &arm);

            // Volume Fader
            SDL_Rect v_bg = { 44, ty + 13, 80, 12 };
            SDL_SetRenderDrawColor(ren, 25, 25, 25, 255);
            SDL_RenderFillRect(ren, &v_bg);
            SDL_Rect v_fill = { 44, ty + 13, (int)(80.0f * g_daw.tracks[i].volume), 12 };
            SDL_SetRenderDrawColor(ren, 150, 150, 150, 255);
            SDL_RenderFillRect(ren, &v_fill);

            // Mute [M]
            SDL_Rect mute_btn = { 132, ty + 10, 20, 20 };
            SDL_SetRenderDrawColor(ren, g_daw.tracks[i].mute ? 200 : 70, g_daw.tracks[i].mute ? 40 : 70, 40, 255);
            SDL_RenderFillRect(ren, &mute_btn);
            draw_text(ren, "M", 138, ty + 16, c_white);

            // Solo [S]
            SDL_Rect solo_btn = { 156, ty + 10, 20, 20 };
            SDL_SetRenderDrawColor(ren, g_daw.tracks[i].solo ? 210 : 70, g_daw.tracks[i].solo ? 180 : 70, 40, 255);
            SDL_RenderFillRect(ren, &solo_btn);
            draw_text(ren, "S", 162, ty + 16, c_white);

            SDL_SetRenderDrawColor(ren, 22, 22, 22, 255);
            SDL_RenderDrawLine(ren, 0, ty + TRACK_HEIGHT, TCP_WIDTH, ty + TRACK_HEIGHT);
        }

        // Add Track [+] below
        int add_y = TOPBAR_HEIGHT + (g_daw.num_tracks * TRACK_HEIGHT) + 8;
        if (add_y + 32 < win_h) {
            SDL_Rect add_box = { TCP_WIDTH / 2 - 16, add_y, 32, 26 };
            SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
            SDL_RenderFillRect(ren, &add_box);
            draw_text(ren, "+", TCP_WIDTH / 2 - 4, add_y + 9, c_white);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    if (g_daw.audio_device > 0) SDL_CloseAudioDevice(g_daw.audio_device);
    for (int i = 0; i < g_daw.num_items; i++) free(g_daw.items[i].pcm_data);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
