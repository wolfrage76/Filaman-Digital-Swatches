#pragma once

#include <stdint.h>

// ============================================================
//  WHAT A BOARD'S PANEL HAS TO DO
//
//  Shared LVGL / backlight / screenshot code talks to the glass through these
//  functions. Each board implements them in boards/<board>/panel.cpp.
//
//  Coordinates are always SCREEN_WIDTH x SCREEN_HEIGHT (800 x 480).
// ============================================================

struct PanelTouchPoint {
  int16_t x;
  int16_t y;
};

// Bring up bus, panel, touch, and leave the screen black with backlight ready.
// False when the panel could not start.
bool panelBegin();

// RGB565 framebuffer in PSRAM (or nullptr if not running). Used by screenshots
// and by the shared LVGL flush blit.
const uint16_t *panelFramebuffer();

// Copy one finished strip into the framebuffer (cache-coherent as needed).
void panelBlitStrip(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px);

// Current fingers on the glass, up to max_points. Returns how many.
uint8_t panelTouchRead(PanelTouchPoint *points, uint8_t max_points);

// Suppress new presses for `ms` and until the finger lifts (menu transitions).
void panelTouchCooldown(unsigned long ms = 280);

// Backlight level 0..255. On PWM boards this sets duty. On on/off-only boards
// this stores a soft-dim factor applied in panelBlitStrip.
void panelSetBrightness(uint8_t brightness);

void panelBacklightOff();
void panelBacklightOn(uint8_t brightness);
