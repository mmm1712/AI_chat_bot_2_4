#include "wifi_app.h"
#include "system_ui.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <cstring>

#ifdef LIST_H
#undef LIST_H
#endif

static TFT_eSPI* tft = nullptr;

static bool opened = false;

enum WifiMode { WIFI_MODE_LIST, WIFI_MODE_CONNECT };
static WifiMode mode = WIFI_MODE_LIST;

static bool passVisible = false;
static bool kbNumbers   = false;

static String selectedSSID = "";
static String passInput    = "";

static bool connecting = false;
static uint32_t connectStartMs = 0;
static bool connectRequested = false;
static String connectSSID = "";
static String connectPASS = "";

static String savedSSID = "";

// Networks
static const int MAX_NET = 12;
static String  ssidList[MAX_NET];
static int     rssiList[MAX_NET];
static uint8_t authList[MAX_NET];
static int     countNet = 0;
static int     selected = -1;
static int     scroll = 0;

static const char* WIFI_NS  = "wifi";
static const char* KEY_SSID = "ssid";
static const char* KEY_PASS = "pass";

static const uint16_t XP_BG     = 0xC618;
static const uint16_t XP_BORDER = 0x7BEF;
static const uint16_t XP_WHITE  = 0xFFFF;
static const uint16_t XP_BLACK  = 0x0000;
static const uint16_t XP_BLUE1  = 0x1C9F;
static const uint16_t XP_BLUE2  = 0x047F;

static const int SCREEN_W = 320;
static const int SCREEN_H = 240;

static const int WIN_X = 0;
static const int WIN_Y = 0;
static const int WIN_W = SCREEN_W;
static const int WIN_H = SCREEN_H;

static const int TITLE_H = 20;
static const int PAD = 4;

static const int CLOSE_W = 18;
static const int CLOSE_H = 18;

static const int BTN_H = 20;
static const int BTN_W = 92;

static const int BTN_Y = WIN_Y + WIN_H - PAD - BTN_H;
static const int BTN_REFRESH_X = WIN_X + PAD;
static const int BTN_CONNECT_X = WIN_X + (WIN_W/2) - (BTN_W/2);
static const int BTN_BACK_X    = WIN_X + WIN_W - PAD - BTN_W;

static const int STATUS_Y = BTN_Y - 16;

static const int CONTENT_X = WIN_X + PAD;
static const int CONTENT_Y = WIN_Y + TITLE_H + PAD;
static const int CONTENT_W = WIN_W - PAD*2;

static const int NETLIST_X = CONTENT_X;
static const int NETLIST_Y = CONTENT_Y;
static const int NETLIST_W = CONTENT_W;
static const int NETLIST_H = (STATUS_Y - 6) - NETLIST_Y;

static int statusX = 0;
static int statusY = 0;
static uint32_t lastStatusTick = 0;

// CONNECT UI
static const int SSID_LABEL_Y = CONTENT_Y + 2;
static const int SSID_BOX_Y   = SSID_LABEL_Y + 14;
static const int SSID_BOX_H   = 20;

static const int PASS_LABEL_Y = SSID_BOX_Y + SSID_BOX_H + 8;
static const int PASS_Y       = PASS_LABEL_Y + 14;
static const int PASS_H       = 20;

static const int SEE_W   = 58;
static const int SEE_GAP = 6;
static const int SEE_H   = PASS_H;

static const int PASS_X = CONTENT_X;
static const int PASS_W = (CONTENT_W) - (SEE_GAP + SEE_W);
static const int SEE_X  = PASS_X + PASS_W + SEE_GAP;
static const int SEE_Y  = PASS_Y;

static const int KB_X = CONTENT_X;
static const int KB_Y = PASS_Y + PASS_H + 8;
static const int KB_W = CONTENT_W;

static const int KB_H = (STATUS_Y - 4) - KB_Y;

static bool scanning    = false;
static bool scanAbort   = false;
static uint32_t scanStartMs = 0;

// retry scheduling
static uint8_t  scanRetry = 0;
static bool     scanRetryPending = false;
static uint32_t scanRetryAtMs = 0;

static const int KB_GAP = 3;

static inline bool inRect(int x,int y,int rx,int ry,int rw,int rh){
  return (x>=rx && x<rx+rw && y>=ry && y<ry+rh);
}
static inline bool inRectPad(int x,int y,int rx,int ry,int rw,int rh,int pad){
  return (x>=rx-pad && x<rx+rw+pad && y>=ry-pad && y<ry+rh+pad);
}

