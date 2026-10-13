#include "filaman_client.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include "app_config.h"
#include "config.h"

#include <algorithm>

FilamanClient filaman;

namespace {

// Route ArduinoJson allocations to PSRAM so parsing big filament/preset lists
// doesn't exhaust the ~scarce internal heap (which TLS also needs ~40 KB of).
struct PsramAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override {
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
  }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override {
    void *q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
    return q ? q : realloc(p, n);
  }
};
PsramAllocator g_psram;

// A JsonDocument that parses/builds in PSRAM.
struct PsramDoc : JsonDocument {
  PsramDoc() : JsonDocument(&g_psram) {}
};

// Minimal percent-encoding for query values.
String urlEncode(const String &s) {
  static const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

// One HTTP exchange. The caller owns the client so a plain request never
// constructs a TLS context (that allocation was exhausting the heap and
// aborting mDNS before OTA could advertise).
FmResult httpExchange(WiFiClient &client, const char *method, const String &url, const String &body,
                      const String &apiKey, JsonDocument &doc, uint16_t timeoutMs,
                      const char *ifNoneMatch, String *etagOut) {
  FmResult r;
  HTTPClient http;
  http.setTimeout(timeoutMs);
  if (!http.begin(client, url)) {
    r.error = "http.begin failed";
    return r;
  }
  const char *headerKeys[] = {"ETag"};
  http.collectHeaders(headerKeys, 1);
  http.addHeader("Authorization", String("ApiKey ") + apiKey);
  if (ifNoneMatch && ifNoneMatch[0]) {
    http.addHeader("If-None-Match", ifNoneMatch);
  }
  if (body.length()) {
    http.addHeader("Content-Type", "application/json");
  }

  int code;
  if (strcmp(method, "GET") == 0) {
    code = http.GET();
  } else if (strcmp(method, "POST") == 0) {
    code = http.POST(body);
  } else if (strcmp(method, "PATCH") == 0) {
    code = http.PATCH(body);
  } else {
    code = http.sendRequest(method, body);
  }

  r.httpStatus = code;
  if (code == 304) {
    r.notModified = true;
    r.ok = true;
    http.end();
    return r;
  }
  if (code > 0) {
    if (code >= 200 && code < 300) {
      if (etagOut) {
        *etagOut = http.header("ETag");
      }
      // Stream the body straight into the (PSRAM) doc — no big intermediate String.
      DeserializationError e = deserializeJson(doc, http.getStream());
      if (e && e != DeserializationError::EmptyInput) {
        r.error = String("JSON: ") + e.c_str();
      } else {
        r.ok = true;
      }
    } else {
      // Surface the server's error detail (FastAPI puts it under "detail").
      String errBody = http.getString();
      String detail = errBody;
      PsramDoc ed;
      if (deserializeJson(ed, errBody) == DeserializationError::Ok) {
        if (ed["detail"].is<const char *>()) {
          detail = ed["detail"].as<const char *>();
        } else if (ed["detail"]["message"].is<const char *>()) {
          detail = ed["detail"]["message"].as<const char *>();
        } else if (ed["detail"][0]["msg"].is<const char *>()) {
          detail = String(ed["detail"][0]["msg"].as<const char *>());
        }
      }
      detail.trim();
      r.error = String("HTTP ") + code + (detail.length() ? ": " + detail.substring(0, 90) : "");
    }
  } else {
    r.error = HTTPClient::errorToString(code);
  }
  http.end();
  return r;
}

}  // namespace

void FilamanClient::begin(const String &baseUrl, const String &apiKey, bool insecureTls) {
  baseUrl_ = baseUrl;
  while (baseUrl_.endsWith("/")) {
    baseUrl_.remove(baseUrl_.length() - 1);
  }
  apiKey_ = apiKey;
  insecure_ = insecureTls;
}

