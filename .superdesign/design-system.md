# Digital Swatch System — panel design system

## Product
A wall-mounted 800×480 touchscreen for a Bambu Lab printer. It shows AMS trays as real filament swatches, plus live print status, through FilaMan. The person using it is standing at the printer, often with filament in hand. They need to see color, material, and how much is left without leaning in.

Boards: Elecrow CrowPanel 7″, Waveshare ESP32-S3-Touch-LCD-7, Guition JC4880P443C 4.3″ (shown as 800×480). Touch targets stay at least 44px.

## Screens
1. AMS View — home. Unit cards for AMS A–D and AMS-HT. Tap a unit for its trays. Idle returns here after 60 seconds.
2. AMS unit — 2×2 tray cards (A–D) or HT1–HT4. Back returns home.
3. Print Status — state, progress, time left, job, active filament, nozzle and bed temperatures. Opened by tapping the printer name.
4. Alerts — HMS and low-spool rows. An amber banner on other screens jumps here.
5. Menu — Print Status, AMS View, Alerts, then Select Printer, Wi-Fi, FilaMan, MQTT, Display, Device, Update, System.
6. Wi-Fi, FilaMan setup, and printer pick are full-screen overlays.

## Job to be done
Glance: which spool is which color, is it low, is the printer actually printing. Tap: open the unit, the job, or the menu. Never hide the filament color behind chrome.

## Color
Chrome is industrial charcoal so the spool colors stay the loudest thing on the glass.

- Background `#0A0E14`
- Surface `#121820`
- Surface 2 `#182028`
- Border `#2A3544`
- Border soft `#1E2834`
- Text `#F2F5F8`
- Muted `#8B98A8`
- Dim `#5C6B7A`
- Accent mint `#3DDC97` — linked state, material pills, healthy remaining, active tray
- Warn `#FFB020` — connecting, spool under 30%
- Danger `#FF5C6A` — Wi-Fi failure, spool under 15%
- AMS card `#13261D`, bay `#183225`
- Glow rim `#D6FF3A` only on glow filament

Filament hex values are data. Paint the swatch with the spool color. Do not recolor spools to match the accent.

## Type
Montserrat only. Do not introduce a second family.
- 22 titles
- 28 the print percentage only
- 18 color names and time remaining
- 14 body
- 12 captions, badges, spec labels (BRAND, MIN/MAX, BED, REMAINING)

## Shape
Tight radii: 8 on tray cards and header buttons, 4 on badges, 0 on AMS unit cards, 16 on the print hero only. 1px borders. No drop shadows. Remaining bars are 8px on detail and 4px on the overview.

## Layout
800×480, 4px page inset, 44px header. Header order: back (hidden on AMS View), title, status, Wi-Fi, version, menu. Content fills the rest. No marketing sections, no hero illustration, no footer nav in the current shell.

## Data that must survive any redesign
- Bay ids A1–A4, B1–B4, C1–C4, D1–D4, HT1–HT4
- Material (PLA, PETG, ABS, ASA, …)
- Color name and brand
- Nozzle min/max and bed temperature
- Remaining percent, grams, and meters
- Unit temperature, humidity, and DRY
- Effect tags: GLOW, TRANS, GALAXY, SPARKLE, GLITTER, CF, GF
- Print state, job name, layer progress, time remaining, ETA
- Active filament
- Nozzle L, nozzle R, bed, chamber
- Connection: green linked, amber connecting, red failed
- Firmware version and the update dot

## Motion
None for decoration. State changes are instant. Low spool is a color change, not an animation.

## Revamp constraints
A redesign may change hierarchy, density, and what is largest at arm's length. It must keep this palette, this type, these labels, and the real spool colors. It must still be one fixed 800×480 screen per page, tappable with a finger, and readable as an instrument panel rather than a website.
