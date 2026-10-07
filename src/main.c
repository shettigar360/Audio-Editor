#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <windows.h>
#include <commdlg.h>

// FFmpeg
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>

#define MAX_TRACKS 16
#define MAX_ITEMS 64
#define RULER_HEIGHT 34
#define TOPBAR_HEIGHT 44
#define TCP_WIDTH 190
#define TRACK_HEIGHT 84
#define BOTTOM_BAR_HEIGHT 22
#define SAMPLE_RATE 48000
#define PEAK_SAMPLES_PER_BIN 256

// 8x8 ASCII font
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
            if (glyph[row] & (1 << (7 - col))) SDL_RenderDrawPoint(r, x + col, y + row);
        }
    }
}

void draw_text(SDL_Renderer *r, const char *str, int x, int y, SDL_Color color) {
    int cur_x = x;
    while (*str) { draw_char(r, *str, cur_x, y, color); cur_x += 8; str++; }
}

// ----------------- MULTITRACK & PEAK ARCHITECTURE -----------------
typedef struct {
    float min_val;
    float max_val;
} PeakBin;

typedef struct {
    int track_idx;
    double start_time;      // Position on the multitrack timeline (seconds)
    double offset_in_file;  // In-point offset from raw audio buffer
    double duration;        // Visible/playable duration (seconds)
    char filename[128];
    float *pcm_data;        // Stereo 32-bit float interleaved samples
    int64_t total_samples;
    PeakBin *peaks;         // Peak cache (min/max pairs per 256 samples)
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
    Track tracks[MAX_TRACKS];
    int num_tracks;
    AudioItem items[MAX_ITEMS];
    int num_items;
    double playhead_seconds;
    bool is_playing;
    double scroll_offset_seconds;
    float pixels_per_second;
    SDL_AudioDeviceID audio_device;
} DawEngine;

static DawEngine g_daw;

// ----------------- FFMPEG DECODER + PEAK GENERATOR -----------------
int decode_and_generate_peaks(const char *filepath, AudioItem *item) {
    AVFormatContext *fmt_ctx = NULL;
    if (avformat_open_input(&fmt_ctx, filepath, NULL, NULL) < 0) return -1;
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) { avformat_close_input(&fmt_ctx); return -1; }

    int stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            stream_idx = i; break;
        }
    }
    if (stream_idx == -1) { avformat_close_input(&fmt_ctx); return -1; }

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

    int capacity = SAMPLE_RATE * 2 * 60;
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
    item->duration = (double)total_samples / SAMPLE_RATE;
    item->offset_in_file = 0.0;
    item->is_selected = false;

    // Build Peak File Cache (Like REAPER .reapeaks)
    int bin_count = (int)(total_samples / PEAK_SAMPLES_PER_BIN) + 1;
    item->peaks = (PeakBin *)malloc(bin_count * sizeof(PeakBin));
    item->peak_count = bin_count;

    for (int b = 0; b < bin_count; b++) {
        float min_v = 0.0f;
        float max_v = 0.0f;
        int64_t start_s = (int64_t)b * PEAK_SAMPLES_PER_BIN;
        for (int s = 0; s < PEAK_SAMPLES_PER_BIN; s++) {
            int64_t idx = start_s + s;
            if (idx >= total_samples) break;
            float mono = (buffer[idx * 2] + buffer[idx * 2 + 1]) * 0.5f;
            if (mono < min_v) min_v = mono;
            if (mono > max_v) max_v = mono;
        }
        item->peaks[b].min_val = min_v;
        item->peaks[b].max_val = max_v;
    }

    const char *slash = strrchr(filepath, '\\');
    if (!slash) slash = strrchr(filepath, '/');
    snprintf(item->filename, sizeof(item->filename), "%s", slash ? slash + 1 : filepath);

    return 0;
}

// ----------------- SPLIT ITEM AT PLAYHEAD ('S') -----------------
void split_selected_item_at_playhead() {
    double cur_t = g_daw.playhead_seconds;
    for (int i = 0; i < g_daw.num_items; i++) {
        AudioItem *it = &g_daw.items[i];
        if (it->is_selected) {
            double it_start = it->start_time;
            double it_end = it->start_time + it->duration;
            if (cur_t > it_start + 0.05 && cur_t < it_end - 0.05 && g_daw.num_items < MAX_ITEMS) {
                // Duplicate into right piece
                AudioItem *r_item = &g_daw.items[g_daw.num_items];
                memcpy(r_item, it, sizeof(AudioItem));

                double split_delta = cur_t - it_start;
                it->duration = split_delta; // Left piece trimmed

                r_item->start_time = cur_t;
                r_item->offset_in_file = it->offset_in_file + split_delta;
                r_item->duration = it_end - cur_t;
                r_item->is_selected = true;
                it->is_selected = false;

                g_daw.num_items++;
                break;
            }
        }
    }
}

