#pragma once
#include <TFT_eSPI.h>

void trash_app_init(TFT_eSPI* display);
void trash_app_open();
void trash_app_tick();
bool trash_app_handleTouch(bool pressed, bool lastPressed, int x, int y);
bool trash_app_is_open();