FmResult FilamanClient::request(const char *method, const String &path, const String &body,
                                JsonDocument &doc, uint16_t timeoutMs, const char *ifNoneMatch,
                                String *etagOut) {
  FmResult r;
  if (!configured()) {
    r.error = "FilaMan not configured";
    return r;
  }

  String builtin = FILAMAN_BASE_URL;
  while (builtin.endsWith("/")) {
    builtin.remove(builtin.length() - 1);
  }
  const String originalUrl = baseUrl_;

  for (int attempt = 0; attempt < 2; ++attempt) {
    const String url = baseUrl_ + path;
    if (baseUrl_.startsWith("https")) {
      WiFiClientSecure secure;
      if (insecure_) {
        secure.setInsecure();
      }
      r = httpExchange(secure, method, url, body, apiKey_, doc, timeoutMs, ifNoneMatch, etagOut);
    } else {
      WiFiClient plain;
      r = httpExchange(plain, method, url, body, apiKey_, doc, timeoutMs, ifNoneMatch, etagOut);
    }

    if (r.ok || attempt == 1 || r.httpStatus > 0) {
      break;
    }
    // A saved URL like "http://mckenna:8083" does not resolve on the ESP.
    // Retry once against the compile-time LAN origin when nothing answered.
    if (!builtin.length() || builtin == baseUrl_) {
      break;
    }
    Serial.printf("FilaMan %s unreachable (%s); retrying %s\n", baseUrl_.c_str(), r.error.c_str(),
                  builtin.c_str());
    baseUrl_ = builtin;
  }

  if (r.ok && baseUrl_ != originalUrl) {
    config().setFilamanBaseUrl(baseUrl_);
  } else if (!r.ok) {
    baseUrl_ = originalUrl;
  }
  return r;
}

FmResult FilamanClient::listFilaments(const String &search, int manufacturerId, int page,
                                      int pageSize, std::vector<FmFilament> &out, int &total) {
  String path = "/api/v1/filaments?page=" + String(page) + "&page_size=" + String(pageSize) +
                "&sort_by=designation&sort_order=asc";
  if (manufacturerId > 0) {
    path += "&manufacturer_id=" + String(manufacturerId);
  }
  if (search.length()) {
    path += "&search=" + urlEncode(search);
  }
  PsramDoc doc;
  FmResult r = request("GET", path, "", doc);
  if (!r.ok) {
    return r;
  }
  total = doc["total"] | 0;
  for (JsonObject it : doc["items"].as<JsonArray>()) {
    FmFilament f;
    f.id = it["id"] | 0;
    f.designation = it["designation"] | "";
    f.materialType = it["material_type"] | "";
    f.manufacturer = it["manufacturer"]["name"] | "";
    f.defaultSpoolWeightG = it["default_spool_weight_g"] | 0;
    f.rawMaterialWeightG = it["raw_material_weight_g"] | 0;
    f.profileBaseName = it["custom_fields"]["bambu_profile_base_name"] | "";
    out.push_back(f);
  }
  return r;
}

FmResult FilamanClient::getFilament(int filamentId, FmFilament &out) {
  PsramDoc doc;
  FmResult r = request("GET", "/api/v1/filaments/" + String(filamentId), "", doc);
  if (!r.ok) {
    return r;
  }
  out.id = doc["id"] | 0;
  out.designation = doc["designation"] | "";
  out.materialType = doc["material_type"] | "";
  out.manufacturer = doc["manufacturer"]["name"] | "";
  out.defaultSpoolWeightG = doc["default_spool_weight_g"] | 0;
  out.rawMaterialWeightG = doc["raw_material_weight_g"] | 0;
  out.profileBaseName = doc["custom_fields"]["bambu_profile_base_name"] | "";
  return r;
}

FmResult FilamanClient::listManufacturers(std::vector<FmManufacturer> &out) {
  PsramDoc doc;
  FmResult r = request("GET", "/api/v1/manufacturers?page=1&page_size=200&sort_by=name", "", doc);
  if (!r.ok) {
    return r;
  }
  // Endpoint may return {items:[...]} or a bare array; handle both.
  JsonArray items = doc["items"].is<JsonArray>() ? doc["items"].as<JsonArray>() : doc.as<JsonArray>();
  for (JsonObject it : items) {
    FmManufacturer m;
    m.id = it["id"] | 0;
    m.name = it["name"] | "";
    if (m.id > 0 && m.name.length()) {
      out.push_back(m);
    }
  }
  return r;
}

