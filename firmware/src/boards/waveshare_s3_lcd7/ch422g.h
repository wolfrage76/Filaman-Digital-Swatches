#pragma once

#include <Wire.h>
#include <stdint.h>

// CH422G I/O expander, the eight push-pull pins EXIO0..7 as outputs.
namespace ch422g {

bool begin(TwoWire &wire, uint8_t initial_levels);
bool write(uint8_t pin, bool level);

}
