Import("env")

import os

# The vendored Arduino_GFX RGB driver hardcodes bb_invalidate_cache = false.
# For an fb-in-PSRAM + bounce-buffer panel that starves scanout under WiFi load,
# leaving cache-invalidate off yields stale/incoherent bounce reads (the artifacts
# and vertical-scroll desync seen on this panel). Espressif recommends enabling it
# for this exact config, and it also frees cache pressure. Re-applied every build
# so a `pio pkg` reinstall of the library can't silently revert it.

TARGET = os.path.join(
    env.subst("$PROJECT_LIBDEPS_DIR"),
    env.subst("$PIOENV"),
    "GFX Library for Arduino",
    "src",
    "databus",
    "Arduino_ESP32RGBPanel.cpp",
)


def patch():
    if not os.path.isfile(TARGET):
        print("[patch_bounce] driver not found yet: %s" % TARGET)
        return
    with open(TARGET, "r") as handle:
        src = handle.read()
    patched = src.replace(
        ".bb_invalidate_cache = false,",
        ".bb_invalidate_cache = true,",
    )
    if patched != src:
        with open(TARGET, "w") as handle:
            handle.write(patched)
        print("[patch_bounce] enabled bb_invalidate_cache")
    else:
        print("[patch_bounce] bb_invalidate_cache already enabled")


# The panel handle is private. panelStopScanout() needs it to delete the
# driver (and its bounce-buffer ISR) before a new image is verified.
HEADER = TARGET[: -len(".cpp")] + ".h"
GETTER = "  esp_lcd_panel_handle_t panelHandle() const { return _panel_handle; }\n"


def patch_header():
    if not os.path.isfile(HEADER):
        print("[patch_bounce] header not found yet: %s" % HEADER)
        return
    with open(HEADER, "r") as handle:
        src = handle.read()
    if GETTER in src:
        return
    anchor = "  uint16_t *getFrameBuffer(int16_t w, int16_t h);\n"
    if anchor not in src:
        raise SystemExit("[patch_bounce] getFrameBuffer anchor missing in %s" % HEADER)
    with open(HEADER, "w") as handle:
        handle.write(src.replace(anchor, anchor + GETTER, 1))
    print("[patch_bounce] added panelHandle()")


patch()
patch_header()
