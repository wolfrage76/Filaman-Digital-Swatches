#include "hardware/panel.h"

#include "boards/board.h"
#include "config.h"
#include "device_log.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <Wire.h>
#include <cstring>
#include <esp_lcd_panel_ops.h>

#include "soc/soc.h"
#include "soc/usb_serial_jtag_reg.h"

#ifndef DISPLAY_USE_BOUNCE_BUFFER
#define DISPLAY_USE_BOUNCE_BUFFER 0
#endif

namespace {

constexpr int TOUCH_MAP_X1 = 800;
constexpr int TOUCH_MAP_X2 = 0;
constexpr int TOUCH_MAP_Y1 = 480;
constexpr int TOUCH_MAP_Y2 = 0;

constexpr unsigned long RELEASE_HOLD_MS = 45;
constexpr unsigned long REPRESS_COOLDOWN_MS = 240;

constexpr size_t BOUNCE_BUFFER_PX = static_cast<size_t>(SCREEN_WIDTH) * 10;

#if DISPLAY_USE_BOUNCE_BUFFER
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    hw_pins::LCD_DE, hw_pins::LCD_VSYNC, hw_pins::LCD_HSYNC, hw_pins::LCD_PCLK,
    14 /* R0 */, 21 /* R1 */, 47 /* R2 */, 48 /* R3 */, 45 /* R4 */,
    9 /* G0 */, 46 /* G1 */, 3 /* G2 */, 8 /* G3 */, 16 /* G4 */, 1 /* G5 */,
    15 /* B0 */, 7 /* B1 */, 6 /* B2 */, 5 /* B3 */, 4 /* B4 */,
    0 /* hsync_polarity */, 40 /* hsync_front_porch */, 48 /* hsync_pulse_width */, 40 /* hsync_back_porch */,
    0 /* vsync_polarity */, 1 /* vsync_front_porch */, 31 /* vsync_pulse_width */, 13 /* vsync_back_porch */,
    1 /* pclk_active_neg */, 15000000 /* prefer_speed */,
    false /* useBigEndian */, 0 /* de_idle_high */, 0 /* pclk_idle_high */,
    BOUNCE_BUFFER_PX);
#else
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    hw_pins::LCD_DE, hw_pins::LCD_VSYNC, hw_pins::LCD_HSYNC, hw_pins::LCD_PCLK,
    14 /* R0 */, 21 /* R1 */, 47 /* R2 */, 48 /* R3 */, 45 /* R4 */,
    9 /* G0 */, 46 /* G1 */, 3 /* G2 */, 8 /* G3 */, 16 /* G4 */, 1 /* G5 */,
    15 /* B0 */, 7 /* B1 */, 6 /* B2 */, 5 /* B3 */, 4 /* B4 */,
    0 /* hsync_polarity */, 40 /* hsync_front_porch */, 48 /* hsync_pulse_width */, 40 /* hsync_back_porch */,
    0 /* vsync_polarity */, 1 /* vsync_front_porch */, 31 /* vsync_pulse_width */, 13 /* vsync_back_porch */,
    1 /* pclk_active_neg */, 8000000 /* prefer_speed */);
#endif

Arduino_RGB_Display *gfx =
    new Arduino_RGB_Display(SCREEN_WIDTH, SCREEN_HEIGHT, bus, 0 /* rotation */, true /* auto_flush */);

TAMC_GT911 touch(hw_pins::TOUCH_SDA, hw_pins::TOUCH_SCL, -1, -1, TOUCH_MAP_X1, TOUCH_MAP_Y1);
uint8_t gt911Addr = 0;
bool touchReady = false;
bool scanoutStopped = false;

bool rawPressed = false;
bool reportedPressed = false;
bool waitForFullRelease = false;
int16_t lastX = 0;
int16_t lastY = 0;
unsigned long lastRawTouchMs = 0;
unsigned long blockPressUntilMs = 0;

