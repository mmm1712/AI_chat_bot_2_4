#pragma once
#include <TFT_eSPI.h>

void settings_app_init(TFT_eSPI* display);
void settings_app_open();
void settings_app_tick();
bool settings_app_handleTouch(bool pressed, bool lastPressed, int x, int y);
bool settings_app_is_open();
