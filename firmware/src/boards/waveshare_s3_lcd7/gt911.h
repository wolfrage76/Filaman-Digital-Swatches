#pragma once

#include <Wire.h>
#include <stdint.h>

namespace gt911 {

struct Point {
  uint16_t x;
  uint16_t y;
};

bool begin(TwoWire &wire);
uint8_t read(Point *points, uint8_t max_points);
uint16_t maxX();
uint16_t maxY();
bool intIdleHigh();
void clearStatus();

}
