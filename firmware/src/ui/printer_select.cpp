#include "printer_select.h"

#include <Arduino.h>
#include <lvgl.h>

#include "display_poll.h"
#include "menu_ui.h"
#include "printer_list.h"
#include "touch_input.h"

// Pick a printer. With FilaMan configured, the choice is stored and the next
// Display API poll follows that printer. Otherwise it is published over MQTT.

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

void showList();

void setStatus(const String &text, uint32_t color) {
  if (!statusLabel) {
    return;
  }
  lv_label_set_text(statusLabel, text.c_str());
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(color), 0);
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

lv_obj_t *makeRowButton(lv_obj_t *parent, const String &text, lv_event_cb_t cb, void *ud,
                        uint32_t textColor = COL_TEXT) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, lv_pct(100));
  lv_obj_set_style_min_height(btn, 46, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(COL_SURFACE_2), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(textColor), 0);
  lv_label_set_text(lbl, text.c_str());
  lv_obj_center(lbl);
  if (cb) {
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
  }
  return btn;
}

// Selection ids are stable strings from the bridge; keep them alive for the
// button callbacks for as long as the list is shown.
String selIds[MAX_PRINTER_OPTIONS];

void onPickClicked(lv_event_t *e) {
  intptr_t i = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  if (i < 0 || i >= MAX_PRINTER_OPTIONS || !selIds[i].length()) {
    return;
  }
  printer_list_publish_selection(selIds[i]);
  setStatus("Switching printer... the dashboard will update shortly.", COL_ACCENT);
  lv_refr_now(NULL);
  delay(700);
  printer_select_close();
}

void onRefresh(lv_event_t *) { showList(); }

void onBackToMenu(lv_event_t *) {
  printer_select_close();
  menu_open();
}

void showList() {
  lv_obj_clean(content);
  lv_label_set_text(titleLabel, "Monitor Printer");

  PrinterOption printers[MAX_PRINTER_OPTIONS];
  String activeId;
  const int n = printer_list_get(printers, MAX_PRINTER_OPTIONS, activeId);

  lv_obj_t *col = makeColumn();

  if (n == 0) {
    setStatus(display_poll_active()
                  ? "No printers yet. Check the FilaMan URL and API key."
                  : "Waiting for the MQTT printer list.",
              COL_MUTED);
  } else {
    setStatus(String(n) + " printer(s) available", COL_MUTED);
    for (int i = 0; i < n; ++i) {
      selIds[i] = printers[i].id;
      const bool active = printers[i].active || printers[i].id == activeId;
      String label = active ? String(LV_SYMBOL_OK) + " " : String("  ");
      label += printers[i].name.length() ? printers[i].name : printers[i].id;
      if (printers[i].model.length()) {
        label += "  (" + printers[i].model + ")";
      }
      makeRowButton(col, label, onPickClicked, reinterpret_cast<void *>(static_cast<intptr_t>(i)),
                    active ? COL_ACCENT : COL_TEXT);
    }
  }

  makeRowButton(col, String(LV_SYMBOL_REFRESH) + "  Refresh", onRefresh, nullptr);
}

}  // namespace

void printer_select_begin() {
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
  lv_obj_set_height(header, 44);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(header, 10, 0);

  lv_obj_t *backBtn = lv_btn_create(header);
  lv_obj_set_size(backBtn, 48, 40);
  lv_obj_set_style_bg_color(backBtn, lv_color_hex(COL_SURFACE_2), 0);
  lv_obj_set_style_border_width(backBtn, 1, 0);
  lv_obj_set_style_border_color(backBtn, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(backBtn, 8, 0);
  lv_obj_set_style_shadow_width(backBtn, 0, 0);
  lv_obj_set_ext_click_area(backBtn, 10);
  lv_obj_add_event_cb(backBtn, onBackToMenu, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *backIcon = lv_label_create(backBtn);
  lv_obj_set_style_text_font(backIcon, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(backIcon, lv_color_hex(COL_ACCENT), 0);
  lv_label_set_text(backIcon, LV_SYMBOL_LEFT);
  lv_obj_center(backIcon);

  titleLabel = lv_label_create(header);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_flex_grow(titleLabel, 1);
  lv_label_set_text(titleLabel, "Monitor Printer");

  lv_obj_t *closeBtn = lv_btn_create(header);
  lv_obj_set_size(closeBtn, 60, 40);
  lv_obj_set_style_bg_color(closeBtn, lv_color_hex(COL_SURFACE_2), 0);
  lv_obj_set_style_border_width(closeBtn, 1, 0);
  lv_obj_set_style_border_color(closeBtn, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(closeBtn, 8, 0);
  lv_obj_add_event_cb(
      closeBtn, [](lv_event_t *) { printer_select_close(); }, LV_EVENT_CLICKED, nullptr);
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

void printer_select_open() {
  if (!overlay) {
    return;
  }
  touch_input_cooldown(280);
  if (lv_indev_t *indev = lv_indev_get_act()) {
    lv_indev_wait_release(indev);
  }
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(overlay);
  showList();
}

void printer_select_close() {
  if (overlay) {
    touch_input_cooldown(280);
    if (lv_indev_t *indev = lv_indev_get_act()) {
      lv_indev_wait_release(indev);
    }
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  }
}

bool printer_select_is_open() {
  return overlay && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}
