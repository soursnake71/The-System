#!/usr/bin/env python3
"""Convert Ensoniq floppy images between .img (sector data) and .hfe (HxC
bitstream, what the Gotek here uses).

Track layout: MFM, 80 cylinders, 2 sides, 512-byte sectors numbered from 0,
index mark, no interleave, 300 rpm, blocks stored cylinder by cylinder, side
0 then side 1 (the .img order). This matches FlashFloppy's and Greaseweazle's
Ensoniq profiles, except gap 3: they use 40 (HD) and 30 (DD), while the HFE
files from the Gotek this project targets use 36 for both. The header fields
and track lengths below are copied from those files too.

Usage:
  hfe.py to-hfe <in.img> <out.hfe>
  hfe.py to-img <in.hfe> <out.img>      (reads back any MFM HFE v1 image)
"""

import struct
import sys

SECTOR = 512
CYLS, SIDES = 80, 2
GEOMETRY = {  # sectors per track: (gap3, kbit/s, HFE interface mode, HFE track length)
    10: (36, 250, 0, 25088),   # 800 KB, interface IBM PC DD
    20: (36, 500, 1, 50024),   # 1.6 MB, interface IBM PC HD
}
GAP1, GAP2, GAP4A, SYNC = 50, 22, 80, 12


class HfeError(Exception):
    pass


def crc16(data, crc=0xFFFF):
    """CRC-CCITT as used by IBM-format floppies."""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


class MfmWriter:
    """Builds a track as MFM cells, first cell first."""

    def __init__(self):
        self.cells = []
        self.prev = 0

    def byte(self, value, count=1):
        for _ in range(count):
            for i in range(7, -1, -1):
                bit = (value >> i) & 1
                self.cells += [int(not (self.prev or bit)), bit]
                self.prev = bit

    def raw(self, pattern):
        """A 16-cell pattern with a missing clock (sync marks)."""
        self.cells += [(pattern >> i) & 1 for i in range(15, -1, -1)]
        self.prev = pattern & 1

    def data(self, data):
        for b in data:
            self.byte(b)


def build_track(sectors, cyl, head, gap3, cells_per_track):
    w = MfmWriter()
    w.byte(0x4E, GAP4A)
    w.byte(0x00, SYNC)
    for _ in range(3):
        w.raw(0x5224)  # C2 with missing clock
    w.byte(0xFC)
    w.byte(0x4E, GAP1)
    for r, payload in enumerate(sectors):
        idam = bytes([0xA1, 0xA1, 0xA1, 0xFE, cyl, head, r, 2])
        w.byte(0x00, SYNC)
        for _ in range(3):
            w.raw(0x4489)  # A1 with missing clock
        w.data(idam[3:] + struct.pack(">H", crc16(idam)))
        w.byte(0x4E, GAP2)
        dam = bytes([0xA1, 0xA1, 0xA1, 0xFB]) + payload
        w.byte(0x00, SYNC)
        for _ in range(3):
            w.raw(0x4489)
        w.data(dam[3:] + struct.pack(">H", crc16(dam)))
        w.byte(0x4E, gap3)
    if len(w.cells) > cells_per_track:
        raise HfeError(f"track is {len(w.cells)} cells, only {cells_per_track} fit")
    while len(w.cells) < cells_per_track:
        w.byte(0x4E)
    return w.cells[:cells_per_track]