constexpr uint32_t PWM_HZ = 5000;
constexpr uint8_t PWM_BITS = 8;
bool pwmOk = false;
uint8_t currentBrightness = 255;

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

void releaseGt911(uint8_t pca) {
  i2cWriteReg(pca, 0x03, 0xFF);
  i2cWriteReg(pca, 0x01, 0xFF);
  i2cWriteReg(pca, 0x02, 0x00);
  i2cWriteReg(pca, 0x03, 0x00);
  i2cWriteReg(pca, 0x01, 0xFC);
  delay(20);
  i2cWriteReg(pca, 0x01, 0xFD);
  delay(100);
  i2cWriteReg(pca, 0x03, 0x02);
  delay(50);
}

void touchBegin() {
  releaseUsbJtagPads();
  Wire.begin(hw_pins::TOUCH_SDA, hw_pins::TOUCH_SCL);
  Wire.setClock(100000);

  uint8_t addr = findGt911();
  if (addr == 0 && i2cProbe(hw_pins::PCA9557_ADDR)) {
    releaseGt911(hw_pins::PCA9557_ADDR);
    addr = findGt911();
  }

  if (addr == 0) {
    touchReady = false;
    device_log("GT911 not found");
    return;
  }

  gt911Addr = addr;
  touch.begin(addr);
  touch.setRotation(ROTATION_NORMAL);
  touchReady = true;
  Wire.setClock(300000);
  device_logf("GT911 touch at 0x%02X", addr);
}

extern "C" {
int Cache_WriteBack_Addr(uint32_t addr, uint32_t size);
uint32_t Cache_Get_DCache_Line_Size(void);
}

void IRAM_ATTR writeBackFramebuffer(const void *start, size_t bytes) {
  uint16_t *fb = gfx->getFramebuffer();
  if (!fb || !start || bytes == 0) {
    return;
  }
  static uint32_t line = 0;
  if (line == 0) {
    line = Cache_Get_DCache_Line_Size();
    if (line != 16 && line != 32 && line != 64) {
      line = 32;
    }
  }
  const uintptr_t fbStart = reinterpret_cast<uintptr_t>(fb);
  const uintptr_t fbEnd =
      fbStart + static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT * sizeof(uint16_t);
  uintptr_t addr = reinterpret_cast<uintptr_t>(start);
  uintptr_t end = addr + bytes;
  uintptr_t aligned = addr & ~(static_cast<uintptr_t>(line) - 1);
  uintptr_t alignedEnd = (end + line - 1) & ~(static_cast<uintptr_t>(line) - 1);
  if (aligned < fbStart) {
    aligned = fbStart;
  }
  if (alignedEnd > fbEnd) {
    alignedEnd = fbEnd;
  }
  if (alignedEnd > aligned) {
    Cache_WriteBack_Addr(static_cast<uint32_t>(aligned),
                         static_cast<uint32_t>(alignedEnd - aligned));
  }
}

}  // namespace

bool panelBegin() {
  pinMode(hw_pins::PANEL_ENABLE, OUTPUT);
  digitalWrite(hw_pins::PANEL_ENABLE, LOW);

  if (!gfx->begin()) {
    device_log("gfx->begin() failed");
    return false;
  }
  gfx->fillScreen(BLACK);

  pinMode(hw_pins::BACKLIGHT, OUTPUT);
  pwmOk = ledcAttach(hw_pins::BACKLIGHT, PWM_HZ, PWM_BITS);
  panelSetBrightness(255);

  touchBegin();
  return gfx->getFramebuffer() != nullptr;
}

const uint16_t *panelFramebuffer() {
  return gfx && !scanoutStopped ? gfx->getFramebuffer() : nullptr;
}

void panelStopScanout() {
  panelBacklightOff();
  if (scanoutStopped) {
    return;
  }
  scanoutStopped = true;
#if DISPLAY_USE_BOUNCE_BUFFER
  // panelHandle() is added to Arduino_GFX by patch_bounce.py.
  if (esp_lcd_panel_handle_t handle = bus->panelHandle()) {
    esp_lcd_panel_del(handle);
  }
#endif
}

