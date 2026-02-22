#include "paint.h"
#include "system_ui.h"
#include <Arduino.h>

static TFT_eSPI* tft = nullptr;
static uint32_t lastStatusTick = 0;

#define SCREEN_W 320
#define SCREEN_H 240

#define TITLE_H   18
#define MENU_H    14
#define STATUS_H  12

#define TOOLS_W   52
#define SCROLL_W  10
#define HSCROLL_H 10
#define PALETTE_H 36

#define WORK_Y    (TITLE_H + MENU_H)

#define CANVAS_X  (TOOLS_W + 3)
#define CANVAS_Y  (WORK_Y + 3)

#define CANVAS_W  (SCREEN_W - TOOLS_W - SCROLL_W - 6)
#define CANVAS_H  (SCREEN_H - TITLE_H - MENU_H - PALETTE_H - STATUS_H - 10)

#define PX 2
#define GW (CANVAS_W / PX)
#define GH (CANVAS_H / PX)

static const uint16_t xp_gray  = 0xC618;
static const uint16_t xp_dark  = 0x8410;
static const uint16_t xp_light = 0xE71C;
static const uint16_t xp_blue  = 0x1C9F;
static const uint16_t xp_panel = 0xDEF7;
static const uint16_t xp_white = 0xFFFF;

static const uint16_t palette[] = {
  TFT_BLACK, TFT_WHITE, 0x7BEF, 0xC618,
  TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN,
  TFT_CYAN, TFT_BLUE, TFT_MAGENTA, 0xFD20,
  0x07FF, 0xFFE0, 0xFBE0, 0x9E7F
};
static const int PALETTE_N = sizeof(palette) / sizeof(palette[0]);
static const int PAL_SW = 14;
static const int PAL_SH = 14;
static const int PAL_GAP = 2;
static const int PAL_COLS = 8;

static uint16_t canvas[GW * GH];
static uint16_t color = TFT_BLACK;
static int selectedColorIdx = 0;

enum Tool {
  TOOL_SELECT,
  TOOL_RECTSEL,
  TOOL_ERASE,
  TOOL_FILL,
  TOOL_PENCIL,
  TOOL_BRUSH,
  TOOL_TEXT,
  TOOL_LINE,
  TOOL_RECT,
  TOOL_ELLIPSE,
  TOOL_COUNT
};

static Tool tool = TOOL_PENCIL;

static bool penDown = false;
static bool startedOnCanvas = false;

static int startGX = 0, startGY = 0;
static int lastGX  = -1, lastGY  = -1;
static bool previewActive = false;
static int prevGX0 = 0, prevGY0 = 0, prevGX1 = 0, prevGY1 = 0;

static uint16_t* snap = nullptr;
static bool      snapOk = false;
static bool      snapWarned = false;

static bool selActive = false;
static bool selDragging = false;
static int  selX=0, selY=0, selW=0, selH=0;
static int  selGrabOffX=0, selGrabOffY=0;
static uint16_t* selBuf = nullptr;
static int  selBufW=0, selBufH=0;

static int16_t* ffX = nullptr;
static int16_t* ffY = nullptr;
static bool ffOk = false;
static bool ffWarned = false;

static const char* statusMsg = nullptr;
static uint32_t statusMsgUntil = 0;
static bool statusDirty = false;

static void setStatusMsg(const char* msg, uint32_t ms = 1500) {
  statusMsg = msg;
  statusMsgUntil = millis() + ms;
  statusDirty = true;
}

static inline void clampXY(int &x, int &y) {
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= GW) x = GW - 1;
  if (y >= GH) y = GH - 1;
}

static inline bool inGrid(int gx, int gy) {
  return (gx >= 0 && gy >= 0 && gx < GW && gy < GH);
}

static inline uint16_t getPixel(int gx, int gy) {
  if (!inGrid(gx,gy)) return TFT_WHITE;
  return canvas[gy * GW + gx];
}

static inline void renderPixel(int gx, int gy, uint16_t c) {
  tft->fillRect(CANVAS_X + gx * PX, CANVAS_Y + gy * PX, PX, PX, c);
}

static inline void setPixel(int gx, int gy, uint16_t c) {
  if (!inGrid(gx,gy)) return;
  canvas[gy * GW + gx] = c;
  renderPixel(gx, gy, c);
}

static void clearCanvas() {
  for (int i = 0; i < GW * GH; i++) canvas[i] = TFT_WHITE;
}

