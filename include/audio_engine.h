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
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_TRACKS 32
#define MAX_ITEMS 1024
#define SAMPLE_RATE 48000
#define PEAK_SAMPLES_PER_BIN 16
#define DAW_PK_CACHE_MAGIC 0x504B4339U
#ifndef DAW_PEAK_POOL_DEFINED
#define DAW_PEAK_POOL_DEFINED
#define MAX_SHARED_PEAK_ENTRIES 256

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
    PeakBin *peaks;             // Shared visual peak array (zero duplication across splits/undo)
    int peak_count;
    FILE *raw_file_handle;      // Persistent handle for ultra-fast 64-bit reading
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

typedef struct {
    bool in_use;
    char raw_audio_path[512];
    PeakBin *peaks;
    int peak_count;
    double total_duration;
    bool has_video;
} SharedPeakPoolEntry;

static SharedPeakPoolEntry g_shared_peak_pool[MAX_SHARED_PEAK_ENTRIES];

// Cross-platform 64-bit file seek (fixes 32-bit long overflow past 2GB / ~44 mins of stereo float PCM on Windows)
static inline int daw_fseek64(FILE *fp, int64_t offset, int origin) {
    if (!fp) return -1;
    clearerr(fp);
#ifdef _WIN32
    return _fseeki64(fp, (int64_t)offset, origin);
#else
    return fseeko(fp, (off_t)offset, origin);
#endif
}

static inline FILE* daw_fopen_LockFreeRead(const char *path) {
    if (!path || !path[0]) return NULL;
    FILE *fp = fopen(path, "rb");
    if (fp) {
        setvbuf(fp, NULL, _IOFBF, 65536);
    }
    return fp;
}

static inline bool daw_is_pooled_peaks(const PeakBin *peaks) {
    if (!peaks) return false;
    for (int i = 0; i < MAX_SHARED_PEAK_ENTRIES; i++) {
        if (g_shared_peak_pool[i].in_use && g_shared_peak_pool[i].peaks == peaks) {
            return true;
        }
    }
    return false;
}

static inline void daw_free_peaks_if_unpooled(PeakBin *peaks) {
    if (!peaks) return;
    if (!daw_is_pooled_peaks(peaks)) {
        free(peaks);
    }
}

static inline PeakBin* daw_find_shared_peaks(const char *raw_path, int *out_peak_count,
                                             double *out_total_dur, bool *out_has_video) {
    if (!raw_path || !raw_path[0]) return NULL;
    for (int i = 0; i < MAX_SHARED_PEAK_ENTRIES; i++) {
        if (g_shared_peak_pool[i].in_use &&
            strcmp(g_shared_peak_pool[i].raw_audio_path, raw_path) == 0 &&
            g_shared_peak_pool[i].peaks != NULL) {
            if (out_peak_count) *out_peak_count = g_shared_peak_pool[i].peak_count;
            if (out_total_dur)  *out_total_dur  = g_shared_peak_pool[i].total_duration;
            if (out_has_video)  *out_has_video  = g_shared_peak_pool[i].has_video;
            return g_shared_peak_pool[i].peaks;
        }
    }
    return NULL;
}

static inline PeakBin* daw_register_shared_peaks(const char *raw_path, PeakBin *peaks,
                                                 int peak_count, double total_dur, bool has_video) {
    if (!raw_path || !raw_path[0] || !peaks) return peaks;
    for (int i = 0; i < MAX_SHARED_PEAK_ENTRIES; i++) {
        if (g_shared_peak_pool[i].in_use &&
            strcmp(g_shared_peak_pool[i].raw_audio_path, raw_path) == 0) {
            if (g_shared_peak_pool[i].peaks && g_shared_peak_pool[i].peaks != peaks) {
                free(peaks);
                return g_shared_peak_pool[i].peaks;
            }
            g_shared_peak_pool[i].peaks = peaks;
            g_shared_peak_pool[i].peak_count = peak_count;
            g_shared_peak_pool[i].total_duration = total_dur;
            g_shared_peak_pool[i].has_video = has_video;
            return peaks;
        }
    }
    for (int i = 0; i < MAX_SHARED_PEAK_ENTRIES; i++) {
        if (!g_shared_peak_pool[i].in_use) {
            g_shared_peak_pool[i].in_use = true;
            strncpy(g_shared_peak_pool[i].raw_audio_path, raw_path, sizeof(g_shared_peak_pool[i].raw_audio_path) - 1);
            g_shared_peak_pool[i].raw_audio_path[sizeof(g_shared_peak_pool[i].raw_audio_path) - 1] = '\0';
            g_shared_peak_pool[i].peaks = peaks;
            g_shared_peak_pool[i].peak_count = peak_count;
            g_shared_peak_pool[i].total_duration = total_dur;
            g_shared_peak_pool[i].has_video = has_video;
            return peaks;
        }
    }
    return peaks;
}

