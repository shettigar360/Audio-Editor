#include "video_player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int open_video_by_path(SimpleVideoPlayer *player, const char *filepath, SDL_Renderer *renderer) {
    if (!player || !filepath || !renderer) return -1;

    close_video(player);

    player->video_stream_idx = -1;
    player->fmt_ctx = NULL;

    if (avformat_open_input(&player->fmt_ctx, filepath, NULL, NULL) < 0) {
        return -1;
    }

    if (avformat_find_stream_info(player->fmt_ctx, NULL) < 0) {
        avformat_close_input(&player->fmt_ctx);
        return -1;
    }

    const AVCodec *codec = NULL;
    for (unsigned int i = 0; i < player->fmt_ctx->nb_streams; i++) {
        if (player->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            player->video_stream_idx = (int)i;
            codec = avcodec_find_decoder(player->fmt_ctx->streams[i]->codecpar->codec_id);
            break;
        }
    }

    if (player->video_stream_idx == -1 || !codec) {
        avformat_close_input(&player->fmt_ctx);
        return -1;
    }

    player->codec_ctx = avcodec_alloc_context3(codec);
    if (!player->codec_ctx) {
        avformat_close_input(&player->fmt_ctx);
        return -1;
    }

    AVStream *stream = player->fmt_ctx->streams[player->video_stream_idx];
    if (avcodec_parameters_to_context(player->codec_ctx, stream->codecpar) < 0) {
        avcodec_free_context(&player->codec_ctx);
        avformat_close_input(&player->fmt_ctx);
        return -1;
    }

    if (avcodec_open2(player->codec_ctx, codec, NULL) < 0) {
        avcodec_free_context(&player->codec_ctx);
        avformat_close_input(&player->fmt_ctx);
        return -1;
    }

    player->width = player->codec_ctx->width;
    player->height = player->codec_ctx->height;
    player->time_base = av_q2d(stream->time_base);

    player->frame = av_frame_alloc();
    player->frame_rgb = av_frame_alloc();

    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, player->width, player->height, 1);
    player->rgb_buffer = (uint8_t *)av_malloc(num_bytes * sizeof(uint8_t));

    av_image_fill_arrays(
        player->frame_rgb->data,
        player->frame_rgb->linesize,
        player->rgb_buffer,
        AV_PIX_FMT_RGB24,
        player->width,
        player->height,
        1
    );

    player->sws_ctx = sws_getContext(
        player->width,
        player->height,
        player->codec_ctx->pix_fmt,
        player->width,
        player->height,
        AV_PIX_FMT_RGB24,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );

    player->texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        player->width,
        player->height
    );

    player->is_open = true;
    player->current_pts_sec = 0.0;
    player->has_active_frame = true;
    strncpy(player->current_loaded_path, filepath, sizeof(player->current_loaded_path) - 1);

    seek_video(player, 0.0);
    return 0;
}

void close_video(SimpleVideoPlayer *player) {
    if (!player) return;

    if (player->texture) {
        SDL_DestroyTexture(player->texture);
        player->texture = NULL;
    }
    if (player->sws_ctx) {
        sws_freeContext(player->sws_ctx);
        player->sws_ctx = NULL;
    }
    if (player->rgb_buffer) {
        av_free(player->rgb_buffer);
        player->rgb_buffer = NULL;
    }
    if (player->frame_rgb) {
        av_frame_free(&player->frame_rgb);
        player->frame_rgb = NULL;
    }
    if (player->frame) {
        av_frame_free(&player->frame);
        player->frame = NULL;
    }
    if (player->codec_ctx) {
        avcodec_free_context(&player->codec_ctx);
        player->codec_ctx = NULL;
    }
    if (player->fmt_ctx) {
        avformat_close_input(&player->fmt_ctx);
        player->fmt_ctx = NULL;
    }

    player->video_stream_idx = -1;
    player->is_open = false;
    player->has_active_frame = false;
    player->current_loaded_path[0] = '\0';
}

void seek_video(SimpleVideoPlayer *player, double seconds) {
    if (!player || !player->fmt_ctx || player->video_stream_idx == -1) return;

    int64_t target_pts = (int64_t)(seconds / player->time_base);
    av_seek_frame(player->fmt_ctx, player->video_stream_idx, target_pts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(player->codec_ctx);

    AVPacket packet;
    while (av_read_frame(player->fmt_ctx, &packet) >= 0) {
        if (packet.stream_index == player->video_stream_idx) {
            int ret = avcodec_send_packet(player->codec_ctx, &packet);
            if (ret >= 0) {
                ret = avcodec_receive_frame(player->codec_ctx, player->frame);
                if (ret == 0) {
                    double pts = player->frame->pts * player->time_base;
                    sws_scale(
                        player->sws_ctx,
                        (const uint8_t *const *)player->frame->data,
                        player->frame->linesize,
                        0,
                        player->height,
                        player->frame_rgb->data,
                        player->frame_rgb->linesize
                    );
                    SDL_UpdateTexture(
                        player->texture,
                        NULL,
                        player->frame_rgb->data[0],
                        player->frame_rgb->linesize[0]
                    );
                    player->current_pts_sec = pts;
                    av_packet_unref(&packet);
                    break;
                }
            }
        }
        av_packet_unref(&packet);
    }
}

void sync_video_to_audio_clock(SimpleVideoPlayer *player, double target_sec, bool force_seek) {
    if (!player || !player->fmt_ctx || player->video_stream_idx == -1) return;

    if (force_seek || fabs(target_sec - player->current_pts_sec) > 0.5) {
        seek_video(player, target_sec);
        return;
    }

    if (target_sec < player->current_pts_sec) {
        seek_video(player, target_sec);
        return;
    }

    AVPacket packet;
    while (player->current_pts_sec < target_sec && av_read_frame(player->fmt_ctx, &packet) >= 0) {
        if (packet.stream_index == player->video_stream_idx) {
            int ret = avcodec_send_packet(player->codec_ctx, &packet);
            if (ret >= 0) {
                while (avcodec_receive_frame(player->codec_ctx, player->frame) == 0) {
                    double pts = player->frame->pts * player->time_base;
                    sws_scale(
                        player->sws_ctx,
                        (const uint8_t *const *)player->frame->data,
                        player->frame->linesize,
                        0,
                        player->height,
                        player->frame_rgb->data,
                        player->frame_rgb->linesize
                    );
                    SDL_UpdateTexture(
                        player->texture,
                        NULL,
                        player->frame_rgb->data[0],
                        player->frame_rgb->linesize[0]
                    );
                    player->current_pts_sec = pts;
                }
            }
        }
        av_packet_unref(&packet);
    }
}
