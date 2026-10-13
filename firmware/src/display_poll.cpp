#include "display_poll.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <time.h>

#include <new>

#include "app_config.h"
#include "config.h"
#include "filaman_client.h"
#include "models.h"
#include "printer_list.h"
#include "ui/ui.h"

namespace {

struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override {
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
  }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override {
    void *q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
    return q ? q : nullptr;
  }
};
PsramAllocator g_psram;
struct PsramDoc : JsonDocument {
  PsramDoc() : JsonDocument(&g_psram) {}
};

constexpr unsigned long POLL_INTERVAL_MS = 10000;
constexpr unsigned long POLL_FAIL_BACKOFF_MS = 20000;
constexpr unsigned long PRINTER_LIST_INTERVAL_MS = 60000;
// Let OTA/mDNS finish advertising before the first FilaMan poll.
constexpr unsigned long POLL_AFTER_WIFI_MS = 2000;
// Leave room for lwIP and the mDNS announcer. Below this, skip the poll
// instead of allocating until the device aborts.
// Plain HTTP no longer builds a TLS client. 24 KB was that TLS budget and it
// skipped every poll, so the board under the header never received a snapshot.
constexpr uint32_t POLL_MIN_FREE_HEAP = 16000;

unsigned long lastSnapshotMs = 0;
unsigned long lastPrinterListMs = 0;
unsigned long wifiReadyAtMs = 0;
unsigned long lastLowHeapLogMs = 0;
int activePrinterId = 0;
char pollStatus[96] = "starting";
uint32_t lastPollLatencyMs = 0;
bool begun = false;
bool lastOk = false;
int consecutiveFails = 0;

String jsonString(JsonVariantConst v) {
  if (v.isNull()) {
    return "";
  }
  if (v.is<const char *>()) {
    return String(v.as<const char *>());
  }
  if (v.is<bool>()) {
    return v.as<bool>() ? "true" : "false";
  }
  if (v.is<int>() || v.is<long>() || v.is<float>() || v.is<double>()) {
    return String(v.as<double>());
  }
  return "";
}

bool jsonBool(JsonVariantConst v, bool fallback = false) {
  if (v.isNull()) {
    return fallback;
  }
  if (v.is<bool>()) {
    return v.as<bool>();
  }
  if (v.is<int>()) {
    return v.as<int>() != 0;
  }
  String s = jsonString(v);
  s.toLowerCase();
  if (s == "true" || s == "1" || s == "on") {
    return true;
  }
  if (s == "false" || s == "0" || s == "off") {
    return false;
  }
  return fallback;
}

int jsonInt(JsonVariantConst v, int fallback = 0) {
  if (v.isNull()) {
    return fallback;
  }
  if (v.is<int>() || v.is<long>()) {
    return v.as<int>();
  }
  if (v.is<float>() || v.is<double>()) {
    return static_cast<int>(v.as<double>());
  }
  String s = jsonString(v);
  if (!s.length()) {
    return fallback;
  }
  return s.toInt();
}

String intStr(JsonVariantConst v) { return v.isNull() ? String("") : String(jsonInt(v)); }

String speedPresetName(int level) {
  switch (level) {
    case 1: return "Silent";
    case 2: return "Standard";
    case 3: return "Sport";
    case 4: return "Ludicrous";
    default: return "";
  }
}

String formatGrams(JsonVariantConst v) {
  if (v.isNull()) {
    return "";
  }
  const double d = v.as<double>();
  if (d < 0) {
    return "";
  }
  const double nearest = static_cast<double>(static_cast<int>(d + (d >= 0 ? 0.5 : -0.5)));
  if (d - nearest < 0.05 && nearest - d < 0.05) {
    return String(static_cast<int>(nearest));
  }
  return String(d, 1);
}

String currencyPrefix(const String &code) {
  if (code == "USD") {
    return "$";
  }
  if (!code.length()) {
    return "EUR ";
  }
  return code + " ";
}

