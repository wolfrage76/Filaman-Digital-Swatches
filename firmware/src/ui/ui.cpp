#include "ui.h"

#include <lvgl.h>

#include "config.h"

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

struct ColumnWidgets {
  lv_obj_t *root = nullptr;
  lv_obj_t *colorRail = nullptr;
  lv_obj_t *title = nullptr;
  lv_obj_t *colorName = nullptr;
  lv_obj_t *manufacturer = nullptr;
  lv_obj_t *material = nullptr;
  lv_obj_t *temps = nullptr;
  lv_obj_t *remainingBar = nullptr;
  lv_obj_t *remaining = nullptr;
  lv_obj_t *cost = nullptr;
  lv_obj_t *meta = nullptr;
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
  lv_obj_t *activeLabel = nullptr;
  lv_obj_t *secondaryLabel = nullptr;
  lv_obj_t *nextJobLabel = nullptr;
  lv_obj_t *hmsLabel = nullptr;
};

SlotState slots[TOTAL_AMS_IDS][COLUMNS_PER_PAGE];
AmsMetaState metas[TOTAL_AMS_IDS];
PrinterState printer;

ColumnWidgets columns[COLUMNS_PER_PAGE];
StatusWidgets statusUi;
NavChip navChips[TOTAL_PAGE_COUNT];
lv_obj_t *screen = nullptr;
lv_obj_t *columnsRoot = nullptr;
lv_obj_t *titleLabel = nullptr;
lv_obj_t *stateLabel = nullptr;
lv_obj_t *connIcon = nullptr;
lv_obj_t *versionLabel = nullptr;
lv_obj_t *ipLabel = nullptr;
lv_obj_t *navRow = nullptr;
String panelIp;
int currentPage = STATUS_PAGE;
unsigned long lastActivityMs = 0;

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
    case 1:
      return "AMS A";
    case 2:
      return "AMS B";
    case 3:
      return "AMS C";
    case 4:
      return "AMS D";
    case AMS_HT_PAGE:
      return "HT";
    default:
      return "?";
  }
}

uint32_t parseColor(const String &value) {
  String text = value;
  text.trim();
  if (text.startsWith("#")) {
    text.remove(0, 1);
  }
  if (text.length() < 6) {
    return 0x202020;
  }
  return strtoul(text.substring(0, 6).c_str(), nullptr, 16);
}

bool isStatusPage() { return currentPage == STATUS_PAGE; }
bool isHtPage() { return currentPage == AMS_HT_PAGE; }
int amsIdForPage() { return currentPage - AMS2PRO_FIRST_PAGE; }

String slotTitleForColumn(int column) {
  if (!isHtPage()) {
    return "SLOT " + String(column + 1);
  }
  return "HT " + String(column + 1);
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
  if (isHtPage()) {
    return "AMS-HT";
  }
  return String("AMS ") + char('A' + amsIdForPage());
}

