#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// FFmpeg Headers
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>

// Lua Header
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

// Python Embedded Header
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define MAX_TRACKS 16
#define RULER_HEIGHT 38
#define TOPBAR_HEIGHT 48
#define TCP_WIDTH 190
#define TRACK_HEIGHT 85

// Basic 8x8 ASCII font bitmap (ASCII 32-126)
static const unsigned char font8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 ' '
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // 33 '!'
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, // 34 '"'
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, // 35 '#'
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00}, // 36 '$'
    {0x00,0x66,0xA6,0xD8,0x1B,0x65,0x66,0x00}, // 37 '%'
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, // 38 '&'
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, // 39 '''
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, // 40 '('
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // 41 ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // 42 '*'
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // 43 '+'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // 44 ','
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // 45 '-'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // 46 '.'
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, // 47 '/'
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // 48 '0'
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // 49 '1'
    {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}, // 50 '2'
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // 51 '3'
    {0x0C,0x1C,0x34,0x64,0x7E,0x04,0x04,0x00}, // 52 '4'
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // 53 '5'
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}, // 54 '6'
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, // 55 '7'
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // 56 '8'
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}, // 57 '9'
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, // 58 ':'
    {0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00}, // 59 ';'
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // 60 '<'
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, // 61 '='
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00}, // 62 '>'
    {0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00}, // 63 '?'
    {0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x3C,0x00}, // 64 '@'
    {0x18,0x3C,0x66,0x7E,0x66,0x66,0x66,0x00}, // 65 'A'
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, // 66 'B'
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, // 67 'C'
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, // 68 'D'
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00}, // 69 'E'
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00}, // 70 'F'
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3A,0x00}, // 71 'G'
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // 72 'H'
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 73 'I'
    {0x0E,0x06,0x06,0x06,0x66,0x66,0x3C,0x00}, // 74 'J'
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}, // 75 'K'
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, // 76 'L'
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, // 77 'M'
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}, // 78 'N'
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // 79 'O'
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, // 80 'P'
    {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00}, // 81 'Q'
    {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}, // 82 'R'
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}, // 83 'S'
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // 84 'T'
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // 85 'U'
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, // 86 'V'
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 87 'W'
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, // 88 'X'
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, // 89 'Y'
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, // 90 'Z'
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // 91 '['
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, // 92 '\'
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // 93 ']'
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, // 94 '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // 95 '_'
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, // 96 '`'
    {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00}, // 97 'a'
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00}, // 98 'b'
    {0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00}, // 99 'c'
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00}, // 100 'd'
    {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00}, // 101 'e'
    {0x1C,0x30,0x78,0x30,0x30,0x30,0x30,0x00}, // 102 'f'
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C}, // 103 'g'
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00}, // 104 'h'
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // 105 'i'
    {0x06,0x00,0x0E,0x06,0x06,0x66,0x3C,0x00}, // 106 'j'
    {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00}, // 107 'k'
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 108 'l'
    {0x00,0x00,0x66,0x7F,0x7F,0x6B,0x63,0x00}, // 109 'm'
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00}, // 110 'n'
    {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00}, // 111 'o'
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60}, // 112 'p'
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06}, // 113 'q'
    {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00}, // 114 'r'
    {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00}, // 115 's'
    {0x18,0x18,0x7E,0x18,0x18,0x18,0x0C,0x00}, // 116 't'
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00}, // 117 'u'
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00}, // 118 'v'
    {0x00,0x00,0x63,0x6B,0x7F,0x3E,0x36,0x00}, // 119 'w'
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00}, // 120 'x'
    {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C}, // 121 'y'
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00}, // 122 'z'
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, // 123 '{'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // 124 '|'
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, // 125 '}'
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}  // 126 '~'
};

void draw_char(SDL_Renderer *r, char c, int x, int y, SDL_Color color) {
    if (c < 32 || c > 126) return;
    const unsigned char *glyph = font8x8[c - 32];
    SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (glyph[row] & (1 << (7 - col))) {
                SDL_RenderDrawPoint(r, x + col, y + row);
            }
        }
    }
}

void draw_text(SDL_Renderer *r, const char *str, int x, int y, SDL_Color color) {
    int cur_x = x;
    while (*str) {
        draw_char(r, *str, cur_x, y, color);
        cur_x += 8;
        str++;
    }
}