FmResult FilamanClient::resolvePrinterId(int &printerIdOut) {
  PsramDoc doc;
  FmResult r = request("GET", "/api/v1/printers", "", doc);
  if (!r.ok) {
    return r;
  }
  JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["items"].as<JsonArray>();
  int fallback = 0;  // a Bambuddy printer that isn't marked active, used if no active one
  for (JsonObject p : arr) {
    String driver = p["driver_key"] | "";  // FilaMan field is driver_key, not driver
    if (!driver.length()) {
      driver = p["driver"] | "";
    }
    driver.toLowerCase();
    if (driver.indexOf("bambuddy") < 0) {
      continue;
    }
    const int id = p["id"] | 0;
    if (id <= 0) {
      continue;
    }
    if (p["is_active"] | false) {
      printerIdOut = id;  // prefer an active printer
      return r;
    }
    if (!fallback) {
      fallback = id;
    }
  }
  if (fallback) {
    printerIdOut = fallback;
    return r;
  }
  r.ok = false;
  r.error = "No Bambuddy printer found";
  return r;
}

namespace {
// Response is {"presets":[...]}; tolerate a bare array or {"items":[...]} too.
JsonArray presetsArray(JsonDocument &doc) {
  if (doc["presets"].is<JsonArray>()) {
    return doc["presets"].as<JsonArray>();
  }
  if (doc.is<JsonArray>()) {
    return doc.as<JsonArray>();
  }
  return doc["items"].as<JsonArray>();
}

String firstToken(const String &s) {
  int sp = s.indexOf(' ');
  return sp < 0 ? s : s.substring(0, sp);
}
String secondToken(const String &s) {
  int a = s.indexOf(' ');
  if (a < 0) return "";
  int b = s.indexOf(' ', a + 1);
  return (b < 0) ? s.substring(a + 1) : s.substring(a + 1, b);
}

// Distinct values (case-insensitive), ordered most-frequent first.
void distinctByFrequency(const std::vector<String> &values, std::vector<String> &out) {
  std::vector<String> keys;      // uppercased key
  std::vector<String> display;   // first-seen display form
  std::vector<int> counts;
  for (const String &v : values) {
    if (!v.length()) continue;
    String key = v;
    key.toUpperCase();
    size_t j = 0;
    for (; j < keys.size(); ++j) {
      if (keys[j] == key) break;
    }
    if (j == keys.size()) {
      keys.push_back(key);
      display.push_back(v);
      counts.push_back(1);
    } else {
      counts[j]++;
    }
  }
  std::vector<size_t> idx(display.size());
  for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
  std::sort(idx.begin(), idx.end(),
            [&](size_t a, size_t b) { return counts[a] > counts[b]; });
  for (size_t i : idx) out.push_back(display[i]);
}
}  // namespace

FmResult FilamanClient::listConnectedModels(int printerId, std::vector<String> &out) {
  PsramDoc body;
  body["action"] = "list_connected_models";
  String bodyStr;
  serializeJson(body, bodyStr);
  PsramDoc doc;
  FmResult r = request(
      "POST", "/api/v1/printers/" + String(printerId) + "/driver/action", bodyStr, doc);
  if (!r.ok) {
    return r;
  }
  for (JsonObject m : doc["data"]["models"].as<JsonArray>()) {
    String model = m["model"] | "";
    if (model.length()) {
      out.push_back(model);
    }
  }
  return r;
}

FmResult FilamanClient::listBambuddyPrinters(PrinterOption *out, int maxOut, int &countOut,
                                             String &preferredActiveId) {
  countOut = 0;
  PsramDoc doc;
  FmResult r = request("GET", "/api/v1/printers", "", doc, 4000);
  if (!r.ok) {
    return r;
  }
  JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["items"].as<JsonArray>();
  String fallbackId;
  for (JsonObject p : arr) {
    // Any active printer works with the Display API (it is driver-agnostic).
    const int id = p["id"] | 0;
    if (id <= 0 || countOut >= maxOut) {
      continue;
    }
    out[countOut].id = String(id);
    out[countOut].name = p["name"] | "";
    out[countOut].model = p["model"] | "";
    if (!out[countOut].model.length()) {
      out[countOut].model = p["printer_model"] | "";
    }
    const bool isActive = p["is_active"] | false;
    out[countOut].active = false;
    if (!fallbackId.length() || isActive) {
      fallbackId = out[countOut].id;
    }
    countOut++;
  }
  if (!preferredActiveId.length() && fallbackId.length()) {
    preferredActiveId = fallbackId;
  }
  return r;
}

