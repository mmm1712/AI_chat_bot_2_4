#pragma once
#include <stdint.h>

void settings_store_init();

uint8_t settings_get_brightness();
bool settings_get_autoconnect();

void settings_set_brightness(uint8_t val);
void settings_set_autoconnect(bool on);

void settings_apply_brightness();
