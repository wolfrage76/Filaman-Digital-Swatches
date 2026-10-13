#pragma once

#include <Arduino.h>

// CrowPanel backlight on GPIO 2. Dim and off are minutes from the last touch.
// A tap while dim or off restores full brightness and is not delivered to LVGL.

void backlight_begin();
void backlight_loop();
// False means this sample must not reach the UI (the wake tap, until release).
bool backlight_filter_touch(bool pressed);
