#include "hardware/panel.h"

#include "boards/board.h"
#include "config.h"
#include "device_log.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_system.h>
#include <cstring>

#include "ch422g.h"
#include "gt911.h"

namespace {

constexpr int PANEL_W = 800;
constexpr int PANEL_H = 480;

// Proven SpoolmanScale timings: 14 MHz over 820x500 clocks ~34 fps.
constexpr uint32_t LCD_PCLK_HZ = 14000000;
constexpr uint32_t LCD_HSYNC_PULSE = 4;
constexpr uint32_t LCD_HSYNC_BACK = 8;
constexpr uint32_t LCD_HSYNC_FRONT = 8;
constexpr uint32_t LCD_VSYNC_PULSE = 4;
constexpr uint32_t LCD_VSYNC_BACK = 8;
constexpr uint32_t LCD_VSYNC_FRONT = 8;
constexpr size_t LCD_BOUNCE_LINES = 10;
constexpr size_t LCD_DMA_BURST_BYTES = 64;

constexpr unsigned long RELEASE_HOLD_MS = 45;
constexpr unsigned long REPRESS_COOLDOWN_MS = 240;

esp_lcd_panel_handle_t s_panel = nullptr;
uint16_t *s_fb = nullptr;
bool s_touch_ok = false;
uint8_t soft_dim = 255;

bool rawPressed = false;
bool reportedPressed = false;
bool waitForFullRelease = false;
int16_t lastX = 0;
int16_t lastY = 0;
unsigned long lastRawTouchMs = 0;
unsigned long blockPressUntilMs = 0;

void panelStop() {
  ch422g::write(hw_pins::EXIO_BACKLIGHT, false);
  if (!s_panel) {
    return;
  }
  s_fb = nullptr;
  esp_lcd_panel_del(s_panel);
  s_panel = nullptr;
}

bool rgbBegin() {
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = LCD_PCLK_HZ;
  cfg.timings.h_res = PANEL_W;
  cfg.timings.v_res = PANEL_H;
  cfg.timings.hsync_pulse_width = LCD_HSYNC_PULSE;
  cfg.timings.hsync_back_porch = LCD_HSYNC_BACK;
  cfg.timings.hsync_front_porch = LCD_HSYNC_FRONT;
  cfg.timings.vsync_pulse_width = LCD_VSYNC_PULSE;
  cfg.timings.vsync_back_porch = LCD_VSYNC_BACK;
  cfg.timings.vsync_front_porch = LCD_VSYNC_FRONT;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 1;
  cfg.bounce_buffer_size_px = PANEL_W * LCD_BOUNCE_LINES;
  cfg.dma_burst_size = LCD_DMA_BURST_BYTES;
  cfg.hsync_gpio_num = hw_pins::LCD_HSYNC;
  cfg.vsync_gpio_num = hw_pins::LCD_VSYNC;
  cfg.de_gpio_num = hw_pins::LCD_DE;
  cfg.pclk_gpio_num = hw_pins::LCD_PCLK;
  cfg.disp_gpio_num = -1;
  for (int i = 0; i < 16; i++) {
    cfg.data_gpio_nums[i] = hw_pins::LCD_DATA[i];
  }
  cfg.flags.fb_in_psram = 1;

  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
  if (err == ESP_OK) {
    err = esp_lcd_panel_reset(s_panel);
  }
  if (err == ESP_OK) {
    err = esp_lcd_panel_init(s_panel);
  }
  if (err != ESP_OK) {
    device_logf("Panel: RGB driver refused, error 0x%x", static_cast<unsigned>(err));
    return false;
  }
  void *fb = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &fb) != ESP_OK || !fb) {
    device_log("Panel: no frame buffer");
    return false;
  }
  s_fb = static_cast<uint16_t *>(fb);
  memset(s_fb, 0, static_cast<size_t>(PANEL_W) * PANEL_H * sizeof(uint16_t));
  esp_register_shutdown_handler(panelStop);
  return true;
}

void touchBegin() {
  static constexpr uint32_t RESET_HOLD_MS = 10;
  static constexpr uint32_t ADDR_HOLD_MS = 10;
  static constexpr uint32_t BOOT_MS = 60;
  pinMode(hw_pins::TOUCH_INT, OUTPUT);
  digitalWrite(hw_pins::TOUCH_INT, LOW);
  ch422g::write(hw_pins::EXIO_TOUCH_RST, false);
  delay(RESET_HOLD_MS);
  ch422g::write(hw_pins::EXIO_TOUCH_RST, true);
  delay(ADDR_HOLD_MS);
  pinMode(hw_pins::TOUCH_INT, INPUT);
  delay(BOOT_MS);
  s_touch_ok = gt911::begin(Wire);
  if (s_touch_ok) {
    device_logf("Touch: GT911 ready (%u x %u)", static_cast<unsigned>(gt911::maxX()),
                static_cast<unsigned>(gt911::maxY()));
  } else {
    device_log("Touch: GT911 does not answer");
  }
}

