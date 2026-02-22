#pragma once
#include <TFT_eSPI.h>

#ifndef KB_TEXT_MAX
#define KB_TEXT_MAX 256
#endif

#ifndef KB_Y
#define KB_Y 140
#endif

typedef enum {
  KB_NONE = 0,
  KB_CHANGED,
  KB_REDRAW,
  KB_HIDE
} KB_Action;

void keyboard_init(TFT_eSPI *display);
KB_Action keyboard_update(bool pressed, int x, int y);

void keyboard_set_visible(bool v);
bool keyboard_is_visible();

void keyboard_clear();
const char* keyboard_get_text();
void keyboard_set_text(const char* s);

void keyboard_draw();

KB_Action keyboard_touch(int x, int y);

void keyboard_release();
KB_Action keyboard_tick(bool pressed, int x, int y);
