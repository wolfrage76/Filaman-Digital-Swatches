#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <lvgl.h>

#include "config.h"
#include "mqtt_client.h"
#include "ota_update.h"
#include "touch_input.h"
#include "ui/ui.h"

namespace {

constexpr int BACKLIGHT_PIN = 2;
constexpr int PANEL_ENABLE_PIN = 38;

// 12 MHz is more stable on CrowPanel 7" RGB panels than 16 MHz.
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    41 /* DE */, 40 /* VSYNC */, 39 /* HSYNC */, 0 /* PCLK */,
    14 /* R0 */, 21 /* R1 */, 47 /* R2 */, 48 /* R3 */, 45 /* R4 */,
    9 /* G0 */, 46 /* G1 */, 3 /* G2 */, 8 /* G3 */, 16 /* G4 */, 1 /* G5 */,
    15 /* B0 */, 7 /* B1 */, 6 /* B2 */, 5 /* B3 */, 4 /* B4 */,
    0 /* hsync_polarity */, 40 /* hsync_front_porch */, 48 /* hsync_pulse_width */, 40 /* hsync_back_porch */,
    0 /* vsync_polarity */, 1 /* vsync_front_porch */, 31 /* vsync_pulse_width */, 13 /* vsync_back_porch */,
    1 /* pclk_active_neg */, 12000000 /* prefer_speed */);

// auto_flush=false so gfx->flush() can Cache_WriteBack the RGB framebuffer after LVGL draws.
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(SCREEN_WIDTH, SCREEN_HEIGHT, bus, 0 /* rotation */, false /* auto_flush */);

lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
lv_indev_drv_t inputDriver;

unsigned long lastWifiAttempt = 0;
unsigned long wifiBeginAt = 0;
wl_status_t lastWifiStatus = WL_IDLE_STATUS;
String lastStatusText;
int lastDisconnectReason = -1;
int wifiRetryCount = 0;
bool wifiScanDone = false;

void setStatus(const String &message) {
  if (message == lastStatusText) {
    return;
  }
  lastStatusText = message;
  ui_set_connection_status(message);
  Serial.println(message);
}

// Direct-mode flush: LVGL draws into the RGB panel framebuffer in place.
void flushDisplay(lv_disp_drv_t *disp, const lv_area_t *, lv_color_t *) {
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

  if (pressed) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void setupDisplay() {
  pinMode(PANEL_ENABLE_PIN, OUTPUT);
  digitalWrite(PANEL_ENABLE_PIN, LOW);

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed");
  }
  gfx->fillScreen(BLACK);

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);
}

void setupTouch() {
  touch_input_begin(SCREEN_WIDTH, SCREEN_HEIGHT);
}

void setupLvgl() {
  lv_init();

  lv_color_t *frameBuffer = reinterpret_cast<lv_color_t *>(gfx->getFramebuffer());
  if (!frameBuffer) {
    Serial.println("RGB framebuffer missing");
    return;
  }

  const uint32_t bufSize = SCREEN_WIDTH * SCREEN_HEIGHT;
  lv_disp_draw_buf_init(&drawBuffer, frameBuffer, nullptr, bufSize);

  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = SCREEN_WIDTH;
  displayDriver.ver_res = SCREEN_HEIGHT;
  displayDriver.flush_cb = flushDisplay;
  displayDriver.draw_buf = &drawBuffer;
  displayDriver.direct_mode = true;
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
  // ESP-IDF wifi_err_reason_t common values
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
    Serial.printf("WiFi disconnect reason=%d\n", lastDisconnectReason);
  } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    lastDisconnectReason = -1;
    const String ip = WiFi.localIP().toString();
    Serial.printf("WiFi got IP %s\n", ip.c_str());
    ui_set_ip_address(ip);
    ota_begin();
  }
}

bool scanForConfiguredSsid() {
  setStatus(String("WiFi scan ") + WIFI_SSID);
  const int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  bool found = false;
  int rssi = 0;
  for (int i = 0; i < n; ++i) {
    if (WiFi.SSID(i) == WIFI_SSID) {
      found = true;
      rssi = WiFi.RSSI(i);
      Serial.printf("Found SSID '%s' ch=%d RSSI=%d enc=%d\n",
                    WIFI_SSID, WiFi.channel(i), rssi, WiFi.encryptionType(i));
      break;
    }
  }
  WiFi.scanDelete();
  if (!found) {
    setStatus("WiFi: SSID not in scan (2.4GHz?)");
    Serial.println("Configured SSID not found in scan");
  } else {
    setStatus(String("WiFi found ") + String(rssi) + "dBm");
  }
  return found;
}

void beginWifiJoin() {
  wifiBeginAt = millis();
  lastWifiAttempt = wifiBeginAt;
  setStatus(String("WiFi joining ") + WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiStatus = WiFi.status();
}

void startWifi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.onEvent(onWifiEvent);

  wifiScanDone = scanForConfiguredSsid();
  beginWifiJoin();
}

void maintainWifi() {
  const wl_status_t status = WiFi.status();
  const unsigned long now = millis();

  if (status == WL_CONNECTED) {
    if (lastWifiStatus != WL_CONNECTED) {
      lastWifiStatus = status;
      const String ip = WiFi.localIP().toString();
      ui_set_ip_address(ip);
      setStatus(String("WiFi ") + ip);
      ota_begin();
    }
    return;
  }

  ui_set_ip_address("");

  // While joining, show progress instead of immediately "retry".
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
      setStatus(String("WiFi joining ") + WIFI_SSID);
    } else {
      setStatus(wifiStatusText(status));
    }
  }

  if (now - wifiBeginAt < WIFI_CONNECT_TIMEOUT_MS) {
    return;  // still giving the current attempt time
  }

  if (now - lastWifiAttempt < WIFI_RECONNECT_MS) {
    return;
  }

  // Re-scan on first few retries so we can tell "wrong band" vs "wrong password".
  if (wifiRetryCount < 3 || (wifiRetryCount % 5) == 0) {
    wifiScanDone = scanForConfiguredSsid();
  }
  wifiRetryCount++;

  Serial.println("WiFi reconnect attempt");
  WiFi.disconnect(true, true);
  delay(200);
  beginWifiJoin();
  setStatus(String("WiFi retry ") + WIFI_SSID);
}

void setupCallbacks() {
  displayMqtt.setSlotCallback(ui_update_slot);
  displayMqtt.setPrinterCallback(ui_update_printer);
  displayMqtt.setMetaCallback(ui_update_meta);
  displayMqtt.setStatusCallback([](const String &message) {
    // Prefer WiFi detail until MQTT is actually useful.
    if (WiFi.status() == WL_CONNECTED || message.startsWith("MQTT")) {
      setStatus(message);
    }
  });
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("CrowPanel H2C AMS monitor starting");

  setupDisplay();
  setupTouch();
  setupLvgl();
  ui_begin();
  lv_obj_invalidate(lv_scr_act());
  for (int i = 0; i < 30; ++i) {
    lv_timer_handler();
    gfx->flush();
    delay(5);
  }

  setupCallbacks();
  displayMqtt.begin();
  startWifi();
}

void loop() {
  maintainWifi();
  if (WiFi.status() == WL_CONNECTED) {
    ota_loop();
    if (!ota_is_active()) {
      displayMqtt.loop();
    }
  }

  if (!ota_is_active()) {
    lv_timer_handler();
    ui_loop();
    gfx->flush();
  }
  delay(5);
}
