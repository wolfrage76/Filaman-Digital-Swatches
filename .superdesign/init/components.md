# Shared UI primitives

Framework: embedded LVGL 8 on ESP32 (C++). There is no React/Tailwind component library.
The panel is a fixed 800×480 landscape touchscreen. Shared visuals live in `firmware/src/ui/ui.cpp` and `firmware/src/ui/menu_ui.cpp`.
Fonts in use: Montserrat 12, 14, 16, 18, 22, 28. Icons are LVGL symbols (`LV_SYMBOL_WIFI`, `LV_SYMBOL_LEFT`, `LV_SYMBOL_LIST`, `LV_SYMBOL_CLOSE`, `LV_SYMBOL_WARNING`) plus two small climate images (thermometer, drop).

## makeLabel
- Path: `firmware/src/ui/ui.cpp`
- One-line label that dots when it overflows.

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, lv_pct(100));
  return label;
}

## makeMetricTile
- Path: `firmware/src/ui/ui.cpp`
- Caption-over-value tile used for nozzle, bed, and chamber.

```cpp
MetricTile makeMetricTile(lv_obj_t *parent, const char *caption) {
  MetricTile tile;
  tile.root = lv_obj_create(parent);
  lv_obj_add_style(tile.root, &cardStyle, 0);
  lv_obj_set_flex_grow(tile.root, 1);
  lv_obj_set_height(tile.root, 64);
  lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(tile.root, 8, 0);
  lv_obj_set_style_pad_row(tile.root, 2, 0);
  lv_obj_clear_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

  tile.caption = makeLabel(tile.root, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  lv_label_set_text(tile.caption, caption);
  tile.value = makeLabel(tile.root, &lv_font_montserrat_14, lv_color_hex(COL_TEXT));
  lv_label_set_text(tile.value, "--");
  return tile;
```

## initStyles
- Path: `firmware/src/ui/ui.cpp`
- Shared styles: column card, active column, empty column, card, hero, pill, nav chip, active nav chip.

```cpp
void initStyles() {
  lv_style_init(&columnStyle);
  lv_style_set_bg_color(&columnStyle, lv_color_hex(COL_SURFACE));
  lv_style_set_bg_opa(&columnStyle, LV_OPA_COVER);
  lv_style_set_border_width(&columnStyle, 1);
  lv_style_set_border_color(&columnStyle, lv_color_hex(COL_BORDER));
  lv_style_set_radius(&columnStyle, 8);
  lv_style_set_clip_corner(&columnStyle, false);

  lv_style_init(&activeColumnStyle);
  lv_style_set_bg_color(&activeColumnStyle, lv_color_hex(0x132028));
  lv_style_set_border_width(&activeColumnStyle, 2);
  lv_style_set_border_color(&activeColumnStyle, lv_color_hex(COL_ACCENT));

  lv_style_init(&emptyColumnStyle);
  lv_style_set_bg_color(&emptyColumnStyle, lv_color_hex(0x0B0F15));
  lv_style_set_border_color(&emptyColumnStyle, lv_color_hex(COL_BORDER_SOFT));

  lv_style_init(&cardStyle);
  lv_style_set_bg_color(&cardStyle, lv_color_hex(COL_SURFACE));
  lv_style_set_bg_opa(&cardStyle, LV_OPA_COVER);
  lv_style_set_border_width(&cardStyle, 1);
  lv_style_set_border_color(&cardStyle, lv_color_hex(COL_BORDER));
  lv_style_set_radius(&cardStyle, 0);

  lv_style_init(&heroStyle);
  lv_style_set_bg_color(&heroStyle, lv_color_hex(COL_SURFACE_2));
  lv_style_set_bg_opa(&heroStyle, LV_OPA_COVER);
  lv_style_set_border_width(&heroStyle, 1);
  lv_style_set_border_color(&heroStyle, lv_color_hex(COL_BORDER));
  lv_style_set_radius(&heroStyle, 16);

  lv_style_init(&pillStyle);
  lv_style_set_bg_color(&pillStyle, lv_color_hex(COL_ACCENT));
  lv_style_set_bg_opa(&pillStyle, LV_OPA_COVER);
  lv_style_set_radius(&pillStyle, 14);
  lv_style_set_border_width(&pillStyle, 0);

  lv_style_init(&navChipStyle);
  lv_style_set_bg_color(&navChipStyle, lv_color_hex(COL_SURFACE));
  lv_style_set_bg_opa(&navChipStyle, LV_OPA_COVER);
  lv_style_set_border_width(&navChipStyle, 1);
  lv_style_set_border_color(&navChipStyle, lv_color_hex(COL_BORDER));
  lv_style_set_radius(&navChipStyle, 10);

  lv_style_init(&navChipActiveStyle);
  lv_style_set_bg_color(&navChipActiveStyle, lv_color_hex(COL_ACCENT));
  lv_style_set_bg_opa(&navChipActiveStyle, LV_OPA_COVER);
  lv_style_set_border_color(&navChipActiveStyle, lv_color_hex(COL_ACCENT));
```

