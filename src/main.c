#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include <SDL2/SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <stdbool.h>

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
    double current_pts;
} SimpleVideoPlayer;

static inline void close_video(SimpleVideoPlayer *v) {
    if (v->texture) { SDL_DestroyTexture(v->texture); v->texture = NULL; }
    if (v->sws_ctx) { sws_freeContext(v->sws_ctx); v->sws_ctx = NULL; }
    if (v->frame_rgb) { av_frame_free(&v->frame_rgb); }
    if (v->frame_yuv) { av_frame_free(&v->frame_yuv); }
    if (v->rgb_buffer) { av_free(v->rgb_buffer); v->rgb_buffer = NULL; }
    if (v->codec_ctx) { avcodec_free_context(&v->codec_ctx); }
    if (v->fmt_ctx) { avformat_close_input(&v->fmt_ctx); }
    v->video_stream_idx = -1;
}

static inline int open_video_by_path(SimpleVideoPlayer *v, const char *filepath, SDL_Renderer *ren) {
    close_video(v);

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
    if (!codec) { avformat_close_input(&fmt); return -1; }

    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, cpar);
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return -1;
    }

    enum AVPixelFormat src_pix_fmt = (ctx->pix_fmt == AV_PIX_FMT_NONE) ? AV_PIX_FMT_YUV420P : ctx->pix_fmt;
    v->fmt_ctx = fmt;
    v->codec_ctx = ctx;
    v->video_stream_idx = v_idx;
    v->width = ctx->width;
    v->height = ctx->height;
    v->current_pts = 0.0;

    v->frame_yuv = av_frame_alloc();
    v->frame_rgb = av_frame_alloc();

    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, ctx->width, ctx->height, 1);
    v->rgb_buffer = (uint8_t *)av_malloc(num_bytes * sizeof(uint8_t));
    av_image_fill_arrays(v->frame_rgb->data, v->frame_rgb->linesize,
                         v->rgb_buffer, AV_PIX_FMT_RGB24, ctx->width, ctx->height, 1);

    v->sws_ctx = sws_getContext(ctx->width, ctx->height, src_pix_fmt,
                                ctx->width, ctx->height, AV_PIX_FMT_RGB24,
                                SWS_BILINEAR, NULL, NULL, NULL);

    v->texture = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, ctx->width, ctx->height);
    strncpy(v->file_path, filepath, sizeof(v->file_path) - 1);
    v->is_open = true;
    return 0;
}

static inline void seek_video(SimpleVideoPlayer *v, double target_seconds) {
    if (!v->fmt_ctx || v->video_stream_idx == -1) return;
    AVStream *st = v->fmt_ctx->streams[v->video_stream_idx];
    int64_t target_ts = (int64_t)(target_seconds / av_q2d(st->time_base));
    av_seek_frame(v->fmt_ctx, v->video_stream_idx, target_ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(v->codec_ctx);
    v->current_pts = target_seconds;
}

static inline void update_video_player(SimpleVideoPlayer *v, double target_seconds) {
    if (!v->fmt_ctx || v->video_stream_idx == -1) return;

    AVStream *st = v->fmt_ctx->streams[v->video_stream_idx];
    double time_base = av_q2d(st->time_base);

    if (fabs(target_seconds - v->current_pts) > 0.5) {
        seek_video(v, target_seconds);
    }

    AVPacket *pkt = av_packet_alloc();
    while (v->current_pts < target_seconds) {
        if (av_read_frame(v->fmt_ctx, pkt) < 0) break;
        if (pkt->stream_index == v->video_stream_idx) {
            if (avcodec_send_packet(v->codec_ctx, pkt) == 0) {
                if (avcodec_receive_frame(v->codec_ctx, v->frame_yuv) == 0) {
                    v->current_pts = v->frame_yuv->pts * time_base;

                    sws_scale(v->sws_ctx,
                              (uint8_t const *const *)v->frame_yuv->data,
                              v->frame_yuv->linesize, 0, v->height,
                              v->frame_rgb->data, v->frame_rgb->linesize);

                    SDL_UpdateTexture(v->texture, NULL,
                                      v->frame_rgb->data[0],
                                      v->frame_rgb->linesize[0]);
                }
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
}

#endif // VIDEO_PLAYER_H
