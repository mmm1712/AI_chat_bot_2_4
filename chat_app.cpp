#include "chat_app.h"
#include "keyboard.h"
#include "ai_client.h"
#include "system_ui.h"
#include <Arduino.h>
#include <cstring>
#include <cstdio>

static TFT_eSPI* tft = nullptr;

#define SCREEN_W 320
#define SCREEN_H 240

#define MAX_MSG 8
#define MAX_LEN 420

static char chatUser[MAX_MSG][MAX_LEN];
static char chatAI[MAX_MSG][MAX_LEN];
static int chatCount = 0;
static uint8_t chatUserLines[MAX_MSG];
static uint8_t chatAILines[MAX_MSG];
static int cachedWrapW = -1;
static bool chatAIExpanded[MAX_MSG];

// Full width chat
static const int RIGHT_PANEL_X = 320;

static int CHAT_TOP = 32;
static int CHAT_BOTTOM = 110;
static int INPUT_Y = 118;
static const int INPUT_H = 28;

static int chatCursorY = 38;
static bool kbVisible = true;
static const int UI_GAP = 6;

static int scrollLine = 0;
static int totalLines = 0;
static int visibleLines = 0;

static const int CHAT_X0 = 8;
static const int CHAT_X1 = RIGHT_PANEL_X - 4;
static const int LINE_H  = 14;
static const int BLOCK_GAP_LINES = 1;
static const int TOGGLE_H = 20;
static const int TOGGLE_GAP = 4;
static const int AI_COLLAPSED_LINES = 3;
static const int AI_TOGGLE_W = 14;
static const int AI_TOGGLE_H = 14;

// Forward declarations for wrapping helpers
static void wrapLines(const char* s, int maxW);
static int wrapAndCountLines(const char* s, int maxW);

static bool draggingChat = false;
static int dragStartY = 0;
static int dragAccum = 0;
static uint32_t lastStatusTick = 0;

static inline bool inRect(int x,int y,int rx,int ry,int rw,int rh){
  return x>=rx && x<=rx+rw && y>=ry && y<=ry+rh;
}

static bool inChatArea(int x, int y) {
  return (x < RIGHT_PANEL_X) && (y >= CHAT_TOP) && (y <= CHAT_BOTTOM);
}

static void pushMessage(const char* user, const char* ai) {
  if (chatCount >= MAX_MSG) {
    for (int i = 1; i < MAX_MSG; i++) {
      strncpy(chatUser[i-1], chatUser[i], MAX_LEN);
      strncpy(chatAI[i-1],   chatAI[i],   MAX_LEN);
      chatUserLines[i-1] = chatUserLines[i];
      chatAILines[i-1] = chatAILines[i];
    }
    chatCount = MAX_MSG - 1;
  }

  strncpy(chatUser[chatCount], user, MAX_LEN - 1);
  chatUser[chatCount][MAX_LEN - 1] = 0;

  strncpy(chatAI[chatCount], ai, MAX_LEN - 1);
  chatAI[chatCount][MAX_LEN - 1] = 0;

  if (cachedWrapW > 0) {
    char u[MAX_LEN + 8];
    char a[MAX_LEN + 8];
    snprintf(u, sizeof(u), "You: %s", chatUser[chatCount]);
    snprintf(a, sizeof(a), "AI:  %s", chatAI[chatCount]);
    chatUserLines[chatCount] = (uint8_t)wrapAndCountLines(u, cachedWrapW);
    chatAILines[chatCount] = (uint8_t)wrapAndCountLines(a, cachedWrapW);
  } else {
    chatUserLines[chatCount] = 1;
    chatAILines[chatCount] = 1;
  }
  chatAIExpanded[chatCount] = false;

  chatCount++;
}

static void applyLayout() {
  if (kbVisible) {
    INPUT_Y = KB_Y - INPUT_H - UI_GAP;
    CHAT_BOTTOM = INPUT_Y - UI_GAP - TOGGLE_H - TOGGLE_GAP;
  } else {
    INPUT_Y = SCREEN_H - INPUT_H - UI_GAP;
    CHAT_BOTTOM = INPUT_Y - UI_GAP - TOGGLE_H - TOGGLE_GAP;
  }
}

static void drawBackButton(bool pressed) {
  const int x = 260, y = 6, w = 52, h = 17;
  uint16_t bg = pressed ? TFT_DARKGREY : TFT_LIGHTGREY;

  tft->fillRoundRect(x, y, w, h, 2, bg);
  tft->drawRoundRect(x, y, w, h, 2, TFT_BLACK);
  tft->setTextColor(TFT_BLACK, bg);
  tft->drawCentreString("BACK", x + w / 2, y + 4, 1);
}

