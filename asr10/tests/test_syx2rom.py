"""Tests for tools/syx2rom.py."""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import syx2rom  # noqa: E402


def message(addr, data):
    nib = bytearray()
    for b in data:
        nib += bytes([b >> 4, b & 0x0F])
    a = bytes([(addr >> 21) & 0x7F, (addr >> 14) & 0x7F, (addr >> 7) & 0x7F, addr & 0x7F])
    return b"\xf0\x7dASR" + a + bytes(nib) + bytes([sum(nib) & 0x7F]) + b"\xf7"


class Syx2RomTests(unittest.TestCase):
    def test_parses_and_skips_damage(self):
        good = message(0xF80100, bytes(range(256)))
        bad = bytearray(message(0xF80200, bytes(256)))
        bad[20] ^= 1  # breaks the sum
        cut = message(0xF80300, bytes(256))[:100]
        chunks, n_bad = syx2rom.parse(cut + good + bytes(bad))
        self.assertEqual(chunks, {0xF80100: bytes(range(256))})
        self.assertEqual(n_bad, 2)


if __name__ == "__main__":
    unittest.main()
