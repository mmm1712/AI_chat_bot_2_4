#include "trash_state.h"
#include <Preferences.h>

static const char* NVS_NS = "trash";
static const char* KEY_MASK = "mask";

static bool inited = false;
static uint8_t mask = 0;

static void loadOnce() {
  if (inited) return;
  Preferences p;
  p.begin(NVS_NS, true);
  mask = (uint8_t)p.getUChar(KEY_MASK, 0);
  p.end();
  inited = true;
}

static void saveMask() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.putUChar(KEY_MASK, mask);
  p.end();
}

void trash_state_init() {
  loadOnce();
}

bool trash_is_deleted(TrashIconId id) {
  loadOnce();
  return (mask & (1u << id)) != 0;
}

void trash_delete_icon(TrashIconId id) {
  loadOnce();
  mask |= (1u << id);
  saveMask();
}

void trash_restore_icon(TrashIconId id) {
  loadOnce();
  mask &= ~(1u << id);
  saveMask();
}

void trash_restore_all() {
  loadOnce();
  mask = 0;
  saveMask();
}

uint8_t trash_deleted_count() {
  loadOnce();
  uint8_t c = 0;
  for (uint8_t i = 0; i < ICON_COUNT; i++) {
    if (mask & (1u << i)) c++;
  }
  return c;
}
