#include "touch_input.h"

#include <Arduino.h>
#include <TAMC_GT911.h>
#include <Wire.h>

namespace {

constexpr int TOUCH_SDA = 19;
constexpr int TOUCH_SCL = 20;
constexpr int TOUCH_MAP_X1 = 800;
constexpr int TOUCH_MAP_X2 = 0;
constexpr int TOUCH_MAP_Y1 = 480;
constexpr int TOUCH_MAP_Y2 = 0;

TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, -1, -1, TOUCH_MAP_X1, TOUCH_MAP_Y1);

int screenWidth = 800;
int screenHeight = 480;
bool touchReady = false;

int mapAxis(int value, int inMin, int inMax, int outMax) {
  if (inMin == inMax) {
    return 0;
  }
  const long mapped = map(value, inMin, inMax, 0, outMax);
  if (mapped < 0) {
    return 0;
  }
  if (mapped > outMax) {
    return outMax;
  }
  return static_cast<int>(mapped);
}

}  // namespace

void touch_input_begin(int width, int height) {
  screenWidth = width > 0 ? width : 800;
  screenHeight = height > 0 ? height : 480;

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);
  touch.begin();
  touch.setRotation(ROTATION_NORMAL);
  touchReady = true;
  Serial.println("GT911 touch init (TAMC_GT911)");
}

bool touch_input_read(int16_t &x, int16_t &y, bool &pressed) {
  if (!touchReady) {
    pressed = false;
    return false;
  }

  touch.read();
  if (!touch.isTouched) {
    pressed = false;
    return true;
  }

  x = static_cast<int16_t>(mapAxis(touch.points[0].x, TOUCH_MAP_X1, TOUCH_MAP_X2, screenWidth - 1));
  y = static_cast<int16_t>(mapAxis(touch.points[0].y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, screenHeight - 1));
  pressed = true;
  return true;
}
