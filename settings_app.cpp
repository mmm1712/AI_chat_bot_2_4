#include "settings_app.h"
#include "settings_store.h"
#include "system_ui.h"
#include <Arduino.h>

static TFT_eSPI* tft = nullptr;
static bool openState = false;

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const int HEADER_H = 26;

static uint8_t brightness = 220;
static bool autoConnect = true;
static int manualHour = 0;
static int manualMinute = 0;
static const int SAVE_Y = 190;

static uint32_t lastStatusTick = 0;

static inline bool inRect(int x,int y,int rx,int ry,int rw,int rh){
  return (x>=rx && x<rx+rw && y>=ry && y<ry+rh);
}

static void drawHeader() {
  tft->fillRect(0, 0, SCREEN_W, HEADER_H, 0x1C9F);
  tft->setTextColor(TFT_WHITE, 0x1C9F);
  tft->drawString("Settings", 8, 6, 2);
  tft->fillRoundRect(SCREEN_W - 52, 5, 46, 16, 2, TFT_LIGHTGREY);
  tft->setTextColor(TFT_BLACK, TFT_LIGHTGREY);
  tft->drawCentreString("BACK", SCREEN_W - 29, 7, 1);

  system_ui_draw_status(SCREEN_W - 92, 5, 0x1C9F);
}

static void drawRowLabel(const char* label, int y) {
  tft->setTextColor(TFT_BLACK, TFT_WHITE);
  tft->drawString(label, 12, y, 2);
}

static void drawPmButton(int x, int y, const char* txt) {
  tft->fillRoundRect(x, y - 2, 28, 18, 3, TFT_WHITE);
  tft->drawRoundRect(x, y - 2, 28, 18, 3, TFT_BLACK);
  tft->setTextColor(TFT_BLACK, TFT_WHITE);
  tft->drawCentreString(txt, x + 14, y, 2);
}

static void drawBrightnessRow() {
  int y = 40;
  drawRowLabel("Brightness", y);

  drawPmButton(190, y, "-");
  drawPmButton(284, y, "+");

  tft->drawRect(220, y - 1, 60, 12, TFT_BLACK);
  int fillW = map(brightness, 20, 255, 2, 58);
  if (fillW < 2) fillW = 2;
  if (fillW > 58) fillW = 58;
  tft->fillRect(221, y, fillW, 10, 0x1C9F);
  if (fillW < 58) tft->fillRect(221 + fillW, y, 58 - fillW, 10, TFT_WHITE);
}

static void drawToggleRow(const char* label, bool on, int y) {
  drawRowLabel(label, y);
  uint16_t bg = on ? 0x07E0 : 0xC618;
  tft->fillRoundRect(230, y - 2, 70, 18, 3, bg);
  tft->setTextColor(TFT_BLACK, bg);
  tft->drawCentreString(on ? "ON" : "OFF", 265, y, 2);
}

static void drawTimeRow(const char* label, int value, int y) {
  drawRowLabel(label, y);
  drawPmButton(190, y, "-");
  drawPmButton(284, y, "+");
  tft->drawRect(230, y - 1, 48, 12, TFT_BLACK);
  tft->setTextColor(TFT_BLACK, TFT_WHITE);
  char buf[4];
  snprintf(buf, sizeof(buf), "%02d", value);
  tft->drawCentreString(buf, 254, y - 1, 2);
}

static void drawAll() {
  tft->fillScreen(TFT_WHITE);
  drawHeader();
  drawBrightnessRow();
  drawToggleRow("Auto WiFi", autoConnect, 80);
  drawTimeRow("Hour", manualHour, 120);
  drawTimeRow("Minute", manualMinute, 150);

  tft->fillRoundRect(110, SAVE_Y, 100, 22, 3, 0x07E0);
  tft->drawRoundRect(110, SAVE_Y, 100, 22, 3, TFT_BLACK);
  tft->setTextColor(TFT_WHITE, 0x07E0);
  tft->drawCentreString("SAVE", 160, SAVE_Y + 4, 2);
}

void settings_app_init(TFT_eSPI* display) {
  tft = display;
}

void settings_app_open() {
  if (!tft) return;
  openState = true;
  brightness = settings_get_brightness();
  autoConnect = settings_get_autoconnect();
  system_ui_time_get(&manualHour, &manualMinute);
  drawAll();
}

void settings_app_tick() {
  if (!openState) return;
  uint32_t now = millis();
  if (now - lastStatusTick > 900) {
    system_ui_tick(SCREEN_W - 92, 5, 0x1C9F);
    lastStatusTick = now;
  }
}

bool settings_app_handleTouch(bool pressed, bool lastPressed, int x, int y) {
  if (!openState || !tft) return false;

  if (pressed && !lastPressed && inRect(x, y, SCREEN_W - 52, 5, 46, 16)) {
    openState = false;
    return false;
  }

  if (pressed && !lastPressed && inRect(x, y, 190, 38, 28, 18)) {
    if (brightness > 20) brightness -= 10;
    settings_set_brightness(brightness);
    drawBrightnessRow();
    return true;
  }
  if (pressed && !lastPressed && inRect(x, y, 284, 38, 28, 18)) {
    if (brightness < 255) brightness += 10;
    settings_set_brightness(brightness);
    drawBrightnessRow();
    return true;
  }

  if (pressed && !lastPressed && inRect(x, y, 230, 78, 70, 18)) {
    autoConnect = !autoConnect;
    settings_set_autoconnect(autoConnect);
    drawToggleRow("Auto WiFi", autoConnect, 80);
    return true;
  }

  if (pressed && !lastPressed && inRect(x, y, 190, 118, 28, 18)) {
    if (manualHour > 0) manualHour -= 1; else manualHour = 23;
    system_ui_time_set_manual(manualHour, manualMinute);
    drawTimeRow("Hour", manualHour, 120);
    return true;
  }
  if (pressed && !lastPressed && inRect(x, y, 284, 118, 28, 18)) {
    if (manualHour < 23) manualHour += 1; else manualHour = 0;
    system_ui_time_set_manual(manualHour, manualMinute);
    drawTimeRow("Hour", manualHour, 120);
    return true;
  }
  if (pressed && !lastPressed && inRect(x, y, 190, 148, 28, 18)) {
    if (manualMinute > 0) manualMinute -= 1; else manualMinute = 59;
    system_ui_time_set_manual(manualHour, manualMinute);
    drawTimeRow("Minute", manualMinute, 150);
    return true;
  }
  if (pressed && !lastPressed && inRect(x, y, 284, 148, 28, 18)) {
    if (manualMinute < 59) manualMinute += 1; else manualMinute = 0;
    system_ui_time_set_manual(manualHour, manualMinute);
    drawTimeRow("Minute", manualMinute, 150);
    return true;
  }

  if (pressed && !lastPressed && inRect(x, y, 110, SAVE_Y, 100, 22)) {
    openState = false;
    return false;
  }

  return true;
}

bool settings_app_is_open() {
  return openState;
}
