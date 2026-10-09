// Tab Bar Interaction (Strict hit-testing with immediate return)
        if (my < TABBAR_HEIGHT) {
            const int tab_w = 140;
            int total_tabs = g_daw.num_tabs;

            // 1. Check clicks on existing tabs
            for (int t = 0; t < total_tabs; t++) {
                int tx = t * tab_w;
                if (mx >= tx && mx < tx + tab_w - 2) {
                    // Clicked on left-side 'x' button (tx + 6 to tx + 22)
                    if (mx >= tx + 6 && mx <= tx + 22 && my >= 5 && my <= 23) {
                        close_tab(t);
                    } else {
                        // Clicked anywhere else on tab -> switch active tab
                        g_daw.active_tab = t;
                    }
                    return; // Crucial: Stop processing to avoid misclicking the + button
                }
            }

            // 2. Check click on [+] button
            int plus_x = total_tabs * tab_w + 4;
            if (g_daw.num_tabs < MAX_TABS && mx >= plus_x && mx <= plus_x + 26 && my >= 4 && my <= TABBAR_HEIGHT - 2) {
                int new_idx = g_daw.num_tabs;
                char name[64];
                snprintf(name, sizeof(name), "Project %d", new_idx + 1);
                init_tab_defaults(new_idx, name);
                g_daw.num_tabs++;
                g_daw.active_tab = new_idx;
                return;
            }
            return;
        }
