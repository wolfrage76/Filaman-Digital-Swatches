#include "mqtt_client.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <math.h>

#include "app_config.h"
#include "config.h"
#include "models.h"

DisplayMqtt displayMqtt;

namespace {

constexpr size_t MQTT_DEDUP_SLOTS = 48;

struct MqttDedupEntry {
  uint32_t topicHash = 0;
  uint32_t payloadHash = 0;
};

MqttDedupEntry mqttDedup[MQTT_DEDUP_SLOTS];
size_t mqttDedupNext = 0;

uint32_t fnv1a(const uint8_t *data, size_t len) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

uint32_t fnv1aStr(const char *text) {
  return fnv1a(reinterpret_cast<const uint8_t *>(text), strlen(text));
}

bool mqttPayloadUnchanged(const char *topic, const byte *payload, unsigned int length) {
  const uint32_t topicHash = fnv1aStr(topic);
  const uint32_t payloadHash = fnv1a(payload, length);
  for (size_t i = 0; i < MQTT_DEDUP_SLOTS; ++i) {
    if (mqttDedup[i].topicHash == topicHash) {
      if (mqttDedup[i].payloadHash == payloadHash) {
        return true;
      }
      mqttDedup[i].payloadHash = payloadHash;
      return false;
    }
  }
  mqttDedup[mqttDedupNext].topicHash = topicHash;
  mqttDedup[mqttDedupNext].payloadHash = payloadHash;
  mqttDedupNext = (mqttDedupNext + 1) % MQTT_DEDUP_SLOTS;
  return false;
}

String jsonString(JsonVariantConst value) {
  if (value.isNull()) {
    return "";
  }
  if (value.is<const char *>()) {
    return String(value.as<const char *>());
  }
  if (value.is<bool>()) {
    return value.as<bool>() ? "true" : "false";
  }
  if (value.is<float>() || value.is<double>() || value.is<int>() || value.is<long>() ||
      value.is<unsigned int>() || value.is<unsigned long>()) {
    const double number = value.as<double>();
    if (number >= -2147483648.0 && number <= 2147483647.0) {
      const long whole = lround(number);
      if (fabs(number - static_cast<double>(whole)) < 0.05) {
        return String(whole);
      }
    }
    return String(number, 1);
  }
  String serialized;
  serializeJson(value, serialized);
  if (serialized.length() >= 2 && serialized.charAt(0) == '"' &&
      serialized.charAt(serialized.length() - 1) == '"') {
    serialized.remove(serialized.length() - 1);
    serialized.remove(0, 1);
  }
  return serialized;
}

int jsonInt(JsonVariantConst value, int fallback = -1) {
  if (value.isNull()) {
    return fallback;
  }
  if (value.is<const char *>()) {
    const char *text = value.as<const char *>();
    if (!text || text[0] == '\0') {
      return fallback;
    }
    return atoi(text);
  }
  if (value.is<bool>()) {
    return value.as<bool>() ? 1 : 0;
  }
  if (value.is<float>() || value.is<double>() || value.is<int>() || value.is<long>() ||
      value.is<unsigned int>() || value.is<unsigned long>()) {
    return static_cast<int>(lround(value.as<double>()));
  }
  const String text = jsonString(value);
  if (!text.length()) {
    return fallback;
  }
  return text.toInt();
}

bool jsonBool(JsonVariantConst value, bool fallback = false) {
  if (value.isNull()) {
    return fallback;
  }
  if (value.is<bool>()) {
    return value.as<bool>();
  }
  if (value.is<const char *>()) {
    String text = value.as<const char *>();
    text.toLowerCase();
    if (text == "1" || text == "true" || text == "on" || text == "online" || text == "yes") {
      return true;
    }
    if (text == "0" || text == "false" || text == "off" || text == "offline" || text == "no") {
      return false;
    }
  }
  if (value.is<int>() || value.is<long>() || value.is<float>() || value.is<double>()) {
    return value.as<double>() != 0.0;
  }
  return fallback;
}

void fillSwatchFromJson(JsonVariantConst s, const String &fallbackColor, uint32_t *outColors,
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

bool parseTopicPart(const String &topic, const String &prefix, const String &suffix, int &amsId,
                    int &slotId) {
  if (!topic.startsWith(prefix)) {
    return false;
  }
  int amsStart = prefix.length();
  int amsEnd = topic.indexOf('/', amsStart);
  if (amsEnd < 0) {
    return false;
  }
  amsId = topic.substring(amsStart, amsEnd).toInt();
  if (!topic.substring(amsEnd).startsWith(suffix)) {
    return false;
  }
  slotId = topic.substring(amsEnd + suffix.length()).toInt();
  return true;
}

}  // namespace

void DisplayMqtt::begin() {
  base_ = config().topicPrefix;  // cache once; used to build/match topics
  client_.setServer(config().mqttHost.c_str(), config().mqttPort);
  client_.setBufferSize(MQTT_BUFFER_SIZE);
  // Keep TCP ops short so a broker hiccup cannot blank the RGB panel for seconds.
  client_.setSocketTimeout(1);
  wifiClient_.setTimeout(400);
  client_.setCallback([this](char *topic, byte *payload, unsigned int length) {
    handleMessage(topic, payload, length);
  });
}

void DisplayMqtt::loop() {
  tookWork_ = false;
  if (!config().mqttHost.length()) {
    return;
  }
  if (!client_.connected()) {
    reconnect();
    tookWork_ = true;
    return;
  }
  client_.loop();
}

void DisplayMqtt::setSlotCallback(SlotUpdateCallback callback) { slotCallback_ = callback; }
void DisplayMqtt::setPrinterCallback(PrinterUpdateCallback callback) { printerCallback_ = callback; }
void DisplayMqtt::setMetaCallback(MetaUpdateCallback callback) { metaCallback_ = callback; }
void DisplayMqtt::setStatusCallback(StatusUpdateCallback callback) { statusCallback_ = callback; }
void DisplayMqtt::setBridgeCallback(BridgeUpdateCallback callback) { bridgeCallback_ = callback; }
void DisplayMqtt::setAlertsCallback(AlertsUpdateCallback callback) { alertsCallback_ = callback; }
void DisplayMqtt::setOverviewCallback(OverviewUpdateCallback callback) { overviewCallback_ = callback; }
void DisplayMqtt::setPrintersCallback(PrintersUpdateCallback callback) { printersCallback_ = callback; }

bool DisplayMqtt::publishMonitoredPrinter(const String &id) {
  if (!client_.connected()) {
    return false;
  }
  JsonDocument doc;
  doc["id"] = id;
  String body;
  serializeJson(doc, body);
  const String topic = base_ + "/ctrl/printer";
  return client_.publish(topic.c_str(), body.c_str(), true);  // retained: bridge honors on restart
}

// Published outside DISPLAY_TOPIC_PREFIX so the panel's own "/#" subscription
// doesn't echo it back.
bool DisplayMqtt::publishDiag(const String &body) {
  if (!client_.connected()) {
    return false;
  }
  return client_.publish("panel/h2c/diag", body.c_str(), false);
}

void DisplayMqtt::reconnect() {
  unsigned long now = millis();
  if (now - lastReconnectAttempt_ < MQTT_RECONNECT_MS) {
    return;
  }
  lastReconnectAttempt_ = now;

  publishStatus("MQTT connecting");
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  char clientId[32];
  snprintf(clientId, sizeof(clientId), "crowpanel-%02x%02x%02x", mac[3], mac[4], mac[5]);
  bool connected = false;
  if (config().mqttUsername.length() > 0) {
    connected = client_.connect(clientId, config().mqttUsername.c_str(),
                                config().mqttPassword.c_str());
  } else {
    connected = client_.connect(clientId);
  }

  if (connected) {
    publishStatus("MQTT connected");
    subscribe();
  } else {
    publishStatus("MQTT failed rc=" + String(client_.state()));
  }
}

void DisplayMqtt::subscribe() {
  String topic = base_ + "/#";
  client_.subscribe(topic.c_str());
}

void DisplayMqtt::handleMessage(char *topicChars, byte *payload, unsigned int length) {
  tookWork_ = true;
  // Retained republishes with identical bodies still cost tens of ms to parse and
  // contend with RGB DMA on the PSRAM bus — skip them before ArduinoJson runs.
  if (mqttPayloadUnchanged(topicChars, payload, length)) {
    return;
  }

  String topic(topicChars);
  const String &base = base_;

  // Bridge LWT may be plain "online"/"offline" text.
  if (topic == base + "/bridge/status") {
    BridgeHealth health;
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (!error && doc.is<JsonObject>()) {
      health.online = jsonBool(doc["online"], jsonString(doc["status"]) == "online");
      health.version = jsonString(doc["version"]);
      health.updatedAt = static_cast<unsigned long>(jsonInt(doc["updated_at"], 0));
    } else {
      String text;
      text.reserve(length + 1);
      for (unsigned int i = 0; i < length; ++i) {
        text += static_cast<char>(payload[i]);
      }
      text.trim();
      text.toLowerCase();
      health.online = (text == "online" || text == "1" || text == "true");
    }
    if (bridgeCallback_) {
      bridgeCallback_(health);
    }
    return;
  }

  // An empty retained payload is the bridge *clearing* a topic (AMS unit dropped,
  // or a printer switch). Reset the matching slot/meta so stale data from the
  // previous printer/state doesn't linger on screen.
  bool payloadBlank = true;
  for (unsigned int i = 0; i < length; ++i) {
    const char c = static_cast<char>(payload[i]);
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
      payloadBlank = false;
      break;
    }
  }
  if (payloadBlank) {
    int amsId = 0;
    int slotId = 0;
    if (parseTopicPart(topic, base + "/ams/", "/slot/", amsId, slotId)) {
      SlotState slot;
      slot.amsId = amsId;
      slot.slot = slotId;
      slot.empty = true;
      if (slotCallback_) {
        slotCallback_(slot);
      }
    } else if (topic.startsWith(base + "/ams/") && topic.endsWith("/meta") &&
               topic.indexOf("/slot/") < 0) {
      AmsMetaState meta;
      const int idStart = (base + "/ams/").length();
      const int idEnd = topic.indexOf('/', idStart);
      meta.amsId = topic.substring(idStart, idEnd).toInt();
      if (metaCallback_) {
        metaCallback_(meta);
      }
    }
    return;  // nothing to parse for any other cleared topic
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    return;
  }

