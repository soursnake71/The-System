"""Runs our OS through the real boot ROM in emu/asr10.py.

Needs Unicorn, local/asr10rom.bin (your own ROM dump) and build/system.img
(`make disk`); skipped otherwise.
"""

import os
import sys
import unittest

HERE = os.path.dirname(__file__)
ROOT = os.path.join(HERE, "..")
ROM = os.path.join(ROOT, "local", "asr10rom.bin")
DISK = os.path.join(ROOT, "build", "system.img")


class EngineTests(unittest.TestCase):
    def test_boots_tunes_and_shows_a_button(self):
        try:
            sys.path.insert(0, os.path.join(ROOT, "emu"))
            import asr10
        except ImportError:
            self.skipTest("unicorn not installed")
        if not (os.path.exists(ROM) and os.path.exists(DISK)):
            self.skipTest("needs local/asr10rom.bin and build/system.img")
        m = asr10.Machine(open(ROM, "rb").read(), log=lambda s: None, disk=open(DISK, "rb").read())
        m.run(35_000_000)
        self.assertIn("TUNE C9 C9 C9 C9", m.panel.render())
        m.panel.press(2)
        m.panel.release(2)
        m.run(10_000_000)
        self.assertIn("BTN 82", m.panel.render())


if __name__ == "__main__":
    unittest.main()
