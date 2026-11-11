# Layouts

Fixed chrome built in `ui_begin()`. Canvas is 800×480 with 4px page padding.
Stack: header (44px) → optional alert banner (28px, hidden until an alert) → page content (flex grow) → no persistent footer in the current render. `buildNavFooter` exists but is not called from `ui_begin`.

Header, left to right:
- Back button, 48×36, hidden on AMS View. Returns to AMS View.
- Title, Montserrat 22. "AMS View", "AMS A"–"AMS D", "AMS-HT", "Alerts", or the printer name on Print Status.
- Status/climate string, Montserrat 12 muted. On AMS View, tapping it opens Print Status.
- Wi-Fi icon. Green when FilaMan is linked, amber while connecting, red × when Wi-Fi failed. Tap opens Wi-Fi setup. Also shows an IP/SSID toast.
- Firmware version, Montserrat 12 dim. Tap opens Firmware Update.
- Menu button, 56×36, hamburger. Green 10px badge when an update is available.

```cpp
void ui_begin() {
  ensureDefaults();
  initStyles();
  lastActivityMs = millis();

  screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  lv_obj_t *root = lv_obj_create(screen);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_style_pad_all(root, 4, 0);
  lv_obj_set_style_pad_row(root, 4, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  // Fixed chrome: [back+title+status ...] [wifi+ver] [menu].
  // Left grows and absorbs remaining width; center/right stay content-sized so
  // showing the back button cannot collapse the title off-screen.
  headerBar = lv_obj_create(root);
  lv_obj_remove_style_all(headerBar);
  lv_obj_set_width(headerBar, lv_pct(100));
  lv_obj_set_height(headerBar, 44);
  lv_obj_set_flex_flow(headerBar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(headerBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(headerBar, 8, 0);
  lv_obj_set_style_clip_corner(headerBar, true, 0);
  lv_obj_clear_flag(headerBar, LV_OBJ_FLAG_SCROLLABLE);

  headerLeft = lv_obj_create(headerBar);
  lv_obj_remove_style_all(headerLeft);
  lv_obj_set_width(headerLeft, 0);
  lv_obj_set_flex_grow(headerLeft, 1);
  lv_obj_set_height(headerLeft, 44);
  lv_obj_set_flex_flow(headerLeft, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(headerLeft, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(headerLeft, 8, 0);
  lv_obj_set_style_pad_left(headerLeft, 2, 0);
  lv_obj_set_style_clip_corner(headerLeft, true, 0);
  lv_obj_clear_flag(headerLeft, LV_OBJ_FLAG_SCROLLABLE);

  backBtn = lv_btn_create(headerLeft);
  lv_obj_set_size(backBtn, 48, 36);
  lv_obj_set_style_bg_color(backBtn, lv_color_hex(COL_SURFACE_2), 0);
  lv_obj_set_style_bg_opa(backBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(backBtn, 1, 0);
  lv_obj_set_style_border_color(backBtn, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(backBtn, 8, 0);
  lv_obj_set_style_shadow_width(backBtn, 0, 0);
  lv_obj_set_style_pad_all(backBtn, 0, 0);
  lv_obj_set_ext_click_area(backBtn, 16);
  lv_obj_add_event_cb(backBtn, backEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(backBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *backIcon = lv_label_create(backBtn);
  lv_obj_set_style_text_font(backIcon, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(backIcon, lv_color_hex(COL_ACCENT), 0);
  lv_label_set_text(backIcon, LV_SYMBOL_LEFT);
  lv_obj_center(backIcon);

  titleLabel = lv_label_create(headerLeft);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(COL_TEXT), 0);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(titleLabel, 140);
  lv_obj_set_style_min_width(titleLabel, 72, 0);
  // Keep the title from flex-shrinking when stateLabel text arrives later.
  lv_obj_set_flex_grow(titleLabel, 0);

  stateLabel = lv_label_create(headerLeft);
  lv_obj_set_style_text_font(stateLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(stateLabel, lv_color_hex(COL_MUTED), 0);
  lv_label_set_long_mode(stateLabel, LV_LABEL_LONG_DOT);
  // width 0 + flex_grow is required for LONG_DOT to shrink inside a flex row;
  // without it a long climate/status string can inflate headerLeft past the
  // screen and leave title/back undrawn until a full reboot layout pass.
  lv_obj_set_width(stateLabel, 0);
  lv_obj_set_flex_grow(stateLabel, 1);
  lv_obj_add_flag(stateLabel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(stateLabel, 10);
  lv_obj_add_event_cb(stateLabel, printerStatusEvent, LV_EVENT_CLICKED, nullptr);

  // Freshness indicator ("Live" / "Ns ago" / "Stale") removed per request.
  // freshLabel stays null; refreshFreshness() no-ops safely on the null guard.

  lv_obj_t *headerCenter = lv_obj_create(headerBar);
  lv_obj_remove_style_all(headerCenter);
  lv_obj_set_size(headerCenter, LV_SIZE_CONTENT, 44);
  lv_obj_set_flex_flow(headerCenter, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(headerCenter, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(headerCenter, 6, 0);
  lv_obj_clear_flag(headerCenter, LV_OBJ_FLAG_SCROLLABLE);

  connIcon = lv_label_create(headerCenter);
  lv_obj_set_style_text_font(connIcon, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(connIcon, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(connIcon, LV_SYMBOL_WIFI);
  lv_obj_add_flag(connIcon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(connIcon, 14);
  lv_obj_add_event_cb(connIcon, connIconEvent, LV_EVENT_CLICKED, nullptr);

  versionLabel = lv_label_create(headerCenter);
  lv_obj_set_style_text_font(versionLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(versionLabel, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(versionLabel, (String("v") + FIRMWARE_VERSION).c_str());
  lv_obj_add_flag(versionLabel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(versionLabel, 12);
  lv_obj_add_event_cb(versionLabel, versionEvent, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *headerRight = lv_obj_create(headerBar);
  lv_obj_remove_style_all(headerRight);
  lv_obj_set_size(headerRight, LV_SIZE_CONTENT, 44);
  lv_obj_set_flex_flow(headerRight, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(headerRight, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_right(headerRight, 2, 0);
  lv_obj_clear_flag(headerRight, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *menuBtn = lv_btn_create(headerRight);
  lv_obj_set_size(menuBtn, 56, 36);
  lv_obj_set_style_bg_color(menuBtn, lv_color_hex(COL_SURFACE_2), 0);
  lv_obj_set_style_bg_opa(menuBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(menuBtn, 1, 0);
  lv_obj_set_style_border_color(menuBtn, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(menuBtn, 8, 0);
  lv_obj_set_style_shadow_width(menuBtn, 0, 0);
  lv_obj_set_style_pad_all(menuBtn, 0, 0);
  lv_obj_set_ext_click_area(menuBtn, 20);
  lv_obj_add_event_cb(menuBtn, menuEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *menuIcon = lv_label_create(menuBtn);
  lv_obj_set_style_text_font(menuIcon, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(menuIcon, lv_color_hex(COL_ACCENT), 0);
  lv_label_set_text(menuIcon, LV_SYMBOL_LIST);
  lv_obj_center(menuIcon);

  menuUpdateBadge = lv_obj_create(menuBtn);
  lv_obj_remove_style_all(menuUpdateBadge);
  lv_obj_set_size(menuUpdateBadge, 10, 10);
  lv_obj_set_style_radius(menuUpdateBadge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(menuUpdateBadge, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_bg_opa(menuUpdateBadge, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(menuUpdateBadge, 0, 0);
  lv_obj_clear_flag(menuUpdateBadge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(menuUpdateBadge, LV_ALIGN_TOP_RIGHT, -5, 4);
  lv_obj_add_flag(menuUpdateBadge, LV_OBJ_FLAG_HIDDEN);

  alertBanner = lv_obj_create(root);
  lv_obj_remove_style_all(alertBanner);
  lv_obj_set_width(alertBanner, lv_pct(100));
  lv_obj_set_height(alertBanner, 28);
  lv_obj_set_style_bg_opa(alertBanner, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(alertBanner, lv_color_hex(COL_WARN), 0);
  lv_obj_set_style_radius(alertBanner, 8, 0);
  lv_obj_set_style_pad_hor(alertBanner, 10, 0);
  lv_obj_set_flex_flow(alertBanner, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(alertBanner, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(alertBanner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(alertBanner, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(alertBanner, alertBannerEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(alertBanner, LV_OBJ_FLAG_HIDDEN);

  alertBannerLabel = lv_label_create(alertBanner);
  lv_obj_set_style_text_font(alertBannerLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(alertBannerLabel, lv_color_hex(COL_BG), 0);
  lv_label_set_long_mode(alertBannerLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(alertBannerLabel, lv_pct(100));
  lv_label_set_text(alertBannerLabel, "");

  contentRoot = lv_obj_create(root);
  lv_obj_remove_style_all(contentRoot);
  lv_obj_set_width(contentRoot, lv_pct(100));
  lv_obj_set_flex_grow(contentRoot, 1);
  lv_obj_set_flex_flow(contentRoot, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(contentRoot, LV_OBJ_FLAG_SCROLLABLE);

  buildStatusPage(contentRoot);
  buildOverviewPage(contentRoot);

  columnsRoot = lv_obj_create(contentRoot);
  lv_obj_remove_style_all(columnsRoot);
  lv_obj_set_width(columnsRoot, lv_pct(100));
  lv_obj_set_flex_grow(columnsRoot, 1);
  lv_obj_set_flex_flow(columnsRoot, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(columnsRoot, 10, 0);
  lv_obj_set_style_pad_row(columnsRoot, 10, 0);
  lv_obj_set_flex_align(columnsRoot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(columnsRoot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(columnsRoot, LV_OBJ_FLAG_HIDDEN);

  for (int column = 0; column < COLUMNS_PER_PAGE; ++column) {
    buildColumn(columnsRoot, column);
  }

  buildAlertsPage(contentRoot);
  buildIpToast();
```

