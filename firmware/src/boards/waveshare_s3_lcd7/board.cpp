#include "boards/board.h"

#include <Arduino.h>
#include <Wire.h>

#include "ch422g.h"
#include "device_log.h"

// Panel and touch held in reset, backlight off, SD selected, USB on GPIO19/20.
static constexpr uint8_t EXIO_AT_BOOT = 0x00;
static constexpr uint32_t BOARD_I2C_HZ = 400000;

void boardEarlyInit() {
  Wire.begin(hw_pins::BOARD_SDA, hw_pins::BOARD_SCL, BOARD_I2C_HZ);
  if (ch422g::begin(Wire, EXIO_AT_BOOT)) {
    device_log("Board: CH422G expander ready");
  } else {
    device_log("Board: CH422G expander does not answer");
  }
}
