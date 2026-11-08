#pragma once

#include <stdint.h>

// Guition JC4880P443C_I_W / _I_W_Y (module JC-ESP32P4-M3): ESP32-P4 rev 3,
// ESP32-C6 Wi-Fi over SDIO, 16 MB flash, 32 MB PSRAM, 4.3" ST7701S 480x800
// MIPI-DSI, GT911. The UI stays 800x480; the panel rotates it onto the glass.

#define BOARD_ID                "guition-jc4880p443"
#define BOARD_MODEL             "Guition JC4880P443C 4.3-inch"
#define BOARD_OTA_ASSET         "firmware-guition-jc4880p443.bin"
#define BOARD_DEFAULT_HOSTNAME  "guition"
#define BOARD_BACKLIGHT_PWM     1

inline void boardEarlyInit() {}

namespace hw_pins {

// Must match src/boards/guition_jc4880p443/board_p4_pins.h.
constexpr int8_t LCD_RST   = 5;
constexpr int8_t LCD_BL    = 23;
constexpr int8_t TOUCH_SDA = 7;
constexpr int8_t TOUCH_SCL = 8;
constexpr int8_t TOUCH_RST = 3;

}
