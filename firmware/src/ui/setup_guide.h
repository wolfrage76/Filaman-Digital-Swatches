#pragma once

#include <Arduino.h>

// First-run step 2: once WiFi is up but FilaMan has no URL/API key, a full-screen
// overlay shows a QR code for the panel's web config page, with options to
// enter the details on the display instead or skip (MQTT-only setups).

void setup_guide_begin();                    // build the (hidden) overlay once, at startup
void setup_guide_open(const String &ip);     // show it for this IP
void setup_guide_close();
bool setup_guide_is_open();
bool setup_guide_dismissed();                // "Skip for now" pressed this boot
