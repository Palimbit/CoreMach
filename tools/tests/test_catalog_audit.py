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

    def test_implementation_document_rejects_paths(self) -> None:
        self.assertIsNone(
            catalog_audit.IMPLEMENTATION_DOCUMENT.fullmatch(
                "../../private/machine-implementation.md"
            )
        )
        self.assertIsNotNone(
            catalog_audit.IMPLEMENTATION_DOCUMENT.fullmatch(
                "machine-implementation.md"
            )
        )

    def test_creation_rejects_unavailable_default(self) -> None:
        errors: list[str] = []
        product = {
            "creation": {
                "fields": [{
                    "id": "memory",
                    "default": "4mb",
                    "choices": [{"id": "4mb", "status": "unavailable"}],
                }],
                "configuration": [],
            }
        }

        catalog_audit.validate_creation("example", product, errors)

        self.assertTrue(any("must not reference an unavailable choice" in error for error in errors))

    def test_creation_accepts_valid_field(self) -> None:
        errors: list[str] = []
        product = {
            "creation": {
                "fields": [{
                    "id": "memory",
                    "default": "512",
                    "choices": [{
                        "id": "512",
                        "status": "validated",
                        "set": [{"section": "Machine", "key": "mem_size", "value": 512}],
                    }],
                }],
                "configuration": [{"section": "Machine", "values": {"mem_size": 512}}],
            }
        }

        catalog_audit.validate_creation("example", product, errors)

        self.assertEqual([], errors)

    def test_media_requires_explicit_kind_and_resource(self) -> None:
        errors: list[str] = []
        catalog_audit.validate_media(
            "example", {"media": {"kind": "image", "resource": "example.jpg"}}, errors
        )

        self.assertEqual(3, len(errors))

    def test_media_accepts_declared_concept_illustration(self) -> None:
        errors: list[str] = []
        catalog_audit.validate_media(
            "example",
            {"media": {
                "kind": "concept_illustration",
                "resource": ":/blumach/catalog/images/example.jpg",
                "label_key": "media.kind.concept_illustration",
            }},
            errors,
        )

        self.assertEqual([], errors)


if __name__ == "__main__":
    unittest.main()