static void renderCanvasAll() {

  tft->fillRect(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H, TFT_WHITE);

  for (int y = 0; y < GH; y++) {
    for (int x = 0; x < GW; x++) {
      uint16_t c = canvas[y * GW + x];
      if (c != TFT_WHITE) renderPixel(x, y, c);
    }
  }

  if (selActive && selBuf && selBufW>0 && selBufH>0) {
    for (int y = 0; y < selBufH; y++) {
      for (int x = 0; x < selBufW; x++) {
        uint16_t c = selBuf[y*selBufW + x];
        if (c != TFT_WHITE) renderPixel(selX + x, selY + y, c);
      }
    }
  }
}

static void renderCanvasRect(int gx0, int gy0, int gx1, int gy1) {
  if (gx0 > gx1) { int t=gx0; gx0=gx1; gx1=t; }
  if (gy0 > gy1) { int t=gy0; gy0=gy1; gy1=t; }
  if (gx0 < 0) gx0 = 0;
  if (gy0 < 0) gy0 = 0;
  if (gx1 >= GW) gx1 = GW - 1;
  if (gy1 >= GH) gy1 = GH - 1;

  int px0 = CANVAS_X + gx0 * PX;
  int py0 = CANVAS_Y + gy0 * PX;
  int w = (gx1 - gx0 + 1) * PX;
  int h = (gy1 - gy0 + 1) * PX;
  tft->fillRect(px0, py0, w, h, TFT_WHITE);

  for (int y = gy0; y <= gy1; y++) {
    for (int x = gx0; x <= gx1; x++) {
      uint16_t c = canvas[y * GW + x];
      if (c != TFT_WHITE) renderPixel(x, y, c);
    }
  }

  if (selActive && selBuf && selBufW>0 && selBufH>0) {
    int sx0 = selX;
    int sy0 = selY;
    int sx1 = selX + selBufW - 1;
    int sy1 = selY + selBufH - 1;
    if (!(sx1 < gx0 || sx0 > gx1 || sy1 < gy0 || sy0 > gy1)) {
      int x0 = max(gx0, sx0);
      int y0 = max(gy0, sy0);
      int x1 = min(gx1, sx1);
      int y1 = min(gy1, sy1);
      for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
          uint16_t c = selBuf[(y - selY) * selBufW + (x - selX)];
          if (c != TFT_WHITE) renderPixel(x, y, c);
        }
      }
    }
  }
}

static void ensureSnapshot() {
  if (snap) { snapOk = true; return; }
  snap = (uint16_t*)malloc(sizeof(uint16_t) * GW * GH);
  snapOk = (snap != nullptr);
  if (!snapOk && !snapWarned) {
    snapWarned = true;
    setStatusMsg("Not enough RAM for Undo");
  }
}

static void takeSnapshot() {
  if (!snapOk) return;
  memcpy(snap, canvas, sizeof(uint16_t) * GW * GH);

}

static void restoreSnapshotToCanvas() {
  if (!snapOk) return;
  memcpy(canvas, snap, sizeof(uint16_t) * GW * GH);
}

static void freeSelection() {
  if (selBuf) { free(selBuf); selBuf = nullptr; }
  selBufW = selBufH = 0;
  selActive = false;
  selDragging = false;
}

static void stamp(int gx, int gy, uint16_t c, int r) {

  for (int yy = gy - r; yy <= gy + r; yy++) {
    for (int xx = gx - r; xx <= gx + r; xx++) {
      if (inGrid(xx,yy)) setPixel(xx,yy,c);
    }
  }
}

static void drawLineGrid(int x0,int y0,int x1,int y1,uint16_t c,int r) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    stamp(x0, y0, c, r);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static void drawRectOutlineGrid(int x0,int y0,int x1,int y1,uint16_t c,int r) {
  if (x0 > x1) { int t=x0; x0=x1; x1=t; }
  if (y0 > y1) { int t=y0; y0=y1; y1=t; }

  drawLineGrid(x0,y0,x1,y0,c,r);
  drawLineGrid(x1,y0,x1,y1,c,r);
  drawLineGrid(x1,y1,x0,y1,c,r);
  drawLineGrid(x0,y1,x0,y0,c,r);
}

static void drawEllipseOutlineGrid(int x0,int y0,int x1,int y1,uint16_t c,int r) {
  if (x0 > x1) { int t=x0; x0=x1; x1=t; }
  if (y0 > y1) { int t=y0; y0=y1; y1=t; }

  float cx = (x0 + x1) * 0.5f;
  float cy = (y0 + y1) * 0.5f;
  float rx = max(1.0f, (x1 - x0) * 0.5f);
  float ry = max(1.0f, (y1 - y0) * 0.5f);

  int steps = (int)(6.0f * (rx + ry));
  if (steps < 32) steps = 32;
  if (steps > 240) steps = 240;

  for (int i = 0; i < steps; i++) {
    float a = (2.0f * 3.1415926f * i) / steps;
    int gx = (int)roundf(cx + cosf(a) * rx);
    int gy = (int)roundf(cy + sinf(a) * ry);
    stamp(gx, gy, c, r);
  }
}

