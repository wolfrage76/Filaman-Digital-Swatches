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

// Hold release for 45 ms so a single missed capacitive frame never splits one
// press into two clicks, then block re-presses for 240 ms to prevent accidental
// double-taps from clicking through to the next screen.
constexpr unsigned long RELEASE_HOLD_MS = 45;
constexpr unsigned long REPRESS_COOLDOWN_MS = 240;

TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, -1, -1, TOUCH_MAP_X1, TOUCH_MAP_Y1);

int screenWidth = 800;
int screenHeight = 480;
uint8_t gt911Addr = 0;
bool touchReady = false;

bool rawPressed = false;
bool reportedPressed = false;
bool waitForFullRelease = false;
int16_t lastX = 0;
int16_t lastY = 0;
unsigned long lastRawTouchMs = 0;
unsigned long blockPressUntilMs = 0;

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

// Read 16-bit GT911 register without clearing 0x814E when bufferStatus == 0.
bool gt911ReadReg(uint8_t addr, uint16_t reg, uint8_t &out) {
  Wire.beginTransmission(addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission() != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  out = Wire.read();
  return true;
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

  gt911Addr = addr;
  touch.begin(addr);
  touch.setRotation(ROTATION_NORMAL);
  touchReady = true;
  // 400 kHz drops touches on some v3 panels. 300 kHz still suits a v2 GT911.
  Wire.setClock(300000);
  log_e("GT911 touch at 0x%02X", addr);
}

void touch_input_cooldown(unsigned long ms) {
  const unsigned long until = millis() + ms;
  if (static_cast<long>(until - blockPressUntilMs) > 0) {
    blockPressUntilMs = until;
  }
  reportedPressed = false;
  waitForFullRelease = true;
}

bool touch_input_read(int16_t &x, int16_t &y, bool &pressed) {
  if (!touchReady) {
    pressed = false;
    return false;
  }

  const unsigned long now = millis();

  // Only call TAMC_GT911::read() when GT911_POINT_INFO (0x814E) bit 7 is set.
  // Otherwise TAMC_GT911::read() sees 0x00 between 10 ms scan frames and falsely
  // reports isTouched=false mid-press, turning one tap into multiple clicks.
  uint8_t pointInfo = 0;
  if (gt911ReadReg(gt911Addr, GT911_POINT_INFO, pointInfo) && (pointInfo & 0x80)) {
    touch.read();
    rawPressed = touch.isTouched;
    if (rawPressed) {
      lastX = static_cast<int16_t>(
          mapAxis(touch.points[0].x, TOUCH_MAP_X1, TOUCH_MAP_X2, screenWidth - 1));
      lastY = static_cast<int16_t>(
          mapAxis(touch.points[0].y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, screenHeight - 1));
      lastRawTouchMs = now;
    }
  }

  const bool debouncedDown =
      rawPressed || (reportedPressed && (now - lastRawTouchMs < RELEASE_HOLD_MS));

  if (waitForFullRelease) {
    if (!debouncedDown) {
      waitForFullRelease = false;
    }
    x = lastX;
    y = lastY;
    pressed = false;
    return true;
  }

  if (reportedPressed) {
    if (!debouncedDown) {
      reportedPressed = false;
      const unsigned long until = now + REPRESS_COOLDOWN_MS;
      if (static_cast<long>(until - blockPressUntilMs) > 0) {
        blockPressUntilMs = until;
      }
    }
  } else if (debouncedDown) {
    if (static_cast<long>(now - blockPressUntilMs) >= 0) {
      reportedPressed = true;
    } else {
      // Finger touched down during cooldown; wait for lift so it doesn't fire late.
      waitForFullRelease = true;
    }
  }

  x = lastX;
  y = lastY;
  pressed = reportedPressed;
  return true;
}