static int kbRowH() {
  // total gaps between 4 rows = 3 gaps
  int h = (KB_H - 3*KB_GAP) / 4;

  if (h < 12) h = 12;

  return h;
}

// ============================================================
// NVS helpers
// ============================================================
static void nvsSaveWifi(const String& ssid, const String& pass) {
  Preferences p;
  p.begin(WIFI_NS, false);
  p.putString(KEY_SSID, ssid);
  p.putString(KEY_PASS, pass);
  p.end();
}

static bool nvsLoadWifi(String& ssidOut, String& passOut) {
  Preferences p;
  p.begin(WIFI_NS, true);
  ssidOut = p.getString(KEY_SSID, "");
  passOut = p.getString(KEY_PASS, "");
  p.end();
  ssidOut.trim();
  passOut.trim();
  return (ssidOut.length() > 0);
}

static void nvsClearWifi() {
  Preferences p;
  p.begin(WIFI_NS, false);
  p.remove(KEY_SSID);
  p.remove(KEY_PASS);
  p.end();

  WiFi.disconnect(true);
}

void wifi_app_forget_saved() {
  nvsClearWifi();
}

// ============================================================
// Drawing primitives
// ============================================================
static void drawBevelRect(int x,int y,int w,int h, uint16_t fill, bool pressed) {
  uint16_t top = pressed ? XP_BORDER : XP_WHITE;
  uint16_t bot = pressed ? XP_WHITE  : XP_BORDER;

  tft->fillRect(x, y, w, h, fill);
  tft->drawFastHLine(x, y, w, top);
  tft->drawFastVLine(x, y, h, top);
  tft->drawFastHLine(x, y+h-1, w, bot);
  tft->drawFastVLine(x+w-1, y, h, bot);
}

static void drawButton(int x,int y,int w,int h,const char* txt, bool pressed=false) {
  drawBevelRect(x,y,w,h,XP_BG,pressed);
  tft->setTextColor(XP_BLACK, XP_BG);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(txt, x + w/2, y + h/2, 2);
  tft->setTextDatum(TL_DATUM);
}

static void drawStatus(const String& s) {
  if (!opened) return;
  tft->fillRect(CONTENT_X, STATUS_Y, CONTENT_W, 14, XP_BG);
  tft->setTextColor(XP_BLACK, XP_BG);
  tft->drawString(s, CONTENT_X, STATUS_Y, 2);
}

static void drawWindowFrame(const char* title) {
  tft->fillRect(WIN_X, WIN_Y, WIN_W, WIN_H, XP_BG);
  tft->drawRect(WIN_X, WIN_Y, WIN_W, WIN_H, XP_BORDER);

  tft->fillRect(WIN_X+1, WIN_Y+1, WIN_W-2, TITLE_H, XP_BLUE2);
  tft->fillRect(WIN_X+1, WIN_Y+1, WIN_W-2, TITLE_H/2, XP_BLUE1);

  tft->setTextColor(XP_WHITE, XP_BLUE2);
  tft->setTextDatum(ML_DATUM);
  tft->drawString(title, WIN_X + 10, WIN_Y + TITLE_H/2 + 1, 2);
  tft->setTextDatum(TL_DATUM);

  statusX = WIN_X + WIN_W - CLOSE_W - 6 - 90;
  statusY = WIN_Y + 2;
  if (statusX < WIN_X + 4) statusX = WIN_X + 4;
  system_ui_draw_status(statusX, statusY, XP_BLUE2);

  int cx = WIN_X + WIN_W - PAD - CLOSE_W;
  int cy = WIN_Y + 1;
  tft->fillRect(cx, cy, CLOSE_W, CLOSE_H, 0xF800);
  tft->drawRect(cx, cy, CLOSE_W, CLOSE_H, XP_WHITE);
  tft->setTextColor(XP_WHITE, 0xF800);
  tft->setTextDatum(MC_DATUM);
  tft->drawString("X", cx + CLOSE_W/2, cy + CLOSE_H/2, 2);
  tft->setTextDatum(TL_DATUM);
}

// ============================================================
// List mode drawing
// ============================================================
static void drawListBox() {
  tft->fillRect(NETLIST_X, NETLIST_Y, NETLIST_W, NETLIST_H, XP_WHITE);
  tft->drawRect(NETLIST_X, NETLIST_Y, NETLIST_W, NETLIST_H, XP_BORDER);
}

