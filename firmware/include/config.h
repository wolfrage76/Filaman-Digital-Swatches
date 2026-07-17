#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "replace-with-wifi-ssid"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "replace-with-wifi-password"
#endif

#ifndef MQTT_HOST
#define MQTT_HOST "192.168.1.10"
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

#ifndef MQTT_USERNAME
#define MQTT_USERNAME ""
#endif

#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif

#ifndef DISPLAY_TOPIC_PREFIX
#define DISPLAY_TOPIC_PREFIX "display/h2c"
#endif

constexpr const char *FIRMWARE_VERSION = "0.8.0";

#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "crowpanel-h2c"
#endif

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif


constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 480;

constexpr int AMS2PRO_COUNT = 4;
constexpr int AMSHT_MAX_COUNT = 4;
constexpr int COLUMNS_PER_PAGE = 4;

// Page 0 = printer/job status (idle home). Pages 1-4 = AMS 2 Pro. Page 5 = AMS-HT.
constexpr int STATUS_PAGE = 0;
constexpr int AMS2PRO_FIRST_PAGE = 1;
constexpr int AMS_HT_PAGE = AMS2PRO_FIRST_PAGE + AMS2PRO_COUNT;
constexpr int TOTAL_PAGE_COUNT = AMS_HT_PAGE + 1;

constexpr int AMS_HT_FIRST_ID = AMS2PRO_COUNT;

constexpr int MQTT_BUFFER_SIZE = 4096;
constexpr unsigned long WIFI_RECONNECT_MS = 20000;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 25000;
constexpr unsigned long MQTT_RECONNECT_MS = 5000;
constexpr unsigned long IDLE_RETURN_MS = 60000;
constexpr unsigned long LVGL_TICK_MS = 5;
