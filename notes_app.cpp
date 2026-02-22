#include "notes_app.h"
#include "keyboard.h"
#include "system_ui.h"
#include <Preferences.h>
#include <Arduino.h>

static TFT_eSPI* tft = nullptr;

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;

static bool openState = false;
static bool kbVisible = true;

static const int HEADER_H = 20;
static const int MENU_H = 16;
static const int TOOL_H = 0;
static const int STATUS_H = 14;
static const int PAD = 6;

static int textTop = HEADER_H + MENU_H + 2;
static int textBottom = SCREEN_H - STATUS_H - 2;

static int scrollLine = 0;
static int totalLines = 0;
static int visibleLines = 0;

static const int LINE_H = 16;
static const int TEXT_X = 6;
static const int TEXT_W = 308;

static const char* NVS_NS = "notes";
static const char* NVS_KEY = "text";

static uint32_t lastStatusTick = 0;
static bool dragging = false;
static int dragLastY = 0;
static int dragAccum = 0;
static const char* statusMsg = "";
static uint32_t statusMsgUntil = 0;
static int statusLine = 1;
static int statusCol = 1;

#define NOTES_MAX KB_TEXT_MAX
#define WRAP_MAX_LINES 100
#define WRAP_LINE_MAX 44

static char notesText[NOTES_MAX + 1];
static char wrapLinesBuf[WRAP_MAX_LINES][WRAP_LINE_MAX];
static int wrapCount = 0;

static inline bool inRect(int x,int y,int rx,int ry,int rw,int rh){
  return (x>=rx && x<rx+rw && y>=ry && y<ry+rh);
}

static void nvsLoad() {
  Preferences p;
  p.begin(NVS_NS, true);
  size_t len = p.getBytesLength(NVS_KEY);
  if (len > 0 && len <= (size_t)(NOTES_MAX + 1)) {
    p.getBytes(NVS_KEY, notesText, len);
    notesText[min((size_t)NOTES_MAX, len - 1)] = 0;
  } else {
    notesText[0] = 0;
  }
  p.end();
}

static void nvsSave() {
  Preferences p;
  p.begin(NVS_NS, false);
  size_t len = strnlen(notesText, NOTES_MAX);
  notesText[len] = 0;
  p.putBytes(NVS_KEY, notesText, len + 1);
  p.end();
}

static void wrapText(const char* s, int maxW) {
  wrapCount = 0;
  char line[WRAP_LINE_MAX] = {0};
  char word[WRAP_LINE_MAX] = {0};
  int lineLen = 0;
  int wordLen = 0;

  auto pushLine = [&](const char* ln) {
    if (wrapCount >= WRAP_MAX_LINES) return;
    strncpy(wrapLinesBuf[wrapCount], ln, WRAP_LINE_MAX - 1);
    wrapLinesBuf[wrapCount][WRAP_LINE_MAX - 1] = 0;
    wrapCount++;
  };

  auto flushWord = [&]() {
    if (wordLen == 0) return;

    if (lineLen == 0 && tft->textWidth(word, 2) > maxW) {
      char chunk[WRAP_LINE_MAX] = {0};
      int chunkLen = 0;
      for (int i = 0; i < wordLen; i++) {
        if (chunkLen >= WRAP_LINE_MAX - 1) {
          pushLine(chunk);
          chunk[0] = 0;
          chunkLen = 0;
        }
        chunk[chunkLen++] = word[i];
        chunk[chunkLen] = 0;
        if (tft->textWidth(chunk, 2) > maxW) {
          chunk[chunkLen - 1] = 0;
          pushLine(chunk);
          chunk[0] = word[i];
          chunk[1] = 0;
          chunkLen = 1;
        }
      }
      if (chunkLen > 0) pushLine(chunk);
      word[0] = 0;
      wordLen = 0;
      return;
    }

    char test[WRAP_LINE_MAX] = {0};
    if (lineLen == 0) {
      strncpy(test, word, WRAP_LINE_MAX - 1);
    } else {
      snprintf(test, sizeof(test), "%s %s", line, word);
    }

    if (tft->textWidth(test, 2) <= maxW) {
      strncpy(line, test, WRAP_LINE_MAX - 1);
      line[WRAP_LINE_MAX - 1] = 0;
      lineLen = strlen(line);
    } else {
      if (lineLen > 0) pushLine(line);
      strncpy(line, word, WRAP_LINE_MAX - 1);
      line[WRAP_LINE_MAX - 1] = 0;
      lineLen = wordLen;
    }

    word[0] = 0;
    wordLen = 0;
  };

  for (int i = 0; s[i] != 0; i++) {
    char c = s[i];
    if (c == '\n') {
      flushWord();
      if (lineLen > 0) pushLine(line);
      line[0] = 0;
      lineLen = 0;
      continue;
    }
    if (c == ' ') {
      flushWord();
      continue;
    }
    if (wordLen >= WRAP_LINE_MAX - 1) flushWord();
    word[wordLen++] = c;
    word[wordLen] = 0;
  }

  flushWord();
  if (lineLen > 0) pushLine(line);
}