static void drawList() {
  tft->fillRect(NETLIST_X+1, NETLIST_Y+1, NETLIST_W-2, NETLIST_H-2, XP_WHITE);

  const int rowH = 20;
  int visibleRows = (NETLIST_H-2) / rowH;

  int maxScroll = countNet - visibleRows;
  if (maxScroll < 0) maxScroll = 0;
  if (scroll < 0) scroll = 0;
  if (scroll > maxScroll) scroll = maxScroll;

  for (int i = 0; i < visibleRows; i++) {
    int idx = scroll + i;
    if (idx >= countNet) break;

    int ry = NETLIST_Y + 2 + i*rowH;
    bool sel = (idx == selected);

    if (sel) {
      tft->fillRect(NETLIST_X+2, ry, NETLIST_W-4, rowH, XP_BLUE2);
      tft->setTextColor(XP_WHITE, XP_BLUE2);
    } else {
      tft->setTextColor(XP_BLACK, XP_WHITE);
    }

    String line = ssidList[idx];
    if (line.length() > 24) line = line.substring(0, 24) + "...";
    tft->drawString(line, NETLIST_X + 6, ry + 2, 2);

    int rssi = rssiList[idx];
    int bars = 0;
    if (rssi > -60) bars = 4;
    else if (rssi > -70) bars = 3;
    else if (rssi > -80) bars = 2;
    else if (rssi > -90) bars = 1;

    int bx = NETLIST_X + NETLIST_W - 34;
    int by = ry + rowH - 4;
    for (int b=0;b<4;b++){
      int h = (b+1)*3;
      uint16_t col = (b < bars) ? (sel ? XP_WHITE : XP_BLACK) : XP_BORDER;
      tft->fillRect(bx + b*6, by - h, 4, h, col);
    }
  }
}

// ============================================================
// Scan helpers (more robust)
// ============================================================
static void stopScanNow() {
  int st = WiFi.scanComplete();
  if (st == -1) { // running
    WiFi.scanDelete();
    delay(120);
  } else {
    WiFi.scanDelete();
    delay(50);
  }
  scanning = false;
}

static void wifiDriverResetForScan() {
  // common workaround when scanNetworks() returns -2
  WiFi.disconnect(false);
  delay(50);
  WiFi.mode(WIFI_OFF);
  delay(180);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  delay(180);
}

static void scheduleScanRetry(uint32_t delayMs, const String& msg) {
  if (scanRetry < 3) scanRetry++;
  scanRetryPending = true;
  scanRetryAtMs = millis() + delayMs;
  if (opened && mode == WIFI_MODE_LIST) drawStatus(msg);
}

static void startScanAsync() {
  if (connecting) { drawStatus("Busy connecting..."); return; }
  if (connectRequested) { drawStatus("Busy connecting..."); return; }
  if (!opened || mode != WIFI_MODE_LIST) return;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  stopScanNow();

  scanAbort = false;
  drawStatus("Scanning...");
  scanStartMs = millis();

  int r = WiFi.scanNetworks(true, false); // async=true, show_hidden=false

  if (r == -2) {
    // driver reset then schedule retry
    wifiDriverResetForScan();
    scheduleScanRetry(350, "Scan start failed — retrying...");
    return;
  }

  scanning = true;

  if (r >= 0) {
    // completed immediately (rare)
    // we'll harvest on next tick (or now)
  }
}

static void harvestScanResults(int n) {
  countNet = 0;
  selected = -1;
  selectedSSID = "";
  scroll = 0;

  for (int i = 0; i < n && countNet < MAX_NET; i++) {
    String s = WiFi.SSID(i);
    s.trim();
    if (s.length() == 0) continue;

    ssidList[countNet] = s;
    rssiList[countNet] = WiFi.RSSI(i);
    authList[countNet] = (uint8_t)WiFi.encryptionType(i);
    countNet++;
  }

  WiFi.scanDelete();
  delay(10);

  String prefer = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : savedSSID;
  prefer.trim();
  if (prefer.length() > 0) {
    for (int i=0;i<countNet;i++){
      if (ssidList[i] == prefer) {
        selected = i;
        selectedSSID = prefer;
        break;
      }
    }
  }

  if (opened && mode == WIFI_MODE_LIST) {
    if (countNet == 0) drawStatus("Found 0 (2.4GHz only!)");
    else drawStatus(String("Found ") + countNet + " networks");
    drawList();
  }

  // success resets retry counter
  scanRetry = 0;
  scanRetryPending = false;
}

