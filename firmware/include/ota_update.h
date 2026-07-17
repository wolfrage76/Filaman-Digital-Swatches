#pragma once

#include <Arduino.h>

// LAN ArduinoOTA now; keep this API so a later web upload/config UI can plug in.
void ota_begin();
void ota_loop();
bool ota_is_active();