String amsClimateSummary() {
  if (isStatusPage()) {
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

String formatTempPair(const String &actual, const String &target) {
  if (!actual.length() && !target.length()) {
    return "--";
  }
  String out = actual.length() ? actual : "--";
  if (target.length() && target != "0") {
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

void setLabelIfChanged(lv_obj_t *label, const String &text) {
  if (!label) {
    return;
  }
  const char *current = lv_label_get_text(label);
  if (current && text == current) {
    return;
  }
  lv_label_set_text(label, text.c_str());
}

void setMetric(MetricTile &tile, const String &value) {
  setLabelIfChanged(tile.value, value);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, lv_pct(100));
  return label;
}

MetricTile makeMetricTile(lv_obj_t *parent, const char *caption) {
  MetricTile tile;
  tile.root = lv_obj_create(parent);
  lv_obj_add_style(tile.root, &cardStyle, 0);
  lv_obj_set_flex_grow(tile.root, 1);
  lv_obj_set_height(tile.root, 72);
  lv_obj_set_flex_flow(tile.root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(tile.root, 10, 0);
  lv_obj_set_style_pad_row(tile.root, 4, 0);
  lv_obj_clear_flag(tile.root, LV_OBJ_FLAG_SCROLLABLE);

  tile.caption = makeLabel(tile.root, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  lv_label_set_text(tile.caption, caption);
  tile.value = makeLabel(tile.root, &lv_font_montserrat_18, lv_color_hex(COL_TEXT));
  lv_label_set_text(tile.value, "--");
  return tile;
}

void refreshNav() {
  for (int i = 0; i < TOTAL_PAGE_COUNT; ++i) {
    NavChip &chip = navChips[i];
    if (!chip.root) {
      continue;
    }
    lv_obj_remove_style(chip.root, &navChipActiveStyle, 0);
    if (i == currentPage) {
      lv_obj_add_style(chip.root, &navChipActiveStyle, 0);
      lv_obj_set_style_text_color(chip.label, lv_color_hex(COL_BG), 0);
    } else {
      lv_obj_set_style_text_color(chip.label, lv_color_hex(COL_MUTED), 0);
    }
  }
}

void refreshFooter() {
  refreshNav();
  setLabelIfChanged(ipLabel, panelIp.length() ? panelIp : "--");
}

void refreshHeader() {
  setLabelIfChanged(titleLabel, pageTitle());
  setLabelIfChanged(stateLabel, isStatusPage() ? String("") : amsClimateSummary());
  refreshFooter();
}

void refreshColumn(int column) {
  SlotState &slot = visibleSlot(column);
  ColumnWidgets &widgets = columns[column];

  lv_label_set_text(widgets.title, slotTitleForColumn(column).c_str());
  lv_obj_set_style_bg_color(widgets.colorRail, lv_color_hex(parseColor(slot.color)), 0);

  String colorName = slot.empty ? "Empty" : slot.colorName;
  if (!colorName.length() && !slot.empty) {
    colorName = "Unknown";
  }
  lv_label_set_text(widgets.colorName, colorName.c_str());
  lv_label_set_text(widgets.manufacturer, (slot.empty ? String("--") : dashOr(slot.manufacturer)).c_str());
  lv_label_set_text(widgets.material, (slot.empty ? String("--") : dashOr(slot.material)).c_str());

  String temps;
  if (!slot.empty && (slot.nozzleMin.length() || slot.nozzleMax.length())) {
    temps = slot.nozzleMin.length() ? slot.nozzleMin : "?";
    temps += "-";
    temps += slot.nozzleMax.length() ? slot.nozzleMax : "?";
    temps += " C";
  } else {
    temps = "--";
  }
  lv_label_set_text(widgets.temps, temps.c_str());

  if (slot.empty) {
    lv_bar_set_value(widgets.remainingBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(widgets.colorRail, LV_OPA_40, 0);
    lv_label_set_text(widgets.remaining, "");
    lv_label_set_text(widgets.cost, "");
    lv_label_set_text(widgets.meta, "");
  } else {
    lv_obj_set_style_bg_opa(widgets.colorRail, LV_OPA_COVER, 0);
    int fill = slot.remainingPercent >= 0 ? slot.remainingPercent : 0;
    lv_bar_set_value(widgets.remainingBar, fill, LV_ANIM_OFF);
    lv_color_t fillColor = lv_color_hex(COL_ACCENT);
    if (slot.remainingPercent >= 0 && slot.remainingPercent < 15) {
      fillColor = lv_color_hex(COL_DANGER);
    } else if (slot.remainingPercent >= 0 && slot.remainingPercent < 30) {
      fillColor = lv_color_hex(COL_WARN);
    }
    lv_obj_set_style_bg_color(widgets.remainingBar, fillColor, LV_PART_INDICATOR);

    String rem;
    if (slot.remainingGrams.length()) {
      rem = slot.remainingGrams + "g";
    }
    if (slot.remainingPercent >= 0) {
      if (rem.length()) {
        rem += "  ";
      }
      rem += String(slot.remainingPercent) + "%";
    }
    lv_label_set_text(widgets.remaining, rem.length() ? rem.c_str() : "--");

    if (slot.costRemaining.length()) {
      lv_label_set_text(widgets.cost, ("$" + slot.costRemaining).c_str());
    } else {
      lv_label_set_text(widgets.cost, "");
    }

    String meta;
    meta += slot.rfid ? "RFID" : "No RFID";
    if (slot.backupBadge.length()) {
      meta += " | ";
      meta += slot.backupBadge;
    }
    if (slot.lastUsed.length()) {
      meta += " | ";
      meta += slot.lastUsed;
    }
    lv_label_set_text(widgets.meta, meta.c_str());
  }

  lv_obj_remove_style(widgets.root, &activeColumnStyle, 0);
  lv_obj_remove_style(widgets.root, &emptyColumnStyle, 0);
  if (slot.active) {
    lv_obj_add_style(widgets.root, &activeColumnStyle, 0);
  } else if (slot.empty) {
    lv_obj_add_style(widgets.root, &emptyColumnStyle, 0);
  }
}

void refreshStatusPage() {
  const String state = printer.state.length() ? printer.state : "unknown";
  setLabelIfChanged(statusUi.stateLabel, state);
  lv_obj_set_style_bg_color(statusUi.statePill, lv_color_hex(stateColor(state)), 0);
  lv_obj_set_style_text_color(statusUi.stateLabel, lv_color_hex(COL_BG), 0);

  int progress = printer.progress >= 0 ? printer.progress : 0;
  if (progress > 100) {
    progress = 100;
  }
  lv_bar_set_value(statusUi.progressBar, progress, LV_ANIM_OFF);
  setLabelIfChanged(statusUi.progressHuge, printer.progress >= 0 ? String(printer.progress) + "%" : "--%");
  setLabelIfChanged(statusUi.timeLeftHuge, formatTimeLeft(printer));
  setLabelIfChanged(statusUi.jobLabel, printer.jobName.length() ? printer.jobName : "No active job");
  setLabelIfChanged(statusUi.etaClockLabel, printer.etaClock.length() ? printer.etaClock : " ");

  setMetric(statusUi.nozzleL, formatTempPair(printer.nozzle, printer.nozzleTarget));
  setMetric(statusUi.nozzleR, formatTempPair(printer.nozzle2, printer.nozzle2Target));
  setMetric(statusUi.bed, formatTempPair(printer.bed, printer.bedTarget));
  setMetric(statusUi.chamber, formatTempPair(printer.chamber, ""));

  lv_obj_set_style_border_color(statusUi.nozzleL.root,
                                lv_color_hex(printer.activeExtruder == 0 ? COL_ACCENT : COL_BORDER), 0);
  lv_obj_set_style_border_width(statusUi.nozzleL.root, printer.activeExtruder == 0 ? 2 : 1, 0);
  lv_obj_set_style_border_color(statusUi.nozzleR.root,
                                lv_color_hex(printer.activeExtruder == 1 ? COL_ACCENT : COL_BORDER), 0);
  lv_obj_set_style_border_width(statusUi.nozzleR.root, printer.activeExtruder == 1 ? 2 : 1, 0);

  lv_obj_set_style_bg_color(statusUi.activeSwatch, lv_color_hex(parseColor(printer.activeColor)), 0);
  setLabelIfChanged(statusUi.activeLabel, printer.activeLabel.length() ? printer.activeLabel : "No active tray");

  String secondary;
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
  setLabelIfChanged(statusUi.secondaryLabel, secondary);

  String nextJob = "Next  ";
  nextJob += printer.nextJob.length() ? printer.nextJob : "queue empty";
  setLabelIfChanged(statusUi.nextJobLabel, nextJob);

  if (printer.hms.length()) {
    setLabelIfChanged(statusUi.hmsLabel, String("HMS  ") + printer.hms);
    lv_obj_clear_flag(statusUi.hmsLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(statusUi.hmsLabel, lv_color_hex(COL_DANGER), 0);
  } else {
    lv_obj_add_flag(statusUi.hmsLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

void refreshPage() {
  refreshHeader();
  const bool status = isStatusPage();
  if (statusUi.root) {
    if (status) {
      lv_obj_clear_flag(statusUi.root, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(statusUi.root, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (columnsRoot) {
    if (status) {
      lv_obj_add_flag(columnsRoot, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(columnsRoot, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (status) {
    refreshStatusPage();
  } else {
    for (int column = 0; column < COLUMNS_PER_PAGE; ++column) {
      refreshColumn(column);
    }
  }
}

bool slotIsVisible(const SlotState &slot) {
  if (isStatusPage()) {
    return false;
  }
  if (isHtPage()) {
    return slot.amsId >= AMS_HT_FIRST_ID && slot.amsId < AMS_HT_FIRST_ID + AMSHT_MAX_COUNT;
  }
  return slot.amsId == amsIdForPage();
}

void gotoPage(int page) {
  if (page < 0 || page >= TOTAL_PAGE_COUNT) {
    return;
  }
  ui_note_activity();
  if (page == currentPage) {
    refreshNav();
    return;
  }
  currentPage = page;
  refreshPage();
  lv_obj_invalidate(lv_scr_act());
}

void navChipEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  const intptr_t page = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  gotoPage(static_cast<int>(page));
}

void buildColumn(lv_obj_t *parent, int index) {
  ColumnWidgets &widgets = columns[index];
  widgets.root = lv_obj_create(parent);
  lv_obj_add_style(widgets.root, &columnStyle, 0);
  lv_obj_set_flex_flow(widgets.root, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(widgets.root, 0, 0);
  lv_obj_set_style_pad_column(widgets.root, 0, 0);
  lv_obj_set_flex_grow(widgets.root, 1);
  lv_obj_set_height(widgets.root, lv_pct(100));
  lv_obj_clear_flag(widgets.root, LV_OBJ_FLAG_SCROLLABLE);

  widgets.colorRail = lv_obj_create(widgets.root);
  lv_obj_remove_style_all(widgets.colorRail);
  lv_obj_set_width(widgets.colorRail, 10);
  lv_obj_set_height(widgets.colorRail, lv_pct(100));
  lv_obj_set_style_bg_opa(widgets.colorRail, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(widgets.colorRail, lv_color_hex(0x333333), 0);
  lv_obj_set_style_radius(widgets.colorRail, 0, 0);

  lv_obj_t *body = lv_obj_create(widgets.root);
  lv_obj_remove_style_all(body);
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_height(body, lv_pct(100));
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(body, 12, 0);
  lv_obj_set_style_pad_row(body, 5, 0);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

  widgets.title = makeLabel(body, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  lv_obj_set_style_text_align(widgets.title, LV_TEXT_ALIGN_CENTER, 0);

  widgets.colorName = makeLabel(body, &lv_font_montserrat_18, lv_color_hex(COL_TEXT));
  widgets.manufacturer = makeLabel(body, &lv_font_montserrat_12, lv_color_hex(COL_MUTED));
  widgets.material = makeLabel(body, &lv_font_montserrat_22, lv_color_hex(COL_TEXT));
  widgets.temps = makeLabel(body, &lv_font_montserrat_12, lv_color_hex(COL_MUTED));

  widgets.remainingBar = lv_bar_create(body);
  lv_obj_set_width(widgets.remainingBar, lv_pct(100));
  lv_obj_set_height(widgets.remainingBar, 8);
  lv_bar_set_range(widgets.remainingBar, 0, 100);
  lv_obj_set_style_bg_color(widgets.remainingBar, lv_color_hex(COL_BORDER_SOFT), LV_PART_MAIN);
  lv_obj_set_style_bg_color(widgets.remainingBar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(widgets.remainingBar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(widgets.remainingBar, 4, LV_PART_INDICATOR);

  widgets.remaining = makeLabel(body, &lv_font_montserrat_14, lv_color_hex(COL_TEXT));
  widgets.cost = makeLabel(body, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  widgets.meta = makeLabel(body, &lv_font_montserrat_12, lv_color_hex(COL_DIM));

  lv_obj_set_style_text_align(widgets.colorName, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(widgets.manufacturer, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(widgets.material, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(widgets.temps, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(widgets.remaining, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(widgets.cost, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_align(widgets.meta, LV_TEXT_ALIGN_CENTER, 0);
}

void buildStatusPage(lv_obj_t *parent) {
  statusUi.root = lv_obj_create(parent);
  lv_obj_remove_style_all(statusUi.root);
  lv_obj_set_width(statusUi.root, lv_pct(100));
  lv_obj_set_flex_grow(statusUi.root, 1);
  lv_obj_set_flex_flow(statusUi.root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(statusUi.root, 10, 0);
  lv_obj_clear_flag(statusUi.root, LV_OBJ_FLAG_SCROLLABLE);

  // Hero: state + % + time left + job + progress + ETA clock
  statusUi.hero = lv_obj_create(statusUi.root);
  lv_obj_add_style(statusUi.hero, &heroStyle, 0);
  lv_obj_set_width(statusUi.hero, lv_pct(100));
  lv_obj_set_height(statusUi.hero, 142);
  lv_obj_set_flex_flow(statusUi.hero, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(statusUi.hero, 14, 0);
  lv_obj_set_style_pad_row(statusUi.hero, 8, 0);
  lv_obj_clear_flag(statusUi.hero, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *heroTop = lv_obj_create(statusUi.hero);
  lv_obj_remove_style_all(heroTop);
  lv_obj_set_width(heroTop, lv_pct(100));
  lv_obj_set_height(heroTop, 44);
  lv_obj_set_flex_flow(heroTop, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(heroTop, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(heroTop, 14, 0);

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
  lv_obj_set_style_text_font(statusUi.progressHuge, &lv_font_montserrat_36, 0);
  lv_obj_set_style_text_color(statusUi.progressHuge, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(statusUi.progressHuge, "--%");

  statusUi.timeLeftHuge = lv_label_create(heroTop);
  lv_obj_set_style_text_font(statusUi.timeLeftHuge, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(statusUi.timeLeftHuge, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_flex_grow(statusUi.timeLeftHuge, 1);
  lv_obj_set_style_text_align(statusUi.timeLeftHuge, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(statusUi.timeLeftHuge, "--");

  statusUi.jobLabel = makeLabel(statusUi.hero, &lv_font_montserrat_16, lv_color_hex(COL_MUTED));

  lv_obj_t *progressRow = lv_obj_create(statusUi.hero);
  lv_obj_remove_style_all(progressRow);
  lv_obj_set_width(progressRow, lv_pct(100));
  lv_obj_set_height(progressRow, 20);
  lv_obj_set_flex_flow(progressRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(progressRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(progressRow, 12, 0);

  statusUi.progressBar = lv_bar_create(progressRow);
  lv_obj_set_flex_grow(statusUi.progressBar, 1);
  lv_obj_set_height(statusUi.progressBar, 12);
  lv_bar_set_range(statusUi.progressBar, 0, 100);
  lv_obj_set_style_bg_color(statusUi.progressBar, lv_color_hex(COL_BORDER_SOFT), LV_PART_MAIN);
  lv_obj_set_style_bg_color(statusUi.progressBar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(statusUi.progressBar, 6, LV_PART_MAIN);
  lv_obj_set_style_radius(statusUi.progressBar, 6, LV_PART_INDICATOR);

  statusUi.etaClockLabel = lv_label_create(progressRow);
  lv_obj_set_style_text_font(statusUi.etaClockLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(statusUi.etaClockLabel, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_width(statusUi.etaClockLabel, 130);
  lv_obj_set_style_text_align(statusUi.etaClockLabel, LV_TEXT_ALIGN_RIGHT, 0);

  // Active filament — promoted
  statusUi.activeCard = lv_obj_create(statusUi.root);
  lv_obj_add_style(statusUi.activeCard, &cardStyle, 0);
  lv_obj_set_width(statusUi.activeCard, lv_pct(100));
  lv_obj_set_height(statusUi.activeCard, 64);
  lv_obj_set_flex_flow(statusUi.activeCard, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(statusUi.activeCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(statusUi.activeCard, 12, 0);
  lv_obj_set_style_pad_column(statusUi.activeCard, 14, 0);
  lv_obj_clear_flag(statusUi.activeCard, LV_OBJ_FLAG_SCROLLABLE);

  statusUi.activeSwatch = lv_obj_create(statusUi.activeCard);
  lv_obj_remove_style_all(statusUi.activeSwatch);
  lv_obj_set_size(statusUi.activeSwatch, 40, 40);
  lv_obj_set_style_radius(statusUi.activeSwatch, 10, 0);
  lv_obj_set_style_bg_opa(statusUi.activeSwatch, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(statusUi.activeSwatch, 1, 0);
  lv_obj_set_style_border_color(statusUi.activeSwatch, lv_color_hex(COL_BORDER), 0);

  lv_obj_t *activeText = lv_obj_create(statusUi.activeCard);
  lv_obj_remove_style_all(activeText);
  lv_obj_set_flex_grow(activeText, 1);
  lv_obj_set_height(activeText, lv_pct(100));
  lv_obj_set_flex_flow(activeText, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(activeText, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(activeText, 2, 0);
  lv_obj_clear_flag(activeText, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *activeCaption = makeLabel(activeText, &lv_font_montserrat_12, lv_color_hex(COL_DIM));
  lv_label_set_text(activeCaption, "ACTIVE FILAMENT");
  statusUi.activeLabel = makeLabel(activeText, &lv_font_montserrat_18, lv_color_hex(COL_TEXT));

  // Temps
  lv_obj_t *tempsRow = lv_obj_create(statusUi.root);
  lv_obj_remove_style_all(tempsRow);
  lv_obj_set_width(tempsRow, lv_pct(100));
  lv_obj_set_height(tempsRow, 72);
  lv_obj_set_flex_flow(tempsRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(tempsRow, 8, 0);
  statusUi.nozzleL = makeMetricTile(tempsRow, "NOZZLE L");
  statusUi.nozzleR = makeMetricTile(tempsRow, "NOZZLE R");
  statusUi.bed = makeMetricTile(tempsRow, "BED");
  statusUi.chamber = makeMetricTile(tempsRow, "CHAMBER");

  // Compact secondary strip (layer / speed / filament + next)
  lv_obj_t *metaCard = lv_obj_create(statusUi.root);
  lv_obj_add_style(metaCard, &cardStyle, 0);
  lv_obj_set_width(metaCard, lv_pct(100));
  lv_obj_set_flex_grow(metaCard, 1);
  lv_obj_set_flex_flow(metaCard, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(metaCard, 12, 0);
  lv_obj_set_style_pad_row(metaCard, 6, 0);
  lv_obj_clear_flag(metaCard, LV_OBJ_FLAG_SCROLLABLE);

  statusUi.secondaryLabel = makeLabel(metaCard, &lv_font_montserrat_14, lv_color_hex(COL_TEXT));
  statusUi.nextJobLabel = makeLabel(metaCard, &lv_font_montserrat_12, lv_color_hex(COL_MUTED));
  statusUi.hmsLabel = makeLabel(metaCard, &lv_font_montserrat_12, lv_color_hex(COL_DANGER));
  lv_obj_add_flag(statusUi.hmsLabel, LV_OBJ_FLAG_HIDDEN);
}

void buildNavFooter(lv_obj_t *parent) {
  lv_obj_t *footer = lv_obj_create(parent);
  lv_obj_remove_style_all(footer);
  lv_obj_set_width(footer, lv_pct(100));
  lv_obj_set_height(footer, 44);
  lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(footer, 8, 0);
  lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

  navRow = lv_obj_create(footer);
  lv_obj_remove_style_all(navRow);
  lv_obj_set_flex_grow(navRow, 1);
  lv_obj_set_height(navRow, 40);
  lv_obj_set_flex_flow(navRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(navRow, 6, 0);
  lv_obj_clear_flag(navRow, LV_OBJ_FLAG_SCROLLABLE);

  for (int page = 0; page < TOTAL_PAGE_COUNT; ++page) {
    NavChip &chip = navChips[page];
    chip.page = page;
    chip.root = lv_obj_create(navRow);
    lv_obj_add_style(chip.root, &navChipStyle, 0);
    lv_obj_set_flex_grow(chip.root, 1);
    lv_obj_set_height(chip.root, 36);
    lv_obj_set_flex_flow(chip.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(chip.root, 4, 0);
    lv_obj_clear_flag(chip.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(chip.root, navChipEvent, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(page)));

    chip.label = lv_label_create(chip.root);
    lv_obj_set_style_text_font(chip.label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(chip.label, lv_color_hex(COL_MUTED), 0);
    lv_label_set_text(chip.label, navLabelForPage(page));
  }

  ipLabel = lv_label_create(footer);
  lv_obj_set_style_text_font(ipLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(ipLabel, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_align(ipLabel, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_width(ipLabel, 110);
  lv_label_set_text(ipLabel, "--");
}

void initStyles() {
  lv_style_init(&columnStyle);
  lv_style_set_bg_color(&columnStyle, lv_color_hex(COL_SURFACE));
  lv_style_set_bg_opa(&columnStyle, LV_OPA_COVER);
  lv_style_set_border_width(&columnStyle, 1);
  lv_style_set_border_color(&columnStyle, lv_color_hex(COL_BORDER));
  lv_style_set_radius(&columnStyle, 14);
  lv_style_set_clip_corner(&columnStyle, true);

  lv_style_init(&activeColumnStyle);
  lv_style_set_border_width(&activeColumnStyle, 2);
  lv_style_set_border_color(&activeColumnStyle, lv_color_hex(COL_ACCENT));

  lv_style_init(&emptyColumnStyle);
  lv_style_set_bg_color(&emptyColumnStyle, lv_color_hex(0x0C1016));
  lv_style_set_border_color(&emptyColumnStyle, lv_color_hex(COL_BORDER_SOFT));

  lv_style_init(&cardStyle);
  lv_style_set_bg_color(&cardStyle, lv_color_hex(COL_SURFACE));
  lv_style_set_bg_opa(&cardStyle, LV_OPA_COVER);
  lv_style_set_border_width(&cardStyle, 1);
  lv_style_set_border_color(&cardStyle, lv_color_hex(COL_BORDER));
  lv_style_set_radius(&cardStyle, 12);

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

void applyConnectionIcon(const String &message) {
  if (!connIcon) {
    return;
  }
  if (message.indexOf("MQTT connected") >= 0) {
    lv_label_set_text(connIcon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(connIcon, lv_color_hex(COL_ACCENT), 0);
    return;
  }
  if (message.indexOf("MQTT") >= 0) {
    lv_label_set_text(connIcon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(connIcon, lv_color_hex(COL_WARN), 0);
    return;
  }
  if (message.indexOf("WiFi") >= 0) {
    const bool hardFail =
        message.indexOf("not found") >= 0 || message.indexOf("auth failed") >= 0 ||
        message.indexOf("fail") >= 0 || message.indexOf("disconnected") >= 0 ||
        message.indexOf("lost") >= 0 || message.indexOf("SSID not") >= 0;
    if (hardFail) {
      lv_label_set_text(connIcon, LV_SYMBOL_CLOSE);
      lv_obj_set_style_text_color(connIcon, lv_color_hex(COL_DANGER), 0);
    } else {
      lv_label_set_text(connIcon, LV_SYMBOL_WIFI);
      lv_obj_set_style_text_color(connIcon, lv_color_hex(COL_WARN), 0);
    }
    return;
  }
  lv_label_set_text(connIcon, LV_SYMBOL_WARNING);
  lv_obj_set_style_text_color(connIcon, lv_color_hex(COL_WARN), 0);
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
  lv_obj_set_style_pad_all(root, 12, 0);
  lv_obj_set_style_pad_row(root, 8, 0);
  lv_obj_set_style_pad_right(root, 16, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *header = lv_obj_create(root);
  lv_obj_remove_style_all(header);
  lv_obj_set_width(header, lv_pct(100));
  lv_obj_set_height(header, 40);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(header, 12, 0);

  titleLabel = lv_label_create(header);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(COL_TEXT), 0);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(titleLabel, 200);

  stateLabel = lv_label_create(header);
  lv_obj_set_style_text_font(stateLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(stateLabel, lv_color_hex(COL_MUTED), 0);
  lv_label_set_long_mode(stateLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(stateLabel, 1);

  connIcon = lv_label_create(header);
  lv_obj_set_style_text_font(connIcon, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(connIcon, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(connIcon, LV_SYMBOL_WIFI);
  lv_obj_set_width(connIcon, 28);

  versionLabel = lv_label_create(header);
  lv_obj_set_style_text_font(versionLabel, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(versionLabel, lv_color_hex(COL_DIM), 0);
  lv_obj_set_width(versionLabel, 52);
  lv_obj_set_style_text_align(versionLabel, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(versionLabel, (String("v") + FIRMWARE_VERSION).c_str());

  buildStatusPage(root);

  columnsRoot = lv_obj_create(root);
  lv_obj_remove_style_all(columnsRoot);
  lv_obj_set_width(columnsRoot, lv_pct(100));
  lv_obj_set_flex_grow(columnsRoot, 1);
  lv_obj_set_flex_flow(columnsRoot, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(columnsRoot, 10, 0);

  for (int column = 0; column < COLUMNS_PER_PAGE; ++column) {
    buildColumn(columnsRoot, column);
  }

  buildNavFooter(root);
  refreshPage();
}

void ui_note_activity() {
  lastActivityMs = millis();
}

void ui_loop() {
  if (currentPage == STATUS_PAGE) {
    return;
  }
  if (millis() - lastActivityMs >= IDLE_RETURN_MS) {
    currentPage = STATUS_PAGE;
    refreshPage();
    lv_obj_invalidate(lv_scr_act());
  }
}

void ui_next_page() {
  gotoPage((currentPage + 1) % TOTAL_PAGE_COUNT);
}

void ui_update_slot(const SlotState &slot) {
  if (slot.amsId < 0 || slot.amsId >= TOTAL_AMS_IDS || slot.slot < 0 || slot.slot >= COLUMNS_PER_PAGE) {
    return;
  }
  slots[slot.amsId][slot.slot] = slot;
  if (!slotIsVisible(slot)) {
    return;
  }
  if (isHtPage()) {
    refreshColumn(slot.amsId - AMS_HT_FIRST_ID);
  } else {
    refreshColumn(slot.slot);
  }
}

void ui_update_printer(const PrinterState &newPrinter) {
  printer = newPrinter;
  if (isStatusPage()) {
    refreshStatusPage();
    refreshHeader();
  } else {
    refreshHeader();
  }
}

void ui_update_meta(const AmsMetaState &meta) {
  if (meta.amsId < 0 || meta.amsId >= TOTAL_AMS_IDS) {
    return;
  }
  metas[meta.amsId] = meta;
  if (!isStatusPage() && (isHtPage() ? meta.amsId >= AMS_HT_FIRST_ID : meta.amsId == amsIdForPage())) {
    refreshHeader();
  }
}

void ui_set_connection_status(const String &message) {
  applyConnectionIcon(message);
}

void ui_set_ip_address(const String &ip) {
  if (panelIp == ip) {
    return;
  }
  panelIp = ip;
  refreshFooter();
}
