#pragma once

#include <Arduino.h>

#include "models.h"

// Printers offered by the Display API or the MQTT fallback, plus the
// monitored id. The web config page and the on-device picker read it. All
// access happens on the single main/LVGL loop, so no locking is needed.

// Replace the stored list (called from the MQTT printers callback).
void printer_list_set(const PrinterOption *printers, int count, const String &activeId);

// Copy up to `max` options into `out`; returns the count. `activeId` receives
// the monitored printer id.
int printer_list_get(PrinterOption *out, int max, String &activeId);

// Register how a selection is applied (Display API id, or MQTT publish).
using PrinterPublishFn = void (*)(const String &id);
void printer_list_set_publisher(PrinterPublishFn fn);

// Apply `id` as the monitored printer and mark it active immediately.
void printer_list_publish_selection(const String &id);
