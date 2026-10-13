#pragma once

#include <Arduino.h>

// Polls FilaMan's Display API (GET /api/v1/display/printers/{id}, schema v3),
// maps it onto the panel's page layout and feeds the same
// ui_update_* callbacks as MQTT. Active when FilamanClient is configured.

void display_poll_begin();
void display_poll_loop();
bool display_poll_active();
// True after repeated Display API failures. The panel keeps polling and also
// accepts MQTT until the next successful snapshot.
bool display_poll_needs_mqtt();
// Printer picker selection (FilaMan printer id as decimal string).
void display_poll_set_printer(const String &filamanPrinterId);
String display_poll_active_printer();
// Short reason the board is or isn't updating. Safe to show on the config page.
const char *display_poll_status();