  if (topic == base + "/alerts") {
    AlertItem alerts[MAX_ALERTS];
    int count = 0;
    JsonArrayConst arr = doc["alerts"].as<JsonArrayConst>();
    if (!arr.isNull()) {
      for (JsonVariantConst item : arr) {
        if (count >= MAX_ALERTS) {
          break;
        }
        alerts[count].id = jsonString(item["id"]);
        alerts[count].severity = jsonString(item["severity"]);
        alerts[count].title = jsonString(item["title"]);
        alerts[count].detail = jsonString(item["detail"]);
        alerts[count].source = jsonString(item["source"]);
        alerts[count].acknowledged = jsonBool(item["acknowledged"], false);
        ++count;
      }
    }
    if (alertsCallback_) {
      alertsCallback_(alerts, count);
    }
    return;
  }

  if (topic == base + "/ams/overview") {
    OverviewUnit units[MAX_OVERVIEW_UNITS];
    int count = 0;
    JsonArrayConst arr = doc["units"].as<JsonArrayConst>();
    if (!arr.isNull()) {
      for (JsonVariantConst unit : arr) {
        if (count >= MAX_OVERVIEW_UNITS) {
          break;
        }
        OverviewUnit &dst = units[count];
        dst.amsId = jsonInt(unit["ams_id"], count);
        dst.amsKind = jsonString(unit["ams_kind"]);
        dst.label = jsonString(unit["label"]);
        dst.temperature = jsonString(unit["temperature"]);
        dst.humidity = jsonString(unit["humidity"]);
        dst.slotCount = 0;
        JsonArrayConst slots = unit["slots"].as<JsonArrayConst>();
        if (!slots.isNull()) {
          for (JsonVariantConst slot : slots) {
            if (dst.slotCount >= MAX_OVERVIEW_SLOTS) {
              break;
            }
            OverviewSlot &os = dst.slots[dst.slotCount];
            os.slot = jsonInt(slot["slot"], dst.slotCount);
            os.empty = jsonBool(slot["empty"], true);
            os.active = jsonBool(slot["active"], false);
            os.rfid = jsonBool(slot["rfid"], false);
            os.color = jsonString(slot["color"]);
            if (!os.color.length()) {
              os.color = "#202020";
            }
            os.colorName = jsonString(slot["color_name"]);
            os.manufacturer = jsonString(slot["manufacturer"]);
            os.material = jsonString(slot["material"]);
            os.remainingPercent = jsonInt(slot["remaining_percent"], -1);
            os.remainingGrams = jsonInt(slot["remaining_grams"], -1);
            if (os.empty) {
              os.colorCount = 0;
              os.colorStyle = SwatchStyle::Flat;
              os.finish = SwatchFinish::None;
            } else {
              fillSwatchFromJson(slot, os.color, os.colors, os.colorCount, os.colorStyle, os.finish);
            }
            applySwatchOverlay(os.finish, os.sparkle, os.fiber, jsonString(slot["material_subgroup"]),
                               os.material, os.empty);
            String kind = dst.amsKind;
            kind.toLowerCase();
            if (kind.indexOf("ht") >= 0) {
              os.temperature = dst.temperature;
              os.humidity = dst.humidity;
              os.drying = dst.drying;
            }
            ++dst.slotCount;
          }
        }
        ++count;
      }
    }
    if (overviewCallback_) {
      overviewCallback_(units, count);
    }
    return;
  }