#endif // DAW_PEAK_POOL_DEFINED

// Simple string hash to generate distinct cache filenames
// Rebuilds high-resolution 32-sample REAPER-style peaks directly from an existing .raw stereo float cache file
static inline PeakBin* daw_rebuild_peaks_from_raw_file(const char *raw_path, const char *pk_cache_path,
                                                       int *out_peak_count, double *out_total_dur, bool has_video) {
    if (!raw_path || !raw_path[0]) return NULL;
    FILE *fp = daw_fopen_LockFreeRead(raw_path);
    if (!fp) return NULL;
    int alloc_bins = 65536;
    PeakBin *peaks = (PeakBin *)calloc((size_t)alloc_bins, sizeof(PeakBin));
    if (!peaks) { fclose(fp); return NULL; }

    float buf[4096 * 2];
    int cur_bin = 0;
    int samp_in_bin = 0;
    float cur_min = 0.0f, cur_max = 0.0f;
    int64_t total_frames = 0;
    size_t got = 0;

    while ((got = fread(buf, sizeof(float) * 2, 4096, fp)) > 0) {
        total_frames += (int64_t)got;
        for (size_t s = 0; s < got; s++) {
            float mono = (buf[s * 2] + buf[s * 2 + 1]) * 0.5f;
            if (samp_in_bin == 0) {
                cur_min = mono;
                cur_max = mono;
            } else {
                if (mono < cur_min) cur_min = mono;
                if (mono > cur_max) cur_max = mono;
            }
            samp_in_bin++;
            if (samp_in_bin >= PEAK_SAMPLES_PER_BIN) {
                if (cur_bin >= alloc_bins) {
                    int new_cap = alloc_bins * 2;
                    PeakBin *grown = (PeakBin *)realloc(peaks, (size_t)new_cap * sizeof(PeakBin));
                    if (!grown) { free(peaks); fclose(fp); return NULL; }
                    memset(grown + alloc_bins, 0, (size_t)(new_cap - alloc_bins) * sizeof(PeakBin));
                    peaks = grown;
                    alloc_bins = new_cap;
                }
                peaks[cur_bin].min_val = cur_min;
                peaks[cur_bin].max_val = cur_max;
                cur_bin++;
                samp_in_bin = 0;
            }
        }
    }
    if (total_frames <= 0) { free(peaks); fclose(fp); return NULL; }
    if (samp_in_bin > 0 && cur_bin < alloc_bins) {
        peaks[cur_bin].min_val = cur_min;
        peaks[cur_bin].max_val = cur_max;
        cur_bin++;
    }
    fclose(fp);

    double actual_dur = (double)total_frames / (double)SAMPLE_RATE;
    if (pk_cache_path && pk_cache_path[0]) {
        FILE *f_pk = fopen(pk_cache_path, "wb");
        if (f_pk) {
            uint32_t magic = DAW_PK_CACHE_MAGIC;
            int has_vid_i = has_video ? 1 : 0;
            fwrite(&magic, sizeof(uint32_t), 1, f_pk);
            fwrite(&actual_dur, sizeof(double), 1, f_pk);
            fwrite(&has_vid_i, sizeof(int), 1, f_pk);
            fwrite(&cur_bin, sizeof(int), 1, f_pk);
            if (cur_bin > 0) fwrite(peaks, sizeof(PeakBin), (size_t)cur_bin, f_pk);
            fclose(f_pk);
        }
    }
    peaks = daw_register_shared_peaks(raw_path, peaks, cur_bin, actual_dur, has_video);
    if (out_peak_count) *out_peak_count = cur_bin;
    if (out_total_dur)  *out_total_dur  = actual_dur;
    return peaks;
}

