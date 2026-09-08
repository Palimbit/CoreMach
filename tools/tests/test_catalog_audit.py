#!/usr/bin/env python3

from pathlib import Path
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import catalog_audit  # noqa: E402


class CatalogAuditTests(unittest.TestCase):
    def test_duplicate_json_key_is_rejected(self) -> None:
        with self.assertRaisesRegex(catalog_audit.DuplicateKeyError, "duplicate JSON key 'same'"):
            catalog_audit._unique_object([("same", "first"), ("same", "second")])

    def test_locale_placeholder_mismatch_is_rejected(self) -> None:
        locales = {
            "en": {"message": "Machine %1 has %2 MB"},
            "es": {"message": "La máquina %1 tiene memoria"},
        }
        errors: list[str] = []

        catalog_audit.validate_translation_sets(locales, {"message"}, errors)

        self.assertTrue(any("placeholders for 'message'" in error for error in errors))

    def test_locale_key_parity_is_required(self) -> None:
        locales = {
            "en": {"first": "First", "second": "Second"},
            "fr": {"first": "Premier"},
        }
        errors: list[str] = []

        catalog_audit.validate_translation_sets(locales, set(), errors)

        self.assertIn("fr.json: missing English key 'second'", errors)


if __name__ == "__main__":
    unittest.main()