String formatCost(JsonVariantConst v, const String &currency) {
  if (v.isNull()) {
    return "";
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%.2f", v.as<double>());
  return currencyPrefix(currency) + buf;
}

// ETA is UTC. Show it in DISPLAY_TIMEZONE (NVS "tz"). Clock time only, plus a
// weekday when the panel clock is synced and the finish is not today.
String formatEtaClock(const String &iso) {
  if (!iso.length()) {
    return "";
  }
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
    return "";
  }
  // Days from civil date to Unix epoch (Howard Hinnant). newlib has no timegm.
  int y = year;
  unsigned m = static_cast<unsigned>(month);
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(day) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int days = era * 146097 + static_cast<int>(doe) - 719468;
  time_t utc = static_cast<time_t>(days) * 86400 + hour * 3600 + minute * 60 + second;
  const char *tee = strchr(iso.c_str(), 'T');
  if (tee) {
    const char *off = strpbrk(tee, "+-");
    if (off) {
      int oh = 0, om = 0;
      const int sign = (*off == '-') ? -1 : 1;
      sscanf(off + 1, "%d:%d", &oh, &om);
      utc -= static_cast<time_t>(sign * (oh * 3600 + om * 60));
    }
  }

  const char *tz = config().displayTimezone.c_str();
  if (!tz || !tz[0]) {
    tz = "UTC0";
  }
  setenv("TZ", tz, 1);
  tzset();
  struct tm local = {};
  localtime_r(&utc, &local);
  int h12 = local.tm_hour % 12;
  if (h12 == 0) {
    h12 = 12;
  }
  String out;
  const time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm today = {};
    localtime_r(&now, &today);
    if (today.tm_year != local.tm_year || today.tm_yday != local.tm_yday) {
      static const char *kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      out += kDays[local.tm_wday];
      out += " ";
    }
  }
  out += String(h12);
  out += ":";
  if (local.tm_min < 10) {
    out += "0";
  }
  out += String(local.tm_min);
  out += local.tm_hour < 12 ? " AM" : " PM";
  return out;
}

String formatRemaining(int seconds) {
  if (seconds < 0) {
    return "";
  }
  const int total = (seconds + 30) / 60;
  const int h = total / 60, m = total % 60;
  if (h > 0) {
    return String(h) + "h " + (m < 10 ? "0" : "") + String(m) + "m";
  }
  return String(total) + " min";
}

void fillSwatchFromJson(JsonObjectConst s, const String &fallbackColor, uint32_t *outColors,
                        uint8_t &outCount, SwatchStyle &outStyle, SwatchFinish &outFinish) {
  outCount = 0;
  outStyle = SwatchStyle::Flat;
  outFinish = SwatchFinish::None;
  JsonArrayConst arr = s["colors"].as<JsonArrayConst>();
  if (!arr.isNull()) {
    for (JsonVariantConst c : arr) {
      if (outCount >= MAX_SLOT_COLORS) {
        break;
      }
      const String hex = jsonString(c);
      if (!hex.length()) {
        continue;
      }
      outColors[outCount++] = parseHexRgb(hex);
    }
  }
  if (outCount == 0 && fallbackColor.length()) {
    outColors[outCount++] = parseHexRgb(fallbackColor);
  }
  outStyle = parseSwatchStyle(jsonString(s["color_style"]), outCount);
  outFinish = parseSwatchFinish(jsonString(s["finish"]));
}

// The panel's pages are fixed: AMS A-D use ids 0-3, the HT page uses ids
// AMS_HT_FIRST_ID.. in the order the printer lists them. Map the Display API's
// raw ids (AMS-HT start at 128, external holder 254/255) onto that layout.
// Returns -1 for units the panel has no page for.
int mapAmsId(int amsId, const String &kind, int &htSeen) {
  if (kind == "external") {
    return -1;
  }
  if (kind == "ams_ht") {
    if (htSeen >= AMSHT_MAX_COUNT) {
      return -1;
    }
    return AMS_HT_FIRST_ID + htSeen++;
  }
  return (amsId >= 0 && amsId < AMS2PRO_COUNT) ? amsId : -1;
}

// The unit table is too big for the loop stack and too big for internal BSS
// (that reservation left mDNS unable to allocate its announcement). Keep it
// in PSRAM; constructors run once via placement new.
OverviewUnit *snapshotUnits() {
  static OverviewUnit *units = nullptr;
  if (units) {
    return units;
  }
  constexpr size_t bytes = sizeof(OverviewUnit) * MAX_OVERVIEW_UNITS;
  void *mem = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) {
    mem = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  }
  if (!mem) {
    return nullptr;
  }
  units = static_cast<OverviewUnit *>(mem);
  for (int i = 0; i < MAX_OVERVIEW_UNITS; ++i) {
    new (units + i) OverviewUnit();
  }
  return units;
}

