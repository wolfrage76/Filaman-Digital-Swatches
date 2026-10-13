#include "ota_update.h"

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "app_config.h"
#include "config.h"
#include "ui/menu_ui.h"
#include "ui/ui.h"

namespace {

bool begun = false;
bool active = false;

String cachedLatestVersion;
String cachedDownloadUrl;
bool forceAutoCheck = false;
unsigned long lastAutoCheckMs = 0;
unsigned long wifiConnectedSinceMs = 0;
bool sawWifiConnected = false;

// First poll shortly after WiFi is up; then hourly. Manifest hits GitHub Pages
// (no API quota); the Releases API fallback is 1 req/hr, well under the
// unauthenticated 60/hr GitHub limit even with a few panels on one IP.
constexpr unsigned long kAutoCheckFirstDelayMs = 90UL * 1000UL;
constexpr unsigned long kAutoCheckIntervalMs = 60UL * 60UL * 1000UL;

constexpr const char *kManifestUrl =
    "https://wolfrage76.github.io/Filaman-Digital-Swatches/manifest-update.json";
constexpr const char *kPagesFirmwareUrl =
    "https://wolfrage76.github.io/Filaman-Digital-Swatches/firmware/firmware.bin";
constexpr const char *kReleasesApiUrl =
    "https://api.github.com/repos/wolfrage76/Filaman-Digital-Swatches/releases/latest";

String cleanVersion(const String &v) {
  String s = v;
  s.trim();
  if (s.startsWith("v") || s.startsWith("V")) {
    s.remove(0, 1);
  }
  return s;
}

void parseVersionParts(const String &v, int parts[3]) {
  parts[0] = parts[1] = parts[2] = 0;
  const String s = cleanVersion(v);
  sscanf(s.c_str(), "%d.%d.%d", &parts[0], &parts[1], &parts[2]);
}

void rememberCheckResult(const String &ver, const String &url) {
  cachedLatestVersion = ver;
  cachedDownloadUrl = url;
  lastAutoCheckMs = millis();
  forceAutoCheck = false;
  ui_refresh_update_badge();
}

}  // namespace

int ota_compare_versions(const String &a, const String &b) {
  int pa[3], pb[3];
  parseVersionParts(a, pa);
  parseVersionParts(b, pb);
  for (int i = 0; i < 3; ++i) {
    if (pa[i] != pb[i]) {
      return pa[i] - pb[i];
    }
  }
  return 0;
}

bool ota_check_github_update(String &latestVersionOut, String &downloadUrlOut, String &errorOut) {
  latestVersionOut = "";
  downloadUrlOut = "";
  errorOut = "";

  if (WiFi.status() != WL_CONNECTED) {
    errorOut = "WiFi not connected";
    return false;
  }

  // 1. Check GitHub Pages manifest first (tiny JSON, fast, no API rate limit).
  {
    WiFiClientSecure secure;
    secure.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if (http.begin(secure, kManifestUrl)) {
      http.addHeader("Cache-Control", "no-cache");
      const int code = http.GET();
      if (code == 200) {
        JsonDocument doc;
        if (deserializeJson(doc, http.getStream()) == DeserializationError::Ok) {
          const String ver = cleanVersion(doc["version"] | "");
          if (ver.length() > 0) {
            latestVersionOut = ver;
            downloadUrlOut = kPagesFirmwareUrl;
          }
        }
      }
      http.end();
    }
  }

  // 2. Also check GitHub Releases API if Pages failed or to pick up a newer release tag.
  {
    WiFiClientSecure secure;
    secure.setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if (http.begin(secure, kReleasesApiUrl)) {
      http.addHeader("User-Agent", "CrowPanel-Filaman-Swatches");
      http.addHeader("Accept", "application/vnd.github+json");
      const int code = http.GET();
      if (code == 200) {
        JsonDocument filter;
        filter["tag_name"] = true;
        filter["assets"][0]["name"] = true;
        filter["assets"][0]["browser_download_url"] = true;
        JsonDocument doc;
        if (deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter)) ==
            DeserializationError::Ok) {
          const String relVer = cleanVersion(doc["tag_name"] | "");
          if (relVer.length() > 0 &&
              (!latestVersionOut.length() || ota_compare_versions(relVer, latestVersionOut) > 0)) {
            latestVersionOut = relVer;
            downloadUrlOut = kPagesFirmwareUrl;
            for (JsonObject asset : doc["assets"].as<JsonArray>()) {
              const String name = asset["name"] | "";
              const String url = asset["browser_download_url"] | "";
              if (url.length() && (name.endsWith("-ota.bin") || name == "firmware.bin")) {
                downloadUrlOut = url;
                break;
              }
            }
          }
        }
      } else if (!latestVersionOut.length()) {
        errorOut = String("GitHub HTTP ") + code;
      }
      http.end();
    } else if (!latestVersionOut.length()) {
      errorOut = "Connection failed";
    }
  }

  if (!latestVersionOut.length()) {
    if (!errorOut.length()) {
      errorOut = "Could not read version from GitHub";
    }
    return false;
  }
  rememberCheckResult(latestVersionOut, downloadUrlOut);
  return true;
}

