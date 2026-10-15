#pragma once

#include <Arduino.h>

// Time zones offered in the web config page and on the panel. People pick a
// place; the POSIX rule is what gets stored (NVS "tz") and passed to TZ.
// Labels are ASCII only so the panel's built-in fonts can draw them.
struct TimezoneOption {
  const char *label;
  const char *posix;
};

extern const TimezoneOption kTimezones[];
extern const int kTimezoneCount;

// Index of the entry whose rule equals `posix`, or -1 if it is not listed
// (for example a rule set from secrets.h or by an older firmware).
int timezone_index(const String &posix);
