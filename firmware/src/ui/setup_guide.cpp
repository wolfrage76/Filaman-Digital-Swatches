#include "setup_guide.h"

#include <lvgl.h>

#include "menu_ui.h"
#include "touch_input.h"

// Step 2 of first-run setup. The QR code opens the panel's own config page
// (http://<ip>/), where the FilaMan card is first. Saving there reboots the
// panel, and the guide does not return once FilaMan is configured.

namespace {

constexpr uint32_t COL_BG = 0x0A0E14;
constexpr uint32_t COL_SURFACE_2 = 0x182028;
constexpr uint32_t COL_BORDER = 0x2A3544;
constexpr uint32_t COL_TEXT = 0xF2F5F8;
constexpr uint32_t COL_MUTED = 0x8B98A8;
constexpr uint32_t COL_ACCENT = 0x3DDC97;
constexpr lv_coord_t kQrSize = 220;

lv_obj_t *overlay = nullptr;
lv_obj_t *urlLabel = nullptr;
lv_obj_t *qr = nullptr;
bool dismissed = false;

void waitRelease() {
  touch_input_cooldown(280);
  if (lv_indev_t *indev = lv_indev_get_act()) {
    lv_indev_wait_release(indev);
  }
}

lv_obj_t *makeLabel(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

lv_obj_t *makeButton(lv_obj_t *parent, const char *text, bool primary, lv_event_cb_t cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, lv_pct(100));
  lv_obj_set_style_min_height(btn, 48, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(primary ? COL_ACCENT : COL_BORDER), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? COL_ACCENT : COL_SURFACE_2), 0);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(primary ? COL_BG : COL_TEXT), 0);
  lv_label_set_text(lbl, text);
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
  return btn;
}

void onEnterHere(lv_event_t *) {
  setup_guide_close();
  menu_open_filaman();
}

void onSkip(lv_event_t *) {
  dismissed = true;
  setup_guide_close();
}

}  // namespace

void setup_guide_begin() {
  overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(overlay);
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(overlay, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(overlay, 24, 0);
  lv_obj_set_style_pad_column(overlay, 28, 0);
  lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(overlay, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);

  // Left: instructions and actions.
  lv_obj_t *text = lv_obj_create(overlay);
  lv_obj_remove_style_all(text);
  lv_obj_set_height(text, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(text, 1);
  lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(text, 10, 0);
  lv_obj_clear_flag(text, LV_OBJ_FLAG_SCROLLABLE);

  makeLabel(text, "STEP 2 OF 2", &lv_font_montserrat_14, COL_ACCENT);
  makeLabel(text, "Connect FilaMan", &lv_font_montserrat_28, COL_TEXT);
  makeLabel(text,
            "WiFi is connected. Now add your FilaMan server address and an API key "
            "so the panel can show your spools.",
            &lv_font_montserrat_16, COL_MUTED);
  makeLabel(text,
            "Scan the code with your phone, or open this address in a browser on the "
            "same network:",
            &lv_font_montserrat_16, COL_MUTED);
  urlLabel = makeLabel(text, "", &lv_font_montserrat_22, COL_ACCENT);

  lv_obj_t *spacer = lv_obj_create(text);
  lv_obj_remove_style_all(spacer);
  lv_obj_set_size(spacer, 1, 6);

  makeButton(text, "Enter on this display", false, onEnterHere);
  makeButton(text, "Skip for now", false, onSkip);

  // Right: QR code on a white quiet zone so phone cameras pick it up.
  lv_obj_t *qrCard = lv_obj_create(overlay);
  lv_obj_remove_style_all(qrCard);
  lv_obj_set_size(qrCard, kQrSize + 32, kQrSize + 32);
  lv_obj_set_style_bg_color(qrCard, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(qrCard, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(qrCard, 12, 0);
  lv_obj_clear_flag(qrCard, LV_OBJ_FLAG_SCROLLABLE);

  qr = lv_qrcode_create(qrCard, kQrSize, lv_color_black(), lv_color_white());
  lv_obj_center(qr);
}

void setup_guide_open(const String &ip) {
  if (!overlay) {
    return;
  }
  const String url = String("http://") + ip + "/";
  lv_label_set_text(urlLabel, url.c_str());
  lv_qrcode_update(qr, url.c_str(), url.length());
  waitRelease();
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(overlay);
}

void setup_guide_close() {
  if (overlay) {
    waitRelease();
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  }
}

bool setup_guide_is_open() { return overlay && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN); }

bool setup_guide_dismissed() { return dismissed; }