static void ensureFloodFill() {
  if (ffX && ffY) { ffOk = true; return; }

  ffX = (int16_t*)malloc(sizeof(int16_t) * GW * GH);
  ffY = (int16_t*)malloc(sizeof(int16_t) * GW * GH);
  ffOk = (ffX && ffY);
  if (!ffOk && !ffWarned) {
    ffWarned = true;
    setStatusMsg("Not enough RAM for Fill");
  }
}

static void floodFill(int sx, int sy, uint16_t newC) {
  if (!ffOk) return;
  if (!inGrid(sx,sy)) return;

  uint16_t oldC = getPixel(sx,sy);
  if (oldC == newC) return;

  int top = 0;
  ffX[top] = sx;
  ffY[top] = sy;
  top++;

  while (top > 0) {
    top--;
    int x = ffX[top];
    int y = ffY[top];

    if (!inGrid(x,y)) continue;
    if (getPixel(x,y) != oldC) continue;

    setPixel(x,y,newC);

    if (top + 4 >= GW*GH) continue;
    ffX[top] = x+1; ffY[top] = y;   top++;
    ffX[top] = x-1; ffY[top] = y;   top++;
    ffX[top] = x;   ffY[top] = y+1; top++;
    ffX[top] = x;   ffY[top] = y-1; top++;
  }
}

static void drawTitle() {
  tft->fillRect(0, 0, SCREEN_W, TITLE_H, xp_blue);
  tft->setTextColor(TFT_WHITE, xp_blue);
  tft->drawString("untitled - Paint", 6, 3, 2);

  system_ui_draw_status(SCREEN_W - 52 - 92 - 4, 1, xp_blue);

  tft->fillRect(SCREEN_W - 52, 2, 16, 14, 0x841F);
  tft->fillRect(SCREEN_W - 34, 2, 16, 14, 0x841F);
  tft->fillRect(SCREEN_W - 16, 2, 14, 14, TFT_RED);
  tft->setTextColor(TFT_WHITE, TFT_RED);
  tft->drawCentreString("X", SCREEN_W - 9, 3, 2);
}

static void drawMenu() {
  tft->fillRect(0, TITLE_H, SCREEN_W, MENU_H, xp_panel);
  tft->setTextColor(TFT_BLACK, xp_panel);
  tft->drawString("File  Edit  View  Image  Colors  Help", 6, TITLE_H + 2, 1);
}

static void drawStatusBar() {
  int y = SCREEN_H - STATUS_H;
  tft->fillRect(0, y, SCREEN_W, STATUS_H, xp_panel);
  tft->drawFastHLine(0, y, SCREEN_W, xp_light);
  tft->drawFastHLine(0, y + 1, SCREEN_W, xp_white);
  tft->setTextColor(TFT_BLACK, xp_panel);
  if (statusMsg && millis() < statusMsgUntil) {
    tft->drawString(statusMsg, 4, y + 2, 1);
  } else {
    statusMsg = nullptr;
    tft->drawString("For Help, click Help Topics on the Help Menu.", 4, y + 2, 1);
  }
  statusDirty = false;
}

static void drawIcon_Select(int cx, int cy) {
  tft->drawLine(cx - 7, cy - 7, cx + 4, cy + 4, TFT_BLACK);
  tft->drawTriangle(cx - 7, cy - 7, cx - 2, cy - 6, cx - 6, cy - 2, TFT_BLACK);
}
static void drawIcon_RectSel(int cx, int cy) {
  tft->drawRect(cx - 7, cy - 6, 14, 12, TFT_BLACK);
  tft->drawPixel(cx - 7, cy - 6, TFT_BLACK);
  tft->drawPixel(cx + 6, cy - 6, TFT_BLACK);
  tft->drawPixel(cx - 7, cy + 5, TFT_BLACK);
  tft->drawPixel(cx + 6, cy + 5, TFT_BLACK);
}
static void drawIcon_Erase(int cx, int cy) {
  tft->fillRect(cx - 7, cy - 4, 14, 8, TFT_WHITE);
  tft->drawRect(cx - 7, cy - 4, 14, 8, TFT_BLACK);
  tft->drawLine(cx - 7, cy - 4, cx - 3, cy - 7, TFT_BLACK);
  tft->drawLine(cx + 6, cy - 4, cx + 2, cy - 7, TFT_BLACK);
}
static void drawIcon_Fill(int cx, int cy) {
  tft->drawRect(cx - 6, cy - 2, 10, 7, TFT_BLACK);
  tft->drawLine(cx + 4, cy - 2, cx + 8, cy - 6, TFT_BLACK);
  tft->drawPixel(cx + 8, cy - 6, TFT_BLACK);
}
static void drawIcon_Pencil(int cx, int cy) {
  tft->drawLine(cx - 7, cy + 6, cx + 7, cy - 6, TFT_BLACK);
  tft->drawLine(cx - 6, cy + 6, cx + 6, cy - 6, TFT_BLACK);
}
static void drawIcon_Brush(int cx, int cy) {
  tft->fillRect(cx - 2, cy - 7, 4, 9, TFT_BLACK);
  tft->drawLine(cx - 6, cy + 2, cx + 6, cy + 2, TFT_BLACK);
  tft->drawLine(cx - 5, cy + 3, cx + 5, cy + 3, TFT_BLACK);
}
static void drawIcon_Text(int cx, int cy) {
  tft->drawChar(cx - 4, cy - 7, 'A', TFT_BLACK, xp_gray, 2);
}
static void drawIcon_Line(int cx, int cy) {
  tft->drawLine(cx - 7, cy + 6, cx + 7, cy - 6, TFT_BLACK);
}
static void drawIcon_Rect(int cx, int cy) {
  tft->drawRect(cx - 7, cy - 6, 14, 12, TFT_BLACK);
}
static void drawIcon_Ellipse(int cx, int cy) {
  tft->drawCircle(cx, cy, 6, TFT_BLACK);
}