bool heapAllowsPoll() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap >= POLL_MIN_FREE_HEAP) {
    return true;
  }
  snprintf(pollStatus, sizeof(pollStatus), "low heap %u", freeHeap);
  const unsigned long now = millis();
  if (now - lastLowHeapLogMs >= 5000) {
    lastLowHeapLogMs = now;
    Serial.printf("display_poll: free heap %u, waiting\n", freeHeap);
  }
  return false;
}

bool applySnapshot(JsonDocument &doc) {
  // FilaMan Display API, schema v3: {"schema_version":3,"printers":[{...}]}
  JsonObject p = doc["printers"][0].as<JsonObject>();
  if (p.isNull()) {
    return false;
  }

  BridgeHealth health;
  health.online = true;  // we got a response from FilaMan
  health.version = String("display v") + jsonInt(doc["schema_version"], 0);
  health.updatedAt = 0;
  ui_update_bridge(health);

  // --- pass 1: units + slots (also finds the active slot for the status page) ---
  OverviewUnit *units = snapshotUnits();
  int unitCount = 0;
  int htSeen = 0;
  SlotState activeSlot;
  String activeUnitLabel;
  bool haveActive = false;
  String currency = jsonString(doc["currency"]);
  if (!currency.length()) {
    currency = "EUR";
  }

  for (JsonObject u : p["ams"].as<JsonArray>()) {
    const String kind = jsonString(u["kind"]);
    const int rawAmsId = jsonInt(u["ams_id"], 0);
    const int amsId = mapAmsId(rawAmsId, kind, htSeen);
    const String panelKind = (kind == "ams_ht") ? "ams_ht" : (kind == "external" ? "external" : "ams2pro");
    const String unitLabel = jsonString(u["label"]);

    JsonObject drying = u["drying"].as<JsonObject>();
    const bool dryingActive = !drying.isNull();

    AmsMetaState meta;
    meta.amsId = amsId;
    meta.amsKind = panelKind;
    meta.temperature = intStr(u["temperature"]);
    meta.humidity = intStr(u["humidity"]);
    if (dryingActive) {
      meta.dryStatus = jsonString(drying["status"]);
      meta.dryTargetTemp = jsonInt(drying["target_temp"], -1);
      meta.dryTime = jsonInt(drying["time"], -1);
    }
    if (amsId >= 0) {
      ui_update_meta(meta);
    }

    OverviewUnit *ov = nullptr;
    if (units && unitCount < MAX_OVERVIEW_UNITS) {
      ov = &units[unitCount++];
      ov->amsId = rawAmsId;
      ov->amsKind = kind.length() ? kind : panelKind;
      ov->label = unitLabel;
      ov->temperature = meta.temperature;
      ov->humidity = meta.humidity;
      ov->drying = dryingActive;
      ov->slotCount = 0;
    }

    for (JsonObject s : u["slots"].as<JsonArray>()) {
      const int slotIndex = jsonInt(s["slot"], 0);
      const bool empty = jsonBool(s["empty"], true);
      const bool active = jsonBool(s["active"], false);
      String color = jsonString(s["color"]);
      if (!color.length()) {
        color = "#202020";
      }
      const String material = jsonString(s["material"]);
      const String subgroup = jsonString(s["material_subgroup"]);
      const int remainingPercent = jsonInt(s["remaining_percent"], -1);

      if (ov && ov->slotCount < MAX_OVERVIEW_SLOTS) {
        OverviewSlot &os = ov->slots[ov->slotCount++];
        os.slot = slotIndex;
        os.empty = empty;
        os.active = active && !empty;
        os.rfid = jsonBool(s["rfid"], false);
        os.color = color;
        if (empty) {
          os.colorCount = 0;
          os.colorStyle = SwatchStyle::Flat;
          os.finish = SwatchFinish::None;
        } else {
          fillSwatchFromJson(s, color, os.colors, os.colorCount, os.colorStyle, os.finish);
        }
        applySwatchOverlay(os.finish, os.sparkle, os.fiber, subgroup, material, empty);
        os.label = jsonString(s["label"]);
        os.material = material;
        os.colorName = jsonString(s["color_name"]);
        if (!os.colorName.length()) {
          os.colorName = jsonString(s["filament"]);
        }
        os.manufacturer = jsonString(s["manufacturer"]);
        os.remainingPercent = empty ? -1 : remainingPercent;
        os.remainingGrams = empty ? -1 : jsonInt(s["remaining_grams"], -1);
        if (kind == "ams_ht") {
          os.temperature = meta.temperature;
          os.humidity = meta.humidity;
          os.drying = dryingActive;
        }
      }

      if (amsId < 0) {
        continue;
      }

      SlotState slot;
      slot.amsId = amsId;
      slot.slot = slotIndex;
      slot.empty = empty;
      slot.active = active;
      slot.rfid = jsonBool(s["rfid"], false);
      slot.amsKind = panelKind;
      slot.color = color;
      if (empty) {
        slot.colorCount = 0;
        slot.colorStyle = SwatchStyle::Flat;
        slot.finish = SwatchFinish::None;
      } else {
        fillSwatchFromJson(s, color, slot.colors, slot.colorCount, slot.colorStyle, slot.finish);
      }
      applySwatchOverlay(slot.finish, slot.sparkle, slot.fiber, subgroup, material, empty);
      slot.colorName = jsonString(s["color_name"]);
      slot.manufacturer = jsonString(s["manufacturer"]);
      slot.material = material;
      slot.nozzleMin = intStr(s["nozzle_min"]);
      slot.nozzleMax = intStr(s["nozzle_max"]);
      slot.remainingPercent = remainingPercent;
      slot.remainingGrams = intStr(s["remaining_grams"]);
      slot.costRemaining = empty ? String("") : formatCost(s["remaining_cost"], currency);
      slot.lastUsed = jsonString(s["last_used"]);
      const String backup = jsonString(s["backup_of"]);
      slot.backupBadge = backup.length() ? String("Backup ") + backup : String("");
      const String src = jsonString(s["remaining_source"]);
      slot.remainingSource = src == "filaman" ? "inventory" : (src == "printer" ? "ams" : "");
      ui_update_slot(slot);

      if (slot.active && !slot.empty && !haveActive) {
        haveActive = true;
        activeSlot = slot;
        activeUnitLabel = jsonString(s["label"]);
      }
    }
  }
  ui_update_overview(units, unitCount);

  // --- alerts ---
  AlertItem alerts[MAX_ALERTS];
  int alertCount = 0;
  String hmsText;
  for (JsonObject a : p["alerts"].as<JsonArray>()) {
    const String source = jsonString(a["source"]);
    if (source == "hms") {
      if (hmsText.length()) {
        hmsText += " | ";
      }
      const String detail = jsonString(a["detail"]);
      hmsText += detail.length() ? detail : jsonString(a["title"]);
    }
    if (alertCount >= MAX_ALERTS) {
      continue;
    }
    alerts[alertCount].id = jsonString(a["id"]);
    alerts[alertCount].severity = jsonString(a["severity"]);
    alerts[alertCount].title = jsonString(a["title"]);
    alerts[alertCount].detail = jsonString(a["detail"]);
    alerts[alertCount].source = source;
    alerts[alertCount].acknowledged = false;
    alertCount++;
  }
  ui_update_alerts(alerts, alertCount);

  // --- printer / job (status page). Formatting is done here, on the device. ---
  PrinterState printer;
  JsonObject job = p["job"].as<JsonObject>();
  JsonObject temps = p["temperatures"].as<JsonObject>();
  printer.name = jsonString(p["name"]);
  printer.connected = jsonBool(p["connected"], true);
  printer.state = jsonString(p["state"]);
  if (!printer.connected) {
    printer.state = "offline";
  }
  printer.jobName = jsonString(job["name"]);
  printer.progress = jsonInt(job["progress"], -1);
  printer.layer = jsonInt(job["layer"], -1);
  printer.totalLayers = jsonInt(job["total_layers"], -1);
  printer.remainingSeconds = jsonInt(job["remaining_seconds"], -1);
  printer.remainingMinutes = printer.remainingSeconds >= 0 ? printer.remainingSeconds / 60 : -1;
  printer.remainingTime = formatRemaining(printer.remainingSeconds);
  printer.etaClock = formatEtaClock(jsonString(job["eta"]));
  if (!job.isNull() && !job["elapsed_seconds"].isNull()) {
    printer.elapsedSeconds = jsonInt(job["elapsed_seconds"], -1);
    if (printer.elapsedSeconds >= 0) {
      printer.elapsedMinutes = printer.elapsedSeconds / 60;
    }
  }
  printer.filamentUsed = formatGrams(job["filament_grams"]);
  JsonVariant queue = p["queue"];
  printer.queueKnown = !queue.isNull();
  if (printer.queueKnown) {
    printer.queueDepth = jsonInt(queue["depth"], 0);
    JsonVariant next = queue["next"];
    if (!next.isNull()) {
      printer.nextJob = jsonString(next["name"]);
      const String grams = formatGrams(next["filament_grams"]);
      const String printTime = formatRemaining(jsonInt(next["print_time_seconds"], -1));
      if (grams.length()) {
        printer.nextJob += "  ";
        printer.nextJob += grams;
        printer.nextJob += "g";
      }
      if (printTime.length()) {
        printer.nextJob += "  ";
        printer.nextJob += printTime;
      }
    }
  }
  printer.nozzle = intStr(temps["nozzle"]);
  printer.nozzleTarget = intStr(temps["nozzle_target"]);
  printer.nozzle2 = temps["nozzle_2"].isNull() ? String("") : String(jsonInt(temps["nozzle_2"]));
  printer.nozzle2Target =
      temps["nozzle_2_target"].isNull() ? String("") : String(jsonInt(temps["nozzle_2_target"]));
  printer.bed = intStr(temps["bed"]);
  printer.bedTarget = intStr(temps["bed_target"]);
  printer.chamber = intStr(temps["chamber"]);
  printer.chamberTarget = intStr(temps["chamber_target"]);
  const int nozzleT = jsonInt(temps["nozzle_target"], 0), nozzleC = jsonInt(temps["nozzle"], 0);
  const int bedT = jsonInt(temps["bed_target"], 0), bedC = jsonInt(temps["bed"], 0);
  const int chT = jsonInt(temps["chamber_target"], 0), chC = jsonInt(temps["chamber"], 0);
  printer.nozzleHeating = nozzleT > 0 && nozzleC < nozzleT - 3;
  if (!temps["nozzle_2"].isNull() && !temps["nozzle_2_target"].isNull()) {
    const int n2t = jsonInt(temps["nozzle_2_target"], 0);
    const int n2c = jsonInt(temps["nozzle_2"], 0);
    printer.nozzle2Heating = n2t > 0 && n2c < n2t - 3;
  }
  printer.bedHeating = bedT > 0 && bedC < bedT - 3;
  printer.chamberHeating = chT > 0 && chC < chT - 3;
  printer.speedLevel = jsonInt(p["speed_level"], -1);
  printer.speedPreset = speedPresetName(printer.speedLevel);
  printer.hms = hmsText;
  printer.alertCount = alertCount;
  printer.schemaVersion = jsonInt(doc["schema_version"], 0);
  if (haveActive) {
    printer.activeColor = activeSlot.color;
    printer.activeColors[0] = 0;
    printer.activeColorCount = activeSlot.colorCount;
    for (uint8_t i = 0; i < activeSlot.colorCount; ++i) {
      printer.activeColors[i] = activeSlot.colors[i];
    }
    printer.activeColorStyle = activeSlot.colorStyle;
    printer.activeFinish = activeSlot.finish;
    printer.activeSparkle = activeSlot.sparkle;
    printer.activeFiber = activeSlot.fiber;
    printer.activeMaterial = activeSlot.material;
    printer.activeLabel = activeUnitLabel + " / " +
                          (activeSlot.colorName.length() ? activeSlot.colorName : activeSlot.material);
    if (activeSlot.material.length() && printer.activeLabel.indexOf(activeSlot.material) < 0) {
      printer.activeLabel += " " + activeSlot.material;
    }
    printer.activeRemainingPercent = activeSlot.remainingPercent;
    printer.activeRemainingGrams = activeSlot.remainingGrams;
    if (activeSlot.remainingPercent >= 0) {
      printer.activeRemaining = String(activeSlot.remainingPercent) + "%";
    } else if (activeSlot.remainingGrams.length()) {
      printer.activeRemaining = activeSlot.remainingGrams + "g";
    }
  } else {
    printer.activeSparkle = SwatchSparkle::None;
    printer.activeFiber = SwatchFiber::None;
  }
  if (!printer.activeColor.length()) {
    printer.activeColor = "#202020";
  }
  if (printer.activeColorCount == 0 && printer.activeColor.length()) {
    printer.activeColors[0] = parseHexRgb(printer.activeColor);
    printer.activeColorCount = 1;
  }
  ui_update_printer(printer);
  return true;
}

