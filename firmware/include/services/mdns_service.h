#pragma once

// The mDNS responder. Makes the panel answer to "<hostname>.local",
// advertises the web console as an _http._tcp service and the OTA port as
// _arduino._tcp, so the panel turns up in Bonjour browsers and the Arduino
// IDE rather than only at an address read off the screen.
//
// ArduinoOTA does not own the responder: it starts mDNS once and never again,
// so a WiFi drop left the panel without a .local name until the next reboot.

// True while the responder is up.
bool mdns_running();

// Idempotent, called from loop(). Derives the responder and its services from
// WiFi, the stored hostname, the mDNS switch and whether the web console is
// listening, so a reconnect or rename takes effect on the next pass.
void mdns_sync_state();

// Takes the responder down now, while the network interface still exists.
// Call before anything that disconnects WiFi or changes its mode: on core 3
// the interface goes with the mode, and a responder freed after that walks a
// dead interface (panic in mdns_free()). The next mdns_sync_state() brings it
// back once the link is up.
void mdns_stop();
