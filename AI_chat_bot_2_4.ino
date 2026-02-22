#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>

#include "touch.h"
#include "keyboard.h"

#include "desktop.h"
#include "chat_app.h"
#include "paint.h"
#include "ai_client.h"
#include "wifi_app.h"
#include "internet_app.h"
#include "notes_app.h"
#include "trash_app.h"
#include "settings_app.h"
#include "settings_store.h"
#include "system_ui.h"
#include "trash_state.h"

#include "welcome.h"

TFT_eSPI tft;
WiFiUDP mouseUdp;
static bool mouseUdpStarted = false;

enum AppState { APP_DESKTOP, APP_CHAT, APP_PAINT, APP_WIFI, APP_INTERNET, APP_NOTES, APP_TRASH, APP_SETTINGS };
static AppState app = APP_DESKTOP;

static bool autoConnectStarted = false;
static uint32_t autoConnectStartMs = 0;

// Mouse bridge (UDP)
static const uint16_t MOUSE_UDP_PORT = 4210;
static bool mouseActive = false;
static bool mousePressed = false;
static int  mouseX = 0;
static int  mouseY = 0;
static uint32_t mouseActiveUntil = 0;
static int  mouseWheel = 0;
static bool mouseDebug = false;
static int lastDrawX = -1;
static int lastDrawY = -1;

static void cursor_reset() {
  cursor_restore();
  lastDrawX = -1;
  lastDrawY = -1;
}

static void mouse_cursor_update() {
  // Debug indicator: small dot in top-left when UDP mouse is active
  if (mouseDebug) {
    tft.fillRect(2, 2, 4, 4, TFT_WHITE);
  }
  if (mouseActive && millis() < mouseActiveUntil) {
    if (mouseX != lastDrawX || mouseY != lastDrawY) {
      cursor_restore();
      cursor_draw(mouseX, mouseY);
      lastDrawX = mouseX;
      lastDrawY = mouseY;
    }
  } else {
    cursor_restore();
    lastDrawX = -1;
    lastDrawY = -1;
  }
}

// Cursor buffer (7x7)
static int cursorX = -1;
static int cursorY = -1;
static const int CUR_W = 9;
static const int CUR_H = 13;
static uint16_t cursorBuf[CUR_W * CUR_H];

static void cursor_restore() {
  if (cursorX < 0 || cursorY < 0) return;
  int idx = 0;
  for (int yy = 0; yy < CUR_H; yy++) {
    for (int xx = 0; xx < CUR_W; xx++) {
      int px = cursorX + xx;
      int py = cursorY + yy;
      if (px >= 0 && px < 320 && py >= 0 && py < 240) {
        tft.drawPixel(px, py, cursorBuf[idx]);
      }
      idx++;
    }
  }
  cursorX = -1;
  cursorY = -1;
}

static void cursor_draw(int x, int y) {
  if (x < 0 || y < 0 || x >= 320 || y >= 240) return;
  // Hotspot at tip (x,y). Cursor box starts at (x,y)
  int ox = x;
  int oy = y;
  if (ox + CUR_W > 320) ox = 320 - CUR_W;
  if (oy + CUR_H > 240) oy = 240 - CUR_H;

  // Save background
  int idx = 0;
  for (int yy = 0; yy < CUR_H; yy++) {
    for (int xx = 0; xx < CUR_W; xx++) {
      int px = ox + xx;
      int py = oy + yy;
      uint16_t col = 0;
      if (px >= 0 && px < 320 && py >= 0 && py < 240) {
        col = tft.readPixel(px, py);
      }
      cursorBuf[idx++] = col;
    }
  }
  cursorX = ox;
  cursorY = oy;

  // Draw arrow cursor (white fill + black outline)
  static const uint8_t arrow[CUR_H][CUR_W] = {
    {1,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0},
    {1,1,1,0,0,0,0,0,0},
    {1,1,1,1,0,0,0,0,0},
    {1,1,1,1,1,0,0,0,0},
    {1,1,1,1,1,1,0,0,0},
    {1,1,1,1,1,1,1,0,0},
    {1,1,1,1,1,0,0,0,0},
    {1,1,1,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0},
    {1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0},
  };

  for (int yy = 0; yy < CUR_H; yy++) {
    for (int xx = 0; xx < CUR_W; xx++) {
      if (!arrow[yy][xx]) continue;
      bool edge = false;
      if (yy == 0 || xx == 0 || yy == CUR_H - 1 || xx == CUR_W - 1) edge = true;
      if (!edge) {
        if (!arrow[yy-1][xx] || !arrow[yy+1][xx] || !arrow[yy][xx-1] || !arrow[yy][xx+1]) edge = true;
      }
      tft.drawPixel(ox + xx, oy + yy, edge ? TFT_BLACK : TFT_WHITE);
    }
  }
}

