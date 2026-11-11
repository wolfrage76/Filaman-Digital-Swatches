# Extractable components

## HeaderBar
- Source: `firmware/src/ui/ui.cpp` `ui_begin` header block
- Category: layout
- Description: 44px bar with back, title, status, Wi-Fi, version, and menu
- Extractable props: title (string, default "AMS View"), showBack (boolean, default false), statusText (string, default ""), wifiState (string, default "linked"), version (string, default "v0.23.1"), showUpdateBadge (boolean, default false)
- Hardcoded: button sizes, Montserrat, mint icons, charcoal surfaces

## AlertBanner
- Source: `firmware/src/ui/ui.cpp` alertBanner in `ui_begin`
- Category: layout
- Description: 28px amber strip under the header
- Extractable props: visible (boolean, default false), message (string, default "")
- Hardcoded: warn background, dark text, radius 8

## OverviewCard
- Source: `firmware/src/ui/ui.cpp` `buildOverviewPage` card loop
- Category: basic
- Description: AMS unit with name, climate, and a row of swatch bays
- Extractable props: unitName (string, default "AMS A"), temperature (string, default "53C"), humidity (string, default "18%"), drying (boolean, default false)
- Hardcoded: ams-card fill, square corners, 12px labels

## OverviewBay
- Source: `firmware/src/ui/ui.cpp` `buildOverviewBay`
- Category: basic
- Description: One slot: color bar, material, color name, brand, remaining
- Extractable props: bayId (string, default "A1"), material (string, default "PLA"), colorName (string, default "Olive Green"), brand (string, default "SUNLU"), percent (number, default 66), swatch (string, default "#6B8E23")
- Hardcoded: 34px swatch, 4px bar

## SlotCard
- Source: `firmware/src/ui/ui.cpp` `buildColumn`
- Category: basic
- Description: Detail tray with color rail, identity, spec strip, remaining
- Extractable props: bayId (string, default "A1"), slotLabel (string, default "SLOT 1"), material (string, default "PLA"), colorName (string), profile (string), brand (string), nozzle (string, default "190-230 C"), bed (string, default "--"), remaining (string), percent (number), active (boolean, default false), empty (boolean, default false)
- Hardcoded: 68px rail, spec captions BRAND MIN/MAX BED REMAINING

## StatusHero
- Source: `firmware/src/ui/ui.cpp` `buildStatusPage`
- Category: basic
- Description: Print state, percent, time left, job, progress, ETA
- Extractable props: state (string, default "PRINTING"), percent (string, default "42%"), timeLeft (string), job (string), eta (string)
- Hardcoded: 118px hero, 28px percent, mint time-left

## MenuHome
- Source: `firmware/src/ui/menu_ui.cpp`
- Category: layout
- Description: Full-screen menu of screen shortcuts and settings tiles
- Extractable props: showUpdateBadge (boolean, default false)
- Hardcoded: tile labels Print Status, AMS View, Alerts, Select Printer, WiFi, FilaMan, MQTT, Display, Device, Update, System
