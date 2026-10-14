#include <Arduino.h>
#include <time.h>
#include <cstring>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <lvgl.h>

#include "app_config.h"
#include "backlight.h"
#include "boards/board.h"
#include "config.h"
#include "device_log.h"
#include "display_capture.h"
#include "display_poll.h"
#include "filaman_client.h"
#include "hardware/panel.h"
#include "mqtt_client.h"
#include "ota_update.h"
#include "services/board_image.h"
#include "printer_list.h"
#include "touch_input.h"
#include "ui/menu_ui.h"
#include "ui/printer_select.h"
#include "ui/ui.h"
#include "ui/wifi_setup.h"
#include "web_portal.h"

SET_LOOP_TASK_STACK_SIZE(24 * 1024);

// Survives soft resets, so a reboot loop is visible in the boot counter.
RTC_NOINIT_ATTR uint32_t bootMagic;
RTC_NOINIT_ATTR uint32_t bootCount;

#ifndef DISPLAY_USE_BOUNCE_BUFFER
#define DISPLAY_USE_BOUNCE_BUFFER 0
#endif

namespace {

constexpr uint32_t BOOT_MAGIC_VALUE = 0x43503748;
constexpr unsigned long DIAG_INTERVAL_MS = 5000;

esp_reset_reason_t bootReason = ESP_RST_UNKNOWN;
unsigned long lastDiagMs = 0;
unsigned long lastLoopStartMs = 0;
unsigned long mqttQuietUntilMs = 0;
bool mqttClientStarted = false;
uint32_t maxLoopUs = 0;
uint32_t maxLvglUs = 0;
uint32_t maxNetUs = 0;

// Offscreen draw buffers in internal SRAM (24 lines = ~38 KB per buffer).
constexpr int LVGL_BUF_LINES = 24;

lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
lv_indev_drv_t inputDriver;
lv_color_t *lvglBuf1 = nullptr;
lv_color_t *lvglBuf2 = nullptr;

unsigned long lastWifiAttempt = 0;
unsigned long wifiBeginAt = 0;
wl_status_t lastWifiStatus = WL_IDLE_STATUS;
String lastStatusText;
int lastDisconnectReason = -1;
int wifiRetryCount = 0;

void setStatus(const String &message) {
  if (message == lastStatusText) {
    return;
  }
  lastStatusText = message;
  ui_set_connection_status(message);
  device_log(message);
}

void pumpDisplay() {
  if (ota_is_active()) {
    return;
  }
  lv_timer_handler();
}

// Partial mode: LVGL renders offscreen, so the panel never scans out a
// half-drawn frame (background-first paint was the visible "black" flash).
void flushDisplay(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  panelBlitStrip(area->x1, area->y1, w, h, reinterpret_cast<const uint16_t *>(&color_p->full));
#if !DISPLAY_USE_BOUNCE_BUFFER
  // Direct-PSRAM mode only: hand the PSRAM bus back to scanout after each strip.
  yield();
  delayMicroseconds(20 + static_cast<uint32_t>(h) * 12);
#endif
  lv_disp_flush_ready(disp);
}

void readLvglTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
  int16_t x = 0;
  int16_t y = 0;
  bool pressed = false;
  if (!touch_input_read(x, y, pressed)) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  if (!backlight_filter_touch(pressed)) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  if (pressed) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

lv_color_t *allocDrawBuffer(size_t pixels) {
  lv_color_t *buf = static_cast<lv_color_t *>(
      heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<lv_color_t *>(
        heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  return buf;
}

void setupLvgl() {
  lv_init();

  if (!panelFramebuffer()) {
    device_log("RGB framebuffer missing");
    return;
  }

  const size_t bufPixels = static_cast<size_t>(SCREEN_WIDTH) * LVGL_BUF_LINES;
  lvglBuf1 = allocDrawBuffer(bufPixels);
  lvglBuf2 = allocDrawBuffer(bufPixels);
  if (!lvglBuf1) {
    device_log("LVGL draw buffer alloc failed");
    return;
  }
  lv_disp_draw_buf_init(&drawBuffer, lvglBuf1, lvglBuf2, bufPixels);

  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = SCREEN_WIDTH;
  displayDriver.ver_res = SCREEN_HEIGHT;
  displayDriver.flush_cb = flushDisplay;
  displayDriver.draw_buf = &drawBuffer;
  displayDriver.direct_mode = false;
  displayDriver.full_refresh = 0;
  lv_disp_drv_register(&displayDriver);

  lv_indev_drv_init(&inputDriver);
  inputDriver.type = LV_INDEV_TYPE_POINTER;
  inputDriver.read_cb = readLvglTouch;
  lv_indev_drv_register(&inputDriver);
}

const char *wifiStatusText(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "WiFi idle";
    case WL_NO_SSID_AVAIL:
      return "WiFi: SSID not found (need 2.4GHz)";
    case WL_SCAN_COMPLETED:
      return "WiFi scanning";
    case WL_CONNECTED:
      return "WiFi connected";
    case WL_CONNECT_FAILED:
      return "WiFi: auth failed (check password)";
    case WL_CONNECTION_LOST:
      return "WiFi lost";
    case WL_DISCONNECTED:
      return "WiFi disconnected";
    default:
      return "WiFi error";
  }
}

const char *disconnectReasonText(int reason) {
  switch (reason) {
    case 2:
      return "auth expire";
    case 15:
      return "4way timeout";
    case 201:
      return "no AP found";
    case 202:
      return "auth fail";
    case 203:
      return "assoc fail";
    case 204:
      return "handshake fail";
    case 205:
      return "conn fail";
    default:
      return nullptr;
  }
}

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastDisconnectReason = info.wifi_sta_disconnected.reason;
    device_logf("WiFi disconnect reason=%d", lastDisconnectReason);
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    lastDisconnectReason = -1;
    const String ip = WiFi.localIP().toString();
    device_logf("WiFi got IP %s", ip.c_str());
    ui_set_ip_address(ip);
    const char *tz =
        config().displayTimezone.length() ? config().displayTimezone.c_str() : "UTC0";
    configTzTime(tz, "pool.ntp.org", "time.nist.gov");
    ota_begin();
    web_portal_begin();
  }
}

