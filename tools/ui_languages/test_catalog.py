"""Jarvis-HOOK: reject broken translations and incomplete F8 catalog coverage."""
import tempfile
import unittest
from pathlib import Path

import catalog
import inventory


class CatalogTests(unittest.TestCase):
    def invalid(self, rows):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.tsv").write_text(rows, encoding="utf-8")
            with self.assertRaises(ValueError):
                catalog.load_rows(path)

    def test_rejects_duplicate_keys(self):
        row = "\t".join(["Example"] * 9) + "\n"
        self.invalid(row + row)

    def test_rejects_missing_translation(self):
        self.invalid("\t".join(["Example"] * 8) + "\n")

    def test_rejects_format_type_changes(self):
        self.invalid("\t".join(["ID %u", "ID %s"] + ["ID %u"] * 7) + "\n")

    def test_rejects_write_through_formats(self):
        self.invalid("\t".join(["Bad %n"] * 9) + "\n")

    def test_rejects_space_flag_write_through_formats(self):
        self.invalid("\t".join(["Bad % n"] * 9) + "\n")

    def test_rejects_control_characters(self):
        self.invalid("\t".join(["Bad\x01value"] * 9) + "\n")

    def test_rejects_oversized_utf8(self):
        self.invalid("\t".join(["語" * 700] * 9) + "\n")

    def test_formats_preserve_argument_order_and_width(self):
        self.invalid("\t".join(["%s: %04X", "%04X: %s"] + ["%s: %04X"] * 7) + "\n")

    def test_all_native_flag_labels_and_help_are_present(self):
        keys = {row[0] for row in catalog.load_rows()}
        required = catalog.native_flag_strings()
        self.assertGreaterEqual(len(required), 170)
        self.assertFalse(required - keys, "Missing F8 translations: " + repr(sorted(required - keys)))

    def test_generated_catalog_matches_authored_sources(self):
        self.assertEqual(catalog.OUTPUT.read_text(encoding="utf-8"), catalog.generate(catalog.load_rows()))

    def test_native_settings_literals_are_complete(self):
        non_display = {" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'()./", "vanguard.%s"}
        required = set(inventory.ui_inventory()) - non_display
        missing = required - {row[0] for row in catalog.load_rows()}
        self.assertFalse(missing, f"{len(missing)} missing settings strings: {sorted(missing)!r}")

    def test_supplementary_unicode_generates_valid_cpp_escape(self):
        generated = catalog.generate([["Example \U0001f600"] * 9])
        self.assertIn(r"\U0001f600", generated)
        self.assertNotIn(r"\ud83d", generated)


if __name__ == "__main__":
    unittest.main()
