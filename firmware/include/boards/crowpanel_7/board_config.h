#pragma once

#include <stdint.h>

// Elecrow CrowPanel 7" ESP32-S3: 800x480 RGB (EK9716), GT911 touch, PCA9557
// expander on v3, PWM backlight on GPIO 2, 4 MB flash + octal PSRAM.

#define BOARD_ID                "crowpanel_7_idf"
#define BOARD_MODEL             "CrowPanel 7-inch"
#define BOARD_OTA_ASSET         "firmware-crowpanel-s3-lcd7.bin"
#define BOARD_DEFAULT_HOSTNAME  "crowpanel"
#define BOARD_BACKLIGHT_PWM     1

inline void boardEarlyInit() {}

namespace hw_pins {

constexpr int8_t PANEL_ENABLE = 38;
constexpr int8_t BACKLIGHT    = 2;

constexpr int8_t TOUCH_SDA = 19;
constexpr int8_t TOUCH_SCL = 20;

constexpr uint8_t PCA9557_ADDR = 0x18;

// RGB bus (Arduino_ESP32RGBPanel order).
constexpr int8_t LCD_DE    = 41;
constexpr int8_t LCD_VSYNC = 40;
constexpr int8_t LCD_HSYNC = 39;
constexpr int8_t LCD_PCLK  = 0;

}
