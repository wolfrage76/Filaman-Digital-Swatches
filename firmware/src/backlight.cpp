#include "backlight.h"

#include "app_config.h"

namespace {

constexpr int BACKLIGHT_PIN = 2;
constexpr uint8_t DUTY_FULL = 255;
constexpr uint8_t DUTY_DIM = 48;
constexpr uint32_t PWM_HZ = 5000;
constexpr uint8_t PWM_BITS = 8;

enum class Level : uint8_t { Full, Dim, Off };

Level level = Level::Full;
unsigned long lastActivityMs = 0;
bool swallowing = false;
bool pwmOk = false;

void writeDuty(uint8_t duty) {
  if (!pwmOk) {
    digitalWrite(BACKLIGHT_PIN, duty > 0 ? HIGH : LOW);
    return;
  }
  ledcWrite(BACKLIGHT_PIN, duty);
}

void setLevel(Level next) {
  if (next == level) {
    return;
  }
  level = next;
  if (next == Level::Full) {
    writeDuty(DUTY_FULL);
  } else if (next == Level::Dim) {
    writeDuty(DUTY_DIM);
  } else {
    writeDuty(0);
  }
}

}  // namespace

void backlight_begin() {
  pinMode(BACKLIGHT_PIN, OUTPUT);
  pwmOk = ledcAttach(BACKLIGHT_PIN, PWM_HZ, PWM_BITS);
  writeDuty(DUTY_FULL);
  level = Level::Full;
  lastActivityMs = millis();
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
