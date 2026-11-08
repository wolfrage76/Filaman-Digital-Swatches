Import("env")

import os
import time

# PlatformIO otherwise picks /dev/ttyS0, the motherboard UART, and esptool
# fails with "Could not configure port". The panel is the Espressif
# USB-Serial/JTAG device (VID 303a), which shows up as /dev/ttyACM*.

ESPRESSIF_VID = 0x303A
WAIT_S = 8


def serial_ports():
    try:
        from serial.tools import list_ports
    except ImportError:
        return []
    return list(list_ports.comports())


def choose_port():
    espressif = []
    usb = []
    for port in serial_ports():
        device = port.device or ""
        if device.startswith("/dev/ttyS"):
            continue
        if port.vid == ESPRESSIF_VID:
            espressif.append(port)
        elif device.startswith("/dev/ttyACM") or device.startswith("/dev/ttyUSB"):
            usb.append(port)
    chosen = espressif or usb
    if not chosen:
        return None
    return chosen[0]


def wait_for_port():
    deadline = time.time() + WAIT_S
    while True:
        port = choose_port()
        if port is not None:
            return port
        if time.time() >= deadline:
            return None
        time.sleep(0.4)


def use_venv_esptool():
    # pioarduino's bundled esptool 5.1 loses the USB-JTAG link while its stub
    # is running. The project venv's 5.3 flasher completes the write.
    project_dir = env.subst("$PROJECT_DIR")
    for rel in ("../.venv/bin/esptool", "../../.venv/bin/esptool"):
        candidate = os.path.abspath(os.path.join(project_dir, rel))
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            env.Replace(UPLOADER=candidate)
            return


def before_upload(target, source, env):
    port = wait_for_port()
    if port is None:
        print(
            "No Espressif USB device found. Plug the Guition into the "
            "USB-Serial/JTAG port (it appears as /dev/ttyACM*) and run the upload again."
        )
        env.Exit(1)
    description = port.description or "USB serial"
    print("Upload port: {} ({})".format(port.device, description))
    env.Replace(UPLOAD_PORT=port.device, MONITOR_PORT=port.device)
    use_venv_esptool()


protocol = env.subst("$UPLOAD_PROTOCOL") or "esptool"
if protocol == "esptool":
    env.AddPreAction("upload", before_upload)
