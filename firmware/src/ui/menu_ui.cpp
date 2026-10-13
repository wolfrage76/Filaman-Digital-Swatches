#include "menu_ui.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "app_config.h"
#include "backlight.h"
#include "config.h"
#include "display_poll.h"
#include "filaman_client.h"
#include "ota_update.h"
#include "printer_select.h"
#include "touch_input.h"
#include "ui.h"
#include "wifi_setup.h"

// Built once and hidden, like the WiFi overlay. Page buttons and form fields
// are created only while the menu is open, then thrown away, so boot does not
// grow the widget tree.

namespace {

constexpr uint32_t COL_BG = 0x0A0E14;
constexpr uint32_t COL_SURFACE_2 = 0x182028;
constexpr uint32_t COL_BORDER = 0x2A3544;
constexpr uint32_t COL_TEXT = 0xF2F5F8;
constexpr uint32_t COL_MUTED = 0x8B98A8;
constexpr uint32_t COL_ACCENT = 0x3DDC97;
constexpr uint32_t COL_WARN = 0xFFB020;
constexpr uint32_t COL_DANGER = 0xFF5C6A;

enum class Pane { Home, Filaman, Mqtt, Device, Display, Update, System };

lv_obj_t *overlay = nullptr;
lv_obj_t *titleLabel = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *fields[5] = {};
lv_obj_t *kb = nullptr;
lv_obj_t *tlsLabel = nullptr;
lv_obj_t *updateProgressBar = nullptr;
lv_obj_t *autoCheckLabel = nullptr;
int fieldCount = 0;
bool tlsOn = false;
bool confirmResetArmed = false;
Pane pane = Pane::Home;

String latestGithubVer;
String latestGithubUrl;

void syncLatestFromOta() {
  const String ver = ota_cached_latest_version();
  if (ver.length()) {
    latestGithubVer = ver;
    latestGithubUrl = ota_cached_download_url();
  }
}

bool updateBadgeVisible() {
  return config().autoCheckUpdates && ota_update_available();
}

void showHome();
void showFilaman();
void showMqtt();
void showDevice();
void showDisplay();
void showUpdate();
void showSystem();
void onMenuScrollEvent(lv_event_t *event);

void setStatus(const String &text, uint32_t color) {
  if (!statusLabel) {
    return;
  }
  lv_label_set_text(statusLabel, text.c_str());
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(color), 0);
  if (text.length() == 0) {
    lv_obj_add_flag(statusLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(statusLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

void clearContent() {
  touch_input_cooldown(260);
  if (lv_indev_t *indev = lv_indev_get_act()) {
    lv_indev_wait_release(indev);
  }
  if (content) {
    lv_obj_clean(content);
  }
  for (int i = 0; i < 5; ++i) {
    fields[i] = nullptr;
  }
  fieldCount = 0;
  kb = nullptr;
  tlsLabel = nullptr;
  updateProgressBar = nullptr;
  autoCheckLabel = nullptr;
}

void onMenuScrollEvent(lv_event_t *event) {
  lv_obj_t *col = lv_event_get_target(event);
  if (!col) {
    return;
  }
  if (lv_event_get_code(event) == LV_EVENT_SCROLL_BEGIN) {
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
  } else {
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_invalidate(col);
  }
}

lv_obj_t *makeScroll() {
  lv_obj_t *col = lv_obj_create(content);
  lv_obj_remove_style_all(col);
  lv_obj_set_width(col, lv_pct(100));
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_style_min_height(col, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 10, 0);
  lv_obj_set_style_pad_bottom(col, 16, 0);
  lv_obj_set_scroll_dir(col, LV_DIR_VER);
  // No scrollbar overlay while scrolling: it is a masked rounded rect
  // repainted every scroll frame. Shown again once the list settles.
  lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(col, LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_add_event_cb(col, onMenuScrollEvent, LV_EVENT_SCROLL_BEGIN, nullptr);
  lv_obj_add_event_cb(col, onMenuScrollEvent, LV_EVENT_SCROLL_END, nullptr);
  return col;
}

lv_obj_t *makeCard(lv_obj_t *parent) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_width(card, lv_pct(100));
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_bg_color(card, lv_color_hex(COL_SURFACE_2), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(card, 0, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_style_pad_row(card, 8, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  return card;
}

void styleButton(lv_obj_t *btn, bool accent) {
  lv_obj_set_style_min_height(btn, 46, 0);
  lv_obj_set_style_radius(btn, 0, 0);
  lv_obj_set_style_border_width(btn, accent ? 0 : 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(accent ? COL_ACCENT : COL_SURFACE_2), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
}

lv_obj_t *makeAction(lv_obj_t *parent, const char *text, bool accent, lv_event_cb_t cb, void *ud) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, lv_pct(100));
  styleButton(btn, accent);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(accent ? COL_BG : COL_TEXT), 0);
  lv_label_set_text(lbl, text);
  lv_obj_center(lbl);
  if (cb) {
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
  }
  return btn;
}

void addSection(lv_obj_t *parent, const char *text) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(lbl, text);
}

lv_obj_t *makeMenuRow(lv_obj_t *parent, lv_coord_t height) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, lv_pct(100));
  if (height > 0) {
    lv_obj_set_height(row, height);
  } else {
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(row, 1);
  }
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 10, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  return row;
}

// Home-menu destination tile: monogram + label. Use ASCII letters only for the
// mark — many LV_SYMBOL_* glyphs are missing from Montserrat and render as
// empty boxes on device.
lv_obj_t *makeMenuTile(lv_obj_t *parent, const char *mark, const char *label, bool emphasize,
                       bool badge, lv_event_cb_t cb, void *ud) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_flex_grow(btn, 1);
  lv_obj_set_width(btn, 0);
  lv_obj_set_height(btn, lv_pct(100));
  lv_obj_set_style_radius(btn, 14, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(emphasize ? 0x15261F : COL_SURFACE_2), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(emphasize ? 0x1C3228 : 0x1E2A34), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn, emphasize ? 2 : 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(emphasize ? COL_ACCENT : COL_BORDER), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 10, 0);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(btn, 8, 0);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

  if (badge) {
    lv_obj_t *dot = lv_obj_create(btn);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -6, 6);
  }

