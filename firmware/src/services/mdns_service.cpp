#include "services/mdns_service.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <mdns.h>

#include "app_config.h"
#include "boards/board.h"
#include "config.h"
#include "device_log.h"
#include "web_portal.h"

namespace {

constexpr uint16_t OTA_PORT = 3232;  // ArduinoOTA's default
constexpr unsigned long SYNC_INTERVAL_MS = 1000;

bool running = false;
bool serviceAdded = false;
// The name the responder actually came up under. Compared against the stored
// hostname every pass, so a rename needs no notification of its own.
String advertised;
unsigned long lastSyncMs = 0;

void stopResponder() {
  if (!running) {
    return;
  }
  MDNS.end();
  running = false;
  serviceAdded = false;
  advertised = "";
}

// The http service is advertised only while there is something behind it, so
// a Bonjour browser never lists a panel whose page refuses to load.
void addService() {
  if (!MDNS.addService("http", "tcp", 80)) {
    return;
  }
  serviceAdded = true;
  MDNS.addServiceTxt("http", "tcp", "fw", FIRMWARE_VERSION);
  MDNS.addServiceTxt("http", "tcp", "model", BOARD_MODEL);
  MDNS.addServiceTxt("http", "tcp", "board", BOARD_ID);
}

// ESPmDNS has no removeService, so this reaches past it into the IDF
// component it wraps.
void removeService() {
  mdns_service_remove("_http", "_tcp");
  serviceAdded = false;
}

}  // namespace

bool mdns_running() { return running; }

void mdns_stop() { stopResponder(); }

void mdns_sync_state() {
  const unsigned long now = millis();
  if (lastSyncMs != 0 && now - lastSyncMs < SYNC_INTERVAL_MS) {
    return;
  }
  lastSyncMs = now;

  const String &host = config().otaHostname;
  if (WiFi.status() != WL_CONNECTED || !config().mdnsEnabled || !host.length()) {
    stopResponder();
    return;
  }

  // Dropped rather than renamed in place: mdns_hostname_set() leaves the
  // advertised services pointing at the old name. Both come back up together
  // further down this same pass.
  if (running && advertised != host) {
    stopResponder();
  }

  if (!running) {
    if (!MDNS.begin(host.c_str())) {
      // Not fatal and not retried on a timer of its own: the next pass tries
      // again a second later, and the IP address still works.
      device_logf("mDNS: failed to start as %s.local", host.c_str());
      return;
    }
    // The instance name is what a browser shows in a device list, so it is
    // the product name rather than the host label.
    MDNS.setInstanceName("Digital Swatch System");
    MDNS.enableArduino(OTA_PORT, config().otaPassword.length() > 0);
    running = true;
    advertised = host;
    device_logf("mDNS: responding as %s.local", host.c_str());
  }

  const bool serving = web_portal_running();
  if (serving && !serviceAdded) {
    addService();
  } else if (!serving && serviceAdded) {
    removeService();
  }
}
