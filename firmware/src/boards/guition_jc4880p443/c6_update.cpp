#include "boards/board.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include <esp32-hal-hosted.h>
#include <esp_system.h>
#include <lvgl.h>

#include "device_log.h"
#include "hardware/panel.h"
#include "ota_update.h"
#include "ui/ui.h"

namespace {

constexpr unsigned long kAfterJoinMs = 12000;
constexpr uint16_t kHttpTimeoutMs = 60000;
constexpr size_t kBufSize = 2048;
constexpr uint32_t kMinHeap = 90000;

bool tried = false;
unsigned long connectedSinceMs = 0;

// Remember a host version we already tried so a panic mid-flash cannot loop.
bool alreadyTried(uint32_t packed) {
  Preferences p;
  if (!p.begin("c6ota", false)) {
    return false;
  }
  const uint32_t skipped = p.getUInt("host", 0);
  p.end();
  return skipped == packed && packed != 0;
}

void markTried(uint32_t packed) {
  Preferences p;
  p.begin("c6ota", false);
  p.putUInt("host", packed);
  p.end();
}

void clearTried() {
  Preferences p;
  if (!p.begin("c6ota", false)) {
    return;
  }
  if (p.isKey("host")) {
    p.remove("host");
  }
  p.end();
}

bool downloadAndFlash(const char *url) {
  WiFiClientSecure secure;
  secure.setInsecure();
  HTTPClient http;
  http.setTimeout(kHttpTimeoutMs);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);
  if (!http.begin(secure, url)) {
    device_log("C6: HTTP begin failed");
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    device_logf("C6: download HTTP %d", code);
    http.end();
    return false;
  }
  int remaining = http.getSize();
  if (remaining <= 0) {
    device_log("C6: update size not received");
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  if (!hostedBeginUpdate()) {
    device_log("C6: hosted begin failed");
    http.end();
    return false;
  }

  uint8_t *buf = static_cast<uint8_t *>(malloc(kBufSize));
  if (!buf) {
    device_log("C6: OTA buffer alloc failed");
    http.end();
    return false;
  }

  bool ok = false;
  uint32_t lastPump = millis();
  while (http.connected() && remaining > 0) {
    const size_t avail = stream->available();
    if (avail == 0) {
      delay(1);
      continue;
    }
    size_t chunk = avail > kBufSize ? kBufSize : avail;
    if (chunk > static_cast<size_t>(remaining)) {
      device_log("C6: extra bytes in download");
      break;
    }
    const int got = stream->readBytes(buf, chunk);
    if (got <= 0) {
      delay(1);
      continue;
    }
    remaining -= got;
    if (!hostedWriteUpdate(buf, static_cast<uint32_t>(got))) {
      device_log("C6: hosted write failed");
      break;
    }
    if (static_cast<uint32_t>(millis() - lastPump) >= 250) {
      lastPump = millis();
      lv_timer_handler();
      panelPoll();
    }
    if (remaining == 0) {
      if (!hostedEndUpdate()) {
        device_log("C6: hosted end failed");
        break;
      }
      if (!hostedActivateUpdate()) {
        device_log("C6: hosted activate failed");
        break;
      }
      ok = true;
    }
  }
  free(buf);
  http.end();
  return ok;
}

void runUpdate() {
  if (!hostedIsInitialized()) {
    device_log("C6: hosted not ready");
    return;
  }

  // Use the versions cached at Wi-Fi init. hostedHasUpdate() is another RPC
  // with a ~1s timeout that starves the UI and has panicked SDIO on this board.
  uint32_t hMaj = 0, hMin = 0, hPat = 0, sMaj = 0, sMin = 0, sPat = 0;
  hostedGetHostVersion(&hMaj, &hMin, &hPat);
  hostedGetSlaveVersion(&sMaj, &sMin, &sPat);
  device_logf("C6: slave %u.%u.%u host %u.%u.%u", sMaj, sMin, sPat, hMaj, hMin, hPat);

  // RPC fail reports 0.0.0. Starting slave OTA in that state panics SDIO.
  if (sMaj == 0 && sMin == 0 && sPat == 0) {
    device_log("C6: slave version unknown, skipping auto-update");
    return;
  }
  if (hMaj == sMaj && hMin == sMin && hPat == sPat) {
    // A failed attempt on the old slave left this host version marked skipped.
    clearTried();
    device_log("C6: radio firmware matches host");
    return;
  }
  if (hMaj < sMaj || (hMaj == sMaj && hMin < sMin) ||
      (hMaj == sMaj && hMin == sMin && hPat <= sPat)) {
    device_log("C6: slave is newer than host, skipping");
    return;
  }

  const uint32_t packed = (hMaj << 16) | (hMin << 8) | hPat;
  if (alreadyTried(packed)) {
    device_log("C6: skip radio update (previous attempt for this host)");
    return;
  }

  // Record the attempt before SDIO traffic so a panic cannot reboot-loop.
  markTried(packed);

  ui_set_connection_status("Updating C6 radio...");
  lv_timer_handler();
  panelPoll();

  const char *url = hostedGetUpdateURL();
  if (!url || !downloadAndFlash(url)) {
    ui_set_connection_status("C6 radio update failed");
    return;
  }

  ui_set_connection_status("C6 radio updated, rebooting");
  device_log("C6: co-processor updated, restarting");
  delay(400);
  esp_restart();
}

}  // namespace

void c6_update_loop() {
  if (tried || ota_is_active()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    connectedSinceMs = 0;
    return;
  }
  if (connectedSinceMs == 0) {
    connectedSinceMs = millis();
  }
  if (millis() - connectedSinceMs < kAfterJoinMs) {
    return;
  }
  if (ESP.getFreeHeap() < kMinHeap) {
    return;
  }
  tried = true;
  runUpdate();
}
