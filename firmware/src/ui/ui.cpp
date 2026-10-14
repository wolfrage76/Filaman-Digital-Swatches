#include "ui.h"

#include <lvgl.h>

#include "app_config.h"
#include "config.h"
#include "display_poll.h"
#include "menu_ui.h"
#include "models.h"
#include "ota_update.h"
#include "printer_select.h"
#include "touch_input.h"
#include "wifi_setup.h"

namespace {

constexpr int TOTAL_AMS_IDS = AMS2PRO_COUNT + AMSHT_MAX_COUNT;

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

struct SwatchPaint {
  uint8_t count = 0;
  uint8_t style = 0;  // SwatchStyle
  uint8_t sparkle = 0;  // SwatchSparkle
  uint8_t fiber = 0;    // SwatchFiber
  uint32_t colors[MAX_SLOT_COLORS] = {};
};

struct ColumnWidgets {
  lv_obj_t *root = nullptr;
  lv_obj_t *colorRail = nullptr;
  lv_obj_t *railFill = nullptr;
  lv_obj_t *railFrost = nullptr;
  lv_obj_t *railHighlight = nullptr;
  lv_obj_t *railOverlay = nullptr;
  lv_obj_t *railBadge = nullptr;
  lv_obj_t *railBadgeLabel = nullptr;
  lv_obj_t *railEffectBadge = nullptr;
  lv_obj_t *railEffectLabel = nullptr;
  lv_obj_t *title = nullptr;
  lv_obj_t *statusPill = nullptr;
  lv_obj_t *materialPill = nullptr;
  lv_obj_t *colorName = nullptr;
  lv_obj_t *manufacturer = nullptr;
  lv_obj_t *material = nullptr;
  lv_obj_t *profileName = nullptr;
  lv_obj_t *temps = nullptr;
  lv_obj_t *bedTemp = nullptr;
  lv_obj_t *remainingBar = nullptr;
  lv_obj_t *remaining = nullptr;
  lv_obj_t *cost = nullptr;
  lv_obj_t *meta = nullptr;
  SwatchPaint paint;
  uint32_t railSig = 0;
};

struct MetricTile {
  lv_obj_t *root = nullptr;
  lv_obj_t *caption = nullptr;
  lv_obj_t *value = nullptr;
};

struct NavChip {
  lv_obj_t *root = nullptr;
  lv_obj_t *label = nullptr;
  int page = 0;
};

struct OverviewBay {
  lv_obj_t *root = nullptr;
  lv_obj_t *swatch = nullptr;
  lv_obj_t *swatchFill = nullptr;
  lv_obj_t *frost = nullptr;
  lv_obj_t *highlight = nullptr;
  lv_obj_t *overlay = nullptr;
  lv_obj_t *bayLabel = nullptr;
  lv_obj_t *material = nullptr;
  lv_obj_t *desc = nullptr;
  lv_obj_t *climate = nullptr;
  lv_obj_t *remainingRow = nullptr;
  lv_obj_t *remainingBar = nullptr;
  lv_obj_t *pct = nullptr;
  lv_obj_t *brand = nullptr;
  SwatchPaint paint;
  uint32_t swatchSig = 0;
};

struct OverviewCard {
  lv_obj_t *root = nullptr;
  lv_obj_t *head = nullptr;
  lv_obj_t *label = nullptr;
  lv_obj_t *climateBox = nullptr;
  lv_obj_t *tempIcon = nullptr;
  lv_obj_t *tempText = nullptr;
  lv_obj_t *humIcon = nullptr;
  lv_obj_t *humText = nullptr;
  lv_obj_t *bayRow = nullptr;
  OverviewBay bays[MAX_OVERVIEW_SLOTS];
  int amsId = 0;
};

struct OverviewCardBind {
  int8_t src[MAX_OVERVIEW_SLOTS];
  uint8_t count = 0;
};

struct AlertRow {
  lv_obj_t *root = nullptr;
  lv_obj_t *title = nullptr;
  lv_obj_t *detail = nullptr;
};

struct StatusWidgets {
  lv_obj_t *root = nullptr;
  lv_obj_t *hero = nullptr;
  lv_obj_t *statePill = nullptr;
  lv_obj_t *stateLabel = nullptr;
  lv_obj_t *progressHuge = nullptr;
  lv_obj_t *timeLeftHuge = nullptr;
  lv_obj_t *jobLabel = nullptr;
  lv_obj_t *progressBar = nullptr;
  lv_obj_t *etaClockLabel = nullptr;
  MetricTile nozzleL;
  MetricTile nozzleR;
  MetricTile bed;
  MetricTile chamber;
  lv_obj_t *activeCard = nullptr;
  lv_obj_t *activeSwatch = nullptr;
  lv_obj_t *activeFill = nullptr;
  lv_obj_t *activeFrost = nullptr;
  lv_obj_t *activeHighlight = nullptr;
  lv_obj_t *activeOverlay = nullptr;
  lv_obj_t *activeLabel = nullptr;
  lv_obj_t *activeRemaining = nullptr;
  SwatchPaint paint;
  uint32_t swatchSig = 0;
  lv_obj_t *secondaryLabel = nullptr;
  lv_obj_t *nextJobLabel = nullptr;
};

SlotState slots[TOTAL_AMS_IDS][COLUMNS_PER_PAGE];
AmsMetaState metas[TOTAL_AMS_IDS];
PrinterState printer;
BridgeHealth bridge;
AlertItem alertItems[MAX_ALERTS];
OverviewUnit overviewUnits[MAX_OVERVIEW_UNITS];
int alertItemCount = 0;
int overviewUnitCount = 0;

ColumnWidgets columns[COLUMNS_PER_PAGE];
StatusWidgets statusUi;
NavChip navChips[TOTAL_PAGE_COUNT];
OverviewCard overviewCards[MAX_OVERVIEW_CARDS];
OverviewCardBind cardBinds[MAX_OVERVIEW_CARDS];
AlertRow alertRows[MAX_ALERTS];

lv_obj_t *screen = nullptr;
lv_obj_t *contentRoot = nullptr;
lv_obj_t *columnsRoot = nullptr;
lv_obj_t *overviewRoot = nullptr;
lv_obj_t *overviewMain = nullptr;
lv_obj_t *overviewAux = nullptr;
lv_obj_t *overviewHint = nullptr;
lv_obj_t *alertsRoot = nullptr;
lv_obj_t *alertsEmpty = nullptr;
lv_obj_t *alertBanner = nullptr;
lv_obj_t *alertBannerLabel = nullptr;
lv_obj_t *headerBar = nullptr;
lv_obj_t *headerLeft = nullptr;
lv_obj_t *titleLabel = nullptr;
lv_obj_t *backBtn = nullptr;
lv_obj_t *stateLabel = nullptr;
lv_obj_t *freshLabel = nullptr;
lv_obj_t *connIcon = nullptr;
lv_obj_t *versionLabel = nullptr;
lv_obj_t *menuUpdateBadge = nullptr;
lv_obj_t *ipLabel = nullptr;
lv_obj_t *navRow = nullptr;
lv_obj_t *ipToast = nullptr;
lv_obj_t *ipToastLabel = nullptr;
unsigned long ipToastUntilMs = 0;

String panelIp;
String connectionMessage;
int currentPage = AMS_OVERVIEW_PAGE;
unsigned long lastActivityMs = 0;
unsigned long lastPrinterUpdateMs = 0;
unsigned long lastFreshnessRefreshMs = 0;
unsigned long remainingPhaseMs = 0;
bool showRemainingGrams = false;
unsigned long lastUiApplyMs = 0;
bool dataDirty = false;
bool connectionDirty = false;
int applyStage = -1;

// Data-driven redraws are coalesced; footer taps stay immediate via gotoPage().
constexpr unsigned long UI_APPLY_MS = 1000;
constexpr unsigned long UI_APPLY_QUIET_MS = 180;
constexpr unsigned long IP_TOAST_MS = 4000;

lv_style_t columnStyle;
lv_style_t activeColumnStyle;
lv_style_t emptyColumnStyle;
lv_style_t cardStyle;
lv_style_t heroStyle;
lv_style_t pillStyle;
lv_style_t navChipStyle;
lv_style_t navChipActiveStyle;

const char *navLabelForPage(int page) {
  switch (page) {
    case STATUS_PAGE:
      return "Printer";
    case AMS_OVERVIEW_PAGE:
      return "All";
    case AMS2PRO_FIRST_PAGE:
      return "A";
    case AMS2PRO_FIRST_PAGE + 1:
      return "B";
    case AMS2PRO_FIRST_PAGE + 2:
      return "C";
    case AMS2PRO_FIRST_PAGE + 3:
      return "D";
    case AMS_HT_PAGE:
      return "HT";
    case ALERTS_PAGE:
      return "Alrt";
    default:
      return "?";
  }
}

uint32_t parseColor(const String &value) { return parseHexRgb(value); }

bool isStatusPage() { return currentPage == STATUS_PAGE; }
bool isOverviewPage() { return currentPage == AMS_OVERVIEW_PAGE; }
bool isAlertsPage() { return currentPage == ALERTS_PAGE; }
bool isHtPage() { return currentPage == AMS_HT_PAGE; }
bool isAmsDetailPage() {
  return currentPage >= AMS2PRO_FIRST_PAGE && currentPage < AMS_HT_PAGE;
}
int amsIdForPage() { return currentPage - AMS2PRO_FIRST_PAGE; }

bool kindHas(const String &kind, const char *token) {
  String k = kind;
  k.toLowerCase();
  return k.indexOf(token) >= 0;
}

bool isHtKind(const String &kind) { return kindHas(kind, "ht"); }
bool isExtKind(const String &kind) { return kindHas(kind, "ext"); }

bool isAuxKind(const String &kind) {
  return isHtKind(kind) || isExtKind(kind);
}

// 8x10 ALPHA_8BIT glyphs, recolored at draw time.
constexpr int ICON_W = 8;
constexpr int ICON_H = 10;
const uint8_t ICON_THERMO_MAP[ICON_W * ICON_H] = {
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00,
    0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
};
const uint8_t ICON_DROP_MAP[ICON_W * ICON_H] = {
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00,
};
lv_img_dsc_t iconThermo;
lv_img_dsc_t iconDrop;
bool climateIconsReady = false;

void initImgIcon(lv_img_dsc_t &dsc, const uint8_t *data) {
  dsc.header.cf = LV_IMG_CF_ALPHA_8BIT;
  dsc.header.always_zero = 0;
  dsc.header.reserved = 0;
  dsc.header.w = ICON_W;
  dsc.header.h = ICON_H;
  dsc.data_size = ICON_W * ICON_H;
  dsc.data = data;
}

void ensureClimateIcons() {
  if (climateIconsReady) {
    return;
  }
  initImgIcon(iconThermo, ICON_THERMO_MAP);
  initImgIcon(iconDrop, ICON_DROP_MAP);
  climateIconsReady = true;
}

int countMainOverview() {
  int n = 0;
  for (int i = 0; i < overviewUnitCount; ++i) {
    if (!isAuxKind(overviewUnits[i].amsKind)) {
      ++n;
    }
  }
  return n;
}

uint32_t swatchInk(uint32_t rgb) {
  const int r = (rgb >> 16) & 0xFF;
  const int g = (rgb >> 8) & 0xFF;
  const int b = rgb & 0xFF;
  // WCAG relative-luminance crossover vs black/white (~L = 0.179).
  const int y = r * 2126 + g * 7152 + b * 722;
  return y > 456450 ? COL_AMS_INK_DARK : 0xFFFFFF;
}

uint32_t swatchSignature(const uint32_t *colors, uint8_t count, SwatchStyle style,
                         SwatchFinish finish, SwatchSparkle sparkle, SwatchFiber fiber,
                         uint32_t fallback) {
  uint32_t h = (static_cast<uint32_t>(count) << 16) | (static_cast<uint32_t>(style) << 8) |
               static_cast<uint32_t>(finish);
  h ^= (static_cast<uint32_t>(sparkle) << 4) | static_cast<uint32_t>(fiber);
  h ^= fallback * 2654435761u;
  for (uint8_t i = 0; i < count; ++i) {
    h ^= colors[i] * (0x9E3779B9u + i * 0x85EBCA6Bu);
  }
  return h;
}

uint32_t lerpChannel(uint32_t a, uint32_t b, int t /*0..256*/) {
  const int ai = static_cast<int>(a);
  const int bi = static_cast<int>(b);
  return static_cast<uint32_t>(ai + (((bi - ai) * t) >> 8));
}

uint32_t lerpRgb(uint32_t a, uint32_t b, int t /*0..256*/) {
  const uint32_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  const uint32_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  return (lerpChannel(ar, br, t) << 16) | (lerpChannel(ag, bg, t) << 8) | lerpChannel(ab, bb, t);
}

uint32_t sampleSwatchGradient(const uint32_t *colors, uint8_t count, int t /*0..256*/) {
  if (count == 0) {
    return 0x202020;
  }
  if (count == 1 || t <= 0) {
    return colors[0];
  }
  if (t >= 256) {
    return colors[count - 1];
  }
  const int segments = count - 1;
  const int scaled = t * segments;
  const int idx = scaled >> 8;
  const int local = scaled & 0xFF;
  const uint8_t i0 = static_cast<uint8_t>(idx < segments ? idx : segments - 1);
  return lerpRgb(colors[i0], colors[i0 + 1], local);
}

void clearSwatchFillChildren(lv_obj_t *fill) {
  if (!fill) {
    return;
  }
  while (lv_obj_get_child_cnt(fill) > 0) {
    lv_obj_del(lv_obj_get_child(fill, 0));
  }
}

// True while the object (or any ancestor container) is mid-scroll. Gradient
// fills drop to half resolution and fiber/sparkle overlays are skipped
// entirely during scroll; the scroll-end invalidate repaints full detail.
bool objOrAncestorScrolling(lv_obj_t *obj) {
  for (lv_obj_t *o = obj; o != nullptr; o = lv_obj_get_parent(o)) {
    if (lv_obj_is_scrolling(o)) {
      return true;
    }
  }
  return false;
}

void swatchFillDrawCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) {
    return;
  }
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
  auto *paint = static_cast<SwatchPaint *>(lv_obj_get_user_data(obj));
  if (!paint || !draw_ctx || paint->count < 2 ||
      paint->style != static_cast<uint8_t>(SwatchStyle::Gradient)) {
    return;
  }

  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);
  lv_area_t visible;
  if (!_lv_area_intersect(&visible, &coords, draw_ctx->clip_area)) {
    return;
  }
  const int w = lv_area_get_width(&coords);
  const int h = lv_area_get_height(&coords);
  if (w <= 0 || h <= 0 || w > SCREEN_WIDTH || h > SCREEN_HEIGHT) {
    return;
  }

  // ~105° diagonal blend (FilaMan AMS View silk gradient). Only the clipped
  // span is painted; walking a broken full-screen width stalled the panel.
  // Half horizontal resolution while scrolling: ~45 rects instead of ~90 per
  // swatch per scroll frame.
  const int denom = w * 4 + h;
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_opa = LV_OPA_COVER;
  const int step = objOrAncestorScrolling(obj) ? 4 : 2;
  const int xStart = visible.x1 - coords.x1;
  const int xEnd = visible.x2 - coords.x1;
  for (int x = xStart - (xStart % step); x <= xEnd; x += step) {
    int t = ((x * 4 + h / 2) * 256) / (denom > 0 ? denom : 1);
    if (t < 0) {
      t = 0;
    }
    if (t > 256) {
      t = 256;
    }
    dsc.bg_color = lv_color_hex(sampleSwatchGradient(paint->colors, paint->count, t));
    lv_area_t strip = {static_cast<lv_coord_t>(coords.x1 + x), visible.y1,
                       static_cast<lv_coord_t>(coords.x1 + x + step - 1), visible.y2};
    if (strip.x2 > coords.x2) {
      strip.x2 = coords.x2;
    }
    lv_draw_rect(draw_ctx, &dsc, &strip);
  }
}

