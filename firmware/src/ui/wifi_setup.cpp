#include "wifi_setup.h"

#include <WiFi.h>
#include <lvgl.h>

#include <vector>

#include "app_config.h"
#include "touch_input.h"

// Scan -> pick SSID -> type password -> save to NVS -> restart to connect.
// Mirrors the kiosk overlay pattern (lv_layer_top overlay, scrollable button
// list, on-screen keyboard).

namespace {

constexpr uint32_t COL_BG = 0x0A0E14;
constexpr uint32_t COL_SURFACE_2 = 0x182028;
constexpr uint32_t COL_BORDER = 0x2A3544;
constexpr uint32_t COL_TEXT = 0xF2F5F8;
constexpr uint32_t COL_MUTED = 0x8B98A8;
constexpr uint32_t COL_ACCENT = 0x3DDC97;

lv_obj_t *overlay = nullptr;
lv_obj_t *titleLabel = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *taPass = nullptr;

std::vector<String> ssids;
String selSsid;

void showWifiInfo();
void showScanList();
void showPassword();

void setStatus(const String &text, uint32_t color) {
  if (!statusLabel) {
    return;
  }
  lv_label_set_text(statusLabel, text.c_str());
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(color), 0);
  lv_refr_now(NULL);  // render now so "Scanning..." shows before the blocking scan
}

void clearContent() {
  touch_input_cooldown(260);
  if (lv_indev_t *indev = lv_indev_get_act()) {
    lv_indev_wait_release(indev);
  }
  lv_obj_clean(content);
  taPass = nullptr;
}

lv_obj_t *makeColumn() {
  lv_obj_t *col = lv_obj_create(content);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, lv_pct(100), lv_pct(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 8, 0);
  lv_obj_set_scroll_dir(col, LV_DIR_VER);
  return col;
}

lv_obj_t *makeRowButton(lv_obj_t *parent, const String &text, lv_event_cb_t cb, void *ud) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, lv_pct(100));
  lv_obj_set_style_min_height(btn, 46, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(COL_SURFACE_2), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(lbl, text.c_str());
  lv_obj_center(lbl);
  if (cb) {
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
  }
  return btn;
}

void onSsidClicked(lv_event_t *e) {
  intptr_t i = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  if (i >= 0 && i < static_cast<intptr_t>(ssids.size())) {
    selSsid = ssids[i];
    showPassword();
  }
}

void onScanAgain(lv_event_t *) { showScanList(); }

void onSave(lv_event_t *) {
  const String pass = taPass ? String(lv_textarea_get_text(taPass)) : String();
  config().setWifi(selSsid, pass);
  setStatus("Saved - restarting to connect...", COL_ACCENT);
  delay(900);
  ESP.restart();  // clean apply: on boot startWifi() joins with the new creds
}

// Home screen: current WiFi status + a button to scan and change it.
void showWifiInfo() {
  clearContent();
  lv_label_set_text(titleLabel, "WiFi");
  setStatus("", COL_MUTED);

  lv_obj_t *col = makeColumn();

  lv_obj_t *info = lv_label_create(col);
  lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
  lv_obj_set_width(info, lv_pct(100));
  lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
  String t;
  uint32_t color = COL_TEXT;
  if (WiFi.status() == WL_CONNECTED) {
    t = String("Connected to ") + WiFi.SSID() + "\nIP: " + WiFi.localIP().toString() +
        "\nSignal: " + String(WiFi.RSSI()) + " dBm";
    color = COL_ACCENT;
  } else if (config().wifiConfigured()) {
    t = config().wifiSsid + "\n(configured, not connected)";
    color = COL_MUTED;
  } else {
    t = "No WiFi configured yet";
    color = COL_MUTED;
  }
  lv_obj_set_style_text_color(info, lv_color_hex(color), 0);
  lv_label_set_text(info, t.c_str());

  makeRowButton(col, String(LV_SYMBOL_WIFI) + "  Scan & change WiFi", onScanAgain, nullptr);
  makeRowButton(
      col, String(LV_SYMBOL_CLOSE) + "  Close",
      [](lv_event_t *) { wifi_setup_close(); }, nullptr);
}