bool idListed(const PrinterOption *printers, int count, int id) {
  if (id <= 0) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    if (printers[i].id.toInt() == id) {
      return true;
    }
  }
  return false;
}

// Optional naming: crowpanel-h2d → printer named H2D. Default hostname
// "crowpanel" does not auto-pick; pin an id or rename the host. The saved id
// is kept when it is still in the list; otherwise the hostname wins over
// "last active printer".
bool hostnameMatchesPrinter(const String &hostname, const String &printerName) {
  String host = hostname;
  String name = printerName;
  host.trim();
  name.trim();
  host.toLowerCase();
  name.toLowerCase();
  if (name.length() < 2 || host.length() < 2) {
    return false;
  }
  if (host == name || host.endsWith(String("-") + name)) {
    return true;
  }
  const String token = String("-") + name;
  const int at = host.indexOf(token);
  if (at < 0) {
    return false;
  }
  const int end = at + token.length();
  return end == static_cast<int>(host.length()) || host.charAt(end) == '-' || host.charAt(end) == '.';
}

int printerIdForHost(const PrinterOption *printers, int count) {
  const String host = config().otaHostname;
  for (int i = 0; i < count; ++i) {
    if (hostnameMatchesPrinter(host, printers[i].name)) {
      return printers[i].id.toInt();
    }
  }
  return 0;
}

