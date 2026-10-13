#include "filaman_client.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include "app_config.h"
#include "config.h"

FilamanClient filaman;

namespace {

// Route ArduinoJson allocations to PSRAM so parsing display snapshots
// doesn't exhaust the scarce internal heap (which TLS also needs ~40 KB of).
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
