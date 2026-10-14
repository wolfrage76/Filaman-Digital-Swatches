#include "touch_input.h"

#include "hardware/panel.h"

void touch_input_begin(int /*screenWidth*/, int /*screenHeight*/) {
  // Touch is started inside panelBegin().
}

void touch_input_cooldown(unsigned long ms) { panelTouchCooldown(ms); }

bool touch_input_read(int16_t &x, int16_t &y, bool &pressed) {
  PanelTouchPoint pt{};
  const uint8_t n = panelTouchRead(&pt, 1);
  if (n == 0) {
    pressed = false;
    return true;
  }
  x = pt.x;
  y = pt.y;
  pressed = true;
  return true;
}
