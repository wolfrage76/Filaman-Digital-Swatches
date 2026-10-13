#pragma once

#include <Arduino.h>

#include "config.h"

// Runtime configuration, persisted in NVS (Preferences namespace "cfg"). Each
// field defaults to its compile-time macro (config.h / secrets.h) on first boot,
// so an unprovisioned device behaves exactly as before. Once saved from the
// on-device WiFi setup screen or the web config page, NVS wins and no reflash /
// secrets.h edit is needed to change settings.
struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  String mqttHost;
  int mqttPort = MQTT_PORT;
  String mqttUsername;
  String mqttPassword;
  String topicPrefix;
  String filamanBaseUrl;
  String filamanApiKey;
  int filamanPrinterId = FILAMAN_PRINTER_ID;
  bool filamanTlsInsecure = FILAMAN_TLS_INSECURE;
  String otaHostname;
  String otaPassword;
  int displayDimMin = 5;
  int displayOffMin = 15;
  int displayFullBrightPct = 100;   // 50, 75, 100
  int displayDimBrightPct = 20;     // 10, 20, 35
  int spoolLowPct = SPOOL_LOW_PERCENT;        // 20, 30, 40
  int spoolCritPct = SPOOL_CRITICAL_PERCENT;  // 10, 15, 20
  int idleReturnSec = 60;           // 0 (off), 60, 180
  String displayTimezone;

  void load();               // read all fields from NVS (macro = default)
  void save() const;         // persist all fields to NVS
  void factoryReset();       // clear NVS namespace "cfg" and reload compile-time defaults
  bool wifiConfigured() const;  // true once a real SSID is set (not the sentinel)
  void setWifi(const String &ssid, const String &pass);  // set + persist just WiFi
  void setFilamanPrinterId(int id);  // remember the on-screen printer choice
  void setFilamanBaseUrl(const String &url);  // persist a URL that actually answered
  void setDisplayTimeouts(int dimMin, int offMin);  // minutes; dim must be shorter than off
  void setDisplayBrightness(int fullPct, int dimPct);
  void setSpoolThresholds(int lowPct, int critPct);
  void setIdleReturnSec(int sec);
};

// Process-wide config singleton (house style, like `displayMqtt`).
AppConfig &config();
