#include "keyboard.h"
#include <Arduino.h>
#include <ctype.h>

static TFT_eSPI *tft = nullptr;

static char text[KB_TEXT_MAX + 1];
static int  cursor = 0;

static bool visible = true;
static bool mode123 = false;
static bool caps    = false;

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;

static const int KB_DY = 0;

static const int PAD_L = 6;
static const int PAD_R = 6;

static const int GAP = 1;

static const int FN_W   = 54;
static const int CAPS_W = 54;

static const int HIT_PAD = 10;
static const uint32_t RELEASE_DEBOUNCE_MS = 70;

static bool     stablePressed   = false;
static uint32_t lastTouchSeenMs = 0;
static bool     keyDown         = false;
static int      activeIdx       = -1;

static bool delHeld = false;
static uint32_t delStart = 0;
static uint32_t delLast  = 0;
static const uint32_t DEL_DELAY  = 450;
static const uint32_t DEL_REPEAT = 80;

static bool capsHeld = false;
static uint32_t capsDownAt = 0;
static bool capsDidClear = false;
static const uint32_t CAPS_CLEAR_HOLD = 500;

static inline bool insidePad(int x,int y,int rx,int ry,int rw,int rh,int pad){
  return x >= (rx - pad) && x < (rx + rw + pad) && y >= (ry - pad) && y < (ry + rh + pad);
}

static void addChar(char c) {
  if (cursor >= KB_TEXT_MAX) return;
  text[cursor++] = c;
  text[cursor] = 0;
}

static void backspaceOnce() {
  if (cursor > 0) {
    cursor--;
    text[cursor] = 0;
  }
}

static int kbTopY() {
  int ty = KB_Y + KB_DY;
  if (ty < 0) ty = 0;
  if (ty > SCREEN_H - 60) ty = SCREEN_H - 60;
  return ty;
}

static int keyH() {
  int ty = kbTopY();
  int availH = SCREEN_H - ty;

  int h = (availH - 3*GAP) / 4;
  if (h < 18) h = 18;
  if (h > 28) h = 28;
  return h;
}

static int spaceH() {

  return keyH();
}

static void drawKey(int x,int y,int w,int h,const char*label, bool pressed) {
  uint16_t fill   = pressed ? TFT_DARKGREY : TFT_LIGHTGREY;
  uint16_t border = pressed ? TFT_BLACK    : TFT_DARKGREY;

  tft->fillRoundRect(x,y,w,h,3, fill);
  tft->drawRoundRect(x,y,w,h,3, border);

  tft->setTextColor(TFT_BLACK, fill);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(label, x + w/2, y + h/2, 2);
  tft->setTextDatum(TL_DATUM);
}

enum KeyType : uint8_t { KT_CHAR, KT_SPACE, KT_DEL, KT_MODE, KT_CAPS, KT_CLR };

struct KeyRect {
  int x,y,w,h;
  KeyType type;
  char ch;
  const char *label;
};

static KeyRect keys[64];
static int keyCount = 0;

static void addKey(int x,int y,int w,int h, KeyType type, const char *label, char ch = 0) {
  if (keyCount >= (int)(sizeof(keys)/sizeof(keys[0]))) return;
  keys[keyCount++] = {x,y,w,h,type,ch,label};
}

