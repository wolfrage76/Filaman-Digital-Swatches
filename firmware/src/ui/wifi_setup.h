#pragma once

// On-device WiFi provisioning: a full-screen overlay that scans for networks,
// lets the user pick an SSID and type the password on-screen, saves to NVS, and
// restarts to connect. Works with no existing network connection.

void wifi_setup_begin();  // build the (hidden) overlay once, at startup
void wifi_setup_open();   // show it and scan
void wifi_setup_close();
bool wifi_setup_is_open();