## Slot card (buildColumn)
- Path: `firmware/src/ui/ui.cpp`
- Detail-page tray card. Left color rail (68px) with bay badge and effect badge. Right body: slot eyebrow, RFID and material pills, color name, profile name, BRAND / MIN/MAX / BED spec strip, remaining label and 8px bar.

```cpp
void buildColumn(lv_obj_t *parent, int index) {
  ColumnWidgets &widgets = columns[index];
  widgets.root = lv_obj_create(parent);
  lv_obj_add_style(widgets.root, &columnStyle, 0);
  lv_obj_set_flex_flow(widgets.root, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(widgets.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(widgets.root, 8, 0);
  lv_obj_set_style_pad_column(widgets.root, 12, 0);
  lv_obj_set_flex_grow(widgets.root, 0);
  lv_obj_set_width(widgets.root, (SCREEN_WIDTH - 8 - 8) / 2);
  lv_obj_set_height(widgets.root, lv_pct(48));
  lv_obj_clear_flag(widgets.root, LV_OBJ_FLAG_SCROLLABLE);

  // Wide filament swatch column on the left of the card
  widgets.colorRail = lv_obj_create(widgets.root);
  lv_obj_remove_style_all(widgets.colorRail);
  lv_obj_set_width(widgets.colorRail, 68);
  lv_obj_set_height(widgets.colorRail, lv_pct(100));
  lv_obj_set_style_bg_opa(widgets.colorRail, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(widgets.colorRail, lv_color_hex(0x161E29), 0);
  lv_obj_set_style_border_width(widgets.colorRail, 1, 0);
  lv_obj_set_style_border_color(widgets.colorRail, lv_color_hex(COL_BORDER_SOFT), 0);
  lv_obj_set_style_radius(widgets.colorRail, 0, 0);
  lv_obj_clear_flag(widgets.colorRail, LV_OBJ_FLAG_SCROLLABLE);

  // Floating bay code pill anchored at top-left of the swatch column (e.g. A1..A4)
  widgets.railBadge = lv_obj_create(widgets.colorRail);
  lv_obj_remove_style_all(widgets.railBadge);
  lv_obj_add_flag(widgets.railBadge, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_pos(widgets.railBadge, 5, 5);
  lv_obj_set_size(widgets.railBadge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(widgets.railBadge, lv_color_hex(0x0A0E14), 0);
  lv_obj_set_style_bg_opa(widgets.railBadge, LV_OPA_80, 0);
  lv_obj_set_style_border_width(widgets.railBadge, 1, 0);
  lv_obj_set_style_border_color(widgets.railBadge, lv_color_hex(0x2A3748), 0);
  lv_obj_set_style_radius(widgets.railBadge, 4, 0);
  lv_obj_set_style_pad_hor(widgets.railBadge, 6, 0);
  lv_obj_set_style_pad_ver(widgets.railBadge, 2, 0);
  lv_obj_clear_flag(widgets.railBadge, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(widgets.railBadge, LV_OBJ_FLAG_CLICKABLE);

  widgets.railBadgeLabel = lv_label_create(widgets.railBadge);
  lv_obj_set_style_text_font(widgets.railBadgeLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(widgets.railBadgeLabel, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(widgets.railBadgeLabel, "A1");

  // Finish/additive legend over the visual treatment. Multiple properties
  // stack vertically (for example GLOW + CF) without consuming body space.
  widgets.railEffectBadge = lv_obj_create(widgets.colorRail);
  lv_obj_remove_style_all(widgets.railEffectBadge);
  lv_obj_add_flag(widgets.railEffectBadge, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_width(widgets.railEffectBadge, 58);
  lv_obj_set_height(widgets.railEffectBadge, LV_SIZE_CONTENT);
  lv_obj_align(widgets.railEffectBadge, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_color(widgets.railEffectBadge, lv_color_hex(0x070A0F), 0);
  lv_obj_set_style_bg_opa(widgets.railEffectBadge, LV_OPA_80, 0);
  lv_obj_set_style_border_width(widgets.railEffectBadge, 1, 0);
  lv_obj_set_style_border_color(widgets.railEffectBadge, lv_color_hex(0x42536A), 0);
  lv_obj_set_style_radius(widgets.railEffectBadge, 4, 0);
  lv_obj_set_style_pad_hor(widgets.railEffectBadge, 3, 0);
  lv_obj_set_style_pad_ver(widgets.railEffectBadge, 2, 0);
  lv_obj_clear_flag(widgets.railEffectBadge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  widgets.railEffectLabel = lv_label_create(widgets.railEffectBadge);
  lv_obj_set_width(widgets.railEffectLabel, lv_pct(100));
  lv_obj_set_style_text_font(widgets.railEffectLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(widgets.railEffectLabel, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_align(widgets.railEffectLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(widgets.railEffectLabel, LV_LABEL_LONG_WRAP);
  lv_label_set_text(widgets.railEffectLabel, "");

  // Right structured telemetry & identity body
  lv_obj_t *body = lv_obj_create(widgets.root);
  lv_obj_remove_style_all(body);
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_height(body, lv_pct(100));
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(body, 2, 0);
  lv_obj_set_style_pad_row(body, 2, 0);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

  // 1. Top Header Bar: Slot Eyebrow (left) + Status & Material Badges (right)
  lv_obj_t *topRow = lv_obj_create(body);
  lv_obj_remove_style_all(topRow);
  lv_obj_set_width(topRow, lv_pct(100));
  lv_obj_set_height(topRow, 22);
  lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

  widgets.title = lv_label_create(topRow);
  lv_obj_set_style_text_font(widgets.title, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(widgets.title, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(widgets.title, "SLOT 1");

  lv_obj_t *badgeRow = lv_obj_create(topRow);
  lv_obj_remove_style_all(badgeRow);
  lv_obj_set_size(badgeRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(badgeRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(badgeRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(badgeRow, 5, 0);
  lv_obj_clear_flag(badgeRow, LV_OBJ_FLAG_SCROLLABLE);

  widgets.statusPill = lv_obj_create(badgeRow);
  lv_obj_remove_style_all(widgets.statusPill);
  lv_obj_set_size(widgets.statusPill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(widgets.statusPill, lv_color_hex(COL_SURFACE_2), 0);
  lv_obj_set_style_bg_opa(widgets.statusPill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(widgets.statusPill, 1, 0);
  lv_obj_set_style_border_color(widgets.statusPill, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(widgets.statusPill, 4, 0);
  lv_obj_set_style_pad_hor(widgets.statusPill, 6, 0);
  lv_obj_set_style_pad_ver(widgets.statusPill, 2, 0);
  lv_obj_clear_flag(widgets.statusPill, LV_OBJ_FLAG_SCROLLABLE);

  widgets.meta = lv_label_create(widgets.statusPill);
  lv_obj_set_style_text_font(widgets.meta, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(widgets.meta, lv_color_hex(COL_MUTED), 0);
  lv_label_set_text(widgets.meta, "RFID");

  widgets.materialPill = lv_obj_create(badgeRow);
  lv_obj_remove_style_all(widgets.materialPill);
  lv_obj_set_size(widgets.materialPill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(widgets.materialPill, lv_color_hex(0x132624), 0);
  lv_obj_set_style_bg_opa(widgets.materialPill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(widgets.materialPill, 1, 0);
  lv_obj_set_style_border_color(widgets.materialPill, lv_color_hex(0x264D44), 0);
  lv_obj_set_style_radius(widgets.materialPill, 4, 0);
  lv_obj_set_style_pad_hor(widgets.materialPill, 7, 0);
  lv_obj_set_style_pad_ver(widgets.materialPill, 2, 0);
  lv_obj_clear_flag(widgets.materialPill, LV_OBJ_FLAG_SCROLLABLE);

  widgets.material = lv_label_create(widgets.materialPill);
  lv_obj_set_style_text_font(widgets.material, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(widgets.material, lv_color_hex(COL_ACCENT), 0);
  lv_label_set_text(widgets.material, "PLA");

  // 2. Primary Filament Identity Block: Color Name + Profile Base Name
  lv_obj_t *identityBox = lv_obj_create(body);
  lv_obj_remove_style_all(identityBox);
  lv_obj_set_width(identityBox, lv_pct(100));
  lv_obj_set_height(identityBox, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(identityBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(identityBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(identityBox, 2, 0);
  lv_obj_clear_flag(identityBox, LV_OBJ_FLAG_SCROLLABLE);

  widgets.colorName = makeLabel(identityBox, &lv_font_montserrat_18, lv_color_hex(COL_TEXT));
  lv_label_set_long_mode(widgets.colorName, LV_LABEL_LONG_DOT);
  lv_obj_set_width(widgets.colorName, lv_pct(100));
  lv_obj_set_style_text_align(widgets.colorName, LV_TEXT_ALIGN_LEFT, 0);

  widgets.profileName = makeLabel(identityBox, &lv_font_montserrat_12, lv_color_hex(COL_MUTED));
  lv_label_set_long_mode(widgets.profileName, LV_LABEL_LONG_DOT);
  lv_obj_set_width(widgets.profileName, lv_pct(100));
  lv_obj_set_style_text_align(widgets.profileName, LV_TEXT_ALIGN_LEFT, 0);

  // 3. Structured Spec Strip: three compact instrument-style columns.
  lv_obj_t *specRow = lv_obj_create(body);
  lv_obj_remove_style_all(specRow);
  lv_obj_set_width(specRow, lv_pct(100));
  lv_obj_set_height(specRow, 38);
  lv_obj_set_style_bg_color(specRow, lv_color_hex(0x0D1219), 0);
  lv_obj_set_style_bg_opa(specRow, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(specRow, 1, 0);
  lv_obj_set_style_border_color(specRow, lv_color_hex(COL_BORDER_SOFT), 0);
  lv_obj_set_style_radius(specRow, 4, 0);
  lv_obj_set_style_pad_hor(specRow, 7, 0);
  lv_obj_set_style_pad_ver(specRow, 3, 0);
  lv_obj_set_style_pad_column(specRow, 5, 0);
  lv_obj_set_flex_flow(specRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(specRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(specRow, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *brandBox = lv_obj_create(specRow);
  lv_obj_remove_style_all(brandBox);
  lv_obj_set_width(brandBox, 0);
  lv_obj_set_height(brandBox, lv_pct(100));
  lv_obj_set_flex_grow(brandBox, 1);
  lv_obj_set_flex_flow(brandBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(brandBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(brandBox, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *brandKey = lv_label_create(brandBox);
  lv_obj_set_style_text_font(brandKey, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(brandKey, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(brandKey, "BRAND");

  widgets.manufacturer = lv_label_create(brandBox);
  lv_obj_set_style_text_font(widgets.manufacturer, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(widgets.manufacturer, lv_color_hex(COL_TEXT), 0);
  lv_label_set_long_mode(widgets.manufacturer, LV_LABEL_LONG_DOT);
  lv_obj_set_width(widgets.manufacturer, lv_pct(100));
  lv_label_set_text(widgets.manufacturer, "--");

  lv_obj_t *divider1 = lv_obj_create(specRow);
  lv_obj_remove_style_all(divider1);
  lv_obj_set_size(divider1, 1, 24);
  lv_obj_set_style_bg_color(divider1, lv_color_hex(COL_BORDER_SOFT), 0);
  lv_obj_set_style_bg_opa(divider1, LV_OPA_COVER, 0);

  lv_obj_t *nozzleBox = lv_obj_create(specRow);
  lv_obj_remove_style_all(nozzleBox);
  lv_obj_set_width(nozzleBox, 0);
  lv_obj_set_height(nozzleBox, lv_pct(100));
  lv_obj_set_flex_grow(nozzleBox, 1);
  lv_obj_set_flex_flow(nozzleBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(nozzleBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(nozzleBox, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *nozzleKey = lv_label_create(nozzleBox);
  lv_obj_set_style_text_font(nozzleKey, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(nozzleKey, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(nozzleKey, "MIN/MAX");

  widgets.temps = lv_label_create(nozzleBox);
  lv_obj_set_style_text_font(widgets.temps, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(widgets.temps, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(widgets.temps, "--");

  lv_obj_t *divider2 = lv_obj_create(specRow);
  lv_obj_remove_style_all(divider2);
  lv_obj_set_size(divider2, 1, 24);
  lv_obj_set_style_bg_color(divider2, lv_color_hex(COL_BORDER_SOFT), 0);
  lv_obj_set_style_bg_opa(divider2, LV_OPA_COVER, 0);

  lv_obj_t *bedBox = lv_obj_create(specRow);
  lv_obj_remove_style_all(bedBox);
  lv_obj_set_width(bedBox, 0);
  lv_obj_set_height(bedBox, lv_pct(100));
  lv_obj_set_flex_grow(bedBox, 1);
  lv_obj_set_flex_flow(bedBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(bedBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_clear_flag(bedBox, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *bedKey = lv_label_create(bedBox);
  lv_obj_set_style_text_font(bedKey, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(bedKey, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(bedKey, "BED");

  widgets.bedTemp = lv_label_create(bedBox);
  lv_obj_set_style_text_font(widgets.bedTemp, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(widgets.bedTemp, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(widgets.bedTemp, "--");

  // 4. Bottom Spool Remaining Gauge: Header Row + Progress Bar
  lv_obj_t *gaugeBox = lv_obj_create(body);
  lv_obj_remove_style_all(gaugeBox);
  lv_obj_set_width(gaugeBox, lv_pct(100));
  lv_obj_set_height(gaugeBox, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(gaugeBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(gaugeBox, 4, 0);
  lv_obj_clear_flag(gaugeBox, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *gaugeHead = lv_obj_create(gaugeBox);
  lv_obj_remove_style_all(gaugeHead);
  lv_obj_set_width(gaugeHead, lv_pct(100));
  lv_obj_set_height(gaugeHead, 16);
  lv_obj_set_flex_flow(gaugeHead, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(gaugeHead, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(gaugeHead, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *remKey = lv_label_create(gaugeHead);
  lv_obj_set_style_text_font(remKey, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(remKey, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(remKey, "REMAINING");

  widgets.remaining = lv_label_create(gaugeHead);
  lv_obj_set_style_text_font(widgets.remaining, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(widgets.remaining, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(widgets.remaining, "--");

  widgets.remainingBar = lv_bar_create(gaugeBox);
  lv_obj_set_width(widgets.remainingBar, lv_pct(100));
  lv_obj_set_height(widgets.remainingBar, 8);
  lv_bar_set_range(widgets.remainingBar, 0, 100);
  lv_obj_set_style_bg_color(widgets.remainingBar, lv_color_hex(0x0B0F15), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(widgets.remainingBar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(widgets.remainingBar, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(widgets.remainingBar, lv_color_hex(COL_BORDER_SOFT), LV_PART_MAIN);
  lv_obj_set_style_bg_color(widgets.remainingBar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(widgets.remainingBar, 3, LV_PART_MAIN);
  lv_obj_set_style_radius(widgets.remainingBar, 3, LV_PART_INDICATOR);
```

