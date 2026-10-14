#pragma once

// ============================================================
//  THE BOARD THIS FIRMWARE IS BUILT FOR
//
//  One firmware tree, several boards. Everything a board decides lives in its
//  own folder under boards/, and nothing outside that folder names a board:
//
//    board_config.h   identity, pins, and what its hardware can do
//    panel.cpp        display, touch and backlight behind hardware/panel.h
//    board.cpp        boardEarlyInit(), when the board needs one
//
//  The environment in platformio.ini picks the board twice: it defines one
//  BOARD_* macro, which selects the header below, and its build_src_filter
//  compiles that board's folder and no other.
//
//  What a board_config.h has to provide:
//
//    BOARD_ID                the environment's name, as in platformio.ini
//    BOARD_MODEL             the name a person would know it by
//    BOARD_OTA_ASSET         app image file name for releases / web flasher
//    BOARD_DEFAULT_HOSTNAME  default OTA / mDNS hostname
//    BOARD_BACKLIGHT_PWM     1 when the backlight can be dimmed in hardware,
//                            0 when it only switches (soft-dim in the blit)
//    boardEarlyInit()        runs first in setup(), before the display
//    namespace hw_pins       pins the board's own panel code needs
// ============================================================

#if defined(BOARD_CROWPANEL_7)
  #include "boards/crowpanel_7/board_config.h"
#elif defined(BOARD_WAVESHARE_S3_LCD7)
  #include "boards/waveshare_s3_lcd7/board_config.h"
#else
  #error "No board selected: the environment in platformio.ini must define a BOARD_* macro"
#endif
