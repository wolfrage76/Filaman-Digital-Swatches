#pragma once

#include <Arduino.h>

// Dim and off are minutes from the last touch. A tap while dim or off restores
// full brightness and is not delivered to LVGL. Hardware PWM vs soft-dim is
// decided by BOARD_BACKLIGHT_PWM in the board panel driver.

void backlight_begin();
void backlight_loop();
void backlight_apply();
// False means this sample must not reach the UI (the wake tap, until release).
bool backlight_filter_touch(bool pressed);
