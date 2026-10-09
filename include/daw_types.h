static inline void init_tab_defaults(int idx, const char *title) {
    ProjectTab *p = &g_daw.tabs[idx];
    memset(p, 0, sizeof(ProjectTab));
    snprintf(p->project_name, sizeof(p->project_name), "%s", title);
    p->num_tracks = 4;
    p->pixels_per_second = 80.0f;
    p->project_duration_sec = 300.0;
    p->show_marker_lane = true;
    p->video = (SimpleVideoPlayer){ .rect = { 650, 160, 480, 310 }, .is_open = false, .video_stream_idx = -1 };

    for (int t = 0; t < MAX_TRACKS; t++) {
        p->tracks[t].id = t + 1;
        p->tracks[t].volume = 1.0f;
        p->track_db[t] = 0.0f;
        p->track_heights[t] = 76;
        snprintf(p->track_names[t], sizeof(p->track_names[t]), "Track %d", t + 1);
    }
}

static inline void close_tab(int idx) {
    if (idx < 0 || idx >= g_daw.num_tabs) return;

    ProjectTab *target = &g_daw.tabs[idx];
    close_video(&target->video);
    for (int i = 0; i < target->num_items; i++) {
        if (target->items[i].raw_file_handle) fclose(target->items[i].raw_file_handle);
        if (target->items[i].peaks) free(target->items[i].peaks);
    }

    if (g_daw.num_tabs <= 1) {
        init_tab_defaults(0, "Project 1");
        g_daw.active_tab = 0;
        return;
    }

    for (int t = idx; t < g_daw.num_tabs - 1; t++) {
        g_daw.tabs[t] = g_daw.tabs[t + 1];
    }
    g_daw.num_tabs--;

    if (g_daw.active_tab >= g_daw.num_tabs) {
        g_daw.active_tab = g_daw.num_tabs - 1;
    }
}

#endif // DAW_TYPES_H