void showScanList() {
  clearContent();
  lv_label_set_text(titleLabel, "WiFi Setup");
  setStatus("Scanning...", COL_MUTED);

  const int n = WiFi.scanNetworks();
  ssids.clear();
  std::vector<int> rssis;
  for (int i = 0; i < n; ++i) {
    String s = WiFi.SSID(i);
    if (!s.length()) {
      continue;
    }
    bool found = false;
    for (size_t j = 0; j < ssids.size(); ++j) {
      if (ssids[j] == s) {
        if (WiFi.RSSI(i) > rssis[j]) rssis[j] = WiFi.RSSI(i);
        found = true;
        break;
      }
    }
    if (!found) {
      ssids.push_back(s);
      rssis.push_back(WiFi.RSSI(i));
    }
  }
  WiFi.scanDelete();

  const String cur = config().wifiConfigured() ? config().wifiSsid : String("none");
  setStatus(String(static_cast<int>(ssids.size())) + " networks   (current: " + cur + ")",
            COL_MUTED);

  lv_obj_t *col = makeColumn();
  makeRowButton(col, String(LV_SYMBOL_REFRESH) + "  Scan again", onScanAgain, nullptr);
  for (size_t i = 0; i < ssids.size(); ++i) {
    String label = String(LV_SYMBOL_WIFI) + "  " + ssids[i];
    if (ssids[i] == config().wifiSsid) {
      label = String(LV_SYMBOL_OK) + " " + ssids[i];  // currently configured
    }
    makeRowButton(col, label, onSsidClicked, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
  }
}

void showPassword() {
  clearContent();
  lv_label_set_text(titleLabel, "WiFi Password");
  setStatus(selSsid + "   (leave blank for open network)", COL_MUTED);

  lv_obj_t *col = makeColumn();

  taPass = lv_textarea_create(col);
  lv_obj_set_width(taPass, lv_pct(100));
  lv_textarea_set_one_line(taPass, true);
  lv_textarea_set_placeholder_text(taPass, "Password");

  makeRowButton(col, String(LV_SYMBOL_OK) + "  Connect", onSave, nullptr);
  makeRowButton(col, String(LV_SYMBOL_LEFT) + "  Back", onScanAgain, nullptr);

  // On-screen keyboard pinned to the bottom, targeting the password field.
  lv_obj_t *kb = lv_keyboard_create(content);
  lv_obj_set_size(kb, lv_pct(100), 190);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, taPass);
}

void onCloseClicked(lv_event_t *) { wifi_setup_close(); }

}  // namespace

void wifi_setup_begin() {
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

  lv_obj_t *header = lv_obj_create(overlay);
  lv_obj_remove_style_all(header);
  lv_obj_set_width(header, lv_pct(100));
  lv_obj_set_height(header, 40);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(header, 10, 0);

  titleLabel = lv_label_create(header);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_flex_grow(titleLabel, 1);
  lv_label_set_text(titleLabel, "WiFi Setup");

  lv_obj_t *closeBtn = lv_btn_create(header);
  lv_obj_set_size(closeBtn, 60, 40);
  lv_obj_set_style_bg_color(closeBtn, lv_color_hex(COL_SURFACE_2), 0);
  lv_obj_set_style_border_width(closeBtn, 1, 0);
  lv_obj_set_style_border_color(closeBtn, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(closeBtn, 8, 0);
  lv_obj_add_event_cb(closeBtn, onCloseClicked, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *xl = lv_label_create(closeBtn);
  lv_obj_set_style_text_color(xl, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(xl, LV_SYMBOL_CLOSE);
  lv_obj_center(xl);

  statusLabel = lv_label_create(overlay);
  lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_width(statusLabel, lv_pct(100));
  lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
  lv_label_set_text(statusLabel, "");

  content = lv_obj_create(overlay);
  lv_obj_remove_style_all(content);
  lv_obj_set_width(content, lv_pct(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
}

void wifi_setup_open() {
  if (!overlay) {
    return;
  }
  touch_input_cooldown(280);
  if (lv_indev_t *indev = lv_indev_get_act()) {
    lv_indev_wait_release(indev);
  }
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(overlay);
  showWifiInfo();
}

void wifi_setup_close() {
  if (overlay) {
    touch_input_cooldown(280);
    if (lv_indev_t *indev = lv_indev_get_act()) {
      lv_indev_wait_release(indev);
    }
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  }
}

bool wifi_setup_is_open() { return overlay && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN); }
