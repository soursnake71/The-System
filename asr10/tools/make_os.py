#!/usr/bin/env python3
"""Build an ASR-10 OS file around our own code.

Layout of the stock OS file, found by disassembling OS 1.61 (docs/hardware.md):

  0x000-0x3FF   header: an image of RAM 0x000-0x3FF. Holds the 68000 vector
                table and, at 0x1C0, the load table.
  0x400-...     segments, back to back, each loaded where the table says.

Load table at 0x1C0: up to 8 entries of (destination address, block count),
both 32-bit big-endian, ended by a zero destination. OS 1.61 has:

  0x000E00  0x39 blocks
  0xFF8000  0x40 blocks
  0x009C00  0x31 blocks
  0xFC6000  0x01 block    (68302 internal RAM)

We keep the stock header and load table byte for byte, so the boot ROM sees
the layout it expects, and change only:
  - the vectors whose stock handler is in the OS being loaded (in 3.53:
    illegal instruction, line F, traps 10 and 11, and four 68302
    interrupts). They now point at our entry point. Vectors whose handler
    is in the ROM stay as they are: the ROM uses them while it loads the
    OS, and pointing them at code that isn't loaded yet hangs the machine
    at "LOADING SYSTEM". 0x0C0-0x0FF holds data, not vectors, and is kept;
  - the segment contents: our code at the start of the segment that loads
    at ENTRY, and every other byte a NOP slide ending in a jump to ENTRY,
    in case the ROM jumps into the loaded OS somewhere.

The header comes from your own stock OS file at build time and is never
committed.

Usage: make_os.py <stock-os-file> <code.bin> <entry-hex> <out-os-file>
"""

import struct
import sys

BLOCK = 512
HEADER = 0x400
TABLE = 0x1C0
TABLE_MAX = 8
VECTOR_RANGES = ((0x008, 0x0C0), (0x100, 0x160))  # 0x0C0-0x0FF is data
NOP = b"\x4e\x71"


class OsError(Exception):
    pass


def load_table(header):
    segs = []
    for i in range(TABLE_MAX):
        dest, blocks = struct.unpack(">II", header[TABLE + 8 * i:TABLE + 8 * i + 8])
        if dest == 0:
            break
        segs.append((dest & 0xFFFFFF, blocks))
    return segs


def jmp(addr):
    return b"\x4e\xf9" + struct.pack(">I", addr)


def slide(length, entry):
    """NOPs ending in `jmp entry`: any even address in it leads to entry."""
    if length < 6:
        raise OsError("segment too small for a slide")
    return NOP * ((length - 6) // 2) + jmp(entry)


def build(stock, code, entry):
    if len(stock) % BLOCK or len(stock) < HEADER:
        raise OsError(f"stock OS file is {len(stock)} bytes, not whole blocks")
    header = bytearray(stock[:HEADER])
    segs = load_table(header)
    if not segs:
        raise OsError("no load table at 0x1C0")
    total = sum(b for _, b in segs) * BLOCK + HEADER
    if total != len(stock):
        raise OsError(f"load table covers {total} bytes, file is {len(stock)}")
    home = [s for s in segs if s[0] <= entry < s[0] + s[1] * BLOCK]
    if not home or home[0][0] != entry:
        raise OsError(f"entry {entry:#x} is not the start of a segment: "
                      + ", ".join(f"{d:#x}" for d, _ in segs))
    hooked = []
    for start, end in VECTOR_RANGES:
        for off in range(start, end, 4):
            target = struct.unpack(">I", header[off:off + 4])[0] & 0xFFFFFF
            if any(d <= target < d + b * BLOCK for d, b in segs):
                header[off:off + 4] = struct.pack(">I", entry)
                hooked.append(off // 4)
    if not hooked:
        raise OsError("no vector points into the loaded OS; nothing to take over")
    out = bytearray(header)
    for dest, blocks in segs:
        size = blocks * BLOCK
        if dest == entry:
            room = size - 6
            if len(code) > room or len(code) % 2:
                raise OsError(f"code is {len(code)} bytes; segment at {dest:#x} holds {room} (even)")
            seg = code + slide(size - len(code), entry)
        else:
            seg = slide(size, entry)
        out += seg
    assert len(out) == len(stock)
    return bytes(out)


def main(argv):
    if len(argv) != 5:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    try:
        stock = open(argv[1], "rb").read()
        code = open(argv[2], "rb").read()
        if len(code) % 2:
            code += b"\0"
        out = build(stock, code, int(argv[3], 16))
    except (OsError, OSError, ValueError) as err:
        print("error:", err, file=sys.stderr)
        return 1
    open(argv[4], "wb").write(out)
    print(f"{argv[4]}: {len(out)} bytes, code {len(code)} bytes at {int(argv[3], 16):#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
