#include "internet_app.h"
#include "windows.h"
#include "system_ui.h"

static TFT_eSPI* tft = nullptr;

static const uint16_t XP_BG     = 0xC618;
static const uint16_t XP_BORDER = 0x7BEF;
static const uint16_t XP_WHITE  = 0xFFFF;
static const uint16_t XP_BLACK  = 0x0000;
static const uint16_t XP_BLUE1  = 0x1C9F;
static const uint16_t XP_BLUE2  = 0x047F;
static const uint16_t XP_RED    = 0xF800;

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;

static const int WIN_X = 0;
static const int WIN_Y = 0;
static const int WIN_W = SCREEN_W;
static const int WIN_H = SCREEN_H;

static const int PAD     = 4;
static const int TITLE_H = 20;

static const int CONTENT_X = WIN_X + PAD;
static const int CONTENT_Y = WIN_Y + TITLE_H + PAD;
static const int CONTENT_W = WIN_W - PAD*2;
static const int CONTENT_H = WIN_H - TITLE_H - PAD*2;

static bool opened  = false;

static const int IMG_W = WINDOWS_WIDTH;
static const int IMG_H = WINDOWS_HEIGHT;
static const int IMG_PAD = 6;

static const int MAX_LINES  = 120;
static const int LINE_CHARS = 44;

static char pageLines[MAX_LINES][LINE_CHARS + 1];
static int  lineCount  = 0;
static int  scrollLine = 0;

static int statusX = 0;
static int statusY = 0;
static uint32_t lastStatusTick = 0;

static inline bool inRect(int x,int y,int rx,int ry,int rw,int rh){
  return (x>=rx && x<rx+rw && y>=ry && y<ry+rh);
}

static void clearLines() {
  for (int i=0;i<MAX_LINES;i++) pageLines[i][0] = 0;
  lineCount = 0;
  scrollLine = 0;
}

static void addLineC(const String& s) {
  if (lineCount >= MAX_LINES) return;
  String t = s;
  t.trim();
  if (t.length() == 0) return;
  t.toCharArray(pageLines[lineCount], LINE_CHARS + 1);
  pageLines[lineCount][LINE_CHARS] = 0;
  lineCount++;
}

static void addWrapped(const String& s) {
  String t = s;
  while (t.length() > 0 && lineCount < MAX_LINES) {
    if ((int)t.length() <= LINE_CHARS) {
      addLineC(t);
      return;
    }

    int cut = LINE_CHARS;
    int sp = t.lastIndexOf(' ', LINE_CHARS);
    if (sp > 8) cut = sp;

    addLineC(t.substring(0, cut));
    t = t.substring(cut);
    t.trim();
  }
}

static void buildFakePage() {
  clearLines();
  addWrapped("Windows XP is a major release of Microsoft's Windows NT "
             "operating system. It was released to manufacturing on "
             "August 24, 2001, and later to retail on October 25, 2001.");
  addLineC("");
  addWrapped("Development of Windows XP began in the late 1990s under the "
             "codename \"Neptune\", built on the Windows NT kernel and "
             "intended for mainstream consumer use.");
  addLineC("");
  addWrapped("Upon its release, Windows XP received critical acclaim, "
             "noting improved performance and stability compared to "
             "Windows Me, a more intuitive interface, and expanded "
             "multimedia capabilities.");
  addLineC("");
  addWrapped("Mainstream support ended on April 14, 2009, and extended "
             "support ended on April 8, 2014. Security updates for some "
             "embedded editions continued until April 2019.");
}

static void drawTitleBar() {
  tft->fillRect(WIN_X, WIN_Y, WIN_W, TITLE_H, XP_BLUE2);
  tft->fillRect(WIN_X, WIN_Y, WIN_W, TITLE_H/2, XP_BLUE1);

  tft->setTextColor(XP_WHITE, XP_BLUE2);
  tft->setTextDatum(ML_DATUM);
  tft->drawString("Wikipedia: Windows XP", WIN_X + 8, WIN_Y + TITLE_H/2, 2);
  tft->setTextDatum(TL_DATUM);

  int cx = WIN_W - PAD - 18;
  int cy = 1;
  tft->fillRect(cx, cy, 18, 18, XP_RED);
  tft->drawRect(cx, cy, 18, 18, XP_WHITE);
  tft->setTextColor(XP_WHITE, XP_RED);
  tft->setTextDatum(MC_DATUM);
  tft->drawString("X", cx + 9, cy + 9, 2);
  tft->setTextDatum(TL_DATUM);

  statusX = WIN_W - PAD - 18 - 6 - 90;
  statusY = 2;
  if (statusX < WIN_X + 4) statusX = WIN_X + 4;
  system_ui_draw_status(statusX, statusY, XP_BLUE2);
}

