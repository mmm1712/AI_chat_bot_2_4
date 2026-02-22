#pragma once
#include <TFT_eSPI.h>

void internet_app_init(TFT_eSPI* display);
bool internet_app_isOpen();
void internet_app_open();
void internet_app_tick();

bool internet_app_handleTouch(bool pressed, bool lastPressed, int x, int y);
