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
    char file_path[512];        // Original media path reference
    char raw_audio_path[512];   // Cached demuxed raw PCM in temp folder
    char label[128];
    PeakBin *peaks;             // Small visual peak array (< 400 KB)
    int peak_count;
    FILE *raw_file_handle;      // Persistent handle for ultra-fast reading
    bool is_selected;
    bool has_video;             // True if media container has a video stream
} AudioItem;

typedef struct {
    int id;
    bool armed;
    bool mute;
    bool solo;
    float volume;
} Track;

// Simple string hash to generate distinct cache filenames
static inline unsigned int hash_path(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

// Demuxes and caches raw 48kHz stereo float PCM and peaks to .cache/ with 0 RAM overhead.
// Also supports video-only containers without an audio stream so video clips still appear on the timeline.
static inline int process_media_cache(const char *filepath, AudioItem *item) {
    if (!filepath || !item) return -1;

#ifdef _WIN32
    CreateDirectoryA(".cache", NULL);
#else
    mkdir(".cache", 0777);
#endif

    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, filepath, NULL, NULL) < 0) return -1;
    if (avformat_find_stream_info(fmt, NULL) < 0) {
        avformat_close_input(&fmt);
        return -1;
    }

    int a_idx = -1;
    int v_idx = -1;
    for (unsigned int i = 0; i < fmt->nb_streams; i++) {
        enum AVMediaType ctype = fmt->streams[i]->codecpar->codec_type;
        if (ctype == AVMEDIA_TYPE_AUDIO && a_idx == -1) {
            a_idx = (int)i;
        } else if (ctype == AVMEDIA_TYPE_VIDEO && v_idx == -1) {
            if (!(fmt->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
                v_idx = (int)i;
            }
        }
    }
    // Fallback if only an attached-pic video stream was present
    if (v_idx == -1) {
        for (unsigned int i = 0; i < fmt->nb_streams; i++) {
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                v_idx = (int)i;
                break;
            }
        }
    }

    if (a_idx == -1 && v_idx == -1) {
        avformat_close_input(&fmt);
        return -1;
    }

    item->has_video = (v_idx != -1);

    double total_sec = 0.0;
    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) {
        total_sec = (double)fmt->duration / AV_TIME_BASE;
    } else if (v_idx != -1 && fmt->streams[v_idx]->duration != AV_NOPTS_VALUE && fmt->streams[v_idx]->duration > 0) {
        total_sec = (double)fmt->streams[v_idx]->duration * av_q2d(fmt->streams[v_idx]->time_base);
    } else if (a_idx != -1 && fmt->streams[a_idx]->duration != AV_NOPTS_VALUE && fmt->streams[a_idx]->duration > 0) {
        total_sec = (double)fmt->streams[a_idx]->duration * av_q2d(fmt->streams[a_idx]->time_base);
    }
    if (total_sec <= 0.0) total_sec = 10.0;

    strncpy(item->file_path, filepath, sizeof(item->file_path) - 1);
    item->file_path[sizeof(item->file_path) - 1] = '\0';

    const char *name = strrchr(filepath, '\\');
    if (!name) name = strrchr(filepath, '/');
    snprintf(item->label, sizeof(item->label), "%s", name ? name + 1 : filepath);

    // Handle video-only files (no audio stream) gracefully
    if (a_idx == -1) {
        avformat_close_input(&fmt);
        item->raw_audio_path[0] = '\0';
        item->raw_file_handle = NULL;
        item->peaks = NULL;
        item->peak_count = 0;
        item->file_total_duration = total_sec;
        item->duration = total_sec;
        item->offset_in_file = 0.0;
        item->is_selected = false;
        return 0;
    }

    unsigned int h = hash_path(filepath);
    snprintf(item->raw_audio_path, sizeof(item->raw_audio_path), ".cache/%u.raw", h);

    AVCodecParameters *cpar = fmt->streams[a_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(cpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        if (item->has_video) {
            item->raw_audio_path[0] = '\0';
            item->raw_file_handle = NULL;
            item->peaks = NULL;
            item->peak_count = 0;
            item->file_total_duration = total_sec;
            item->duration = total_sec;
            item->offset_in_file = 0.0;
            item->is_selected = false;
            return 0;
        }
        return -1;
    }

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, cpar);
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        if (item->has_video) {
            item->raw_audio_path[0] = '\0';
            item->raw_file_handle = NULL;
            item->peaks = NULL;
            item->peak_count = 0;
            item->file_total_duration = total_sec;
            item->duration = total_sec;
            item->offset_in_file = 0.0;
            item->is_selected = false;
            return 0;
        }
        return -1;
    }

    if (ctx->ch_layout.nb_channels <= 0) {
        int ch = (cpar->ch_layout.nb_channels > 0) ? cpar->ch_layout.nb_channels : 2;
        av_channel_layout_default(&ctx->ch_layout, ch);
    }

    SwrContext *swr = NULL;
    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_FLT, SAMPLE_RATE,
                            &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, NULL) < 0 ||
        swr_init(swr) < 0) {
        if (swr) swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        if (item->has_video) {
            item->raw_audio_path[0] = '\0';
            item->raw_file_handle = NULL;
            item->peaks = NULL;
            item->peak_count = 0;
            item->file_total_duration = total_sec;
            item->duration = total_sec;
            item->offset_in_file = 0.0;
            item->is_selected = false;
            return 0;
        }
        return -1;
    }

    FILE *f_raw = fopen(item->raw_audio_path, "wb");
    if (!f_raw) {
        swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return -1;
    }

    int estimated_bins = (int)((total_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN) + 2000;
    PeakBin *peak_cache = (PeakBin *)calloc(estimated_bins, sizeof(PeakBin));
    int current_bin = 0;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    float temp_pcm[2048 * 2]; // Stereo float chunk
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
    double actual_dur = (total_written_frames > 0) ? ((double)total_written_frames / SAMPLE_RATE) : total_sec;
    item->file_total_duration = actual_dur;
    item->duration = actual_dur;
    item->offset_in_file = 0.0;
    item->is_selected = false;

    item->raw_file_handle = fopen(item->raw_audio_path, "rb");
    return 0;
}

#endif // AUDIO_ENGINE_H
