#!/usr/bin/env python3

from pathlib import Path
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import catalog_builder  # noqa: E402


class CatalogBuilderTests(unittest.TestCase):
    def test_bundle_locale_key_mismatch_is_rejected(self) -> None:
        locales = {
            "en": {"first": "First", "second": "Second"},
            "es": {"first": "Primero"},
        }

        with self.assertRaisesRegex(ValueError, "es.json key mismatch"):
            catalog_builder.validate_bundle_translations(locales, "test bundle")

    def test_duplicate_translation_ownership_is_rejected(self) -> None:
        target = {"en": {"product.first.summary": "First"}}
        incoming = {"en": {"product.first.summary": "Duplicate"}}

        with self.assertRaisesRegex(ValueError, "duplicate en translation key"):
            catalog_builder.merge_translations(target, incoming, Path("second"))

    def test_qrc_preserves_runtime_aliases(self) -> None:
        qrc = catalog_builder.render_qrc(
            ["en", "es"], ["toshiba-t5100-implementation.md"]
        )

        self.assertIn('prefix="/blumach/catalog"', qrc)
        self.assertIn('alias="catalog.json"', qrc)
        self.assertIn('alias="locales/es.json"', qrc)
        self.assertIn(
            'alias="documents/toshiba-t5100-implementation.md"', qrc
        )

    def test_implementation_documents_are_deduplicated(self) -> None:
        catalog = {
            "products": [
                {"implementation": {"document": "machine-implementation.md"}},
                {"implementation": {"document": "machine-implementation.md"}},
                {},
            ]
        }

        self.assertEqual(
            catalog_builder.implementation_documents(catalog),
            ["machine-implementation.md"],
        )

    def test_generated_resource_is_initialized_by_qt_main(self) -> None:
        repository = Path(__file__).resolve().parents[2]
        qt_main = (repository / "src" / "qt" / "qt_main.cpp").read_text(encoding="utf-8")

        self.assertIn("Q_INIT_RESOURCE(blumach_catalog);", qt_main)


if __name__ == "__main__":
    unittest.main()