static void drawHeader() {
  tft->fillRect(0, 0, 320, 28, TFT_BLUE);
  tft->setTextColor(TFT_WHITE, TFT_BLUE);
  tft->drawString("AI Chat", 10, 6, 2);
  system_ui_draw_status(164, 6, TFT_BLUE);
  drawBackButton(false);
}

static void clearChatArea() {
  chatCursorY = CHAT_TOP + 6;
}

static void drawInputBar() {
  tft->drawRect(4, INPUT_Y, 240, INPUT_H, TFT_BLACK);

  tft->setTextColor(TFT_BLACK, TFT_WHITE);
  tft->drawString(">", 8, INPUT_Y + 6, 2);

  tft->fillRoundRect(250, INPUT_Y, 66, INPUT_H, 6, TFT_GREEN);
  tft->setTextColor(TFT_WHITE, TFT_GREEN);
  tft->drawCentreString("SEND", 283, INPUT_Y + 6, 2);

  // Clear toggle area to avoid ghosting
  tft->fillRect(250, INPUT_Y - TOGGLE_H - TOGGLE_GAP, 66, TOGGLE_H, TFT_WHITE);
  tft->fillRoundRect(250, INPUT_Y - TOGGLE_H - TOGGLE_GAP, 66, TOGGLE_H, 6, TFT_LIGHTGREY);
  tft->setTextColor(TFT_BLACK, TFT_LIGHTGREY);
  tft->drawCentreString(kbVisible ? "HIDE" : "SHOW", 283, INPUT_Y - TOGGLE_H - TOGGLE_GAP + 4, 2);
}

static void drawInputTextClipped(const String& s) {
  int maxW = 210;
  String out = s;

  while (out.length() > 0 && tft->textWidth(out, 2) > maxW) {
    out.remove(0, 1);
  }

  tft->setTextColor(TFT_BLACK, TFT_WHITE);
  tft->drawString(out, 22, INPUT_Y + 6, 2);
}

static void updateInputText() {
  tft->fillRect(20, INPUT_Y + 2, 210, INPUT_H - 4, TFT_WHITE);
  drawInputTextClipped(keyboard_get_text());
}

// ============================================================
// Word wrapping (clean word boundaries)
// ============================================================
#define MAX_LINES 80
#define LINE_MAX_CHARS 56
static char wrapBuffer[MAX_LINES][LINE_MAX_CHARS];
static int wrapCount = 0;

