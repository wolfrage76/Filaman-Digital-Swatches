# Page dependency trees

## Print Status (page 0)
Entry: `firmware/src/ui/ui.cpp` `buildStatusPage` (2693–2834), mounted from `ui_begin`
Dependencies:
- `initStyles` heroStyle, pillStyle, cardStyle
- `makeLabel`
- `makeMetricTile`
- `ensureSwatchLayers` / `applyColorSwatch` for the active filament chip
- Header from `ui_begin` (title = printer name)
- Data: `PrinterState` in `firmware/include/models.h`

## AMS View (page 1, home)
Entry: `buildOverviewPage` (2930–3060)
Dependencies:
- `buildOverviewBay` (2846–2928)
- `makeLabel`, `makeClimateIcon`, `ensureClimateIcons`
- `layoutOverviewBoard` (1828) sets 1- or 2-up card sizes
- `passClicksToParent` so the whole card is the hit target
- Header from `ui_begin`
- Data: `OverviewUnit` in `firmware/include/models.h`

## AMS A–D and AMS-HT (pages 2–6)
Entry: `columnsRoot` + `buildColumn` ×4 (2400–2690)
Dependencies:
- `columnStyle`, `activeColumnStyle`, `emptyColumnStyle`
- `makeLabel`
- `applyColorSwatch` on `colorRail`
- Header climate string from `amsClimateSummary`
- Back button returns to AMS View
- Data: `SlotState`, `AmsMetaState`

## Alerts (page 7)
Entry: `buildAlertsPage` (3062–3091)
Dependencies:
- `cardStyle`, `makeLabel`
- `alertBanner` in the shell (hidden until there is an alert; tap opens this page)

## Menu
Entry: `firmware/src/ui/menu_ui.cpp` `menu_begin` / `menu_open`
Dependencies:
- `addCardHeader`, `makeMenuTile`
- Closes Wi-Fi and printer-select overlays when opened

## Wi-Fi setup
Entry: `firmware/src/ui/wifi_setup.cpp`
Dependencies:
- Full-screen overlay, network list, on-screen keyboard

## First-run FilaMan guide
Entry: `firmware/src/ui/setup_guide.cpp`

## Printer select
Entry: `firmware/src/ui/printer_select.cpp`
