// 1. Chrome-Style Project Tabs Bar [ x  Project N ]  [+]
    SDL_Rect tab_bar = { 0, 0, win_w, TABBAR_HEIGHT };
    SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
    SDL_RenderFillRect(ren, &tab_bar);

    const int tab_w = 140;
    for (int t = 0; t < g_daw.num_tabs; t++) {
        int tx = t * tab_w;
        bool is_act = (t == g_daw.active_tab);

        // Tab background
        SDL_Rect tr = { tx, 2, tab_w - 2, TABBAR_HEIGHT - 2 };
        SDL_SetRenderDrawColor(ren, is_act ? 34 : 22, is_act ? 34 : 22, is_act ? 34 : 22, 255);
        SDL_RenderFillRect(ren, &tr);

        // Tab outline
        SDL_SetRenderDrawColor(ren, is_act ? 50 : 28, is_act ? 50 : 28, is_act ? 50 : 28, 255);
        SDL_RenderDrawRect(ren, &tr);

        // 'x' close button box on the left
        SDL_Rect close_btn = { tx + 6, 7, 16, 16 };
        SDL_SetRenderDrawColor(ren, is_act ? 48 : 28, is_act ? 48 : 28, is_act ? 48 : 28, 255);
        SDL_RenderFillRect(ren, &close_btn);
        draw_text(ren, "x", tx + 10, 9, c_muted);

        // Project Title
        draw_text(ren, g_daw.tabs[t].project_name, tx + 28, 9, is_act ? c_white : c_muted);
    }

    // [+] Button to add new project tab
    int plus_x = g_daw.num_tabs * tab_w + 4;
    if (g_daw.num_tabs < MAX_TABS) {
        SDL_Rect btn_add_tab = { plus_x, 4, 26, TABBAR_HEIGHT - 6 };
        SDL_SetRenderDrawColor(ren, 30, 30, 30, 255);
        SDL_RenderFillRect(ren, &btn_add_tab);
        SDL_SetRenderDrawColor(ren, 50, 50, 50, 255);
        SDL_RenderDrawRect(ren, &btn_add_tab);
        draw_text(ren, "+", plus_x + 9, 8, c_white);
    }