  lv_obj_t *chip = lv_obj_create(btn);
  lv_obj_remove_style_all(chip);
  lv_obj_set_size(chip, 36, 36);
  lv_obj_set_style_radius(chip, 10, 0);
  lv_obj_set_style_bg_color(chip, lv_color_hex(emphasize ? COL_ACCENT : 0x243040), 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *icon = lv_label_create(chip);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(icon, lv_color_hex(emphasize ? COL_BG : COL_ACCENT), 0);
  lv_label_set_text(icon, mark);
  lv_obj_center(icon);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lbl, lv_pct(100));
  lv_label_set_text(lbl, label);

  if (cb) {
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
  }
  return btn;
}

void addNavRow(lv_obj_t *parent, const char *text, bool last, lv_event_cb_t cb, void *ud,
               bool badge = false) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, lv_pct(100));
  lv_obj_set_height(btn, 48);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_radius(btn, 0, 0);
  lv_obj_set_style_pad_hor(btn, 8, 0);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(btn, 10, 0);
  if (!last) {
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_radius(btn, 0, 0);
  }
  lv_obj_t *lbl = lv_label_create(btn);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(lbl, text);
  lv_obj_set_flex_grow(lbl, 1);

  if (badge) {
    lv_obj_t *dot = lv_obj_create(btn);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  }

  lv_obj_t *chev = lv_label_create(btn);
  lv_obj_set_style_text_font(chev, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(chev, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(chev, LV_SYMBOL_RIGHT);
  if (cb) {
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
  }
}

void ensureKeyboard(lv_obj_t *ta) {
  if (!kb) {
    kb = lv_keyboard_create(content);
    lv_obj_set_width(kb, lv_pct(100));
    lv_obj_set_height(kb, 168);
    lv_obj_set_style_radius(kb, 0, 0);
  }
  lv_keyboard_set_textarea(kb, ta);
}

void onFieldFocus(lv_event_t *event) {
  ensureKeyboard(lv_event_get_target(event));
}

lv_obj_t *addField(lv_obj_t *parent, const char *caption, const char *value, bool secret) {
  lv_obj_t *cap = lv_label_create(parent);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(cap, caption);

  lv_obj_t *ta = lv_textarea_create(parent);
  lv_obj_set_width(ta, lv_pct(100));
  lv_obj_set_style_bg_color(ta, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_color(ta, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_border_width(ta, 1, 0);
  lv_obj_set_style_radius(ta, 0, 0);
  lv_obj_set_style_text_color(ta, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_pad_ver(ta, 8, 0);
  lv_textarea_set_one_line(ta, true);
  if (secret) {
    lv_textarea_set_placeholder_text(ta, "leave blank to keep");
  } else if (value && value[0]) {
    lv_textarea_set_text(ta, value);
  }
  lv_obj_add_event_cb(ta, onFieldFocus, LV_EVENT_FOCUSED, nullptr);
  lv_obj_add_event_cb(ta, onFieldFocus, LV_EVENT_CLICKED, nullptr);
  if (fieldCount < 5) {
    fields[fieldCount++] = ta;
  }
  return ta;
}

String fieldText(int index) {
  if (index < 0 || index >= fieldCount || !fields[index]) {
    return String();
  }
  return String(lv_textarea_get_text(fields[index]));
}

void restartSaved() {
  setStatus("Saved. Restarting...", COL_ACCENT);
  lv_refr_now(nullptr);
  delay(700);
  ESP.restart();
}

void onSave(lv_event_t *) {
  AppConfig next = config();
  if (pane == Pane::Filaman) {
    next.filamanBaseUrl = fieldText(0);
    const String key = fieldText(1);
    if (key.length()) {
      next.filamanApiKey = key;
    }
    next.filamanTlsInsecure = tlsOn;
  } else if (pane == Pane::Mqtt) {
    next.mqttHost = fieldText(0);
    const String port = fieldText(1);
    if (port.length()) {
      next.mqttPort = port.toInt();
    }
    next.mqttUsername = fieldText(2);
    const String pass = fieldText(3);
    if (pass.length()) {
      next.mqttPassword = pass;
    }
    const String pfx = fieldText(4);
    if (pfx.length()) {
      next.topicPrefix = pfx;
    }
  } else if (pane == Pane::Device) {
    next.otaHostname = fieldText(0);
    const String tz = fieldText(1);
    if (tz.length()) {
      next.displayTimezone = tz;
    }
    const String pass = fieldText(2);
    if (pass.length()) {
      next.otaPassword = pass;
    }
  } else {
    return;
  }
  next.save();
  restartSaved();
}

void onTls(lv_event_t *) {
  tlsOn = !tlsOn;
  if (tlsLabel) {
    lv_label_set_text(tlsLabel, tlsOn ? "TLS insecure: on" : "TLS insecure: off");
  }
}

void onTestFilaman(lv_event_t *) {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("WiFi not connected.", COL_DANGER);
    return;
  }
  String url = fieldText(0);
  url.trim();
  String key = fieldText(1);
  key.trim();
  if (!key.length()) {
    key = config().filamanApiKey;
  }
  if (!url.length() || !key.length()) {
    setStatus("Enter both Base URL and API key.", COL_WARN);
    return;
  }
  setStatus("Testing FilaMan connection...", COL_MUTED);
  lv_refr_now(nullptr);

  FilamanClient testClient;
  testClient.begin(url, key, tlsOn);
  PrinterOption printers[MAX_PRINTER_OPTIONS];
  int count = 0;
  String activeId;
  const unsigned long t0 = millis();
  FmResult r = testClient.listBambuddyPrinters(printers, MAX_PRINTER_OPTIONS, count, activeId);
  const unsigned long dt = millis() - t0;
  if (r.ok) {
    String msg = String("Connected (") + dt + " ms)  |  Found " + count +
                 (count == 1 ? " printer" : " printers");
    if (count > 0 && printers[0].name.length()) {
      msg += ": " + printers[0].name;
    }
    setStatus(msg, COL_ACCENT);
  } else {
    setStatus(String("Connection failed: ") + r.error, COL_DANGER);
  }
}

void onTestMqtt(lv_event_t *) {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("WiFi not connected.", COL_DANGER);
    return;
  }
  String host = fieldText(0);
  host.trim();
  int port = fieldText(1).toInt();
  if (port <= 0) {
    port = 1883;
  }
  String user = fieldText(2);
  user.trim();
  String pass = fieldText(3);
  if (!pass.length()) {
    pass = config().mqttPassword;
  }
  if (!host.length()) {
    setStatus("Enter MQTT broker host.", COL_WARN);
    return;
  }
  setStatus(String("Testing MQTT ") + host + ":" + port + "...", COL_MUTED);
  lv_refr_now(nullptr);

  WiFiClient tcp;
  tcp.setTimeout(3);
  const unsigned long t0 = millis();
  if (!tcp.connect(host.c_str(), static_cast<uint16_t>(port))) {
    setStatus(String("TCP connect failed to ") + host + ":" + port, COL_DANGER);
    return;
  }
  PubSubClient mqtt(tcp);
  mqtt.setServer(host.c_str(), static_cast<uint16_t>(port));
  String cid = String("crowpanel-test-") + String(static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFF), HEX);
  bool ok = false;
  if (user.length()) {
    ok = mqtt.connect(cid.c_str(), user.c_str(), pass.c_str());
  } else {
    ok = mqtt.connect(cid.c_str());
  }
  const unsigned long dt = millis() - t0;
  if (ok) {
    mqtt.disconnect();
    setStatus(String("MQTT broker connected! (") + dt + " ms)", COL_ACCENT);
  } else {
    setStatus(String("MQTT auth/handshake failed (rc=") + mqtt.state() + ")", COL_DANGER);
  }
}

void onScreen(lv_event_t *event) {
  const int page = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  menu_close();
  ui_open_page(page);
}

void onPrinter(lv_event_t *) {
  menu_close();
  printer_select_open();
}

void onWifi(lv_event_t *) {
  menu_close();
  wifi_setup_open();
}

void onBackHome(lv_event_t *) { showHome(); }

void showHome() {
  pane = Pane::Home;
  confirmResetArmed = false;
  clearContent();
  lv_label_set_text(titleLabel, "Menu");
  // Compact one-line status in the header row. ASCII separators only —
  // Montserrat does not include middot/other punctuation (empty boxes).
  // Hostname/.local is omitted: many LANs do not resolve mDNS.
  String sub = String("v") + FIRMWARE_VERSION;
  if (WiFi.status() == WL_CONNECTED) {
    sub += "  |  " + WiFi.localIP().toString();
  } else {
    sub += "  |  WiFi offline";
  }
  setStatus(sub, COL_MUTED);

  // No scrolling on the home grid — tiles fill the available content area.
  lv_obj_t *col = lv_obj_create(content);
  lv_obj_remove_style_all(col);
  lv_obj_set_width(col, lv_pct(100));
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_style_min_height(col, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 10, 0);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

  addSection(col, "SCREENS");
  lv_obj_t *screens = makeMenuRow(col, 86);
  makeMenuTile(screens, "P", "Printer", true, false, onScreen,
               reinterpret_cast<void *>(static_cast<intptr_t>(STATUS_PAGE)));
  makeMenuTile(screens, "A", "AMS View", true, false, onScreen,
               reinterpret_cast<void *>(static_cast<intptr_t>(AMS_OVERVIEW_PAGE)));
  makeMenuTile(screens, "!", "Alerts", true, false, onScreen,
               reinterpret_cast<void *>(static_cast<intptr_t>(ALERTS_PAGE)));

  addSection(col, "CONFIGURATION");
  lv_obj_t *cfg = lv_obj_create(col);
  lv_obj_remove_style_all(cfg);
  lv_obj_set_width(cfg, lv_pct(100));
  lv_obj_set_flex_grow(cfg, 1);
  lv_obj_set_style_min_height(cfg, 0, 0);
  lv_obj_set_flex_flow(cfg, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(cfg, 10, 0);
  lv_obj_clear_flag(cfg, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *row1 = makeMenuRow(cfg, 0);
  makeMenuTile(row1, "Pr", "Printer", false, false, onPrinter, nullptr);
  makeMenuTile(row1, "Wi", "WiFi", false, false, onWifi, nullptr);
  makeMenuTile(row1, "Fi", "FilaMan", false, false, [](lv_event_t *) { showFilaman(); }, nullptr);
  makeMenuTile(row1, "Mq", "MQTT", false, false, [](lv_event_t *) { showMqtt(); }, nullptr);

  lv_obj_t *row2 = makeMenuRow(cfg, 0);
  makeMenuTile(row2, "Di", "Display", false, false, [](lv_event_t *) { showDisplay(); }, nullptr);
  makeMenuTile(row2, "De", "Device", false, false, [](lv_event_t *) { showDevice(); }, nullptr);
  makeMenuTile(row2, "Up", "Update", false, updateBadgeVisible(), [](lv_event_t *) { showUpdate(); },
               nullptr);
  makeMenuTile(row2, "Sy", "System", false, false, [](lv_event_t *) { showSystem(); }, nullptr);
}

void showFilaman() {
  pane = Pane::Filaman;
  clearContent();
  lv_label_set_text(titleLabel, "FilaMan");
  setStatus("Blank API key keeps the saved key.", COL_MUTED);
  tlsOn = config().filamanTlsInsecure;

  lv_obj_t *col = makeScroll();
  lv_obj_t *card = makeCard(col);
  addField(card, "Base URL", config().filamanBaseUrl.c_str(), false);
  addField(card, "API key", "", true);
  lv_obj_t *tlsBtn = makeAction(card, "", false, onTls, nullptr);
  tlsLabel = lv_obj_get_child(tlsBtn, 0);
  lv_label_set_text(tlsLabel, tlsOn ? "TLS insecure: on" : "TLS insecure: off");

  makeAction(col, "Test Connection", false, onTestFilaman, nullptr);
  makeAction(col, "Save & reboot", true, onSave, nullptr);
  makeAction(col, "Back", false, onBackHome, nullptr);
}

void showMqtt() {
  pane = Pane::Mqtt;
  clearContent();
  lv_label_set_text(titleLabel, "MQTT");
  setStatus("Blank password keeps the saved password.", COL_MUTED);

  lv_obj_t *col = makeScroll();
  lv_obj_t *card = makeCard(col);
  const String port = String(config().mqttPort);
  addField(card, "Host", config().mqttHost.c_str(), false);
  addField(card, "Port", port.c_str(), false);
  addField(card, "Username", config().mqttUsername.c_str(), false);
  addField(card, "Password", "", true);
  addField(card, "Topic prefix", config().topicPrefix.c_str(), false);
  makeAction(col, "Test Connection", false, onTestMqtt, nullptr);
  makeAction(col, "Save & reboot", true, onSave, nullptr);
  makeAction(col, "Back", false, onBackHome, nullptr);
}

constexpr int kTimeoutChoices[] = {5, 10, 15, 30};

int longerTimeout(int minutes) {
  for (int choice : kTimeoutChoices) {
    if (choice > minutes) {
      return choice;
    }
  }
  return 0;
}

int shorterTimeout(int minutes) {
  int found = 0;
  for (int choice : kTimeoutChoices) {
    if (choice < minutes) {
      found = choice;
    }
  }
  return found;
}

enum ChoiceKind : uint8_t {
  CK_FULL_BRIGHT = 0,
  CK_DIM_BRIGHT,
  CK_IDLE,
  CK_SPOOL_LOW,
  CK_SPOOL_CRIT,
  CK_DIM_MIN,
  CK_OFF_MIN,
};

struct ChoiceRowCtx {
  const int *values = nullptr;
  int count = 0;
  ChoiceKind kind = CK_FULL_BRIGHT;
};

// One context per option row in the Display pane. Reset in showDisplay();
// row objects hold a pointer, so no heap churn on rebuild.
ChoiceRowCtx choiceCtx[8];
int choiceCtxUsed = 0;

int currentValForKind(ChoiceKind kind) {
  switch (kind) {
    case CK_FULL_BRIGHT:
      return config().displayFullBrightPct;
    case CK_DIM_BRIGHT:
      return config().displayDimBrightPct;
    case CK_IDLE:
      return config().idleReturnSec;
    case CK_SPOOL_LOW:
      return config().spoolLowPct;
    case CK_SPOOL_CRIT:
      return config().spoolCritPct;
    case CK_DIM_MIN:
      return config().displayDimMin;
    case CK_OFF_MIN:
      return config().displayOffMin;
  }
  return 0;
}

bool choiceAllowed(ChoiceKind kind, int value) {
  if (kind == CK_DIM_MIN) {
    return longerTimeout(value) > 0;
  }
  if (kind == CK_OFF_MIN) {
    return shorterTimeout(value) > 0;
  }
  return true;
}

// Re-style one option row in place after a tap. No lv_obj_clean rebuild,
// no flex re-layout of the whole pane.
void restyleChoiceRow(lv_obj_t *row) {
  if (!row) {
    return;
  }
  auto *ctx = static_cast<ChoiceRowCtx *>(lv_obj_get_user_data(row));
  if (!ctx) {
    return;
  }
  const int current = currentValForKind(ctx->kind);
  const uint32_t n = lv_obj_get_child_cnt(row);
  for (uint32_t i = 0; i < n; ++i) {
    lv_obj_t *btn = lv_obj_get_child(row, i);
    if (!btn) {
      continue;
    }
    const int value = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn)));
    const bool allowed = choiceAllowed(ctx->kind, value);
    const bool selected = allowed && (value == current);
    if (allowed) {
      lv_obj_clear_state(btn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    } else {
      lv_obj_add_state(btn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_opa(btn, LV_OPA_40, 0);
    }
    styleButton(btn, selected);
    if (allowed && !selected) {
      // styleButton resets border for unselected buttons; keep the card border.
      lv_obj_set_style_border_width(btn, 1, 0);
    }
    if (lv_obj_t *lbl = lv_obj_get_child(btn, 0)) {
      lv_obj_set_style_text_color(
          lbl, lv_color_hex(selected ? COL_BG : (allowed ? COL_TEXT : COL_MUTED)), 0);
    }
  }
}

void onChoicePicked(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_target(event);
  lv_obj_t *row = lv_obj_get_parent(btn);
  auto *ctx = static_cast<ChoiceRowCtx *>(lv_obj_get_user_data(row));
  if (!ctx) {
    return;
  }
  const int value = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (!choiceAllowed(ctx->kind, value) || value == currentValForKind(ctx->kind)) {
    return;
  }
  switch (ctx->kind) {
    case CK_FULL_BRIGHT:
      config().setDisplayBrightness(value, config().displayDimBrightPct);
      backlight_apply();
      break;
    case CK_DIM_BRIGHT:
      config().setDisplayBrightness(config().displayFullBrightPct, value);
      break;
    case CK_IDLE:
      config().setIdleReturnSec(value);
      break;
    case CK_SPOOL_LOW:
      config().setSpoolThresholds(value, config().spoolCritPct);
      break;
    case CK_SPOOL_CRIT:
      config().setSpoolThresholds(config().spoolLowPct, value);
      break;
    case CK_DIM_MIN: {
      int off = config().displayOffMin;
      if (off <= value) {
        off = longerTimeout(value);
      }
      config().setDisplayTimeouts(value, off);
      break;
    }
    case CK_OFF_MIN: {
      int dim = config().displayDimMin;
      if (dim >= value) {
        dim = shorterTimeout(value);
      }
      config().setDisplayTimeouts(dim, value);
      break;
    }
  }
  restyleChoiceRow(row);
  // A dim/off change can alter the allowed set of the sibling timeout row.
  if (ctx->kind == CK_DIM_MIN || ctx->kind == CK_OFF_MIN) {
    if (lv_obj_t *card = lv_obj_get_parent(row)) {
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(card); ++i) {
        lv_obj_t *sib = lv_obj_get_child(card, i);
        if (sib && sib != row) {
          restyleChoiceRow(sib);
        }
      }
    }
  }
}

void addTimeoutRow(lv_obj_t *parent, const char *caption, bool dimRow) {
  lv_obj_t *cap = lv_label_create(parent);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(cap, caption);

  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, 44);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  ChoiceKind kind = dimRow ? CK_DIM_MIN : CK_OFF_MIN;
  ChoiceRowCtx *ctx = nullptr;
  if (choiceCtxUsed < static_cast<int>(sizeof(choiceCtx) / sizeof(choiceCtx[0]))) {
    ctx = &choiceCtx[choiceCtxUsed++];
    ctx->values = kTimeoutChoices;
    ctx->count = static_cast<int>(sizeof(kTimeoutChoices) / sizeof(kTimeoutChoices[0]));
    ctx->kind = kind;
    lv_obj_set_user_data(row, ctx);
  }

  for (int minutes : kTimeoutChoices) {
    lv_obj_t *btn = lv_btn_create(row);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, 44);
    lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(minutes)));
    lv_obj_add_event_cb(btn, onChoicePicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(minutes)));
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl, (String(minutes) + " min").c_str());
    lv_obj_center(lbl);
  }
  restyleChoiceRow(row);
}