static void drawHeader() {
  tft->fillRect(0, 0, SCREEN_W, HEADER_H, 0x047F);
  tft->fillRect(0, 0, SCREEN_W, HEADER_H/2, 0x1C9F);
  tft->setTextColor(TFT_WHITE, 0x047F);
  tft->drawString("Notepad", 8, 3, 2);

  tft->fillRect(SCREEN_W - 18, 1, 16, 16, TFT_RED);
  tft->drawRect(SCREEN_W - 18, 1, 16, 16, TFT_WHITE);
  tft->setTextColor(TFT_WHITE, TFT_RED);
  tft->drawCentreString("X", SCREEN_W - 10, 2, 2);

  system_ui_draw_status(SCREEN_W - 92 - 22, 2, 0x047F);
}

static void drawMenuBar() {
  int y = HEADER_H;
  tft->fillRect(0, y, SCREEN_W, MENU_H, 0xE71C);
  tft->setTextColor(TFT_BLACK, 0xE71C);
  tft->drawString("Save", 6, y + 3, 1);
  tft->drawString("Load", 52, y + 3, 1);
  tft->drawString("Clear", 98, y + 3, 1);
  tft->drawString(kbVisible ? "Hide" : "Show", 152, y + 3, 1);
}

static void drawToolbar() {
  // removed toolbar row
  return;
}

static void drawStatusBar() {
  int y = kbVisible ? (KB_Y - STATUS_H - 2) : (SCREEN_H - STATUS_H);
  tft->fillRect(0, y, SCREEN_W, STATUS_H, 0xE71C);
  tft->drawFastHLine(0, y, SCREEN_W, 0x7BEF);
  tft->setTextColor(TFT_BLACK, 0xE71C);
  if (statusMsg && statusMsg[0]) {
    tft->drawString(statusMsg, 6, y + 2, 1);
  }
  // No Ln/Col per request
}
static void drawTextArea() {
  tft->fillRect(0, textTop, SCREEN_W, textBottom - textTop, TFT_WHITE);

  const char* text = keyboard_get_text();
  wrapText(text, TEXT_W);

  visibleLines = (textBottom - textTop) / LINE_H;
  totalLines = max(1, wrapCount);
  int maxScroll = max(0, totalLines - visibleLines);
  scrollLine = constrain(scrollLine, 0, maxScroll);

  int first = scrollLine;
  int last = scrollLine + visibleLines;

  int y = textTop + 2;
  int lineIndex = 0;
  tft->setTextColor(TFT_BLACK, TFT_WHITE);

  for (int i = 0; i < wrapCount; i++) {
    if (lineIndex >= first && lineIndex < last) {
      tft->drawString(wrapLinesBuf[i], TEXT_X, y, 2);
      y += LINE_H;
    }
    lineIndex++;
    if (lineIndex >= last) break;
  }
}

void notes_app_scroll_steps(int steps) {
  int maxScroll = max(0, totalLines - visibleLines);
  scrollLine = constrain(scrollLine - steps, 0, maxScroll);
  drawTextArea();
}

void notes_app_init(TFT_eSPI* display) {
  tft = display;
}

void notes_app_open() {
  if (!tft) return;
  openState = true;

  nvsLoad();
  keyboard_set_text(notesText);
  kbVisible = true;
  keyboard_set_visible(true);
  scrollLine = 0;

  textBottom = SCREEN_H - STATUS_H - 2;
  if (kbVisible) textBottom = KB_Y - STATUS_H - 2;

  tft->fillScreen(TFT_WHITE);
  drawHeader();
  drawMenuBar();
  drawToolbar();
  drawTextArea();
  drawStatusBar();
  keyboard_draw();
}

