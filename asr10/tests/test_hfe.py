"""Tests for tools/hfe.py: .img -> .hfe -> .img must give back the same bytes.

The encoder was checked against Greaseweazle's own decoder
(`gw convert --format=ensoniq.1600`), which read all 3200 sectors back
unchanged; that check isn't run here because it needs Greaseweazle.
"""

import os
import random
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import hfe  # noqa: E402


class HfeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rng = random.Random(1)
        cls.img = bytes(rng.getrandbits(8) for _ in range(80 * 2 * 20 * 512))
        cls.hfe = hfe.img_to_hfe(cls.img)

    def test_header(self):
        self.assertEqual(self.hfe[:8], b"HXCPICFE")
        rev, cyls, sides, enc, rate, rpm = struct.unpack_from("<BBBBHH", self.hfe, 8)
        self.assertEqual((rev, cyls, sides, enc, rate, rpm), (0, 80, 2, 0, 500, 0))
        self.assertEqual(self.hfe[16], 1)                              # interface: IBM PC HD
        self.assertEqual(struct.unpack_from("<HH", self.hfe, 512), (2, 50024))  # as on the Gotek

    def test_round_trip_hd(self):
        self.assertEqual(hfe.hfe_to_img(self.hfe), self.img)

    def test_crc_catches_damage(self):
        bad = bytearray(self.hfe)
        bad[1024 + 5000] ^= 0x02  # flip a data cell (odd cells carry data) in a sector
        with self.assertRaises(hfe.HfeError):
            hfe.hfe_to_img(bytes(bad))

    def test_round_trip_dd(self):
        img = bytes(range(256)) * (80 * 2 * 10 * 2)
        out = hfe.img_to_hfe(img)
        self.assertEqual(struct.unpack_from("<HH", out, 512), (2, 25088))
        self.assertEqual(hfe.hfe_to_img(out), img)

    def test_rejects_odd_size(self):
        with self.assertRaises(hfe.HfeError):
            hfe.img_to_hfe(bytes(1000))

    def test_crc16_reference(self):
        # IDAM for c0 h0 r1 n2, whose CRC is well known: 0xCA6F
        self.assertEqual(hfe.crc16(bytes([0xA1, 0xA1, 0xA1, 0xFE, 0, 0, 1, 2])), 0xCA6F)


if __name__ == "__main__":
    unittest.main()
