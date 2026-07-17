#pragma once

#include <Arduino.h>

#include "models.h"

void ui_begin();
void ui_loop();
void ui_next_page();
void ui_note_activity();
void ui_update_slot(const SlotState &slot);
void ui_update_printer(const PrinterState &printer);
void ui_update_meta(const AmsMetaState &meta);
void ui_set_connection_status(const String &message);
void ui_set_ip_address(const String &ip);