void addChoiceRow(lv_obj_t *parent, const char *caption, const int *values, const char *const *labels,
                  int count, ChoiceKind kind) {
  lv_obj_t *cap = lv_label_create(parent);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(cap, caption);

  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, 44);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  ChoiceRowCtx *ctx = nullptr;
  if (choiceCtxUsed < static_cast<int>(sizeof(choiceCtx) / sizeof(choiceCtx[0]))) {
    ctx = &choiceCtx[choiceCtxUsed++];
    ctx->values = values;
    ctx->count = count;
    ctx->kind = kind;
    lv_obj_set_user_data(row, ctx);
  }

  for (int i = 0; i < count; ++i) {
    lv_obj_t *btn = lv_btn_create(row);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, 44);
    lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(values[i])));
    lv_obj_add_event_cb(btn, onChoicePicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(values[i])));
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl, labels[i]);
    lv_obj_center(lbl);
  }
  restyleChoiceRow(row);
}

void showDisplay() {
  pane = Pane::Display;
  clearContent();
  choiceCtxUsed = 0;
  lv_label_set_text(titleLabel, "Display & Alerts");
  setStatus("All settings below take effect immediately and persist across reboots.", COL_MUTED);

  lv_obj_t *col = makeScroll();

  addSection(col, "BACKLIGHT & SLEEP");
  lv_obj_t *cardBrt = makeCard(col);
  static constexpr int kFullVals[] = {50, 75, 100};
  static constexpr const char *kFullLbls[] = {"50%", "75%", "100%"};
  addChoiceRow(cardBrt, "Normal brightness", kFullVals, kFullLbls, 3, CK_FULL_BRIGHT);

  static constexpr int kDimVals[] = {10, 20, 35};
  static constexpr const char *kDimLbls[] = {"10%", "20%", "35%"};
  addChoiceRow(cardBrt, "Dimmed brightness", kDimVals, kDimLbls, 3, CK_DIM_BRIGHT);

  addTimeoutRow(cardBrt, "Dim after", true);
  addTimeoutRow(cardBrt, "Turn off after", false);

  addSection(col, "NAVIGATION & SPOOL WARNINGS");
  lv_obj_t *cardNav = makeCard(col);
  static constexpr int kIdleVals[] = {0, 60, 180};
  static constexpr const char *kIdleLbls[] = {"Off", "1 min", "3 min"};
  addChoiceRow(cardNav, "Auto-return to AMS View", kIdleVals, kIdleLbls, 3, CK_IDLE);

  static constexpr int kLowVals[] = {20, 30, 40};
  static constexpr const char *kLowLbls[] = {"20%", "30%", "40%"};
  addChoiceRow(cardNav, "Low spool warning (amber)", kLowVals, kLowLbls, 3, CK_SPOOL_LOW);

  static constexpr int kCritVals[] = {10, 15, 20};
  static constexpr const char *kCritLbls[] = {"10%", "15%", "20%"};
  addChoiceRow(cardNav, "Critical spool alert (red)", kCritVals, kCritLbls, 3, CK_SPOOL_CRIT);

  makeAction(col, "Back", false, onBackHome, nullptr);
}

