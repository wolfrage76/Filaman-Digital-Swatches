#pragma once

#include <Arduino.h>
#include <functional>

void ota_begin();
void ota_loop();
bool ota_is_active();

// Records when this firmware version first saw a real (NTP) clock. Call from
// loop(); cheap once stamped. ota_installed_at() is 0 until then (Unix seconds).
void ota_install_stamp_loop();
uint32_t ota_installed_at();

// Compare semantic versions (ignoring leading 'v'). Returns >0 if a > b, 0 if equal, <0 if a < b.
int ota_compare_versions(const String &a, const String &b);

// Check GitHub for the latest published firmware version.
bool ota_check_github_update(String &latestVersionOut, String &downloadUrlOut, String &errorOut);

// Download and flash firmware from GitHub into the inactive OTA partition.
// Calls `onProgress(pct)` (0..100) during the download/flash.
bool ota_install_github_update(const String &downloadUrl,
                               std::function<void(int pct)> onProgress,
                               String &errorOut);

// Background poll when AppConfig::autoCheckUpdates is enabled.
void ota_auto_check_loop();
void ota_auto_check_request_now();  // run on next eligible loop (e.g. after enabling)

// Cached result from the last successful GitHub version check.
bool ota_update_available();
String ota_cached_latest_version();
String ota_cached_download_url();