static inline unsigned int hash_path(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

// Demuxes and caches raw 48kHz stereo float PCM and shared peaks to .cache/ with minimal RAM overhead.
static inline int process_media_cache(const char *filepath, AudioItem *item) {
    if (!filepath || !item) return -1;

#ifdef _WIN32
    CreateDirectoryA(".cache", NULL);
#else
    mkdir(".cache", 0777);
#endif

    strncpy(item->file_path, filepath, sizeof(item->file_path) - 1);
    item->file_path[sizeof(item->file_path) - 1] = '\0';

    const char *name = strrchr(filepath, '\\');
    if (!name) name = strrchr(filepath, '/');
    snprintf(item->label, sizeof(item->label), "%s", name ? name + 1 : filepath);

    unsigned int h = hash_path(filepath);
    snprintf(item->raw_audio_path, sizeof(item->raw_audio_path), ".cache/%u.raw", h);

    // 1. Check in-RAM shared peak pool first (0 disk I/O, 0 RAM allocation)
    int pooled_cnt = 0;
    double pooled_dur = 0.0;
    bool pooled_vid = false;
    PeakBin *pooled_pk = daw_find_shared_peaks(item->raw_audio_path, &pooled_cnt, &pooled_dur, &pooled_vid);
    if (pooled_pk && pooled_dur > 0.0) {
        FILE *f_existing = daw_fopen_LockFreeRead(item->raw_audio_path);
        if (f_existing) {
            item->peaks = pooled_pk;
            item->peak_count = pooled_cnt;
            item->file_total_duration = pooled_dur;
            item->duration = pooled_dur;
            item->offset_in_file = 0.0;
            item->is_selected = false;
            item->has_video = pooled_vid;
            item->raw_file_handle = f_existing;
            return 0;
        }
    }

    char pk_cache_path[512];
    snprintf(pk_cache_path, sizeof(pk_cache_path), ".cache/%u.pk", h);
    FILE *chk_raw = daw_fopen_LockFreeRead(item->raw_audio_path);
    if (chk_raw) {
        fclose(chk_raw);
        bool has_vid_guess = false;
        const char *ext_c = strrchr(filepath, '.');
        if (ext_c && strstr(".mp4.MP4.mov.MOV.mkv.MKV.avi.AVI.webm.WEBM", ext_c) != NULL) {
            has_vid_guess = true;
        }
        int rb_cnt = 0;
        double rb_dur = 0.0;
        PeakBin *rb_pk = daw_rebuild_peaks_from_raw_file(item->raw_audio_path, pk_cache_path, &rb_cnt, &rb_dur, has_vid_guess);
        if (rb_pk && rb_dur > 0.0) {
            item->has_video = has_vid_guess;
            item->peaks = rb_pk;
            item->peak_count = rb_cnt;
            item->file_total_duration = rb_dur;
            item->duration = rb_dur;
            item->offset_in_file = 0.0;
            item->is_selected = false;
            item->raw_file_handle = daw_fopen_LockFreeRead(item->raw_audio_path);
            return 0;
        }
    }

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
    ctx->thread_count = 0;
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
    setvbuf(f_raw, NULL, _IOFBF, 65536);

    int estimated_bins = (int)((total_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN) + 1024;
    if (estimated_bins < 1024) estimated_bins = 1024;
    PeakBin *peak_cache = (PeakBin *)calloc((size_t)estimated_bins, sizeof(PeakBin));
    int current_bin = 0;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int pcm_cap_frames = 16384;
    float *temp_pcm = (float *)malloc((size_t)pcm_cap_frames * 2 * sizeof(float));
    if (!temp_pcm) {
        fclose(f_raw);
        if (peak_cache) free(peak_cache);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return -1;
    }
    float cur_min = 0.0f, cur_max = 0.0f;
    int samples_in_bin = 0;
    int64_t total_written_frames = 0;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == a_idx) {
            if (avcodec_send_packet(ctx, pkt) == 0) {
                while (avcodec_receive_frame(ctx, frame) == 0) {
                    int needed_out = (int)((double)frame->nb_samples * (double)SAMPLE_RATE / (double)(ctx->sample_rate > 0 ? ctx->sample_rate : SAMPLE_RATE)) + 1024;
                    if (needed_out < frame->nb_samples + 256) needed_out = frame->nb_samples + 256;
                    if (needed_out > pcm_cap_frames) {
                        int new_cap = needed_out * 2;
                        float *grown_pcm = (float *)realloc(temp_pcm, (size_t)new_cap * 2 * sizeof(float));
                        if (grown_pcm) {
                            temp_pcm = grown_pcm;
                            pcm_cap_frames = new_cap;
                        }
                    }
                    const uint8_t **in_data = (const uint8_t **)frame->extended_data;
                    int in_samples = frame->nb_samples;
                    for (;;) {
                        uint8_t *out_d[2] = { (uint8_t *)temp_pcm, NULL };
                        int converted = swr_convert(swr, out_d, pcm_cap_frames, in_data, in_samples);
                        in_data = NULL;
                        in_samples = 0;
                        if (converted <= 0) break;
                        fwrite(temp_pcm, sizeof(float) * 2, (size_t)converted, f_raw);
                        total_written_frames += converted;

                        for (int s = 0; s < converted; s++) {
                            float mono = (temp_pcm[s * 2] + temp_pcm[s * 2 + 1]) * 0.5f;
                            if (samples_in_bin == 0) {
                                cur_min = mono;
                                cur_max = mono;
                            } else {
                                if (mono < cur_min) cur_min = mono;
                                if (mono > cur_max) cur_max = mono;
                            }
                            samples_in_bin++;

                            if (samples_in_bin >= PEAK_SAMPLES_PER_BIN) {
                                if (current_bin >= estimated_bins && peak_cache) {
                                    int new_cap = estimated_bins * 2;
                                    PeakBin *grown = (PeakBin *)realloc(peak_cache, (size_t)new_cap * sizeof(PeakBin));
                                    if (grown) {
                                        memset(grown + estimated_bins, 0, (size_t)(new_cap - estimated_bins) * sizeof(PeakBin));
                                        peak_cache = grown;
                                        estimated_bins = new_cap;
                                    }
                                }
                                if (peak_cache && current_bin < estimated_bins) {
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

    // Flush decoder and resampler so 100% of audio frames and final peak bin are written
    avcodec_send_packet(ctx, NULL);
    while (avcodec_receive_frame(ctx, frame) == 0) {
        const uint8_t **in_data = (const uint8_t **)frame->extended_data;
        int in_samples = frame->nb_samples;
        for (;;) {
            uint8_t *out_d[2] = { (uint8_t *)temp_pcm, NULL };
            int converted = swr_convert(swr, out_d, pcm_cap_frames, in_data, in_samples);
            in_data = NULL;
            in_samples = 0;
            if (converted <= 0) break;
            fwrite(temp_pcm, sizeof(float) * 2, (size_t)converted, f_raw);
            total_written_frames += converted;
            for (int s = 0; s < converted; s++) {
                float mono = (temp_pcm[s * 2] + temp_pcm[s * 2 + 1]) * 0.5f;
                if (mono < cur_min) cur_min = mono;
                if (mono > cur_max) cur_max = mono;
                samples_in_bin++;
                if (samples_in_bin >= PEAK_SAMPLES_PER_BIN) {
                    if (current_bin >= estimated_bins && peak_cache) {
                        int new_cap = estimated_bins * 2;
                        PeakBin *grown = (PeakBin *)realloc(peak_cache, (size_t)new_cap * sizeof(PeakBin));
                        if (grown) {
                            memset(grown + estimated_bins, 0, (size_t)(new_cap - estimated_bins) * sizeof(PeakBin));
                            peak_cache = grown;
                            estimated_bins = new_cap;
                        }
                    }
                    if (peak_cache && current_bin < estimated_bins) {
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
    for (;;) {
        uint8_t *out_d[2] = { (uint8_t *)temp_pcm, NULL };
        int converted = swr_convert(swr, out_d, pcm_cap_frames, NULL, 0);
        if (converted <= 0) break;
        fwrite(temp_pcm, sizeof(float) * 2, (size_t)converted, f_raw);
        total_written_frames += converted;
        for (int s = 0; s < converted; s++) {
            float mono = (temp_pcm[s * 2] + temp_pcm[s * 2 + 1]) * 0.5f;
            if (mono < cur_min) cur_min = mono;
            if (mono > cur_max) cur_max = mono;
            samples_in_bin++;
            if (samples_in_bin >= PEAK_SAMPLES_PER_BIN) {
                if (current_bin >= estimated_bins && peak_cache) {
                    int new_cap = estimated_bins * 2;
                    PeakBin *grown = (PeakBin *)realloc(peak_cache, (size_t)new_cap * sizeof(PeakBin));
                    if (grown) {
                        memset(grown + estimated_bins, 0, (size_t)(new_cap - estimated_bins) * sizeof(PeakBin));
                        peak_cache = grown;
                        estimated_bins = new_cap;
                    }
                }
                if (peak_cache && current_bin < estimated_bins) {
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
    if (samples_in_bin > 0 && peak_cache && current_bin < estimated_bins) {
        peak_cache[current_bin].min_val = cur_min;
        peak_cache[current_bin].max_val = cur_max;
        current_bin++;
    }

    free(temp_pcm);
    fclose(f_raw);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    double actual_dur = (total_written_frames > 0) ? ((double)total_written_frames / SAMPLE_RATE) : total_sec;
    item->peaks = daw_register_shared_peaks(item->raw_audio_path, peak_cache, current_bin, actual_dur, item->has_video);
    item->peak_count = current_bin;
    item->file_total_duration = actual_dur;
    item->duration = actual_dur;
    item->offset_in_file = 0.0;
    item->is_selected = false;

    item->raw_file_handle = daw_fopen_LockFreeRead(item->raw_audio_path);
    return 0;
}

#endif // AUDIO_ENGINE_H
