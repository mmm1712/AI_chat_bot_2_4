#pragma once
#include <stdint.h>
#include <TFT_eSPI.h>

// Init with display pointer
void wifi_app_init(TFT_eSPI* display);

// Open WiFi window (draws UI)
void wifi_app_open();

// Call frequently from loop()
void wifi_app_tick();

// Touch handler:
// returns true  -> still open (keep routing touch to wifi app)
// returns false -> closed (caller should return to desktop)
bool wifi_app_handleTouch(bool pressed, bool lastPressed, int x, int y);

// Optional blocking autoconnect (not recommended during boot)
bool wifi_app_autoconnect(uint32_t timeoutMs);

// Forget stored credentials
void wifi_app_forget_saved();

// IMPORTANT: so main .ino can decide who owns input
bool wifi_app_is_open();

// Backward-compatible alias if you accidentally used camelCase:
inline bool wifi_app_isOpen() { return wifi_app_is_open(); }
