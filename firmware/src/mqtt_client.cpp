#include "mqtt_client.h"

#include <ArduinoJson.h>
#include <math.h>

#include "config.h"

DisplayMqtt displayMqtt;

namespace {

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
  // ArduinoJson may store whole numbers as long/float; accept any numeric.
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
  if (serialized.length() >= 2 && serialized.charAt(0) == '"' && serialized.charAt(serialized.length() - 1) == '"') {
    serialized.remove(serialized.length() - 1);
    serialized.remove(0, 1);
  }
  return serialized;
}

// Avoid `doc[key] | -1` — on some ArduinoJson builds that can be treated as bitwise OR
// (e.g. progress 0.0 becomes -1 and shows as "--%").
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

bool parseTopicPart(const String &topic, const String &prefix, const String &suffix, int &amsId, int &slotId) {
  if (!topic.startsWith(prefix)) {
    return false;
  }

  int amsStart = prefix.length();
  int amsEnd = topic.indexOf('/', amsStart);
  if (amsEnd < 0) {
    return false;
  }
  amsId = topic.substring(amsStart, amsEnd).toInt();

  String expected = suffix;
  if (!topic.substring(amsEnd).startsWith(expected)) {
    return false;
  }

  int slotStart = amsEnd + expected.length();
  slotId = topic.substring(slotStart).toInt();
  return true;
}

}  // namespace

void DisplayMqtt::begin() {
  client_.setServer(MQTT_HOST, MQTT_PORT);
  client_.setBufferSize(MQTT_BUFFER_SIZE);
  client_.setCallback([this](char *topic, byte *payload, unsigned int length) {
    handleMessage(topic, payload, length);
  });
}

void DisplayMqtt::loop() {
  if (!client_.connected()) {
    reconnect();
  }
  client_.loop();
}

void DisplayMqtt::setSlotCallback(SlotUpdateCallback callback) {
  slotCallback_ = callback;
}

void DisplayMqtt::setPrinterCallback(PrinterUpdateCallback callback) {
  printerCallback_ = callback;
}

void DisplayMqtt::setMetaCallback(MetaUpdateCallback callback) {
  metaCallback_ = callback;
}

void DisplayMqtt::setStatusCallback(StatusUpdateCallback callback) {
  statusCallback_ = callback;
}

void DisplayMqtt::reconnect() {
  unsigned long now = millis();
  if (now - lastReconnectAttempt_ < MQTT_RECONNECT_MS) {
    return;
  }
  lastReconnectAttempt_ = now;

  publishStatus("MQTT connecting");
  bool connected = false;
  if (String(MQTT_USERNAME).length() > 0) {
    connected = client_.connect("crowpanel-h2c-ams", MQTT_USERNAME, MQTT_PASSWORD);
  } else {
    connected = client_.connect("crowpanel-h2c-ams");
  }

  if (connected) {
    publishStatus("MQTT connected");
    subscribe();
  } else {
    publishStatus("MQTT failed rc=" + String(client_.state()));
  }
}

void DisplayMqtt::subscribe() {
  String topic = String(DISPLAY_TOPIC_PREFIX) + "/#";
  client_.subscribe(topic.c_str());
}

void DisplayMqtt::handleMessage(char *topicChars, byte *payload, unsigned int length) {
  String topic(topicChars);
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    return;
  }

  String base = String(DISPLAY_TOPIC_PREFIX);
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
    printer.activeLabel = jsonString(doc["active_label"]);
    printer.progress = jsonInt(doc["progress"], -1);
    printer.layer = jsonInt(doc["layer"], -1);
    printer.totalLayers = jsonInt(doc["total_layers"], -1);
    printer.remainingSeconds = jsonInt(doc["remaining_seconds"], -1);
    printer.remainingMinutes = jsonInt(doc["remaining_minutes"], -1);
    printer.speedLevel = jsonInt(doc["speed_level"], -1);
    printer.activeExtruder = jsonInt(doc["active_extruder"], 0);
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
    slot.empty = doc["empty"].isNull() ? true : doc["empty"].as<bool>();
    slot.active = doc["active"].isNull() ? false : doc["active"].as<bool>();
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
    slot.rfid = doc["rfid"].isNull() ? false : doc["rfid"].as<bool>();
    if (slotCallback_) {
      slotCallback_(slot);
    }
    return;
  }

  String metaPrefix = base + "/ams/";
  String metaSuffix = "/meta";
  if (topic.startsWith(metaPrefix) && topic.endsWith(metaSuffix)) {
    int idStart = metaPrefix.length();
    int idEnd = topic.indexOf('/', idStart);
    AmsMetaState meta;
    meta.amsId = topic.substring(idStart, idEnd).toInt();
    meta.amsKind = jsonString(doc["ams_kind"]);
    meta.temperature = jsonString(doc["temperature"]);
    meta.humidity = jsonString(doc["humidity"]);
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