void drawSpeck(lv_draw_ctx_t *draw_ctx, lv_coord_t cx, lv_coord_t cy) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.radius = LV_RADIUS_CIRCLE;
  dsc.border_width = 0;
  dsc.bg_color = lv_color_hex(0x080A12);
  dsc.bg_opa = LV_OPA_60;
  lv_area_t ring = {static_cast<lv_coord_t>(cx - 2), static_cast<lv_coord_t>(cy - 2),
                    static_cast<lv_coord_t>(cx + 2), static_cast<lv_coord_t>(cy + 2)};
  lv_draw_rect(draw_ctx, &dsc, &ring);
  dsc.bg_color = lv_color_hex(0xFFFFFF);
  dsc.bg_opa = LV_OPA_COVER;
  lv_area_t core = {static_cast<lv_coord_t>(cx - 1), static_cast<lv_coord_t>(cy - 1),
                    static_cast<lv_coord_t>(cx + 1), static_cast<lv_coord_t>(cy + 1)};
  lv_draw_rect(draw_ctx, &dsc, &core);
}

void swatchOverlayDrawCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) {
    return;
  }
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
  auto *paint = static_cast<SwatchPaint *>(lv_obj_get_user_data(obj));
  if (!paint || !draw_ctx || (paint->sparkle == 0 && paint->fiber == 0)) {
    return;
  }
  // Speckle/fiber lines are the costliest overlay pass (~36 line draws +
  // up to 16 masked circle rects per swatch). Skip while scrolling; the
  // scroll-end invalidate repaints them once the list settles.
  if (objOrAncestorScrolling(obj)) {
    return;
  }

  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);
  lv_area_t visible;
  if (!_lv_area_intersect(&visible, &coords, draw_ctx->clip_area)) {
    return;
  }
  const int w = lv_area_get_width(&coords);
  const int h = lv_area_get_height(&coords);
  // The detail-page rail is a few pixels wide; ticks there read as noise.
  if (w < 24 || h < 8 || w > SCREEN_WIDTH || h > SCREEN_HEIGHT) {
    return;
  }

  if (paint->fiber != 0) {
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 1;
    line.round_start = 0;
    line.round_end = 0;
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 6; ++col) {
        const int x = coords.x1 + (col * w) / 6 + 2;
        const int y = coords.y1 + ((row * h) / 3) + (h / 2) + ((col & 1) ? h / 6 : 0);
        lv_point_t a = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
        lv_point_t b = {static_cast<lv_coord_t>(x + 6), static_cast<lv_coord_t>(y - 4)};
        line.color = lv_color_hex(0xFFFFFF);
        line.opa = LV_OPA_70;
        lv_draw_line(draw_ctx, &line, &a, &b);
        a.x = static_cast<lv_coord_t>(a.x + 3);
        a.y = static_cast<lv_coord_t>(a.y + 2);
        b.x = static_cast<lv_coord_t>(b.x + 3);
        b.y = static_cast<lv_coord_t>(b.y + 2);
        line.color = lv_color_hex(0x000000);
        line.opa = LV_OPA_40;
        lv_draw_line(draw_ctx, &line, &a, &b);
      }
    }
  }

  if (paint->sparkle != 0) {
    // Same pinpoint layout FilaMan uses. Glitter adds a tighter second set.
    // Positions are percentages so a short AMS bar still shows the specks.
    static const int8_t specks[][2] = {
        {22, 68}, {62, 28}, {40, 42}, {80, 60}, {14, 36}, {48, 82}, {86, 18}, {32, 16},
    };
    const int count = paint->sparkle == static_cast<uint8_t>(SwatchSparkle::Glitter) ? 8 : 5;
    for (int i = 0; i < count; ++i) {
      const lv_coord_t cx = static_cast<lv_coord_t>(coords.x1 + (w * specks[i][0]) / 100);
      const lv_coord_t cy = static_cast<lv_coord_t>(coords.y1 + (h * specks[i][1]) / 100);
      drawSpeck(draw_ctx, cx, cy);
    }
  }
}

