#pragma once
#include <TFT_eSPI.h>

void paint_init(TFT_eSPI* display);
void paint_draw();
void paint_tick();
void paint_release();
bool paint_handleTouch(int x, int y);
