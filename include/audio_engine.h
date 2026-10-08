#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <SDL2/SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRACKS 32
#define MAX_ITEMS 128
#define SAMPLE_RATE 48000
#define PEAK_SAMPLES_PER_BIN 256

typedef struct {
    float min_val;
    float max_val;
} PeakBin;

typedef struct {
    int track_idx;
    double start_time;
    double offset_in_file;
    double duration;
    double file_total_duration;
    char file_path[512];        // Reference path only
    char label[128];
    PeakBin *peaks;             // Small peak cache (< 500 KB)
    int peak_count;
    bool is_selected;

    // Streaming playback state (Zero RAM: only keeps current packet)
    AVFormatContext *play_fmt;
    AVCodecContext *play_ctx;
    SwrContext *play_swr;
    int audio_stream_idx;
    double play_current_sec;
} AudioItem;

typedef struct {
    int id;
    bool armed;
    bool mute;
    bool solo;
    float volume;
} Track;

// Opens an on-demand audio stream for low-latency playback
static inline void init_item_audio_stream(AudioItem *it) {
    if (it->play_fmt) return;

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, it->file_path, NULL, NULL) < 0) return;
    if (avformat_find_stream_info(fmt, NULL) < 0) { avformat_close_input(&fmt); return; }

    int a_idx = -1;
    for (unsigned int i = 0; i < fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            a_idx = i;
            break;
        }
    }
    if (a_idx == -1) { avformat_close_input(&fmt); return; }

    AVCodecParameters *cpar = fmt->streams[a_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(cpar->codec_id);
    if (!codec) { avformat_close_input(&fmt); return; }

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, cpar);
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return;
    }

    SwrContext *swr = swr_alloc();
    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_FLT, SAMPLE_RATE,
                        &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, NULL);
    swr_init(swr);

    it->play_fmt = fmt;
    it->play_ctx = ctx;
    it->play_swr = swr;
    it->audio_stream_idx = a_idx;
    it->play_current_sec = 0.0;
}

static inline void close_item_audio_stream(AudioItem *it) {
    if (it->play_swr) { swr_free(&it->play_swr); it->play_swr = NULL; }
    if (it->play_ctx) { avcodec_free_context(&it->play_ctx); it->play_ctx = NULL; }
    if (it->play_fmt) { avformat_close_input(&it->play_fmt); it->play_fmt = NULL; }
    it->audio_stream_idx = -1;
}

// Extract peaks cleanly on load without saving PCM buffer
static inline int load_media_item_peaks_only(const char *filepath, AudioItem *item) {
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) return -1;
    if (avformat_find_stream_info(fmt, NULL) < 0) { avformat_close_input(&fmt); return -1; }

    int a_idx = -1;
    for (unsigned int i = 0; i < fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            a_idx = i;
            break;
        }
    }
    if (a_idx == -1) { avformat_close_input(&fmt); return -1; }

    AVCodecParameters *cpar = fmt->streams[a_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(cpar->codec_id);
    if (!codec) { avformat_close_input(&fmt); return -1; }
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, cpar);
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return -1;
    }

    SwrContext *swr = swr_alloc();
    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_MONO;
    swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_FLT, SAMPLE_RATE,
                        &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, NULL);
    swr_init(swr);

    double total_sec = (fmt->duration != AV_NOPTS_VALUE) ? ((double)fmt->duration / AV_TIME_BASE) : 10.0;
    int estimated_bins = (int)((total_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN) + 2000;
    PeakBin *peak_cache = (PeakBin *)calloc(estimated_bins, sizeof(PeakBin));
    int current_bin = 0;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    float temp_pcm[2048];
    float cur_min = 0.0f, cur_max = 0.0f;
    int samples_in_bin = 0;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == a_idx) {
            if (avcodec_send_packet(ctx, pkt) == 0) {
                while (avcodec_receive_frame(ctx, frame) == 0) {
                    uint8_t *out_d[1] = { (uint8_t *)temp_pcm };
                    int converted = swr_convert(swr, out_d, 2048, (const uint8_t **)frame->extended_data, frame->nb_samples);
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
    avformat_close_input(&fmt);

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

    init_item_audio_stream(item);
    return 0;
}

#endif // AUDIO_ENGINE_H
