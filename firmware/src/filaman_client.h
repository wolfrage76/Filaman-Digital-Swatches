#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <vector>

#include "models.h"

// Thin REST client for the FilaMan API (filament select -> slicer profile ->
// create spool -> write tag). All calls are blocking HTTP+JSON; run them from a
// deliberate user action, not the render path. Auth is a single ApiKey header
// (no CSRF). See docs/apispoolcreationworkflow.md.

// Result of any call. Check ok; on failure httpStatus/error explain why.
struct FmResult {
  bool ok = false;
  bool notModified = false;  // HTTP 304: keep the last display snapshot
  int httpStatus = 0;
  String error;
};

struct FmFilament {
  int id = 0;
  String designation;
  String materialType;
  String manufacturer;
  int defaultSpoolWeightG = 0;
  int rawMaterialWeightG = 0;
  // Current default slicer profile (custom_fields.bambu_profile_base_name); empty
  // if none set. Shown so the picker can highlight the active one.
  String profileBaseName;
};

// A Bambu cloud slicer preset, deduped to base names (group=base). Send baseName
// to the set-profile endpoints — never the PFUS code.
struct FmCloudPreset {
  String baseName;
  String displayName;
  String model;
  bool isCustom = false;
};

// Fields for creating a spool. Only non-sentinel fields are sent, so extending
// this later (add a member, set it in createSpool) needs no call-site changes.
struct FmSpoolCreate {
  int filamentId = 0;
  int initialTotalWeightG = -1;
  int emptySpoolWeightG = -1;
  int spoolCoreWeightG = -1;  // core/adapter insert weight (optional)
  int remainingWeightG = -1;  // net filament; set explicitly so it isn't inferred
  int locationId = -1;
  int lowWeightThresholdG = -1;
  String lotNumber;
  // Add more SpoolCreate fields here as needed (purchase_price, custom_fields...).
};

// A filament manufacturer (for the selection dropdown).
struct FmManufacturer {
  int id = 0;
  String name;
};

// An online FilaMan device (scale/NFC) that can write RFID tags.
struct FmDevice {
  int id = 0;
  String name;
  String ip;
};

// State of an in-progress tag write.
enum class FmWrite { None, Pending, Success, Error };
struct FmWriteResult {
  FmWrite status = FmWrite::None;
  String tagUuid;
  String error;
};

// Target for a slicer-profile set: a filament (inherited by new spools) or a
// single spool (override). One picker UI, two endpoints.
enum class FmProfileTarget { Filament, Spool };

class FilamanClient {
 public:
  void begin(const String &baseUrl, const String &apiKey, bool insecureTls);
  bool configured() const { return baseUrl_.length() && apiKey_.length(); }

  // Phase 1: filament list/search. `total` returns the full match count.
  // manufacturerId 0 = any (server filters by manufacturer_id + free-text search;
  // material must be filtered client-side — the server ignores material_type).
  FmResult listFilaments(const String &search, int manufacturerId, int page, int pageSize,
                         std::vector<FmFilament> &out, int &total);
  FmResult getFilament(int filamentId, FmFilament &out);
  // Manufacturer list for the selection dropdown (sorted by name).
  FmResult listManufacturers(std::vector<FmManufacturer> &out);

  // Phase 2: slicer profiles. printerId 0 auto-detects a Bambuddy printer.
  FmResult resolvePrinterId(int &printerIdOut);
  // Connected printer models (e.g. ["H2C","P2S"]) via the driver's list action.
  FmResult listConnectedModels(int printerId, std::vector<String> &out);
  // Dashboard: list FilaMan printers (any driver — the Display API is universal).
  // If preferredActiveId is empty, picks the first / is_active printer into it.
  FmResult listBambuddyPrinters(PrinterOption *out, int maxOut, int &countOut,
                                String &preferredActiveId);
  // Dashboard: GET /api/v1/display/printers/{id} (FilaMan Display API, schema v3).
  FmResult fetchDisplaySnapshot(int printerId, bool refresh, JsonDocument &docOut);
  // Loads base-name presets compatible with the given models (server-filtered,
  // deduped). Pass ALL connected models for a base/default picker (any connected
  // model), or a SINGLE model for a per-model override picker (only that model's
  // profiles). Base names are structured "Manufacturer Material Color...".
  FmResult loadPresetsForModels(int printerId, const std::vector<String> &models,
                                std::vector<FmCloudPreset> &out);
  // Progressive drill-down helpers over an already-loaded preset set.
  void presetManufacturers(const std::vector<FmCloudPreset> &presets, std::vector<String> &out);
  void presetMaterials(const std::vector<FmCloudPreset> &presets, const String &manufacturer,
                       std::vector<String> &out);
  void presetsForManMat(const std::vector<FmCloudPreset> &presets, const String &manufacturer,
                        const String &material, std::vector<FmCloudPreset> &out);
  // Set the default (base) profile — inherited by new spools, spans all models.
  FmResult setSlicerProfile(FmProfileTarget target, int targetId, const String &baseName,
                            bool applyToExisting);
  // Set a per-model override (only affects that printer model).
  FmResult setModelOverride(FmProfileTarget target, int targetId, const String &model,
                            const String &baseName);

  // Phase 4: create. Returns the new spool_id.
  FmResult createSpool(const FmSpoolCreate &req, int &spoolIdOut);

  // Phase 5: RFID tag write (needs an online scale/NFC device).
  FmResult listActiveDevices(std::vector<FmDevice> &out);
  FmResult startWriteTag(int deviceId, int spoolId);   // async: begins the write
  FmResult getWriteStatus(int deviceId, FmWriteResult &out);  // poll until Success/Error

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
