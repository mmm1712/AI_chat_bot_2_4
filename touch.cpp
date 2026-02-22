#include "touch.h"
#include <bb_captouch.h>

#define TOUCH_SDA 33
#define TOUCH_SCL 32
#define TOUCH_INT 21
#define TOUCH_RST 25

static BBCapTouch touch;
static TOUCHINFO ti;

void touch_init() {

  touch.init(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT, 400000, &Wire);
  touch.setOrientation(1, TOUCH_SCREEN_W, TOUCH_SCREEN_H);
}

bool touch_is_pressed() {
  return (touch.getSamples(&ti) > 0) && (ti.count > 0);
}

bool touch_get(int &x, int &y) {
  if (touch.getSamples(&ti) <= 0 || ti.count <= 0) return false;

  x = ti.y[0];
  y = (TOUCH_SCREEN_H - 1) - ti.x[0];

  x = constrain(x, 0, TOUCH_SCREEN_W - 1);
  y = constrain(y, 0, TOUCH_SCREEN_H - 1);

  return true;
}
