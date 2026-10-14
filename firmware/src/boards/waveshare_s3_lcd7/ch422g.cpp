#include "ch422g.h"

namespace ch422g {

static constexpr uint8_t REG_MODE = 0x24;
static constexpr uint8_t REG_OUTPUTS = 0x38;
static constexpr uint8_t MODE_IO_OUTPUT = 0x01;

static TwoWire *s_wire = nullptr;
static uint8_t s_levels = 0;

static bool writeReg(uint8_t reg, uint8_t value) {
  if (!s_wire) {
    return false;
  }
  s_wire->beginTransmission(reg);
  s_wire->write(value);
  return s_wire->endTransmission() == 0;
}

bool begin(TwoWire &wire, uint8_t initial_levels) {
  s_wire = &wire;
  s_levels = initial_levels;
  const bool levels_ok = writeReg(REG_OUTPUTS, s_levels);
  const bool mode_ok = writeReg(REG_MODE, MODE_IO_OUTPUT);
  return levels_ok && mode_ok;
}

bool write(uint8_t pin, bool level) {
  if (level) {
    s_levels |= static_cast<uint8_t>(1u << pin);
  } else {
    s_levels &= static_cast<uint8_t>(~(1u << pin));
  }
  return writeReg(REG_OUTPUTS, s_levels);
}

}
