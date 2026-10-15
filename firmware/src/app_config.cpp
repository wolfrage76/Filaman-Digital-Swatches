#include "app_config.h"

#include <Preferences.h>
#include <esp_mac.h>

#include "boards/board.h"

namespace {
AppConfig g_config;

// Board name plus the last two MAC bytes (e.g. "crowpanel-3f2a"), so several
// panels on one LAN don't all claim the same mDNS name. Readable before WiFi starts.
String defaultHostname() {
#ifdef OTA_HOSTNAME
  return OTA_HOSTNAME;
#else
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char suffix[6];
  snprintf(suffix, sizeof(suffix), "%02x%02x", mac[4], mac[5]);
  return String(BOARD_DEFAULT_HOSTNAME) + "-" + suffix;
#endif
}

constexpr const char *kNs = "cfg";          // NVS namespace
constexpr const char *kSsidSentinel = "replace-with-wifi-ssid";  // config.h default
constexpr int kTimeoutChoices[] = {5, 10, 15, 30};

bool timeoutChoice(int minutes) {
  for (int choice : kTimeoutChoices) {
    if (choice == minutes) {
      return true;
    }
  }
  return false;
}

int nextLongerTimeout(int minutes) {
  for (int choice : kTimeoutChoices) {
    if (choice > minutes) {
      return choice;
    }
  }
  return 0;
}

void normalizeDisplayTimeouts(int &dimMin, int &offMin) {
  if (!timeoutChoice(dimMin)) {
    dimMin = 5;
  }
  if (!timeoutChoice(offMin)) {
    offMin = 15;
  }
  if (dimMin < offMin) {
    return;
  }
  const int longer = nextLongerTimeout(dimMin);
  if (longer > 0) {
    offMin = longer;
    return;
  }
  dimMin = 15;
  offMin = 30;
}

void normalizeBrightness(int &fullPct, int &dimPct) {
  if (fullPct != 50 && fullPct != 75 && fullPct != 100) {
    fullPct = 100;
  }
  if (dimPct != 10 && dimPct != 20 && dimPct != 35) {
    dimPct = 20;
  }
}

void normalizeSpoolThresholds(int &lowPct, int &critPct) {
  if (lowPct != 20 && lowPct != 30 && lowPct != 40) {
    lowPct = SPOOL_LOW_PERCENT;
  }
  if (critPct != 10 && critPct != 15 && critPct != 20) {
    critPct = SPOOL_CRITICAL_PERCENT;
  }
  if (critPct >= lowPct) {
    critPct = (lowPct <= 20) ? 10 : 15;
  }
}

void normalizeIdleReturn(int &sec) {
  if (sec != 0 && sec != 60 && sec != 180) {
    sec = 60;
  }
}
}  // namespace

AppConfig &config() { return g_config; }

void AppConfig::load() {
  Preferences p;
  p.begin(kNs, /*readOnly=*/true);
  // NVS keys must be <=15 chars.
  wifiSsid = p.getString("wifiSsid", WIFI_SSID);
  wifiPassword = p.getString("wifiPass", WIFI_PASSWORD);
  mqttHost = p.getString("mqttHost", MQTT_HOST);
  mqttPort = p.getInt("mqttPort", MQTT_PORT);
  mqttUsername = p.getString("mqttUser", MQTT_USERNAME);
  mqttPassword = p.getString("mqttPass", MQTT_PASSWORD);
  topicPrefix = p.getString("topicPfx", DISPLAY_TOPIC_PREFIX);
  filamanBaseUrl = p.getString("fmUrl", FILAMAN_BASE_URL);
  filamanApiKey = p.getString("fmKey", FILAMAN_API_KEY);
  filamanPrinterId = p.getInt("fmPrinter", FILAMAN_PRINTER_ID);
  filamanTlsInsecure = p.getBool("fmTls", FILAMAN_TLS_INSECURE);
  otaHostname = p.getString("otaHost", "");
  if (!otaHostname.length()) {
    otaHostname = defaultHostname();
  }
  otaPassword = p.getString("otaPass", OTA_PASSWORD);
  displayDimMin = p.getInt("dimMin", 5);
  displayOffMin = p.getInt("offMin", 15);
  displayFullBrightPct = p.getInt("brtFull", 100);
  displayDimBrightPct = p.getInt("brtDim", 20);
  spoolLowPct = p.getInt("spLow", SPOOL_LOW_PERCENT);
  spoolCritPct = p.getInt("spCrit", SPOOL_CRITICAL_PERCENT);
  idleReturnSec = p.getInt("idleSec", 60);
  displayTimezone = p.getString("tz", DISPLAY_TIMEZONE);
  if (!displayTimezone.length()) {
    displayTimezone = DISPLAY_TIMEZONE;
  }
  autoCheckUpdates = p.getBool("autoOta", false);
#if __has_include("secrets.h")
  // Settings never saved from the UI live only as secrets.h macros. Persist
  // them so a later OTA to a public build (placeholder defaults) keeps them.
  bool seedNvs = false;
  for (const char *key : {"wifiSsid", "wifiPass", "mqttHost", "mqttPort", "mqttUser",
                          "mqttPass", "topicPfx", "fmUrl", "fmKey", "fmPrinter", "fmTls",
                          "otaHost", "otaPass", "tz"}) {
    if (!p.isKey(key)) {
      seedNvs = true;
      break;
    }
  }
#endif
  p.end();
  normalizeDisplayTimeouts(displayDimMin, displayOffMin);
  normalizeBrightness(displayFullBrightPct, displayDimBrightPct);
  normalizeSpoolThresholds(spoolLowPct, spoolCritPct);
  normalizeIdleReturn(idleReturnSec);
#if __has_include("secrets.h")
  if (seedNvs) {
    save();
  }
#endif
}

