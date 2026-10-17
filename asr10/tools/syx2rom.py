#!/usr/bin/env python3
"""Rebuild the ASR-10 boot ROM from the SysEx dump the explorer OS sends.

Each message: F0 7D 'A' 'S' 'R', a 28-bit address as four 7-bit bytes,
256 data bytes as 512 nibbles (high first), a 7-bit sum of the nibbles, F7.
The dump loops, so a capture may start mid-ROM and include repeats; every
chunk is checked and the first good copy kept.

Usage: syx2rom.py <capture.syx> <out.bin>   (out covers 0xF80000-0xFBFFFF)
"""

import sys

ROM_START, ROM_END, CHUNK = 0xF80000, 0xFC0000, 256


def parse(data):
    """Yield (address, bytes) for every intact message; count bad ones."""
    good, bad = {}, 0
    i = 0
    while True:
        i = data.find(b"\xf0\x7dASR", i)
        if i < 0:
            break
        end = i + 5 + 4 + 2 * CHUNK + 2
        msg = data[i:end]
        i += 1
        if len(msg) < end - (i - 1) or msg[-1] != 0xF7:
            bad += 1
            continue
        a = msg[5:9]
        addr = (a[0] << 21) | (a[1] << 14) | (a[2] << 7) | a[3]
        nib = msg[9:9 + 2 * CHUNK]
        if any(n > 0x0F for n in nib) or (sum(nib) & 0x7F) != msg[-2]:
            bad += 1
            continue
        good.setdefault(addr, bytes((nib[k] << 4) | nib[k + 1] for k in range(0, len(nib), 2)))
    return good, bad


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    good, bad = parse(open(argv[1], "rb").read())
    missing = [a for a in range(ROM_START, ROM_END, CHUNK) if a not in good]
    rom = b"".join(good.get(a, b"\xff" * CHUNK) for a in range(ROM_START, ROM_END, CHUNK))
    open(argv[2], "wb").write(rom)
    total = (ROM_END - ROM_START) // CHUNK
    print(f"{argv[2]}: {total - len(missing)} of {total} chunks, {bad} damaged messages skipped")
    if missing:
        print(f"missing {len(missing)} chunks, first at {missing[0]:#x}; capture another pass to fill them")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