static void mouse_udp_poll() {
  int packetSize = mouseUdp.parsePacket();
  if (packetSize <= 0) return;

  char buf[64];
  int len = mouseUdp.read(buf, sizeof(buf) - 1);
  if (len <= 0) return;
  buf[len] = 0;

  int x = 0, y = 0, p = 0, w = 0;
  int n = sscanf(buf, "%d,%d,%d,%d", &x, &y, &p, &w);
  if (n >= 3) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > 319) x = 319;
    if (y > 239) y = 239;
    mouseX = x;
    mouseY = y;
    mousePressed = (p != 0);
    mouseWheel = (n == 4) ? w : 0;
    mouseActive = true;
    mouseActiveUntil = millis() + 1000;
    mouseDebug = true;
    // no serial spam
  }
}

static inline bool inRect(int x,int y,int rx,int ry,int rw,int rh){
  return x>=rx && x<rx+rw && y>=ry && y<ry+rh;
}

static void show_welcome(uint32_t ms = 800) {
  tft.setSwapBytes(true);
  tft.pushImage(0, 0, WELCOME_WIDTH, WELCOME_HEIGHT, (uint16_t*)welcome_map);
  delay(ms);
}

static void startAutoConnectNonBlocking() {
  Preferences p;
  p.begin("wifi", true);
  String ssid = p.getString("ssid", "");
  String pass = p.getString("pass", "");
  p.end();

  ssid.trim();
  pass.trim();
  if (ssid.length() == 0) return;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (pass.length() == 0) WiFi.begin(ssid.c_str());
  else WiFi.begin(ssid.c_str(), pass.c_str());

  autoConnectStarted = true;
  autoConnectStartMs = millis();
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);

  show_welcome(800);

  settings_store_init();
  settings_apply_brightness();
  trash_state_init();

  touch_init();
  keyboard_init(&tft);

  paint_init(&tft);
  wifi_app_init(&tft);
  internet_app_init(&tft);
  notes_app_init(&tft);
  trash_app_init(&tft);
  settings_app_init(&tft);
  system_ui_init(&tft);
  system_ui_time_begin();

  desktop_init(&tft);
  chat_init(&tft);

  desktop_draw();

  if (settings_get_autoconnect()) {
    startAutoConnectNonBlocking();
  }
}