static void drawToolIcon(int cx, int cy, Tool id) {
  switch (id) {
    case TOOL_SELECT:  drawIcon_Select(cx, cy); break;
    case TOOL_RECTSEL: drawIcon_RectSel(cx, cy); break;
    case TOOL_ERASE:   drawIcon_Erase(cx, cy); break;
    case TOOL_FILL:    drawIcon_Fill(cx, cy); break;
    case TOOL_PENCIL:  drawIcon_Pencil(cx, cy); break;
    case TOOL_BRUSH:   drawIcon_Brush(cx, cy); break;
    case TOOL_TEXT:    drawIcon_Text(cx, cy); break;
    case TOOL_LINE:    drawIcon_Line(cx, cy); break;
    case TOOL_RECT:    drawIcon_Rect(cx, cy); break;
    case TOOL_ELLIPSE: drawIcon_Ellipse(cx, cy); break;
    default: break;
  }
}

static const int TOOL_BTN  = 22;
static const int TOOL_GAP  = 3;
static const int TOOL_COLS = 2;

static int toolsTopY() { return WORK_Y + 4; }

static void drawToolButton(int bx, int by, Tool id, bool selected) {
  uint16_t bg = selected ? TFT_WHITE : xp_gray;

  tft->fillRect(bx, by, TOOL_BTN, TOOL_BTN, bg);

  tft->drawFastHLine(bx, by, TOOL_BTN, xp_light);
  tft->drawFastVLine(bx, by, TOOL_BTN, xp_light);
  tft->drawFastHLine(bx, by + TOOL_BTN - 1, TOOL_BTN, xp_dark);
  tft->drawFastVLine(bx + TOOL_BTN - 1, by, TOOL_BTN, xp_dark);

  if (selected) tft->drawRect(bx, by, TOOL_BTN, TOOL_BTN, TFT_BLACK);

  drawToolIcon(bx + TOOL_BTN / 2, by + TOOL_BTN / 2, id);
}

static void drawTools() {
  int h = SCREEN_H - WORK_Y - PALETTE_H - STATUS_H;
  tft->fillRect(0, WORK_Y, TOOLS_W, h, xp_gray);
  tft->drawFastVLine(TOOLS_W, WORK_Y, h, xp_dark);

  int startX = 6;
  int startY = toolsTopY();
  int i = 0;

  for (int r = 0; r < 5; r++) {
    for (int c = 0; c < TOOL_COLS; c++) {
      Tool id = (Tool)i;
      int bx = startX + c * (TOOL_BTN + TOOL_GAP);
      int by = startY + r * (TOOL_BTN + TOOL_GAP);
      drawToolButton(bx, by, id, tool == id);
      i++;
      if (i >= TOOL_COUNT) break;
    }
    if (i >= TOOL_COUNT) break;
  }
}

