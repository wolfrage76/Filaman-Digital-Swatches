#include "menu_ui.h"

#include <lvgl.h>

#include "app_config.h"
#include "config.h"
#include "printer_select.h"
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

enum class Pane { Home, Filaman, Mqtt, Device, Display };

lv_obj_t *overlay = nullptr;
lv_obj_t *titleLabel = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *fields[4] = {};
lv_obj_t *kb = nullptr;
lv_obj_t *tlsLabel = nullptr;
int fieldCount = 0;
bool tlsOn = false;
Pane pane = Pane::Home;

void showHome();
void showFilaman();
void showMqtt();
void showDevice();
void showDisplay();

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
  if (content) {
    lv_obj_clean(content);
  }
  for (int i = 0; i < 4; ++i) {
    fields[i] = nullptr;
  }
  fieldCount = 0;
  kb = nullptr;
  tlsLabel = nullptr;
}

lv_obj_t *makeScroll() {
  lv_obj_t *col = lv_obj_create(content);
  lv_obj_remove_style_all(col);
  lv_obj_set_width(col, lv_pct(100));
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_style_min_height(col, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 10, 0);
  lv_obj_set_scroll_dir(col, LV_DIR_VER);
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
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_style_pad_row(card, 8, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  return card;
}

void styleButton(lv_obj_t *btn, bool accent) {
  lv_obj_set_style_min_height(btn, 46, 0);
  lv_obj_set_style_radius(btn, 10, 0);
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

void addNavRow(lv_obj_t *parent, const char *text, bool last, lv_event_cb_t cb, void *ud) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, lv_pct(100));
  lv_obj_set_height(btn, 48);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_pad_hor(btn, 8, 0);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
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
    lv_obj_set_style_radius(kb, 8, 0);
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
  lv_obj_set_style_radius(ta, 8, 0);
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
  if (fieldCount < 4) {
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
  } else if (pane == Pane::Device) {
    next.otaHostname = fieldText(0);
    const String pass = fieldText(1);
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
  clearContent();
  lv_label_set_text(titleLabel, "Menu");
  setStatus("", COL_MUTED);

  lv_obj_t *col = makeScroll();
  addSection(col, "SCREENS");
  lv_obj_t *screens = lv_obj_create(col);
  lv_obj_remove_style_all(screens);
  lv_obj_set_width(screens, lv_pct(100));
  lv_obj_set_height(screens, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(screens, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(screens, 8, 0);
  lv_obj_clear_flag(screens, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *printBtn =
      makeAction(screens, "Printer", false, onScreen, reinterpret_cast<void *>(static_cast<intptr_t>(STATUS_PAGE)));
  lv_obj_t *amsBtn = makeAction(screens, "AMS View", false, onScreen,
                                reinterpret_cast<void *>(static_cast<intptr_t>(AMS_OVERVIEW_PAGE)));
  lv_obj_t *alertsBtn =
      makeAction(screens, "Alerts", false, onScreen, reinterpret_cast<void *>(static_cast<intptr_t>(ALERTS_PAGE)));
  lv_obj_set_flex_grow(printBtn, 1);
  lv_obj_set_flex_grow(amsBtn, 1);
  lv_obj_set_flex_grow(alertsBtn, 1);
  lv_obj_set_width(printBtn, LV_SIZE_CONTENT);
  lv_obj_set_width(amsBtn, LV_SIZE_CONTENT);
  lv_obj_set_width(alertsBtn, LV_SIZE_CONTENT);

  addSection(col, "CONFIGURATION");
  lv_obj_t *card = makeCard(col);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_pad_row(card, 0, 0);
  addNavRow(card, "Printer", false, onPrinter, nullptr);
  addNavRow(card, "WiFi", false, onWifi, nullptr);
  addNavRow(card, "FilaMan", false, [](lv_event_t *) { showFilaman(); }, nullptr);
  addNavRow(card, "MQTT", false, [](lv_event_t *) { showMqtt(); }, nullptr);
  addNavRow(card, "Display", false, [](lv_event_t *) { showDisplay(); }, nullptr);
  addNavRow(card, "Device", true, [](lv_event_t *) { showDevice(); }, nullptr);
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

void onTimeoutPicked(lv_event_t *event) {
  const intptr_t packed = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  const bool dimRow = packed < 0;
  const int minutes = static_cast<int>(dimRow ? -packed : packed);
  int dim = config().displayDimMin;
  int off = config().displayOffMin;
  if (dimRow) {
    const int longer = longerTimeout(minutes);
    if (longer <= 0) {
      return;
    }
    dim = minutes;
    if (off <= dim) {
      off = longer;
    }
  } else {
    const int shorter = shorterTimeout(minutes);
    if (shorter <= 0) {
      return;
    }
    off = minutes;
    if (dim >= off) {
      dim = shorter;
    }
  }
  config().setDisplayTimeouts(dim, off);
  showDisplay();
}

void addTimeoutRow(lv_obj_t *parent, const char *caption, bool dimRow) {
  lv_obj_t *cap = lv_label_create(parent);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(cap, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(cap, caption);

  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, 48);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  const int current = dimRow ? config().displayDimMin : config().displayOffMin;
  for (int minutes : kTimeoutChoices) {
    const bool allowed = dimRow ? longerTimeout(minutes) > 0 : shorterTimeout(minutes) > 0;
    const bool selected = minutes == current;
    lv_obj_t *btn = lv_btn_create(row);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, 48);
    styleButton(btn, selected);
    if (!allowed) {
      lv_obj_add_state(btn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_opa(btn, LV_OPA_40, 0);
    } else if (!selected) {
      lv_obj_add_event_cb(btn, onTimeoutPicked, LV_EVENT_CLICKED,
                          reinterpret_cast<void *>(static_cast<intptr_t>(dimRow ? -minutes : minutes)));
    }
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(selected ? COL_BG : (allowed ? COL_TEXT : COL_MUTED)), 0);
    lv_label_set_text(lbl, (String(minutes) + " min").c_str());
    lv_obj_center(lbl);
  }
}

void showDisplay() {
  pane = Pane::Display;
  clearContent();
  lv_label_set_text(titleLabel, "Display");
  setStatus("Dims first, then turns off. A tap wakes the screen without pressing a control.",
            COL_MUTED);

  lv_obj_t *col = makeScroll();
  lv_obj_t *card = makeCard(col);
  addTimeoutRow(card, "Dim after", true);
  addTimeoutRow(card, "Turn off after", false);
  makeAction(col, "Back", false, onBackHome, nullptr);
}

void showDevice() {
  pane = Pane::Device;
  clearContent();
  lv_label_set_text(titleLabel, "Device");
  setStatus("Hostname and the OTA / config password.", COL_MUTED);

  lv_obj_t *col = makeScroll();
  lv_obj_t *card = makeCard(col);
  addField(card, "Hostname", config().otaHostname.c_str(), false);
  addField(card, "OTA / config password", "", true);
  makeAction(col, "Save & reboot", true, onSave, nullptr);
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
  lv_label_set_text(titleLabel, "Menu");

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

void menu_close() {
  if (overlay) {
    clearContent();
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  }
}

bool menu_is_open() { return overlay && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN); }
