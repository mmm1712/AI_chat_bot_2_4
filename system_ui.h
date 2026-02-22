#pragma once
#include <TFT_eSPI.h>

// Init once with display pointer
void system_ui_init(TFT_eSPI* display);
void system_ui_time_begin();
void system_ui_time_manual_sync();
void system_ui_time_set_manual(int hour, int minute);
void system_ui_time_get(int* hour, int* minute);

// Draw status (time only) in a small right-aligned block.
// x,y define the top-left of the status block.
void system_ui_draw_status(int x, int y, uint16_t bg);

// Redraw only when state changes or every second for clock.
void system_ui_tick(int x, int y, uint16_t bg);
