#include "app_config.h"

#include <Preferences.h>

namespace {
AppConfig g_config;
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
  otaHostname = p.getString("otaHost", OTA_HOSTNAME);
  otaPassword = p.getString("otaPass", OTA_PASSWORD);
  displayDimMin = p.getInt("dimMin", 5);
  displayOffMin = p.getInt("offMin", 15);
  displayTimezone = p.getString("tz", DISPLAY_TIMEZONE);
  if (!displayTimezone.length()) {
    displayTimezone = DISPLAY_TIMEZONE;
  }
  p.end();
  normalizeDisplayTimeouts(displayDimMin, displayOffMin);
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
  p.putString("tz", displayTimezone);
  p.end();
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
