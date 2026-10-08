#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <SDL2/SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <stdbool.h>

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
    char file_path[512];
    char label[128];
    PeakBin *peaks;
    int peak_count;
    float *audio_samples;
    int64_t total_sample_pairs;
    bool is_selected;
} AudioItem;

typedef struct {
    int id;
    bool armed;
    bool mute;
    bool solo;
    float volume;
} Track;

static inline int load_media_item(const char *filepath, AudioItem *item) {
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
    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_FLT, SAMPLE_RATE,
                        &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, NULL);
    swr_init(swr);

    int64_t capacity = (int64_t)SAMPLE_RATE * 2 * 60;
    float *pcm_buffer = (float *)malloc(capacity * sizeof(float));
    int64_t total_frames = 0;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == a_idx) {
            if (avcodec_send_packet(ctx, pkt) == 0) {
                while (avcodec_receive_frame(ctx, frame) == 0) {
                    int dst_samples = av_rescale_rnd(swr_get_delay(swr, ctx->sample_rate) + frame->nb_samples,
                                                     SAMPLE_RATE, ctx->sample_rate, AV_ROUND_UP);

                    if ((total_frames + dst_samples) * 2 > capacity) {
                        capacity = (total_frames + dst_samples) * 4;
                        pcm_buffer = (float *)realloc(pcm_buffer, capacity * sizeof(float));
                    }

                    uint8_t *out_d[2] = { (uint8_t *)(pcm_buffer + total_frames * 2), NULL };
                    int conv = swr_convert(swr, out_d, dst_samples,
                                           (const uint8_t **)frame->extended_data, frame->nb_samples);
                    if (conv > 0) total_frames += conv;
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

    item->audio_samples = pcm_buffer;
    item->total_sample_pairs = total_frames;
    double dur = (double)total_frames / SAMPLE_RATE;
    item->file_total_duration = dur;
    item->duration = dur;
    item->offset_in_file = 0.0;
    item->is_selected = false;
    strncpy(item->file_path, filepath, sizeof(item->file_path) - 1);

    int bin_count = (int)(total_frames / PEAK_SAMPLES_PER_BIN) + 1;
    item->peaks = (PeakBin *)malloc(bin_count * sizeof(PeakBin));
    item->peak_count = bin_count;

    for (int b = 0; b < bin_count; b++) {
        float mn = 0.0f, mx = 0.0f;
        int64_t start_f = (int64_t)b * PEAK_SAMPLES_PER_BIN;
        for (int s = 0; s < PEAK_SAMPLES_PER_BIN; s++) {
            int64_t idx = start_f + s;
            if (idx >= total_frames) break;
            float mono = (pcm_buffer[idx * 2] + pcm_buffer[idx * 2 + 1]) * 0.5f;
            if (mono < mn) mn = mono;
            if (mono > mx) mx = mono;
        }
        item->peaks[b].min_val = mn;
        item->peaks[b].max_val = mx;
    }

    const char *name = strrchr(filepath, '\\');
    if (!name) name = strrchr(filepath, '/');
    snprintf(item->label, sizeof(item->label), "%s", name ? name + 1 : filepath);
    return 0;
}

#endif // AUDIO_ENGINE_H