  if (topic == base + "/printers") {
    PrinterOption printers[MAX_PRINTER_OPTIONS];
    int count = 0;
    const String activeId = jsonString(doc["active_id"]);
    JsonArrayConst arr = doc["printers"].as<JsonArrayConst>();
    if (!arr.isNull()) {
      for (JsonVariantConst p : arr) {
        if (count >= MAX_PRINTER_OPTIONS) {
          break;
        }
        printers[count].id = jsonString(p["id"]);
        printers[count].name = jsonString(p["name"]);
        printers[count].model = jsonString(p["model"]);
        printers[count].active = jsonBool(p["active"], false);
        ++count;
      }
    }
    if (printersCallback_) {
      printersCallback_(printers, count, activeId);
    }
    return;
  }

  if (topic == base + "/printer") {
    PrinterState printer;
    printer.name = jsonString(doc["name"]);
    printer.state = jsonString(doc["state"]);
    printer.jobName = jsonString(doc["job_name"]);
    printer.nozzle = jsonString(doc["nozzle"]);
    printer.nozzleTarget = jsonString(doc["nozzle_target"]);
    printer.nozzle2 = jsonString(doc["nozzle_2"]);
    printer.nozzle2Target = jsonString(doc["nozzle_2_target"]);
    printer.bed = jsonString(doc["bed"]);
    printer.bedTarget = jsonString(doc["bed_target"]);
    printer.chamber = jsonString(doc["chamber"]);
    printer.chamberTarget = jsonString(doc["chamber_target"]);
    printer.filamentUsed = jsonString(doc["filament_used"]);
    printer.remainingTime = jsonString(doc["remaining_time"]);
    printer.etaClock = jsonString(doc["eta_clock"]);
    printer.speedPreset = jsonString(doc["speed_preset"]);
    printer.fanPart = jsonString(doc["fan_part"]);
    printer.fanAux = jsonString(doc["fan_aux"]);
    printer.fanChamber = jsonString(doc["fan_chamber"]);
    printer.wifiRssi = jsonString(doc["wifi_signal"]);
    printer.hms = jsonString(doc["hms"]);
    printer.nextJob = jsonString(doc["next_job"]);
    printer.chamberLight = jsonString(doc["chamber_light"]);
    printer.door = jsonString(doc["door"]);
    printer.filamentSensor = jsonString(doc["filament_sensor"]);
    printer.activeColor = jsonString(doc["active_color"]);
    printer.activeMaterial = jsonString(doc["active_material"]);
    applySwatchOverlay(printer.activeFinish, printer.activeSparkle, printer.activeFiber,
                       jsonString(doc["active_material_subgroup"]), printer.activeMaterial, false);
    printer.activeLabel = jsonString(doc["active_label"]);
    printer.activeRemaining = jsonString(doc["active_remaining"]);
    printer.connected = jsonBool(doc["connected"], true);
    printer.nozzleHeating = jsonBool(doc["nozzle_heating"], false);
    printer.nozzle2Heating = jsonBool(doc["nozzle_2_heating"], false);
    printer.bedHeating = jsonBool(doc["bed_heating"], false);
    printer.chamberHeating = jsonBool(doc["chamber_heating"], false);
    printer.progress = jsonInt(doc["progress"], -1);
    printer.layer = jsonInt(doc["layer"], -1);
    printer.totalLayers = jsonInt(doc["total_layers"], -1);
    printer.remainingSeconds = jsonInt(doc["remaining_seconds"], -1);
    printer.remainingMinutes = jsonInt(doc["remaining_minutes"], -1);
    printer.elapsedMinutes = jsonInt(doc["elapsed_minutes"], -1);
    printer.speedLevel = jsonInt(doc["speed_level"], -1);
    printer.activeExtruder = jsonInt(doc["active_extruder"], 0);
    printer.queueDepth = jsonInt(doc["queue_depth"], 0);
    printer.queueKnown = true;
    printer.alertCount = jsonInt(doc["alert_count"], 0);
    printer.updatedAt = static_cast<unsigned long>(jsonInt(doc["updated_at"], 0));
    printer.schemaVersion = jsonInt(doc["schema_version"], 0);
    if (printer.activeColor.length() == 0) {
      printer.activeColor = "#202020";
    }
    if (printerCallback_) {
      printerCallback_(printer);
    }
    return;
  }