def cells_to_hfe_bytes(cells):
    """HFE stores cells least significant bit first in each byte."""
    out = bytearray(len(cells) // 8)
    for i, c in enumerate(cells):
        if c:
            out[i // 8] |= 1 << (i % 8)
    return bytes(out)


def img_to_hfe(img):
    per_side = len(img) // (CYLS * SIDES * SECTOR)
    if per_side not in GEOMETRY or len(img) != CYLS * SIDES * per_side * SECTOR:
        raise HfeError(f"{len(img)} bytes is not an 800 KB or 1.6 MB Ensoniq image")
    gap3, rate, ifmode, track_len = GEOMETRY[per_side]
    side_bytes = track_len // 2
    cells = side_bytes * 8
    track_blocks = -(-track_len // 512)

    header = bytearray(b"\xff" * 512)
    header[0:26] = (b"HXCPICFE" + struct.pack("<BBBBHHBBH", 0, CYLS, SIDES, 0, rate, 0,
                                              ifmode, 1, 1)
                    + b"\xff\xff\xff\xff\xff\xff")
    lut = bytearray(b"\xff" * 512)
    tracks = bytearray()
    for cyl in range(CYLS):
        struct.pack_into("<HH", lut, cyl * 4, 2 + cyl * track_blocks, track_len)
        sides = []
        for head in range(SIDES):
            first = ((cyl * SIDES) + head) * per_side
            secs = [img[(first + s) * SECTOR:(first + s + 1) * SECTOR] for s in range(per_side)]
            sides.append(cells_to_hfe_bytes(build_track(secs, cyl, head, gap3, cells)))
        body = bytearray()
        for off in range(0, side_bytes, 256):
            for s in sides:
                body += s[off:off + 256].ljust(256, b"\x00")
        tracks += body.ljust(track_blocks * 512, b"\x00")
    return bytes(header + lut + tracks)


def hfe_to_img(hfe):
    """Decode every sector of an MFM HFE v1 image; checks all CRCs."""
    if hfe[:8] != b"HXCPICFE":
        raise HfeError("not an HFE v1 file")
    ncyl, nsides = hfe[9], hfe[10]
    lut_off = struct.unpack_from("<H", hfe, 18)[0] * 512
    found = {}
    for cyl in range(ncyl):
        off, length = struct.unpack_from("<HH", hfe, lut_off + cyl * 4)
        raw = hfe[off * 512:off * 512 + length]
        for head in range(nsides):
            side = bytearray()
            for blk in range(0, len(raw), 512):
                side += raw[blk + head * 256:blk + head * 256 + 256]
            cells = [(b >> i) & 1 for b in side for i in range(8)]
            for key, data in decode_side(cells):
                found[key] = data
    if not found:
        raise HfeError("no sectors found")
    per_side = max(r for _, _, r in found) + 1
    img = bytearray()
    for cyl in range(ncyl):
        for head in range(nsides):
            for r in range(per_side):
                if (cyl, head, r) not in found:
                    raise HfeError(f"missing sector c{cyl} h{head} r{r}")
                img += found[(cyl, head, r)]
    return bytes(img)


def decode_side(cells):
    word = 0
    sync = 0x4489
    i = 0
    n = len(cells)
    pending_id = None

    def read_bytes(pos, count):
        out = bytearray()
        for k in range(count):
            v = 0
            for j in range(8):
                v = (v << 1) | cells[pos + 16 * k + 2 * j + 1]
            out.append(v)
        return out

    while i < n:
        word = ((word << 1) | cells[i]) & 0xFFFF
        i += 1
        if word != sync or i + 16 * 2 > n:
            continue
        # skip the remaining A1 marks
        while i + 16 <= n and read_bytes(i, 1)[0] == 0xA1 and cells[i:i + 16] == [(sync >> b) & 1 for b in range(15, -1, -1)]:
            i += 16
        mark = read_bytes(i, 1)[0]
        if mark == 0xFE and i + 16 * 7 <= n:
            body = read_bytes(i, 7)
            if crc16(b"\xa1\xa1\xa1" + body) == 0:
                pending_id = (body[1], body[2], body[3], 128 << body[4])
            i += 16 * 7
        elif mark == 0xFB and pending_id and i + 16 * (3 + pending_id[3]) <= n:
            body = read_bytes(i, 3 + pending_id[3])
            if crc16(b"\xa1\xa1\xa1" + body) != 0:
                raise HfeError(f"bad data CRC at {pending_id[:3]}")
            yield pending_id[:3], bytes(body[1:-2])
            pending_id = None
            i += 16 * (3 + 512)


def main(argv):
    if len(argv) != 4 or argv[1] not in ("to-hfe", "to-img"):
        print(__doc__.strip().split("Usage:")[1], file=sys.stderr)
        return 2
    try:
        data = open(argv[2], "rb").read()
        out = img_to_hfe(data) if argv[1] == "to-hfe" else hfe_to_img(data)
    except (HfeError, OSError) as err:
        print("error:", err, file=sys.stderr)
        return 1
    open(argv[3], "wb").write(out)
    print(f"{argv[3]}: {len(out)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