static void drawContentFrame() {
  tft->fillRect(CONTENT_X, CONTENT_Y, CONTENT_W, CONTENT_H, XP_WHITE);
  tft->drawRect(CONTENT_X, CONTENT_Y, CONTENT_W, CONTENT_H, XP_BORDER);
}

static void drawPage() {
  drawContentFrame();

  int x0 = CONTENT_X + 6;
  int y0 = CONTENT_Y + 6;

  tft->setTextColor(XP_BLACK, XP_WHITE);
  tft->drawString("Windows XP", x0, y0, 4);
  y0 += 20;

  tft->setTextColor(0x001F, XP_WHITE);
  tft->drawString("Article", x0, y0, 2);
  tft->drawString("Talk", x0 + 50, y0, 2);
  tft->drawFastHLine(x0, y0 + 14, CONTENT_W - 12, XP_BORDER);
  y0 += 18;

  tft->setTextColor(XP_BLACK, XP_WHITE);
  tft->drawString("From Wikipedia, the free encyclopedia", x0, y0, 2);
  y0 += 16;

  int imgX = CONTENT_X + 8;
  int imgY = y0 + 4;
  tft->drawRect(imgX - 2, imgY - 2, IMG_W + 4, IMG_H + 4, XP_BORDER);
  tft->pushImage(imgX, imgY, IMG_W, IMG_H, windows_map);

  int boxX = imgX + IMG_W + 8;
  int boxY = imgY;
  int boxW = CONTENT_X + CONTENT_W - boxX - 6;
  int boxH = IMG_H + 4;
  if (boxW < 70) boxW = 70;
  tft->drawRect(boxX, boxY, boxW, boxH, XP_BORDER);
  tft->setTextColor(XP_BLACK, XP_WHITE);
  tft->drawString("Developer", boxX + 4, boxY + 4, 2);
  tft->setTextColor(0x001F, XP_WHITE);
  tft->drawString("Microsoft", boxX + 4, boxY + 18, 2);
  tft->setTextColor(XP_BLACK, XP_WHITE);
  tft->drawString("Released", boxX + 4, boxY + 34, 2);
  tft->setTextColor(XP_BLACK, XP_WHITE);
  tft->drawString("Oct 25, 2001", boxX + 4, boxY + 48, 2);

  int textStartY = imgY + IMG_H + IMG_PAD + 4;
  int x = CONTENT_X + 6;
  int y = textStartY;

  const int lineH = 14;
  int bottomY = CONTENT_Y + CONTENT_H - 6;
  int visible = (bottomY - y) / lineH;
  if (visible < 1) visible = 1;

  if (scrollLine < 0) scrollLine = 0;
  int maxScroll = lineCount - visible;
  if (maxScroll < 0) maxScroll = 0;
  if (scrollLine > maxScroll) scrollLine = maxScroll;

  tft->setTextColor(XP_BLACK, XP_WHITE);

  if (lineCount == 0) {
    tft->drawString("(no text loaded)", x, y, 2);
    return;
  }

  for (int i=0;i<visible;i++) {
    int idx = scrollLine + i;
    if (idx >= lineCount) break;
    tft->drawString(pageLines[idx], x, y, 2);
    y += lineH;
  }
}

static void drawAllUI() {
  tft->fillScreen(XP_BG);
  drawTitleBar();
  drawContentFrame();
  drawPage();
}

void internet_app_init(TFT_eSPI* display) {
  tft = display;
  buildFakePage();
}

bool internet_app_isOpen() { return opened; }

void internet_app_open() {
  if (!tft) return;
  opened = true;
  scrollLine = 0;
  drawAllUI();
}

void internet_app_tick() {
  if (!opened) return;
  uint32_t now = millis();
  if (now - lastStatusTick > 900) {
    system_ui_tick(statusX, statusY, XP_BLUE2);
    lastStatusTick = now;
  }
}

bool internet_app_handleTouch(bool pressed, bool lastPressed, int x, int y) {
  if (!opened) return false;

  if (!(pressed && !lastPressed)) return true;

  int titleCloseX = WIN_W - PAD - 18;
  if (inRect(x,y, titleCloseX, 1, 18, 18)) {
    opened = false;
    return false;
  }

  if (inRect(x,y, CONTENT_X, CONTENT_Y, CONTENT_W, CONTENT_H)) {
    if (y < CONTENT_Y + CONTENT_H/2) scrollLine--;
    else scrollLine++;
    drawPage();
    return true;
  }

  return true;
}
