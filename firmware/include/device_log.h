#pragma once

#include <Arduino.h>

// Small in-memory event log for the diagnostics webpage. Messages are also
// written to Serial so existing USB diagnostics remain intact.
void device_log(const String &message);
void device_logf(const char *format, ...);
String device_log_snapshot();
void device_log_clear();