static void buildKeys() {
  keyCount = 0;

  const int KH = keyH();
  const int SH = spaceH();

  const int topY = kbTopY();
  const int y0 = topY;
  const int y1 = topY + (KH + GAP);
  const int y2 = topY + 2*(KH + GAP);
  const int y3 = topY + 3*(KH + GAP);

  const int rightX = SCREEN_W - PAD_R;
  const int xMode = rightX - FN_W;
  const int xClr  = xMode - GAP - FN_W;
  const int xDel  = xMode;

  const int row1X = PAD_L;
  const int row2X = PAD_L + 10;

  if (!mode123) {
    const char *r1 = "QWERTYUIOP";
    const char *r2 = "ASDFGHJKL";
    const char *r3 = "ZXCVBNM";

    {
      int n = 10;
      int avail = (SCREEN_W - PAD_L - PAD_R) - (n-1)*GAP;
      int w = avail / n;
      int x = row1X;
      for (int i=0;i<n;i++){
        addKey(x, y0, w, KH, KT_CHAR, nullptr, r1[i]);
        x += w + GAP;
      }
    }

    {
      int n = 9;
      int leftW = (xDel - GAP) - row2X;
      int avail = leftW - (n-1)*GAP;
      int w = avail / n;
      int x = row2X;
      for (int i=0;i<n;i++){
        addKey(x, y1, w, KH, KT_CHAR, nullptr, r2[i]);
        x += w + GAP;
      }
      addKey(xDel, y1, FN_W, KH, KT_DEL, "DEL");
    }

    {
      addKey(row1X, y2, CAPS_W, KH, KT_CAPS, "CAPS");

      int letterStart = row1X + CAPS_W + GAP;
      int letterEnd   = xClr - GAP;
      int avail = (letterEnd - letterStart) - (7-1)*GAP;
      int w = avail / 7;

      int x = letterStart;
      for (int i=0;i<7;i++){
        addKey(x, y2, w, KH, KT_CHAR, nullptr, r3[i]);
        x += w + GAP;
      }

      addKey(xClr,  y2, FN_W, KH, KT_CLR,  "CLR");
      addKey(xMode, y2, FN_W, KH, KT_MODE, "123");
    }

    addKey(PAD_L, y3, SCREEN_W - PAD_L - PAD_R, SH, KT_SPACE, "SPACE");
  }
  else {
    const char *nums = "1234567890";
    const char *r2[] = {"-","/",":",";","(",")","$","&","@"};
    const char *r3[] = {".",",","?","!","'","\"","+","=","#"};

    {
      int n = 10;
      int avail = (SCREEN_W - PAD_L - PAD_R) - (n-1)*GAP;
      int w = avail / n;
      int x = row1X;
      for (int i=0;i<n;i++){
        addKey(x, y0, w, KH, KT_CHAR, nullptr, nums[i]);
        x += w + GAP;
      }
    }

    {
      int n = 9;
      int leftW = (xDel - GAP) - row2X;
      int avail = leftW - (n-1)*GAP;
      int w = avail / n;
      int x = row2X;
      for (int i=0;i<n;i++){
        addKey(x, y1, w, KH, KT_CHAR, r2[i], r2[i][0]);
        x += w + GAP;
      }
      addKey(xDel, y1, FN_W, KH, KT_DEL, "DEL");
    }

    {
      int letterStart = row1X;
      int letterEnd   = xClr - GAP;
      int avail = (letterEnd - letterStart) - (7-1)*GAP;
      int w = avail / 7;

      int x = letterStart;
      for (int i=0;i<7;i++){
        addKey(x, y2, w, KH, KT_CHAR, r3[i], r3[i][0]);
        x += w + GAP;
      }

      addKey(xClr,  y2, FN_W, KH, KT_CLR,  "CLR");
      addKey(xMode, y2, FN_W, KH, KT_MODE, "ABC");
    }

    addKey(PAD_L, y3, SCREEN_W - PAD_L - PAD_R, SH, KT_SPACE, "SPACE");
  }
}

static void drawOneKey(int i, bool pressed) {
  const KeyRect &k = keys[i];

  if (k.type == KT_CHAR) {
    if (k.label) {
      drawKey(k.x,k.y,k.w,k.h,k.label, pressed);
    } else {
      char tmp[2] = {0,0};
      char c = k.ch;
      if (!mode123 && isalpha((unsigned char)c)) c = caps ? toupper((unsigned char)c) : tolower((unsigned char)c);
      tmp[0] = c;
      drawKey(k.x,k.y,k.w,k.h,tmp, pressed);
    }
    return;
  }

  if (k.type == KT_CAPS) {
    drawKey(k.x,k.y,k.w,k.h, caps ? "CAPS*" : "CAPS", pressed);
    return;
  }

  drawKey(k.x,k.y,k.w,k.h,k.label, pressed);
}

static int hitTest(int x, int y) {
  buildKeys();
  for (int i=0;i<keyCount;i++){
    if (insidePad(x,y, keys[i].x, keys[i].y, keys[i].w, keys[i].h, HIT_PAD)) return i;
  }
  return -1;
}

static KB_Action commitKey(const KeyRect &k) {
  switch (k.type) {
    case KT_SPACE: addChar(' '); return KB_CHANGED;
    case KT_DEL:
      backspaceOnce();
      delHeld = true; delStart = millis(); delLast = delStart;
      return KB_CHANGED;
    case KT_CLR: keyboard_clear(); return KB_CHANGED;
    case KT_MODE: mode123 = !mode123; return KB_REDRAW;
    case KT_CAPS:
      caps = !caps;
      capsHeld = true; capsDownAt = millis(); capsDidClear = false;
      return KB_REDRAW;
    case KT_CHAR: {
      char c = k.ch;
      if (!mode123 && isalpha((unsigned char)c)) c = caps ? toupper((unsigned char)c) : tolower((unsigned char)c);
      addChar(c);
      return KB_CHANGED;
    }
  }
  return KB_NONE;
}