struct TzPreset {
  const char *label;
  const char *posix;
};

constexpr TzPreset kTzPresets[] = {
    {"US East", "EST5EDT,M3.2.0,M11.1.0"},
    {"US Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"US Mtn", "MST7MDT,M3.2.0,M11.1.0"},
    {"US Pac", "PST8PDT,M3.2.0,M11.1.0"},
    {"UTC", "UTC0"},
    {"UK", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"EU Cent", "CET-1CEST,M3.5.0,M10.5.0/3"},
};

void onTzPresetPicked(lv_event_t *event) {
  const int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (idx < 0 || idx >= static_cast<int>(sizeof(kTzPresets) / sizeof(kTzPresets[0]))) {
    return;
  }
  if (fieldCount > 1 && fields[1]) {
    lv_textarea_set_text(fields[1], kTzPresets[idx].posix);
    setStatus(String("Selected ") + kTzPresets[idx].label + " (" + kTzPresets[idx].posix + ")",
              COL_ACCENT);
  }
}

void showDevice() {
  pane = Pane::Device;
  clearContent();
  lv_label_set_text(titleLabel, "Device & Timezone");
  setStatus("Hostname, timezone preset or custom POSIX TZ, and the OTA / config password.",
            COL_MUTED);

  lv_obj_t *col = makeScroll();
  lv_obj_t *card = makeCard(col);
  addField(card, "Hostname", config().otaHostname.c_str(), false);
  addField(card, "Timezone (POSIX TZ)", config().displayTimezone.c_str(), false);

  lv_obj_t *tzCap = lv_label_create(card);
  lv_obj_set_style_text_font(tzCap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(tzCap, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(tzCap, "Quick timezone presets");

  lv_obj_t *tzRow = lv_obj_create(card);
  lv_obj_remove_style_all(tzRow);
  lv_obj_set_width(tzRow, lv_pct(100));
  lv_obj_set_height(tzRow, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(tzRow, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(tzRow, 6, 0);
  lv_obj_set_style_pad_row(tzRow, 6, 0);
  lv_obj_clear_flag(tzRow, LV_OBJ_FLAG_SCROLLABLE);

  constexpr int kTzCount = static_cast<int>(sizeof(kTzPresets) / sizeof(kTzPresets[0]));
  for (int i = 0; i < kTzCount; ++i) {
    const bool active = (config().displayTimezone == kTzPresets[i].posix);
    lv_obj_t *btn = lv_btn_create(tzRow);
    lv_obj_set_height(btn, 38);
    lv_obj_set_style_pad_hor(btn, 12, 0);
    styleButton(btn, active);
    lv_obj_set_style_min_height(btn, 38, 0);
    lv_obj_add_event_cb(btn, onTzPresetPicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(active ? COL_BG : COL_TEXT), 0);
    lv_label_set_text(lbl, kTzPresets[i].label);
    lv_obj_center(lbl);
  }

  addField(card, "OTA / config password", "", true);
  makeAction(col, "Save & reboot", true, onSave, nullptr);
  makeAction(col, "Back", false, onBackHome, nullptr);
}

void runGithubCheck() {
  setStatus("Checking GitHub for updates...", COL_MUTED);
  lv_refr_now(nullptr);
  String ver, url, err;
  if (ota_check_github_update(ver, url, err)) {
    latestGithubVer = ver;
    latestGithubUrl = url;
    showUpdate();
    const int cmp = ota_compare_versions(latestGithubVer, FIRMWARE_VERSION);
    if (cmp > 0) {
      setStatus(String("Update available: v") + latestGithubVer, COL_ACCENT);
    } else {
      setStatus(String("Up to date (v") + latestGithubVer + ")", COL_MUTED);
    }
  } else {
    setStatus(String("Update check failed: ") + err, COL_DANGER);
  }
}

void onCheckUpdates(lv_event_t *) {
  runGithubCheck();
}

void onAutoCheckToggle(lv_event_t *) {
  const bool enabled = !config().autoCheckUpdates;
  config().setAutoCheckUpdates(enabled);
  ui_refresh_update_badge();
  if (enabled) {
    if (autoCheckLabel) {
      lv_label_set_text(autoCheckLabel, "Automatically check for updates: on");
    }
    runGithubCheck();
  } else {
    showUpdate();
    setStatus("Automatic update checks disabled.", COL_MUTED);
  }
}

void onInstallGithubUpdate(lv_event_t *) {
  touch_input_cooldown(500);
  if (updateProgressBar) {
    lv_obj_clear_flag(updateProgressBar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(updateProgressBar, 0, LV_ANIM_OFF);
  }
  setStatus("Connecting to GitHub...", COL_ACCENT);
  lv_refr_now(nullptr);

  String err;
  const bool ok = ota_install_github_update(
      latestGithubUrl,
      [](int pct) {
        if (updateProgressBar) {
          lv_bar_set_value(updateProgressBar, pct, LV_ANIM_OFF);
        }
        setStatus(String("Downloading & flashing... ") + pct + "%", COL_ACCENT);
        lv_refr_now(nullptr);
      },
      err);

  if (ok) {
    setStatus("Update complete! Rebooting...", COL_ACCENT);
    lv_refr_now(nullptr);
    delay(800);
    ESP.restart();
  } else {
    setStatus(String("Update failed: ") + err, COL_DANGER);
    lv_refr_now(nullptr);
  }
}

void showUpdate() {
  pane = Pane::Update;
  clearContent();
  lv_label_set_text(titleLabel, "Firmware Update");
  syncLatestFromOta();

  const bool hasLatest = latestGithubVer.length() > 0;
  const bool newerAvailable =
      hasLatest && ota_compare_versions(latestGithubVer, FIRMWARE_VERSION) > 0;
  const bool autoOn = config().autoCheckUpdates;

  if (!hasLatest) {
    setStatus(autoOn ? "Automatic checks enabled - waiting for the next GitHub poll."
                     : "Check GitHub for the latest firmware release (keeps all saved settings).",
              COL_MUTED);
  } else if (newerAvailable) {
    setStatus(String("Update available: v") + latestGithubVer, COL_ACCENT);
  } else {
    setStatus(String("Up to date (v") + latestGithubVer + ")", COL_MUTED);
  }

  lv_obj_t *col = makeScroll();
  lv_obj_t *card = makeCard(col);

  lv_obj_t *curLbl = lv_label_create(card);
  lv_obj_set_style_text_font(curLbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(curLbl, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(curLbl, (String("Installed version:  v") + FIRMWARE_VERSION).c_str());

  lv_obj_t *latLbl = lv_label_create(card);
  lv_obj_set_style_text_font(latLbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(
      latLbl, lv_color_hex(newerAvailable ? COL_ACCENT : (hasLatest ? COL_TEXT : COL_MUTED)), 0);
  if (hasLatest) {
    String t = String("Latest on GitHub:   v") + latestGithubVer;
    if (newerAvailable) {
      t += "  (NEW)";
    }
    lv_label_set_text(latLbl, t.c_str());
  } else {
    lv_label_set_text(latLbl, "Latest on GitHub:   (tap Check for updates)");
  }

  updateProgressBar = lv_bar_create(card);
  lv_obj_set_width(updateProgressBar, lv_pct(100));
  lv_obj_set_height(updateProgressBar, 10);
  lv_bar_set_range(updateProgressBar, 0, 100);
  lv_bar_set_value(updateProgressBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(updateProgressBar, lv_color_hex(COL_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_color(updateProgressBar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(updateProgressBar, 5, LV_PART_MAIN);
  lv_obj_set_style_radius(updateProgressBar, 5, LV_PART_INDICATOR);
  lv_obj_add_flag(updateProgressBar, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *autoBtn = makeAction(col, "", autoOn, onAutoCheckToggle, nullptr);
  autoCheckLabel = lv_obj_get_child(autoBtn, 0);
  if (autoCheckLabel) {
    lv_label_set_text(autoCheckLabel,
                      autoOn ? "Automatically check for updates: on"
                             : "Automatically check for updates: off");
  }

  makeAction(col, (String(LV_SYMBOL_REFRESH) + "  Check for updates").c_str(), !hasLatest,
             onCheckUpdates, nullptr);

  String installText;
  if (newerAvailable) {
    installText = String(LV_SYMBOL_DOWNLOAD) + "  Install v" + latestGithubVer + " from GitHub";
  } else if (hasLatest) {
    installText = String(LV_SYMBOL_DOWNLOAD) + "  Reinstall v" + latestGithubVer + " from GitHub";
  } else {
    installText = String(LV_SYMBOL_DOWNLOAD) + "  Install latest from GitHub";
  }
  makeAction(col, installText.c_str(), newerAvailable, onInstallGithubUpdate, nullptr);

  makeAction(col, "Back", false, onBackHome, nullptr);
}

void addStatLine(lv_obj_t *parent, const char *label, const String &val) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *k = lv_label_create(row);
  lv_obj_set_style_text_font(k, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(k, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(k, label);

  lv_obj_t *v = lv_label_create(row);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(v, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(v, val.c_str());
}

String formatUptime() {
  const unsigned long totalSec = millis() / 1000UL;
  const unsigned long d = totalSec / 86400UL;
  const unsigned long h = (totalSec % 86400UL) / 3600UL;
  const unsigned long m = (totalSec % 3600UL) / 60UL;
  const unsigned long s = totalSec % 60UL;
  if (d > 0) {
    return String(d) + "d " + h + "h " + m + "m";
  }
  if (h > 0) {
    return String(h) + "h " + m + "m " + s + "s";
  }
  return String(m) + "m " + s + "s";
}

void onRebootNow(lv_event_t *) {
  setStatus("Rebooting display...", COL_ACCENT);
  lv_refr_now(nullptr);
  delay(500);
  ESP.restart();
}

void onFactoryReset(lv_event_t *) {
  if (!confirmResetArmed) {
    confirmResetArmed = true;
    showSystem();
    setStatus("Tap 'Confirm Factory Reset' again to erase saved NVS settings and reboot.", COL_WARN);
    return;
  }
  config().factoryReset();
  setStatus("Factory settings restored. Rebooting...", COL_ACCENT);
  lv_refr_now(nullptr);
  delay(700);
  ESP.restart();
}

void showSystem() {
  pane = Pane::System;
  clearContent();
  lv_label_set_text(titleLabel, "System & Diagnostics");
  setStatus("Live hardware, network, and FilaMan telemetry.", COL_MUTED);

  lv_obj_t *col = makeScroll();
  lv_obj_t *card = makeCard(col);

  addStatLine(card, "Firmware", String("v") + FIRMWARE_VERSION);
  addStatLine(card, "Uptime", formatUptime());

  if (WiFi.status() == WL_CONNECTED) {
    addStatLine(card, "WiFi SSID / RSSI",
                WiFi.SSID() + "  (" + String(WiFi.RSSI()) + " dBm)");
    addStatLine(card, "IP / Hostname",
                WiFi.localIP().toString() + "  (" + config().otaHostname + ".local)");
  } else {
    addStatLine(card, "WiFi", "Disconnected");
  }

  String pollInfo = display_poll_active() ? String(display_poll_status()) : String("MQTT mode");
  if (display_poll_active() && display_poll_last_latency_ms() > 0) {
    pollInfo += "  (" + String(display_poll_last_latency_ms()) + " ms)";
  }
  addStatLine(card, "Data source", pollInfo);

  const uint32_t freeInt = ESP.getFreeHeap() / 1024U;
  const uint32_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U;
  addStatLine(card, "Free RAM / PSRAM",
              String(freeInt) + " KB internal  |  " + String(freePsram) + " KB PSRAM");

  makeAction(col, "Refresh Diagnostics", false, [](lv_event_t *) { showSystem(); }, nullptr);
  makeAction(col, "Reboot Display", false, onRebootNow, nullptr);

  lv_obj_t *rstBtn = makeAction(
      col, confirmResetArmed ? "Confirm Factory Reset (Erases NVS)" : "Factory Reset Settings",
      false, onFactoryReset, nullptr);
  lv_obj_set_style_border_color(rstBtn, lv_color_hex(COL_DANGER), 0);
  if (lv_obj_t *lbl = lv_obj_get_child(rstBtn, 0)) {
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_DANGER), 0);
  }

  makeAction(col, "Back", false, onBackHome, nullptr);
}

void onCloseClicked(lv_event_t *) { menu_close(); }

}  // namespace

void menu_begin() {
  overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(overlay);
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(overlay, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(overlay, 12, 0);
  lv_obj_set_style_pad_row(overlay, 8, 0);
  lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);

  // Single-row header: title | truncated status | close. A second status line
  // under the title kept growing/overflowing into the content area on long
  // v/IP/host strings even with LONG_DOT, so keep everything on one clipped row.
  lv_obj_t *header = lv_obj_create(overlay);
  lv_obj_remove_style_all(header);
  lv_obj_set_width(header, lv_pct(100));
  lv_obj_set_height(header, 44);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(header, 10, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_clip_corner(header, true, 0);

  titleLabel = lv_label_create(header);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(titleLabel, "Menu");
  lv_obj_set_style_min_width(titleLabel, 70, 0);

  statusLabel = lv_label_create(header);
  lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_flex_grow(statusLabel, 1);
  lv_obj_set_width(statusLabel, 0);  // required so flex + LONG_DOT can shrink
  lv_obj_set_style_max_height(statusLabel, 22, 0);
  lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_DOT);
  lv_label_set_text(statusLabel, "");

  lv_obj_t *closeBtn = lv_btn_create(header);
  lv_obj_set_size(closeBtn, 60, 40);
  lv_obj_set_style_bg_color(closeBtn, lv_color_hex(COL_SURFACE_2), 0);
  lv_obj_set_style_border_width(closeBtn, 1, 0);
  lv_obj_set_style_border_color(closeBtn, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(closeBtn, 10, 0);
  lv_obj_add_event_cb(closeBtn, onCloseClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *xl = lv_label_create(closeBtn);
  lv_obj_set_style_text_color(xl, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(xl, LV_SYMBOL_CLOSE);
  lv_obj_center(xl);

  content = lv_obj_create(overlay);
  lv_obj_remove_style_all(content);
  lv_obj_set_width(content, lv_pct(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_min_height(content, 0, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(content, 8, 0);
  lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
}

void menu_open() {
  if (!overlay) {
    return;
  }
  wifi_setup_close();
  printer_select_close();
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(overlay);
  showHome();
}

void menu_open_update() {
  if (!overlay) {
    return;
  }
  wifi_setup_close();
  printer_select_close();
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(overlay);
  showUpdate();
}

void menu_close() {
  if (overlay) {
    clearContent();
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  }
}

bool menu_is_open() { return overlay && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN); }