int16_t clampTo(int32_t v, int32_t maxv) {
  if (v < 0) {
    return 0;
  }
  if (v > maxv) {
    return static_cast<int16_t>(maxv);
  }
  return static_cast<int16_t>(v);
}

// Keep a readable floor so duty 10% is not three grey levels (SpoolmanScale).
constexpr float SOFT_DIM_FLOOR = 0.25f;

uint16_t dimPixel(uint16_t px) {
  if (soft_dim >= 255) {
    return px;
  }
  const float scale =
      SOFT_DIM_FLOOR + (1.0f - SOFT_DIM_FLOOR) * (static_cast<float>(soft_dim) / 255.0f);
  const uint8_t r = (px >> 11) & 0x1F;
  const uint8_t g = (px >> 5) & 0x3F;
  const uint8_t b = px & 0x1F;
  const uint8_t nr = static_cast<uint8_t>(r * scale + 0.5f);
  const uint8_t ng = static_cast<uint8_t>(g * scale + 0.5f);
  const uint8_t nb = static_cast<uint8_t>(b * scale + 0.5f);
  return static_cast<uint16_t>((nr << 11) | (ng << 5) | nb);
}

}  // namespace

bool panelBegin() {
  static constexpr uint32_t LCD_RESET_SETTLE_MS = 20;
  ch422g::write(hw_pins::EXIO_LCD_RST, true);
  delay(LCD_RESET_SETTLE_MS);
  const bool lcd_ok = rgbBegin();
  if (!lcd_ok) {
    device_log("Panel: RGB driver did not start");
  } else {
    device_logf("Panel: %d x %d at %u MHz", PANEL_W, PANEL_H,
                static_cast<unsigned>(LCD_PCLK_HZ / 1000000));
  }
  touchBegin();
  if (lcd_ok) {
    ch422g::write(hw_pins::EXIO_BACKLIGHT, true);
  }
  return lcd_ok;
}

const uint16_t *panelFramebuffer() { return s_fb; }

void panelBlitStrip(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px) {
  if (!s_fb || !px || w <= 0 || h <= 0) {
    return;
  }
  if (x < 0 || y < 0 || x >= PANEL_W || y >= PANEL_H) {
    return;
  }
  const int32_t stride = w;
  if (x + w > PANEL_W) {
    w = PANEL_W - x;
  }
  if (y + h > PANEL_H) {
    h = PANEL_H - y;
  }
  for (int32_t row = 0; row < h; ++row) {
    uint16_t *dst = s_fb + (y + row) * PANEL_W + x;
    const uint16_t *src = px + row * stride;
    if (soft_dim >= 255) {
      memcpy(dst, src, static_cast<size_t>(w) * sizeof(uint16_t));
    } else {
      for (int32_t col = 0; col < w; ++col) {
        dst[col] = dimPixel(src[col]);
      }
    }
  }
}

uint8_t panelTouchRead(PanelTouchPoint *points, uint8_t max_points) {
  if (!s_touch_ok || !points || max_points == 0) {
    return 0;
  }

  const unsigned long now = millis();
  gt911::Point raw[5];
  const uint8_t n = gt911::read(raw, 1);
  rawPressed = n > 0;
  if (rawPressed) {
    const int32_t chip_w = gt911::maxX() ? gt911::maxX() : PANEL_W;
    const int32_t chip_h = gt911::maxY() ? gt911::maxY() : PANEL_H;
    lastX = clampTo(static_cast<int32_t>(raw[0].x) * PANEL_W / chip_w, PANEL_W - 1);
    lastY = clampTo(static_cast<int32_t>(raw[0].y) * PANEL_H / chip_h, PANEL_H - 1);
    lastRawTouchMs = now;
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

void panelTouchCooldown(unsigned long ms) {
  const unsigned long until = millis() + ms;
  if (static_cast<long>(until - blockPressUntilMs) > 0) {
    blockPressUntilMs = until;
  }
  reportedPressed = false;
  waitForFullRelease = true;
}

void panelSetBrightness(uint8_t brightness) { soft_dim = brightness; }

void panelBacklightOff() { ch422g::write(hw_pins::EXIO_BACKLIGHT, false); }

void panelBacklightOn(uint8_t brightness) {
  soft_dim = brightness ? brightness : 255;
  ch422g::write(hw_pins::EXIO_BACKLIGHT, true);
}
