#pragma once

#include <Arduino.h>

// Read-only access to the panel's RGB565 framebuffer. Callers must consume it
// synchronously from the main loop; LVGL rendering is paused while a web
// request handler is running.
const uint16_t *display_framebuffer_rgb565();