void refreshPrinterList() {
  PrinterOption printers[MAX_PRINTER_OPTIONS];
  int count = 0;
  const int pinned = config().filamanPrinterId;
  String activeId = pinned > 0 ? String(pinned) : (activePrinterId > 0 ? String(activePrinterId) : "");
  FmResult r = filaman.listBambuddyPrinters(printers, MAX_PRINTER_OPTIONS, count, activeId);
  if (!r.ok) {
    snprintf(pollStatus, sizeof(pollStatus), "list: %.80s", r.error.c_str());
    Serial.printf("display_poll: printer list failed: %s\n", r.error.c_str());
    return;
  }

  int chosen = 0;
  if (pinned > 0 && idListed(printers, count, pinned)) {
    chosen = pinned;
  } else {
    chosen = printerIdForHost(printers, count);
    if (chosen <= 0) {
      chosen = activeId.toInt();
    }
    if (chosen <= 0 || !idListed(printers, count, chosen)) {
      chosen = 0;
      for (int i = 0; i < count; ++i) {
        chosen = printers[i].id.toInt();
        if (chosen > 0) {
          break;
        }
      }
    }
    if (pinned > 0 && chosen > 0 && chosen != pinned) {
      Serial.printf("display_poll: printer %d is gone; using %d\n", pinned, chosen);
      config().setFilamanPrinterId(chosen);
    }
  }
  if (chosen > 0 && chosen != activePrinterId) {
    Serial.printf("display_poll: monitoring printer %d\n", chosen);
    activePrinterId = chosen;
  }
  for (int i = 0; i < count; ++i) {
    printers[i].active = (printers[i].id.toInt() == activePrinterId);
  }
  printer_list_set(printers, count, String(activePrinterId > 0 ? activePrinterId : 0));
}

