#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <esp32-hal-hosted.h>

// One-shot: init ESP-Hosted over SDIO, do not start Wi-Fi, push the embedded
// Arduino C6 slave image (2.9.6). Old factory slaves panic if Wi-Fi traffic
// shares the bus during hosted OTA.

extern "C" {
extern const uint8_t c6_fw_start[];
extern const uint8_t c6_fw_end[];
}

namespace {

constexpr size_t kChunk = 1024;

void say(const char *msg) {
  Serial.println(msg);
  Serial.flush();
}

void sayf(const char *fmt, ...) {
  char buf[192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  say(buf);
}

bool initHosted() {
  // Init the SDIO transport only. Do not call WiFi.begin(): traffic on the
  // same bus is what panicked factory C6 firmware during hosted OTA.
  if (!hostedInitWiFi()) {
    say("hostedInitWiFi failed");
    return false;
  }
  return true;
}

bool flashC6() {
  const uint8_t *fw = c6_fw_start;
  const size_t len = static_cast<size_t>(c6_fw_end - c6_fw_start);
  if (len < 200000) {
    sayf("embedded C6 image too small (%u bytes)", static_cast<unsigned>(len));
    return false;
  }
  sayf("C6 image %u bytes", static_cast<unsigned>(len));

  uint32_t hMaj = 0, hMin = 0, hPat = 0, sMaj = 0, sMin = 0, sPat = 0;
  hostedGetHostVersion(&hMaj, &hMin, &hPat);
  hostedGetSlaveVersion(&sMaj, &sMin, &sPat);
  sayf("host %u.%u.%u  slave %u.%u.%u (0.0.0 means RPC failed; OTA still tried)", hMaj, hMin,
       hPat, sMaj, sMin, sPat);

  if (!hostedBeginUpdate()) {
    say("hostedBeginUpdate failed");
    return false;
  }
  say("writing...");

  size_t off = 0;
  uint32_t lastPct = 0;
  while (off < len) {
    size_t n = len - off;
    if (n > kChunk) {
      n = kChunk;
    }
    uint8_t *chunk = const_cast<uint8_t *>(fw + off);
    if (!hostedWriteUpdate(chunk, static_cast<uint32_t>(n))) {
      sayf("hostedWriteUpdate failed at %u", static_cast<unsigned>(off));
      return false;
    }
    off += n;
    const uint32_t pct = static_cast<uint32_t>((off * 100) / len);
    if (pct != lastPct && pct % 5 == 0) {
      lastPct = pct;
      sayf("%u%%  %u / %u", pct, static_cast<unsigned>(off), static_cast<unsigned>(len));
    }
    delay(1);
  }

  if (!hostedEndUpdate()) {
    say("hostedEndUpdate failed (image rejected by slave)");
    return false;
  }
  if (!hostedActivateUpdate()) {
    say("hostedActivateUpdate failed");
    return false;
  }
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);
  say("=== Guition C6 SDIO OTA helper ===");
  say("Wi-Fi will not start. Watch this port; then reflash Digital Swatch.");

  if (!initHosted()) {
    say("FAIL: could not talk to the C6 over SDIO");
    return;
  }
  if (!flashC6()) {
    say("FAIL: C6 OTA did not complete. UART flash on JP1 is the fallback.");
    return;
  }
  say("PASS: C6 radio firmware written. Reflash Digital Swatch:");
  say("  cd firmware && ../.venv/bin/pio run -e guition-jc4880p443 -t upload");
}

void loop() { delay(1000); }