FmResult FilamanClient::fetchDisplaySnapshot(int printerId, bool /*refresh*/, JsonDocument &docOut) {
  // FilaMan Display API (schema v3): driver-agnostic, read-only, cheap to poll.
  // See docs/display-api.md in filaman-system. If-None-Match skips an unchanged board.
  if (printerId != displayEtagPrinter_) {
    displayEtag_ = "";
    displayEtagPrinter_ = printerId;
  }
  String etag;
  FmResult r = request("GET", "/api/v1/display/printers/" + String(printerId), "", docOut, 4000,
                       displayEtag_.length() ? displayEtag_.c_str() : nullptr, &etag);
  if (r.notModified) {
    return r;
  }
  if (r.ok) {
    displayEtag_ = etag;
  } else if (r.httpStatus == 404) {
    displayEtag_ = "";
  }
  return r;
}

FmResult FilamanClient::loadPresetsForModels(int printerId, const std::vector<String> &models,
                                             std::vector<FmCloudPreset> &out) {
  const size_t kCap = 400;
  const String stem =
      "/api/v1/printers/" + String(printerId) + "/driver/cloud-presets?group=base";
  FmResult r;
  r.ok = true;
  for (const String &model : models) {
    String path = stem;
    if (model.length()) {
      path += "&model=" + model;
    }
    PsramDoc doc;
    r = request("GET", path, "", doc);
    if (!r.ok) {
      return r;
    }
    JsonArray arr = presetsArray(doc);
    if (arr.size() == 0) {
      r = request("GET", path + "&refresh=1", "", doc);  // cold cache -> cloud reload
      if (!r.ok) {
        return r;
      }
      arr = presetsArray(doc);
    }
    for (JsonObject p : arr) {
      if (out.size() >= kCap) {
        break;
      }
      const char *bn = p["baseName"] | "";
      if (!bn[0]) {
        continue;
      }
      bool dup = false;  // dedupe by baseName across models (union)
      for (const FmCloudPreset &e : out) {
        if (e.baseName == bn) {
          dup = true;
          break;
        }
      }
      if (dup) {
        continue;
      }
      FmCloudPreset preset;
      preset.baseName = bn;
      preset.displayName = p["displayName"] | bn;
      preset.model = p["model"] | "";
      preset.isCustom = p["isCustom"] | false;
      out.push_back(preset);
    }
  }
  return r;
}

void FilamanClient::presetManufacturers(const std::vector<FmCloudPreset> &presets,
                                        std::vector<String> &out) {
  std::vector<String> firsts;
  for (const FmCloudPreset &p : presets) {
    firsts.push_back(firstToken(p.baseName));
  }
  distinctByFrequency(firsts, out);
}

void FilamanClient::presetMaterials(const std::vector<FmCloudPreset> &presets,
                                    const String &manufacturer, std::vector<String> &out) {
  String man = manufacturer;
  man.toUpperCase();
  std::vector<String> seconds;
  for (const FmCloudPreset &p : presets) {
    String f = firstToken(p.baseName);
    f.toUpperCase();
    if (f == man) {
      String s = secondToken(p.baseName);
      if (s.length()) seconds.push_back(s);
    }
  }
  distinctByFrequency(seconds, out);
}

void FilamanClient::presetsForManMat(const std::vector<FmCloudPreset> &presets,
                                     const String &manufacturer, const String &material,
                                     std::vector<FmCloudPreset> &out) {
  String man = manufacturer, mat = material;
  man.toUpperCase();
  mat.toUpperCase();
  for (const FmCloudPreset &p : presets) {
    String f = firstToken(p.baseName);
    f.toUpperCase();
    String s = secondToken(p.baseName);
    s.toUpperCase();
    if (f == man && s == mat) {
      out.push_back(p);
    }
  }
}

