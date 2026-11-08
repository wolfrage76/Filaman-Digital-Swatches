Import("env")

import os
import urllib.request

URL = "https://espressif.github.io/arduino-esp32/hosted/esp32c6-v2.9.6.bin"
PROJECT = env.subst("$PROJECT_DIR")
BIN_PATH = os.path.join(PROJECT, "esp32c6-v2.9.6.bin")
ASM_PATH = os.path.join(PROJECT, "src", "c6_fw.S")


def fetch():
    if os.path.isfile(BIN_PATH) and os.path.getsize(BIN_PATH) > 200000:
        print("[c6_ota] using cached {}".format(BIN_PATH))
        return
    print("[c6_ota] downloading {}".format(URL))
    urllib.request.urlretrieve(URL, BIN_PATH)
    size = os.path.getsize(BIN_PATH)
    if size < 200000:
        raise SystemExit("[c6_ota] download too small ({} bytes)".format(size))
    print("[c6_ota] saved {} ({} bytes)".format(BIN_PATH, size))


def write_asm():
    os.makedirs(os.path.dirname(ASM_PATH), exist_ok=True)
    incbin = os.path.abspath(BIN_PATH).replace("\\", "/")
    with open(ASM_PATH, "w", encoding="utf-8") as out:
        out.write('.section .rodata.c6_fw,"a"\n')
        out.write(".global c6_fw_start\n")
        out.write(".global c6_fw_end\n")
        out.write(".align 4\n")
        out.write("c6_fw_start:\n")
        out.write('.incbin "{}"\n'.format(incbin))
        out.write("c6_fw_end:\n")
    print("[c6_ota] wrote {}".format(ASM_PATH))


fetch()
write_asm()
