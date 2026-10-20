"""Tests for tools/patch_os.py, on a stand-in file (the real OS stays in local/)."""

import hashlib
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import patch_os  # noqa: E402

LOCAL_OS = os.path.join(os.path.dirname(__file__), "..", "local", "os353.bin")


class PatchOsTests(unittest.TestCase):
    def fake(self):
        p = patch_os.PATCHES["skip-keyboard-tune"]
        data = bytearray(0x20000)
        data[p["offset"]:p["offset"] + 6] = p["expect"]
        return bytes(data)

    def test_refuses_other_files(self):
        with self.assertRaises(patch_os.PatchError):
            patch_os.apply(self.fake(), ["skip-keyboard-tune"])

    def test_refuses_unknown_patch(self):
        with self.assertRaises(patch_os.PatchError):
            patch_os.apply(self.fake(), ["nope"])

    def test_changes_one_byte_when_hash_matches(self):
        data = self.fake()
        saved = dict(patch_os.OS_SHA1)
        patch_os.OS_SHA1["3.53"] = hashlib.sha1(data).hexdigest()
        try:
            out = patch_os.apply(data, ["skip-keyboard-tune"])
        finally:
            patch_os.OS_SHA1.update(saved)
        diff = [i for i in range(len(data)) if data[i] != out[i]]
        self.assertEqual(diff, [0x1CB43])
        self.assertEqual(out[0x1CB43], 0)

    @unittest.skipUnless(os.path.exists(LOCAL_OS), "needs your OS 3.53 in local/os353.bin")
    def test_real_os(self):
        data = open(LOCAL_OS, "rb").read()
        out = patch_os.apply(data, ["skip-keyboard-tune"])
        self.assertEqual(sum(a != b for a, b in zip(data, out)), 1)


if __name__ == "__main__":
    unittest.main()
