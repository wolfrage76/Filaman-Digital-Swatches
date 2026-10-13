#pragma once

#include <stdint.h>

void touch_input_begin(int screenWidth, int screenHeight);
bool touch_input_read(int16_t &x, int16_t &y, bool &pressed);
// Suppress new touch presses for `ms` milliseconds and until the finger is lifted.
void touch_input_cooldown(unsigned long ms = 280);
