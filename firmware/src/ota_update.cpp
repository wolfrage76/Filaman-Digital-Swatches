#include "ota_update.h"

#include <ArduinoOTA.h>
#include <WiFi.h>

#include "app_config.h"
#include "config.h"
#include "ui/ui.h"

namespace {

bool begun = false;
bool active = false;

}  // namespace

void ota_begin() {
  if (begun || WiFi.status() != WL_CONNECTED) {
    return;
  }

  ArduinoOTA.setHostname(config().otaHostname.c_str());
  if (config().otaPassword.length() > 0) {
    ArduinoOTA.setPassword(config().otaPassword.c_str());
  }

  ArduinoOTA.onStart([]() {
    active = true;
    ui_set_connection_status("OTA updating...");
    Serial.println("OTA start");
  });
  ArduinoOTA.onEnd([]() {
    active = false;
    ui_set_connection_status("OTA done, rebooting");
    Serial.println("OTA end");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    active = false;
    String msg = "OTA error ";
    msg += String(static_cast<int>(error));
    ui_set_connection_status(msg);
    Serial.println(msg);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (total == 0) {
      return;
    }
    static unsigned int lastPct = 999;
    const unsigned int pct = (progress * 100U) / total;
    if (pct == lastPct || (pct % 10) != 0) {
      return;
    }
    lastPct = pct;
    Serial.printf("OTA %u%%\n", pct);
  });

  ArduinoOTA.begin();
  begun = true;
  Serial.printf("OTA ready as %s @ %s\n", config().otaHostname.c_str(),
                WiFi.localIP().toString().c_str());
}

void ota_loop() {
  if (!begun) {
    return;
  }
  ArduinoOTA.handle();
}

bool ota_is_active() {
  return active;
}
