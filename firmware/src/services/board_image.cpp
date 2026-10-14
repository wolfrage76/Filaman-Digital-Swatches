#include "services/board_image.h"

#include <string.h>

#include "boards/board.h"

namespace {

const char MARKER[] = "FilamanSwatches-board:" BOARD_ID "\n";
constexpr size_t PREFIX_LEN = sizeof("FilamanSwatches-board:") - 1;
constexpr size_t MARKER_LEN = sizeof(MARKER) - 1;
constexpr char MARKER_END = '\n';
constexpr size_t NAME_MAX = 40;

// CrowPanel images shipped before board markers are accepted on CrowPanel only.
constexpr bool UNMARKED_IS_OURS =
#if defined(BOARD_CROWPANEL_7)
    true;
#else
    false;
#endif

BoardImageVerdict s_verdict = BOARD_IMAGE_UNMARKED;
size_t s_matched = 0;
bool s_in_name = false;
char s_name[NAME_MAX + 1];
size_t s_name_len = 0;

void nameComplete() {
  const size_t own_len = MARKER_LEN - PREFIX_LEN - 1;
  const bool ours =
      (s_name_len == own_len) && (memcmp(s_name, MARKER + PREFIX_LEN, own_len) == 0);
  if (ours) {
    s_verdict = BOARD_IMAGE_MATCH;
  } else if (s_verdict == BOARD_IMAGE_UNMARKED) {
    s_verdict = BOARD_IMAGE_FOREIGN;
  }
}

// Keep the marker string in .rodata so it appears in the flashed image.
volatile const char *const kKeepMarker = MARKER;

}  // namespace

const char *boardImageMarker() {
  (void)kKeepMarker;
  static char line[MARKER_LEN];
  memcpy(line, MARKER, MARKER_LEN - 1);
  line[MARKER_LEN - 1] = '\0';
  return line;
}

void boardImageScanBegin() {
  s_verdict = BOARD_IMAGE_UNMARKED;
  s_matched = 0;
  s_in_name = false;
  s_name_len = 0;
}

BoardImageVerdict boardImageScanFeed(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char c = static_cast<char>(data[i]);
    if (s_in_name) {
      if (c == MARKER_END) {
        nameComplete();
        s_in_name = false;
      } else if (s_name_len < NAME_MAX) {
        s_name[s_name_len++] = c;
      } else {
        s_in_name = false;
      }
      continue;
    }
    if (c == MARKER[s_matched]) {
      if (++s_matched == PREFIX_LEN) {
        s_matched = 0;
        s_in_name = true;
        s_name_len = 0;
      }
    } else {
      s_matched = (c == MARKER[0]) ? 1 : 0;
    }
  }
  return s_verdict;
}

bool boardImageScanAccepts() {
  if (s_verdict == BOARD_IMAGE_MATCH) {
    return true;
  }
  if (s_verdict == BOARD_IMAGE_FOREIGN) {
    return false;
  }
  return UNMARKED_IS_OURS;
}
