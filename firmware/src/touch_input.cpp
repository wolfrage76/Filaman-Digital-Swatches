#include "touch_input.h"

#include <Arduino.h>
#include <TAMC_GT911.h>
#include <Wire.h>

#include "soc/soc.h"
#include "soc/usb_serial_jtag_reg.h"

namespace {

constexpr int TOUCH_SDA = 19;
constexpr int TOUCH_SCL = 20;
constexpr int TOUCH_MAP_X1 = 800;
constexpr int TOUCH_MAP_X2 = 0;
constexpr int TOUCH_MAP_Y1 = 480;
constexpr int TOUCH_MAP_Y2 = 0;
// CrowPanel v3.0 straps PCA9557 A2/A1/A0 to ground.
constexpr uint8_t PCA9557_ADDR = 0x18;

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

bool i2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// GPIO19/20 are also the USB Serial/JTAG pads. Display init can restore the
// pad pulls, so clear them again immediately before the touch bus starts.
void releaseUsbJtagPads() {
  SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PAD_PULL_OVERRIDE);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLUP);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLDOWN);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DM_PULLUP);
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DM_PULLDOWN);
}

uint8_t findGt911() {
  if (i2cProbe(GT911_ADDR1)) {
    return GT911_ADDR1;
  }
  if (i2cProbe(GT911_ADDR2)) {
    return GT911_ADDR2;
  }
  return 0;
}

// v3 holds GT911 in reset until IO0 rises with IO1 still low, which selects
// address 0x5D. v2 has no expander, so this never runs when the chip already
// answers.
void releaseGt911(uint8_t pca) {
  i2cWriteReg(pca, 0x03, 0xFF);  // all inputs
  i2cWriteReg(pca, 0x01, 0xFF);  // output latch high
  i2cWriteReg(pca, 0x02, 0x00);  // no polarity inversion
  i2cWriteReg(pca, 0x03, 0x00);  // all outputs
  i2cWriteReg(pca, 0x01, 0xFC);  // reset and INT low
  delay(20);
  i2cWriteReg(pca, 0x01, 0xFD);  // release reset, keep INT low
  delay(100);
  i2cWriteReg(pca, 0x03, 0x02);  // INT back to an input
  delay(50);
}

}  // namespace

void touch_input_begin(int width, int height) {
  screenWidth = width > 0 ? width : 800;
  screenHeight = height > 0 ? height : 480;

  releaseUsbJtagPads();
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(100000);

  uint8_t addr = findGt911();
  if (addr == 0 && i2cProbe(PCA9557_ADDR)) {
    releaseGt911(PCA9557_ADDR);
    addr = findGt911();
  }

  if (addr == 0) {
    touchReady = false;
    log_e("GT911 not found");
    return;
  }

  touch.begin(addr);
  touch.setRotation(ROTATION_NORMAL);
  touchReady = true;
  // 400 kHz drops touches on some v3 panels. 300 kHz still suits a v2 GT911.
  Wire.setClock(300000);
  log_e("GT911 touch at 0x%02X", addr);
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