static void wrapLines(const char* s, int maxW) {
  wrapCount = 0;
  char line[LINE_MAX_CHARS] = {0};
  char word[LINE_MAX_CHARS] = {0};
  int lineLen = 0;
  int wordLen = 0;

  auto pushLine = [&](const char* ln) {
    if (wrapCount < MAX_LINES) {
      strncpy(wrapBuffer[wrapCount], ln, LINE_MAX_CHARS - 1);
      wrapBuffer[wrapCount][LINE_MAX_CHARS - 1] = 0;
      wrapCount++;
    }
  };

  auto flushWord = [&]() {
    if (wordLen == 0) return;

    if (lineLen == 0 && tft->textWidth(word, 2) > maxW) {
      // Force split a very long word
      char chunk[LINE_MAX_CHARS] = {0};
      int chunkLen = 0;
      for (int i = 0; i < wordLen; i++) {
        if (chunkLen >= LINE_MAX_CHARS - 1) {
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

    char test[LINE_MAX_CHARS] = {0};
    if (lineLen == 0) {
      strncpy(test, word, LINE_MAX_CHARS - 1);
    } else {
      snprintf(test, sizeof(test), "%s %s", line, word);
    }

    if (tft->textWidth(test, 2) <= maxW) {
      strncpy(line, test, LINE_MAX_CHARS - 1);
      line[LINE_MAX_CHARS - 1] = 0;
      lineLen = strlen(line);
    } else {
      if (lineLen > 0) pushLine(line);
      strncpy(line, word, LINE_MAX_CHARS - 1);
      line[LINE_MAX_CHARS - 1] = 0;
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

    if (wordLen >= LINE_MAX_CHARS - 1) {
      flushWord();
    }
    word[wordLen++] = c;
    word[wordLen] = 0;
  }

  flushWord();
  if (lineLen > 0) pushLine(line);
}

static int wrapAndCountLines(const char* s, int maxW) {
  wrapLines(s, maxW);
  return max(1, wrapCount);
}

static void drawWrappedLineWindowLimited(const char* s, int maxW,
                                         int firstLineToDraw, int lastLineToDraw,
                                         int &currentLineIndex, int &y,
                                         int maxLines) {
  wrapLines(s, maxW);
  int limit = wrapCount;
  if (maxLines > 0 && maxLines < limit) limit = maxLines;
  for (int i = 0; i < limit; i++) {
    if (currentLineIndex >= firstLineToDraw && currentLineIndex < lastLineToDraw) {
      if (y >= CHAT_TOP && y + LINE_H <= CHAT_BOTTOM) {
        tft->fillRect(CHAT_X0, y, CHAT_X1 - CHAT_X0, LINE_H, TFT_WHITE);
      }
      tft->drawString(wrapBuffer[i], CHAT_X0, y, 2);
      y += LINE_H;
    }
    currentLineIndex++;
  }
}

static int aiVisibleLinesForIndex(int i) {
  int full = max(1, (int)chatAILines[i]);
  if (chatAIExpanded[i]) return full;
  return min(full, AI_COLLAPSED_LINES);
}

static void drawAIToggleAt(int x, int y, bool expanded) {
  uint16_t bg = TFT_LIGHTGREY;
  tft->fillRect(x, y, AI_TOGGLE_W, AI_TOGGLE_H, bg);
  tft->drawRect(x, y, AI_TOGGLE_W, AI_TOGGLE_H, TFT_BLACK);
  int cx = x + AI_TOGGLE_W / 2;
  int cy = y + AI_TOGGLE_H / 2;
  if (expanded) {
    tft->fillTriangle(cx - 4, cy + 2, cx + 4, cy + 2, cx, cy - 3, TFT_BLACK);
  } else {
    tft->fillTriangle(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3, TFT_BLACK);
  }
}

static void drawChatHistory() {
  clearChatArea();

  int maxW = CHAT_X1 - CHAT_X0;
  if (maxW != cachedWrapW) {
    cachedWrapW = maxW;
    for (int i = 0; i < chatCount; i++) {
      char u[MAX_LEN + 8];
      char a[MAX_LEN + 8];
      snprintf(u, sizeof(u), "You: %s", chatUser[i]);
      snprintf(a, sizeof(a), "AI:  %s", chatAI[i]);
      chatUserLines[i] = (uint8_t)wrapAndCountLines(u, cachedWrapW);
      chatAILines[i] = (uint8_t)wrapAndCountLines(a, cachedWrapW);
    }
  }
  visibleLines = (CHAT_BOTTOM - chatCursorY) / LINE_H;

  totalLines = 0;
  for (int i = 0; i < chatCount; i++) {
    totalLines += max(1, (int)chatUserLines[i]);
    totalLines += aiVisibleLinesForIndex(i);
    totalLines += BLOCK_GAP_LINES;
  }

  int maxScroll = max(0, totalLines - visibleLines);
  scrollLine = constrain(scrollLine, 0, maxScroll);

  tft->setTextColor(TFT_BLACK, TFT_WHITE);

  int first = scrollLine;
  int last  = scrollLine + visibleLines;

  int currentLine = 0;
  int y = chatCursorY;

  for (int i = 0; i < chatCount; i++) {
    char u[MAX_LEN + 8];
    char a[MAX_LEN + 8];
    snprintf(u, sizeof(u), "You: %s", chatUser[i]);
    snprintf(a, sizeof(a), "AI:  %s", chatAI[i]);

    drawWrappedLineWindowLimited(u, maxW, first, last, currentLine, y, chatUserLines[i]);
    int aiLinesToShow = aiVisibleLinesForIndex(i);
    int aiFirstLineIndex = currentLine;
    drawWrappedLineWindowLimited(a, maxW, first, last, currentLine, y, aiLinesToShow);

    if (chatAILines[i] > AI_COLLAPSED_LINES) {
      if (aiFirstLineIndex >= first && aiFirstLineIndex < last) {
        int ty = chatCursorY + (aiFirstLineIndex - first) * LINE_H;
        int tx = CHAT_X1 - AI_TOGGLE_W - 2;
        drawAIToggleAt(tx, ty, chatAIExpanded[i]);
      }
    }

    currentLine += BLOCK_GAP_LINES;
    y += BLOCK_GAP_LINES * LINE_H;
    if (currentLine >= last) break;
  }

  if (y < CHAT_BOTTOM) {
    tft->fillRect(CHAT_X0, y, CHAT_X1 - CHAT_X0, CHAT_BOTTOM - y, TFT_WHITE);
  }
}

static int hitAiToggle(int x, int y) {
  if (x < CHAT_X1 - AI_TOGGLE_W - 2 || x > CHAT_X1 - 2) return -1;
  int maxW = CHAT_X1 - CHAT_X0;
  int first = scrollLine;
  int last  = scrollLine + visibleLines;
  int currentLine = 0;

  for (int i = 0; i < chatCount; i++) {
    currentLine += max(1, (int)chatUserLines[i]);
    if (chatAILines[i] > AI_COLLAPSED_LINES) {
      int aiFirstLineIndex = currentLine;
      if (aiFirstLineIndex >= first && aiFirstLineIndex < last) {
        int ty = chatCursorY + (aiFirstLineIndex - first) * LINE_H;
        if (y >= ty && y <= ty + AI_TOGGLE_H) return i;
      }
    }
    currentLine += aiVisibleLinesForIndex(i);
  }
  return -1;
}

void chat_scroll_steps(int steps) {
  if (!tft) return;
  int maxScroll = max(0, totalLines - visibleLines);
  scrollLine = constrain(scrollLine - steps, 0, maxScroll);
  drawChatHistory();
  drawInputBar();
  updateInputText();
}

void chat_init(TFT_eSPI* display) {
  tft = display;

  kbVisible = true;
}

void chat_draw() {
  applyLayout();

  tft->fillScreen(TFT_WHITE);
  drawHeader();
  drawChatHistory();
  drawInputBar();
  updateInputText();

  keyboard_set_visible(kbVisible);
  if (kbVisible) keyboard_draw();
}

void chat_tick() {
  uint32_t now = millis();
  if (now - lastStatusTick > 900) {
    system_ui_tick(164, 6, TFT_BLUE);
    lastStatusTick = now;
  }
}

void chat_release() {
  keyboard_release();
  draggingChat = false;
  dragAccum = 0;
  drawChatHistory();
}

void chat_handleTouch(bool pressed, bool lastPressed, int x, int y) {
  if (!tft) return;

  if (pressed && kbVisible) {
    KB_Action tickA = keyboard_tick(true, x, y);
    if (tickA == KB_CHANGED) {
      updateInputText();
      return;
    }
  }

  if (pressed && !lastPressed && inRect(x, y, 260, 4, 52, 17)) {
    return;
  }

  if (pressed && !lastPressed && inRect(x, y, 250, INPUT_Y - TOGGLE_H - TOGGLE_GAP, 66, TOGGLE_H)) {
    kbVisible = !kbVisible;
    chat_draw();
    return;
  }

  if (pressed && !lastPressed && !kbVisible && inRect(x, y, 4, INPUT_Y, 240, INPUT_H)) {
    kbVisible = true;
    chat_draw();
    return;
  }

  if (pressed && !lastPressed && inChatArea(x, y)) {
    int idx = hitAiToggle(x, y);
    if (idx >= 0) {
      chatAIExpanded[idx] = !chatAIExpanded[idx];
      drawChatHistory();
      return;
    }
  }

  if (pressed && inChatArea(x, y)) {
    if (!draggingChat) {
      draggingChat = true;
      dragStartY = y;
      dragAccum = 0;
      return;
    }

    int dy = y - dragStartY;
    dragStartY = y;
    dragAccum += dy;

    if (abs(dragAccum) >= LINE_H) {
      int steps = dragAccum / LINE_H;
      dragAccum -= steps * LINE_H;

      int maxScroll = max(0, totalLines - visibleLines);
      scrollLine = constrain(scrollLine - steps, 0, maxScroll);
      drawChatHistory();
    }
    return;
  }

  if (pressed && !lastPressed && inRect(x, y, 250, INPUT_Y, 66, INPUT_H)) {
    String userText = keyboard_get_text();
    userText.trim();

    if (userText.length() > 0) {
      // Clear input immediately for better UX
      keyboard_clear();
      updateInputText();

      String aiText = ai_sendMessage(userText);
      pushMessage(userText.c_str(), aiText.c_str());

      scrollLine = 0; // show from the beginning after sending
      drawChatHistory();
    }
    return;
  }

  if (kbVisible && pressed && !lastPressed) {
    KB_Action a = keyboard_touch(x, y);

    if (a == KB_CHANGED) {
      updateInputText();
    } else if (a == KB_REDRAW) {
      keyboard_draw();
    } else if (a == KB_HIDE) {
      kbVisible = false;
      chat_draw();
    }
    return;
  }
}
