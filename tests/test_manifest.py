import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("gen_manifest", ROOT / "scripts/gen_manifest.py")
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)


class ManifestTest(unittest.TestCase):
    def test_versioned_catalog(self):
        catalog = json.loads((ROOT / "manifest/manifest.json").read_text(encoding="utf-8"))
        entries = catalog["entries"]
        self.assertEqual(len(entries), len({entry["id"] for entry in entries}))
        self.assertEqual(catalog["total_themes"] + catalog["total_avatars"], len(entries))
        self.assertGreaterEqual(catalog["total_themes"], 850)
        for entry in entries:
            if entry["type"] == "theme":
                self.assertEqual(bool(entry["pkg_stub_url"]), bool(entry["pkg_payload_url"]))

    def test_only_pairs_and_deduplicates_identical_files(self):
        def record(size, digest):
            return {"size": str(size), "sha1": digest}

        files = {
            "A_1.pkg": record(64, "stub"),
            "A_2.pkg": record(4096, "body"),
            "A.jpg": record(2000, "image"),
            "B_1.pkg": record(64, "stub"),
            "B_2.pkg": record(4096, "body"),
            "C_1.pkg": record(64, "other"),
            "standalone.pkg": record(123, "single"),
        }
        entries, duplicates = generator.build_catalog({"entries": []}, {}, files)
        self.assertEqual((len(entries), duplicates), (1, 1))
        self.assertEqual(entries[0]["stub_size"], 64)
        self.assertEqual(entries[0]["payload_size"], 4096)
        self.assertTrue(entries[0]["preview_url"].endswith("/A.jpg"))


if __name__ == "__main__":
    unittest.main()
