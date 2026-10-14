#include "gt911.h"

#include <Arduino.h>

namespace gt911 {

// Which of the two the chip takes is decided by its INT line during reset.
static constexpr uint8_t ADDR_A = 0x5D;
static constexpr uint8_t ADDR_B = 0x14;

static constexpr uint16_t REG_X_MAX       = 0x8048;   // x low, x high, y low, y high
static constexpr uint16_t REG_INT_TRIGGER = 0x804D;   // bits 0..1
static constexpr uint16_t REG_STATUS      = 0x814E;
static constexpr uint16_t REG_POINTS      = 0x814F;

static constexpr uint8_t STATUS_FRAME_READY = 0x80;
static constexpr uint8_t STATUS_COUNT_MASK  = 0x0F;
static constexpr uint8_t POINT_BYTES        = 8;      // id, x, y, size, one spare
static constexpr uint8_t CHIP_POINTS        = 5;

// INT trigger modes 1 (falling edge) and 2 (low level) rest high.
static constexpr uint8_t INT_TRIGGER_FALLING = 1;
static constexpr uint8_t INT_TRIGGER_LOW     = 2;

// A finger that lifts ends in a frame with no points. Should that frame never
// arrive, the touch must not stay down for good.
static constexpr uint32_t HELD_FRAME_MAX_MS = 150;

static TwoWire* s_wire = nullptr;
static uint8_t  s_addr = 0;
static uint16_t s_max_x = 0, s_max_y = 0;
static bool     s_int_idle_high = false;

static Point    s_last[CHIP_POINTS];
static uint8_t  s_last_count = 0;
static uint32_t s_last_ms    = 0;

static bool readReg(uint16_t reg, uint8_t* out, uint8_t len) {
  s_wire->beginTransmission(s_addr);
  s_wire->write((uint8_t)(reg >> 8));
  s_wire->write((uint8_t)(reg & 0xFF));
  if (s_wire->endTransmission(false) != 0) return false;
  if (s_wire->requestFrom(s_addr, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) out[i] = (uint8_t)s_wire->read();
  return true;
}

static bool writeReg(uint16_t reg, uint8_t value) {
  s_wire->beginTransmission(s_addr);
  s_wire->write((uint8_t)(reg >> 8));
  s_wire->write((uint8_t)(reg & 0xFF));
  s_wire->write(value);
  return s_wire->endTransmission() == 0;
}

static bool answers(uint8_t addr) {
  s_wire->beginTransmission(addr);
  return s_wire->endTransmission() == 0;
}

bool begin(TwoWire& wire) {
  s_wire = &wire;
  s_addr = 0;
  s_last_count = 0;
  if (answers(ADDR_A))      s_addr = ADDR_A;
  else if (answers(ADDR_B)) s_addr = ADDR_B;
  else return false;

  uint8_t res[4];
  if (readReg(REG_X_MAX, res, sizeof(res))) {
    s_max_x = (uint16_t)(res[0] | (res[1] << 8));
    s_max_y = (uint16_t)(res[2] | (res[3] << 8));
  }
  uint8_t trigger = 0;
  if (readReg(REG_INT_TRIGGER, &trigger, 1)) {
    trigger &= 0x03;
    s_int_idle_high = (trigger == INT_TRIGGER_FALLING || trigger == INT_TRIGGER_LOW);
  }
  clearStatus();
  return true;
}

uint8_t read(Point* points, uint8_t max_points) {
  if (!s_addr) return 0;
  uint8_t status = 0;
  const bool got = readReg(REG_STATUS, &status, 1);
  if (got && (status & STATUS_FRAME_READY)) {
    uint8_t n = status & STATUS_COUNT_MASK;
    if (n > CHIP_POINTS) n = 0;
    uint8_t raw[CHIP_POINTS * POINT_BYTES];
    if (n > 0 && !readReg(REG_POINTS, raw, (uint8_t)(n * POINT_BYTES))) n = 0;
    for (uint8_t i = 0; i < n; i++) {
      const uint8_t* p = raw + i * POINT_BYTES;
      s_last[i].x = (uint16_t)(p[1] | (p[2] << 8));
      s_last[i].y = (uint16_t)(p[3] | (p[4] << 8));
    }
    s_last_count = n;
    s_last_ms    = millis();
    writeReg(REG_STATUS, 0);
  } else if (s_last_count > 0 && millis() - s_last_ms > HELD_FRAME_MAX_MS) {
    s_last_count = 0;
  }
  const uint8_t n = (s_last_count < max_points) ? s_last_count : max_points;
  for (uint8_t i = 0; i < n; i++) points[i] = s_last[i];
  return n;
}

uint16_t maxX() { return s_max_x; }
uint16_t maxY() { return s_max_y; }
bool intIdleHigh() { return s_int_idle_high; }

void clearStatus() {
  if (s_addr) writeReg(REG_STATUS, 0);
}

}