static void pollScanComplete() {
  if (!scanning) return;

  int n = WiFi.scanComplete();

  if (n == -1) {
    // running
    if (millis() - scanStartMs > 15000) {
      scanning = false;
      WiFi.scanDelete();
      scheduleScanRetry(350, "Scan timeout — retrying...");
    }
    return;
  }

  if (n < 0) {
    // failed (-2)
    scanning = false;
    WiFi.scanDelete();
    scheduleScanRetry(350, "Scan failed — retrying...");
    return;
  }

  // complete
  scanning = false;

  if (scanAbort) {
    scanAbort = false;
    WiFi.scanDelete();
    return;
  }

  harvestScanResults(n);
}

// ============================================================
// Connect mode: password + See/Hide + keyboard
// ============================================================
static void redrawPassFieldOnly();
static void drawPassError(const char* msg);

static void drawPasswordBox() {
  tft->setTextColor(XP_BLACK, XP_BG);
  tft->drawString("Password:", CONTENT_X, PASS_LABEL_Y, 2);

  tft->fillRect(PASS_X, PASS_Y, PASS_W, PASS_H, XP_WHITE);
  tft->drawRect(PASS_X, PASS_Y, PASS_W, PASS_H, XP_BORDER);

  drawButton(SEE_X, SEE_Y, SEE_W, SEE_H, passVisible ? "Hide" : "See");
  redrawPassFieldOnly();
}

static void redrawPassFieldOnly() {
  tft->fillRect(PASS_X+1, PASS_Y+1, PASS_W-2, PASS_H-2, XP_WHITE);

  String shown;
  if (passVisible) shown = passInput;
  else {
    shown.reserve(passInput.length());
    for (int i=0;i<passInput.length();i++) shown += "*";
  }

  const int maxChars = 22;
  if (shown.length() > maxChars) {
    shown = "..." + shown.substring(shown.length() - maxChars);
  }

  tft->setTextColor(XP_BLACK, XP_WHITE);
  tft->drawString(shown, PASS_X + 4, PASS_Y + 2, 2);
}

static void drawPassError(const char* msg) {
  // Draw error text inside the password box so we don't disturb the keyboard.
  tft->fillRect(PASS_X+1, PASS_Y+1, PASS_W-2, PASS_H-2, XP_WHITE);
  tft->setTextColor(0xF800, XP_WHITE); // red
  tft->drawString(msg, PASS_X + 4, PASS_Y + 2, 2);
}

static void drawKey(int x,int y,int w,int h,const char* label, bool pressed=false) {
  drawBevelRect(x,y,w,h,XP_BG,pressed);
  tft->setTextColor(XP_BLACK, XP_BG);
  tft->setTextDatum(MC_DATUM);
  tft->drawString(label, x + w/2, y + h/2, 2);
  tft->setTextDatum(TL_DATUM);
}

static const char* kbRow1() { return kbNumbers ? "1234567890"     : "qwertyuiop"; }
static const char* kbRow2() { return kbNumbers ? "@#$%&*()-_="    : "asdfghjkl";  }
static const char* kbRow3() { return kbNumbers ? ".,:;!?/+\\"     : "zxcvbnm";    }

