#pragma once
#include <TFT_eSPI.h>

void notes_app_init(TFT_eSPI* display);
void notes_app_open();
void notes_app_tick();
bool notes_app_handleTouch(bool pressed, bool lastPressed, int x, int y);
bool notes_app_is_open();
void notes_app_scroll_steps(int steps);
