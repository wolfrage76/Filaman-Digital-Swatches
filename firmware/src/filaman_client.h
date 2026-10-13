#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "models.h"

// Thin REST client for the FilaMan Display API (GET /api/v1/printers and
// GET /api/v1/display/printers/{id}). Auth is a single ApiKey header (no CSRF).

struct FmResult {
  bool ok = false;
  bool notModified = false;  // HTTP 304: keep the last display snapshot
  int httpStatus = 0;
  String error;
};

class FilamanClient {
 public:
  void begin(const String &baseUrl, const String &apiKey, bool insecureTls);
  bool configured() const { return baseUrl_.length() && apiKey_.length(); }

  // Dashboard: list FilaMan printers (any driver — the Display API is universal).
  // If preferredActiveId is empty, picks the first / is_active printer into it.
  FmResult listBambuddyPrinters(PrinterOption *out, int maxOut, int &countOut,
                                String &preferredActiveId);
  // Dashboard: GET /api/v1/display/printers/{id} (FilaMan Display API, schema v3).
  FmResult fetchDisplaySnapshot(int printerId, bool refresh, JsonDocument &docOut);

 private:
  String baseUrl_;
  String apiKey_;
  bool insecure_ = true;

  // Low-level request. method = GET/POST/PATCH; body empty for GET. On a 2xx the
  // response is streamed directly into `doc` (PSRAM-backed) to keep internal heap
  // low; pass a throwaway doc when the body isn't needed.
  FmResult request(const char *method, const String &path, const String &body, JsonDocument &doc,
                   uint16_t timeoutMs = 15000, const char *ifNoneMatch = nullptr,
                   String *etagOut = nullptr);

  String displayEtag_;
  int displayEtagPrinter_ = 0;
};

extern FilamanClient filaman;