static void drawKeyboard() {
  // Background
  tft->fillRect(KB_X, KB_Y, KB_W, KB_H, XP_BG);

  const char* row1 = kbRow1();
  const char* row2 = kbRow2();
  const char* row3 = kbRow3();

  const int rowH = kbRowH();
  const int gap  = KB_GAP;

  // Start at the top of the keyboard box to avoid overlapping status text
  int y = KB_Y;

  // Row 1
  {
    int n = (int)strlen(row1);
    int keyW = (KB_W - (n-1)*gap) / n;
    int x = KB_X;
    for (int i=0;i<n;i++){
      char tmp[2] = { row1[i], 0 };
      drawKey(x, y, keyW, rowH, tmp, false);
      x += keyW + gap;
    }
    y += rowH + gap;
  }

  // Row 2
  {
    int n = (int)strlen(row2);
    int keyW = (KB_W - (n-1)*gap) / n;
    int x = KB_X;
    for (int i=0;i<n;i++){
      char tmp[2] = { row2[i], 0 };
      drawKey(x, y, keyW, rowH, tmp, false);
      x += keyW + gap;
    }
    y += rowH + gap;
  }

  // Row 3 + Del
  {
    int n = (int)strlen(row3);
    int delW = 70;
    int keyW = (KB_W - delW - (n*gap)) / n;

    int x = KB_X;
    for (int i=0;i<n;i++){
      char tmp[2] = { row3[i], 0 };
      drawKey(x, y, keyW, rowH, tmp, false);
      x += keyW + gap;
    }
    drawKey(x, y, delW, rowH, "Del", false);
    y += rowH + gap;
  }

  // Bottom row
  {
    int toggleW = 70;
    int clrW    = 70;
    int okW     = 70;

    int x = KB_X;
    drawKey(x, y, toggleW, rowH, kbNumbers ? "ABC" : "123", false);
    x += toggleW + gap;

    int spaceW = KB_W - (toggleW + gap + clrW + gap + okW);
    drawKey(x, y, spaceW, rowH, "Space", false);

    int clrX = KB_X + KB_W - (clrW + gap + okW);
    drawKey(clrX, y, clrW, rowH, "Clr", false);

    int okX = clrX + clrW + gap;
    drawKey(okX, y, okW, rowH, "OK", false);
  }
}

enum KbType { KB_NONE, KB_CHAR, KB_DEL, KB_TOGGLE, KB_SPACE, KB_CLR, KB_OK };

struct KbHit {
  KbType type = KB_NONE;
  char ch = 0;
  int x=0,y=0,w=0,h=0;
};

static KbHit kbActive;
static bool  kbHasActive = false;

static void drawKbHit(const KbHit& h, bool pressed) {
  if (h.type == KB_NONE) return;

  if (h.type == KB_CHAR) {
    char tmp[2] = { h.ch, 0 };
    drawKey(h.x, h.y, h.w, h.h, tmp, pressed);
  } else if (h.type == KB_DEL)    drawKey(h.x, h.y, h.w, h.h, "Del", pressed);
  else if (h.type == KB_TOGGLE)   drawKey(h.x, h.y, h.w, h.h, kbNumbers ? "ABC" : "123", pressed);
  else if (h.type == KB_SPACE)    drawKey(h.x, h.y, h.w, h.h, "Space", pressed);
  else if (h.type == KB_CLR)      drawKey(h.x, h.y, h.w, h.h, "Clr", pressed);
  else if (h.type == KB_OK)       drawKey(h.x, h.y, h.w, h.h, "OK", pressed);
}

static bool sameHit(const KbHit& a, const KbHit& b) {
  return a.type==b.type && a.ch==b.ch && a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h;
}

static KbHit kbHitTest(int x, int y) {
  KbHit h;
  if (!inRect(x,y,KB_X,KB_Y,KB_W,KB_H)) return h;

  const int pad = 6;
  const int rowH = kbRowH();
  const int gap  = KB_GAP;

  const char* row1 = kbRow1();
  const char* row2 = kbRow2();
  const char* row3 = kbRow3();

  // Row1
  {
    int n = (int)strlen(row1);
    int keyW = (KB_W - (n-1)*gap) / n;
    int y0 = KB_Y;
    int kx = KB_X;
    for (int i=0;i<n;i++){
      if (inRectPad(x,y,kx,y0,keyW,rowH,pad)) {
        h.type=KB_CHAR; h.ch=row1[i]; h.x=kx; h.y=y0; h.w=keyW; h.h=rowH;
        return h;
      }
      kx += keyW + gap;
    }
  }

  // Row2
  {
    int n = (int)strlen(row2);
    int keyW = (KB_W - (n-1)*gap) / n;
    int y0 = KB_Y + (rowH+gap);
    int kx = KB_X;
    for (int i=0;i<n;i++){
      if (inRectPad(x,y,kx,y0,keyW,rowH,pad)) {
        h.type=KB_CHAR; h.ch=row2[i]; h.x=kx; h.y=y0; h.w=keyW; h.h=rowH;
        return h;
      }
      kx += keyW + gap;
    }
  }

  // Row3 + Del
  {
    int y0 = KB_Y + 2*(rowH+gap);
    int n = (int)strlen(row3);
    int delW = 70;
    int keyW = (KB_W - delW - (n*gap)) / n;

    int kx = KB_X;
    for (int i=0;i<n;i++){
      if (inRectPad(x,y,kx,y0,keyW,rowH,pad)) {
        h.type=KB_CHAR; h.ch=row3[i]; h.x=kx; h.y=y0; h.w=keyW; h.h=rowH;
        return h;
      }
      kx += keyW + gap;
    }
    if (inRectPad(x,y,kx,y0,delW,rowH,pad)) {
      h.type=KB_DEL; h.x=kx; h.y=y0; h.w=delW; h.h=rowH;
      return h;
    }
  }

  // Bottom row
  {
    int y0 = KB_Y + 3*(rowH+gap);

    int toggleW = 70;
    int clrW    = 70;
    int okW     = 70;

    int x0 = KB_X;
    if (inRectPad(x,y,x0,y0,toggleW,rowH,pad)) {
      h.type=KB_TOGGLE; h.x=x0; h.y=y0; h.w=toggleW; h.h=rowH; return h;
    }
    x0 += toggleW + gap;

    int spaceW = KB_W - (toggleW + gap + clrW + gap + okW);
    if (inRectPad(x,y,x0,y0,spaceW,rowH,pad)) {
      h.type=KB_SPACE; h.x=x0; h.y=y0; h.w=spaceW; h.h=rowH; return h;
    }

    int clrX = KB_X + KB_W - (clrW + gap + okW);
    if (inRectPad(x,y,clrX,y0,clrW,rowH,pad)) {
      h.type=KB_CLR; h.x=clrX; h.y=y0; h.w=clrW; h.h=rowH; return h;
    }

    int okX = clrX + clrW + gap;
    if (inRectPad(x,y,okX,y0,okW,rowH,pad)) {
      h.type=KB_OK; h.x=okX; h.y=y0; h.w=okW; h.h=rowH; return h;
    }
  }

  return h;
}

