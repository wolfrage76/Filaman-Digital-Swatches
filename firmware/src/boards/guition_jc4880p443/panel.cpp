#include "hardware/panel.h"

#include "boards/board.h"
#include "config.h"
#include "device_log.h"

#include <Arduino.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <cstring>

#include "board_p4.h"
#include "ui/menu_ui.h"
#include <lvgl.h>

namespace {

constexpr int NATIVE_W = BOARD_P4_LCD_H_RES;
constexpr int NATIVE_H = BOARD_P4_LCD_V_RES;

constexpr unsigned long RELEASE_HOLD_MS = 45;
constexpr unsigned long REPRESS_COOLDOWN_MS = 240;

uint16_t *logical = nullptr;
bool ready = false;
bool touchOk = false;

// One hardware rotate at a time. A burst of LVGL flushes used to stack full-frame
// PPA jobs on the same PSRAM bus the panel is scanning, and the glass froze.
constexpr uint32_t kPresentGapUs = 16000;
constexpr size_t kCacheLine = 64;
int dirtyY0 = SCREEN_HEIGHT;
int dirtyY1 = 0;
bool frameDirty = false;
uint32_t lastPresentStartUs = 0;

bool rawPressed = false;
bool reportedPressed = false;
bool waitForFullRelease = false;
int16_t lastX = 0;
int16_t lastY = 0;
unsigned long lastRawTouchMs = 0;
unsigned long blockPressUntilMs = 0;

static_assert(BOARD_P4_LCD_RST == hw_pins::LCD_RST, "LCD reset pin drifted from board_p4_pins.h");
static_assert(BOARD_P4_LCD_BL == hw_pins::LCD_BL, "backlight pin drifted from board_p4_pins.h");
static_assert(BOARD_P4_TOUCH_SDA == hw_pins::TOUCH_SDA, "touch SDA drifted from board_p4_pins.h");
static_assert(BOARD_P4_TOUCH_SCL == hw_pins::TOUCH_SCL, "touch SCL drifted from board_p4_pins.h");
static_assert(BOARD_P4_TOUCH_RST == hw_pins::TOUCH_RST, "touch reset drifted from board_p4_pins.h");
static_assert(BOARD_P4_ROTATION_CCW == 270 || BOARD_P4_ROTATION_CCW == 90, "rotation");

int16_t clampCoord(int v, int maxv) {
  if (v < 0) {
    return 0;
  }
  if (v > maxv) {
    return static_cast<int16_t>(maxv);
  }
  return static_cast<int16_t>(v);
}

void mapTouch(int px, int py, int16_t &lx, int16_t &ly) {
  px = clampCoord(px, NATIVE_W - 1);
  py = clampCoord(py, NATIVE_H - 1);
#if BOARD_P4_ROTATION_CCW == 270
  // Inverse of the PPA's 270 CCW turn: logical x is native y.
  const int x = py;
  const int y = (NATIVE_W - 1) - px;
#else
  const int x = (NATIVE_H - 1) - py;
  const int y = px;
#endif
  lx = clampCoord(x, SCREEN_WIDTH - 1);
  ly = clampCoord(y, SCREEN_HEIGHT - 1);
}

void writeAll(const uint8_t *data, size_t len) {
  while (len > 0) {
    const size_t chunk = len > 4096 ? 4096 : len;
    Serial.write(data, chunk);
    len -= chunk;
    data += chunk;
    yield();
  }
}

void dumpFrame(const char *tag, int w, int h, const uint16_t *px) {
  if (!px || w <= 0 || h <= 0) {
    Serial.printf("---%s missing---\n", tag);
    return;
  }
  Serial.printf("---%s %d %d---\n", tag, w, h);
  Serial.flush();
  writeAll(reinterpret_cast<const uint8_t *>(px), static_cast<size_t>(w) * h * sizeof(uint16_t));
  Serial.write('\n');
}

void markDirty(int y, int h) {
  if (h <= 0) {
    return;
  }
  if (y < dirtyY0) {
    dirtyY0 = y;
  }
  if (y + h > dirtyY1) {
    dirtyY1 = y + h;
  }
  frameDirty = true;
}

void syncLogical() {
  if (!logical) {
    return;
  }
  const size_t total = static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT * sizeof(uint16_t);
  size_t start = 0;
  size_t end = total;
  if (dirtyY1 > dirtyY0) {
    start = static_cast<size_t>(dirtyY0) * SCREEN_WIDTH * sizeof(uint16_t);
    end = static_cast<size_t>(dirtyY1) * SCREEN_WIDTH * sizeof(uint16_t);
  }
  start &= ~(kCacheLine - 1);
  end = (end + kCacheLine - 1) & ~(kCacheLine - 1);
  if (end > total) {
    end = total;
  }
  if (end > start) {
    esp_cache_msync(reinterpret_cast<uint8_t *>(logical) + start, end - start,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }
}

uint32_t presentNow() {
  if (!ready || !logical) {
    return 0;
  }
  syncLogical();
  const uint32_t started = micros();
  board_p4_present_rotated(logical, SCREEN_WIDTH, SCREEN_HEIGHT);
  const uint32_t dt = micros() - started;
  frameDirty = false;
  dirtyY0 = SCREEN_HEIGHT;
  dirtyY1 = 0;
  lastPresentStartUs = started;
  return dt;
}

void presentIfDue() {
  if (!frameDirty) {
    return;
  }
  const uint32_t now = micros();
  if (lastPresentStartUs != 0 && static_cast<uint32_t>(now - lastPresentStartUs) < kPresentGapUs) {
    return;
  }
  presentNow();
}

void runBench() {
  if (!logical || !ready) {
    Serial.println("BENCH frames=0 avg_us=0 max_us=0 orient=bad");
    return;
  }
  constexpr int kFrames = 30;
  uint64_t sum = 0;
  uint32_t mx = 0;
  for (int i = 0; i < kFrames; ++i) {
    const uint16_t color = (i & 1) ? 0xF800 : 0x07E0;
    const int x0 = (i * 24) % (SCREEN_WIDTH - 24);
    for (int y = 0; y < SCREEN_HEIGHT; y += 4) {
      for (int x = 0; x < 24; ++x) {
        logical[static_cast<size_t>(y) * SCREEN_WIDTH + x0 + x] = color;
      }
    }
    logical[0] = 0xF800;
    markDirty(0, SCREEN_HEIGHT);
    const uint32_t dt = presentNow();
    sum += dt;
    if (dt > mx) {
      mx = dt;
    }
  }
  int gw = 0;
  int gh = 0;
  const uint16_t *glass = board_p4_front_fb(&gw, &gh);
  bool orient = false;
  if (glass && gw == NATIVE_W && gh == NATIVE_H) {
#if BOARD_P4_ROTATION_CCW == 270
    const int px = NATIVE_W - 1;
    const int py = 0;
#else
    const int px = 0;
    const int py = NATIVE_H - 1;
#endif
    orient = glass[static_cast<size_t>(py) * gw + px] == 0xF800;
  }
  Serial.printf("BENCH frames=%d avg_us=%u max_us=%u orient=%s\n", kFrames,
                static_cast<unsigned>(sum / kFrames), mx, orient ? "ok" : "bad");
  Serial.flush();
}

void dumpScreens() {
  // Log lines from the Wi-Fi task would land in the middle of the pixel bytes.
  const esp_log_level_t previous = esp_log_level_get("*");
  esp_log_level_set("*", ESP_LOG_NONE);
  if (logical) {
    const size_t bytes = static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT * sizeof(uint16_t);
    esp_cache_msync(logical, bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  }
  int w = 0;
  int h = 0;
  dumpFrame("UI", SCREEN_WIDTH, SCREEN_HEIGHT, logical);
  const uint16_t *glass = board_p4_front_fb(&w, &h);
  dumpFrame("GLASS", w, h, glass);
  Serial.println("---END---");
  Serial.flush();
  esp_log_level_set("*", previous);
}

char cmd[8];
uint8_t cmdLen = 0;

}  // namespace

bool panelBegin() {
  device_logf("PSRAM free %u KB",
              static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  if (board_p4_display_init() != ESP_OK) {
    device_log("Panel: ST7701S MIPI display did not start");
    return false;
  }

  const size_t bytes = static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT * sizeof(uint16_t);
  logical = static_cast<uint16_t *>(
      heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!logical) {
    logical = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (!logical) {
    device_log("Panel: landscape framebuffer alloc failed");
    return false;
  }
  memset(logical, 0, bytes);
  esp_cache_msync(logical, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  board_p4_present_rotated(logical, SCREEN_WIDTH, SCREEN_HEIGHT);
  lastPresentStartUs = micros();

  touchOk = board_p4_touch_init() == ESP_OK;
  if (!touchOk) {
    device_log("Touch: GT911 does not answer");
  } else {
    device_log("Touch: GT911 ready");
  }
  device_logf("Panel: %d x %d landscape, glass %d x %d, rotate %d CCW", SCREEN_WIDTH, SCREEN_HEIGHT,
              NATIVE_W, NATIVE_H, BOARD_P4_ROTATION_CCW);
  ready = true;
  return true;
}

const uint16_t *panelFramebuffer() {
  if (logical) {
    // The PPA reads this buffer through DMA. A later CPU read can observe a
    // byte-swapped copy of the untouched rows still sitting in cache, which
    // made screenshots show a brown band under the header. Invalidate first so
    // the read matches the frame that was presented.
    const size_t bytes = static_cast<size_t>(SCREEN_WIDTH) * SCREEN_HEIGHT * sizeof(uint16_t);
    esp_cache_msync(logical, bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  }
  return logical;
}

void panelStopScanout() {
  ready = false;
  board_p4_display_stop();
}

void panelBlitStrip(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *px) {
  if (!logical || !px || w <= 0 || h <= 0) {
    return;
  }
  if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) {
    return;
  }
  const int32_t stride = w;
  if (x + w > SCREEN_WIDTH) {
    w = SCREEN_WIDTH - x;
  }
  if (y + h > SCREEN_HEIGHT) {
    h = SCREEN_HEIGHT - y;
  }
  for (int32_t row = 0; row < h; ++row) {
    memcpy(logical + static_cast<size_t>(y + row) * SCREEN_WIDTH + x, px + row * stride,
           static_cast<size_t>(w) * sizeof(uint16_t));
  }
  markDirty(y, h);
}

void panelPresent() { presentIfDue(); }

uint8_t panelTouchRead(PanelTouchPoint *points, uint8_t max_points) {
  if (!touchOk || !points || max_points == 0) {
    return 0;
  }

  const unsigned long now = millis();
  int px = 0;
  int py = 0;
  bool down = false;
  rawPressed = board_p4_touch_read(&px, &py, &down) && down;
  if (rawPressed) {
    mapTouch(px, py, lastX, lastY);
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

void panelSetBrightness(uint8_t brightness) { board_p4_set_brightness(brightness); }

void panelBacklightOff() { board_p4_set_brightness(0); }

void panelBacklightOn(uint8_t brightness) { board_p4_set_brightness(brightness ? brightness : 255); }

void panelPoll() {
  presentIfDue();
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      cmd[cmdLen] = '\0';
      if (cmdLen == 4 && strcmp(cmd, "shot") == 0) {
        dumpScreens();
      } else if (cmdLen == 5 && strcmp(cmd, "bench") == 0) {
        runBench();
      } else if (cmdLen == 4 && strcmp(cmd, "menu") == 0) {
        const uint32_t t0 = micros();
        menu_open();
        lv_refr_now(nullptr);
        const uint32_t openUs = micros() - t0;
        menu_close();
        lv_refr_now(nullptr);
        Serial.printf("MENU open_us=%u\n", openUs);
        Serial.flush();
      }
      cmdLen = 0;
      continue;
    }
    if (cmdLen + 1 < sizeof(cmd)) {
      cmd[cmdLen++] = c;
    }
  }
}
