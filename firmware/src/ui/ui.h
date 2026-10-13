#pragma once

#include <Arduino.h>

#include "models.h"

void ui_begin();
void ui_open_page(int page);
void ui_loop();
void ui_next_page();
void ui_prev_page();
void ui_note_activity();
void ui_note_fresh();
void ui_update_slot(const SlotState &slot);
void ui_update_printer(const PrinterState &printer);
void ui_update_meta(const AmsMetaState &meta);
void ui_update_bridge(const BridgeHealth &health);
void ui_update_alerts(const AlertItem *alerts, int count);
void ui_update_overview(const OverviewUnit *units, int count);
void ui_set_connection_status(const String &message);
void ui_set_ip_address(const String &ip);
void ui_handle_swipe(int16_t startX, int16_t endX);
bool ui_has_active_alerts();
// Green badge on the hamburger when auto-check found a newer GitHub release.
void ui_refresh_update_badge();