void pollSnapshot() {
  if (activePrinterId <= 0) {
    refreshPrinterList();
    if (activePrinterId <= 0) {
      ui_set_connection_status("No printer in FilaMan");
      snprintf(pollStatus, sizeof(pollStatus), "no printer");
      if (consecutiveFails < 1000) {
        consecutiveFails++;
      }
      return;
    }
  }

  PsramDoc doc;
  const unsigned long t0 = millis();
  FmResult r = filaman.fetchDisplaySnapshot(activePrinterId, true, doc);
  lastPollLatencyMs = static_cast<uint32_t>(millis() - t0);
  if (r.notModified) {
    lastOk = true;
    consecutiveFails = 0;
    snprintf(pollStatus, sizeof(pollStatus), "linked printer %d", activePrinterId);
    ui_note_fresh();
    ui_set_connection_status("FilaMan linked");
    return;
  }
  if (!r.ok && r.httpStatus == 404) {
    const int missing = activePrinterId;
    activePrinterId = 0;
    refreshPrinterList();
    if (activePrinterId > 0 && activePrinterId != missing) {
      r = filaman.fetchDisplaySnapshot(activePrinterId, true, doc);
    }
  }
  if (!r.ok) {
    lastOk = false;
    if (consecutiveFails < 1000) {
      consecutiveFails++;
    }
    snprintf(pollStatus, sizeof(pollStatus), "snapshot: %.80s", r.error.c_str());
    Serial.printf("display_poll: snapshot failed: %s\n", r.error.c_str());
    ui_set_connection_status(String("FilaMan: ") + r.error);
    return;
  }
  if (!applySnapshot(doc)) {
    lastOk = false;
    if (consecutiveFails < 1000) {
      consecutiveFails++;
    }
    snprintf(pollStatus, sizeof(pollStatus), "snapshot: empty printer[]");
    Serial.println("display_poll: snapshot missing printers[0]");
    ui_set_connection_status("FilaMan: empty printer response");
    return;
  }
  lastOk = true;
  consecutiveFails = 0;
  snprintf(pollStatus, sizeof(pollStatus), "linked printer %d", activePrinterId);
  ui_set_connection_status("FilaMan linked");
}

}  // namespace

