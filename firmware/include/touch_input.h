#pragma once

#include <stdint.h>

void touch_input_begin(int screenWidth, int screenHeight);
bool touch_input_read(int16_t &x, int16_t &y, bool &pressed);
