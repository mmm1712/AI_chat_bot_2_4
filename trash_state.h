#pragma once
#include <stdint.h>

enum TrashIconId : uint8_t {
  ICON_AI = 0,
  ICON_PAINT,
  ICON_INTERNET,
  ICON_NOTES,
  ICON_WIFI,
  ICON_COUNT
};

void trash_state_init();
bool trash_is_deleted(TrashIconId id);
void trash_delete_icon(TrashIconId id);
void trash_restore_icon(TrashIconId id);
void trash_restore_all();
uint8_t trash_deleted_count();