void display_poll_begin() {
  begun = true;
  lastSnapshotMs = 0;
  lastPrinterListMs = 0;
  consecutiveFails = 0;
  if (activePrinterId <= 0) {
    if (config().filamanPrinterId > 0) {
      activePrinterId = config().filamanPrinterId;
    } else if (FILAMAN_PRINTER_ID > 0) {
      activePrinterId = FILAMAN_PRINTER_ID;
    }
  }
  printer_list_set_publisher([](const String &id) { display_poll_set_printer(id); });
}

bool display_poll_active() { return begun && filaman.configured(); }

bool display_poll_needs_mqtt() {
  return display_poll_active() && consecutiveFails >= 2;
}

void display_poll_set_printer(const String &filamanPrinterId) {
  const int id = filamanPrinterId.toInt();
  if (id <= 0) {
    return;
  }
  activePrinterId = id;
  lastSnapshotMs = 0;  // force immediate refresh
}

String display_poll_active_printer() {
  return activePrinterId > 0 ? String(activePrinterId) : "";
}

const char *display_poll_status() { return pollStatus; }

uint32_t display_poll_last_latency_ms() { return lastPollLatencyMs; }

void display_poll_loop() {
  if (!display_poll_active() || WiFi.status() != WL_CONNECTED) {
    wifiReadyAtMs = 0;
    return;
  }
  const unsigned long now = millis();
  if (wifiReadyAtMs == 0) {
    wifiReadyAtMs = now + POLL_AFTER_WIFI_MS;
    return;
  }
  if (static_cast<long>(wifiReadyAtMs - now) > 0) {
    return;
  }
  if (!heapAllowsPoll()) {
    return;
  }
  const unsigned long snapshotInterval = lastOk ? POLL_INTERVAL_MS : POLL_FAIL_BACKOFF_MS;
  const unsigned long listInterval = lastOk ? PRINTER_LIST_INTERVAL_MS : POLL_FAIL_BACKOFF_MS;
  if (now - lastPrinterListMs >= listInterval || lastPrinterListMs == 0) {
    lastPrinterListMs = now;
    refreshPrinterList();
  }
  if (now - lastSnapshotMs >= snapshotInterval || lastSnapshotMs == 0) {
    lastSnapshotMs = now;
    pollSnapshot();
  }
}