void ensureSwatchLayers(lv_obj_t *swatch, lv_obj_t **fillOut, lv_obj_t **frostOut,
                        lv_obj_t **highlightOut, lv_obj_t **overlayOut) {
  if (!swatch) {
    return;
  }
  if (!*fillOut) {
    *fillOut = lv_obj_create(swatch);
    lv_obj_remove_style_all(*fillOut);
    lv_obj_set_size(*fillOut, lv_pct(100), lv_pct(100));
    lv_obj_align(*fillOut, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(*fillOut, LV_OPA_COVER, 0);
    lv_obj_clear_flag(*fillOut, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(*fillOut, LV_OBJ_FLAG_SCROLLABLE);
    // The swatch is a flex row for its label. A 100% color layer in that row
    // is measured as another full-width item and the layout never settles,
    // so nothing under the header gets drawn.
    lv_obj_add_flag(*fillOut, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_move_background(*fillOut);
    lv_obj_add_event_cb(*fillOut, swatchFillDrawCb, LV_EVENT_DRAW_MAIN, nullptr);
  }
  if (!*frostOut) {
    *frostOut = lv_obj_create(swatch);
    lv_obj_remove_style_all(*frostOut);
    lv_obj_set_size(*frostOut, lv_pct(100), lv_pct(100));
    lv_obj_align(*frostOut, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(*frostOut, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(*frostOut, LV_OPA_20, 0);
    lv_obj_clear_flag(*frostOut, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(*frostOut, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(*frostOut, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(*frostOut, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 5; ++i) {
      lv_obj_t *line = lv_obj_create(*frostOut);
      lv_obj_remove_style_all(line);
      lv_obj_set_size(line, lv_pct(100), 1);
      lv_obj_set_pos(line, 0, 3 + i * 6);
      lv_obj_set_style_bg_color(line, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_bg_opa(line, LV_OPA_40, 0);
      lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }
  }
  if (!*highlightOut) {
    *highlightOut = lv_obj_create(swatch);
    lv_obj_remove_style_all(*highlightOut);
    lv_obj_set_size(*highlightOut, lv_pct(70), lv_pct(55));
    lv_obj_align(*highlightOut, LV_ALIGN_TOP_LEFT, 0, 0);
    // Soft glass corner wash (FilaMan uses a diagonal white fade).
    lv_obj_set_style_bg_color(*highlightOut, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(*highlightOut, LV_OPA_40, 0);
    lv_obj_clear_flag(*highlightOut, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(*highlightOut, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(*highlightOut, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(*highlightOut, LV_OBJ_FLAG_HIDDEN);
  }
  if (overlayOut && !*overlayOut) {
    *overlayOut = lv_obj_create(swatch);
    lv_obj_remove_style_all(*overlayOut);
    lv_obj_set_size(*overlayOut, lv_pct(100), lv_pct(100));
    lv_obj_align(*overlayOut, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(*overlayOut, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(*overlayOut, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(*overlayOut, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(*overlayOut, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(*overlayOut, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(*overlayOut, swatchOverlayDrawCb, LV_EVENT_DRAW_MAIN, nullptr);
  }
}

void applyColorSwatch(lv_obj_t *swatch, lv_obj_t **fillOut, lv_obj_t **frostOut,
                      lv_obj_t **highlightOut, lv_obj_t **overlayOut, SwatchPaint *paint,
                      uint32_t &sig, const uint32_t *colors, uint8_t count, SwatchStyle style,
                      SwatchFinish finish, SwatchSparkle sparkle, SwatchFiber fiber,
                      uint32_t fallback, bool empty) {
  if (!swatch || !paint) {
    return;
  }
  ensureSwatchLayers(swatch, fillOut, frostOut, highlightOut, overlayOut);
  lv_obj_t *fill = *fillOut;
  lv_obj_t *frost = *frostOut;
  lv_obj_t *highlight = *highlightOut;

  uint32_t useColors[MAX_SLOT_COLORS];
  uint8_t useCount = 0;
  SwatchStyle useStyle = SwatchStyle::Flat;
  SwatchFinish useFinish = SwatchFinish::None;
  if (!empty) {
    useCount = count > MAX_SLOT_COLORS ? MAX_SLOT_COLORS : count;
    for (uint8_t i = 0; i < useCount; ++i) {
      useColors[i] = colors[i];
    }
    if (useCount == 0) {
      useColors[0] = fallback;
      useCount = 1;
    }
    useStyle = (useCount > 1) ? style : SwatchStyle::Flat;
    if (useCount > 1 && useStyle == SwatchStyle::Flat) {
      useStyle = SwatchStyle::Striped;
    }
    useFinish = finish;
  } else {
    useColors[0] = fallback;
    useCount = 1;
  }

  const SwatchSparkle useSparkle = empty ? SwatchSparkle::None : sparkle;
  const SwatchFiber useFiber = empty ? SwatchFiber::None : fiber;
  const uint32_t nextSig =
      swatchSignature(useColors, useCount, useStyle, useFinish, useSparkle, useFiber, fallback);
  if (nextSig != sig) {
    sig = nextSig;
    paint->count = useCount;
    paint->style = static_cast<uint8_t>(useStyle);
    paint->sparkle = static_cast<uint8_t>(useSparkle);
    paint->fiber = static_cast<uint8_t>(useFiber);
    for (uint8_t i = 0; i < useCount; ++i) {
      paint->colors[i] = useColors[i];
    }

    clearSwatchFillChildren(fill);
    lv_obj_set_user_data(fill, paint);

    if (useStyle == SwatchStyle::Striped && useCount > 1) {
      lv_obj_set_style_bg_opa(fill, LV_OPA_TRANSP, 0);
      lv_obj_set_flex_flow(fill, LV_FLEX_FLOW_ROW);
      lv_obj_set_style_pad_all(fill, 0, 0);
      lv_obj_set_style_pad_column(fill, 0, 0);
      for (uint8_t i = 0; i < useCount; ++i) {
        lv_obj_t *band = lv_obj_create(fill);
        lv_obj_remove_style_all(band);
        lv_obj_set_height(band, lv_pct(100));
        lv_obj_set_flex_grow(band, 1);
        lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(band, lv_color_hex(useColors[i]), 0);
        lv_obj_clear_flag(band, LV_OBJ_FLAG_CLICKABLE);
      }
    } else if (useStyle == SwatchStyle::Gradient && useCount > 1) {
      lv_obj_set_style_bg_opa(fill, LV_OPA_TRANSP, 0);
      lv_obj_invalidate(fill);
    } else {
      lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(fill, lv_color_hex(useColors[0]), 0);
    }

    lv_obj_set_style_bg_opa(swatch, LV_OPA_TRANSP, 0);

    if (overlayOut && *overlayOut) {
      lv_obj_set_user_data(*overlayOut, paint);
      if (useSparkle == SwatchSparkle::None && useFiber == SwatchFiber::None) {
        lv_obj_add_flag(*overlayOut, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_clear_flag(*overlayOut, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(*overlayOut);
        lv_obj_invalidate(*overlayOut);
      }
    }
  }

  if (useFinish == SwatchFinish::Translucent) {
    lv_obj_set_style_bg_color(highlight, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(highlight, LV_OPA_40, 0);
    lv_obj_clear_flag(frost, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(highlight, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(frost);
    lv_obj_move_foreground(highlight);
    if (overlayOut && *overlayOut && !lv_obj_has_flag(*overlayOut, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_move_foreground(*overlayOut);
    }
  } else if (useFinish == SwatchFinish::Glow) {
    // A phosphor wash and bright inner frame remain legible on both light and
    // dark filament colors without the expensive wide LVGL outline pass.
    lv_obj_add_flag(frost, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(highlight, lv_color_hex(0xB8FF63), 0);
    lv_obj_set_style_bg_opa(highlight, LV_OPA_30, 0);
    lv_obj_clear_flag(highlight, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(highlight);
    if (overlayOut && *overlayOut && !lv_obj_has_flag(*overlayOut, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_move_foreground(*overlayOut);
    }
  } else {
    lv_obj_add_flag(frost, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(highlight, LV_OBJ_FLAG_HIDDEN);
  }

  if (useFinish == SwatchFinish::Glow) {
    lv_obj_set_style_border_width(swatch, 2, 0);
    lv_obj_set_style_border_color(swatch, lv_color_hex(0xB8FF63), 0);
    lv_obj_set_style_border_opa(swatch, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(swatch, 0, 0);
  } else {
    lv_obj_set_style_outline_width(swatch, 0, 0);
    lv_obj_set_style_border_width(swatch, 0, 0);
  }
}

String bayLabelFor(const OverviewUnit &unit, const OverviewSlot &slot, int index) {
  if (slot.label.length()) {
    return slot.label;
  }
  String k = unit.amsKind;
  k.toLowerCase();
  if (k.indexOf("ht") >= 0) {
    return unit.label.length() ? unit.label : String("HT");
  }
  if (k.indexOf("ext") >= 0) {
    return String("Ext") + String(index + 1);
  }
  const char letter = (unit.amsId >= 0 && unit.amsId < 26) ? static_cast<char>('A' + unit.amsId) : 'A';
  return String(letter) + String(slot.slot + 1);
}

// --- Dynamic AMS presence (Display API or MQTT overview) ---------------------
// A unit is shown only when the latest snapshot includes it, so nav buttons
// and detail columns follow the live overview instead of a fixed count.
bool amsUnitPresent(int amsId) {
  for (int i = 0; i < overviewUnitCount; ++i) {
    if (!isAuxKind(overviewUnits[i].amsKind) && overviewUnits[i].amsId == amsId) {
      return true;
    }
  }
  return false;
}

int htPresentCount() {
  int n = 0;
  for (int i = 0; i < overviewUnitCount; ++i) {
    if (overviewUnits[i].amsKind.indexOf("ht") >= 0) {
      ++n;
    }
  }
  if (n > AMSHT_MAX_COUNT) {
    n = AMSHT_MAX_COUNT;
  }
  return n;
}

// Should this page's nav chip be shown? Print/All/Alerts are always available;
// AMS A-D appear per connected AMS 2 Pro unit; HT appears if any AMS-HT is present.
bool pageVisibleForNav(int page) {
  if (page == STATUS_PAGE || page == AMS_OVERVIEW_PAGE || page == ALERTS_PAGE) {
    return true;
  }
  if (page == AMS_HT_PAGE) {
    return htPresentCount() > 0;
  }
  if (page >= AMS2PRO_FIRST_PAGE && page < AMS_HT_PAGE) {
    return amsUnitPresent(page - AMS2PRO_FIRST_PAGE);
  }
  return false;
}

int nextVisiblePage(int from, int dir) {
  for (int step = 1; step <= TOTAL_PAGE_COUNT; ++step) {
    int p = (from + dir * step) % TOTAL_PAGE_COUNT;
    if (p < 0) {
      p += TOTAL_PAGE_COUNT;
    }
    if (pageVisibleForNav(p)) {
      return p;
    }
  }
  return AMS_OVERVIEW_PAGE;
}

bool hasActiveAlerts() {
  if (alertItemCount > 0) {
    return true;
  }
  return printer.alertCount > 0;
}

String slotTitleForColumn(int column) {
  if (!isHtPage()) {
    return "SLOT " + String(column + 1);
  }
  return "HT UNIT " + String(column + 1);
}

String slotBadgeForColumn(int column) {
  if (!isHtPage()) {
    const int ams = amsIdForPage();
    const char letter = (ams >= 0 && ams < 26) ? static_cast<char>('A' + ams) : 'A';
    return String(letter) + String(column + 1);
  }
  return "HT" + String(column + 1);
}

String slotEffectText(const SlotState &slot) {
  if (slot.empty) {
    return "";
  }
  String text;
  auto append = [&](const char *value) {
    if (text.length()) {
      text += "\n";
    }
    text += value;
  };
  if (slot.finish == SwatchFinish::Glow) {
    append("GLOW");
  } else if (slot.finish == SwatchFinish::Translucent) {
    append("TRANS");
  }
  if (slot.sparkle == SwatchSparkle::Galaxy) {
    append("GALAXY");
  } else if (slot.sparkle == SwatchSparkle::Sparkle) {
    append("SPARKLE");
  } else if (slot.sparkle == SwatchSparkle::Glitter) {
    append("GLITTER");
  }
  if (slot.fiber == SwatchFiber::Cf) {
    append("CF");
  } else if (slot.fiber == SwatchFiber::Gf) {
    append("GF");
  }
  return text;
}

SlotState &visibleSlot(int column) {
  if (!isHtPage()) {
    return slots[amsIdForPage()][column];
  }
  return slots[AMS_HT_FIRST_ID + column][0];
}

String pageTitle() {
  if (isStatusPage()) {
    return printer.name.length() ? printer.name : "H2C";
  }
  if (isOverviewPage()) {
    return "AMS View";
  }
  if (isAlertsPage()) {
    return "Alerts";
  }
  if (isHtPage()) {
    return "AMS-HT";
  }
  return String("AMS ") + char('A' + amsIdForPage());
}

String amsClimateSummary() {
  if (isStatusPage() || isOverviewPage() || isAlertsPage()) {
    return "";
  }
  if (isHtPage()) {
    String parts;
    for (int i = 0; i < AMSHT_MAX_COUNT; ++i) {
      const AmsMetaState &meta = metas[AMS_HT_FIRST_ID + i];
      if (!meta.temperature.length() && !meta.humidity.length()) {
        continue;
      }
      if (parts.length()) {
        parts += "   ";
      }
      parts += "HT";
      parts += String(i + 1);
      parts += " ";
      if (meta.temperature.length()) {
        parts += meta.temperature;
        parts += "C";
      }
      if (meta.humidity.length()) {
        if (meta.temperature.length()) {
          parts += " ";
        }
        parts += meta.humidity;
        parts += "%";
      }
    }
    return parts;
  }

  const AmsMetaState &meta = metas[amsIdForPage()];
  String summary;
  if (meta.temperature.length()) {
    summary += meta.temperature;
    summary += " C";
  }
  if (meta.humidity.length()) {
    if (summary.length()) {
      summary += " / ";
    }
    summary += meta.humidity;
    summary += "% RH";
  }
  if (meta.dryStatus.length()) {
    if (summary.length()) {
      summary += " | ";
    }
    summary += meta.dryStatus;
  }
  if (!summary.length()) {
    summary = printer.state.length() ? printer.state : "";
  }
  return summary;
}

uint32_t stateColor(const String &state) {
  String s = state;
  s.toUpperCase();
  if (s.indexOf("RUN") >= 0 || s.indexOf("PRINT") >= 0) {
    return COL_ACCENT;
  }
  if (s.indexOf("PAUSE") >= 0) {
    return COL_WARN;
  }
  if (s.indexOf("FINISH") >= 0 || s.indexOf("DONE") >= 0 || s.indexOf("SUCCESS") >= 0) {
    return COL_INFO;
  }
  if (s.indexOf("FAIL") >= 0 || s.indexOf("ERROR") >= 0 || s.indexOf("OFFLINE") >= 0) {
    return COL_DANGER;
  }
  return COL_MUTED;
}

String formatTempPair(const String &actual, const String &target, bool showZeroTarget = false) {
  if (!actual.length() && !target.length()) {
    return "--";
  }
  String out = actual.length() ? actual : "--";
  if (target.length() && (showZeroTarget || target != "0")) {
    out += " / ";
    out += target;
  }
  out += " C";
  return out;
}

String formatTimeLeft(const PrinterState &state) {
  int totalMinutes = state.remainingMinutes;
  if (totalMinutes < 0 && state.remainingSeconds >= 0) {
    totalMinutes = state.remainingSeconds / 60;
    if (state.remainingTime.endsWith("s") && !state.remainingTime.endsWith("ms") &&
        state.remainingSeconds > 0 && state.remainingSeconds <= 24 * 60) {
      totalMinutes = state.remainingSeconds;
    }
  }
  if (totalMinutes < 0 && state.remainingTime.length()) {
    return state.remainingTime;
  }
  if (totalMinutes < 0) {
    return "--";
  }
  const int hours = totalMinutes / 60;
  const int minutes = totalMinutes % 60;
  if (hours > 0) {
    String out = String(hours) + "h ";
    if (minutes < 10) {
      out += "0";
    }
    out += String(minutes) + "m";
    return out;
  }
  return String(totalMinutes) + " min";
}

String dashOr(const String &value, const char *fallback = "--") {
  return value.length() ? value : fallback;
}

bool stateContains(const String &state, const char *token) {
  String s = state;
  s.toUpperCase();
  return s.indexOf(token) >= 0;
}

uint32_t spoolBorderColor(const OverviewSlot &slot) {
  if (slot.empty) {
    return COL_DIM;
  }
  if (slot.active) {
    return COL_ACCENT;
  }
  if (slot.remainingPercent >= 0 && slot.remainingPercent < config().spoolCritPct) {
    return COL_DANGER;
  }
  if (slot.remainingPercent >= 0 && slot.remainingPercent < config().spoolLowPct) {
    return COL_WARN;
  }
  return COL_BORDER;
}

// AMS View bays are narrow, so one figure occupies the label and they trade
// places. A missing grams value stays on percent, so that bay never changes.
String remainingAmountText(int percent, int grams) {
  if (showRemainingGrams && grams >= 0) {
    return String(grams) + "g";
  }
  if (percent >= 0) {
    return String(percent) + "%";
  }
  if (grams >= 0) {
    return String(grams) + "g";
  }
  return "";
}

String remainingAmountText(int percent, const String &grams) {
  int g = -1;
  if (grams.length()) {
    g = grams.toInt();
  }
  return remainingAmountText(percent, g);
}

// Per-unit AMS pages have room for both figures at once.
String remainingBothText(int percent, int grams) {
  String out;
  if (percent >= 0) {
    out += String(percent);
    out += "%";
  }
  if (grams >= 0) {
    if (out.length()) {
      out += "  ";
    }
    out += String(grams);
    out += "g";
  }
  return out;
}

String remainingBothText(int percent, const String &grams) {
  int g = -1;
  if (grams.length()) {
    g = grams.toInt();
  }
  return remainingBothText(percent, g);
}

uint32_t remainingBarColor(int percent) {
  if (percent >= 0 && percent < config().spoolCritPct) {
    return COL_DANGER;
  }
  if (percent >= 0 && percent < config().spoolLowPct) {
    return COL_WARN;
  }
  return COL_ACCENT;
}

int remainingLengthMeters(const SlotState &slot) {
  if (slot.densityGcm3 <= 0.0f || slot.diameterMm <= 0.0f || !slot.remainingGrams.length()) {
    return -1;
  }
  const float grams = slot.remainingGrams.toFloat();
  if (grams < 0.0f) {
    return -1;
  }
  // Volume = mass / density; divide by circular filament cross-section.
  // With grams, g/cm3, and diameter in mm, this simplifies directly to metres.
  constexpr float PI_F = 3.14159265f;
  const float metres =
      (4.0f * grams) / (PI_F * slot.densityGcm3 * slot.diameterMm * slot.diameterMm);
  return metres >= 0.0f ? static_cast<int>(metres + 0.5f) : -1;
}

void ensureDefaults() {
  for (int ams = 0; ams < TOTAL_AMS_IDS; ++ams) {
    for (int slot = 0; slot < COLUMNS_PER_PAGE; ++slot) {
      slots[ams][slot].amsId = ams;
      slots[ams][slot].slot = slot;
      slots[ams][slot].amsKind = ams < AMS2PRO_COUNT ? "ams2pro" : "ams_ht";
    }
    metas[ams].amsId = ams;
    metas[ams].amsKind = ams < AMS2PRO_COUNT ? "ams2pro" : "ams_ht";
  }
}

bool setLabelIfChanged(lv_obj_t *label, const String &text) {
  if (!label) {
    return false;
  }
  const char *current = lv_label_get_text(label);
  if (current && text == current) {
    return false;
  }
  lv_label_set_text(label, text.c_str());
  return true;
}

// LVGL invalidates on every lv_obj_set_style_*/add_flag call regardless of
// whether the value actually changed, and flag/style changes force a relayout
// that repaints far more than the widget itself. Re-applying identical values
// each refresh cycle was repainting most of the screen, so cache and skip.
enum StyleProp : uint8_t {
  PROP_TEXT_COLOR = 0,
  PROP_BG_COLOR,
  PROP_BG_COLOR_INDICATOR,
  PROP_BG_OPA,
  PROP_BORDER_COLOR,
  PROP_BORDER_WIDTH,
  PROP_STYLE_TAG,
};

struct StyleCacheEntry {
  lv_obj_t *obj;
  uint8_t prop;
  uint32_t value;
};

// AMS View bays add a lot of styled widgets; undersizing caused every refresh
// to re-apply styles and invalidate large joined dirty regions.
StyleCacheEntry styleCache[1024];
size_t styleCacheUsed = 0;

bool styleValueChanged(lv_obj_t *obj, uint8_t prop, uint32_t value) {
  for (size_t i = 0; i < styleCacheUsed; ++i) {
    StyleCacheEntry &entry = styleCache[i];
    if (entry.obj == obj && entry.prop == prop) {
      if (entry.value == value) {
        return false;
      }
      entry.value = value;
      return true;
    }
  }
  if (styleCacheUsed < (sizeof(styleCache) / sizeof(styleCache[0]))) {
    styleCache[styleCacheUsed++] = {obj, prop, value};
    return true;
  }
  // Cache full: still apply (safe) but this should not happen at 1024.
  return true;
}

void setTextColorIfChanged(lv_obj_t *obj, uint32_t color) {
  if (!obj || !styleValueChanged(obj, PROP_TEXT_COLOR, color)) {
    return;
  }
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}

void setBgColorIfChanged(lv_obj_t *obj, uint32_t color) {
  if (!obj || !styleValueChanged(obj, PROP_BG_COLOR, color)) {
    return;
  }
  lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
}

void setIndicatorColorIfChanged(lv_obj_t *obj, uint32_t color) {
  if (!obj || !styleValueChanged(obj, PROP_BG_COLOR_INDICATOR, color)) {
    return;
  }
  lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_INDICATOR);
}

void setBgOpaIfChanged(lv_obj_t *obj, lv_opa_t opa) {
  if (!obj || !styleValueChanged(obj, PROP_BG_OPA, opa)) {
    return;
  }
  lv_obj_set_style_bg_opa(obj, opa, 0);
}

void setBorderColorIfChanged(lv_obj_t *obj, uint32_t color) {
  if (!obj || !styleValueChanged(obj, PROP_BORDER_COLOR, color)) {
    return;
  }
  lv_obj_set_style_border_color(obj, lv_color_hex(color), 0);
}

void setBorderWidthIfChanged(lv_obj_t *obj, lv_coord_t width) {
  if (!obj || !styleValueChanged(obj, PROP_BORDER_WIDTH, static_cast<uint32_t>(width))) {
    return;
  }
  lv_obj_set_style_border_width(obj, width, 0);
}

void setHiddenIfChanged(lv_obj_t *obj, bool hidden) {
  if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden) {
    return;
  }
  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

void setBarValueIfChanged(lv_obj_t *bar, int32_t value) {
  if (!bar || lv_bar_get_value(bar) == value) {
    return;
  }
  lv_bar_set_value(bar, value, LV_ANIM_OFF);
}

// tag identifies which optional style is currently applied (0 = none).
void applyOptionalStyle(lv_obj_t *obj, uint32_t tag, lv_style_t *style) {
  if (!obj || !styleValueChanged(obj, PROP_STYLE_TAG, tag)) {
    return;
  }
  lv_obj_remove_style(obj, &activeColumnStyle, 0);
  lv_obj_remove_style(obj, &emptyColumnStyle, 0);
  if (style) {
    lv_obj_add_style(obj, style, 0);
  }
}

void setMetric(MetricTile &tile, const String &value) {
  setLabelIfChanged(tile.value, value);
}

void setTileHeating(MetricTile &tile, bool heating, bool activeExtruder) {
  if (!tile.root) {
    return;
  }
  uint32_t border = COL_BORDER;
  int width = 1;
  if (heating) {
    border = COL_WARN;
    width = 2;
  }
  if (activeExtruder) {
    border = COL_ACCENT;
    width = 2;
  }
  setBorderColorIfChanged(tile.root, border);
  setBorderWidthIfChanged(tile.root, width);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, lv_pct(100));
  return label;
}

lv_obj_t *makeClimateIcon(lv_obj_t *parent, const lv_img_dsc_t *src) {
  lv_obj_t *img = lv_img_create(parent);
  lv_img_set_src(img, src);
  lv_obj_set_style_img_recolor(img, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
  lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
  return img;
}

void applyHeaderClimate(OverviewCard &card, const String &temperature, const String &humidity,
                        bool drying) {
  const bool showTemp = temperature.length() > 0;
  const bool showHum = humidity.length() > 0;
  setHiddenIfChanged(card.tempIcon, !showTemp);
  setHiddenIfChanged(card.tempText, !showTemp && !drying);
  if (card.tempText && (showTemp || drying)) {
    String text;
    if (drying) {
      text += "DRY";
    }
    if (showTemp) {
      if (text.length()) {
        text += " ";
      }
      text += temperature;
      text += "C";
    }
    setLabelIfChanged(card.tempText, text);
  }
  setHiddenIfChanged(card.humIcon, !showHum);
  setHiddenIfChanged(card.humText, !showHum);
  if (showHum && card.humText) {
    setLabelIfChanged(card.humText, humidity + "%");
  }
}

void applyBayClimate(lv_obj_t *label, const String &temperature, const String &humidity,
                    bool drying) {
  if (!label) {
    return;
  }
  String text;
  if (drying) {
    text += "DRY ";
  }
  if (temperature.length()) {
    text += temperature;
    text += "C";
  }
  if (humidity.length()) {
    if (text.length()) {
      text += " ";
    }
    text += humidity;
    text += "%";
  }
  setLabelIfChanged(label, text);
  setHiddenIfChanged(label, text.length() == 0);
}

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
}

String freshnessText() {
  if (connectionMessage.indexOf("MQTT connecting") >= 0 ||
      connectionMessage.indexOf("MQTT failed") >= 0 ||
      (connectionMessage.indexOf("MQTT") >= 0 && connectionMessage.indexOf("connected") < 0 &&
       connectionMessage.indexOf("WiFi") < 0)) {
    if (connectionMessage.indexOf("connected") < 0) {
      return "MQTT...";
    }
  }
  if (connectionMessage.indexOf("WiFi") >= 0 && connectionMessage.indexOf("MQTT connected") < 0) {
    if (connectionMessage.indexOf("connected") < 0 && connectionMessage.indexOf("MQTT") < 0) {
      return "MQTT...";
    }
  }
  if (display_poll_active() && !display_poll_needs_mqtt()) {
    if (lastPrinterUpdateMs == 0) {
      return "FilaMan...";
    }
  } else if (!bridge.online) {
    return lastPrinterUpdateMs == 0 ? "MQTT..." : "MQTT offline";
  } else if (lastPrinterUpdateMs == 0) {
    return "MQTT...";
  }
  const unsigned long age = millis() - lastPrinterUpdateMs;
  if (age <= FRESH_LIVE_MS) {
    return "Live";
  }
  if (age <= FRESH_STALE_MS) {
    const unsigned long secs = age / 1000UL;
    return String(secs) + "s ago";
  }
  return "Stale";
}

uint32_t freshnessColor(const String &text) {
  if (text == "Live") {
    return COL_ACCENT;
  }
  if (text == "Stale" || text == "MQTT offline") {
    return COL_DANGER;
  }
  if (text.endsWith("s ago")) {
    return COL_WARN;
  }
  return COL_MUTED;
}

void refreshFreshness() {
  const String text = freshnessText();
  setLabelIfChanged(freshLabel, text);
  setTextColorIfChanged(freshLabel, freshnessColor(text));
}

void refreshNav() {
  for (int i = 0; i < TOTAL_PAGE_COUNT; ++i) {
    NavChip &chip = navChips[i];
    if (!chip.root) {
      continue;
    }
    // Hidden flex children collapse out of the row, so absent AMS units simply
    // disappear from the footer.
    setHiddenIfChanged(chip.root, !pageVisibleForNav(i));
    const bool active = (i == currentPage);
    if (styleValueChanged(chip.root, PROP_STYLE_TAG, active ? 1 : 0)) {
      lv_obj_remove_style(chip.root, &navChipActiveStyle, 0);
      if (active) {
        lv_obj_add_style(chip.root, &navChipActiveStyle, 0);
      }
    }
    setTextColorIfChanged(chip.label, active ? COL_BG : COL_MUTED);
  }
}

void refreshFooter() {
  refreshNav();
  setLabelIfChanged(ipLabel, panelIp.length() ? panelIp : "--");
}

void refreshAlertBanner() {
  if (!alertBanner || !alertBannerLabel) {
    return;
  }
  // AMS View is a filament board — keep HMS/printer alerts off this page.
  if (isOverviewPage() || alertItemCount <= 0) {
    setHiddenIfChanged(alertBanner, true);
    return;
  }

  int best = 0;
  for (int i = 1; i < alertItemCount; ++i) {
    const String &sev = alertItems[i].severity;
    if (sev == "danger" && alertItems[best].severity != "danger") {
      best = i;
    } else if (sev == "warn" && alertItems[best].severity == "info") {
      best = i;
    }
  }

  const AlertItem &item = alertItems[best];
  String text = item.title.length() ? item.title : "Alert";
  if (alertItemCount > 1) {
    text += "  (+";
    text += String(alertItemCount - 1);
    text += ")";
  }
  setLabelIfChanged(alertBannerLabel, text);

  uint32_t bg = COL_WARN;
  String sev = item.severity;
  sev.toLowerCase();
  if (sev == "danger") {
    bg = COL_DANGER;
  } else if (sev == "info") {
    bg = COL_INFO;
  }
  setBgColorIfChanged(alertBanner, bg);
  setHiddenIfChanged(alertBanner, false);
}

void refreshHeaderLabels() {
  const bool hideBack = isOverviewPage();
  const bool backChanged = backBtn && lv_obj_has_flag(backBtn, LV_OBJ_FLAG_HIDDEN) != hideBack;
  setHiddenIfChanged(backBtn, hideBack);
  // Title needs room for "AMS View"; on detail pages with the back button
  // visible, keep it compact so climate/status text still has space.
  if (titleLabel) {
    lv_obj_set_width(titleLabel, hideBack ? 140 : 100);
  }
  bool chromeChanged = backChanged;
  chromeChanged |= setLabelIfChanged(titleLabel, pageTitle());
  if (isStatusPage()) {
    if (lastPrinterUpdateMs == 0) {
      chromeChanged |= setLabelIfChanged(stateLabel, "Linking");
    } else {
      chromeChanged |= setLabelIfChanged(stateLabel, printer.state.length() ? printer.state : "");
    }
    setTextColorIfChanged(stateLabel, COL_MUTED);
  } else if (isOverviewPage()) {
    // Printer name + state + progress can grow after MQTT connects. Without a
    // forced flex pass, LONG_DOT content can inflate headerLeft and leave the
    // title undrawn until a full reboot rebuilds the tree.
    String summary = printer.name.length() ? printer.name : String("");
    if (printer.state.length()) {
      if (summary.length()) {
        summary += "  ";
      }
      summary += printer.state;
    }
    if (printer.progress >= 0) {
      if (summary.length()) {
        summary += "  ";
      }
      summary += String(printer.progress);
      summary += "%";
    }
    if (!summary.length()) {
      summary = String(overviewUnitCount) + " units";
    }
    chromeChanged |= setLabelIfChanged(stateLabel, summary);
    setTextColorIfChanged(stateLabel, COL_ACCENT);
  } else if (isAlertsPage()) {
    chromeChanged |= setLabelIfChanged(stateLabel, String(alertItemCount) + " alerts");
    setTextColorIfChanged(stateLabel, COL_MUTED);
  } else {
    chromeChanged |= setLabelIfChanged(stateLabel, amsClimateSummary());
    setTextColorIfChanged(stateLabel, COL_MUTED);
  }
  // Back visibility, title width, or status text length all change the left
  // flex min-width. Relayout whenever chrome text/structure changes — not only
  // on navigation — so the main AMS View header cannot stay crushed until reboot.
  if (chromeChanged && headerBar) {
    lv_obj_update_layout(headerBar);
    lv_obj_invalidate(headerBar);
  }
}

void refreshHeader() {
  refreshHeaderLabels();
  refreshFreshness();
  refreshFooter();
}

void refreshColumn(int column) {
  SlotState &slot = visibleSlot(column);
  ColumnWidgets &widgets = columns[column];

  setLabelIfChanged(widgets.title, slotTitleForColumn(column));
  setTextColorIfChanged(widgets.title, slot.active ? COL_ACCENT : COL_MUTED);
  setLabelIfChanged(widgets.railBadgeLabel, slotBadgeForColumn(column));

  const uint32_t railColor = slot.colorCount > 0 ? slot.colors[0] : parseColor(slot.color);
  applyColorSwatch(widgets.colorRail, &widgets.railFill, &widgets.railFrost, &widgets.railHighlight,
                   &widgets.railOverlay, &widgets.paint, widgets.railSig, slot.colors, slot.colorCount,
                   slot.colorStyle, slot.finish, slot.sparkle, slot.fiber,
                   slot.empty ? 0x161E29 : railColor, slot.empty);
  if (slot.finish != SwatchFinish::Glow) {
    // applyColorSwatch owns this object's border for glow and clears it for
    // other finishes, so restore the normal frame directly after each apply.
    lv_obj_set_style_border_width(widgets.colorRail, 1, 0);
    lv_obj_set_style_border_color(
        widgets.colorRail,
        lv_color_hex(slot.empty ? COL_BORDER_SOFT : (slot.active ? COL_ACCENT : COL_BORDER)), 0);
    lv_obj_set_style_border_opa(widgets.colorRail, LV_OPA_COVER, 0);
  }
  if (widgets.railBadge) {
    lv_obj_move_foreground(widgets.railBadge);
  }
  const String effectText = slotEffectText(slot);
  setHiddenIfChanged(widgets.railEffectBadge, effectText.length() == 0);
  if (effectText.length()) {
    setLabelIfChanged(widgets.railEffectLabel, effectText);
    lv_obj_move_foreground(widgets.railEffectBadge);
  }

  String colorName = slot.empty ? "Empty Slot" : slot.colorName;
  if (!colorName.length() && !slot.empty) {
    colorName = "Unnamed Filament";
  }
  setLabelIfChanged(widgets.colorName, colorName);
  setTextColorIfChanged(widgets.colorName, slot.empty ? COL_DIM : COL_TEXT);

  String profileText;
  if (slot.empty) {
    profileText = "No filament spool loaded";
  } else if (slot.profileBaseName.length() > 0) {
    profileText = slot.profileBaseName;
  } else if (slot.manufacturer.length() || slot.material.length()) {
    profileText = slot.manufacturer;
    if (slot.material.length()) {
      if (profileText.length()) {
        profileText += " ";
      }
      profileText += slot.material;
    }
    profileText += " Profile";
  } else {
    profileText = "Standard Filament Profile";
  }
  setHiddenIfChanged(widgets.profileName, false);
  setLabelIfChanged(widgets.profileName, profileText);
  setTextColorIfChanged(widgets.profileName, slot.empty ? COL_DIM : COL_MUTED);

  setLabelIfChanged(widgets.manufacturer, slot.empty ? String("--") : dashOr(slot.manufacturer));
  setTextColorIfChanged(widgets.manufacturer, slot.empty ? COL_DIM : COL_TEXT);

  const bool hasMat = !slot.empty && slot.material.length() > 0;
  setHiddenIfChanged(widgets.materialPill, !hasMat);
  if (hasMat) {
    setLabelIfChanged(widgets.material, slot.material);
  }

  String temps;
  if (!slot.empty && (slot.nozzleMin.length() || slot.nozzleMax.length())) {
    temps = slot.nozzleMin.length() ? slot.nozzleMin : "?";
    temps += "-";
    temps += slot.nozzleMax.length() ? slot.nozzleMax : "?";
    temps += " C";
  } else {
    temps = "--";
  }
  setLabelIfChanged(widgets.temps, temps);
  setTextColorIfChanged(widgets.temps, slot.empty ? COL_DIM : COL_TEXT);

  String bed = (!slot.empty && slot.bedTemp.length()) ? slot.bedTemp + " C" : String("--");
  setLabelIfChanged(widgets.bedTemp, bed);
  setTextColorIfChanged(widgets.bedTemp, slot.empty ? COL_DIM : COL_TEXT);

  if (slot.empty) {
    setBarValueIfChanged(widgets.remainingBar, 0);
    setBgOpaIfChanged(widgets.colorRail, LV_OPA_60);
    setLabelIfChanged(widgets.remaining, String("--"));
    setTextColorIfChanged(widgets.remaining, COL_DIM);

    setLabelIfChanged(widgets.meta, String("EMPTY"));
    setTextColorIfChanged(widgets.meta, COL_DIM);
    setBgColorIfChanged(widgets.statusPill, 0x10151D);
    setBorderColorIfChanged(widgets.statusPill, COL_BORDER_SOFT);
  } else {
    setBgOpaIfChanged(widgets.colorRail, LV_OPA_COVER);
    int fill = slot.remainingPercent >= 0 ? slot.remainingPercent : 0;
    setBarValueIfChanged(widgets.remainingBar, fill);
    const uint32_t barCol = remainingBarColor(slot.remainingPercent);
    setIndicatorColorIfChanged(widgets.remainingBar, barCol);

    String rem;
    if (slot.remainingPercent >= 0) {
      rem += String(slot.remainingPercent) + "%";
    }
    const int grams = slot.remainingGrams.length() ? slot.remainingGrams.toInt() : -1;
    if (grams >= 0) {
      if (rem.length()) {
        rem += "  (";
        rem += String(grams) + "g)";
      } else {
        rem += String(grams) + "g";
      }
    }
    const int metres = remainingLengthMeters(slot);
    if (metres >= 0) {
      if (rem.length()) {
        rem += "  |  ";
      }
      rem += "~" + String(metres) + "m";
    }
    setLabelIfChanged(widgets.remaining, rem.length() ? rem : String("--"));
    const uint32_t remTextCol =
        (slot.remainingPercent >= 0 && slot.remainingPercent < config().spoolLowPct) ? barCol
                                                                                     : COL_TEXT;
    setTextColorIfChanged(widgets.remaining, remTextCol);

    // Status pill: ACTIVE > Backup badge > RFID / MANUAL
    if (slot.active) {
      setLabelIfChanged(widgets.meta, String("ACTIVE"));
      setTextColorIfChanged(widgets.meta, COL_ACCENT);
      setBgColorIfChanged(widgets.statusPill, 0x112E22);
      setBorderColorIfChanged(widgets.statusPill, COL_ACCENT);
    } else if (slot.backupBadge.length()) {
      setLabelIfChanged(widgets.meta, slot.backupBadge);
      setTextColorIfChanged(widgets.meta, 0x7CC4FA);
      setBgColorIfChanged(widgets.statusPill, 0x132436);
      setBorderColorIfChanged(widgets.statusPill, 0x284B6E);
    } else if (slot.rfid) {
      setLabelIfChanged(widgets.meta, String("RFID"));
      setTextColorIfChanged(widgets.meta, COL_MUTED);
      setBgColorIfChanged(widgets.statusPill, COL_SURFACE_2);
      setBorderColorIfChanged(widgets.statusPill, COL_BORDER);
    } else {
      setLabelIfChanged(widgets.meta, String("MANUAL"));
      setTextColorIfChanged(widgets.meta, COL_DIM);
      setBgColorIfChanged(widgets.statusPill, 0x131922);
      setBorderColorIfChanged(widgets.statusPill, COL_BORDER_SOFT);
    }
  }

  if (slot.active) {
    applyOptionalStyle(widgets.root, 1, &activeColumnStyle);
  } else if (slot.empty) {
    applyOptionalStyle(widgets.root, 2, &emptyColumnStyle);
  } else {
    applyOptionalStyle(widgets.root, 0, nullptr);
  }
}

void clearCardBinds() {
  for (int i = 0; i < MAX_OVERVIEW_CARDS; ++i) {
    cardBinds[i].count = 0;
    for (int s = 0; s < MAX_OVERVIEW_SLOTS; ++s) {
      cardBinds[i].src[s] = -1;
    }
  }
}

void pushCardUnit(int card, int unitIndex) {
  if (card < 0 || card >= MAX_OVERVIEW_CARDS) {
    return;
  }
  OverviewCardBind &b = cardBinds[card];
  if (b.count >= MAX_OVERVIEW_SLOTS) {
    return;
  }
  b.src[b.count++] = static_cast<int8_t>(unitIndex);
}

void rebuildOverviewBinds() {
  clearCardBinds();
  int htIdx[MAX_AMS_HT_UNITS];
  int htN = 0;
  int extIdx[4];
  int extN = 0;
  int mainCard = 0;
  for (int i = 0; i < overviewUnitCount; ++i) {
    if (isHtKind(overviewUnits[i].amsKind)) {
      if (htN < MAX_AMS_HT_UNITS) {
        htIdx[htN++] = i;
      }
    } else if (isExtKind(overviewUnits[i].amsKind)) {
      if (extN < 4) {
        extIdx[extN++] = i;
      }
    } else if (mainCard < AMS2PRO_COUNT) {
      pushCardUnit(mainCard++, i);
    }
  }

  int auxCard = AMS2PRO_COUNT;

  if (htN <= AMS2PRO_COUNT) {
    for (int i = 0; i < htN && auxCard < MAX_OVERVIEW_CARDS; ++i) {
      pushCardUnit(auxCard++, htIdx[i]);
    }
  } else {
    for (int i = 0; i < htN && auxCard < MAX_OVERVIEW_CARDS;) {
      int n = htN - i;
      if (n > MAX_OVERVIEW_SLOTS) {
        n = MAX_OVERVIEW_SLOTS;
      }
      const int card = auxCard++;
      for (int k = 0; k < n; ++k) {
        pushCardUnit(card, htIdx[i + k]);
      }
      i += n;
    }
  }

  for (int i = 0; i < extN && auxCard < MAX_OVERVIEW_CARDS; ++i) {
    pushCardUnit(auxCard++, extIdx[i]);
  }
}

int unitIndexForCard(int card) {
  if (card < 0 || card >= MAX_OVERVIEW_CARDS || cardBinds[card].count <= 0) {
    return -1;
  }
  return cardBinds[card].src[0];
}

bool cardUnitFor(int card, OverviewUnit &out) {
  if (card < 0 || card >= MAX_OVERVIEW_CARDS) {
    return false;
  }
  const OverviewCardBind &b = cardBinds[card];
  if (b.count <= 0) {
    return false;
  }
  if (b.count == 1) {
    const int idx = b.src[0];
    if (idx < 0 || idx >= overviewUnitCount) {
      return false;
    }
    out = overviewUnits[idx];
    return true;
  }

  out = OverviewUnit();
  out.amsKind = "ams_ht";
  String firstLabel;
  String lastLabel;
  for (int i = 0; i < b.count; ++i) {
    const int idx = b.src[i];
    if (idx < 0 || idx >= overviewUnitCount) {
      continue;
    }
    const OverviewUnit &u = overviewUnits[idx];
    if (!firstLabel.length()) {
      firstLabel = u.label;
    }
    lastLabel = u.label;
    OverviewSlot slot;
    if (u.slotCount > 0) {
      slot = u.slots[0];
    }
    if (!slot.label.length()) {
      slot.label = u.label.length() ? u.label : String("HT");
    }
    slot.temperature = u.temperature;
    slot.humidity = u.humidity;
    slot.drying = u.drying;
    if (out.slotCount < MAX_OVERVIEW_SLOTS) {
      out.slots[out.slotCount++] = slot;
    }
    if (u.drying) {
      out.drying = true;
    }
    if (!out.amsId) {
      out.amsId = u.amsId;
    }
  }
  if (firstLabel.length() && lastLabel.length() && firstLabel != lastLabel) {
    out.label = firstLabel + "-" + lastLabel;
  } else {
    out.label = firstLabel.length() ? firstLabel : String("AMS-HT");
  }
  return out.slotCount > 0;
}

int countAuxCards() {
  int n = 0;
  for (int i = AMS2PRO_COUNT; i < MAX_OVERVIEW_CARDS; ++i) {
    if (cardBinds[i].count > 0) {
      ++n;
    }
  }
  return n;
}

void layoutOverviewBoard() {
  if (!overviewMain || !overviewAux) {
    return;
  }
  const int mainCount = countMainOverview();
  const int auxCount = countAuxCards();
  setHiddenIfChanged(overviewMain, mainCount <= 0);
  setHiddenIfChanged(overviewAux, auxCount <= 0);
  if (overviewHint) {
    const bool waiting = mainCount + auxCount <= 0;
    setHiddenIfChanged(overviewHint, !waiting);
    if (waiting) {
      setLabelIfChanged(overviewHint,
                        connectionMessage.length() ? connectionMessage : String("Linking to FilaMan"));
    }
  }
  // More than one AMS goes onto two rows: two stand one above the other, each
  // as wide as the page, three and four make two rows of two. AMS-HT and
  // external spools stay together on the one row under them.
  const int mainRows = mainCount <= 0 ? 0 : (mainCount == 1 ? 1 : 2);
  const int rows = mainRows + (auxCount > 0 ? 1 : 0);

  // Every row is the same height, an AMS row or the one under them: the cards
  // show the same things. The height is in pixels, the page less the gaps
  // shared out, because percentages do not know about the gaps and left the
  // rows a few pixels apart in height. Height 0 plus flex grow never settled,
  // so the area under the header stayed black.
  lv_obj_set_flex_grow(overviewMain, 0);
  lv_obj_set_flex_grow(overviewAux, 0);
  lv_obj_update_layout(overviewRoot);
  const int gap = lv_obj_get_style_pad_row(overviewRoot, 0);
  const int pageH = lv_obj_get_content_height(overviewRoot);
  const int rowH = (rows > 0 && pageH > gap * rows) ? (pageH - gap * (rows - 1)) / rows : 0;
  if (rowH > 0) {
    lv_obj_set_height(overviewMain, mainRows > 0 ? mainRows * rowH + gap * (mainRows - 1) : 0);
    lv_obj_set_height(overviewAux, auxCount > 0 ? rowH : 0);
  } else if (rows > 0) {
    lv_obj_set_height(overviewMain, lv_pct(100 * mainRows / rows));
    lv_obj_set_height(overviewAux, lv_pct(auxCount > 0 ? 100 / rows : 0));
  }

  // Two cards side by side share the width the same way, so an AMS row ends
  // where the row under it does: 48 percent each left it short of the edge.
  const int colGap = lv_obj_get_style_pad_column(overviewMain, 0);
  const int pageW = lv_obj_get_content_width(overviewMain);
  const bool pair = mainCount > 2;
  const int cardW = (pair && pageW > colGap) ? (pageW - colGap) / 2 : 0;
  const uint32_t cardH = mainCount <= 1 ? 100 : 48;
  for (int i = 0; i < AMS2PRO_COUNT; ++i) {
    lv_obj_t *card = overviewCards[i].root;
    if (!card) {
      continue;
    }
    const uint32_t tag = (pair ? 1u << 31 : 0u) | (cardH << 24) |
                         (static_cast<uint32_t>(cardW) << 12) | static_cast<uint32_t>(rowH);
    if (!styleValueChanged(card, PROP_STYLE_TAG, tag)) {
      continue;
    }
    lv_obj_set_flex_grow(card, 0);
    lv_obj_set_width(card, cardW > 0 ? cardW : lv_pct(pair ? 48 : 100));
    lv_obj_set_height(card, rowH > 0 ? rowH : lv_pct(cardH));
  }
}

void refreshOverviewBay(OverviewBay &bay, const OverviewUnit &unit, int index, bool cellBay,
                        bool showSlotClimate) {
  if (!bay.root) {
    return;
  }
  if (index >= unit.slotCount) {
    setHiddenIfChanged(bay.root, true);
    return;
  }
  setHiddenIfChanged(bay.root, false);

  const OverviewSlot &slot = unit.slots[index];
  const int tag = (cellBay ? 2 : 0) | (unit.slotCount << 2) | (showSlotClimate ? 1 : 0);
  if (styleValueChanged(bay.root, PROP_STYLE_TAG, static_cast<uint32_t>(tag))) {
    if (cellBay) {
      lv_obj_set_flex_grow(bay.root, 0);
      lv_obj_set_width(bay.root, lv_pct(48));
      lv_obj_set_height(bay.root, lv_pct(48));
    } else {
      lv_obj_set_width(bay.root, LV_SIZE_CONTENT);
      lv_obj_set_flex_grow(bay.root, 1);
      lv_obj_set_height(bay.root, lv_pct(100));
    }
    if (bay.swatch) {
      // A thin bar on an AMS slot as on an AMS-HT one: the 16 pixels it gives
      // up are the second line of a colour name that does not fit on one.
      int swatchH = 22;
      if (cellBay && !isHtKind(unit.amsKind)) {
        swatchH = 28;
      }
      lv_obj_set_height(bay.swatch, swatchH);
    }
  }

  String title = bayLabelFor(unit, slot, index);
  setLabelIfChanged(bay.bayLabel, title);

  if (showSlotClimate) {
    applyBayClimate(bay.climate, slot.temperature, slot.humidity, slot.drying);
  } else {
    applyBayClimate(bay.climate, String(), String(), false);
  }

  if (slot.empty) {
    setBgColorIfChanged(bay.root, COL_AMS_CARD);
    setBorderColorIfChanged(bay.root, COL_DIM);
    setBorderWidthIfChanged(bay.root, 1);
    applyColorSwatch(bay.swatch, &bay.swatchFill, &bay.frost, &bay.highlight, &bay.overlay,
                     &bay.paint, bay.swatchSig, nullptr, 0, SwatchStyle::Flat, SwatchFinish::None,
                     SwatchSparkle::None, SwatchFiber::None, COL_AMS_SOFT, true);
    setTextColorIfChanged(bay.bayLabel, COL_MUTED);
    if (bay.bayLabel) {
      lv_obj_move_foreground(bay.bayLabel);
    }
    setLabelIfChanged(bay.material, String("Empty"));
    setTextColorIfChanged(bay.material, COL_MUTED);
    setHiddenIfChanged(bay.desc, true);
    setHiddenIfChanged(bay.remainingRow, true);
    setHiddenIfChanged(bay.brand, true);
    return;
  }

  const uint32_t color = slot.colorCount > 0 ? slot.colors[0] : parseColor(slot.color);
  const uint32_t ink = swatchInk(color);
  setBgColorIfChanged(bay.root, COL_AMS_SOFT);
  setBorderColorIfChanged(bay.root, spoolBorderColor(slot));
  setBorderWidthIfChanged(bay.root, slot.active ? 2 : 1);
  applyColorSwatch(bay.swatch, &bay.swatchFill, &bay.frost, &bay.highlight, &bay.overlay, &bay.paint,
                   bay.swatchSig, slot.colors, slot.colorCount, slot.colorStyle, slot.finish,
                   slot.sparkle, slot.fiber, color, false);
  setTextColorIfChanged(bay.bayLabel, ink);
  if (bay.bayLabel) {
    lv_obj_move_foreground(bay.bayLabel);
  }
  setLabelIfChanged(bay.material, slot.material.length() ? slot.material : String("--"));
  setTextColorIfChanged(bay.material, COL_TEXT);

  // The colour name alone. The slot that feeds is told by its border, and
  // the word after the name cost a line the name may need itself.
  const bool showDesc = slot.colorName.length() > 0;
  setHiddenIfChanged(bay.desc, !showDesc);
  if (showDesc) {
    setLabelIfChanged(bay.desc, slot.colorName);
  }

  const bool showBrand = slot.manufacturer.length() > 0;
  setHiddenIfChanged(bay.brand, !showBrand);
  if (showBrand) {
    setLabelIfChanged(bay.brand, slot.manufacturer);
  }

  if (slot.remainingPercent >= 0) {
    setHiddenIfChanged(bay.remainingRow, false);
    int fill = slot.remainingPercent;
    if (fill > 100) {
      fill = 100;
    }
    setBarValueIfChanged(bay.remainingBar, fill);
    setIndicatorColorIfChanged(bay.remainingBar, remainingBarColor(slot.remainingPercent));
    setLabelIfChanged(bay.pct, remainingAmountText(slot.remainingPercent, slot.remainingGrams));
  } else {
    setHiddenIfChanged(bay.remainingRow, true);
  }
}

void refreshOverviewUnit(int index) {
  if (index < 0 || index >= MAX_OVERVIEW_CARDS) {
    return;
  }
  if (index == 0) {
    rebuildOverviewBinds();
    layoutOverviewBoard();
  }
  OverviewCard &card = overviewCards[index];
  if (!card.root) {
    return;
  }
  static OverviewUnit unit;
  if (!cardUnitFor(index, unit)) {
    setHiddenIfChanged(card.root, true);
    return;
  }

  card.amsId = unit.amsId;
  setHiddenIfChanged(card.root, false);

  String label = unit.label.length() ? unit.label : (String("AMS ") + String(unit.amsId));
  setLabelIfChanged(card.label, label);

  const bool groupedHt = cardBinds[index].count > 1;
  if (groupedHt) {
    applyHeaderClimate(card, String(), String(), unit.drying);
  } else {
    applyHeaderClimate(card, unit.temperature, unit.humidity, unit.drying);
  }

  // Regular AMS always lays its slots in one horizontal row. The 2×2 cell
  // layout is only for a grouped AMS-HT card that has more than two bays.
  const bool compactAux = isAuxKind(unit.amsKind) && unit.slotCount <= 2;
  const bool cellBay = index >= AMS2PRO_COUNT && !compactAux;
  for (int c = 0; c < MAX_OVERVIEW_SLOTS; ++c) {
    refreshOverviewBay(card.bays[c], unit, c, cellBay, groupedHt);
  }
}

void refreshOverviewPage() {
  rebuildOverviewBinds();
  for (int i = 0; i < MAX_OVERVIEW_CARDS; ++i) {
    refreshOverviewUnit(i);
  }
}

void refreshAlertRow(int index) {
  if (index == 0) {
    setHiddenIfChanged(alertsEmpty, alertItemCount > 0);
  }
  if (index < 0 || index >= MAX_ALERTS) {
    return;
  }
  AlertRow &row = alertRows[index];
  if (!row.root) {
    return;
  }
  if (index >= alertItemCount) {
    setHiddenIfChanged(row.root, true);
    return;
  }
  setHiddenIfChanged(row.root, false);
  const AlertItem &item = alertItems[index];
  setLabelIfChanged(row.title, item.title.length() ? item.title : String("Alert"));
  setLabelIfChanged(row.detail, item.detail.length() ? item.detail : dashOr(item.source, ""));

  uint32_t border = COL_INFO;
  String sev = item.severity;
  sev.toLowerCase();
  if (sev == "danger") {
    border = COL_DANGER;
  } else if (sev == "warn") {
    border = COL_WARN;
  }
  setBorderColorIfChanged(row.root, border);
  setBorderWidthIfChanged(row.root, 2);
}

void refreshAlertsPage() {
  setHiddenIfChanged(alertsEmpty, alertItemCount > 0);
  for (int i = 0; i < MAX_ALERTS; ++i) {
    refreshAlertRow(i);
  }
}

void refreshStatusHero() {
  // Default PrinterState is "offline" until the first snapshot. That is not a
  // report from the printer.
  const String state = lastPrinterUpdateMs == 0
                           ? String("LINKING")
                           : (printer.state.length() ? printer.state : String("unknown"));
  setLabelIfChanged(statusUi.stateLabel, state);
  setBgColorIfChanged(statusUi.statePill, stateColor(state));
  setTextColorIfChanged(statusUi.stateLabel, COL_BG);

  int progress = printer.progress >= 0 ? printer.progress : 0;
  if (progress > 100) {
    progress = 100;
  }
  setBarValueIfChanged(statusUi.progressBar, progress);
  setLabelIfChanged(statusUi.progressHuge, printer.progress >= 0 ? String(printer.progress) + "%" : String("--%"));
  setLabelIfChanged(statusUi.timeLeftHuge, formatTimeLeft(printer));
  setLabelIfChanged(statusUi.jobLabel, printer.jobName.length() ? printer.jobName : String("No active job"));
  setLabelIfChanged(statusUi.etaClockLabel, printer.etaClock.length() ? printer.etaClock : String(" "));
}

void refreshStatusTemps() {
  setMetric(statusUi.nozzleL, formatTempPair(printer.nozzle, printer.nozzleTarget));
  setMetric(statusUi.nozzleR, formatTempPair(printer.nozzle2, printer.nozzle2Target, true));
  setMetric(statusUi.bed, formatTempPair(printer.bed, printer.bedTarget));
  setMetric(statusUi.chamber, formatTempPair(printer.chamber, printer.chamberTarget));

  setTileHeating(statusUi.nozzleL, printer.nozzleHeating, printer.activeExtruder == 0);
  setTileHeating(statusUi.nozzleR, printer.nozzle2Heating, printer.activeExtruder == 1);
  setTileHeating(statusUi.bed, printer.bedHeating, false);
  setTileHeating(statusUi.chamber, printer.chamberHeating, false);
}

void refreshStatusActive() {
  const uint32_t activeColor =
      printer.activeColorCount > 0 ? printer.activeColors[0] : parseColor(printer.activeColor);
  applyColorSwatch(statusUi.activeSwatch, &statusUi.activeFill, &statusUi.activeFrost,
                   &statusUi.activeHighlight, &statusUi.activeOverlay, &statusUi.paint,
                   statusUi.swatchSig, printer.activeColors, printer.activeColorCount,
                   printer.activeColorStyle, printer.activeFinish, printer.activeSparkle,
                   printer.activeFiber, activeColor, !printer.activeMaterial.length() &&
                                                         !printer.activeLabel.length());
  if (printer.activeFinish != SwatchFinish::Glow) {
    lv_obj_set_style_border_width(statusUi.activeSwatch, 1, 0);
    lv_obj_set_style_border_color(statusUi.activeSwatch, lv_color_hex(COL_BORDER), 0);
  }
  setLabelIfChanged(statusUi.activeLabel,
                    printer.activeLabel.length() ? printer.activeLabel : String("No active tray"));
  String activeRem = remainingBothText(printer.activeRemainingPercent, printer.activeRemainingGrams);
  if (!activeRem.length()) {
    activeRem = printer.activeRemaining.length() ? printer.activeRemaining : String("--");
  }
  setLabelIfChanged(statusUi.activeRemaining, activeRem);

  const String state = printer.state.length() ? printer.state : "unknown";
  String secondary;
  if (stateContains(state, "ERROR") || stateContains(state, "FAIL") || stateContains(state, "PAUSE")) {
    if (alertItemCount > 0 && alertItems[0].title.length()) {
      secondary = alertItems[0].title;
      if (alertItems[0].detail.length()) {
        secondary += " - ";
        secondary += alertItems[0].detail;
      }
    } else if (printer.hms.length()) {
      secondary = String("HMS  ") + printer.hms;
    } else {
      secondary = "Check printer / alerts";
    }
  } else if (stateContains(state, "RUN") || stateContains(state, "PRINT")) {
    if (printer.layer >= 0) {
      secondary += "Layer ";
      secondary += String(printer.layer);
      if (printer.totalLayers >= 0) {
        secondary += "/";
        secondary += String(printer.totalLayers);
      }
    } else {
      secondary += "Layer --";
    }
    secondary += "   ";
    secondary += dashOr(printer.speedPreset, "Speed --");
    secondary += "   ";
    if (printer.filamentUsed.length()) {
      secondary += printer.filamentUsed;
      if (printer.filamentUsed.indexOf('g') < 0 && printer.filamentUsed.indexOf('G') < 0) {
        secondary += "g";
      }
    } else {
      secondary += "Filament --";
    }
    int elapsedSec = printer.elapsedSeconds;
    if (elapsedSec < 0 && printer.elapsedMinutes >= 0) {
      elapsedSec = printer.elapsedMinutes * 60;
    }
    if (elapsedSec >= 0) {
      secondary += "   Elapsed ";
      const int hours = elapsedSec / 3600;
      const int mins = (elapsedSec % 3600) / 60;
      if (hours > 0) {
        secondary += String(hours);
        secondary += "h ";
        secondary += String(mins);
        secondary += "m";
      } else {
        secondary += String(mins);
        secondary += "m";
      }
    }
  } else {
    secondary = "Idle";
    if (printer.queueKnown && printer.nextJob.length()) {
      secondary += "  Next ready";
    } else if (printer.queueKnown) {
      secondary = "Idle - queue empty";
    }
    if (!printer.connected) {
      secondary = lastPrinterUpdateMs == 0 ? String("Linking to FilaMan") : String("Printer offline");
    }
  }
  setLabelIfChanged(statusUi.secondaryLabel, secondary);
}

void refreshStatusQueue() {
  if (!printer.queueKnown) {
    setLabelIfChanged(statusUi.nextJobLabel, "Next  --");
    return;
  }
  String nextJob = "Next  ";
  nextJob += printer.nextJob.length() ? printer.nextJob : "queue empty";
  nextJob += "   Queue ";
  nextJob += String(printer.queueDepth);
  setLabelIfChanged(statusUi.nextJobLabel, nextJob);
}

void refreshStatusPage() {
  refreshStatusHero();
  refreshStatusTemps();
  refreshStatusActive();
  refreshStatusQueue();
}

void layoutDetailColumns();

void refreshPage() {
  refreshHeader();
  lv_timer_handler();
  refreshAlertBanner();
  lv_timer_handler();

  const bool status = isStatusPage();
  const bool overview = isOverviewPage();
  const bool alerts = isAlertsPage();
  const bool detail = isAmsDetailPage() || isHtPage();

  setHiddenIfChanged(statusUi.root, !status);
  setHiddenIfChanged(overviewRoot, !overview);
  setHiddenIfChanged(columnsRoot, !detail);
  setHiddenIfChanged(alertsRoot, !alerts);
  lv_timer_handler();

  if (status) {
    refreshStatusHero();
    lv_timer_handler();
    refreshStatusTemps();
    lv_timer_handler();
    refreshStatusActive();
    lv_timer_handler();
    refreshStatusQueue();
  } else if (overview) {
    for (int i = 0; i < MAX_OVERVIEW_CARDS; ++i) {
      refreshOverviewUnit(i);
      lv_timer_handler();
    }
  } else if (alerts) {
    for (int i = 0; i < MAX_ALERTS; ++i) {
      refreshAlertRow(i);
      lv_timer_handler();
    }
  } else if (detail) {
    // AMS 2 Pro always has 4 slots in a 2x2 grid; the HT page shows one column
    // per connected AMS-HT unit (grouped up to AMSHT_MAX_COUNT per HT button).
    layoutDetailColumns();
    const int visibleColumns = isHtPage() ? htPresentCount() : COLUMNS_PER_PAGE;
    for (int column = 0; column < COLUMNS_PER_PAGE; ++column) {
      const bool show = column < visibleColumns;
      setHiddenIfChanged(columns[column].root, !show);
      if (show) {
        refreshColumn(column);
      }
      lv_timer_handler();
    }
  }
}

bool slotIsVisible(const SlotState &slot) {
  if (!isAmsDetailPage() && !isHtPage()) {
    return false;
  }
  if (isHtPage()) {
    return slot.amsId >= AMS_HT_FIRST_ID && slot.amsId < AMS_HT_FIRST_ID + AMSHT_MAX_COUNT;
  }
  return slot.amsId == amsIdForPage();
}

void gotoPage(int page);

void navChipEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ui_note_activity();
  const intptr_t page = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  gotoPage(static_cast<int>(page));
}

void alertBannerEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  gotoPage(ALERTS_PAGE);
}

void overviewCardEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ui_note_activity();
  const intptr_t card = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  const int unitIndex = unitIndexForCard(static_cast<int>(card));
  if (unitIndex < 0 || unitIndex >= overviewUnitCount) {
    return;
  }
  const OverviewUnit &unit = overviewUnits[unitIndex];
  String kind = unit.amsKind;
  kind.toLowerCase();
  if (kind.indexOf("ht") >= 0) {
    gotoPage(AMS_HT_PAGE);
  } else if (kind.indexOf("ext") >= 0) {
    return;
  } else if (unit.amsId >= 0 && unit.amsId < AMS2PRO_COUNT) {
    gotoPage(AMS2PRO_FIRST_PAGE + unit.amsId);
  }
}

void gotoPage(int page) {
  if (page < 0 || page >= TOTAL_PAGE_COUNT) {
    return;
  }
  ui_note_activity();
  touch_input_cooldown(260);
  if (lv_indev_t *indev = lv_indev_get_act()) {
    lv_indev_wait_release(indev);
  }
  if (page == currentPage) {
    refreshNav();
    return;
  }
  currentPage = page;

  // Instant, cheap feedback: active tab, page title, and reveal the target root.
  // These are small invalidations; the root swap is the one unavoidable
  // full-area flush, kept short by the per-strip pacing in flushDisplay().
  refreshNav();
  refreshHeaderLabels();
  refreshAlertBanner();
  const bool status = isStatusPage();
  const bool overview = isOverviewPage();
  const bool alerts = isAlertsPage();
  const bool detail = isAmsDetailPage() || isHtPage();
  setHiddenIfChanged(statusUi.root, !status);
  setHiddenIfChanged(overviewRoot, !overview);
  setHiddenIfChanged(columnsRoot, !detail);
  setHiddenIfChanged(alertsRoot, !alerts);
  if (detail) {
    layoutDetailColumns();
  }
  if (headerBar) {
    lv_obj_update_layout(headerBar);
    lv_obj_invalidate(headerBar);
  }

  // Repaint the body incrementally across loop ticks via the staged apply path
  // (one group per tick) instead of one synchronous ~200 ms refreshPage(). That
  //200 ms blit was monopolizing PSRAM and starving RGB scanout -> black flash.
  applyStage = 0;
  dataDirty = false;
}

void layoutDetailColumns() {
  if (!columnsRoot) {
    return;
  }
  // 2x2 slot grid for AMS units and 3-4 HT units. One or two HT units use a
  // full-height row. At two columns, 392px cards fill the 792px inner width.
  const bool ht = isHtPage();
  const int htCount = ht ? htPresentCount() : COLUMNS_PER_PAGE;
  const bool singleHt = ht && htCount == 1;
  const bool oneHtRow = ht && htCount <= 2;
  const int colGap = 8;
  const int fullW = SCREEN_WIDTH - 8;
  const int cardW = singleHt ? fullW : (fullW - colGap) / 2;

  lv_obj_set_flex_flow(columnsRoot, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(columnsRoot, colGap, 0);
  lv_obj_set_style_pad_row(columnsRoot, 8, 0);
  lv_obj_set_flex_align(columnsRoot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_SPACE_BETWEEN);
  for (int i = 0; i < COLUMNS_PER_PAGE; ++i) {
    if (!columns[i].root) {
      continue;
    }
    lv_obj_set_flex_grow(columns[i].root, 0);
    lv_obj_set_width(columns[i].root, cardW);
    lv_obj_set_height(columns[i].root, lv_pct(oneHtRow ? 100 : 48));
  }
}

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
}

void buildStatusPage(lv_obj_t *parent) {
  statusUi.root = lv_obj_create(parent);
  lv_obj_remove_style_all(statusUi.root);
  lv_obj_set_width(statusUi.root, lv_pct(100));
  lv_obj_set_flex_grow(statusUi.root, 1);
  lv_obj_set_flex_flow(statusUi.root, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(statusUi.root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *contentCol = statusUi.root;
  lv_obj_set_width(contentCol, lv_pct(100));
  lv_obj_set_flex_grow(contentCol, 1);
  lv_obj_set_height(contentCol, lv_pct(100));
  lv_obj_set_flex_flow(contentCol, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(contentCol, 6, 0);
  lv_obj_clear_flag(contentCol, LV_OBJ_FLAG_SCROLLABLE);

  statusUi.hero = lv_obj_create(contentCol);
  lv_obj_add_style(statusUi.hero, &heroStyle, 0);
  lv_obj_set_width(statusUi.hero, lv_pct(100));
  lv_obj_set_height(statusUi.hero, 118);
  lv_obj_set_flex_flow(statusUi.hero, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(statusUi.hero, 12, 0);
  lv_obj_set_style_pad_row(statusUi.hero, 6, 0);
  lv_obj_clear_flag(statusUi.hero, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *heroTop = lv_obj_create(statusUi.hero);
  lv_obj_remove_style_all(heroTop);
  lv_obj_set_width(heroTop, lv_pct(100));
  lv_obj_set_height(heroTop, 40);
  lv_obj_set_flex_flow(heroTop, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(heroTop, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(heroTop, 12, 0);

  statusUi.statePill = lv_obj_create(heroTop);
  lv_obj_add_style(statusUi.statePill, &pillStyle, 0);
  lv_obj_set_height(statusUi.statePill, 28);
  lv_obj_set_style_pad_hor(statusUi.statePill, 12, 0);
  lv_obj_set_style_pad_ver(statusUi.statePill, 4, 0);
  lv_obj_clear_flag(statusUi.statePill, LV_OBJ_FLAG_SCROLLABLE);
  statusUi.stateLabel = lv_label_create(statusUi.statePill);
  lv_obj_set_style_text_font(statusUi.stateLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(statusUi.stateLabel, lv_color_hex(COL_BG), 0);
  lv_label_set_text(statusUi.stateLabel, "IDLE");

  statusUi.progressHuge = lv_label_create(heroTop);
  lv_obj_set_style_text_font(statusUi.progressHuge, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(statusUi.progressHuge, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(statusUi.progressHuge, "--%");

  statusUi.timeLeftHuge = lv_label_create(heroTop);
  lv_obj_set_style_text_font(statusUi.timeLeftHuge, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(statusUi.timeLeftHuge, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_flex_grow(statusUi.timeLeftHuge, 1);
  lv_obj_set_style_text_align(statusUi.timeLeftHuge, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(statusUi.timeLeftHuge, "--");

  statusUi.jobLabel = makeLabel(statusUi.hero, &lv_font_montserrat_14, lv_color_hex(COL_MUTED));

  lv_obj_t *progressRow = lv_obj_create(statusUi.hero);
  lv_obj_remove_style_all(progressRow);
  lv_obj_set_width(progressRow, lv_pct(100));
  lv_obj_set_height(progressRow, 18);
  lv_obj_set_flex_flow(progressRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(progressRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(progressRow, 12, 0);

  statusUi.progressBar = lv_bar_create(progressRow);
  lv_obj_set_flex_grow(statusUi.progressBar, 1);
  lv_obj_set_height(statusUi.progressBar, 10);
  lv_bar_set_range(statusUi.progressBar, 0, 100);
  lv_obj_set_style_bg_color(statusUi.progressBar, lv_color_hex(COL_BORDER_SOFT), LV_PART_MAIN);
  lv_obj_set_style_bg_color(statusUi.progressBar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(statusUi.progressBar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(statusUi.progressBar, 0, LV_PART_INDICATOR);

  statusUi.etaClockLabel = lv_label_create(progressRow);
  lv_obj_set_style_text_font(statusUi.etaClockLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(statusUi.etaClockLabel, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_width(statusUi.etaClockLabel, 130);
  lv_obj_set_style_text_align(statusUi.etaClockLabel, LV_TEXT_ALIGN_RIGHT, 0);

  statusUi.activeCard = lv_obj_create(contentCol);
  lv_obj_add_style(statusUi.activeCard, &cardStyle, 0);
  lv_obj_set_width(statusUi.activeCard, lv_pct(100));
  lv_obj_set_height(statusUi.activeCard, 52);
  lv_obj_set_flex_flow(statusUi.activeCard, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(statusUi.activeCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(statusUi.activeCard, 10, 0);
  lv_obj_set_style_pad_column(statusUi.activeCard, 12, 0);
  lv_obj_clear_flag(statusUi.activeCard, LV_OBJ_FLAG_SCROLLABLE);

  statusUi.activeSwatch = lv_obj_create(statusUi.activeCard);
  lv_obj_remove_style_all(statusUi.activeSwatch);
  lv_obj_set_size(statusUi.activeSwatch, 34, 34);
  lv_obj_set_style_radius(statusUi.activeSwatch, 8, 0);
  lv_obj_set_style_bg_opa(statusUi.activeSwatch, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(statusUi.activeSwatch, 1, 0);
  lv_obj_set_style_border_color(statusUi.activeSwatch, lv_color_hex(COL_BORDER), 0);

  lv_obj_t *activeText = lv_obj_create(statusUi.activeCard);
  lv_obj_remove_style_all(activeText);
  lv_obj_set_flex_grow(activeText, 1);
  lv_obj_set_height(activeText, lv_pct(100));
  lv_obj_set_flex_flow(activeText, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(activeText, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(activeText, 1, 0);
  lv_obj_clear_flag(activeText, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *activeCaption = makeLabel(activeText, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  lv_label_set_text(activeCaption, "ACTIVE FILAMENT");
  statusUi.activeLabel = makeLabel(activeText, &lv_font_montserrat_14, lv_color_hex(COL_TEXT));

  statusUi.activeRemaining = lv_label_create(statusUi.activeCard);
  lv_obj_set_style_text_font(statusUi.activeRemaining, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(statusUi.activeRemaining, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_width(statusUi.activeRemaining, 120);
  lv_obj_set_style_text_align(statusUi.activeRemaining, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(statusUi.activeRemaining, "--");

  lv_obj_t *tempsRow = lv_obj_create(contentCol);
  lv_obj_remove_style_all(tempsRow);
  lv_obj_set_width(tempsRow, lv_pct(100));
  lv_obj_set_height(tempsRow, 64);
  lv_obj_set_flex_flow(tempsRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(tempsRow, 6, 0);
  statusUi.nozzleL = makeMetricTile(tempsRow, "NOZZLE L");
  statusUi.nozzleR = makeMetricTile(tempsRow, "NOZZLE R");
  statusUi.bed = makeMetricTile(tempsRow, "BED");
  statusUi.chamber = makeMetricTile(tempsRow, "CHAMBER");

  lv_obj_t *metaCard = lv_obj_create(contentCol);
  lv_obj_add_style(metaCard, &cardStyle, 0);
  lv_obj_set_width(metaCard, lv_pct(100));
  lv_obj_set_height(metaCard, 52);
  lv_obj_set_flex_flow(metaCard, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(metaCard, 8, 0);
  lv_obj_set_style_pad_row(metaCard, 4, 0);
  lv_obj_clear_flag(metaCard, LV_OBJ_FLAG_SCROLLABLE);

  statusUi.secondaryLabel = makeLabel(metaCard, &lv_font_montserrat_12, lv_color_hex(COL_TEXT));
  statusUi.nextJobLabel = makeLabel(metaCard, &lv_font_montserrat_12, lv_color_hex(COL_MUTED));
}

void passClicksToParent(lv_obj_t *obj) {
  const uint32_t count = lv_obj_get_child_cnt(obj);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t *child = lv_obj_get_child(obj, i);
    lv_obj_clear_flag(child, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(child, LV_OBJ_FLAG_SCROLLABLE);
    passClicksToParent(child);
  }
}

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

void buildOverviewPage(lv_obj_t *parent) {
  overviewRoot = lv_obj_create(parent);
  lv_obj_remove_style_all(overviewRoot);
  lv_obj_set_width(overviewRoot, lv_pct(100));
  lv_obj_set_flex_grow(overviewRoot, 1);
  lv_obj_set_flex_flow(overviewRoot, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(overviewRoot, 6, 0);
  lv_obj_clear_flag(overviewRoot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(overviewRoot, LV_OBJ_FLAG_HIDDEN);

  overviewHint = lv_label_create(overviewRoot);
  lv_obj_set_width(overviewHint, lv_pct(100));
  lv_obj_set_style_text_font(overviewHint, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(overviewHint, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_align(overviewHint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(overviewHint, 24, 0);
  lv_label_set_text(overviewHint, "Linking to FilaMan");

  overviewMain = lv_obj_create(overviewRoot);
  lv_obj_remove_style_all(overviewMain);
  lv_obj_set_width(overviewMain, lv_pct(100));
  lv_obj_set_height(overviewMain, lv_pct(100));
  lv_obj_set_flex_grow(overviewMain, 0);
  lv_obj_set_flex_flow(overviewMain, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(overviewMain, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(overviewMain, 8, 0);
  // The same gap as between the rows of the page, see layoutOverviewBoard.
  lv_obj_set_style_pad_row(overviewMain, 6, 0);
  lv_obj_clear_flag(overviewMain, LV_OBJ_FLAG_SCROLLABLE);

  overviewAux = lv_obj_create(overviewRoot);
  lv_obj_remove_style_all(overviewAux);
  lv_obj_set_width(overviewAux, lv_pct(100));
  lv_obj_set_height(overviewAux, 0);
  lv_obj_set_flex_grow(overviewAux, 0);
  lv_obj_set_flex_flow(overviewAux, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(overviewAux, 6, 0);
  lv_obj_clear_flag(overviewAux, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < MAX_OVERVIEW_CARDS; ++i) {
    OverviewCard &card = overviewCards[i];
    lv_obj_t *row = (i < AMS2PRO_COUNT) ? overviewMain : overviewAux;
    card.root = lv_obj_create(row);
    lv_obj_remove_style_all(card.root);
    lv_obj_set_style_bg_opa(card.root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card.root, lv_color_hex(COL_AMS_CARD), 0);
    lv_obj_set_style_border_width(card.root, 1, 0);
    lv_obj_set_style_border_color(card.root, lv_color_hex(0x1F3A2C), 0);
    lv_obj_set_style_radius(card.root, 0, 0);
    lv_obj_set_style_pad_all(card.root, 6, 0);
    lv_obj_set_style_pad_row(card.root, 4, 0);
    lv_obj_set_style_min_width(card.root, 0, 0);
    if (i < AMS2PRO_COUNT) {
      // Width/height are refined in layoutOverviewBoard (1 or 2 units per row).
      lv_obj_set_flex_grow(card.root, 0);
      lv_obj_set_width(card.root, lv_pct(48));
      lv_obj_set_height(card.root, lv_pct(100));
    } else {
      lv_obj_set_flex_grow(card.root, 1);
      lv_obj_set_height(card.root, lv_pct(100));
    }
    lv_obj_set_flex_flow(card.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card.root, overviewCardEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    lv_obj_add_flag(card.root, LV_OBJ_FLAG_HIDDEN);

    card.head = lv_obj_create(card.root);
    lv_obj_remove_style_all(card.head);
    lv_obj_set_width(card.head, lv_pct(100));
    lv_obj_set_height(card.head, 18);
    lv_obj_set_flex_flow(card.head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card.head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                         LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card.head, 4, 0);
    lv_obj_clear_flag(card.head, LV_OBJ_FLAG_SCROLLABLE);

    card.label = lv_label_create(card.head);
    lv_obj_set_style_text_font(card.label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(card.label, lv_color_hex(COL_TEXT), 0);
    lv_label_set_long_mode(card.label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(card.label, 56);

    ensureClimateIcons();
    card.climateBox = lv_obj_create(card.head);
    lv_obj_remove_style_all(card.climateBox);
    lv_obj_set_height(card.climateBox, 16);
    lv_obj_set_flex_grow(card.climateBox, 1);
    lv_obj_set_flex_flow(card.climateBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card.climateBox, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                         LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card.climateBox, 3, 0);
    lv_obj_clear_flag(card.climateBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card.climateBox, LV_OBJ_FLAG_CLICKABLE);

    card.tempIcon = makeClimateIcon(card.climateBox, &iconThermo);
    card.tempText = lv_label_create(card.climateBox);
    lv_obj_set_style_text_font(card.tempText, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(card.tempText, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_width(card.tempText, LV_SIZE_CONTENT);
    lv_label_set_text(card.tempText, "");

    card.humIcon = makeClimateIcon(card.climateBox, &iconDrop);
    lv_obj_set_style_pad_left(card.humIcon, 6, 0);
    card.humText = lv_label_create(card.climateBox);
    lv_obj_set_style_text_font(card.humText, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(card.humText, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_width(card.humText, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(card.humText, 2, 0);
    lv_label_set_text(card.humText, "");

    card.bayRow = lv_obj_create(card.root);
    lv_obj_remove_style_all(card.bayRow);
    lv_obj_set_width(card.bayRow, lv_pct(100));
    lv_obj_set_flex_grow(card.bayRow, 1);
    // AMS slots stay in one row. HT cards may wrap into a 2×2 when grouped.
    lv_obj_set_flex_flow(card.bayRow,
                         i < AMS2PRO_COUNT ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(card.bayRow, 4, 0);
    lv_obj_set_style_pad_column(card.bayRow, 4, 0);
    lv_obj_clear_flag(card.bayRow, LV_OBJ_FLAG_SCROLLABLE);

    for (int b = 0; b < MAX_OVERVIEW_SLOTS; ++b) {
      buildOverviewBay(card.bays[b], card.bayRow, i >= AMS2PRO_COUNT);
    }
    // Slots, labels, and bars sit on top of the card. Leave them non-clickable
    // so a tap anywhere on the unit opens that unit's page.
    passClicksToParent(card.root);
  }
}

void buildAlertsPage(lv_obj_t *parent) {
  alertsRoot = lv_obj_create(parent);
  lv_obj_remove_style_all(alertsRoot);
  lv_obj_set_width(alertsRoot, lv_pct(100));
  lv_obj_set_flex_grow(alertsRoot, 1);
  lv_obj_set_flex_flow(alertsRoot, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(alertsRoot, 8, 0);
  lv_obj_add_flag(alertsRoot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(alertsRoot, LV_DIR_VER);

  alertsEmpty = makeLabel(alertsRoot, &lv_font_montserrat_16, lv_color_hex(COL_MUTED));
  lv_label_set_text(alertsEmpty, "No alerts");
  lv_obj_set_style_text_align(alertsEmpty, LV_TEXT_ALIGN_CENTER, 0);

  for (int i = 0; i < MAX_ALERTS; ++i) {
    AlertRow &row = alertRows[i];
    row.root = lv_obj_create(alertsRoot);
    lv_obj_add_style(row.root, &cardStyle, 0);
    lv_obj_set_width(row.root, lv_pct(100));
    lv_obj_set_height(row.root, 64);
    lv_obj_set_flex_flow(row.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(row.root, 10, 0);
    lv_obj_set_style_pad_row(row.root, 4, 0);
    lv_obj_clear_flag(row.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row.root, LV_OBJ_FLAG_HIDDEN);

    row.title = makeLabel(row.root, &lv_font_montserrat_14, lv_color_hex(COL_TEXT));
    row.detail = makeLabel(row.root, &lv_font_montserrat_12, lv_color_hex(COL_MUTED));
  }
}

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
}

void hideIpToast() {
  if (ipToast) {
    setHiddenIfChanged(ipToast, true);
  }
  ipToastUntilMs = 0;
}

void showIpToast() {
  if (!ipToast || !ipToastLabel) {
    return;
  }
  String text;
  if (panelIp.length()) {
    text = "IP  ";
    text += panelIp;
    text += "\nSSID  ";
    text += WIFI_SSID;
  } else if (connectionMessage.length()) {
    text = connectionMessage;
  } else {
    text = "No IP yet";
  }
  setLabelIfChanged(ipToastLabel, text);
  setHiddenIfChanged(ipToast, false);
  lv_obj_move_foreground(ipToast);
  ipToastUntilMs = millis() + IP_TOAST_MS;
}

void ipToastEvent(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    hideIpToast();
  }
}

void connIconEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ui_note_activity();
  wifi_setup_open();  // WiFi info + scan/change
}

void menuEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ui_note_activity();
  menu_open();
}

void versionEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ui_note_activity();
  menu_open_update();
}

void backEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  gotoPage(AMS_OVERVIEW_PAGE);
}

void printerStatusEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  if (!isOverviewPage()) {
    return;
  }
  gotoPage(STATUS_PAGE);
}

// Floats above every page on the top layer so the IP is reachable from anywhere.
void buildIpToast() {
  ipToast = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(ipToast);
  lv_obj_set_style_bg_color(ipToast, lv_color_hex(COL_SURFACE_2), 0);
  lv_obj_set_style_bg_opa(ipToast, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ipToast, 1, 0);
  lv_obj_set_style_border_color(ipToast, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_radius(ipToast, 12, 0);
  lv_obj_set_style_pad_all(ipToast, 14, 0);
  lv_obj_set_size(ipToast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(ipToast, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_clear_flag(ipToast, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ipToast, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(ipToast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(ipToast, ipToastEvent, LV_EVENT_CLICKED, nullptr);

  ipToastLabel = lv_label_create(ipToast);
  lv_obj_set_style_text_font(ipToastLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(ipToastLabel, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(ipToastLabel, "");
}

void applyConnectionIcon(const String &message) {
  if (!connIcon) {
    return;
  }
  if (message.indexOf("MQTT connected") >= 0 || message.indexOf("FilaMan linked") >= 0) {
    setLabelIfChanged(connIcon, LV_SYMBOL_WIFI);
    setTextColorIfChanged(connIcon, COL_ACCENT);
    return;
  }
  if (message.indexOf("MQTT") >= 0 || message.indexOf("FilaMan") >= 0) {
    setLabelIfChanged(connIcon, LV_SYMBOL_WIFI);
    setTextColorIfChanged(connIcon, COL_WARN);
    return;
  }
  if (message.indexOf("WiFi") >= 0) {
    const bool hardFail =
        message.indexOf("not found") >= 0 || message.indexOf("auth failed") >= 0 ||
        message.indexOf("fail") >= 0 || message.indexOf("disconnected") >= 0 ||
        message.indexOf("lost") >= 0 || message.indexOf("SSID not") >= 0;
    if (hardFail) {
      setLabelIfChanged(connIcon, LV_SYMBOL_CLOSE);
      setTextColorIfChanged(connIcon, COL_DANGER);
    } else {
      setLabelIfChanged(connIcon, LV_SYMBOL_WIFI);
      setTextColorIfChanged(connIcon, COL_WARN);
    }
    return;
  }
  setLabelIfChanged(connIcon, LV_SYMBOL_WARNING);
  setTextColorIfChanged(connIcon, COL_WARN);
}

// AMS View only. The label width is fixed, so swapping % and g does not reflow.
void refreshRemainingLabelsOnly() {
  if (!isOverviewPage()) {
    return;
  }
  for (int i = 0; i < MAX_OVERVIEW_CARDS; ++i) {
    OverviewCard &card = overviewCards[i];
    if (!card.root || lv_obj_has_flag(card.root, LV_OBJ_FLAG_HIDDEN)) {
      continue;
    }
    OverviewUnit unit;
    if (!cardUnitFor(i, unit)) {
      continue;
    }
    for (int c = 0; c < unit.slotCount && c < MAX_OVERVIEW_SLOTS; ++c) {
      const OverviewSlot &slot = unit.slots[c];
      if (slot.remainingPercent < 0 && slot.remainingGrams < 0) {
        continue;
      }
      setLabelIfChanged(card.bays[c].pct, remainingAmountText(slot.remainingPercent, slot.remainingGrams));
    }
  }
}

}  // namespace

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
  refreshPage();

  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  Serial.printf("lvgl mem used=%u%% free=%u frag=%u%%\n", mon.used_pct, mon.free_size, mon.frag_pct);
}

void ui_note_activity() {
  lastActivityMs = millis();
}

void ui_note_fresh() { lastPrinterUpdateMs = millis(); }

// Applied one stage per tick so LVGL cannot join distant dirty rectangles into
// one full-screen blit (that was the multi-hundred-ms stall / black flash).
void applyDataStage(int stage) {
  // Shared chrome: 0..3
  if (stage == 0) {
    if (connectionDirty) {
      connectionDirty = false;
      applyConnectionIcon(connectionMessage);
    }
    return;
  }
  if (stage == 1) {
    refreshFreshness();
    return;
  }
  if (stage == 2) {
    refreshAlertBanner();
    return;
  }
  if (stage == 3) {
    refreshHeaderLabels();
    return;
  }
  if (stage == 4) {
    refreshFooter();
    return;
  }

  const int body = stage - 5;
  if (isStatusPage()) {
    switch (body) {
      case 0:
        refreshStatusHero();
        break;
      case 1:
        refreshStatusTemps();
        break;
      case 2:
        refreshStatusActive();
        break;
      case 3:
        refreshStatusQueue();
        break;
      default:
        break;
    }
    return;
  }
  if (isOverviewPage()) {
    refreshOverviewUnit(body);
    return;
  }
  if (isAlertsPage()) {
    refreshAlertRow(body);
    return;
  }
  if (body < COLUMNS_PER_PAGE) {
    const int visibleColumns = isHtPage() ? htPresentCount() : COLUMNS_PER_PAGE;
    const bool show = body < visibleColumns;
    setHiddenIfChanged(columns[body].root, !show);
    if (show) {
      refreshColumn(body);
    }
  }
}

int applyStageCount() {
  if (isStatusPage()) {
    return 5 + 4;
  }
  if (isOverviewPage()) {
    return 5 + MAX_OVERVIEW_CARDS;
  }
  if (isAlertsPage()) {
    return 5 + MAX_ALERTS;
  }
  return 5 + COLUMNS_PER_PAGE;
}

void markDataDirty() { dataDirty = true; }

void ui_loop() {
  const unsigned long now = millis();
  if (ipToastUntilMs != 0 && now >= ipToastUntilMs) {
    hideIpToast();
  }
  if (now - lastFreshnessRefreshMs >= 1000UL) {
    lastFreshnessRefreshMs = now;
    refreshFreshness();
  }
  if (now - remainingPhaseMs >= 10000UL) {
    remainingPhaseMs = now;
    showRemainingGrams = !showRemainingGrams;
    // Skip while a staged repaint is running; that pass reads the new phase.
    if (applyStage < 0) {
      refreshRemainingLabelsOnly();
    }
  }

  // If the AMS unit we're viewing was unplugged (or the monitored printer
  // changed), its nav chip is gone - bounce back to AMS View.
  if (!pageVisibleForNav(currentPage)) {
    gotoPage(AMS_OVERVIEW_PAGE);
  }

  if (applyStage >= 0) {
    if (now - lastActivityMs >= UI_APPLY_QUIET_MS) {
      for (int step = 0; step < 2 && applyStage >= 0; ++step) {
        applyDataStage(applyStage);
        if (++applyStage >= applyStageCount()) {
          applyStage = -1;
          lastUiApplyMs = now;
        }
      }
    }
  } else if (dataDirty && (now - lastUiApplyMs >= UI_APPLY_MS) &&
             (now - lastActivityMs >= UI_APPLY_QUIET_MS)) {
    dataDirty = false;
    applyStage = 0;
  }

  // AMS View is home. Other pages return there after idle; stay put on All.
  if (currentPage == AMS_OVERVIEW_PAGE) {
    return;
  }
  const int idleSec = config().idleReturnSec;
  if (idleSec > 0 && now - lastActivityMs >= static_cast<unsigned long>(idleSec) * 1000UL) {
    gotoPage(AMS_OVERVIEW_PAGE);
  }
}

void ui_next_page() {
  gotoPage(nextVisiblePage(currentPage, +1));
}

void ui_prev_page() {
  gotoPage(nextVisiblePage(currentPage, -1));
}

void ui_handle_swipe(int16_t startX, int16_t endX) {
  // Caller already validated threshold/orientation; advance exactly one page.
  if (endX > startX) {
    ui_prev_page();
  } else if (endX < startX) {
    ui_next_page();
  }
}

void ui_update_slot(const SlotState &slot) {
  if (slot.amsId < 0 || slot.amsId >= TOTAL_AMS_IDS || slot.slot < 0 || slot.slot >= COLUMNS_PER_PAGE) {
    return;
  }
  slots[slot.amsId][slot.slot] = slot;
  if (slotIsVisible(slot)) {
    markDataDirty();
  }
}

void ui_update_printer(const PrinterState &newPrinter) {
  printer = newPrinter;
  lastPrinterUpdateMs = millis();
  markDataDirty();
}

void ui_update_meta(const AmsMetaState &meta) {
  if (meta.amsId < 0 || meta.amsId >= TOTAL_AMS_IDS) {
    return;
  }
  metas[meta.amsId] = meta;
  if (isAmsDetailPage() && meta.amsId == amsIdForPage()) {
    markDataDirty();
  } else if (isHtPage() && meta.amsId >= AMS_HT_FIRST_ID) {
    markDataDirty();
  }
}

void ui_update_bridge(const BridgeHealth &health) {
  bridge = health;
  markDataDirty();
}

void ui_update_alerts(const AlertItem *alerts, int count) {
  alertItemCount = 0;
  if (alerts && count > 0) {
    if (count > MAX_ALERTS) {
      count = MAX_ALERTS;
    }
    for (int i = 0; i < count; ++i) {
      alertItems[i] = alerts[i];
    }
    alertItemCount = count;
  }
  markDataDirty();
}

void ui_update_overview(const OverviewUnit *units, int count) {
  overviewUnitCount = 0;
  if (units && count > 0) {
    if (count > MAX_OVERVIEW_UNITS) {
      count = MAX_OVERVIEW_UNITS;
    }
    for (int i = 0; i < count; ++i) {
      overviewUnits[i] = units[i];
    }
    overviewUnitCount = count;
  }
  markDataDirty();
}

void ui_open_page(int page) { gotoPage(page); }

void ui_set_connection_status(const String &message) {
  if (connectionMessage == message) {
    return;
  }
  connectionMessage = message;
  connectionDirty = true;
  markDataDirty();
}

void ui_set_ip_address(const String &ip) {
  if (panelIp == ip) {
    return;
  }
  panelIp = ip;
  refreshFooter();
}

void ui_refresh_update_badge() {
  if (!menuUpdateBadge) {
    return;
  }
  const bool show = config().autoCheckUpdates && ota_update_available();
  if (show) {
    lv_obj_clear_flag(menuUpdateBadge, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(menuUpdateBadge, LV_OBJ_FLAG_HIDDEN);
  }
}

bool ui_has_active_alerts() {
  return hasActiveAlerts();
}