static void drawCanvasWithScrollbars() {
  tft->fillRect(CANVAS_X - 2, CANVAS_Y - 2, CANVAS_W + 4, CANVAS_H + 4, xp_dark);
  tft->fillRect(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H, TFT_WHITE);

  int vsX = CANVAS_X + CANVAS_W + 2;
  tft->fillRect(vsX, CANVAS_Y - 2, SCROLL_W, CANVAS_H + 4, xp_panel);
  tft->drawRect(vsX, CANVAS_Y - 2, SCROLL_W, CANVAS_H + 4, xp_dark);

  tft->fillRect(vsX + 1, CANVAS_Y - 1, SCROLL_W - 2, 10, xp_gray);
  tft->fillRect(vsX + 1, CANVAS_Y + CANVAS_H - 9, SCROLL_W - 2, 10, xp_gray);
  tft->fillRect(vsX + 1, CANVAS_Y + 18, SCROLL_W - 2, 20, xp_gray);

  int hsY = CANVAS_Y + CANVAS_H + 2;
  tft->fillRect(CANVAS_X - 2, hsY, CANVAS_W + 4, HSCROLL_H, xp_panel);
  tft->drawRect(CANVAS_X - 2, hsY, CANVAS_W + 4, HSCROLL_H, xp_dark);

  tft->fillRect(CANVAS_X - 1, hsY + 1, 10, HSCROLL_H - 2, xp_gray);
  tft->fillRect(CANVAS_X + CANVAS_W - 9, hsY + 1, 10, HSCROLL_H - 2, xp_gray);
  tft->fillRect(CANVAS_X + 18, hsY + 1, 30, HSCROLL_H - 2, xp_gray);

  tft->fillRect(vsX, hsY, SCROLL_W, HSCROLL_H, xp_gray);
  tft->drawRect(vsX, hsY, SCROLL_W, HSCROLL_H, xp_dark);
}

static void drawPalette() {
  int top = SCREEN_H - STATUS_H - PALETTE_H;
  tft->fillRect(0, top, SCREEN_W, PALETTE_H, xp_panel);
  tft->drawFastHLine(0, top, SCREEN_W, xp_dark);

  int boxX = 6;
  int boxY = top + 6;
  tft->fillRect(boxX, boxY, 18, 18, TFT_WHITE);
  tft->fillRect(boxX + 6, boxY + 6, 18, 18, color);
  tft->drawRect(boxX, boxY, 18, 18, TFT_BLACK);
  tft->drawRect(boxX + 6, boxY + 6, 18, 18, TFT_BLACK);

  int startX = 40;
  int startY = top + 4;

  for (int i = 0; i < PALETTE_N; i++) {
    int r = i / PAL_COLS;
    int c = i % PAL_COLS;
    if (r >= 2) break;

    int px = startX + c * (PAL_SW + PAL_GAP);
    int py = startY + r * (PAL_SH + PAL_GAP);

    uint16_t cc = palette[i];
    tft->fillRect(px, py, PAL_SW, PAL_SH, cc);

    tft->drawFastHLine(px, py, PAL_SW, xp_light);
    tft->drawFastVLine(px, py, PAL_SH, xp_light);
    tft->drawFastHLine(px, py + PAL_SH - 1, PAL_SW, xp_dark);
    tft->drawFastVLine(px + PAL_SW - 1, py, PAL_SH, xp_dark);

    if (i == selectedColorIdx) tft->drawRect(px - 1, py - 1, PAL_SW + 2, PAL_SH + 2, TFT_BLACK);
  }
}

static void drawPaletteSelection(int prevIdx, int newIdx) {
  int top = SCREEN_H - STATUS_H - PALETTE_H;
  int startX = 40;
  int startY = top + 4;

  auto drawSwatchBorder = [&](int idx, bool selected) {
    if (idx < 0 || idx >= PALETTE_N) return;
    int r = idx / PAL_COLS;
    int c = idx % PAL_COLS;
    if (r >= 2) return;
    int px = startX + c * (PAL_SW + PAL_GAP);
    int py = startY + r * (PAL_SH + PAL_GAP);
    if (selected) tft->drawRect(px - 1, py - 1, PAL_SW + 2, PAL_SH + 2, TFT_BLACK);
    else {
      tft->fillRect(px - 1, py - 1, PAL_SW + 2, PAL_SH + 2, xp_panel);
      uint16_t cc = palette[idx];
      tft->fillRect(px, py, PAL_SW, PAL_SH, cc);
      tft->drawFastHLine(px, py, PAL_SW, xp_light);
      tft->drawFastVLine(px, py, PAL_SH, xp_light);
      tft->drawFastHLine(px, py + PAL_SH - 1, PAL_SW, xp_dark);
      tft->drawFastVLine(px + PAL_SW - 1, py, PAL_SH, xp_dark);
    }
  };

  drawSwatchBorder(prevIdx, false);
  drawSwatchBorder(newIdx, true);

  int boxX = 6;
  int boxY = top + 6;
  tft->fillRect(boxX + 6, boxY + 6, 18, 18, color);
  tft->drawRect(boxX + 6, boxY + 6, 18, 18, TFT_BLACK);
}
static bool inCanvas(int x, int y) {
  return (x >= CANVAS_X && x < CANVAS_X + CANVAS_W &&
          y >= CANVAS_Y && y < CANVAS_Y + CANVAS_H);
}

