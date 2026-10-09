#ifndef DAW_UI_H
#define DAW_UI_H

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include "font.h"
#include "daw_types.h"

static inline void render_daw_ui(SDL_Renderer *ren, int win_w, int win_h) {
    ProjectTab *p = get_cur_tab();
    int m_lane_h = p->show_marker_lane ? MARKER_BAR_HEIGHT : 0;
    int view_w = win_w - g_daw.tcp_width - SCROLLBAR_SIZE;
    int view_h = win_h - TABBAR_HEIGHT - TOPBAR_HEIGHT - RULER_HEIGHT - m_lane_h - SCROLLBAR_SIZE;
    int max_h_tracks = 0;
    for (int t = 0; t < p->num_tracks; t++) max_h_tracks += p->track_heights[t];

    SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
    SDL_RenderClear(ren);

    SDL_Color c_white = { 224, 224, 224, 255 };
    SDL_Color c_muted = { 130, 130, 130, 255 };
    SDL_Color c_blue  = { 26, 86, 219, 255 };

    // 1. Chrome-Style Project Tabs Bar [ x Project N ] [+]
    SDL_Rect tab_bar = { 0, 0, win_w, TABBAR_HEIGHT };
    SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
    SDL_RenderFillRect(ren, &tab_bar);

    const int tab_w = 140;
    for (int t = 0; t < g_daw.num_tabs; t++) {
        int tx = t * tab_w;
        bool is_act = (t == g_daw.active_tab);

        SDL_Rect tr = { tx, 2, tab_w - 2, TABBAR_HEIGHT - 2 };
        SDL_SetRenderDrawColor(ren, is_act ? 34 : 22, is_act ? 34 : 22, is_act ? 34 : 22, 255);
        SDL_RenderFillRect(ren, &tr);

        SDL_SetRenderDrawColor(ren, is_act ? 50 : 28, is_act ? 50 : 28, is_act ? 50 : 28, 255);
        SDL_RenderDrawRect(ren, &tr);

        SDL_Rect close_btn = { tx + 6, 7, 16, 16 };
        SDL_SetRenderDrawColor(ren, is_act ? 48 : 28, is_act ? 48 : 28, is_act ? 48 : 28, 255);
        SDL_RenderFillRect(ren, &close_btn);
        draw_text(ren, "x", tx + 10, 9, c_muted);

        draw_text(ren, g_daw.tabs[t].project_name, tx + 28, 9, is_act ? c_white : c_muted);
    }

    int plus_x = g_daw.num_tabs * tab_w + 4;
    if (g_daw.num_tabs < MAX_TABS) {
        SDL_Rect btn_add_tab = { plus_x, 4, 26, TABBAR_HEIGHT - 6 };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &btn_add_tab);
        SDL_SetRenderDrawColor(ren, 50, 50, 50, 255);
        SDL_RenderDrawRect(ren, &btn_add_tab);
        draw_text(ren, "+", plus_x + 9, 8, c_white);
    }

    // 2. Top Bar
    SDL_Rect topbar = { 0, TABBAR_HEIGHT, win_w, TOPBAR_HEIGHT };
    SDL_SetRenderDrawColor(ren, 28, 28, 28, 255);
    SDL_RenderFillRect(ren, &topbar);

    SDL_Rect btn_file = { 8, TABBAR_HEIGHT + 5, 46, 26 };
    SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
    SDL_RenderFillRect(ren, &btn_file);
    draw_text(ren, "File", 18, TABBAR_HEIGHT + 13, c_white);

    SDL_Rect btn_act = { 56, TABBAR_HEIGHT + 5, 60, 26 };
    SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
    SDL_RenderFillRect(ren, &btn_act);
    draw_text(ren, "Action", 64, TABBAR_HEIGHT + 13, c_white);

    SDL_Rect btn_mk_toggle = { 122, TABBAR_HEIGHT + 5, 66, 26 };
    SDL_SetRenderDrawColor(ren, p->show_marker_lane ? 55 : 40, p->show_marker_lane ? 55 : 40, p->show_marker_lane ? 55 : 40, 255);
    SDL_RenderFillRect(ren, &btn_mk_toggle);
    draw_text(ren, "Marker", 130, TABBAR_HEIGHT + 13, c_white);

    SDL_Rect v_menu = { 194, TABBAR_HEIGHT + 5, 88, 26 };
    SDL_SetRenderDrawColor(ren, p->video.is_open ? 55 : 40, p->video.is_open ? 55 : 40, p->video.is_open ? 55 : 40, 255);
    SDL_RenderFillRect(ren, &v_menu);
    draw_text(ren, "View:Video", 200, TABBAR_HEIGHT + 13, c_white);

    char clock_buf[32];
    format_timecode(p->playhead_seconds, clock_buf, sizeof(clock_buf));
    int clock_x = (win_w / 2) - 52;
    SDL_Rect clock_bg = { clock_x - 10, TABBAR_HEIGHT + 4, 128, 28 };
    SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
    SDL_RenderFillRect(ren, &clock_bg);
    draw_text(ren, clock_buf, clock_x - 4, TABBAR_HEIGHT + 12, c_white);

    SDL_Rect btn_undo = { win_w - 228, TABBAR_HEIGHT + 5, 52, 26 };
    SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
    SDL_RenderFillRect(ren, &btn_undo);
    draw_text(ren, "Undo", win_w - 218, TABBAR_HEIGHT + 13, c_white);

    SDL_Rect btn_redo = { win_w - 172, TABBAR_HEIGHT + 5, 52, 26 };
    SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
    SDL_RenderFillRect(ren, &btn_redo);
    draw_text(ren, "Redo", win_w - 162, TABBAR_HEIGHT + 13, c_white);

    SDL_Rect btn_zm_out = { win_w - 116, TABBAR_HEIGHT + 5, 56, 26 };
    SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
    SDL_RenderFillRect(ren, &btn_zm_out);
    draw_text(ren, "Zoom-", win_w - 110, TABBAR_HEIGHT + 13, c_white);

    SDL_Rect btn_zm_in = { win_w - 56, TABBAR_HEIGHT + 5, 50, 26 };
    SDL_SetRenderDrawColor(ren, 42, 42, 42, 255);
    SDL_RenderFillRect(ren, &btn_zm_in);
    draw_text(ren, "Zoom+", win_w - 50, TABBAR_HEIGHT + 13, c_white);

    // 3. Corner Header
    int corner_y = TABBAR_HEIGHT + TOPBAR_HEIGHT;
    SDL_Rect corner_area = { 0, corner_y, g_daw.tcp_width, RULER_HEIGHT + m_lane_h };
    SDL_SetRenderDrawColor(ren, 32, 32, 32, 255);
    SDL_RenderFillRect(ren, &corner_area);

    SDL_Rect btn_corner_add = { 8, corner_y + 10, 72, 26 };
    SDL_SetRenderDrawColor(ren, 48, 48, 48, 255);
    SDL_RenderFillRect(ren, &btn_corner_add);
    draw_text(ren, "+ Track", 16, corner_y + 18, c_white);

    SDL_Rect btn_corner_play = { 86, corner_y + 10, 72, 26 };
    SDL_SetRenderDrawColor(ren, p->is_playing ? 65 : 48, 48, 48, 255);
    SDL_RenderFillRect(ren, &btn_corner_play);
    draw_text(ren, p->is_playing ? "Pause" : "Play", 102, corner_y + 18, c_white);

    // 4. Time Ruler & Marker Bar
    SDL_Rect ruler = { g_daw.tcp_width, corner_y, view_w, RULER_HEIGHT };
    SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
    SDL_RenderFillRect(ren, &ruler);

    double min_px_gap = 90.0;
    double sec_per_tick = min_px_gap / p->pixels_per_second;
    double nice_steps[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0 };
    int num_steps = sizeof(nice_steps) / sizeof(nice_steps[0]);
    double chosen_step = nice_steps[num_steps - 1];
    for (int s = 0; s < num_steps; s++) {
        if (nice_steps[s] >= sec_per_tick) { chosen_step = nice_steps[s]; break; }
    }

    double start_tick = floor(p->scroll_x_sec / chosen_step) * chosen_step;
    double end_tick = p->scroll_x_sec + (double)view_w / p->pixels_per_second + chosen_step;

    for (double t = start_tick; t <= end_tick; t += chosen_step) {
        int rx = g_daw.tcp_width + (int)((t - p->scroll_x_sec) * p->pixels_per_second);
        if (rx < g_daw.tcp_width || rx > g_daw.tcp_width + view_w) continue;

        char t_str[32];
        if (chosen_step < 1.0) snprintf(t_str, sizeof(t_str), "%.2fs", t);
        else if (t < 60.0) snprintf(t_str, sizeof(t_str), "%02ds", (int)t);
        else if (t < 3600.0) {
            int m = (int)(t / 60); int s = (int)(t - m * 60);
            snprintf(t_str, sizeof(t_str), "%02d:%02d", m, s);
        } else {
            int h = (int)(t / 3600); int m = (int)((t - h * 3600) / 60);
            snprintf(t_str, sizeof(t_str), "%02d:%02d:00", h, m);
        }
        draw_text(ren, t_str, rx + 4, corner_y + 8, c_muted);
        SDL_SetRenderDrawColor(ren, 55, 55, 55, 255);
        SDL_RenderDrawLine(ren, rx, corner_y + 16, rx, corner_y + RULER_HEIGHT);
    }

    // Logic Pro Marker Lane
    int marker_y = corner_y + RULER_HEIGHT;
    if (p->show_marker_lane) {
        SDL_Rect marker_bg = { g_daw.tcp_width, marker_y, view_w, MARKER_BAR_HEIGHT };
        SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
        SDL_RenderFillRect(ren, &marker_bg);
        SDL_SetRenderDrawColor(ren, 40, 40, 40, 255);
        SDL_RenderDrawLine(ren, g_daw.tcp_width, marker_y + MARKER_BAR_HEIGHT - 1, g_daw.tcp_width + view_w, marker_y + MARKER_BAR_HEIGHT - 1);

        for (int m = 0; m < p->num_markers; m++) {
            int mk_x = g_daw.tcp_width + (int)((p->markers[m].time_sec - p->scroll_x_sec) * p->pixels_per_second);
            if (mk_x >= g_daw.tcp_width && mk_x <= g_daw.tcp_width + view_w) {
                SDL_Rect m_tag = { mk_x, marker_y + 2, 74, MARKER_BAR_HEIGHT - 4 };
                bool is_editing_this = (g_daw.input_focus == INPUT_FOCUS_MARKER && g_daw.editing_marker_idx == m);
                bool is_selected_this = (g_daw.selected_marker_idx == m);

                SDL_SetRenderDrawColor(ren, is_editing_this ? 26 : 48, is_editing_this ? 86 : 48, is_editing_this ? 219 : 48, 255);
                SDL_RenderFillRect(ren, &m_tag);

                if (is_selected_this && !is_editing_this) {
                    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                    SDL_RenderDrawRect(ren, &m_tag);
                }

                if (is_editing_this) {
                    draw_text(ren, g_daw.editing_marker_buf, mk_x + 4, marker_y + 6, c_white);
                } else {
                    draw_text(ren, p->markers[m].text, mk_x + 4, marker_y + 6, c_white);
                }
            }
        }
    }

    int header_top_y = marker_y + m_lane_h;
    SDL_Rect timeline_clip = { g_daw.tcp_width, header_top_y, view_w, view_h };
    SDL_RenderSetClipRect(ren, &timeline_clip);

    for (double t = start_tick; t <= end_tick; t += chosen_step) {
        int gx = g_daw.tcp_width + (int)((t - p->scroll_x_sec) * p->pixels_per_second);
        if (gx < g_daw.tcp_width || gx > g_daw.tcp_width + view_w) continue;
        SDL_SetRenderDrawColor(ren, 38, 38, 38, 255);
        SDL_RenderDrawLine(ren, gx, header_top_y, gx, header_top_y + view_h);
    }

    int track_cur_y = header_top_y - p->scroll_y_px;
    for (int i = 0; i < p->num_tracks; i++) {
        track_cur_y += p->track_heights[i];
        SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
        SDL_RenderDrawLine(ren, g_daw.tcp_width, track_cur_y, g_daw.tcp_width + view_w, track_cur_y);
    }

    // 5. Render Clips & Waveforms
    for (int i = 0; i < p->num_items; i++) {
        AudioItem *it = &p->items[i];
        int iy = header_top_y - p->scroll_y_px;
        for (int t = 0; t < it->track_idx; t++) iy += p->track_heights[t];
        iy += 2;
        int ih = p->track_heights[it->track_idx] - 4;
        int ix = g_daw.tcp_width + (int)((it->start_time - p->scroll_x_sec) * p->pixels_per_second);
        int iw = (int)(it->duration * p->pixels_per_second);

        if (ix + iw < g_daw.tcp_width || ix > g_daw.tcp_width + view_w) continue;

        float gain_multiplier = db_to_linear(p->track_db[it->track_idx]);

        SDL_Rect clip_r = { ix, iy, iw, ih };
        SDL_SetRenderDrawColor(ren, it->is_selected ? 48 : 34, it->is_selected ? 48 : 34, it->is_selected ? 48 : 34, 255);
        SDL_RenderFillRect(ren, &clip_r);
        SDL_SetRenderDrawColor(ren, it->is_selected ? 180 : 80, it->is_selected ? 180 : 80, it->is_selected ? 180 : 80, 255);
        SDL_RenderDrawRect(ren, &clip_r);

        for (int o = 0; o < p->num_items; o++) {
            if (o == i) continue;
            AudioItem *ot = &p->items[o];
            if (ot->track_idx == it->track_idx && ot->start_time < it->start_time) {
                double overlap_s = it->start_time;
                double overlap_e = fmin(it->start_time + it->duration, ot->start_time + ot->duration);
                if (overlap_e > overlap_s) {
                    int ox = g_daw.tcp_width + (int)((overlap_s - p->scroll_x_sec) * p->pixels_per_second);
                    int ow = (int)((overlap_e - overlap_s) * p->pixels_per_second);
                    SDL_SetRenderDrawColor(ren, 185, 30, 30, 255);
                    for (int px = 0; px < ow; px++) {
                        float factor = (float)px / (float)ow;
                        int y_fade_in = iy + ih - (int)(sinf(factor * 1.57079f) * ih);
                        int y_fade_out = iy + (int)(sinf(factor * 1.57079f) * ih);
                        SDL_RenderDrawPoint(ren, ox + px, y_fade_in);
                        SDL_RenderDrawPoint(ren, ox + px, y_fade_out);
                    }
                }
            }
        }

        int mid_y = iy + ih / 2;
        SDL_SetRenderDrawColor(ren, 200, 200, 200, 255);

        for (int col = 0; col < iw; col++) {
            int px = ix + col;
            if (px < g_daw.tcp_width || px > g_daw.tcp_width + view_w) continue;

            double cur_item_sec = it->offset_in_file + ((double)col / p->pixels_per_second);
            int bin_idx = (int)((cur_item_sec * SAMPLE_RATE) / PEAK_SAMPLES_PER_BIN);

            if (bin_idx >= 0 && bin_idx < it->peak_count) {
                float mn = it->peaks[bin_idx].min_val * gain_multiplier;
                float mx = it->peaks[bin_idx].max_val * gain_multiplier;

                int y1 = mid_y - (int)(mx * (ih / 2.0f - 2));
                int y2 = mid_y - (int)(mn * (ih / 2.0f - 2));
                if (y1 < iy) y1 = iy;
                if (y2 > iy + ih) y2 = iy + ih;
                if (y1 == y2) y2 += 1;
                SDL_RenderDrawLine(ren, px, y1, px, y2);
            }
        }
        draw_text(ren, it->label, ix + 6, iy + 6, c_white);
    }

    if (g_daw.marquee_selecting) {
        int mx1 = fmin(g_daw.marquee_start_x, g_daw.marquee_cur_x);
        int my1 = fmin(g_daw.marquee_start_y, g_daw.marquee_cur_y);
        int mw = abs(g_daw.marquee_cur_x - g_daw.marquee_start_x);
        int mh = abs(g_daw.marquee_cur_y - g_daw.marquee_start_y);
        SDL_Rect mr = { mx1, my1, mw, mh };
        SDL_SetRenderDrawColor(ren, 100, 100, 100, 255);
        SDL_RenderDrawRect(ren, &mr);
    }

    int cur_px = g_daw.tcp_width + (int)((p->playhead_seconds - p->scroll_x_sec) * p->pixels_per_second);
    if (cur_px >= g_daw.tcp_width && cur_px <= g_daw.tcp_width + view_w) {
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_RenderDrawLine(ren, cur_px, corner_y, cur_px, header_top_y + view_h);
    }
    SDL_RenderSetClipRect(ren, NULL);

    // 6. 3-Section Track Header (TCP)
    SDL_Rect tcp_clip = { 0, header_top_y, g_daw.tcp_width, view_h };
    SDL_RenderSetClipRect(ren, &tcp_clip);
    for (int i = 0; i < p->num_tracks; i++) {
        int ty = header_top_y - p->scroll_y_px;
        for (int t = 0; t < i; t++) ty += p->track_heights[t];
        int th = p->track_heights[i];

        SDL_Rect tp = { 2, ty + 2, g_daw.tcp_width - 4, th - 4 };
        SDL_SetRenderDrawColor(ren, p->track_selected[i] ? 45 : 32, p->track_selected[i] ? 45 : 32, p->track_selected[i] ? 45 : 32, 255);
        SDL_RenderFillRect(ren, &tp);

        SDL_Rect name_bar = { 4, ty + 4, g_daw.tcp_width - 8, 22 };
        SDL_SetRenderDrawColor(ren, 26, 26, 26, 255);
        SDL_RenderFillRect(ren, &name_bar);

        if (g_daw.input_focus == INPUT_FOCUS_TRACK_NAME && g_daw.editing_track_name_idx == i) {
            SDL_SetRenderDrawColor(ren, c_blue.r, c_blue.g, c_blue.b, 255);
            SDL_RenderFillRect(ren, &name_bar);
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
            SDL_RenderDrawRect(ren, &name_bar);
            draw_text(ren, g_daw.editing_name_buf, 8, ty + 9, c_white);
        } else {
            draw_text(ren, p->track_names[i], 8, ty + 9, c_white);
        }

        int bot_y = ty + 34;

        SDL_Rect btn_minus = { 6, bot_y, 18, 20 };
        SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
        SDL_RenderFillRect(ren, &btn_minus);
        draw_text(ren, "-", 12, bot_y + 4, c_white);

        char db_txt[16];
        snprintf(db_txt, sizeof(db_txt), "%+.1f", p->track_db[i]);
        draw_text(ren, db_txt, 28, bot_y + 4, c_white);

        SDL_Rect btn_plus = { 78, bot_y, 18, 20 };
        SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
        SDL_RenderFillRect(ren, &btn_plus);
        draw_text(ren, "+", 84, bot_y + 4, c_white);

        int m_x = g_daw.tcp_width - 56;
        int s_x = g_daw.tcp_width - 28;

        SDL_Rect m_btn = { m_x, bot_y, 22, 20 };
        SDL_SetRenderDrawColor(ren, p->tracks[i].mute ? 160 : 44, 44, 44, 255);
        SDL_RenderFillRect(ren, &m_btn);
        draw_text(ren, "M", m_x + 7, bot_y + 4, c_white);

        SDL_Rect s_btn = { s_x, bot_y, 22, 20 };
        SDL_SetRenderDrawColor(ren, p->tracks[i].solo ? 160 : 44, p->tracks[i].solo ? 140 : 44, 44, 255);
        SDL_RenderFillRect(ren, &s_btn);
        draw_text(ren, "S", s_x + 7, bot_y + 4, c_white);

        SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
        SDL_RenderDrawLine(ren, 0, ty + th, g_daw.tcp_width, ty + th);
    }
    SDL_RenderSetClipRect(ren, NULL);

    SDL_SetRenderDrawColor(ren, 45, 45, 45, 255);
    SDL_RenderDrawLine(ren, g_daw.tcp_width - 1, TABBAR_HEIGHT + TOPBAR_HEIGHT, g_daw.tcp_width - 1, win_h);

    // Scrollbars
    SDL_Rect bar_h = { g_daw.tcp_width, win_h - SCROLLBAR_SIZE, view_w, SCROLLBAR_SIZE };
    SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
    SDL_RenderFillRect(ren, &bar_h);

    int total_w_timeline = (int)(p->project_duration_sec * p->pixels_per_second);
    float thumb_w_sz = (total_w_timeline > 0) ? fmaxf(30.0f, (float)view_w * ((float)view_w / total_w_timeline)) : view_w;
    double max_scroll_sec = (total_w_timeline > view_w) ? (double)(total_w_timeline - view_w) / p->pixels_per_second : 0.0001;
    float thumb_x = g_daw.tcp_width + (max_scroll_sec > 0 ? (float)(p->scroll_x_sec / max_scroll_sec) * (view_w - thumb_w_sz) : 0);
    SDL_Rect thumb_h = { (int)thumb_x, win_h - SCROLLBAR_SIZE + 2, (int)thumb_w_sz, SCROLLBAR_SIZE - 4 };
    SDL_SetRenderDrawColor(ren, 65, 65, 65, 255);
    SDL_RenderFillRect(ren, &thumb_h);

    SDL_Rect bar_v = { win_w - SCROLLBAR_SIZE, header_top_y, SCROLLBAR_SIZE, view_h };
    SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
    SDL_RenderFillRect(ren, &bar_v);

    float thumb_h_sz = (max_h_tracks > 0) ? fmaxf(30.0f, (float)view_h * ((float)view_h / max_h_tracks)) : view_h;
    float thumb_y = header_top_y + (max_h_tracks > view_h ? ((float)p->scroll_y_px / (max_h_tracks - view_h)) * (view_h - thumb_h_sz) : 0);
    SDL_Rect thumb_v = { win_w - SCROLLBAR_SIZE + 2, (int)thumb_y, SCROLLBAR_SIZE - 4, (int)thumb_h_sz };
    SDL_SetRenderDrawColor(ren, 65, 65, 65, 255);
    SDL_RenderFillRect(ren, &thumb_v);

    // 7. Popups & Menus
    if (g_daw.menu_action_open) {
        int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
        SDL_Rect a_box = { 56, m_start, 134, 56 };
        SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
        SDL_RenderFillRect(ren, &a_box);
        SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
        SDL_RenderDrawRect(ren, &a_box);
        draw_text(ren, "Lua Scripts...", 64, m_start + 8, c_white);
        draw_text(ren, "Python Scripts...", 64, m_start + 32, c_white);
    }

    if (g_daw.script_manager_open) {
        SDL_Rect *sr = &g_daw.script_win_rect;
        SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
        SDL_RenderFillRect(ren, sr);
        SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
        SDL_RenderDrawRect(ren, sr);

        SDL_Rect s_hdr = { sr->x, sr->y, sr->w, 26 };
        SDL_SetRenderDrawColor(ren, 36, 36, 36, 255);
        SDL_RenderFillRect(ren, &s_hdr);

        char win_title[64];
        snprintf(win_title, sizeof(win_title), "%s Script Manager", g_daw.script_manager_is_python ? "Python" : "Lua");
        draw_text(ren, win_title, sr->x + 8, sr->y + 8, c_white);
        draw_text(ren, "X", sr->x + sr->w - 18, sr->y + 8, c_white);

        SDL_Rect list_box = { sr->x + 12, sr->y + 34, sr->w - 24, 180 };
        SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
        SDL_RenderFillRect(ren, &list_box);

        for (int s = 0; s < g_daw.num_scripts; s++) {
            int sy = sr->y + 38 + (s * 22);
            if (s == g_daw.script_selected_idx) {
                SDL_Rect s_row = { sr->x + 14, sy - 2, sr->w - 28, 20 };
                SDL_SetRenderDrawColor(ren, 48, 48, 48, 255);
                SDL_RenderFillRect(ren, &s_row);
            }
            draw_text(ren, g_daw.scripts[s].name, sr->x + 18, sy + 2, c_white);
            draw_text(ren, g_daw.scripts[s].shortcut, sr->x + sr->w - 70, sy + 2, c_white);
        }

        int by = sr->y + sr->h - 38;
        SDL_Rect btn_add_sc = { sr->x + 12, by, 92, 28 };
        SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
        SDL_RenderFillRect(ren, &btn_add_sc);
        draw_text(ren, "Add Script", sr->x + 18, by + 8, c_white);

        SDL_Rect btn_run_sc = { sr->x + 114, by, 92, 28 };
        SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
        SDL_RenderFillRect(ren, &btn_run_sc);
        draw_text(ren, "Run Script", sr->x + 120, by + 8, c_white);

        SDL_Rect btn_del_sc = { sr->x + 216, by, 92, 28 };
        SDL_SetRenderDrawColor(ren, 44, 44, 44, 255);
        SDL_RenderFillRect(ren, &btn_del_sc);
        draw_text(ren, "Delete", sr->x + 236, by + 8, c_white);
    }

    if (g_daw.menu_file_open) {
        int m_start = TABBAR_HEIGHT + TOPBAR_HEIGHT;
        SDL_Rect m_box = { 8, m_start, 192, 146 };
        SDL_SetRenderDrawColor(ren, 24, 24, 24, 255);
        SDL_RenderFillRect(ren, &m_box);
        SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
        SDL_RenderDrawRect(ren, &m_box);

        draw_text(ren, "Import Project (.ehs)", 16, m_start + 6, c_white);
        draw_text(ren, "Export Project (.ehs)", 16, m_start + 30, c_white);
        draw_text(ren, "Import Media", 16, m_start + 54, c_white);
        draw_text(ren, "Export Media -> Audio >", 16, m_start + 78, c_white);
        draw_text(ren, "Export Media -> Video >", 16, m_start + 102, c_white);
        draw_text(ren, "Batch Export (Stems)", 16, m_start + 126, c_white);

        if (g_daw.submenu_export_audio_open) {
            SDL_Rect sub_a = { 200, m_start + 76, 110, 94 };
            SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
            SDL_RenderFillRect(ren, &sub_a);
            SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
            SDL_RenderDrawRect(ren, &sub_a);
            draw_text(ren, ".WAV Audio", 208, m_start + 82, c_white);
            draw_text(ren, ".MP3 Audio", 208, m_start + 104, c_white);
            draw_text(ren, ".FLAC Audio", 208, m_start + 126, c_white);
            draw_text(ren, ".OGG Audio", 208, m_start + 148, c_white);
        }

        if (g_daw.submenu_export_video_open) {
            SDL_Rect sub_v = { 200, m_start + 100, 110, 94 };
            SDL_SetRenderDrawColor(ren, 20, 20, 20, 255);
            SDL_RenderFillRect(ren, &sub_v);
            SDL_SetRenderDrawColor(ren, 60, 60, 60, 255);
            SDL_RenderDrawRect(ren, &sub_v);
            draw_text(ren, ".MP4 Video", 208, m_start + 106, c_white);
            draw_text(ren, ".MKV Video", 208, m_start + 128, c_white);
            draw_text(ren, ".MOV Video", 208, m_start + 150, c_white);
            draw_text(ren, ".AVI Video", 208, m_start + 172, c_white);
        }
    }

    if (p->video.is_open) {
        SDL_Rect s_rect = { p->video.rect.x + 4, p->video.rect.y + 4, p->video.rect.w, p->video.rect.h };
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 140);
        SDL_RenderFillRect(ren, &s_rect);

        SDL_Rect v_bar = { p->video.rect.x, p->video.rect.y, p->video.rect.w, 24 };
        SDL_SetRenderDrawColor(ren, 36, 36, 36, 255);
        SDL_RenderFillRect(ren, &v_bar);

        draw_text(ren, "Video Monitor", p->video.rect.x + 8, p->video.rect.y + 7, c_white);
        draw_text(ren, "X", p->video.rect.x + p->video.rect.w - 18, p->video.rect.y + 7, c_white);

        SDL_Rect v_canvas = { p->video.rect.x, p->video.rect.y + 24, p->video.rect.w, p->video.rect.h - 24 };
        if (p->video.texture) {
            SDL_RenderCopy(ren, p->video.texture, NULL, &v_canvas);
        } else {
            SDL_SetRenderDrawColor(ren, 12, 12, 12, 255);
            SDL_RenderFillRect(ren, &v_canvas);
            draw_text(ren, "No Video Stream Loaded", p->video.rect.x + 120, p->video.rect.y + 140, c_muted);
        }

        SDL_Rect resize_handle = { p->video.rect.x + p->video.rect.w - 12, p->video.rect.y + p->video.rect.h - 12, 10, 10 };
        SDL_SetRenderDrawColor(ren, 100, 100, 100, 255);
        SDL_RenderFillRect(ren, &resize_handle);
    }

    SDL_RenderPresent(ren);
}

#endif // DAW_UI_H
