Import("env")

# custom_sdkconfig / ESP-IDF builds ignore build_src_filter, so every
# boards/<name>/*.cpp would compile. Park the non-selected board folders
# under .pio/board_stash/ for the duration of the build.
#
# Use a shared stash (not per-env) so switching crowpanel <-> waveshare
# always restores the parked folder before stashing the other.

import atexit
import os
import shutil

PROJECT_DIR = env.subst("$PROJECT_DIR")
BOARDS = os.path.join(PROJECT_DIR, "src", "boards")
STASH = os.path.join(PROJECT_DIR, ".pio", "board_stash")

BOARD_FOLDERS = {
    "BOARD_CROWPANEL_7": "crowpanel_7",
    "BOARD_WAVESHARE_S3_LCD7": "waveshare_s3_lcd7",
}


def selected_board_folder():
    flags = " ".join(str(f) for f in env.get("CPPDEFINES", []))
    flags += " " + env.subst("$BUILD_FLAGS")
    for macro, folder in BOARD_FOLDERS.items():
        if macro in flags:
            return folder
    return None


def restore_stashed_boards():
    """Pull any parked board folders back into src/boards/, including legacy per-env stashes."""
    if not os.path.isdir(STASH):
        return
    # Legacy layout: .pio/board_stash/<env>/<board>/
    for name in list(os.listdir(STASH)):
        path = os.path.join(STASH, name)
        if not os.path.isdir(path):
            continue
        # New layout: direct board folder under STASH
        if name in BOARD_FOLDERS.values():
            dst = os.path.join(BOARDS, name)
            if not os.path.exists(dst):
                shutil.move(path, dst)
                print("[select_board] restored boards/{}".format(name))
            continue
        # Old per-env stash directory
        for board in list(os.listdir(path)):
            src = os.path.join(path, board)
            if not os.path.isdir(src):
                continue
            dst = os.path.join(BOARDS, board)
            if not os.path.exists(dst):
                shutil.move(src, dst)
                print("[select_board] restored boards/{} (from {})".format(board, name))
        # Drop empty legacy env dirs
        try:
            if not os.listdir(path):
                os.rmdir(path)
        except OSError:
            pass


def stash_other_boards():
    board = selected_board_folder()
    if not board or not os.path.isdir(BOARDS):
        print("[select_board] no BOARD_* folder selected")
        return
    os.makedirs(STASH, exist_ok=True)
    restore_stashed_boards()
    for name in list(os.listdir(BOARDS)):
        path = os.path.join(BOARDS, name)
        if not os.path.isdir(path):
            continue
        if name == board:
            continue
        if name not in BOARD_FOLDERS.values():
            continue
        dest = os.path.join(STASH, name)
        if os.path.exists(dest):
            shutil.rmtree(dest)
        shutil.move(path, dest)
        print("[select_board] stashed boards/{} (building {})".format(name, board))


stash_other_boards()
# Put parked board folders back when PlatformIO exits so a Waveshare build
# does not leave CrowPanel sources out of the working tree (and the reverse).
atexit.register(restore_stashed_boards)