static void toGrid(int x, int y, int &gx, int &gy) {
  gx = (x - CANVAS_X) / PX;
  gy = (y - CANVAS_Y) / PX;
  clampXY(gx, gy);
}

static Tool toolFromTouch(int x, int y) {
  int startX = 6;
  int startY = toolsTopY();

  if (x < 0 || x > TOOLS_W) return TOOL_COUNT;
  if (y < startY) return TOOL_COUNT;

  int relX = x - startX;
  int relY = y - startY;

  int cellW = TOOL_BTN + TOOL_GAP;
  int cellH = TOOL_BTN + TOOL_GAP;

  int col = relX / cellW;
  int row = relY / cellH;

  if (col < 0 || col >= TOOL_COLS) return TOOL_COUNT;
  if (row < 0 || row >= 5) return TOOL_COUNT;

  int bx = startX + col * cellW;
  int by = startY + row * cellH;

  if (x < bx || x >= bx + TOOL_BTN) return TOOL_COUNT;
  if (y < by || y >= by + TOOL_BTN) return TOOL_COUNT;

  int idx = row * TOOL_COLS + col;
  if (idx < 0 || idx >= TOOL_COUNT) return TOOL_COUNT;
  return (Tool)idx;
}

static int paletteIndexFromTouch(int x, int y) {
  int top = SCREEN_H - STATUS_H - PALETTE_H;

  int startX = 40;
  int startY = top + 4;

  if (x < startX || y < startY) return -1;
  if (y >= startY + 2 * (PAL_SH + PAL_GAP)) return -1;

  int relX = x - startX;
  int relY = y - startY;

  int col = relX / (PAL_SW + PAL_GAP);
  int row = relY / (PAL_SH + PAL_GAP);

  int px = startX + col * (PAL_SW + PAL_GAP);
  int py = startY + row * (PAL_SH + PAL_GAP);

  if (x < px || x >= px + PAL_SW) return -1;
  if (y < py || y >= py + PAL_SH) return -1;

  int idx = row * PAL_COLS + col;
  if (idx < 0 || idx >= PALETTE_N) return -1;
  return idx;
}

static bool inSelection(int gx, int gy) {
  return selActive && (gx >= selX && gy >= selY && gx < selX + selW && gy < selY + selH);
}

static void cutSelectionFromCanvas() {

  for (int y = 0; y < selH; y++) {
    for (int x = 0; x < selW; x++) {
      int gx = selX + x;
      int gy = selY + y;
      if (inGrid(gx,gy)) canvas[gy*GW + gx] = TFT_WHITE;
    }
  }
}

static void commitSelectionToCanvas() {
  if (!selActive || !selBuf) return;
  for (int y = 0; y < selBufH; y++) {
    for (int x = 0; x < selBufW; x++) {
      uint16_t c = selBuf[y*selBufW + x];
      int gx = selX + x;
      int gy = selY + y;
      if (inGrid(gx,gy)) canvas[gy*GW + gx] = c;
    }
  }
}

void paint_init(TFT_eSPI* display) {
  tft = display;
  clearCanvas();
  selectedColorIdx = 0;
  color = palette[selectedColorIdx];

  ensureSnapshot();
  ensureFloodFill();

  freeSelection();
}

void paint_draw() {
  tft->fillScreen(xp_gray);

  drawTitle();
  drawMenu();
  drawTools();
  drawCanvasWithScrollbars();
  drawPalette();
  drawStatusBar();

  renderCanvasAll();
}

void paint_tick() {
  uint32_t now = millis();
  if (now - lastStatusTick > 900) {
    system_ui_tick(SCREEN_W - 52 - 92 - 4, 1, xp_blue);
    lastStatusTick = now;
  }
  if (statusDirty || (statusMsg && now > statusMsgUntil)) {
    drawStatusBar();
  }
}

void paint_release() {

  if (!tft) return;

  if (!penDown) return;

  if ((tool == TOOL_RECTSEL || tool == TOOL_SELECT) && selDragging) {

    selDragging = false;

    renderCanvasAll();
  }

  if (startedOnCanvas) {
    if (tool == TOOL_LINE || tool == TOOL_RECT || tool == TOOL_ELLIPSE) {
      int x0 = startGX, y0 = startGY;
      int x1 = lastGX,  y1 = lastGY;

      int r = 0;
      if (tool == TOOL_LINE)   drawLineGrid(x0,y0,x1,y1,color,r);
      if (tool == TOOL_RECT)   drawRectOutlineGrid(x0,y0,x1,y1,color,r);
      if (tool == TOOL_ELLIPSE)drawEllipseOutlineGrid(x0,y0,x1,y1,color,r);

      if (previewActive) {
        renderCanvasRect(prevGX0, prevGY0, prevGX1, prevGY1);
        previewActive = false;
      }
      renderCanvasAll();
    }

    if (tool == TOOL_RECTSEL || tool == TOOL_SELECT) {

      if (previewActive) {
        renderCanvasRect(prevGX0, prevGY0, prevGX1, prevGY1);
        previewActive = false;
      }
      renderCanvasAll();
    }
  }

  penDown = false;
  startedOnCanvas = false;
  lastGX = lastGY = -1;
  previewActive = false;
}

