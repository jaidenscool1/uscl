import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("validate", Path(__file__).resolve().parents[1] / "scripts/validate.py")
validate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validate)

class SfoTests(unittest.TestCase):
    def test_reject_header_and_bounds(self):
        for data in (b"", b"\0PSF", b"\0PSF" + struct.pack("<4I", 0x101, 0xffffffff, 4, 0xffffffff)):
            with self.assertRaises(ValueError):
                validate.sfo_entries(data)

    def test_packaged_identity(self):
        sfo = Path(__file__).resolve().parents[1] / "build/stage/sce_sys/param.sfo"
        if not sfo.exists():
            self.skipTest("Build a package first")
        data = sfo.read_bytes()
        result = validate.sfo_entries(data)
        self.assertEqual(result["TITLE_ID"], "USCL00001")
        self.assertEqual(result["CATEGORY"], "gd")
        self.assertEqual(result["SYSTEM_VER"], 0)
        for size in (0, 19, 20, len(data) - 1):
            with self.assertRaises(ValueError):
                validate.sfo_entries(data[:size])

if __name__ == "__main__":
    unittest.main()
