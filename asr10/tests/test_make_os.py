"""Tests for tools/make_os.py, using a synthetic stock OS file with the 1.61 layout.

The emulation test runs build/system.bin on an emulated 68000 and is skipped
if Unicorn isn't installed or the OS hasn't been built (run `make` first).
"""

import os
import struct
import sys
import unittest

HERE = os.path.dirname(__file__)
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import make_os  # noqa: E402

SEGS = [(0x000E00, 0x39), (0xFF8000, 0x40), (0x009C00, 0x31), (0xFC6000, 0x01)]


ROM_HANDLER = 0xFFF882DA
OS_VECTORS = {4: 0xFFFC6000, 42: 0xFFFF88E8, 74: 0xFFFF8D92}  # as in 3.53


def fake_stock(segs=SEGS):
    header = bytearray(b"\xAA" * 0x400)  # stand-in for the stock header bytes
    for v in list(range(2, 48)) + list(range(64, 88)):
        header[4 * v:4 * v + 4] = struct.pack(">I", OS_VECTORS.get(v, ROM_HANDLER))
    header[0xC0:0xC4] = struct.pack(">I", 0x000114F4)  # data that looks like an OS address
    for i, (dest, blocks) in enumerate(segs):
        header[0x1C0 + 8 * i:0x1C8 + 8 * i] = struct.pack(">II", dest, blocks)
    header[0x1C0 + 8 * len(segs):0x1C8 + 8 * len(segs)] = bytes(8)
    return bytes(header) + b"\x55" * (sum(b for _, b in segs) * 512)


CODE = bytes.fromhex("46fc2700 60fe")  # move #$2700,sr; bra.s *


class MakeOsTests(unittest.TestCase):
    def test_keeps_size_header_and_table(self):
        stock = fake_stock()
        out = make_os.build(stock, CODE, 0xE00)
        self.assertEqual(len(out), len(stock))
        self.assertEqual(out[:8], stock[:8])               # vectors 0 and 1 untouched
        self.assertEqual(out[0x0C0:0x100], stock[0x0C0:0x100])  # data, not vectors
        self.assertEqual(out[0x160:0x400], stock[0x160:0x400])  # data and load table kept

    def test_only_os_vectors_point_at_entry(self):
        out = make_os.build(fake_stock(), CODE, 0xE00)
        for v in list(range(2, 48)) + list(range(64, 88)):
            want = 0xE00 if v in OS_VECTORS else ROM_HANDLER
            self.assertEqual(struct.unpack(">I", out[4 * v:4 * v + 4])[0], want, f"vector {v}")

    def test_code_then_slides(self):
        out = make_os.build(fake_stock(), CODE, 0xE00)
        self.assertEqual(out[0x400:0x400 + len(CODE)], CODE)
        seg1_end = 0x400 + 0x39 * 512
        self.assertEqual(out[seg1_end - 6:seg1_end], bytes.fromhex("4ef900000e00"))
        self.assertEqual(out[seg1_end:seg1_end + 4], b"\x4e\x71\x4e\x71")
        self.assertEqual(out[-6:], bytes.fromhex("4ef900000e00"))

    def test_rejects_bad_inputs(self):
        with self.assertRaises(make_os.OsError):
            make_os.build(fake_stock(), CODE, 0x1000)       # not a segment start
        with self.assertRaises(make_os.OsError):
            make_os.build(fake_stock()[:-512], CODE, 0xE00)  # table doesn't match size
        with self.assertRaises(make_os.OsError):
            make_os.build(fake_stock(), b"\0" * (0x39 * 512), 0xE00)  # code too big


class EmulationTests(unittest.TestCase):
    """Runs build/system.bin (the panel explorer and ROM dump) on an emulated 68000."""

    def setUp(self):
        try:
            import emulate
        except ImportError:
            self.skipTest("unicorn not installed")
        code_path = os.path.join(HERE, "..", "build", "system.bin")
        if not os.path.exists(code_path):
            self.skipTest("run make first")
        import random
        import tempfile
        code = open(code_path, "rb").read()
        code += b"\0" * (len(code) % 2)
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
            f.write(make_os.build(fake_stock(), code, 0xE00))
        self.addCleanup(os.unlink, f.name)
        self.os_file = f.name
        self.emulate = emulate
        self.rom = bytes(random.Random(5).getrandbits(8) for _ in range(0x40000))

    def test_starts_from_any_entry(self):
        for entry in (0xE00, 0x4000, 0xFF801E, 0x9C02, 0xFC6100):
            out, _ = self.emulate.run(self.os_file, entry, 1_000_000, rom=self.rom)
            self.assertTrue(bytes(out["panel"]).startswith(b" A"), hex(entry))

    def test_shows_panel_bytes_and_dumps_rom(self):
        import syx2rom
        out, _ = self.emulate.run(self.os_file, 0xE00, 6_000_000, rom=self.rom,
                                  panel_input=bytes([0xC5]))
        # MIDI port report (transmitter off, then on), first message sent, a key
        self.assertEqual(bytes(out["panel"]), b" A 00 04 04 TX KEYS C5")
        good, _ = syx2rom.parse(bytes(out["midi"]))
        self.assertGreater(len(good), 100)
        self.assertIn(0xF80000, good)
        for addr, chunk in good.items():
            self.assertEqual(chunk, self.rom[addr - 0xF80000:addr - 0xF80000 + 256])

    def test_waits_for_panel_replies(self):
        out, _ = self.emulate.run(self.os_file, 0xE00, 3_000_000, rom=self.rom, panel_replies=True)
        self.assertTrue(bytes(out["panel"]).startswith(b" A 00 04 04"))
        self.assertEqual(out["replies_read"], len(out["panel"]))


if __name__ == "__main__":
    unittest.main()