bool paint_handleTouch(int x, int y) {
  if (!tft) return false;

  if (x > SCREEN_W - 16 && y < TITLE_H) return false;

  bool isDownEvent = (!penDown);

  int palIdxAny = paletteIndexFromTouch(x, y);
  if (palIdxAny >= 0) {
    int prevIdx = selectedColorIdx;
    color = palette[palIdxAny];
    selectedColorIdx = palIdxAny;
    if (prevIdx != selectedColorIdx) drawPaletteSelection(prevIdx, selectedColorIdx);
    penDown = false;
    startedOnCanvas = false;
    return true;
  }

  Tool hitAny = toolFromTouch(x, y);
  if (hitAny != TOOL_COUNT) {
    tool = hitAny;
    drawTools();
    selDragging = false;
    penDown = false;
    startedOnCanvas = false;
    return true;
  }

  if (isDownEvent) {
  }

  if (!inCanvas(x, y)) {

    if (isDownEvent) { penDown = true; startedOnCanvas = false; }
    return true;
  }

  int gx, gy;
  toGrid(x, y, gx, gy);

  if (isDownEvent) {
    penDown = true;
    startedOnCanvas = true;
    startGX = gx; startGY = gy;
    lastGX  = gx; lastGY  = gy;

    if (tool == TOOL_LINE || tool == TOOL_RECT || tool == TOOL_ELLIPSE ||
        tool == TOOL_RECTSEL || tool == TOOL_SELECT) {
      previewActive = false;
    }

    if (tool == TOOL_FILL) {

      floodFill(gx, gy, color);
      renderCanvasAll();
      return true;
    }

    if (tool == TOOL_TEXT) {

      tft->setTextColor(color, TFT_WHITE);
      tft->drawChar(CANVAS_X + gx*PX, CANVAS_Y + gy*PX, 'A', color, TFT_WHITE, 2);

      stamp(gx, gy, color, 1);
      return true;
    }

    if (tool == TOOL_RECTSEL || tool == TOOL_SELECT) {

      if (selActive && inSelection(gx,gy)) {
        selDragging = true;
        selGrabOffX = gx - selX;
        selGrabOffY = gy - selY;
        return true;
      } else {

        if (selActive) {
          commitSelectionToCanvas();
          freeSelection();
        }
        selActive = false;
        selDragging = false;

        return true;
      }
    }

    if (tool == TOOL_PENCIL) {
      setPixel(gx, gy, color);
      return true;
    }
    if (tool == TOOL_BRUSH) {
      stamp(gx, gy, color, 1);
      return true;
    }
    if (tool == TOOL_ERASE) {
      stamp(gx, gy, TFT_WHITE, 1);
      return true;
    }

    return true;
  }

  if (!penDown) return true;

  int prevGX = lastGX;
  int prevGY = lastGY;
  lastGX = gx; lastGY = gy;

  if ((tool == TOOL_RECTSEL || tool == TOOL_SELECT) && selDragging && selBuf) {

    int newX = gx - selGrabOffX;
    int newY = gy - selGrabOffY;

    if (newX < 0) newX = 0;
    if (newY < 0) newY = 0;
    if (newX + selW > GW) newX = GW - selW;
    if (newY + selH > GH) newY = GH - selH;

    selX = newX;
    selY = newY;

    renderCanvasRect(prevGX0, prevGY0, prevGX1, prevGY1);

    drawRectOutlineGrid(selX, selY, selX+selW-1, selY+selH-1, TFT_BLACK, 0);
    return true;
  }

  if (tool == TOOL_PENCIL) {
    drawLineGrid(prevGX, prevGY, gx, gy, color, 0);
    return true;
  }

  if (tool == TOOL_BRUSH) {
    drawLineGrid(prevGX, prevGY, gx, gy, color, 1);
    return true;
  }

  if (tool == TOOL_ERASE) {
    drawLineGrid(prevGX, prevGY, gx, gy, TFT_WHITE, 1);
    return true;
  }

  if (tool == TOOL_LINE || tool == TOOL_RECT || tool == TOOL_ELLIPSE ||
      tool == TOOL_RECTSEL || tool == TOOL_SELECT) {
    int x0 = startGX, y0 = startGY;
    int x1 = gx,     y1 = gy;

    auto previewStamp = [&](int pgx,int pgy,uint16_t pc,int pr){
      for(int yy=pgy-pr; yy<=pgy+pr; yy++){
        for(int xx=pgx-pr; xx<=pgx+pr; xx++){
          if(inGrid(xx,yy)) renderPixel(xx,yy,pc);
        }
      }
    };

    auto previewLine = [&](int ax,int ay,int bx,int by,uint16_t pc,int pr){
      int dx = abs(bx - ax), sx = ax < bx ? 1 : -1;
      int dy = -abs(by - ay), sy = ay < by ? 1 : -1;
      int err = dx + dy;
      while(true){
        previewStamp(ax,ay,pc,pr);
        if(ax==bx && ay==by) break;
        int e2 = 2*err;
        if(e2 >= dy){ err += dy; ax += sx; }
        if(e2 <= dx){ err += dx; ay += sy; }
      }
    };

    auto previewRect = [&](int ax,int ay,int bx,int by,uint16_t pc,int pr){
      if(ax>bx){int t=ax;ax=bx;bx=t;}
      if(ay>by){int t=ay;ay=by;by=t;}
      previewLine(ax,ay,bx,ay,pc,pr);
      previewLine(bx,ay,bx,by,pc,pr);
      previewLine(bx,by,ax,by,pc,pr);
      previewLine(ax,by,ax,ay,pc,pr);
    };

    auto previewEllipse = [&](int ax,int ay,int bx,int by,uint16_t pc,int pr){
      if(ax>bx){int t=ax;ax=bx;bx=t;}
      if(ay>by){int t=ay;ay=by;by=t;}
      float cx = (ax + bx) * 0.5f;
      float cy = (ay + by) * 0.5f;
      float rx = max(1.0f, (bx - ax) * 0.5f);
      float ry = max(1.0f, (by - ay) * 0.5f);
      int steps = (int)(6.0f * (rx + ry));
      if(steps<32) steps=32;
      if(steps>240) steps=240;
      for(int i=0;i<steps;i++){
        float a = (2.0f * 3.1415926f * i) / steps;
        int ex = (int)roundf(cx + cosf(a)*rx);
        int ey = (int)roundf(cy + sinf(a)*ry);
        previewStamp(ex,ey,pc,pr);
      }
    };

    int gx0 = min(x0, x1) - 1;
    int gy0 = min(y0, y1) - 1;
    int gx1 = max(x0, x1) + 1;
    int gy1 = max(y0, y1) + 1;

    if (previewActive) {
      renderCanvasRect(prevGX0, prevGY0, prevGX1, prevGY1);
    }

    if (tool == TOOL_LINE) {
      previewLine(x0,y0,x1,y1,TFT_BLACK,0);
    } else if (tool == TOOL_RECT) {
      previewRect(x0,y0,x1,y1,TFT_BLACK,0);
    } else if (tool == TOOL_ELLIPSE) {
      previewEllipse(x0,y0,x1,y1,TFT_BLACK,0);
    } else if (tool == TOOL_RECTSEL || tool == TOOL_SELECT) {
      previewRect(x0,y0,x1,y1,TFT_BLACK,0);
    }

    previewActive = true;
    prevGX0 = gx0;
    prevGY0 = gy0;
    prevGX1 = gx1;
    prevGY1 = gy1;

    return true;
  }

  return true;
}

static void finalizeSelectionFromDrag() {

  int x0 = startGX, y0 = startGY;
  int x1 = lastGX,  y1 = lastGY;
  if (x0 > x1) { int t=x0; x0=x1; x1=t; }
  if (y0 > y1) { int t=y0; y0=y1; y1=t; }

  int w = x1 - x0 + 1;
  int h = y1 - y0 + 1;
  if (w < 1 || h < 1) return;

  if (w > GW) w = GW;
  if (h > GH) h = GH;

  freeSelection();
  selBufW = w;
  selBufH = h;
  selBuf = (uint16_t*)malloc(sizeof(uint16_t) * w * h);
  if (!selBuf) {
    selBufW = selBufH = 0;
    selActive = false;
    return;
  }

  selX = x0; selY = y0;
  selW = w;  selH = h;
  selActive = true;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      selBuf[y*w + x] = getPixel(selX + x, selY + y);
    }
  }

  cutSelectionFromCanvas();

  renderCanvasAll();

  drawRectOutlineGrid(selX, selY, selX+selW-1, selY+selH-1, TFT_BLACK, 0);
}
