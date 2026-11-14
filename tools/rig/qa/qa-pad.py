"""A virtual Xbox 360 pad (ViGEmBus) for the rig QA suite's gamepad-mode scenarios (tools/rig/qa/README.md, "Gamepad
mode scenarios").

    qa-pad.py <ViGEmClient.dll> <state file> <log file> [<owner pid>]

Plugs a pad in, then sets its state from <state file> each time the file changes, until the file says `end` or is
gone, the owner (the suite's process; a venv's python.exe is a launcher, so not the parent) is gone, or MAX_SECONDS
have passed (a suite killed hard leaves no pad plugged in);
then unplugs it. The state is one line of comma-separated parts, all held until the next state: buttons `a`,
`b`, `x`, `y`, `lb`, `rb`, `ls`, `rs` (the stick clicks), `start`, `back`, `up`, `down`, `left`, `right` (the D-pad),
triggers `lt`, `rt` (pulled all the way), sticks `lx=N`, `ly=N`, `rx=N`, `ry=N` (-32768 to 32767, up and right
positive). An empty line is the pad at rest. Each state applied is logged with its time. Needs ViGEmBus installed
and the client DLL (ViGEmClient, from the ViGEm project's releases; the suite looks for it in the rig's
tools\\bin\\vigem).
"""
import ctypes
import os
import sys
import time
from ctypes import c_byte, c_short, c_uint, c_ushort, c_void_p, Structure


class XusbReport(Structure):
    _fields_ = [("wButtons", c_ushort), ("bLeftTrigger", c_byte), ("bRightTrigger", c_byte),
                ("sThumbLX", c_short), ("sThumbLY", c_short), ("sThumbRX", c_short), ("sThumbRY", c_short)]


VIGEM_OK = 0x20000000
BUTTONS = {"up": 0x0001, "down": 0x0002, "left": 0x0004, "right": 0x0008, "start": 0x0010, "back": 0x0020,
           "ls": 0x0040, "rs": 0x0080, "lb": 0x0100, "rb": 0x0200, "a": 0x1000, "b": 0x2000, "x": 0x4000,
           "y": 0x8000}
STICKS = {"lx": "sThumbLX", "ly": "sThumbLY", "rx": "sThumbRX", "ry": "sThumbRY"}
MAX_SECONDS = 20 * 60  # longer than any scenario
STILL_ACTIVE = 259


def process_alive(pid):
    """Whether process `pid` still runs (Windows; True when it cannot be told)."""
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.restype = c_void_p
    kernel32.OpenProcess.argtypes = (c_uint, ctypes.c_int, c_uint)
    kernel32.GetExitCodeProcess.argtypes = (c_void_p, ctypes.POINTER(ctypes.c_ulong))
    kernel32.CloseHandle.argtypes = (c_void_p,)
    handle = kernel32.OpenProcess(0x1000, 0, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    code = ctypes.c_ulong()
    ok = kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
    kernel32.CloseHandle(handle)
    return not ok or code.value == STILL_ACTIVE


def parse(text):
    """The report for a state line; raises ValueError on a part it does not know."""
    r = XusbReport()
    for part in (p.strip().lower() for p in text.split(",")):
        if not part:
            continue
        if "=" in part:
            key, value = (s.strip() for s in part.split("=", 1))
            if key not in STICKS:
                raise ValueError("unknown stick '%s'" % key)
            setattr(r, STICKS[key], max(-32768, min(32767, int(value))))
        elif part in BUTTONS:
            r.wButtons |= BUTTONS[part]
        elif part in ("lt", "rt"):
            setattr(r, "bLeftTrigger" if part == "lt" else "bRightTrigger", -1)  # 255 in the signed byte field
        else:
            raise ValueError("unknown part '%s'" % part)
    return r


def main():
    if len(sys.argv) not in (4, 5):
        print(__doc__)
        return 2
    dll_path, state_path, log_path = sys.argv[1:4]
    owner = int(sys.argv[4]) if len(sys.argv) == 5 else None
    log = open(log_path, "a", encoding="utf-8", buffering=1)

    def note(text):
        log.write("%s %s\n" % (time.strftime("%H:%M:%S") + ("%.3f" % (time.time() % 1))[1:], text))

    try:
        dll = ctypes.CDLL(dll_path)
        dll.vigem_alloc.restype = c_void_p
        dll.vigem_connect.argtypes = (c_void_p,)
        dll.vigem_connect.restype = c_uint
        dll.vigem_target_x360_alloc.restype = c_void_p
        dll.vigem_target_add.argtypes = (c_void_p, c_void_p)
        dll.vigem_target_add.restype = c_uint
        dll.vigem_target_remove.argtypes = (c_void_p, c_void_p)
        dll.vigem_target_free.argtypes = (c_void_p,)
        dll.vigem_disconnect.argtypes = (c_void_p,)
        dll.vigem_free.argtypes = (c_void_p,)
        dll.vigem_target_x360_update.argtypes = (c_void_p, c_void_p, XusbReport)
        dll.vigem_target_x360_update.restype = c_uint
    except (OSError, AttributeError) as e:
        note("the ViGEm client could not be loaded from %s: %s" % (dll_path, e))
        return 1
    client = dll.vigem_alloc()
    rc = dll.vigem_connect(client)
    if rc != VIGEM_OK:
        note("vigem_connect failed 0x%x (is ViGEmBus installed?)" % rc)
        return 1
    pad = dll.vigem_target_x360_alloc()
    rc = dll.vigem_target_add(client, pad)
    if rc != VIGEM_OK:
        note("vigem_target_add failed 0x%x" % rc)
        dll.vigem_disconnect(client)
        dll.vigem_free(client)
        return 1
    note("pad plugged in")
    try:
        dll.vigem_target_x360_update(client, pad, XusbReport())
        stamp = None
        started = time.time()
        checked = started
        while True:
            if time.time() - started > MAX_SECONDS:
                note("ran %d s: unplugged" % MAX_SECONDS)
                break
            if owner and time.time() - checked > 1.0:
                checked = time.time()
                if not process_alive(owner):
                    note("its owner (pid %d) is gone: unplugged" % owner)
                    break
            try:
                now = os.stat(state_path).st_mtime_ns
            except FileNotFoundError:
                note("state file gone")
                break
            if now != stamp:
                stamp = now
                try:
                    with open(state_path, encoding="utf-8") as f:
                        text = f.read().strip()
                except (PermissionError, OSError):
                    stamp = None  # being replaced: read again
                    time.sleep(0.002)
                    continue
                if text == "end":
                    break
                try:
                    report = parse(text)
                except ValueError as e:
                    note("state '%s' not applied: %s" % (text, e))
                    continue
                rc = dll.vigem_target_x360_update(client, pad, report)
                note("state '%s'%s" % (text, "" if rc == VIGEM_OK else " (update failed 0x%x)" % rc))
            time.sleep(0.005)
    finally:
        dll.vigem_target_x360_update(client, pad, XusbReport())
        dll.vigem_target_remove(client, pad)
        dll.vigem_target_free(pad)
        dll.vigem_disconnect(client)
        dll.vigem_free(client)
        note("pad unplugged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
