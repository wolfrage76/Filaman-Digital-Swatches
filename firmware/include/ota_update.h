#pragma once

#include <Arduino.h>
#include <functional>

void ota_begin();
void ota_loop();
bool ota_is_active();

// Compare semantic versions (ignoring leading 'v'). Returns >0 if a > b, 0 if equal, <0 if a < b.
int ota_compare_versions(const String &a, const String &b);

// Check GitHub for the latest published firmware version.
bool ota_check_github_update(String &latestVersionOut, String &downloadUrlOut, String &errorOut);

// Download and flash firmware from GitHub into the inactive OTA partition.
// Calls `onProgress(pct)` (0..100) during the download/flash.
bool ota_install_github_update(const String &downloadUrl,
                               std::function<void(int pct)> onProgress,
                               String &errorOut);
