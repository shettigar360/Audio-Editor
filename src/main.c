#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <stdio.h>
#include <stdbool.h>

#include "font.h"
#include "video_player.h"
#include "audio_engine.h"
#include "daw_plugins.h"
#include "daw_types.h"
#include "project_io.h"
#include "daw_ui.h"
#include "daw_input.h"

DawEngine g_daw;

static SDL_Cursor *g_cursor_arrow = NULL;
static SDL_Cursor *g_cursor_size_we = NULL;
static SDL_Cursor *g_cursor_size_ns = NULL;

void audio_mixer_callback(void *userdata, Uint8 *stream, int len) {
    DawEngine *eng = (DawEngine *)userdata;
    ProjectTab *p = &eng->tabs[eng->active_tab];
    float *out = (float *)stream;
    int frames_needed = len / (sizeof(float) * 2);
    memset(stream, 0, len);

    if (!p->is_playing) return;

    bool any_solo = false;
    for (int t = 0; t < p->num_tracks; t++) {
        if (p->tracks[t].solo) { any_solo = true; break; }
    }

    double st = p->playhead_seconds;
    double et = st + ((double)frames_needed / SAMPLE_RATE);

    for (int i = 0; i < p->num_items; i++) {
        AudioItem *it = &p->items[i];
        if (!it->raw_file_handle) continue;

        Track *tr = &p->tracks[it->track_idx];
        if (tr->mute) continue;
        if (any_solo && !tr->solo) continue;

        float linear_gain = db_to_linear(p->track_db[it->track_idx]);
        double it_s = it->start_time;
        double it_e = it->start_time + it->duration;

        if (it_e > st && it_s < et) {
            int64_t sample_offset = (int64_t)(((st - it_s) + it->offset_in_file) * SAMPLE_RATE);
            if (sample_offset >= 0) {
                fseek(it->raw_file_handle, sample_offset * sizeof(float) * 2, SEEK_SET);
                float chunk[1024 * 2];
                size_t read_frames = fread(chunk, sizeof(float) * 2, frames_needed, it->raw_file_handle);
                for (size_t f = 0; f < read_frames; f++) {
                    out[f * 2]     += chunk[f * 2] * linear_gain;
                    out[f * 2 + 1] += chunk[f * 2 + 1] * linear_gain;
                }
            }
        }
    }

    process_master_plugins(out, frames_needed, &p->master_filter, &p->master_delay);
    p->playhead_seconds += (double)frames_needed / SAMPLE_RATE;
}

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) return 1;

    SDL_Window *win = SDL_CreateWindow(
        "[unsaved project] - Professional Multitrack DAW",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

#ifdef _WIN32
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(win, &wmInfo)) {
        HWND hwnd = wmInfo.info.win.window;
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));
    }
#endif

    g_cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    g_cursor_size_we = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    g_cursor_size_ns = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);

    memset(&g_daw, 0, sizeof(DawEngine));
    g_daw.num_tabs = 1;
    g_daw.active_tab = 0;
    g_daw.tcp_width = 200;
    g_daw.input_focus = INPUT_FOCUS_NONE;
    g_daw.editing_track_name_idx = -1;
    g_daw.editing_marker_idx = -1;
    g_daw.selected_marker_idx = -1;
    g_daw.selected_action_idx = 0;
    g_daw.action_window_open = false;
    g_daw.action_win_rect = (SDL_Rect){ 360, 140, 560, 460 };

    // Default Render Dialog settings
    g_daw.render_window_open = false;
    g_daw.render_win_rect = (SDL_Rect){ 380, 110, 520, 500 };
    strncpy(g_daw.render_settings.directory, "D:\\Harish\\Project file test", sizeof(g_daw.render_settings.directory) - 1);
    strncpy(g_daw.render_settings.filename, "test only can delete", sizeof(g_daw.render_settings.filename) - 1);
    g_daw.render_settings.source_idx = 0;
    g_daw.render_settings.bounds_idx = 0;
    g_daw.render_settings.sample_rate = 48000;
    g_daw.render_settings.channels = 1; // Default mono like image
    g_daw.render_settings.format_idx = 0; // WAV
    g_daw.render_settings.bit_depth_idx = 1; // 24-bit PCM
    g_daw.render_settings.auto_increment = true;
    g_daw.render_settings.skip_silent = true;

    init_tab_defaults(0, "Project 1");
    init_default_actions();
    push_undo_state();

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_mixer_callback;
    want.userdata = &g_daw;
    g_daw.audio_device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (g_daw.audio_device > 0) SDL_PauseAudioDevice(g_daw.audio_device, 0);

    bool running = true;
    SDL_Event e;

    while (running) {
        int win_w, win_h;
        SDL_GetWindowSize(win, &win_w, &win_h);

        while (SDL_PollEvent(&e)) {
            handle_daw_input_events(&e, ren, g_cursor_arrow, g_cursor_size_we, g_cursor_size_ns, &running);
        }

        render_daw_ui(ren, win_w, win_h);
        SDL_Delay(16);
    }

    if (g_daw.audio_device > 0) SDL_CloseAudioDevice(g_daw.audio_device);
    for (int t = 0; t < g_daw.num_tabs; t++) {
        close_video(&g_daw.tabs[t].video);
        for (int i = 0; i < g_daw.tabs[t].num_items; i++) {
            if (g_daw.tabs[t].items[i].raw_file_handle) fclose(g_daw.tabs[t].items[i].raw_file_handle);
            if (g_daw.tabs[t].items[i].peaks) free(g_daw.tabs[t].items[i].peaks);
        }
    }
    SDL_FreeCursor(g_cursor_arrow);
    SDL_FreeCursor(g_cursor_size_we);
    SDL_FreeCursor(g_cursor_size_ns);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
