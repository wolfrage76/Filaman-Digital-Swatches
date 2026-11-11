# Theme tokens

Compact summary. Values are LVGL hex colors (`0xRRGGBB`) from `firmware/src/ui/ui.cpp`.

## Color
| Token | Hex | Role |
|---|---|---|
| bg | #0A0E14 | Screen background |
| surface | #121820 | Cards, nav chips |
| surface-2 | #182028 | Header buttons, hero |
| border | #2A3544 | Card and button borders |
| border-soft | #1E2834 | Spec strip, empty column |
| text | #F2F5F8 | Primary text |
| muted | #8B98A8 | Secondary text, inactive nav |
| dim | #5C6B7A | Captions |
| accent | #3DDC97 | Mint: material pills, bars, icons, linked Wi-Fi, active chip |
| warn | #FFB020 | Amber connecting / alert banner |
| danger | #FF5C6A | Low spool, Wi-Fi failure. Remaining under the low threshold paints this. |
| info | #5AA7FF | Informational |
| ams-card | #13261D | AMS unit card |
| ams-soft | #183225 | Bay background |
| ams-ink-dark | #0B0F0D | Text on a light swatch |
| glow-frame | #D6FF3A | Phosphor rim for glow filament |
| rail-fallback | #161E29 | Empty color rail |
| spec-strip | #0D1219 | Detail spec background |
| material-pill-bg | #132624 | Material pill |
| material-pill-border | #264D44 | Material pill border |
| active-column | #132028 | Tray currently printing, 2px mint border |
| empty-column | #0B0F15 | Empty tray |

Filament color is content, not chrome. Each swatch paints the spool's real color, with frost/highlight overlays for translucent, glitter, galaxy, and fiber (CF/GF).

Remaining bar: mint above 30%, amber from 15–30%, danger below 15% (`SPOOL_LOW_PERCENT` 30, `SPOOL_CRITICAL_PERCENT` 15).

## Type
Montserrat only. 12 captions and badges, 14 body and metrics, 16 nav and empty states, 18 color names and time-left, 22 titles and header icons, 28 print percent.

## Space and radius
Screen 800×480. Page pad 4. Header 44. Back button 48×36, menu 56×36, radius 8. Detail cards radius 8, pad 8, gap 10, height ~48% so they form a 2×2. Color rail 68px wide. Overview cards square radius 0, pad 6, gap 8 column / 6 row. Bay swatch height 34. Remaining bar 8px on detail, 4px on overview. Hero radius 16, height 118. Pills radius 4 (badges) or 14 (state pill). Nav chip radius 10, height 52.

## Motion
No decorative animation. Page changes are immediate. Data redraws coalesce (1s / 180ms quiet). Idle 60s returns to AMS View. Backlight dims, then blanks, unless set to stay dim.

## Raw tokens
```cpp

// Industrial charcoal + mint accent (readable on CrowPanel RGB).
constexpr uint32_t COL_BG = 0x0A0E14;
constexpr uint32_t COL_SURFACE = 0x121820;
constexpr uint32_t COL_SURFACE_2 = 0x182028;
constexpr uint32_t COL_BORDER = 0x2A3544;
constexpr uint32_t COL_BORDER_SOFT = 0x1E2834;
constexpr uint32_t COL_TEXT = 0xF2F5F8;
constexpr uint32_t COL_MUTED = 0x8B98A8;
constexpr uint32_t COL_DIM = 0x5C6B7A;
constexpr uint32_t COL_ACCENT = 0x3DDC97;
constexpr uint32_t COL_WARN = 0xFFB020;
constexpr uint32_t COL_DANGER = 0xFF5C6A;
constexpr uint32_t COL_INFO = 0x5AA7FF;
constexpr uint32_t COL_AMS_CARD = 0x13261D;
constexpr uint32_t COL_AMS_SOFT = 0x183225;
constexpr uint32_t COL_AMS_INK_DARK = 0x0B0F0D;
constexpr uint32_t COL_GLOW_FRAME = 0xD6FF3A;  // chartreuse phosphor rim (FilaMan AMS View)

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