// ============================================================
// Password editing
// ============================================================
static void addChar(char c) {
  if (passInput.length() >= 64) return;
  passInput += c;
  redrawPassFieldOnly();
}
static void backspace() {
  if (passInput.length() == 0) return;
  passInput.remove(passInput.length()-1);
  redrawPassFieldOnly();
}
static void clearPass() {
  {
  String ss, pw;
  if (nvsLoadWifi(ss, pw) && ss == selectedSSID) passInput = pw;
  else passInput = "";
}

  redrawPassFieldOnly();
}

static bool isAuthOpen(uint8_t a) {
#ifdef WIFI_AUTH_OPEN
  return a == (uint8_t)WIFI_AUTH_OPEN;
#else
  return a == 0;
#endif
}

static bool selectedIsOpenNetwork() {
  if (selected < 0 || selected >= countNet) return false;
  return isAuthOpen(authList[selected]);
}

static bool doConnect() {
  if (connecting || connectRequested) return false;
  if (selectedSSID.length() == 0) { drawStatus("No SSID selected"); return false; }
  if (!selectedIsOpenNetwork() && passInput.length() == 0) {
    drawPassError("Password required");
    return false;
  }
  connectSSID = selectedSSID;
  connectPASS = passInput;
  connectRequested = true;
  return true;
}

static bool handleKeyboardTouch(bool pressed, bool lastPressed, int x, int y) {
  if (mode != WIFI_MODE_CONNECT) return false;

  if (pressed) {
    KbHit now = kbHitTest(x,y);

    if (!kbHasActive) {
      kbActive = now;
      kbHasActive = (now.type != KB_NONE);
      if (kbHasActive) drawKbHit(kbActive, true);
    } else {
      if (!sameHit(now, kbActive)) {
        drawKbHit(kbActive, false);
        kbActive = now;
        kbHasActive = (now.type != KB_NONE);
        if (kbHasActive) drawKbHit(kbActive, true);
      }
    }
    return (now.type != KB_NONE) || kbHasActive;
  }

  if (!pressed && lastPressed && kbHasActive) {
    drawKbHit(kbActive, false);

    switch (kbActive.type) {
      case KB_CHAR:   addChar(kbActive.ch); break;
      case KB_DEL:    backspace(); break;
      case KB_TOGGLE: kbNumbers = !kbNumbers; drawKeyboard(); break;
      case KB_SPACE:  addChar(' '); break;
      case KB_CLR:    clearPass(); break;
      case KB_OK:
        if (doConnect()) {
          mode = WIFI_MODE_LIST;
          drawWindowFrame("Wireless Networks");
          drawListBox();
          drawButton(BTN_REFRESH_X, BTN_Y, BTN_W, BTN_H, "Refresh");
          drawButton(BTN_CONNECT_X, BTN_Y, BTN_W, BTN_H, "Connect");
          drawButton(BTN_BACK_X,    BTN_Y, BTN_W, BTN_H, "Back");
          drawStatus("Connecting...");
          drawList();
        }
        break;
      default: break;
    }

    kbHasActive = false;
    kbActive = KbHit();
    return true;
  }

  return false;
}

