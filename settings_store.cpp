#include "settings_store.h"
#include <Preferences.h>
#include <Arduino.h>

static const char* NVS_NS = "settings";
static const char* KEY_BRIGHT = "bright";
static const char* KEY_AUTO   = "autoc";

static bool inited = false;
static uint8_t gBright = 220;
static bool gAuto = true;

static const int BL_PIN = 27;
static const int BL_CH  = 0;
static const int BL_FREQ = 5000;
static const int BL_RES  = 8;
static bool blInited = false;

static void loadOnce() {
  if (inited) return;
  Preferences p;
  p.begin(NVS_NS, true);
  gBright = (uint8_t)p.getUChar(KEY_BRIGHT, gBright);
  gAuto   = p.getBool(KEY_AUTO, gAuto);
  p.end();
  inited = true;
}

void settings_store_init() {
  loadOnce();
}

uint8_t settings_get_brightness() { loadOnce(); return gBright; }
bool settings_get_autoconnect() { loadOnce(); return gAuto; }

static void saveAll() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.putUChar(KEY_BRIGHT, gBright);
  p.putBool(KEY_AUTO, gAuto);
  p.end();
}

void settings_set_brightness(uint8_t val) {
  loadOnce();
  gBright = val;
  saveAll();
  settings_apply_brightness();
}

void settings_set_autoconnect(bool on) {
  loadOnce();
  gAuto = on;
  saveAll();
}

void settings_apply_brightness() {
  loadOnce();
  if (!blInited) {
    ledcSetup(BL_CH, BL_FREQ, BL_RES);
    ledcAttachPin(BL_PIN, BL_CH);
    blInited = true;
  }
  ledcWrite(BL_CH, gBright);
}
