#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <SDL2/SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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
    char raw_audio_path[512];
    char label[128];
    PeakBin *peaks;
    int peak_count;
    FILE *raw_file_handle;
    bool is_selected;
} AudioItem;

typedef struct {
    int id;
    bool armed;
    bool mute;
    bool solo;
    float volume;
} Track;

static inline unsigned int hash_path(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

static inline int process_media_cache(const char *filepath, AudioItem *item) {
#ifdef _WIN32
    CreateDirectoryA(".cache", NULL);
#else
    mkdir(".cache", 0777);
#endif

    unsigned int h = hash_path(filepath);
    snprintf(item->raw_audio_path, sizeof(item->raw_audio_path), ".cache/%u.raw", h);

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

    FILE *f_raw = fopen(item->raw_audio_path, "wb");
    if (!f_raw) {
        swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return -1;
    }

    double total_sec = (fmt->duration != AV_NOPTS_VALUE) ? ((double)fmt->duration / AV_TIME_BASE) : 10.0;
    int estimated_bins = (int)((total_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN) + 2000;
    PeakBin *peak_cache = (PeakBin *)calloc(estimated_bins, sizeof(PeakBin));
    int current_bin = 0;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    float temp_pcm[2048 * 2];
    float cur_min = 0.0f, cur_max = 0.0f;
    int samples_in_bin = 0;
    int64_t total_written_frames = 0;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == a_idx) {
            if (avcodec_send_packet(ctx, pkt) == 0) {
                while (avcodec_receive_frame(ctx, frame) == 0) {
                    uint8_t *out_d[2] = { (uint8_t *)temp_pcm, NULL };
                    int converted = swr_convert(swr, out_d, 2048, (const uint8_t **)frame->extended_data, frame->nb_samples);
                    if (converted > 0) {
                        fwrite(temp_pcm, sizeof(float) * 2, converted, f_raw);
                        total_written_frames += converted;

                        for (int s = 0; s < converted; s++) {
                            float mono = (temp_pcm[s * 2] + temp_pcm[s * 2 + 1]) * 0.5f;
                            if (mono < cur_min) cur_min = mono;
                            if (mono > cur_max) cur_max = mono;
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
        }
        av_packet_unref(pkt);
    }

    fclose(f_raw);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    item->peaks = peak_cache;
    item->peak_count = current_bin;
    double actual_dur = (double)total_written_frames / SAMPLE_RATE;
    item->file_total_duration = actual_dur;
    item->duration = actual_dur;
    item->offset_in_file = 0.0;
    item->is_selected = false;
    strncpy(item->file_path, filepath, sizeof(item->file_path) - 1);

    const char *name = strrchr(filepath, '\\');
    if (!name) name = strrchr(filepath, '/');
    snprintf(item->label, sizeof(item->label), "%s", name ? name + 1 : filepath);

    item->raw_file_handle = fopen(item->raw_audio_path, "rb");
    return 0;
}

#endif // AUDIO_ENGINE_H
