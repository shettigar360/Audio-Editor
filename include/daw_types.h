typedef struct {
    ProjectTab tabs[MAX_TABS];
    int num_tabs;
    int active_tab;
    int tcp_width;

    SDL_AudioDeviceID audio_device;

    // Scripts
    DawScript scripts[MAX_SCRIPTS];
    int num_scripts;
    bool script_manager_open;
    bool script_manager_is_python;
    int script_selected_idx;
    SDL_Rect script_win_rect;
    bool script_win_dragging;
    int script_drag_offset_x;
    int script_drag_offset_y;

    // Menus
    bool menu_file_open;
    bool menu_action_open;
    bool submenu_export_audio_open;
    bool submenu_export_video_open;

    // Marquee Selection Box
    bool marquee_selecting;
    int marquee_start_x;
    int marquee_start_y;
    int marquee_cur_x;
    int marquee_cur_y;

    // Marker Selection
    int selected_marker_idx;

    // Exclusive Text Box Input State
    InputFocusType input_focus;
    int editing_track_name_idx;
    char editing_name_buf[64];
    int editing_marker_idx;
    char editing_marker_buf[64];
} DawEngine;

extern DawEngine g_daw;