void delayWithDisplayPump(unsigned long ms) {
  const unsigned long start = millis();
  while (millis() - start < ms) {
    pumpDisplay();
    delay(2);
  }
}

void beginWifiJoin() {
  wifiBeginAt = millis();
  lastWifiAttempt = wifiBeginAt;
  setStatus(String("WiFi joining ") + config().wifiSsid);
  WiFi.begin(config().wifiSsid.c_str(), config().wifiPassword.c_str());
  lastWifiStatus = WiFi.status();
}

void startWifi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_MIN_MODEM);
  WiFi.setHostname(config().otaHostname.c_str());
  WiFi.onEvent(onWifiEvent);
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  if (!config().wifiConfigured()) {
    setStatus("Set up WiFi on screen");
    wifi_setup_open();
    return;
  }
  beginWifiJoin();
}

void maintainWifi() {
  const wl_status_t status = WiFi.status();
  const unsigned long now = millis();

  if (status == WL_CONNECTED) {
    wifiRetryCount = 0;
    if (lastWifiStatus != WL_CONNECTED) {
      lastWifiStatus = status;
      const String ip = WiFi.localIP().toString();
      ui_set_ip_address(ip);
      setStatus(String("WiFi ") + ip);
      ota_begin();
      web_portal_begin();
    }
    return;
  }

  ui_set_ip_address("");

  if (status != lastWifiStatus) {
    lastWifiStatus = status;
    if (status == WL_NO_SSID_AVAIL) {
      setStatus("WiFi: SSID not found (need 2.4GHz)");
    } else if (status == WL_CONNECT_FAILED) {
      setStatus("WiFi: auth failed (check password)");
    } else if (status == WL_DISCONNECTED && lastDisconnectReason >= 0) {
      const char *why = disconnectReasonText(lastDisconnectReason);
      if (why) {
        setStatus(String("WiFi fail: ") + why);
      } else {
        setStatus(String("WiFi fail rc=") + String(lastDisconnectReason));
      }
    } else if (now - wifiBeginAt < WIFI_CONNECT_TIMEOUT_MS) {
      setStatus(String("WiFi joining ") + config().wifiSsid);
    } else {
      setStatus(wifiStatusText(status));
    }
  }

  if (now - wifiBeginAt < WIFI_CONNECT_TIMEOUT_MS) {
    return;
  }

  if (now - lastWifiAttempt < WIFI_RECONNECT_MS) {
    return;
  }

  wifiRetryCount++;
  device_log("WiFi reconnect attempt");
  WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
  delayWithDisplayPump(50);
  beginWifiJoin();
  setStatus(String("WiFi retry ") + config().wifiSsid);
}

const char *resetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "poweron";
    case ESP_RST_EXT:
      return "ext";
    case ESP_RST_SW:
      return "sw";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "int_wdt";
    case ESP_RST_TASK_WDT:
      return "task_wdt";
    case ESP_RST_WDT:
      return "wdt";
    case ESP_RST_DEEPSLEEP:
      return "deepsleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
  }
}

void publishDiagnostics() {
  String body = "{\"v\":\"";
  body += FIRMWARE_VERSION;
  body += "\",\"board\":\"";
  body += BOARD_ID;
  body += "\",\"up\":" + String(millis());
  body += ",\"boot\":" + String(bootCount);
  body += ",\"rst\":\"" + String(resetReasonText(bootReason)) + "\"";
  body += ",\"heap\":" + String(static_cast<uint32_t>(ESP.getFreeHeap()));
  body += ",\"minheap\":" + String(static_cast<uint32_t>(ESP.getMinFreeHeap()));
  body += ",\"psram\":" + String(static_cast<uint32_t>(ESP.getFreePsram()));
  body += ",\"bounce\":";
#if DISPLAY_USE_BOUNCE_BUFFER
  body += "1";
#else
  body += "0";
#endif
  body += ",\"rssi\":" + String(WiFi.RSSI());
  body += ",\"loop_us\":" + String(maxLoopUs);
  body += ",\"lvgl_us\":" + String(maxLvglUs);
  body += ",\"net_us\":" + String(maxNetUs);
  body += "}";
  if (displayMqtt.publishDiag(body)) {
    maxLoopUs = 0;
    maxLvglUs = 0;
    maxNetUs = 0;
  } else if (display_poll_active()) {
    maxLoopUs = 0;
    maxLvglUs = 0;
    maxNetUs = 0;
  }
  Serial.println(body);
}

