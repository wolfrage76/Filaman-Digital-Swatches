#pragma once

// On-device web config page (synchronous WebServer on port 80). Serves an
// HTML form to view/edit runtime settings (WiFi/MQTT/FilaMan/device) and saves
// them to NVS, then reboots to apply. HTTP basic auth uses the OTA password.

void web_portal_begin();  // idempotent: starts the server once WiFi is up
void web_portal_loop();   // pump the server (call from loop() when connected)
