#!/usr/bin/env python3

from pathlib import Path
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import catalog_audit  # noqa: E402


class CatalogAuditTests(unittest.TestCase):
    def test_advanced_machine_field_requires_boolean(self) -> None:
        product = {"creation": {"fields": [{
            "id": "jumpers", "default": "auto", "portable_advanced": "yes",
            "choices": [{"id": "auto", "status": "documented"}],
        }]}}
        errors: list[str] = []
        catalog_audit.validate_creation("example", product, errors)
        self.assertTrue(any("portable_advanced must be a boolean" in error
                            for error in errors))

    def test_generated_disk_must_be_an_explicit_required_resource(self) -> None:
        errors: list[str] = []
        catalog_audit.validate_creation("example", {
            "portable_resources": [
                {"role": "hard-disk-0", "folder": "example/disks", "known": []}
            ],
            "creation": {"fields": [{
                "id": "commercial", "default": "with-disk", "choices": [{
                    "id": "with-disk", "status": "documented",
                    "portable_require_assets": ["hard-disk-0"],
                    "portable_generate_assets": ["hard-disk-0"],
                }]
            }]}
        }, errors)
        self.assertEqual([], errors)
        catalog_audit.validate_creation("example", {
            "creation": {"fields": [{
                "id": "commercial", "default": "with-disk", "choices": [{
                    "id": "with-disk", "status": "documented",
                    "portable_generate_assets": ["hard-disk-0"],
                }]
            }]}
        }, errors)
        self.assertTrue(any("required resource role" in error for error in errors))

    def test_expansion_slots_are_unique_and_explicitly_modelled(self) -> None:
        errors: list[str] = []
        catalog_audit.validate_expansion_slots("example", {
            "expansion_slots": [
                {"id": "isa-1", "bus": "isa8", "length": "full", "modelled": False},
                {"id": "isa-1", "bus": "isa8", "length": "full", "modelled": "no"},
            ]
        }, errors)
        self.assertTrue(any("duplicate slot" in error for error in errors))
        self.assertTrue(any("modelled must be a boolean" in error for error in errors))

    def test_portable_resource_metadata_rejects_unsafe_or_invalid_entries(self) -> None:
        errors: list[str] = []
        catalog_audit.validate_portable_resources("example", {
            "portable_resources": [
                {"role": "firmware", "folder": "../private", "known": [
                    {"name": "BIOS", "size": 32768, "sha256": "bad", "preferred": True}
                ]},
                {"role": "firmware", "folder": "maker/model", "known": []},
            ]
        }, errors)
        self.assertTrue(any("safe relative directory" in error for error in errors))
        self.assertTrue(any("sha256" in error for error in errors))
        self.assertTrue(any("duplicates role" in error for error in errors))

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

    def test_portable_adapter_ids_are_unique_and_hyphenated(self) -> None:
        errors: list[str] = []
        catalog_audit.validate_portable_adapters(
            {
                "first": {"portable_adapter_id": "machine-a"},
                "second": {"portable_adapter_id": "machine-a"},
                "third": {"portable_adapter_id": "legacy_name"},
            },
            errors,
        )

        self.assertTrue(any("duplicates platform 'first'" in error for error in errors))
        self.assertTrue(any("lowercase hyphenated" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