static void drawConnectScreen() {
  mode = WIFI_MODE_CONNECT;
  kbNumbers = false;
  passVisible = false;

  kbHasActive = false;
  kbActive = KbHit();

  passInput = "";

  drawWindowFrame("Connect");

  tft->setTextColor(XP_BLACK, XP_BG);
  tft->drawString("Network:", CONTENT_X, SSID_LABEL_Y, 2);

  tft->fillRect(CONTENT_X, SSID_BOX_Y, CONTENT_W, SSID_BOX_H, XP_WHITE);
  tft->drawRect(CONTENT_X, SSID_BOX_Y, CONTENT_W, SSID_BOX_H, XP_BORDER);

  tft->setTextColor(XP_BLACK, XP_WHITE);
  String s = selectedSSID;
  if (s.length() > 26) s = s.substring(0,26) + "...";
  tft->drawString(s, CONTENT_X + 6, SSID_BOX_Y + 3, 2);

  drawPasswordBox();
  drawKeyboard();

  drawButton(BTN_REFRESH_X, BTN_Y, BTN_W, BTN_H, "Forget");
  drawButton(BTN_CONNECT_X, BTN_Y, BTN_W, BTN_H, "Cancel");
  drawButton(BTN_BACK_X,    BTN_Y, BTN_W, BTN_H, "Back");
}

void wifi_app_init(TFT_eSPI* display) {
  tft = display;

  String ss, pw;
  if (nvsLoadWifi(ss, pw)) savedSSID = ss;
  else savedSSID = "";
}

bool wifi_app_autoconnect(uint32_t timeoutMs) {
  String ss, pw;
  if (!nvsLoadWifi(ss, pw)) return false;

  savedSSID = ss;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ss.c_str(), pw.c_str());

  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(50);
  }
  return (WiFi.status() == WL_CONNECTED);
}

void wifi_app_open() {
  if (!tft) return;
  opened = true;
  mode = WIFI_MODE_LIST;

  scanAbort = false;
  scanning = false;
  scanRetry = 0;
  scanRetryPending = false;

  {
    String ss, pw;
    if (nvsLoadWifi(ss, pw)) savedSSID = ss;
    else savedSSID = "";
  }

  drawWindowFrame("Wireless Networks");
  drawListBox();

  drawButton(BTN_REFRESH_X, BTN_Y, BTN_W, BTN_H, "Refresh");
  drawButton(BTN_CONNECT_X, BTN_Y, BTN_W, BTN_H, "Connect");
  drawButton(BTN_BACK_X,    BTN_Y, BTN_W, BTN_H, "Back");

  if (WiFi.status() == WL_CONNECTED) {
    drawStatus(String("Connected   ") + WiFi.SSID());
  } else if (savedSSID.length() > 0) {
    drawStatus(String("Not connected (saved: ") + savedSSID + ")");
  } else {
    drawStatus("Not connected");
  }

  drawList();
  startScanAsync();
}

void wifi_app_tick() {
  if (opened) {
    uint32_t now = millis();
    if (now - lastStatusTick > 900) {
      system_ui_tick(statusX, statusY, XP_BLUE2);
      lastStatusTick = now;
    }
  }
  if (connectRequested && !connecting) {
    connectRequested = false;
    stopScanNow();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(false);
    delay(150);
    if (connectPASS.length() == 0) WiFi.begin(connectSSID.c_str());
    else WiFi.begin(connectSSID.c_str(), connectPASS.c_str());
    connectStartMs = millis();
    connecting = true;
  }

  if (!connecting && !connectRequested) {
    pollScanComplete();
  }

  if (!scanning && scanRetryPending) {
    if (connecting) {
      scanRetryAtMs = millis() + 500;
    } else if (millis() >= scanRetryAtMs) {
      scanRetryPending = false;
      startScanAsync();
    }
  }

  if (!connecting) return;

  wl_status_t st = WiFi.status();

  if (st == WL_CONNECTED) {
    connecting = false;

 nvsSaveWifi(connectSSID, connectPASS);
savedSSID = connectSSID;

    if (opened) drawStatus("Connected. Saved");
  }
  else if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
    connecting = false;
    WiFi.disconnect(false);
    if (opened) drawStatus("Failed ❌");
  }
  else if (millis() - connectStartMs > 12000) {
    connecting = false;
    WiFi.disconnect(false);
    if (opened) drawStatus("Wrong password ❌");
  }
}

