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
#define MQTT_HOST ""
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
#define DISPLAY_TOPIC_PREFIX "display/amsview"
#endif

// --- FilaMan Display API ---
#ifndef FILAMAN_BASE_URL
#define FILAMAN_BASE_URL ""  // origin, e.g. http://host:8083 or https://host
#endif
#ifndef FILAMAN_API_KEY
#define FILAMAN_API_KEY ""  // "uak.1.<secret>" — create once in the FilaMan UI
#endif
// FilaMan printer ID to monitor. 0 = auto-detect the first printer via GET /api/v1/printers.
#ifndef FILAMAN_PRINTER_ID
#define FILAMAN_PRINTER_ID 0
#endif
// Skip TLS cert validation for a LAN https FilaMan (ignored for http URLs).
#ifndef FILAMAN_TLS_INSECURE
#define FILAMAN_TLS_INSECURE 1
#endif

// Public project pages, shown in the web console footer.
#define PROJECT_GITHUB_REPO "wolfrage76/Filaman-Digital-Swatches"
#define PROJECT_GITHUB_URL "https://github.com/" PROJECT_GITHUB_REPO
#define PROJECT_MAKERWORLD_URL "https://makerworld.com/en/@Wofl"

constexpr const char *FIRMWARE_VERSION = "0.22.0";
constexpr int MQTT_SCHEMA_VERSION = 2;

// OTA_HOSTNAME (secrets.h, optional) replaces the generated default hostname,
// <BOARD_DEFAULT_HOSTNAME>-<last 4 hex of the WiFi MAC>.

// POSIX TZ for the finish-time clock. Override in secrets.h. UTC0 if unset.
#ifndef DISPLAY_TIMEZONE
#define DISPLAY_TIMEZONE "UTC0"
#endif

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 480;

constexpr int AMS2PRO_COUNT = 4;
constexpr int AMSHT_MAX_COUNT = 4;
constexpr int COLUMNS_PER_PAGE = 4;

// Page 0 = printer. Page 1 = AMS overview. Pages 2-5 = AMS A-D. Page 6 = HT. Page 7 = alerts.
constexpr int STATUS_PAGE = 0;
constexpr int AMS_OVERVIEW_PAGE = 1;
constexpr int AMS2PRO_FIRST_PAGE = 2;
constexpr int AMS_HT_PAGE = AMS2PRO_FIRST_PAGE + AMS2PRO_COUNT;
constexpr int ALERTS_PAGE = AMS_HT_PAGE + 1;
constexpr int TOTAL_PAGE_COUNT = ALERTS_PAGE + 1;

constexpr int AMS_HT_FIRST_ID = AMS2PRO_COUNT;

constexpr int MQTT_BUFFER_SIZE = 8192;
constexpr unsigned long WIFI_RECONNECT_MS = 20000;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 25000;
constexpr unsigned long MQTT_RECONNECT_MS = 5000;
constexpr unsigned long IDLE_RETURN_MS = 60000;
constexpr unsigned long LVGL_TICK_MS = 5;
constexpr unsigned long FRESH_LIVE_MS = 15000;
constexpr unsigned long FRESH_STALE_MS = 90000;
constexpr int SPOOL_LOW_PERCENT = 30;
constexpr int SPOOL_CRITICAL_PERCENT = 15;