bool ota_install_github_update(const String &downloadUrl,
                               std::function<void(int pct)> onProgress,
                               String &errorOut) {
  errorOut = "";
  if (WiFi.status() != WL_CONNECTED) {
    errorOut = "WiFi not connected";
    return false;
  }

  const String url = downloadUrl.length() ? downloadUrl : String(kPagesFirmwareUrl);
  active = true;
  ui_set_connection_status("GitHub OTA...");

  WiFiClientSecure secure;
  secure.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);

  if (!http.begin(secure, url)) {
    active = false;
    errorOut = "HTTP begin failed";
    return false;
  }
  http.addHeader("User-Agent", "CrowPanel-Filaman-Swatches");

  const int code = http.GET();
  if (code != 200) {
    http.end();
    active = false;
    errorOut = String("Download HTTP ") + code;
    return false;
  }

  const int totalLen = http.getSize();
  if (totalLen > 0 && totalLen < 200000) {
    http.end();
    active = false;
    errorOut = "Invalid firmware size";
    return false;
  }

  if (!Update.begin(totalLen > 0 ? static_cast<size_t>(totalLen) : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    http.end();
    active = false;
    errorOut = String("Update.begin: ") + Update.errorString();
    return false;
  }

  if (onProgress) {
    onProgress(0);
  }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[2048];
  size_t written = 0;
  int lastPct = 0;
  unsigned long lastDataMs = millis();

  while (http.connected() && (totalLen <= 0 || written < static_cast<size_t>(totalLen))) {
    const size_t avail = stream->available();
    if (avail > 0) {
      const size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
      const int readBytes = stream->readBytes(buf, toRead);
      if (readBytes > 0) {
        if (Update.write(buf, static_cast<size_t>(readBytes)) != static_cast<size_t>(readBytes)) {
          Update.abort();
          http.end();
          active = false;
          errorOut = String("Flash write: ") + Update.errorString();
          return false;
        }
        written += static_cast<size_t>(readBytes);
        lastDataMs = millis();
        if (totalLen > 0 && onProgress) {
          const int pct = static_cast<int>((written * 100ULL) / static_cast<size_t>(totalLen));
          if (pct >= lastPct + 5 || pct == 100) {
            lastPct = pct;
            onProgress(pct);
          }
        }
      }
    } else {
      if (millis() - lastDataMs > 15000) {
        Update.abort();
        http.end();
        active = false;
        errorOut = "Download timed out";
        return false;
      }
      delay(2);
    }
    yield();
  }

  http.end();

  if (written < 200000) {
    Update.abort();
    active = false;
    errorOut = "Download incomplete";
    return false;
  }

  if (!Update.end(true) || !Update.isFinished()) {
    active = false;
    errorOut = String("Verify failed: ") + Update.errorString();
    return false;
  }

  if (onProgress) {
    onProgress(100);
  }
  ui_set_connection_status("OTA done, rebooting");
  return true;
}

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

bool ota_update_available() {
  return cachedLatestVersion.length() > 0 &&
         ota_compare_versions(cachedLatestVersion, FIRMWARE_VERSION) > 0;
}

String ota_cached_latest_version() { return cachedLatestVersion; }

String ota_cached_download_url() { return cachedDownloadUrl; }

void ota_auto_check_request_now() { forceAutoCheck = true; }

void ota_auto_check_loop() {
  if (!config().autoCheckUpdates) {
    forceAutoCheck = false;
    return;
  }
  if (WiFi.status() != WL_CONNECTED || active) {
    sawWifiConnected = false;
    wifiConnectedSinceMs = 0;
    return;
  }
  if (!sawWifiConnected) {
    sawWifiConnected = true;
    wifiConnectedSinceMs = millis();
  }
  // Avoid freezing the open menu during a multi-second HTTPS fetch.
  if (menu_is_open() && !forceAutoCheck) {
    return;
  }

  const unsigned long now = millis();
  if (!forceAutoCheck) {
    if (lastAutoCheckMs == 0) {
      if (now - wifiConnectedSinceMs < kAutoCheckFirstDelayMs) {
        return;
      }
    } else if (now - lastAutoCheckMs < kAutoCheckIntervalMs) {
      return;
    }
  }

  forceAutoCheck = false;
  lastAutoCheckMs = now;
  String ver, url, err;
  if (ota_check_github_update(ver, url, err)) {
    Serial.printf("Auto update check: latest v%s (%s)\n", ver.c_str(),
                  ota_update_available() ? "newer available" : "up to date");
  } else {
    Serial.printf("Auto update check failed: %s\n", err.c_str());
  }
}
