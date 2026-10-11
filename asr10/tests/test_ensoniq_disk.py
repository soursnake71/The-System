"""Tests for tools/ensoniq_disk.py against a synthetic image built to the documented layout.

Run from asr10/: python3 -m unittest discover tests
"""

import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import ensoniq_disk as ed  # noqa: E402


def make_image(blocks=1600, files=()):
    """Build a blank image, then add (name, type, data) files contiguously."""
    d = ed.Disk(bytes(blocks * ed.BLOCK))
    data = d.data
    data[ed.BLOCK + 0x0E:ed.BLOCK + 0x12] = struct.pack(">I", blocks)
    data[ed.BLOCK + 0x26:ed.BLOCK + 0x28] = b"ID"
    data[2 * ed.BLOCK + 0x1C:2 * ed.BLOCK + 0x1E] = b"OS"
    for i in range(d.fat_blocks):
        off = (ed.FAT_START + i) * ed.BLOCK
        data[off + 510:off + 512] = b"FB"
    system = ed.FAT_START + d.fat_blocks
    for n in range(system):
        d.fat_set(n, ed.FAT_EOF)  # reserved blocks are never free
    nxt = system
    for slot, (name, ftype, payload) in enumerate(files):
        need = -(-len(payload) // ed.BLOCK)
        for i in range(need):
            d.fat_set(nxt + i, nxt + i + 1 if i + 1 < need else ed.FAT_EOF)
            data[(nxt + i) * ed.BLOCK:(nxt + i + 1) * ed.BLOCK] = payload[i * ed.BLOCK:(i + 1) * ed.BLOCK].ljust(ed.BLOCK, b"\0")
        raw = bytearray(ed.DIR_ENTRY)
        raw[1] = ftype
        raw[2:14] = name.ljust(12).encode()
        raw[14:22] = struct.pack(">HHI", need, need, nxt)
        d.write_entry(slot, raw)
        nxt += need
    d.set_free_count(d.count_free())
    return d


class DiskTests(unittest.TestCase):
    def test_blank_image_passes_checks(self):
        self.assertEqual(make_image().problems(), [])
        self.assertEqual(make_image(3200).problems(), [])

    def test_rejects_wrong_size(self):
        with self.assertRaises(ed.DiskError):
            ed.Disk(bytes(1000 * ed.BLOCK))

    def test_reports_missing_signature(self):
        d = make_image()
        d.data[ed.BLOCK + 0x26] = 0
        self.assertIn('block 1: no "ID" signature at 0x26', d.problems())

    def test_extract_round_trip(self):
        payload = bytes(range(256)) * 9  # 2304 bytes, 5 blocks
        d = make_image(files=[("ASR OS", 32, payload)])
        e = d.find("ASR OS")
        self.assertEqual(e.size, 5)
        self.assertEqual(d.read_file(e)[:len(payload)], payload)

    def test_replace_grows_and_keeps_disk_consistent(self):
        d = make_image(files=[("OS", 32, b"a" * 1024), ("SND", 3, b"b" * 512)])
        free_before = d.count_free()
        new = b"N" * 5000  # 10 blocks; old run is blocked by SND, so it moves
        d.replace_file(d.find("OS"), new)
        e = d.find("OS")
        self.assertEqual((e.size, e.contiguous), (10, 10))
        self.assertEqual(d.read_file(e)[:len(new)], new)
        self.assertEqual(d.read_file(d.find("SND")), b"b" * 512)
        self.assertEqual(d.count_free(), free_before - 8)
        self.assertEqual(d.problems(), [])

    def test_replace_shrinks_in_place(self):
        d = make_image(files=[("OS", 32, b"a" * 4096)])
        start = d.find("OS").start
        d.replace_file(d.find("OS"), b"z" * 100)
        e = d.find("OS")
        self.assertEqual((e.start, e.size), (start, 1))
        self.assertEqual(d.problems(), [])

    def test_leaked_chain_is_a_warning_and_count_is_adjusted(self):
        d = make_image(files=[("OS", 32, b"a" * 1024)])
        system = ed.FAT_START + d.fat_blocks
        leak = system + 10                         # a chain no file owns
        d.fat_set(leak, ed.FAT_EOF)
        self.assertEqual(d.problems(), [])
        self.assertEqual(len(d.warnings()), 1)
        before = d.free_count()
        d.replace_file(d.find("os"), b"z" * 3 * 512)  # 2 -> 3 blocks
        self.assertEqual(d.free_count(), before - 1)

    def test_find_os_by_type(self):
        d = make_image(files=[("SND", 3, b"b" * 512), ("ASR-10 OS", 32, b"o" * 512)])
        self.assertEqual(d.find("os").name, "ASR-10 OS")
        with self.assertRaises(ed.DiskError):
            make_image(files=[("SND", 3, b"b" * 512)]).find("os")

    def test_cli_replace(self):
        d = make_image(files=[("OS", 32, b"a" * 1024)])
        with tempfile.TemporaryDirectory() as tmp:
            src, data, out = (os.path.join(tmp, n) for n in ("in.img", "os.bin", "out.img"))
            d.save(src)
            with open(data, "wb") as f:
                f.write(b"hello")
            self.assertEqual(ed.main(["replace", src, "0", data, out]), 0)
            self.assertEqual(ed.main(["info", out]), 0)
            self.assertEqual(ed.Disk.load(out).read_file(ed.Disk.load(out).find("OS"))[:5], b"hello")


if __name__ == "__main__":
    unittest.main()
