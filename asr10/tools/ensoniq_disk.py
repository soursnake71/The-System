#!/usr/bin/env python3
"""Inspect and modify Ensoniq EPS / EPS-16+ / ASR-10 floppy images (.img).

The layout below is the commonly documented Ensoniq disk format (as used by
EnsoniqFS, Epslin and FlashFloppy). Every assumption is checked against the
signatures on the image, so running `info` on a real ASR-10 OS disk tells us
whether the layout holds.

  block size        512 bytes
  DD disk           1600 blocks (80 tracks x 2 sides x 10 sectors)
  HD disk           3200 blocks (80 tracks x 2 sides x 20 sectors)
  block 1           device ID block, "ID" at offset 0x26, total blocks at 0x0E
  block 2           OS block, free-block count at 0x00, "OS" at 0x1C
  blocks 3-4        root directory, 39 entries of 26 bytes
  blocks 5..        FAT, 170 three-byte entries per block, "FB" at offset 510
                    entry value 0 = free, 1 = end of file, 2 = bad block

Directory entry (26 bytes, big-endian):
  0      type-specific byte
  1      file type (0 = empty slot)
  2-13   name, 12 chars, space padded
  14-15  size in blocks
  16-17  number of contiguous blocks from the start block
  18-21  start block
  22-25  type-specific (multi-file index etc.)

Commands:
  info     <img>                     signatures, size, free blocks
  ls       <img>                     root directory
  extract  <img> <entry> <out>       write a file's blocks to <out>
  replace  <img> <entry> <data> <out-img>
                                     copy <img> to <out-img> with entry's
                                     contents replaced by <data>
  hexdump  <img> <block>             dump one 512-byte block

<entry> is a directory slot number from `ls` or a file name.
"""

import argparse
import struct
import sys

BLOCK = 512
DIR_BLOCKS = (3, 4)
DIR_ENTRY = 26
DIR_ENTRIES = 39
FAT_START = 5
FAT_PER_BLOCK = 170
FAT_FREE, FAT_EOF, FAT_BAD = 0, 1, 2

# Best-effort names; unknown codes are shown as numbers.
FILE_TYPES = {
    1: "EPS OS",
    2: "directory",
    3: "instrument",
    4: "bank",
    5: "sequence",
    6: "song",
    7: "sysex",
    8: "parent dir",
    9: "macro",
}


class DiskError(Exception):
    pass


class Entry:
    def __init__(self, slot, raw):
        self.slot = slot
        self.raw = bytes(raw)
        self.type_byte = raw[0]
        self.file_type = raw[1]
        self.name = raw[2:14].decode("ascii", "replace").rstrip()
        self.size, self.contiguous = struct.unpack(">HH", raw[14:18])
        self.start = struct.unpack(">I", raw[18:22])[0]
        self.extra = raw[22:26]

    @property
    def type_name(self):
        return FILE_TYPES.get(self.file_type, f"type {self.file_type}")


