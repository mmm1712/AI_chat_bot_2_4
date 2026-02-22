#include "trash_app.h"
#include "trash_icon.h"
#include "paint_icon.h"
#include "internet_icon.h"
#include "notes_icon.h"
#include "wifi_icon.h"
#include "system_ui.h"
#include "trash_state.h"
#include <Arduino.h>

#ifdef LIST_H
#undef LIST_H
#endif

static TFT_eSPI* tft = nullptr;
static bool openState = false;

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const int HEADER_H = 26;
static const int LIST_Y = 64;
static const int LIST_H = 92;
static const int ROW_H = 40;

static uint32_t lastStatusTick = 0;
static int selectedIdx = -1;
static int listScroll = 0;

static inline bool inRect(int x,int y,int rx,int ry,int rw,int rh){
  return (x>=rx && x<rx+rw && y>=ry && y<ry+rh);
}

static void drawHeader() {
  tft->fillRect(0, 0, SCREEN_W, HEADER_H, 0x1C9F);
  tft->setTextColor(TFT_WHITE, 0x1C9F);
  tft->drawString("Trash", 8, 6, 2);

  tft->fillRect(SCREEN_W - 18, 5, 14, 14, TFT_RED);
  tft->drawRect(SCREEN_W - 18, 5, 14, 14, TFT_WHITE);
  tft->setTextColor(TFT_WHITE, TFT_RED);
  tft->drawCentreString("X", SCREEN_W - 11, 6, 2);

  system_ui_draw_status(SCREEN_W - 92 - 18, 5, 0x1C9F);
}

static void drawBin() {
  int x = SCREEN_W/2 - TRASH_ICON_WIDTH/2;
  int y = 70;
  tft->setSwapBytes(true);
  tft->pushImage(x, y, TRASH_ICON_WIDTH, TRASH_ICON_HEIGHT, trash_icon_map, 0x0000);

  if (trash_deleted_count() > 0) {
    // Fake papers
    tft->fillRect(x + 10, y + 6, 12, 6, TFT_WHITE);
    tft->fillRect(x + 18, y + 14, 10, 6, TFT_WHITE);
  }
}

static void drawButtons() {
  int y = LIST_Y + LIST_H + 8;
  tft->fillRoundRect(90, y, 140, 24, 3, 0x047F);
  tft->drawRoundRect(90, y, 140, 24, 3, TFT_WHITE);
  tft->setTextColor(TFT_WHITE, 0x047F);
  tft->drawCentreString("RESTORE", 160, y + 5, 2);
}

static void drawStatus() {
  tft->fillRect(0, 40, SCREEN_W, 20, TFT_WHITE);
  tft->setTextColor(TFT_BLACK, TFT_WHITE);
  uint8_t count = trash_deleted_count();
  if (count == 0) {
    tft->drawCentreString("Trash is empty", SCREEN_W/2, 42, 2);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "Items: %d", count);
    tft->drawCentreString(buf, SCREEN_W/2, 42, 2);
  }
}

static void drawIconForId(TrashIconId id, int x, int y) {
  tft->setSwapBytes(true);
  if (id == ICON_AI) {
    tft->fillRoundRect(x, y, 28, 28, 5, 0x1C9F);
    tft->drawRoundRect(x, y, 28, 28, 5, TFT_WHITE);
    tft->setTextColor(TFT_WHITE, 0x1C9F);
    tft->drawCentreString("AI", x + 14, y + 6, 2);
    return;
  }
  if (id == ICON_PAINT) {
    tft->pushImage(x, y, PAINT_ICON_WIDTH, PAINT_ICON_HEIGHT, paint_icon_map, 0x0000);
    return;
  }
  if (id == ICON_INTERNET) {
    tft->pushImage(x, y, INTERNET_ICON_WIDTH, INTERNET_ICON_HEIGHT, internet_icon_map, 0x0000);
    return;
  }
  if (id == ICON_NOTES) {
    tft->pushImage(x, y, NOTES_ICON_WIDTH, NOTES_ICON_HEIGHT, notes_icon_map, 0x0000);
    return;
  }
  if (id == ICON_WIFI) {
    tft->pushImage(x, y, WIFI_ICON_WIDTH, WIFI_ICON_HEIGHT, wifi_icon_map, 0x0000);
    return;
  }
}

static int deletedCount() {
  return (int)trash_deleted_count();
}

