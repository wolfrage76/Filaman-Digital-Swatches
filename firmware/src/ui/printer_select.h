#pragma once

// Full-screen overlay to choose which printer to monitor.
// Reads the shared printer_list registry (Display API or MQTT).

void printer_select_begin();  // build the (hidden) overlay once, at startup
void printer_select_open();   // show it and render the current printer list
void printer_select_close();
bool printer_select_is_open();