void loop() {
  static bool lastPressed = false;
  static int lastX = 0;
  static int lastY = 0;

  wifi_app_tick();
  internet_app_tick();
  notes_app_tick();
  trash_app_tick();
  settings_app_tick();
  if (app == APP_DESKTOP) desktop_tick();
  if (app == APP_CHAT) chat_tick();
  if (app == APP_PAINT) paint_tick();
  ai_pollSerial();

  if (autoConnectStarted) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      autoConnectStarted = false;
      Serial.print("WiFi OK, IP: ");
      Serial.println(WiFi.localIP());
    } else if (millis() - autoConnectStartMs > 8000) {
      autoConnectStarted = false;
    }
  }

  if (!mouseUdpStarted && WiFi.status() == WL_CONNECTED) {
    mouseUdp.begin(MOUSE_UDP_PORT);
    mouseUdpStarted = true;
    Serial.println("UDP mouse listener started");
  }

  mouse_udp_poll();

  int x = lastX, y = lastY;
  bool pressed = false;

  if (mouseActive && millis() < mouseActiveUntil) {
    x = mouseX;
    y = mouseY;
    pressed = mousePressed;
  } else {
    mouseActive = false;
    mouseDebug = false;
    pressed = touch_is_pressed();
    if (pressed) {
      if (!touch_get(x, y)) {
        lastPressed = pressed;
        return;
      }
      lastX = x;
      lastY = y;
    }
  }

  if (mouseActive && mouseWheel != 0) {
    if (app == APP_NOTES) {
      notes_app_scroll_steps(mouseWheel);
    }
    mouseWheel = 0;
  }

  if (!pressed && lastPressed) {
    keyboard_release();
    paint_release();
    if (app == APP_CHAT) chat_release();
  }

  if (app == APP_WIFI) {
    bool keepOpen = wifi_app_handleTouch(pressed, lastPressed, x, y);
    if (!keepOpen) {
      app = APP_DESKTOP;
      cursor_reset();
      desktop_draw();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }
    lastPressed = pressed;
    mouse_cursor_update();
    return;
  }

  if (app == APP_INTERNET) {
    bool keepOpen = internet_app_handleTouch(pressed, lastPressed, x, y);
    if (!keepOpen) {
      app = APP_DESKTOP;
      cursor_reset();
      desktop_draw();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }
    lastPressed = pressed;
    mouse_cursor_update();
    return;
  }

  if (app == APP_NOTES) {
    bool keepOpen = notes_app_handleTouch(pressed, lastPressed, x, y);
    if (!keepOpen) {
      app = APP_DESKTOP;
      cursor_reset();
      desktop_draw();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }
    lastPressed = pressed;
    mouse_cursor_update();
    return;
  }

  if (app == APP_TRASH) {
    bool keepOpen = trash_app_handleTouch(pressed, lastPressed, x, y);
    if (!keepOpen) {
      app = APP_DESKTOP;
      cursor_reset();
      desktop_draw();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }
    lastPressed = pressed;
    mouse_cursor_update();
    return;
  }

  if (app == APP_SETTINGS) {
    bool keepOpen = settings_app_handleTouch(pressed, lastPressed, x, y);
    if (!keepOpen) {
      app = APP_DESKTOP;
      cursor_reset();
      desktop_draw();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }
    lastPressed = pressed;
    mouse_cursor_update();
    return;
  }

  if (app == APP_DESKTOP) {
    desktop_set_mouse_mode(mouseActive && millis() < mouseActiveUntil);
    DesktopAction a = desktop_handleTouch(pressed, lastPressed, x, y);

    if (a == DESKTOP_OPEN_CHAT) {
      app = APP_CHAT;
      keyboard_clear();
      cursor_reset();
      chat_draw();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }
    else if (a == DESKTOP_OPEN_PAINT) {
      app = APP_PAINT;
      cursor_reset();
      paint_draw();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }
    else if (a == DESKTOP_OPEN_WIFI) {
      app = APP_WIFI;
      cursor_reset();
      wifi_app_open();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }
    else if (a == DESKTOP_OPEN_INTERNET) {
      app = APP_INTERNET;
      cursor_reset();
      internet_app_open();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }
    else if (a == DESKTOP_OPEN_NOTES) {
      app = APP_NOTES;
      cursor_reset();
      notes_app_open();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }
    else if (a == DESKTOP_OPEN_TRASH) {
      app = APP_TRASH;
      cursor_reset();
      trash_app_open();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }
    else if (a == DESKTOP_OPEN_SETTINGS) {
      app = APP_SETTINGS;
      cursor_reset();
      settings_app_open();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }

    lastPressed = pressed;
    mouse_cursor_update();
    return;
  }

  if (app == APP_CHAT) {
    if (pressed && !lastPressed && inRect(x, y, 260, 4, 52, 17)) {
      app = APP_DESKTOP;
      cursor_reset();
      desktop_draw();
      lastPressed = true;
      mouse_cursor_update();
      return;
    }
    chat_handleTouch(pressed, lastPressed, x, y);
    lastPressed = pressed;
    mouse_cursor_update();
    return;
  }

  if (app == APP_PAINT) {
    if (pressed && !lastPressed) {
      if (x >= 320 - 16 - 6 && x < 320 - 6 && y >= 2 && y < 16) {
        app = APP_DESKTOP;
        cursor_reset();
        desktop_draw();
        lastPressed = true;
        mouse_cursor_update();
        return;
      }
    }

    if (pressed) {
      bool keepOpen = paint_handleTouch(x, y);
      if (!keepOpen) {
        app = APP_DESKTOP;
        cursor_reset();
        desktop_draw();
        lastPressed = true;
        mouse_cursor_update();
        return;
      }
    }

    lastPressed = pressed;
    mouse_cursor_update();
    return;
  }

  lastPressed = pressed;
  mouse_cursor_update();
}