class Disk:
    def __init__(self, data):
        self.data = bytearray(data)
        if len(self.data) % BLOCK:
            raise DiskError(f"image size {len(self.data)} is not a multiple of {BLOCK}")
        self.blocks = len(self.data) // BLOCK
        if self.blocks not in (1600, 3200):
            raise DiskError(f"{self.blocks} blocks; expected 1600 (DD) or 3200 (HD)")
        self.fat_blocks = -(-self.blocks // FAT_PER_BLOCK)

    @classmethod
    def load(cls, path):
        with open(path, "rb") as f:
            return cls(f.read())

    def save(self, path):
        with open(path, "wb") as f:
            f.write(self.data)

    def block(self, n):
        return self.data[n * BLOCK:(n + 1) * BLOCK]

    def problems(self):
        """Return a list of format assumptions this image violates."""
        out = []
        if self.block(1)[0x26:0x28] != b"ID":
            out.append('block 1: no "ID" signature at 0x26')
        if self.block(2)[0x1C:0x1E] != b"OS":
            out.append('block 2: no "OS" signature at 0x1C')
        for i in range(self.fat_blocks):
            if self.block(FAT_START + i)[510:512] != b"FB":
                out.append(f'FAT block {FAT_START + i}: no "FB" signature')
        id_total = struct.unpack(">I", self.block(1)[0x0E:0x12])[0]
        if id_total != self.blocks:
            out.append(f"block 1 says {id_total} blocks, image has {self.blocks}")
        if self.free_count() != self.count_free():
            out.append(f"block 2 free count {self.free_count()} != FAT free count {self.count_free()}")
        return out

    # FAT

    def fat_get(self, n):
        off = (FAT_START + n // FAT_PER_BLOCK) * BLOCK + (n % FAT_PER_BLOCK) * 3
        b = self.data[off:off + 3]
        return (b[0] << 16) | (b[1] << 8) | b[2]

    def fat_set(self, n, value):
        off = (FAT_START + n // FAT_PER_BLOCK) * BLOCK + (n % FAT_PER_BLOCK) * 3
        self.data[off:off + 3] = bytes(((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF))

    def chain(self, start):
        blocks, n = [], start
        while True:
            if not (0 < n < self.blocks) or n in blocks:
                raise DiskError(f"broken FAT chain at block {n} (from {start})")
            blocks.append(n)
            nxt = self.fat_get(n)
            if nxt == FAT_EOF:
                return blocks
            if nxt in (FAT_FREE, FAT_BAD):
                raise DiskError(f"FAT chain from {start} hits {'free' if nxt == FAT_FREE else 'bad'} block at {n}")
            n = nxt

    def count_free(self):
        return sum(1 for n in range(self.blocks) if self.fat_get(n) == FAT_FREE)

    def free_count(self):
        return struct.unpack(">I", self.block(2)[0:4])[0]

    def set_free_count(self, value):
        self.data[2 * BLOCK:2 * BLOCK + 4] = struct.pack(">I", value)

    # directory

    def entries(self):
        raw = self.block(DIR_BLOCKS[0]) + self.block(DIR_BLOCKS[1])
        return [Entry(i, raw[i * DIR_ENTRY:(i + 1) * DIR_ENTRY]) for i in range(DIR_ENTRIES)]

    def find(self, key):
        used = [e for e in self.entries() if e.file_type]
        if key.isdigit():
            for e in used:
                if e.slot == int(key):
                    return e
            raise DiskError(f"slot {key} is empty")
        matches = [e for e in used if e.name.upper() == key.upper()]
        if len(matches) != 1:
            raise DiskError(f"{len(matches)} entries named {key!r}")
        return matches[0]

    def write_entry(self, slot, raw):
        off = DIR_BLOCKS[0] * BLOCK + slot * DIR_ENTRY
        self.data[off:off + DIR_ENTRY] = raw

    def read_file(self, entry):
        blocks = self.chain(entry.start)
        if len(blocks) != entry.size:
            raise DiskError(f"{entry.name}: chain has {len(blocks)} blocks, entry says {entry.size}")
        return b"".join(self.block(n) for n in blocks)

    def replace_file(self, entry, payload):
        """Rewrite entry's data in place, reusing its blocks and growing if needed."""
        need = max(1, -(-len(payload) // BLOCK))
        old = self.chain(entry.start)
        for n in old:
            self.fat_set(n, FAT_FREE)
        # Prefer one contiguous run from the old start block; the boot ROM may
        # assume the OS is contiguous.
        run = list(range(entry.start, entry.start + need))
        if run[-1] >= self.blocks or any(self.fat_get(n) != FAT_FREE for n in run):
            run = self._find_run(need)
        for i, n in enumerate(run):
            self.fat_set(n, run[i + 1] if i + 1 < len(run) else FAT_EOF)
            chunk = payload[i * BLOCK:(i + 1) * BLOCK].ljust(BLOCK, b"\0")
            self.data[n * BLOCK:(n + 1) * BLOCK] = chunk
        raw = bytearray(entry.raw)
        raw[14:18] = struct.pack(">HH", need, need)
        raw[18:22] = struct.pack(">I", run[0])
        self.write_entry(entry.slot, raw)
        self.set_free_count(self.count_free())

    def _find_run(self, need):
        first_data = FAT_START + self.fat_blocks
        n = first_data
        while n + need <= self.blocks:
            bad = next((m for m in range(n, n + need) if self.fat_get(m) != FAT_FREE), None)
            if bad is None:
                return list(range(n, n + need))
            n = bad + 1
        raise DiskError(f"no run of {need} contiguous free blocks")


def hexdump(data, base=0):
    for i in range(0, len(data), 16):
        row = data[i:i + 16]
        text = "".join(chr(c) if 32 <= c < 127 else "." for c in row)
        print(f"{base + i:06x}  {row.hex(' '):<47}  {text}")


def cmd_info(a):
    d = Disk.load(a.img)
    print(f"{a.img}: {d.blocks} blocks ({'HD' if d.blocks == 3200 else 'DD'}), {d.fat_blocks} FAT blocks")
    print(f"free blocks: header {d.free_count()}, FAT {d.count_free()}")
    probs = d.problems()
    for p in probs:
        print("MISMATCH:", p)
    print("layout matches the documented Ensoniq format" if not probs else f"{len(probs)} mismatch(es)")
    return 1 if probs else 0


def cmd_ls(a):
    d = Disk.load(a.img)
    print("slot  type             name          blocks  contig  start  extra")
    for e in d.entries():
        if e.file_type:
            print(f"{e.slot:4}  {e.type_name:<15}  {e.name:<12}  {e.size:6}  {e.contiguous:6}  {e.start:5}  {e.extra.hex()}")
    return 0


def cmd_extract(a):
    d = Disk.load(a.img)
    e = d.find(a.entry)
    data = d.read_file(e)
    with open(a.out, "wb") as f:
        f.write(data)
    print(f"wrote {len(data)} bytes of {e.name!r} ({e.type_name}) to {a.out}")
    return 0


def cmd_replace(a):
    d = Disk.load(a.img)
    e = d.find(a.entry)
    with open(a.data, "rb") as f:
        payload = f.read()
    d.replace_file(e, payload)
    probs = d.problems()
    if probs:
        raise DiskError("result fails checks: " + "; ".join(probs))
    d.save(a.out)
    print(f"{a.out}: {e.name!r} now {len(payload)} bytes ({-(-len(payload) // BLOCK)} blocks)")
    return 0


def cmd_hexdump(a):
    d = Disk.load(a.img)
    hexdump(d.block(a.block), a.block * BLOCK)
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("info"); s.add_argument("img"); s.set_defaults(fn=cmd_info)
    s = sub.add_parser("ls"); s.add_argument("img"); s.set_defaults(fn=cmd_ls)
    s = sub.add_parser("extract"); s.add_argument("img"); s.add_argument("entry"); s.add_argument("out"); s.set_defaults(fn=cmd_extract)
    s = sub.add_parser("replace"); s.add_argument("img"); s.add_argument("entry"); s.add_argument("data"); s.add_argument("out"); s.set_defaults(fn=cmd_replace)
    s = sub.add_parser("hexdump"); s.add_argument("img"); s.add_argument("block", type=int); s.set_defaults(fn=cmd_hexdump)
    a = p.parse_args(argv)
    try:
        return a.fn(a)
    except (DiskError, OSError) as err:
        print("error:", err, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