typedef struct {
    int id;
    bool armed;
    bool mute;
    bool solo;
    float volume;
} Track;

typedef struct {
    SDL_Rect rect;
    bool is_open;
    bool is_dragging;
    int drag_offset_x;
    int drag_offset_y;
} VideoPopup;

int main(int argc, char *argv[]) {
    // 1. Initialize SDL2
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "[unsaved project] - Multitrack Audio Editor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // 2. Setup Multitrack State
    int num_tracks = 1;
    Track tracks[MAX_TRACKS];
    for (int i = 0; i < MAX_TRACKS; i++) {
        tracks[i].id = i + 1;
        tracks[i].armed = (i == 0);
        tracks[i].mute = false;
        tracks[i].solo = false;
        tracks[i].volume = 0.8f;
    }

    // 3. Setup Floating Video Window State
    VideoPopup video_win = {
        .rect = { 600, 300, 420, 280 },
        .is_open = true,
        .is_dragging = false
    };

    // 4. Playhead State
    float playhead_x = 0.0f;
    bool is_playing = false;

    // 5. Main Loop
    bool running = true;
    SDL_Event e;

    while (running) {
        int win_w, win_h;
        SDL_GetWindowSize(win, &win_w, &win_h);

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_SPACE) {
                    is_playing = !is_playing;
                } else if (e.key.keysym.sym == SDLK_v) {
                    video_win.is_open = !video_win.is_open; // Toggle Video Window
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x;
                int my = e.button.y;

                // Handle Video Window drag & close
                if (video_win.is_open && mx >= video_win.rect.x && mx <= video_win.rect.x + video_win.rect.w &&
                    my >= video_win.rect.y && my <= video_win.rect.y + 24) {
                    // Check close button
                    if (mx >= video_win.rect.x + video_win.rect.w - 20) {
                        video_win.is_open = false;
                    } else {
                        video_win.is_dragging = true;
                        video_win.drag_offset_x = mx - video_win.rect.x;
                        video_win.drag_offset_y = my - video_win.rect.y;
                    }
                }
                // Handle Add Track [+] button click
                else if (mx >= 50 && mx <= 90 && my >= TOPBAR_HEIGHT + (num_tracks * TRACK_HEIGHT) + 10 &&
                         my <= TOPBAR_HEIGHT + (num_tracks * TRACK_HEIGHT) + 40) {
                    if (num_tracks < MAX_TRACKS) num_tracks++;
                }
                // Handle TCP Mute / Solo clicks
                else if (mx < TCP_WIDTH && my >= TOPBAR_HEIGHT) {
                    int t_idx = (my - TOPBAR_HEIGHT) / TRACK_HEIGHT;
                    if (t_idx >= 0 && t_idx < num_tracks) {
                        int rel_y = (my - TOPBAR_HEIGHT) % TRACK_HEIGHT;
                        if (rel_y >= 10 && rel_y <= 28) {
                            if (mx >= 135 && mx <= 153) tracks[t_idx].mute = !tracks[t_idx].mute;
                            if (mx >= 155 && mx <= 173) tracks[t_idx].solo = !tracks[t_idx].solo;
                        }
                    }
                }
                // Handle Timeline ruler click (scrub playhead)
                else if (mx >= TCP_WIDTH && my >= TOPBAR_HEIGHT && my <= TOPBAR_HEIGHT + RULER_HEIGHT) {
                    playhead_x = (float)(mx - TCP_WIDTH);
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                video_win.is_dragging = false;
            } else if (e.type == SDL_MOUSEMOTION) {
                if (video_win.is_dragging) {
                    video_win.rect.x = e.motion.x - video_win.drag_offset_x;
                    video_win.rect.y = e.motion.y - video_win.drag_offset_y;
                }
            }
        }

        if (is_playing) {
            playhead_x += 1.2f;
            if (playhead_x > win_w - TCP_WIDTH) playhead_x = 0;
        }

        // ================= RENDER =================
        // Background
        SDL_SetRenderDrawColor(ren, 43, 43, 43, 255);
        SDL_RenderClear(ren);

        // 1. Menu & Topbar
        SDL_Rect topbar = { 0, 0, win_w, TOPBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 33, 33, 33, 255);
        SDL_RenderFillRect(ren, &topbar);

        SDL_Color text_white = { 210, 210, 210, 255 };
        SDL_Color text_muted = { 130, 130, 130, 255 };
        SDL_Color text_teal  = { 0, 200, 160, 255 };

        draw_text(ren, "File   Edit   View   Insert   Item   Track   Options   Actions   Help", 14, 8, text_white);
        draw_text(ren, "[Add new track]", 540, 8, text_teal);
        draw_text(ren, "[48kHz 24bit WAV : 2/2ch WaveOut]  Space:Play/Pause  V:Video", win_w - 530, 8, text_muted);

        // 2. Timeline Grid & Lanes
        int timeline_w = win_w - TCP_WIDTH;
        for (int i = 0; i < timeline_w / 60 + 1; i++) {
            int grid_x = TCP_WIDTH + (i * 60);
            SDL_SetRenderDrawColor(ren, (i % 2 == 0) ? 46 : 41, (i % 2 == 0) ? 46 : 41, (i % 2 == 0) ? 46 : 41, 255);
            SDL_Rect col = { grid_x, TOPBAR_HEIGHT + RULER_HEIGHT, 60, win_h };
            SDL_RenderFillRect(ren, &col);

            SDL_SetRenderDrawColor(ren, 35, 35, 35, 255);
            SDL_RenderDrawLine(ren, grid_x, TOPBAR_HEIGHT + RULER_HEIGHT, grid_x, win_h);
        }

        // Horizontal track dividers
        for (int i = 0; i < num_tracks; i++) {
            int ty = TOPBAR_HEIGHT + RULER_HEIGHT + (i * TRACK_HEIGHT);
            SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
            SDL_RenderDrawLine(ren, TCP_WIDTH, ty, win_w, ty);
        }

        // 3. Timeline Ruler
        SDL_Rect ruler = { TCP_WIDTH, TOPBAR_HEIGHT, timeline_w, RULER_HEIGHT };
        SDL_SetRenderDrawColor(ren, 32, 32, 32, 255);
        SDL_RenderFillRect(ren, &ruler);

        for (int i = 0; i < timeline_w / 120 + 1; i++) {
            int rx = TCP_WIDTH + (i * 120);
            char bar_txt[32], time_txt[32];
            snprintf(bar_txt, sizeof(bar_txt), "%d.1", i * 2 + 1);
            snprintf(time_txt, sizeof(time_txt), "0:%02d.000", i * 4);

            draw_text(ren, bar_txt, rx + 4, TOPBAR_HEIGHT + 6, text_white);
            draw_text(ren, time_txt, rx + 4, TOPBAR_HEIGHT + 20, text_muted);

            SDL_SetRenderDrawColor(ren, 65, 65, 65, 255);
            SDL_RenderDrawLine(ren, rx, TOPBAR_HEIGHT, rx, TOPBAR_HEIGHT + RULER_HEIGHT);
        }

        // 4. Playhead Line & Triangle Pointer
        int cur_px = TCP_WIDTH + (int)playhead_x;
        SDL_SetRenderDrawColor(ren, 19, 168, 158, 255);
        SDL_RenderDrawLine(ren, cur_px, TOPBAR_HEIGHT, cur_px, win_h);
        // Playhead top triangle
        for (int t = 0; t < 8; t++) {
            SDL_RenderDrawLine(ren, cur_px - t, TOPBAR_HEIGHT + t, cur_px + t, TOPBAR_HEIGHT + t);
        }

        // 5. Track Control Panels (Left Sidebar / TCP)
        SDL_Rect tcp_bg = { 0, TOPBAR_HEIGHT, TCP_WIDTH, win_h - TOPBAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 50, 50, 50, 255);
        SDL_RenderFillRect(ren, &tcp_bg);

        for (int i = 0; i < num_tracks; i++) {
            int ty = TOPBAR_HEIGHT + (i * TRACK_HEIGHT);
            SDL_Rect track_panel = { 2, ty + 2, TCP_WIDTH - 4, TRACK_HEIGHT - 4 };
            SDL_SetRenderDrawColor(ren, 62, 62, 62, 255);
            SDL_RenderFillRect(ren, &track_panel);

            // Track index
            char num_str[8];
            snprintf(num_str, sizeof(num_str), "%d", tracks[i].id);
            draw_text(ren, num_str, 8, ty + 12, text_white);

            // Record Arm circle
            SDL_Rect arm_btn = { 24, ty + 10, 16, 16 };
            SDL_SetRenderDrawColor(ren, tracks[i].armed ? 190 : 80, 40, 40, 255);
            SDL_RenderFillRect(ren, &arm_btn);

            // Volume bar representation
            SDL_Rect vol_bg = { 46, ty + 12, 80, 12 };
            SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
            SDL_RenderFillRect(ren, &vol_bg);
            SDL_Rect vol_fill = { 46, ty + 12, (int)(80 * tracks[i].volume), 12 };
            SDL_SetRenderDrawColor(ren, 150, 150, 150, 255);
            SDL_RenderFillRect(ren, &vol_fill);

            // Mute [M] Button
            SDL_Rect mute_btn = { 135, ty + 10, 18, 18 };
            SDL_SetRenderDrawColor(ren, tracks[i].mute ? 180 : 75, tracks[i].mute ? 40 : 75, 40, 255);
            SDL_RenderFillRect(ren, &mute_btn);
            draw_text(ren, "M", 140, ty + 15, text_white);

            // Solo [S] Button
            SDL_Rect solo_btn = { 158, ty + 10, 18, 18 };
            SDL_SetRenderDrawColor(ren, tracks[i].solo ? 200 : 75, tracks[i].solo ? 170 : 75, 40, 255);
            SDL_RenderFillRect(ren, &solo_btn);
            draw_text(ren, "S", 163, ty + 15, text_white);

            // Divider
            SDL_SetRenderDrawColor(ren, 25, 25, 25, 255);
            SDL_RenderDrawLine(ren, 0, ty + TRACK_HEIGHT, TCP_WIDTH, ty + TRACK_HEIGHT);
        }

        // [+] Add Track button
        int add_y = TOPBAR_HEIGHT + (num_tracks * TRACK_HEIGHT) + 12;
        if (add_y + 30 < win_h) {
            SDL_Rect add_btn = { TCP_WIDTH / 2 - 14, add_y, 28, 28 };
            SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
            SDL_RenderFillRect(ren, &add_btn);
            draw_text(ren, "+", TCP_WIDTH / 2 - 4, add_y + 10, text_white);
        }

        // 6. Floating Video Window (Pop-up matching the screenshot)
        if (video_win.is_open) {
            // Drop shadow
            SDL_Rect shadow = { video_win.rect.x + 4, video_win.rect.y + 4, video_win.rect.w, video_win.rect.h };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 120);
            SDL_RenderFillRect(ren, &shadow);

            // Video Window Titlebar (White bar matching REAPER)
            SDL_Rect v_bar = { video_win.rect.x, video_win.rect.y, video_win.rect.w, 24 };
            SDL_SetRenderDrawColor(ren, 245, 245, 245, 255);
            SDL_RenderFillRect(ren, &v_bar);

            SDL_Color v_title_color = { 50, 50, 50, 255 };
            draw_text(ren, "Video Window", video_win.rect.x + 8, video_win.rect.y + 7, v_title_color);
            draw_text(ren, "x", video_win.rect.x + video_win.rect.w - 14, video_win.rect.y + 6, v_title_color);

            // Submenu bar ("Options")
            SDL_Rect v_sub = { video_win.rect.x, video_win.rect.y + 24, video_win.rect.w, 22 };
            SDL_SetRenderDrawColor(ren, 235, 235, 235, 255);
            SDL_RenderFillRect(ren, &v_sub);
            draw_text(ren, "Options", video_win.rect.x + 8, video_win.rect.y + 30, v_title_color);

            // Black Video Display surface
            SDL_Rect v_body = { video_win.rect.x, video_win.rect.y + 46, video_win.rect.w, video_win.rect.h - 46 };
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderFillRect(ren, &v_body);
        }

        SDL_RenderPresent(ren);
        SDL_Delay(16); // ~60 FPS
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
