#pragma once

#include <stdint.h>

// Waveshare ESP32-S3-Touch-LCD-7: 16 MB flash, 8 MB octal PSRAM, 800x480 RGB
// (ST7262), GT911 touch, CH422G expander for backlight / resets.

#define BOARD_ID                "waveshare-s3-lcd7"
#define BOARD_MODEL             "Waveshare ESP32-S3-Touch-LCD-7"
#define BOARD_OTA_ASSET         "firmware-waveshare-s3-lcd7.bin"
#define BOARD_DEFAULT_HOSTNAME  "waveshare-ams"
#define BOARD_BACKLIGHT_PWM     0

void boardEarlyInit();

namespace hw_pins {

constexpr int8_t BOARD_SDA = 8;
constexpr int8_t BOARD_SCL = 9;
constexpr int8_t TOUCH_INT = 4;

constexpr int8_t LCD_PCLK  = 7;
constexpr int8_t LCD_HSYNC = 46;
constexpr int8_t LCD_VSYNC = 3;
constexpr int8_t LCD_DE    = 5;
constexpr int8_t LCD_DATA[16] = {
    14, 38, 18, 17, 10,     // B3..B7
    39, 0, 45, 48, 47, 21,  // G2..G7
    1, 2, 42, 41, 40,       // R3..R7
};

constexpr uint8_t EXIO_TOUCH_RST = 1;
constexpr uint8_t EXIO_BACKLIGHT = 2;
constexpr uint8_t EXIO_LCD_RST   = 3;
constexpr uint8_t EXIO_SD_CS     = 4;
constexpr uint8_t EXIO_USB_SEL   = 5;

}
