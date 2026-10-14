#pragma once

#include <stddef.h>
#include <stdint.h>

// Embeds "FilamanSwatches-board:<BOARD_ID>\n" in the image so an OTA upload
// for the wrong board can be refused.

const char *boardImageMarker();

void boardImageScanBegin();
enum BoardImageVerdict : uint8_t {
  BOARD_IMAGE_UNMARKED = 0,
  BOARD_IMAGE_MATCH,
  BOARD_IMAGE_FOREIGN,
};
BoardImageVerdict boardImageScanFeed(const uint8_t *data, size_t len);
bool boardImageScanAccepts();
