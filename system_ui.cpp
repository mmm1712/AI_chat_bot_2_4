#include "system_ui.h"
#include <WiFi.h>
#include <Arduino.h>
#include <time.h>

static TFT_eSPI* tft = nullptr;

static wl_status_t lastWifi = WL_DISCONNECTED;
static uint32_t lastClockSec = 0;
static char lastClock[6] = "00:00";
static uint16_t lastClockBg = 0x047F;
static bool manualTime = false;
static int manualHour = 0;
static int manualMin = 0;
static uint32_t manualSetMs = 0;

static const int STATUS_W = 90;
static const int STATUS_H = 16;

static void drawBattery(int x, int y, uint16_t fg, uint16_t bg) {
  int w = 22, h = 10;
  tft->fillRect(x, y, w + 2, h, bg);
  tft->drawRect(x, y, w, h, fg);
  tft->fillRect(x + w, y + 3, 2, h - 6, fg);

  int level = 3; // fake full
  int fillW = (w - 4) * level / 3;
  tft->fillRect(x + 2, y + 2, fillW, h - 4, fg);
  if (fillW < w - 4) {
    tft->fillRect(x + 2 + fillW, y + 2, (w - 4) - fillW, h - 4, bg);
  }
}

static void drawWifi(int x, int y, bool connected, uint16_t fg, uint16_t bg) {
  tft->fillRect(x - 1, y - 1, 16, 16, bg);
  uint16_t c = fg;
  tft->drawPixel(x + 7, y + 7, c);
  tft->drawCircle(x + 7, y + 7, 3, c);
  tft->drawCircle(x + 7, y + 7, 5, c);
  tft->drawCircle(x + 7, y + 7, 7, c);
  if (!connected) {
    tft->drawLine(x, y + 12, x + 14, y - 2, 0xF800);
  }
}

static void drawClock(int x, int y, uint16_t fg, uint16_t bg) {
  char buf[6] = "--:--";
  if (manualTime) {
    uint32_t elapsedMin = (millis() - manualSetMs) / 60000;
    int totalMin = manualHour * 60 + manualMin + (int)elapsedMin;
    totalMin %= (24 * 60);
    int hh = totalMin / 60;
    int mm = totalMin % 60;
    snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
  } else {
    time_t now = time(nullptr);
    struct tm timeinfo;
    if (localtime_r(&now, &timeinfo)) {
      snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    }
  }
  // Overdraw previous time using the caller background, then draw new time
  tft->setTextColor(bg, bg);
  tft->drawString(lastClock, x, y, 2);
  tft->setTextColor(fg, bg);
  tft->drawString(buf, x, y, 2);
  strncpy(lastClock, buf, sizeof(lastClock) - 1);
  lastClock[sizeof(lastClock) - 1] = 0;
}

void system_ui_init(TFT_eSPI* display) {
  tft = display;
}

void system_ui_time_begin() {
  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();
}

void system_ui_time_manual_sync() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

void system_ui_time_set_manual(int hour, int minute) {
  if (hour < 0) hour = 0;
  if (hour > 23) hour = 23;
  if (minute < 0) minute = 0;
  if (minute > 59) minute = 59;
  manualTime = true;
  manualHour = hour;
  manualMin = minute;
  manualSetMs = millis();
}

void system_ui_time_get(int* hour, int* minute) {
  if (!hour || !minute) return;
  if (manualTime) {
    uint32_t elapsedMin = (millis() - manualSetMs) / 60000;
    int totalMin = manualHour * 60 + manualMin + (int)elapsedMin;
    totalMin %= (24 * 60);
    *hour = totalMin / 60;
    *minute = totalMin % 60;
    return;
  }
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (localtime_r(&now, &timeinfo)) {
    *hour = timeinfo.tm_hour;
    *minute = timeinfo.tm_min;
  } else {
    *hour = 0;
    *minute = 0;
  }
}

void system_ui_draw_status(int x, int y, uint16_t bg) {
  if (!tft) return;
  // Clear a small status strip to avoid stale pixels.
  tft->fillRect(x, y, STATUS_W, STATUS_H, bg);
  bool connected = (WiFi.status() == WL_CONNECTED);
  drawWifi(x + 2, y + 2, connected, TFT_WHITE, bg);
  drawBattery(x + 22, y + 3, TFT_WHITE, bg);
  drawClock(x + 48, y + 1, TFT_WHITE, bg);

  lastWifi = WiFi.status();
  lastClockSec = millis() / 1000;
}

void system_ui_tick(int x, int y, uint16_t bg) {
  if (!tft) return;
  uint32_t nowSec = millis() / 1000;
  wl_status_t cur = WiFi.status();

  if (cur != lastWifi || nowSec != lastClockSec) {
    system_ui_draw_status(x, y, bg);
  }
}
