#!/usr/bin/env python3
"""Apply small, checked patches to your own copy of Ensoniq's ASR-10 OS.

This is the one place the project changes Ensoniq's code instead of
replacing it. Each patch names the OS file it is for (by SHA-1), the bytes
it expects to find, and the bytes it writes. Nothing is changed unless the
file and every expected byte match. Only the patch descriptions live in
this repository; the OS stays in local/.

Patches:

  skip-keyboard-tune   OS 3.53. Start-up sets "tune the keyboard" (RAM
                       0x3BD) to 1 on every boot (`move.b #1,$3BD` at
                       0x17140, file offset 0x1CB40). When tuning fails
                       and you answer Cancel, the OS clears that flag and
                       carries on. This sets it to 0 instead, so the OS
                       skips tuning and the failure message; the keyboard
                       behaves as it does after Cancel.

Usage:
  patch_os.py <os-file-in> <out-os-file> <patch> [<patch> ...]
  patch_os.py --list
"""

import hashlib
import sys

PATCHES = {
    "skip-keyboard-tune": {
        "os": "3.53",
        "offset": 0x1CB40,
        "expect": bytes.fromhex("11fc000103bd"),  # move.b #1,$03BD
        "write": bytes.fromhex("11fc000003bd"),   # move.b #0,$03BD
    },
}

OS_SHA1 = {
    "3.53": "023e876a6fe8b013dc1ad8f4efa244a1a4d992d3",  # the OS file on the user's boot disk
}


class PatchError(Exception):
    pass


def apply(data, names):
    data = bytearray(data)
    digest = hashlib.sha1(data).hexdigest()
    for name in names:
        if name not in PATCHES:
            raise PatchError(f"unknown patch {name!r}; see --list")
        p = PATCHES[name]
        want = OS_SHA1[p["os"]]
        if digest != want:
            raise PatchError(f"{name} is for OS {p['os']} (SHA-1 {want}); this file is {digest}")
        off, expect = p["offset"], p["expect"]
        found = bytes(data[off:off + len(expect)])
        if found != expect:
            raise PatchError(f"{name}: expected {expect.hex()} at {off:#x}, found {found.hex()}")
        data[off:off + len(p["write"])] = p["write"]
    return bytes(data)


def main(argv):
    if len(argv) == 2 and argv[1] == "--list":
        for name, p in PATCHES.items():
            print(f"{name}  (OS {p['os']})")
        return 0
    if len(argv) < 4:
        print(__doc__.strip().split("Usage:")[1], file=sys.stderr)
        return 2
    try:
        out = apply(open(argv[1], "rb").read(), argv[3:])
    except (PatchError, OSError) as err:
        print("error:", err, file=sys.stderr)
        return 1
    open(argv[2], "wb").write(out)
    print(f"{argv[2]}: applied {', '.join(argv[3:])}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