// ----------------- REALTIME AUDIO MIXER CALLBACK -----------------
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
                    double rel_t = (cur_t - it_start) + it->offset_in_file;
                    int64_t sample_pos = (int64_t)(rel_t * SAMPLE_RATE);
                    if (sample_pos >= 0 && sample_pos < it->total_samples) {
                        out[f * 2]     += it->pcm_data[sample_pos * 2] * tr->volume;
                        out[f * 2 + 1] += it->pcm_data[sample_pos * 2 + 1] * tr->volume;
                    }
                }
            }
        }
    }

    engine->playhead_seconds += (double)frames_needed / SAMPLE_RATE;
}

// ----------------- NATIVE FILE IMPORT -----------------
void open_file_dialog(char *out_path, int max_len) {
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Audio Files (*.wav;*.mp3;*.flac;*.ogg)\0*.wav;*.mp3;*.flac;*.ogg\0All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) strncpy(out_path, szFile, max_len);
    else out_path[0] = '\0';
}

// ----------------- MAIN APP -----------------
int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return 1;

    SDL_Window *win = SDL_CreateWindow(
        "[unsaved project] - Multitrack DAW Editor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    memset(&g_daw, 0, sizeof(DawEngine));
    g_daw.num_tracks = 3;
    g_daw.pixels_per_second = 70.0f; // Default zoom
    g_daw.scroll_offset_seconds = 0.0;

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

    // Interaction states
    int dragging_item_idx = -1;
    bool dragging_trim_left = false;
    bool dragging_trim_right = false;
    double drag_start_mouse_time = 0.0;
    double item_orig_start = 0.0;
    double item_orig_dur = 0.0;
    double item_orig_offset = 0.0;

    bool dragging_scrollbar = false;
    bool dragging_zoom_handle = false;
    int scrollbar_drag_start_x = 0;
    double orig_scroll_time = 0.0;

    int dragging_vol_track = -1;
    bool scrubbing_timeline = false;
    char status_bar[128] = "Space: Play/Pause | S: Split Item | Drag/Trim clips | Drag Bottom Bar";

    bool running = true;
    SDL_Event e;

    while (running) {
        int win_w, win_h;
        SDL_GetWindowSize(win, &win_w, &win_h);
        int timeline_w = win_w - TCP_WIDTH;

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }
            // Drag-and-drop audio files
            else if (e.type == SDL_DROPFILE) {
                char *file = e.drop.file;
                int my = 0; SDL_GetMouseState(NULL, &my);
                int trk = (my - TOPBAR_HEIGHT) / TRACK_HEIGHT;
                if (trk < 0) trk = 0;
                if (trk >= g_daw.num_tracks) trk = g_daw.num_tracks - 1;

                if (g_daw.num_items < MAX_ITEMS) {
                    AudioItem *item = &g_daw.items[g_daw.num_items];
                    item->track_idx = trk;
                    item->start_time = g_daw.playhead_seconds;
                    if (decode_and_generate_peaks(file, item) == 0) {
                        g_daw.num_items++;
                        snprintf(status_bar, sizeof(status_bar), "Imported: %s (Peaks Generated)", item->filename);
                    }
                }
                SDL_free(file);
            }
            // Hotkeys
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_SPACE) {
                    g_daw.is_playing = !g_daw.is_playing;
                } else if (e.key.keysym.sym == SDLK_s) {
                    split_selected_item_at_playhead();
                    snprintf(status_bar, sizeof(status_bar), "Item split at playhead (S)");
                } else if (e.key.keysym.sym == SDLK_DELETE) {
                    // Delete selected item
                    for (int i = 0; i < g_daw.num_items; i++) {
                        if (g_daw.items[i].is_selected) {
                            for (int j = i; j < g_daw.num_items - 1; j++) g_daw.items[j] = g_daw.items[j + 1];
                            g_daw.num_items--;
                            snprintf(status_bar, sizeof(status_bar), "Deleted item");
                            break;
                        }
                    }
                }
            }
            // Mouse Down
            else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x;
                int my = e.button.y;

                // 1. Bottom Scroll & Zoom Drag Bar Click
                if (my >= win_h - BOTTOM_BAR_HEIGHT && mx >= TCP_WIDTH) {
                    float rel_bar_x = (float)(mx - TCP_WIDTH);
                    float total_w = (float)timeline_w;
                    float thumb_w = fmaxf(40.0f, total_w * (50.0f / g_daw.pixels_per_second));
                    float max_scroll_time = 300.0f; // 5 min project span
                    float thumb_x = (float)(g_daw.scroll_offset_seconds / max_scroll_time) * (total_w - thumb_w);

                    if (rel_bar_x >= thumb_x + thumb_w - 12 && rel_bar_x <= thumb_x + thumb_w + 4) {
                        dragging_zoom_handle = true;
                    } else if (rel_bar_x >= thumb_x && rel_bar_x <= thumb_x + thumb_w) {
                        dragging_scrollbar = true;
                        scrollbar_drag_start_x = mx;
                        orig_scroll_time = g_daw.scroll_offset_seconds;
                    } else {
                        // Click to jump scrollbar
                        g_daw.scroll_offset_seconds = (rel_bar_x / total_w) * max_scroll_time;
                    }
                    continue;
                }

                // 2. Topbar Actions
                if (my <= TOPBAR_HEIGHT) {
                    if (mx >= 430 && mx <= 545) {
                        char p[MAX_PATH]; open_file_dialog(p, sizeof(p));
                        if (strlen(p) > 0 && g_daw.num_items < MAX_ITEMS) {
                            AudioItem *it = &g_daw.items[g_daw.num_items];
                            it->track_idx = 0; it->start_time = g_daw.playhead_seconds;
                            if (decode_and_generate_peaks(p, it) == 0) g_daw.num_items++;
                        }
                    } else if (mx >= 555 && mx <= 655 && g_daw.num_tracks < MAX_TRACKS) {
                        g_daw.num_tracks++;
                    } else if (mx >= 665 && mx <= 725) {
                        g_daw.is_playing = !g_daw.is_playing;
                    }
                    continue;
                }

                // 3. Track Control Panel (TCP)
                if (mx < TCP_WIDTH && my > TOPBAR_HEIGHT) {
                    int t_idx = (my - TOPBAR_HEIGHT) / TRACK_HEIGHT;
                    if (t_idx >= 0 && t_idx < g_daw.num_tracks) {
                        int rel_y = (my - TOPBAR_HEIGHT) % TRACK_HEIGHT;
                        if (mx >= 20 && mx <= 38 && rel_y >= 10 && rel_y <= 28) g_daw.tracks[t_idx].armed = !g_daw.tracks[t_idx].armed;
                        else if (mx >= 44 && mx <= 124 && rel_y >= 12 && rel_y <= 28) {
                            dragging_vol_track = t_idx;
                            g_daw.tracks[t_idx].volume = (float)(mx - 44) / 80.0f;
                        } else if (mx >= 132 && mx <= 152 && rel_y >= 10 && rel_y <= 30) g_daw.tracks[t_idx].mute = !g_daw.tracks[t_idx].mute;
                        else if (mx >= 156 && mx <= 176 && rel_y >= 10 && rel_y <= 30) g_daw.tracks[t_idx].solo = !g_daw.tracks[t_idx].solo;
                    }
                    continue;
                }

                // 4. Multitrack Timeline Area: Clips Selection, Dragging, Trimming
                if (mx >= TCP_WIDTH && my > TOPBAR_HEIGHT + RULER_HEIGHT && my < win_h - BOTTOM_BAR_HEIGHT) {
                    double click_time = g_daw.scroll_offset_seconds + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second;
                    int click_track = (my - (TOPBAR_HEIGHT + RULER_HEIGHT)) / TRACK_HEIGHT;

                    // Deselect previous
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

                            int ix = TCP_WIDTH + (int)((it->start_time - g_daw.scroll_offset_seconds) * g_daw.pixels_per_second);
                            int iw = (int)(it->duration * g_daw.pixels_per_second);

                            if (mx <= ix + 8) dragging_trim_left = true;
                            else if (mx >= ix + iw - 8) dragging_trim_right = true;

                            hit = true;
                            break;
                        }
                    }
                    if (!hit) {
                        // Clicked empty lane -> Move playhead
                        scrubbing_timeline = true;
                        g_daw.playhead_seconds = fmax(0.0, click_time);
                    }
                    continue;
                }

                // 5. Ruler Scrubbing
                if (mx >= TCP_WIDTH && my >= TOPBAR_HEIGHT && my <= TOPBAR_HEIGHT + RULER_HEIGHT) {
                    scrubbing_timeline = true;
                    g_daw.playhead_seconds = fmax(0.0, g_daw.scroll_offset_seconds + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second);
                }
            }
            // Mouse Up
            else if (e.type == SDL_MOUSEBUTTONUP) {
                dragging_vol_track = -1;
                scrubbing_timeline = false;
                dragging_item_idx = -1;
                dragging_trim_left = false;
                dragging_trim_right = false;
                dragging_scrollbar = false;
                dragging_zoom_handle = false;
            }
            // Mouse Motion
            else if (e.type == SDL_MOUSEMOTION) {
                int mx = e.motion.x;
                int my = e.motion.y;

                // Scrollbar dragging
                if (dragging_scrollbar) {
                    float delta_x = (float)(mx - scrollbar_drag_start_x);
                    float total_w = (float)timeline_w;
                    float max_scroll_time = 300.0f;
                    g_daw.scroll_offset_seconds = fmax(0.0, orig_scroll_time + (delta_x / total_w) * max_scroll_time);
                }
                // Zoom handle dragging
                if (dragging_zoom_handle) {
                    g_daw.pixels_per_second = fmaxf(15.0f, fminf(300.0f, g_daw.pixels_per_second + (float)e.motion.xrel * 0.8f));
                }
                // Item editing (Move, Left Trim, Right Trim)
                if (dragging_item_idx != -1) {
                    AudioItem *it = &g_daw.items[dragging_item_idx];
                    double cur_mouse_time = g_daw.scroll_offset_seconds + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second;
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
                        // Move Item along timeline and across tracks
                        it->start_time = fmax(0.0, item_orig_start + delta_t);
                        int target_t = (my - (TOPBAR_HEIGHT + RULER_HEIGHT)) / TRACK_HEIGHT;
                        if (target_t >= 0 && target_t < g_daw.num_tracks) it->track_idx = target_t;
                    }
                }
                // Playhead scrub
                if (scrubbing_timeline && mx >= TCP_WIDTH) {
                    g_daw.playhead_seconds = fmax(0.0, g_daw.scroll_offset_seconds + (double)(mx - TCP_WIDTH) / g_daw.pixels_per_second);
                }
            }
        }

        // ================= RENDERING =================
        SDL_SetRenderDrawColor(ren, 43, 43, 43, 255);
        SDL_RenderClear(ren);

        SDL_Color c_white = { 220, 220, 220, 255 };
        SDL_Color c_muted = { 130, 130, 130, 255 };
        SDL_Color c_teal  = { 0, 210, 170, 255 };

        // 1. Top Bar
        SDL_Rect bar = { 0, 0, win_w, TOPBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &bar);
        draw_text(ren, "File  Edit  View  Track  Actions", 14, 8, c_white);

        SDL_Rect btn_imp = { 430, 6, 115, 22 };
        SDL_SetRenderDrawColor(ren, 52, 52, 52, 255);
        SDL_RenderFillRect(ren, &btn_imp);
        draw_text(ren, "Import Media", 437, 13, c_white);

        SDL_Rect btn_add = { 555, 6, 100, 22 };
        SDL_SetRenderDrawColor(ren, 52, 52, 52, 255);
        SDL_RenderFillRect(ren, &btn_add);
        draw_text(ren, "+ Track", 570, 13, c_teal);

        SDL_Rect btn_play = { 665, 6, 65, 22 };
        SDL_SetRenderDrawColor(ren, g_daw.is_playing ? 160 : 52, g_daw.is_playing ? 60 : 52, 52, 255);
        SDL_RenderFillRect(ren, &btn_play);
        draw_text(ren, g_daw.is_playing ? "Stop" : "Play", 680, 13, c_white);

        draw_text(ren, status_bar, 745, 13, c_teal);

        // 2. Timeline Grid
        int sec_step = (g_daw.pixels_per_second < 40) ? 2 : 1;
        for (int s = 0; s < 300; s += sec_step) {
            int gx = TCP_WIDTH + (int)((s - g_daw.scroll_offset_seconds) * g_daw.pixels_per_second);
            if (gx < TCP_WIDTH || gx > win_w) continue;

            SDL_SetRenderDrawColor(ren, 48, 48, 48, 255);
            SDL_RenderDrawLine(ren, gx, TOPBAR_HEIGHT + RULER_HEIGHT, gx, win_h - BOTTOM_BAR_HEIGHT);
        }

        // Horizontal Track Separators
        for (int i = 0; i <= g_daw.num_tracks; i++) {
            int ty = TOPBAR_HEIGHT + RULER_HEIGHT + (i * TRACK_HEIGHT);
            SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
            SDL_RenderDrawLine(ren, TCP_WIDTH, ty, win_w, ty);
        }

        // 3. Render Clips with Real Peak Waveforms
        for (int i = 0; i < g_daw.num_items; i++) {
            AudioItem *it = &g_daw.items[i];
            int ix = TCP_WIDTH + (int)((it->start_time - g_daw.scroll_offset_seconds) * g_daw.pixels_per_second);
            int iy = TOPBAR_HEIGHT + RULER_HEIGHT + (it->track_idx * TRACK_HEIGHT) + 4;
            int iw = (int)(it->duration * g_daw.pixels_per_second);
            int ih = TRACK_HEIGHT - 8;

            if (ix + iw < TCP_WIDTH || ix > win_w) continue; // Offscreen culling

            // Clip Body
            SDL_Rect clip_rect = { ix, iy, iw, ih };
            SDL_SetRenderDrawColor(ren, it->is_selected ? 42 : 32, it->is_selected ? 70 : 50, it->is_selected ? 95 : 70, 255);
            SDL_RenderFillRect(ren, &clip_rect);

            // Clip Border
            SDL_SetRenderDrawColor(ren, it->is_selected ? 0 : 70, it->is_selected ? 220 : 130, it->is_selected ? 200 : 150, 255);
            SDL_RenderDrawRect(ren, &clip_rect);

            // Real Peak Waveform rendering
            int mid_y = iy + ih / 2;
            SDL_SetRenderDrawColor(ren, 85, 200, 160, 255);

            for (int col = 0; col < iw; col++) {
                int px = ix + col;
                if (px < TCP_WIDTH || px > win_w) continue;

                double col_time = it->offset_in_file + ((double)col / g_daw.pixels_per_second);
                int bin_idx = (int)((col_time * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN);

                if (bin_idx >= 0 && bin_idx < it->peak_count) {
                    float min_v = it->peaks[bin_idx].min_val;
                    float max_v = it->peaks[bin_idx].max_val;
                    int y1 = mid_y - (int)(max_v * (ih / 2.0f - 2));
                    int y2 = mid_y - (int)(min_v * (ih / 2.0f - 2));
                    SDL_RenderDrawLine(ren, px, y1, px, y2);
                }
            }

            draw_text(ren, it->filename, ix + 6, iy + 6, c_white);
        }

        // 4. Timeline Ruler
        SDL_Rect ruler = { TCP_WIDTH, TOPBAR_HEIGHT, timeline_w, RULER_HEIGHT };
        SDL_SetRenderDrawColor(ren, 28, 28, 28, 255);
        SDL_RenderFillRect(ren, &ruler);

        for (int s = 0; s < 300; s += 2) {
            int rx = TCP_WIDTH + (int)((s - g_daw.scroll_offset_seconds) * g_daw.pixels_per_second);
            if (rx < TCP_WIDTH || rx > win_w) continue;

            char bar_txt[16], time_txt[16];
            snprintf(bar_txt, sizeof(bar_txt), "%d.1", s + 1);
            snprintf(time_txt, sizeof(time_txt), "0:%02d.00", s);

            draw_text(ren, bar_txt, rx + 4, TOPBAR_HEIGHT + 4, c_white);
            draw_text(ren, time_txt, rx + 4, TOPBAR_HEIGHT + 18, c_muted);

            SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
            SDL_RenderDrawLine(ren, rx, TOPBAR_HEIGHT, rx, TOPBAR_HEIGHT + RULER_HEIGHT);
        }

        // 5. Playhead Line & Cursor
        int cur_px = TCP_WIDTH + (int)((g_daw.playhead_seconds - g_daw.scroll_offset_seconds) * g_daw.pixels_per_second);
        if (cur_px >= TCP_WIDTH && cur_px <= win_w) {
            SDL_SetRenderDrawColor(ren, 0, 210, 170, 255);
            SDL_RenderDrawLine(ren, cur_px, TOPBAR_HEIGHT, cur_px, win_h - BOTTOM_BAR_HEIGHT);
            for (int t = 0; t < 8; t++) {
                SDL_RenderDrawLine(ren, cur_px - t, TOPBAR_HEIGHT + t, cur_px + t, TOPBAR_HEIGHT + t);
            }
        }

        // 6. Track Control Panels (Left TCP)
        SDL_Rect tcp_bg = { 0, TOPBAR_HEIGHT, TCP_WIDTH, win_h - TOPBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 48, 48, 48, 255);
        SDL_RenderFillRect(ren, &tcp_bg);

        for (int i = 0; i < g_daw.num_tracks; i++) {
            int ty = TOPBAR_HEIGHT + RULER_HEIGHT + (i * TRACK_HEIGHT);
            SDL_Rect tp = { 2, ty + 2, TCP_WIDTH - 4, TRACK_HEIGHT - 4 };
            SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
            SDL_RenderFillRect(ren, &tp);

            char num_str[8]; snprintf(num_str, sizeof(num_str), "%d", g_daw.tracks[i].id);
            draw_text(ren, num_str, 6, ty + 12, c_white);

            SDL_Rect arm = { 20, ty + 10, 18, 18 };
            SDL_SetRenderDrawColor(ren, g_daw.tracks[i].armed ? 210 : 80, 40, 40, 255);
            SDL_RenderFillRect(ren, &arm);

            SDL_Rect v_bg = { 44, ty + 13, 80, 12 };
            SDL_SetRenderDrawColor(ren, 25, 25, 25, 255);
            SDL_RenderFillRect(ren, &v_bg);
            SDL_Rect v_fill = { 44, ty + 13, (int)(80.0f * g_daw.tracks[i].volume), 12 };
            SDL_SetRenderDrawColor(ren, 150, 150, 150, 255);
            SDL_RenderFillRect(ren, &v_fill);

            SDL_Rect mute_btn = { 132, ty + 10, 20, 20 };
            SDL_SetRenderDrawColor(ren, g_daw.tracks[i].mute ? 200 : 70, g_daw.tracks[i].mute ? 40 : 70, 40, 255);
            SDL_RenderFillRect(ren, &mute_btn);
            draw_text(ren, "M", 138, ty + 16, c_white);

            SDL_Rect solo_btn = { 156, ty + 10, 20, 20 };
            SDL_SetRenderDrawColor(ren, g_daw.tracks[i].solo ? 210 : 70, g_daw.tracks[i].solo ? 180 : 70, 40, 255);
            SDL_RenderFillRect(ren, &solo_btn);
            draw_text(ren, "S", 162, ty + 16, c_white);

            SDL_SetRenderDrawColor(ren, 22, 22, 22, 255);
            SDL_RenderDrawLine(ren, 0, ty + TRACK_HEIGHT, TCP_WIDTH, ty + TRACK_HEIGHT);
        }

        // 7. Bottom Horizontal Scroll & Zoom Drag Bar (REAPER style)
        SDL_Rect b_bar = { TCP_WIDTH, win_h - BOTTOM_BAR_HEIGHT, timeline_w, BOTTOM_BAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &b_bar);

        float total_w = (float)timeline_w;
        float thumb_w = fmaxf(40.0f, total_w * (50.0f / g_daw.pixels_per_second));
        float max_scroll_time = 300.0f;
        float thumb_x = TCP_WIDTH + (float)(g_daw.scroll_offset_seconds / max_scroll_time) * (total_w - thumb_w);

        SDL_Rect thumb_rect = { (int)thumb_x, win_h - BOTTOM_BAR_HEIGHT + 2, (int)thumb_w, BOTTOM_BAR_HEIGHT - 4 };
        SDL_SetRenderDrawColor(ren, 75, 75, 75, 255);
        SDL_RenderFillRect(ren, &thumb_rect);

        // Zoom handle indicator on right edge of thumb
        SDL_Rect handle_rect = { (int)(thumb_x + thumb_w - 6), win_h - BOTTOM_BAR_HEIGHT + 2, 6, BOTTOM_BAR_HEIGHT - 4 };
        SDL_SetRenderDrawColor(ren, 0, 200, 160, 255);
        SDL_RenderFillRect(ren, &handle_rect);

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