FmResult FilamanClient::setSlicerProfile(FmProfileTarget target, int targetId,
                                         const String &baseName, bool applyToExisting) {
  String path = (target == FmProfileTarget::Filament)
                    ? "/api/v1/filaments/" + String(targetId) + "/slicer-profile/default"
                    : "/api/v1/spools/" + String(targetId) + "/slicer-profile/default";
  PsramDoc body;
  body["base_name"] = baseName;
  if (target == FmProfileTarget::Filament) {
    body["apply_to_existing"] = applyToExisting;
  }
  String bodyStr;
  serializeJson(body, bodyStr);
  PsramDoc resp;
  return request("POST", path, bodyStr, resp);
}

FmResult FilamanClient::setModelOverride(FmProfileTarget target, int targetId, const String &model,
                                         const String &baseName) {
  const String tgt =
      (target == FmProfileTarget::Filament) ? "/api/v1/filaments/" : "/api/v1/spools/";
  const String path = tgt + String(targetId) + "/slicer-profile/models/" + model;
  PsramDoc body;
  body["base_name"] = baseName;
  String bodyStr;
  serializeJson(body, bodyStr);
  PsramDoc resp;
  return request("POST", path, bodyStr, resp);
}

FmResult FilamanClient::createSpool(const FmSpoolCreate &req, int &spoolIdOut) {
  PsramDoc body;
  body["filament_id"] = req.filamentId;
  if (req.initialTotalWeightG >= 0) {
    body["initial_total_weight_g"] = req.initialTotalWeightG;
  }
  if (req.emptySpoolWeightG >= 0) {
    body["empty_spool_weight_g"] = req.emptySpoolWeightG;
  }
  if (req.spoolCoreWeightG >= 0) {
    body["spool_core_weight_g"] = req.spoolCoreWeightG;
  }
  if (req.remainingWeightG >= 0) {
    body["remaining_weight_g"] = req.remainingWeightG;
  }
  if (req.locationId >= 0) {
    body["location_id"] = req.locationId;
  }
  if (req.lowWeightThresholdG >= 0) {
    body["low_weight_threshold_g"] = req.lowWeightThresholdG;
  }
  if (req.lotNumber.length()) {
    body["lot_number"] = req.lotNumber;
  }
  String bodyStr;
  serializeJson(body, bodyStr);
  PsramDoc resp;
  FmResult r = request("POST", "/api/v1/spools", bodyStr, resp);
  if (r.ok) {
    spoolIdOut = resp["id"] | 0;
  }
  return r;
}

FmResult FilamanClient::listActiveDevices(std::vector<FmDevice> &out) {
  PsramDoc doc;
  FmResult r = request("GET", "/api/v1/devices/active", "", doc);
  if (!r.ok) {
    return r;
  }
  JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["items"].as<JsonArray>();
  for (JsonObject d : arr) {
    FmDevice dev;
    dev.id = d["id"] | 0;
    dev.name = d["name"] | "";
    dev.ip = d["ip_address"] | "";
    if (dev.id > 0) {
      out.push_back(dev);
    }
  }
  return r;
}

FmResult FilamanClient::startWriteTag(int deviceId, int spoolId) {
  PsramDoc body;
  body["spool_id"] = spoolId;
  String bodyStr;
  serializeJson(body, bodyStr);
  PsramDoc resp;
  return request("POST", "/api/v1/devices/" + String(deviceId) + "/write-tag", bodyStr, resp);
}

FmResult FilamanClient::getWriteStatus(int deviceId, FmWriteResult &out) {
  PsramDoc doc;
  FmResult r = request("GET", "/api/v1/devices/" + String(deviceId) + "/write-status", "", doc);
  if (!r.ok) {
    return r;
  }
  String s = doc["status"] | "none";
  s.toLowerCase();
  if (s == "pending") {
    out.status = FmWrite::Pending;
  } else if (s == "success") {
    out.status = FmWrite::Success;
  } else if (s == "error") {
    out.status = FmWrite::Error;
  } else {
    out.status = FmWrite::None;
  }
  out.tagUuid = doc["tag_uuid"] | "";
  out.error = doc["error_message"] | "";
  return r;
}