void setupCallbacks() {
  displayMqtt.setSlotCallback(ui_update_slot);
  displayMqtt.setPrinterCallback(ui_update_printer);
  displayMqtt.setMetaCallback(ui_update_meta);
  displayMqtt.setBridgeCallback(ui_update_bridge);
  displayMqtt.setAlertsCallback(ui_update_alerts);
  displayMqtt.setOverviewCallback(ui_update_overview);
  displayMqtt.setPrintersCallback(
      [](const PrinterOption *printers, int count, const String &activeId) {
        printer_list_set(printers, count, activeId);
      });
  displayMqtt.setStatusCallback([](const String &message) {
    if (WiFi.status() == WL_CONNECTED || message.startsWith("MQTT")) {
      setStatus(message);
    }
  });
}

}  // namespace

const uint16_t *display_framebuffer_rgb565() { return panelFramebuffer(); }

void setup() {
  Serial.begin(115200);
  delay(200);
  bootReason = esp_reset_reason();
  if (bootMagic != BOOT_MAGIC_VALUE || bootReason == ESP_RST_POWERON) {
    bootMagic = BOOT_MAGIC_VALUE;
    bootCount = 0;
  }
  bootCount++;
  device_logf("%s AMS monitor starting (boot #%u, reset=%s)", BOARD_MODEL, bootCount,
              resetReasonText(bootReason));
  device_log(boardImageMarker());

  config().load();
  const char *tz = config().displayTimezone.length() ? config().displayTimezone.c_str() : "UTC0";
  setenv("TZ", tz, 1);
  tzset();

  boardEarlyInit();
  if (!panelBegin()) {
    device_log("panelBegin() failed");
  }
  backlight_begin();
  setupLvgl();
  ui_begin();
  wifi_setup_begin();
  printer_select_begin();
  menu_begin();
  for (int i = 0; i < 40; ++i) {
    lv_timer_handler();
    delay(5);
  }

  setupCallbacks();
  filaman.begin(config().filamanBaseUrl, config().filamanApiKey, config().filamanTlsInsecure);
  if (filaman.configured()) {
    display_poll_begin();
    setStatus("FilaMan display mode");
  } else {
    printer_list_set_publisher([](const String &id) { displayMqtt.publishMonitoredPrinter(id); });
  }
  startWifi();
}

void loop() {
  const uint32_t loopStart = micros();
  const unsigned long nowMs = millis();
  if (lastLoopStartMs != 0) {
    const uint32_t gapUs = static_cast<uint32_t>(nowMs - lastLoopStartMs) * 1000U;
    if (gapUs > maxLoopUs) {
      maxLoopUs = gapUs;
    }
  }
  lastLoopStartMs = nowMs;

  if (!ota_is_active()) {
    lv_timer_handler();
    backlight_loop();
    if (nowMs >= mqttQuietUntilMs) {
      ui_loop();
    }
  }
  const uint32_t afterLvgl = micros();
  if (afterLvgl - loopStart > maxLvglUs) {
    maxLvglUs = afterLvgl - loopStart;
  }

  maintainWifi();
  if (WiFi.status() == WL_CONNECTED) {
    ota_loop();
    ota_auto_check_loop();
    web_portal_loop();
    if (!ota_is_active()) {
      bool polled = false;
      if (display_poll_active() && nowMs >= mqttQuietUntilMs) {
        display_poll_loop();
        polled = true;
      }
      // MQTT only when FilaMan is unset, or the Display API has failed twice.
      // A successful poll in this same turn clears that fallback.
      const bool useMqtt = !display_poll_active() || display_poll_needs_mqtt();
      if (useMqtt && nowMs >= mqttQuietUntilMs) {
        if (!mqttClientStarted) {
          displayMqtt.begin();
          mqttClientStarted = true;
        }
        displayMqtt.loop();
        if (displayMqtt.tookWork()) {
          mqttQuietUntilMs = nowMs + 350;
        } else if (polled) {
          mqttQuietUntilMs = nowMs + 50;
        }
      } else if (polled) {
        mqttQuietUntilMs = nowMs + 50;
      }
      lv_timer_handler();
    }
  }
  const uint32_t afterNet = micros();
  if (afterNet - afterLvgl > maxNetUs) {
    maxNetUs = afterNet - afterLvgl;
  }

  if (nowMs - lastDiagMs >= DIAG_INTERVAL_MS) {
    lastDiagMs = nowMs;
    publishDiagnostics();
  }

  delay(1);
}