## Nav footer (built, not mounted on the current shell)
```cpp
void buildNavFooter(lv_obj_t *parent) {
  lv_obj_t *footer = lv_obj_create(parent);
  lv_obj_remove_style_all(footer);
  lv_obj_set_width(footer, lv_pct(100));
  lv_obj_set_height(footer, 56);
  lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(footer, 8, 0);
  lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

  navRow = lv_obj_create(footer);
  lv_obj_remove_style_all(navRow);
  lv_obj_set_flex_grow(navRow, 1);
  lv_obj_set_height(navRow, 52);
  lv_obj_set_flex_flow(navRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(navRow, 6, 0);
  lv_obj_clear_flag(navRow, LV_OBJ_FLAG_SCROLLABLE);

  for (int page = 0; page < TOTAL_PAGE_COUNT; ++page) {
    NavChip &chip = navChips[page];
    chip.page = page;
    chip.root = lv_obj_create(navRow);
    lv_obj_add_style(chip.root, &navChipStyle, 0);
    lv_obj_set_flex_grow(chip.root, 1);
    lv_obj_set_height(chip.root, 52);
    lv_obj_set_flex_flow(chip.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(chip.root, 2, 0);
    // Extend the touch area beyond the chip so the taller tabs are easy to hit.
    lv_obj_set_ext_click_area(chip.root, 6);
    lv_obj_clear_flag(chip.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(chip.root, navChipEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(page)));

    chip.label = lv_label_create(chip.root);
    lv_obj_set_style_text_font(chip.label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(chip.label, lv_color_hex(COL_MUTED), 0);
    lv_label_set_text(chip.label, navLabelForPage(page));
  }
  // Footer IP label removed per request; the IP is still available by tapping the
  // WiFi icon (toast) and in the Print-page Info panel. ipLabel stays null so
  // refreshFooter()'s setLabelIfChanged(ipLabel, ...) safely no-ops.
}
```

## Menu overlay
Full-screen overlay from `firmware/src/ui/menu_ui.cpp`. Home grid:
- Screens: Print Status, AMS View, Alerts
- Settings row: Select Printer, Wi‑Fi, FilaMan, MQTT
- Settings row: Display, Device, Update, System
Subpages: FilaMan server, MQTT broker, Display (backlight, sleep, return-to-AMS, spool alerts, orientation), Device (hostname, timezone, security), Firmware Update, System status.
