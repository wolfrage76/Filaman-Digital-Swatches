#include "device_log.h"

#include <cstdarg>

namespace {

constexpr size_t LOG_LINE_COUNT = 40;
constexpr size_t LOG_LINE_LENGTH = 160;

char lines[LOG_LINE_COUNT][LOG_LINE_LENGTH] = {};
size_t nextLine = 0;
size_t lineCount = 0;

void storeLine(const char *message) {
  if (!message) {
    return;
  }
  snprintf(lines[nextLine], LOG_LINE_LENGTH, "[%10lu ms] %s", millis(), message);
  nextLine = (nextLine + 1) % LOG_LINE_COUNT;
  if (lineCount < LOG_LINE_COUNT) {
    ++lineCount;
  }
}

}  // namespace

void device_log(const String &message) {
  String clean = message;
  clean.replace("\r", "");
  clean.replace("\n", " ");
  clean.trim();
  if (!clean.length()) {
    return;
  }
  Serial.println(clean);
  storeLine(clean.c_str());
}

void device_logf(const char *format, ...) {
  char buffer[LOG_LINE_LENGTH];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  device_log(String(buffer));
}

String device_log_snapshot() {
  String out;
  out.reserve(lineCount * 96);
  const size_t oldest = (nextLine + LOG_LINE_COUNT - lineCount) % LOG_LINE_COUNT;
  for (size_t i = 0; i < lineCount; ++i) {
    const size_t index = (oldest + i) % LOG_LINE_COUNT;
    out += lines[index];
    out += '\n';
  }
  return out;
}

void device_log_clear() {
  nextLine = 0;
  lineCount = 0;
  for (size_t i = 0; i < LOG_LINE_COUNT; ++i) {
    lines[i][0] = '\0';
  }
  device_log("Diagnostics log cleared");
}
