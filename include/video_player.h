#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#include <SDL2/SDL.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>

typedef struct {
    SDL_Rect rect;
    bool is_open;
    bool is_dragging;
    bool is_resizing;
    int drag_x;
    int drag_y;

    // Track active playback state
    bool has_active_frame;
    char current_loaded_path[512];

    // FFmpeg context
    AVFormatContext *fmt_ctx;
    AVCodecContext *codec_ctx;
    struct SwsContext *sws_ctx;
    AVFrame *frame;
    AVFrame *frame_rgb;
    uint8_t *rgb_buffer;
    int video_stream_idx;
    double time_base;

    // SDL Display texture
    SDL_Texture *texture;
    int width;
    int height;
    double current_pts_sec;
} SimpleVideoPlayer;

// Video player API declarations
int open_video_by_path(SimpleVideoPlayer *player, const char *filepath, SDL_Renderer *renderer);
void close_video(SimpleVideoPlayer *player);
void seek_video(SimpleVideoPlayer *player, double seconds);
void sync_video_to_audio_clock(SimpleVideoPlayer *player, double target_sec, bool force_seek);

#ifdef __cplusplus
}
#endif

#endif // VIDEO_PLAYER_H