void AppConfig::save() const {
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  p.putString("wifiSsid", wifiSsid);
  p.putString("wifiPass", wifiPassword);
  p.putString("mqttHost", mqttHost);
  p.putInt("mqttPort", mqttPort);
  p.putString("mqttUser", mqttUsername);
  p.putString("mqttPass", mqttPassword);
  p.putString("topicPfx", topicPrefix);
  p.putString("fmUrl", filamanBaseUrl);
  p.putString("fmKey", filamanApiKey);
  p.putInt("fmPrinter", filamanPrinterId);
  p.putBool("fmTls", filamanTlsInsecure);
  p.putString("otaHost", otaHostname);
  p.putString("otaPass", otaPassword);
  p.putInt("dimMin", displayDimMin);
  p.putInt("offMin", displayOffMin);
  p.putInt("brtFull", displayFullBrightPct);
  p.putInt("brtDim", displayDimBrightPct);
  p.putInt("spLow", spoolLowPct);
  p.putInt("spCrit", spoolCritPct);
  p.putInt("idleSec", idleReturnSec);
  p.putString("tz", displayTimezone);
  p.putBool("autoOta", autoCheckUpdates);
  p.end();
}

void AppConfig::factoryReset() {
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  p.clear();
  p.end();
  load();
}

bool AppConfig::wifiConfigured() const {
  return wifiSsid.length() > 0 && wifiSsid != kSsidSentinel;
}

void AppConfig::setWifi(const String &ssid, const String &pass) {
  wifiSsid = ssid;
  wifiPassword = pass;
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  p.putString("wifiSsid", ssid);
  p.putString("wifiPass", pass);
  p.end();
}

void AppConfig::setFilamanPrinterId(int id) {
  if (id <= 0 || id == filamanPrinterId) {
    return;
  }
  filamanPrinterId = id;
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  p.putInt("fmPrinter", id);
  p.end();
}

void AppConfig::setDisplayTimeouts(int dimMin, int offMin) {
  normalizeDisplayTimeouts(dimMin, offMin);
  if (dimMin == displayDimMin && offMin == displayOffMin) {
    return;
  }
  displayDimMin = dimMin;
  displayOffMin = offMin;
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  p.putInt("dimMin", displayDimMin);
  p.putInt("offMin", displayOffMin);
  p.end();
}

void AppConfig::setDisplayBrightness(int fullPct, int dimPct) {
  normalizeBrightness(fullPct, dimPct);
  if (fullPct == displayFullBrightPct && dimPct == displayDimBrightPct) {
    return;
  }
  displayFullBrightPct = fullPct;
  displayDimBrightPct = dimPct;
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  p.putInt("brtFull", displayFullBrightPct);
  p.putInt("brtDim", displayDimBrightPct);
  p.end();
}

void AppConfig::setSpoolThresholds(int lowPct, int critPct) {
  normalizeSpoolThresholds(lowPct, critPct);
  if (lowPct == spoolLowPct && critPct == spoolCritPct) {
    return;
  }
  spoolLowPct = lowPct;
  spoolCritPct = critPct;
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  p.putInt("spLow", spoolLowPct);
  p.putInt("spCrit", spoolCritPct);
  p.end();
}

void AppConfig::setIdleReturnSec(int sec) {
  normalizeIdleReturn(sec);
  if (sec == idleReturnSec) {
    return;
  }
  idleReturnSec = sec;
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  p.putInt("idleSec", idleReturnSec);
  p.end();
}

void AppConfig::setFilamanBaseUrl(const String &url) {
  if (!url.length() || url == filamanBaseUrl) {
    return;
  }
  filamanBaseUrl = url;
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  p.putString("fmUrl", filamanBaseUrl);
  p.end();
}

void AppConfig::setAutoCheckUpdates(bool enabled) {
  if (enabled == autoCheckUpdates) {
    return;
  }
  autoCheckUpdates = enabled;
  Preferences p;
  p.begin(kNs, /*readOnly=*/false);
  p.putBool("autoOta", autoCheckUpdates);
  p.end();
}
