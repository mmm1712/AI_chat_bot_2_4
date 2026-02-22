#pragma once

#include <Arduino.h>

#define TOUCH_SCREEN_W 320
#define TOUCH_SCREEN_H 240

bool touch_is_pressed();

void touch_init();

bool touch_get(int &x, int &y);