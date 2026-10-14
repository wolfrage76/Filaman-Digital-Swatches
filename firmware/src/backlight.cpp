#include "backlight.h"

#include <lvgl.h>

#include "app_config.h"
#include "boards/board.h"
#include "hardware/panel.h"

namespace {

enum class Level : uint8_t { Full, Dim, Off };

Level level = Level::Full;
unsigned long lastActivityMs = 0;
bool swallowing = false;
#if !BOARD_BACKLIGHT_PWM
uint8_t lastSoftDuty = 0xFF;
#endif

uint8_t dutyForPct(int pct) {
  if (pct <= 0) {
    return 0;
  }
  if (pct >= 100) {
    return 255;
  }
  return static_cast<uint8_t>((pct * 255 + 50) / 100);
}

void applyLevelDuty(Level lv) {
  if (lv == Level::Off) {
    panelBacklightOff();
    return;
  }
  const uint8_t duty = dutyForPct(lv == Level::Full ? config().displayFullBrightPct
                                                    : config().displayDimBrightPct);
  panelBacklightOn(duty);
  panelSetBrightness(duty);
#if !BOARD_BACKLIGHT_PWM
  // Soft-dim is applied only on LVGL flush. Force a full redraw so every
  // pixel picks up the new factor (otherwise stale strips stay bright).
  if (duty != lastSoftDuty) {
    lastSoftDuty = duty;
    if (lv_disp_get_default()) {
      lv_obj_invalidate(lv_scr_act());
    }
  }
#endif
}

void setLevel(Level next) {
  if (next == level) {
    return;
  }
  level = next;
  applyLevelDuty(level);
}

}  // namespace

void backlight_begin() {
  level = Level::Full;
  applyLevelDuty(level);
  lastActivityMs = millis();
}

void backlight_apply() {
  lastActivityMs = millis();
  level = Level::Full;
  applyLevelDuty(level);
}

void backlight_loop() {
  if (swallowing || level == Level::Off) {
    return;
  }
  const unsigned long idle = millis() - lastActivityMs;
  const unsigned long offMs = static_cast<unsigned long>(config().displayOffMin) * 60000UL;
  const unsigned long dimMs = static_cast<unsigned long>(config().displayDimMin) * 60000UL;
  if (idle >= offMs) {
    setLevel(Level::Off);
  } else if (idle >= dimMs) {
    setLevel(Level::Dim);
  } else {
    setLevel(Level::Full);
  }
}

bool backlight_filter_touch(bool pressed) {
  if (swallowing) {
    if (!pressed) {
      swallowing = false;
    }
    return false;
  }
  if (pressed && level != Level::Full) {
    setLevel(Level::Full);
    lastActivityMs = millis();
    swallowing = true;
    return false;
  }
  if (pressed) {
    lastActivityMs = millis();
  }
  return true;
}
