#ifndef DAW_TABS_H
#define DAW_TABS_H

#include "daw_types.h"

void init_tab_defaults(int idx, const char *title);
void force_close_tab(int idx);
void request_close_tab(int idx);
void handle_save_modal_choice(int choice, bool *running);

#endif // DAW_TABS_H