static KB_Action handleHoldAt(bool pressedNow, int x, int y) {
  if (!pressedNow) {
    delHeld = false;
    capsHeld = false;
    capsDidClear = false;
    return KB_NONE;
  }

  uint32_t now = millis();

  if (!mode123 && capsHeld && !capsDidClear) {
    buildKeys();
    for (int i=0;i<keyCount;i++){
      if (keys[i].type == KT_CAPS) {
        if (insidePad(x,y, keys[i].x, keys[i].y, keys[i].w, keys[i].h, HIT_PAD)) {
          if (now - capsDownAt >= CAPS_CLEAR_HOLD) {
            keyboard_clear();
            capsDidClear = true;
            return KB_CHANGED;
          }
        }
        break;
      }
    }
  }

  if (delHeld) {
    buildKeys();
    for (int i=0;i<keyCount;i++){
      if (keys[i].type == KT_DEL) {
        if (!insidePad(x,y, keys[i].x, keys[i].y, keys[i].w, keys[i].h, HIT_PAD)) return KB_NONE;

        if ((now - delStart) > DEL_DELAY && (now - delLast) > DEL_REPEAT) {
          delLast = now;
          backspaceOnce();
          return KB_CHANGED;
        }
        return KB_NONE;
      }
    }
  }

  return KB_NONE;
}

void keyboard_init(TFT_eSPI *display) {
  tft = display;
  keyboard_clear();
}

void keyboard_set_visible(bool v){ visible = v; }
bool keyboard_is_visible(){ return visible; }

void keyboard_clear() {
  cursor = 0;
  text[0] = 0;
}

const char* keyboard_get_text(){ return text; }

void keyboard_set_text(const char* s) {
  if (!s) {
    keyboard_clear();
    return;
  }
  int n = 0;
  while (s[n] && n < KB_TEXT_MAX) {
    text[n] = s[n];
    n++;
  }
  text[n] = 0;
  cursor = n;
}

void keyboard_release() {
  keyDown = false;
  delHeld = false;
  capsHeld = false;
  capsDidClear = false;

  if (activeIdx >= 0 && tft) {
    buildKeys();
    drawOneKey(activeIdx, false);
  }
  activeIdx = -1;
  stablePressed = false;
}

void keyboard_draw() {
  if (!visible || !tft) return;

  buildKeys();

  int topY = kbTopY();
  tft->fillRect(0, topY, SCREEN_W, SCREEN_H - topY, TFT_WHITE);

  for (int i=0;i<keyCount;i++){
    drawOneKey(i, (i == activeIdx));
  }
}

KB_Action keyboard_update(bool pressed, int x, int y) {
  if (!visible || !tft) return KB_NONE;

  uint32_t now = millis();

  if (pressed) {
    lastTouchSeenMs = now;

    if (!stablePressed) {
      stablePressed = true;
      keyDown = false;
    }

    int idx = hitTest(x, y);

    if (idx != activeIdx) {
      if (activeIdx >= 0) { buildKeys(); drawOneKey(activeIdx, false); }
      activeIdx = idx;
      if (activeIdx >= 0) { buildKeys(); drawOneKey(activeIdx, true); }
    }

    if (activeIdx >= 0 && !keyDown) {
      keyDown = true;
      buildKeys();
      KB_Action a = commitKey(keys[activeIdx]);
      if (a == KB_REDRAW) keyboard_draw();
      return a;
    }

    return handleHoldAt(true, x, y);
  }

  if (stablePressed && (now - lastTouchSeenMs) > RELEASE_DEBOUNCE_MS) {
    stablePressed = false;
    keyDown = false;
    delHeld = false;
    capsHeld = false;
    capsDidClear = false;

    if (activeIdx >= 0) {
      buildKeys();
      drawOneKey(activeIdx, false);
      activeIdx = -1;
    }
  }

  return KB_NONE;
}

KB_Action keyboard_touch(int x, int y) { return keyboard_update(true, x, y); }
KB_Action keyboard_tick(bool pressed, int x, int y) { return keyboard_update(pressed, x, y); }