void notes_app_tick() {
  if (!openState) return;
  uint32_t now = millis();
  if (now - lastStatusTick > 900) {
    system_ui_tick(SCREEN_W - 92 - 22, 2, 0x047F);
    lastStatusTick = now;
  }
  if (statusMsgUntil && now > statusMsgUntil) {
    statusMsgUntil = 0;
    statusMsg = "";
    drawStatusBar();
  }
}

bool notes_app_handleTouch(bool pressed, bool lastPressed, int x, int y) {
  if (!openState || !tft) return false;

  if (pressed && kbVisible) {
    KB_Action tickA = keyboard_tick(true, x, y);
    if (tickA == KB_CHANGED) {
      drawTextArea();
      return true;
    }
  }

  if (pressed && !lastPressed && inRect(x, y, SCREEN_W - 52, 5, 46, 16)) {
    openState = false;
    return false;
  }

  int ty = HEADER_H;
  if (pressed && !lastPressed && inRect(x, y, SCREEN_W - 18, 1, 16, 16)) {
    // Auto-save on exit
    strncpy(notesText, keyboard_get_text(), NOTES_MAX);
    notesText[NOTES_MAX] = 0;
    nvsSave();
    openState = false;
    return false;
  }

  int menuY = HEADER_H;
  // Save/Load/Clear/Hide menu items
  if (pressed && !lastPressed && inRect(x, y, 4, menuY + 2, 42, MENU_H)) {
    strncpy(notesText, keyboard_get_text(), NOTES_MAX);
    notesText[NOTES_MAX] = 0;
    nvsSave();
    statusMsg = "Saved";
    statusMsgUntil = millis() + 1500;
    drawStatusBar();
    return true;
  }
  if (pressed && !lastPressed && inRect(x, y, 48, menuY + 2, 42, MENU_H)) {
    nvsLoad();
    keyboard_set_text(notesText);
    drawTextArea();
    statusMsg = "Loaded";
    statusMsgUntil = millis() + 1500;
    drawStatusBar();
    return true;
  }
  if (pressed && !lastPressed && inRect(x, y, 92, menuY + 2, 46, MENU_H)) {
    notesText[0] = 0;
    keyboard_clear();
    drawTextArea();
    statusMsg = "Cleared";
    statusMsgUntil = millis() + 1500;
    drawStatusBar();
    return true;
  }
  if (pressed && !lastPressed && inRect(x, y, 146, menuY + 2, 56, MENU_H)) {
    kbVisible = !kbVisible;
    keyboard_set_visible(kbVisible);
    textBottom = kbVisible ? (KB_Y - STATUS_H - 2) : (SCREEN_H - STATUS_H - 2);
    drawMenuBar();
    drawToolbar();
    drawTextArea();
    drawStatusBar();
    if (kbVisible) keyboard_draw();
    else tft->fillRect(0, KB_Y, SCREEN_W, SCREEN_H - KB_Y - STATUS_H, TFT_WHITE);
    return true;
  }

  // Scroll by dragging in text area
  if (pressed && inRect(x, y, 0, textTop, SCREEN_W, textBottom - textTop)) {
    if (!dragging) {
      dragging = true;
      dragLastY = y;
      dragAccum = 0;
      return true;
    }
    int dy = y - dragLastY;
    dragLastY = y;
    dragAccum += dy;
    if (abs(dragAccum) >= LINE_H) {
      int steps = dragAccum / LINE_H;
      dragAccum -= steps * LINE_H;
      int maxScroll = max(0, totalLines - visibleLines);
      scrollLine = constrain(scrollLine - steps, 0, maxScroll);
      drawTextArea();
    }
    return true;
  }

  if (!pressed && lastPressed) {
    dragging = false;
    dragAccum = 0;
    keyboard_release();
  }

  if (kbVisible && pressed && !lastPressed) {
    KB_Action a = keyboard_touch(x, y);
    if (a == KB_CHANGED) {
      drawTextArea();
    } else if (a == KB_REDRAW) {
      keyboard_draw();
    } else if (a == KB_HIDE) {
      kbVisible = false;
      keyboard_set_visible(false);
      textBottom = SCREEN_H - 2;
      drawToolbar();
      drawTextArea();
      tft->fillRect(0, KB_Y, SCREEN_W, SCREEN_H - KB_Y, TFT_WHITE);
    }
    return true;
  }

  return true;
}

bool notes_app_is_open() {
  return openState;
}