void panelBlitStrip(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px) {
  uint16_t *fb = scanoutStopped ? nullptr : gfx->getFramebuffer();
  if (!fb || !px || w <= 0 || h <= 0) {
    return;
  }
  if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) {
    return;
  }
  const int stride = w;
  if (x + w > SCREEN_WIDTH) {
    w = SCREEN_WIDTH - x;
  }
  if (y + h > SCREEN_HEIGHT) {
    h = SCREEN_HEIGHT - y;
  }
  if (x == 0 && w == SCREEN_WIDTH && stride == SCREEN_WIDTH) {
    uint16_t *dst = fb + (static_cast<size_t>(y) * SCREEN_WIDTH);
    const size_t totalBytes =
        static_cast<size_t>(SCREEN_WIDTH) * static_cast<size_t>(h) * sizeof(uint16_t);
    memcpy(dst, px, totalBytes);
    writeBackFramebuffer(dst, totalBytes);
    return;
  }
  uint16_t *firstDst = fb + (static_cast<size_t>(y) * SCREEN_WIDTH) + x;
  for (int row = 0; row < h; ++row) {
    uint16_t *dst = fb + (static_cast<size_t>(y + row) * SCREEN_WIDTH) + x;
    const size_t nbytes = static_cast<size_t>(w) * sizeof(uint16_t);
    memcpy(dst, px + static_cast<size_t>(row) * stride, nbytes);
  }
  const uint16_t *lastEnd = fb + (static_cast<size_t>(y + h - 1) * SCREEN_WIDTH) + x + w;
  writeBackFramebuffer(firstDst, static_cast<size_t>(lastEnd - firstDst) * sizeof(uint16_t));
}

uint8_t panelTouchRead(PanelTouchPoint *points, uint8_t max_points) {
  if (!touchReady || !points || max_points == 0) {
    return 0;
  }

  const unsigned long now = millis();
  uint8_t pointInfo = 0;
  if (gt911ReadReg(gt911Addr, GT911_POINT_INFO, pointInfo) && (pointInfo & 0x80)) {
    touch.read();
    rawPressed = touch.isTouched;
    if (rawPressed) {
      lastX = static_cast<int16_t>(
          mapAxis(touch.points[0].x, TOUCH_MAP_X1, TOUCH_MAP_X2, SCREEN_WIDTH - 1));
      lastY = static_cast<int16_t>(
          mapAxis(touch.points[0].y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, SCREEN_HEIGHT - 1));
      lastRawTouchMs = now;
    }
  }

  const bool debouncedDown =
      rawPressed || (reportedPressed && (now - lastRawTouchMs < RELEASE_HOLD_MS));

  if (waitForFullRelease) {
    if (!debouncedDown) {
      waitForFullRelease = false;
    }
    return 0;
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
      waitForFullRelease = true;
    }
  }

  if (!reportedPressed) {
    return 0;
  }
  points[0].x = lastX;
  points[0].y = lastY;
  return 1;
}

void panelSetBrightness(uint8_t brightness) {
  currentBrightness = brightness;
  if (!pwmOk) {
    digitalWrite(hw_pins::BACKLIGHT, brightness > 0 ? HIGH : LOW);
    return;
  }
  ledcWrite(hw_pins::BACKLIGHT, brightness);
}

void panelTouchCooldown(unsigned long ms) {
  const unsigned long until = millis() + ms;
  if (static_cast<long>(until - blockPressUntilMs) > 0) {
    blockPressUntilMs = until;
  }
  reportedPressed = false;
  waitForFullRelease = true;
}

void panelBacklightOff() { panelSetBrightness(0); }

void panelBacklightOn(uint8_t brightness) { panelSetBrightness(brightness); }

// CrowPanel routes GT911 on GPIO19/20 over USB Serial/JTAG pads.
__attribute__((constructor)) static void crowpanel_release_usb_jtag_phy() {
  releaseUsbJtagPads();
}
