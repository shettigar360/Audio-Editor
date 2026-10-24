#ifndef DAW_TABS_H
#define DAW_TABS_H

#include "daw_types.h"

void init_tab_defaults(int idx, const char *title);
void force_close_tab(int idx);
void request_close_tab(int idx);
void handle_save_modal_choice(int choice, bool *running);

// Per-tab state synchronization hooks implemented in main.c and invoked by daw_tabs.c
void daw_shift_per_tab_arrays_on_close(int closed_idx, int old_num_tabs);
void daw_evict_unused_shared_peaks_after_tab_close(void);
void daw_prepare_tab_close_state(int idx);
bool daw_is_tab_unmodified_setup(int idx);
void daw_clear_open_project_after_save_flag(void);
void daw_cleanup_temp_media_for_tab(int idx);

#endif // DAW_TABS_H