## Overview bay
- Path: `firmware/src/ui/ui.cpp`
- Compact swatch cell inside an AMS unit card.

```cpp
void buildOverviewBay(OverviewBay &bay, lv_obj_t *parent, bool withClimate) {
  bay.root = lv_obj_create(parent);
  lv_obj_remove_style_all(bay.root);
  lv_obj_set_style_bg_opa(bay.root, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(bay.root, lv_color_hex(COL_AMS_SOFT), 0);
  lv_obj_set_style_border_width(bay.root, 1, 0);
  lv_obj_set_style_border_color(bay.root, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_radius(bay.root, 0, 0);
  lv_obj_set_style_min_width(bay.root, 0, 0);
  lv_obj_set_flex_flow(bay.root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_grow(bay.root, 1);
  lv_obj_set_height(bay.root, lv_pct(100));
  lv_obj_clear_flag(bay.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(bay.root, LV_OBJ_FLAG_CLICKABLE);

  bay.swatch = lv_obj_create(bay.root);
  lv_obj_remove_style_all(bay.swatch);
  lv_obj_set_width(bay.swatch, lv_pct(100));
  lv_obj_set_height(bay.swatch, 34);
  lv_obj_set_style_bg_opa(bay.swatch, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(bay.swatch, 6, 0);
  lv_obj_set_style_pad_ver(bay.swatch, 4, 0);
  lv_obj_set_style_clip_corner(bay.swatch, true, 0);
  lv_obj_set_flex_flow(bay.swatch, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bay.swatch, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(bay.swatch, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(bay.swatch, LV_OBJ_FLAG_CLICKABLE);

  bay.bayLabel = lv_label_create(bay.swatch);
  lv_obj_set_style_text_font(bay.bayLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(bay.bayLabel, lv_color_hex(COL_TEXT), 0);
  lv_label_set_long_mode(bay.bayLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(bay.bayLabel, lv_pct(100));
  // Inset the slot id only. Padding the swatch also shifts the color fill.
  lv_obj_set_style_pad_left(bay.bayLabel, 10, 0);
  lv_obj_move_foreground(bay.bayLabel);

  bay.material = makeLabel(bay.root, &lv_font_montserrat_14, lv_color_hex(COL_TEXT));
  lv_obj_set_style_pad_hor(bay.material, 6, 0);
  bay.desc = makeLabel(bay.root, &lv_font_montserrat_12, lv_color_hex(COL_MUTED));
  lv_obj_set_style_pad_hor(bay.desc, 6, 0);
  lv_label_set_long_mode(bay.desc, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(bay.desc, lv_pct(100));
  lv_obj_set_flex_grow(bay.desc, 1);

  if (withClimate) {
    bay.climate = makeLabel(bay.root, &lv_font_montserrat_12, lv_color_hex(COL_MUTED));
    lv_obj_set_style_pad_hor(bay.climate, 6, 0);
    lv_obj_add_flag(bay.climate, LV_OBJ_FLAG_HIDDEN);
  }

  bay.brand = makeLabel(bay.root, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  lv_obj_set_style_pad_hor(bay.brand, 6, 0);
  lv_label_set_long_mode(bay.brand, LV_LABEL_LONG_DOT);
  lv_obj_set_width(bay.brand, lv_pct(100));
  lv_obj_add_flag(bay.brand, LV_OBJ_FLAG_HIDDEN);

  bay.remainingRow = lv_obj_create(bay.root);
  lv_obj_remove_style_all(bay.remainingRow);
  lv_obj_set_width(bay.remainingRow, lv_pct(100));
  lv_obj_set_height(bay.remainingRow, 12);
  lv_obj_set_style_pad_hor(bay.remainingRow, 6, 0);
  lv_obj_set_flex_flow(bay.remainingRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bay.remainingRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                       LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(bay.remainingRow, 4, 0);
  lv_obj_clear_flag(bay.remainingRow, LV_OBJ_FLAG_SCROLLABLE);

  bay.remainingBar = lv_bar_create(bay.remainingRow);
  lv_obj_set_flex_grow(bay.remainingBar, 1);
  lv_obj_set_height(bay.remainingBar, 4);
  lv_bar_set_range(bay.remainingBar, 0, 100);
  lv_obj_set_style_bg_color(bay.remainingBar, lv_color_hex(0x0B1410), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bay.remainingBar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);

  bay.pct = lv_label_create(bay.remainingRow);
  lv_obj_set_style_text_font(bay.pct, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(bay.pct, lv_color_hex(COL_MUTED), 0);
  // Fixed width so "100%" and "1000g" swap without resizing the bar.
  lv_obj_set_width(bay.pct, 48);
  lv_obj_set_style_text_align(bay.pct, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(bay.pct, "");
}
```