  int amsId = 0;
  int slotId = 0;
  if (parseTopicPart(topic, base + "/ams/", "/slot/", amsId, slotId)) {
    SlotState slot;
    slot.amsId = jsonInt(doc["ams_id"], amsId);
    slot.slot = jsonInt(doc["slot"], slotId);
    slot.empty = jsonBool(doc["empty"], true);
    slot.active = jsonBool(doc["active"], false);
    slot.amsKind = jsonString(doc["ams_kind"]);
    slot.color = jsonString(doc["color"]);
    slot.colorName = jsonString(doc["color_name"]);
    slot.manufacturer = jsonString(doc["manufacturer"]);
    slot.material = jsonString(doc["material"]);
    slot.nozzleMin = jsonString(doc["nozzle_min"]);
    slot.nozzleMax = jsonString(doc["nozzle_max"]);
    slot.remainingPercent = jsonInt(doc["remaining_percent"], -1);
    slot.remainingGrams = jsonString(doc["remaining_grams"]);
    slot.costRemaining = jsonString(doc["cost_remaining"]);
    slot.lastUsed = jsonString(doc["last_used"]);
    slot.backupBadge = jsonString(doc["backup_badge"]);
    slot.remainingSource = jsonString(doc["remaining_source"]);
    slot.rfid = jsonBool(doc["rfid"], false);
    if (slot.empty) {
      slot.colorCount = 0;
      slot.colorStyle = SwatchStyle::Flat;
      slot.finish = SwatchFinish::None;
    } else {
      fillSwatchFromJson(doc.as<JsonVariantConst>(), slot.color, slot.colors, slot.colorCount,
                         slot.colorStyle, slot.finish);
    }
    applySwatchOverlay(slot.finish, slot.sparkle, slot.fiber, jsonString(doc["material_subgroup"]),
                       slot.material, slot.empty);
    if (slotCallback_) {
      slotCallback_(slot);
    }
    return;
  }

  String metaPrefix = base + "/ams/";
  String metaSuffix = "/meta";
  if (topic.startsWith(metaPrefix) && topic.endsWith(metaSuffix) &&
      topic.indexOf("/slot/") < 0 && topic.indexOf("/overview") < 0) {
    int idStart = metaPrefix.length();
    int idEnd = topic.indexOf('/', idStart);
    AmsMetaState meta;
    meta.amsId = topic.substring(idStart, idEnd).toInt();
    meta.amsKind = jsonString(doc["ams_kind"]);
    meta.temperature = jsonString(doc["temperature"]);
    meta.humidity = jsonString(doc["humidity"]);
    meta.dryStatus = jsonString(doc["dry_status"]);
    meta.dryTargetTemp = jsonInt(doc["dry_target_temp"], -1);
    meta.dryTime = jsonInt(doc["dry_time"], -1);
    if (metaCallback_) {
      metaCallback_(meta);
    }
  }
}

void DisplayMqtt::publishStatus(const String &message) {
  if (statusCallback_) {
    statusCallback_(message);
  }
}