static int iconIdByRow(int row) {
  int idx = 0;
  for (int i = 0; i < ICON_COUNT; i++) {
    if (!trash_is_deleted((TrashIconId)i)) continue;
    if (idx == row) return i;
    idx++;
  }
  return -1;
}

static void drawList() {
  tft->fillRect(20, LIST_Y, SCREEN_W - 40, LIST_H, TFT_WHITE);
  tft->drawRect(20, LIST_Y, SCREEN_W - 40, LIST_H, 0x7BEF);
  tft->setTextColor(TFT_BLACK, TFT_WHITE);

  int count = deletedCount();
  if (count == 0) {
    selectedIdx = -1;
    listScroll = 0;
    return;
  }
  if (selectedIdx >= count) selectedIdx = -1;

  const char* names[ICON_COUNT] = {"AI Chat", "Paint", "Internet", "Notes", "WiFi"};
  int maxRows = LIST_H / ROW_H;
  if (listScroll < 0) listScroll = 0;
  if (listScroll > count - maxRows) listScroll = max(0, count - maxRows);

  for (int row = 0; row < maxRows; row++) {
    int globalRow = listScroll + row;
    int id = iconIdByRow(globalRow);
    if (id < 0) break;
    int y = LIST_Y + 4 + row * ROW_H;
    if (globalRow == selectedIdx) {
      tft->fillRect(24, y - 2, SCREEN_W - 48, ROW_H - 4, 0x1C9F);
      tft->setTextColor(TFT_WHITE, 0x1C9F);
    } else {
      tft->setTextColor(TFT_BLACK, TFT_WHITE);
    }
    drawIconForId((TrashIconId)id, 30, y);
    tft->drawString(names[id], 70, y + 8, 2);
  }

  // scroll hints
  if (count > maxRows) {
    int cx = SCREEN_W - 30;
    int upY = LIST_Y + 6;
    int dnY = LIST_Y + LIST_H - 10;
    tft->drawTriangle(cx, upY + 6, cx - 6, upY + 12, cx + 6, upY + 12, 0x7BEF);
    tft->drawTriangle(cx, dnY + 6, cx - 6, dnY, cx + 6, dnY, 0x7BEF);
  }
}

void trash_app_init(TFT_eSPI* display) {
  tft = display;
}

void trash_app_open() {
  if (!tft) return;
  openState = true;
  selectedIdx = -1;
  listScroll = 0;
  tft->fillScreen(TFT_WHITE);
  drawHeader();
  drawStatus();
  drawBin();
  drawList();
  drawButtons();
}

void trash_app_tick() {
  if (!openState) return;
  uint32_t now = millis();
  if (now - lastStatusTick > 900) {
    system_ui_tick(SCREEN_W - 92 - 18, 5, 0x1C9F);
    lastStatusTick = now;
  }
}

bool trash_app_handleTouch(bool pressed, bool lastPressed, int x, int y) {
  if (!openState || !tft) return false;

  // Close (larger hitbox for reliability)
  if (pressed && !lastPressed && inRect(x, y, SCREEN_W - 28, 2, 24, 20)) {
    openState = false;
    return false;
  }

  if (pressed && !lastPressed && inRect(x, y, 90, LIST_Y + LIST_H + 8, 140, 24)) {
    if (selectedIdx >= 0) {
      int id = iconIdByRow(selectedIdx);
      if (id >= 0) trash_restore_icon((TrashIconId)id);
      selectedIdx = -1;
      drawStatus();
      drawBin();
      drawList();
    }
    return true;
  }

  if (pressed && !lastPressed && inRect(x, y, 20, LIST_Y, SCREEN_W - 40, LIST_H)) {
    int count = deletedCount();
    int maxRows = LIST_H / ROW_H;
    if (count > maxRows) {
      int upZoneY = LIST_Y + 2;
      int dnZoneY = LIST_Y + LIST_H - 12;
      if (y < upZoneY + 12) {
        listScroll = max(0, listScroll - 1);
        drawList();
        return true;
      }
      if (y > dnZoneY) {
        listScroll = min(max(0, count - maxRows), listScroll + 1);
        drawList();
        return true;
      }
    }
    int relY = y - (LIST_Y + 4);
    int row = relY / ROW_H;
    if (row < 0) row = 0;
    selectedIdx = listScroll + row;
    drawList();
    return true;
  }

  return true;
}

bool trash_app_is_open() {
  return openState;
}
