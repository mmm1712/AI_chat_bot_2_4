#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

enum DesktopAction {
  DESKTOP_NONE = 0,

  DESKTOP_OPEN_CHAT,
  DESKTOP_OPEN_PAINT,
  DESKTOP_OPEN_TRASH,
  DESKTOP_OPEN_INTERNET,
  DESKTOP_OPEN_NOTES,
  DESKTOP_OPEN_WIFI,
  DESKTOP_OPEN_SETTINGS,

  DESKTOP_PROPERTIES_CHAT,
  DESKTOP_PROPERTIES_PAINT,
  DESKTOP_PROPERTIES_TRASH,
  DESKTOP_PROPERTIES_INTERNET,
  DESKTOP_PROPERTIES_NOTES,
  DESKTOP_PROPERTIES_WIFI,
  DESKTOP_PROPERTIES_SETTINGS
};

void desktop_init(TFT_eSPI* display);
void desktop_draw();
void desktop_tick();
void desktop_set_mouse_mode(bool on);

DesktopAction desktop_handleTouch(bool pressed, bool lastPressed, int x, int y);

// Debug mouse cursor on desktop
void desktop_cursor_move(int x, int y);
void desktop_cursor_hide();
void desktop_mouse_indicator(bool on);
