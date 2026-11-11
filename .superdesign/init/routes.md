# Routes / screens

There is no URL router. Pages are integer indexes in `firmware/include/config.h`.

| Page index | Name | Builder | What it shows |
|---|---|---|---|
| 0 | Print Status | `buildStatusPage` | Printer name as title. State pill, huge percent, time remaining, job name, progress bar, ETA. Active filament swatch. Nozzle L/R, bed, chamber tiles. Secondary and next-job lines. |
| 1 | AMS View (home) | `buildOverviewPage` | Up to four AMS unit cards (A–D) in a wrap, plus a row of AMS-HT cards. Each card: unit name, temp, humidity, DRY flag, and one bay per loaded slot. Tap a card to open that unit. Idle returns here after 60s. |
| 2 | AMS A | `buildColumn` ×4 | 2×2 tray cards for slots A1–A4. |
| 3 | AMS B | same | B1–B4. |
| 4 | AMS C | same | C1–C4. |
| 5 | AMS D | same | D1–D4. |
| 6 | AMS-HT | same | HT1–HT4, one slot each. Header shows per-unit climate. |
| 7 | Alerts | `buildAlertsPage` | "No alerts" or a vertical list of title + detail cards. Hidden banner on other pages jumps here. |

Overlays, not pages:
- Menu (`menu_ui.cpp`)
- Wi-Fi setup (`wifi_setup.cpp`): network list and on-screen keyboard
- FilaMan first-run guide (`setup_guide.cpp`): URL, API key, QR to the web config
- Printer select (`printer_select.cpp`)

Web surfaces, separate from the panel:
- `docs/index.html` — browser installer for the three boards
- On-device web portal in `firmware/src/web_portal.cpp` — config, diagnostics, screenshot, OTA
