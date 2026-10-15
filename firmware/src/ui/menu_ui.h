#pragma once

// Full-screen menu opened from the header hamburger. Switches pages and holds
// the configuration forms and GitHub OTA updater.

void menu_begin();
void menu_open();
void menu_open_update();
void menu_open_filaman();
void menu_close();
bool menu_is_open();