// ============================================================
// Touch handler
// ============================================================
bool wifi_app_handleTouch(bool pressed, bool lastPressed, int x, int y) {
  // ✅ CRITICAL: if not open, DO NOT consume touches
  if (!opened) return false;

  if (mode == WIFI_MODE_CONNECT) {
    if (handleKeyboardTouch(pressed, lastPressed, x, y)) return true;
  }

  if (!(pressed && !lastPressed)) return true;

  int cx = WIN_X + WIN_W - PAD - CLOSE_W;
  int cy = WIN_Y + 1;

  if (inRect(x,y,cx,cy,CLOSE_W,CLOSE_H)) {
    scanAbort = true;
    if (scanning) stopScanNow();
    opened = false;
    return false;
  }

  if (inRect(x,y,BTN_BACK_X,BTN_Y,BTN_W,BTN_H)) {
    scanAbort = true;
    if (scanning) stopScanNow();
    opened = false;
    return false;
  }

  // ===================== LIST MODE =====================
  if (mode == WIFI_MODE_LIST) {
    if (inRect(x,y,BTN_REFRESH_X,BTN_Y,BTN_W,BTN_H)) {
      drawButton(BTN_REFRESH_X, BTN_Y, BTN_W, BTN_H, "Refresh", true);
      delay(60);
      drawButton(BTN_REFRESH_X, BTN_Y, BTN_W, BTN_H, "Refresh", false);
      startScanAsync();
      return true;
    }

    if (inRect(x,y,BTN_CONNECT_X,BTN_Y,BTN_W,BTN_H)) {
      if (selectedSSID.length() == 0) {
        drawStatus("Select a network first");
        return true;
      }
      drawConnectScreen();
      return true;
    }

    if (inRect(x,y,NETLIST_X,NETLIST_Y,NETLIST_W,NETLIST_H)) {
      const int rowH = 20;
      int relY = y - (NETLIST_Y + 2);
      int row = relY / rowH;
      int idx = scroll + row;

      if (idx >= 0 && idx < countNet) {
        selected = idx;
        selectedSSID = ssidList[selected];
        drawList();
        drawStatus(String("Selected: ") + selectedSSID);
      }
      return true;
    }

    return true;
  }

  // ===================== CONNECT MODE =====================
  if (mode == WIFI_MODE_CONNECT) {
    if (inRect(x,y,BTN_REFRESH_X,BTN_Y,BTN_W,BTN_H)) {
      nvsClearWifi();
      savedSSID = "";
      drawStatus("Saved WiFi cleared");
      return true;
    }

    if (inRect(x,y,BTN_CONNECT_X,BTN_Y,BTN_W,BTN_H)) {
      mode = WIFI_MODE_LIST;

      drawWindowFrame("Wireless Networks");
      drawListBox();

      drawButton(BTN_REFRESH_X, BTN_Y, BTN_W, BTN_H, "Refresh");
      drawButton(BTN_CONNECT_X, BTN_Y, BTN_W, BTN_H, "Connect");
      drawButton(BTN_BACK_X,    BTN_Y, BTN_W, BTN_H, "Back");

      if (WiFi.status() == WL_CONNECTED) {
        drawStatus(String("Connected ✅  ") + WiFi.SSID());
      } else if (savedSSID.length() > 0) {
        drawStatus(String("Not connected (saved: ") + savedSSID + ")");
      } else {
        drawStatus("Not connected");
      }

      drawList();
      startScanAsync();
      return true;
    }

    if (inRect(x,y, SEE_X, SEE_Y, SEE_W, SEE_H)) {
      passVisible = !passVisible;
      drawButton(SEE_X, SEE_Y, SEE_W, SEE_H, passVisible ? "Hide" : "See", true);
      delay(40);
      drawButton(SEE_X, SEE_Y, SEE_W, SEE_H, passVisible ? "Hide" : "See", false);
      redrawPassFieldOnly();
      return true;
    }

    return true;
  }

  return true;
}
