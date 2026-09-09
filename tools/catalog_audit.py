#!/usr/bin/env python3
"""Validate the BluMach historical catalogue and its locale packs."""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys
from typing import Any, Iterable
from urllib.parse import urlparse
import xml.etree.ElementTree as ET


CATALOG_SCHEMA = "blumach-catalog-v3"
ENTITY_ID = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
FACET_ID = re.compile(r"^[a-z0-9]+(?:_[a-z0-9]+)*$")
TRANSLATION_KEY = re.compile(r"^[a-z0-9][a-z0-9_.-]*$")
PLACEHOLDER = re.compile(r"%(?:L?\d+|n)")
IMPLEMENTATION_DOCUMENT = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*-implementation\.md$")
REQUIRED_COLLECTIONS = (
    "filter_facets",
    "manufacturers",
    "families",
    "platforms",
    "products",
)
VALID_STATUSES = {
    "validated",
    "partial",
    "experimental",
    "research",
    "not_bootable",
}


class DuplicateKeyError(ValueError):
    pass


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateKeyError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def load_json(path: Path, errors: list[str]) -> Any | None:
    try:
        with path.open("r", encoding="utf-8") as stream:
            return json.load(stream, object_pairs_hook=_unique_object)
    except FileNotFoundError:
        errors.append(f"{path}: file not found")
    except (json.JSONDecodeError, UnicodeDecodeError, DuplicateKeyError) as exc:
        errors.append(f"{path}: {exc}")
    return None


def require_unique_ids(
    values: Any,
    collection: str,
    errors: list[str],
    pattern: re.Pattern[str] = ENTITY_ID,
    style: str = "lowercase hyphenated",
) -> dict[str, dict[str, Any]]:
    if not isinstance(values, list):
        errors.append(f"catalog.json: {collection!r} must be an array")
        return {}

    indexed: dict[str, dict[str, Any]] = {}
    for position, value in enumerate(values):
        location = f"catalog.json: {collection}[{position}]"
        if not isinstance(value, dict):
            errors.append(f"{location} must be an object")
            continue
        entity_id = value.get("id")
        if not isinstance(entity_id, str) or not pattern.fullmatch(entity_id):
            errors.append(f"{location}.id must be a {style} identifier")
            continue
        if entity_id in indexed:
            errors.append(f"catalog.json: duplicate {collection} id {entity_id!r}")
            continue
        indexed[entity_id] = value
    return indexed


def translation_references(value: Any) -> Iterable[tuple[str, str]]:
    def walk(item: Any, path: str) -> Iterable[tuple[str, str]]:
        if isinstance(item, list):
            for position, child in enumerate(item):
                yield from walk(child, f"{path}[{position}]")
        elif isinstance(item, dict):
            for key, child in item.items():
                child_path = f"{path}.{key}" if path else key
                if key.endswith("_key") and isinstance(child, str) and child:
                    yield child_path, child
                elif key.endswith("_keys") and isinstance(child, list):
                    for position, translation_key in enumerate(child):
                        if isinstance(translation_key, str) and translation_key:
                            yield f"{child_path}[{position}]", translation_key
                yield from walk(child, child_path)

    return walk(value, "catalog.json")


def validate_facets(
    catalog: dict[str, Any],
    products: dict[str, dict[str, Any]],
    errors: list[str],
) -> None:
    facets = require_unique_ids(
        catalog.get("filter_facets"),
        "filter_facets",
        errors,
        FACET_ID,
        "lowercase snake_case",
    )
    allowed: dict[str, set[str]] = {}
    for facet_id, facet in facets.items():
        values = require_unique_ids(
            facet.get("values"),
            f"filter_facets[{facet_id!r}].values",
            errors,
            FACET_ID,
            "lowercase snake_case",
        )
        allowed[facet_id] = set(values)

    def check_values(owner: str, configured: Any) -> None:
        if not isinstance(configured, dict):
            errors.append(f"catalog.json: {owner}.facets must be an object")
            return
        for facet_id, values in configured.items():
            if facet_id not in allowed:
                errors.append(f"catalog.json: {owner} uses unknown facet {facet_id!r}")
                continue
            if not isinstance(values, list) or not values:
                errors.append(f"catalog.json: {owner}.facets.{facet_id} must be a non-empty array")
                continue
            for value in values:
                if value not in allowed[facet_id]:
                    errors.append(
                        f"catalog.json: {owner} uses unknown {facet_id} value {value!r}"
                    )

    for product_id, product in products.items():
        check_values(f"product {product_id!r}", product.get("facets", {}))
        profiles = product.get("filter_profiles", [])
        if not isinstance(profiles, list):
            errors.append(f"catalog.json: product {product_id!r}.filter_profiles must be an array")
            continue
        profile_ids: set[str] = set()
        for position, profile in enumerate(profiles):
            owner = f"product {product_id!r} filter_profiles[{position}]"
            if not isinstance(profile, dict):
                errors.append(f"catalog.json: {owner} must be an object")
                continue
            profile_id = profile.get("id")
            if not isinstance(profile_id, str) or not ENTITY_ID.fullmatch(profile_id):
                errors.append(f"catalog.json: {owner}.id must be a lowercase hyphenated identifier")
            elif profile_id in profile_ids:
                errors.append(f"catalog.json: product {product_id!r} has duplicate filter profile {profile_id!r}")
            else:
                profile_ids.add(profile_id)
            check_values(owner, profile.get("facets", {}))


def validate_catalog(catalog: Any, errors: list[str]) -> set[str]:
    if not isinstance(catalog, dict):
        errors.append("catalog.json: root must be an object")
        return set()
    if catalog.get("schema") != CATALOG_SCHEMA:
        errors.append(
            f"catalog.json: schema must be {CATALOG_SCHEMA!r}, got {catalog.get('schema')!r}"
        )
    for collection in REQUIRED_COLLECTIONS:
        if collection not in catalog:
            errors.append(f"catalog.json: missing required collection {collection!r}")

    manufacturers = require_unique_ids(catalog.get("manufacturers"), "manufacturers", errors)
    families = require_unique_ids(catalog.get("families"), "families", errors)
    platforms = require_unique_ids(catalog.get("platforms"), "platforms", errors)
    products = require_unique_ids(catalog.get("products"), "products", errors)

    for family_id, family in families.items():
        manufacturer_id = family.get("manufacturer_id")
        if manufacturer_id not in manufacturers:
            errors.append(
                f"catalog.json: family {family_id!r} references unknown manufacturer {manufacturer_id!r}"
            )
        parent_id = family.get("parent_family_id")
        if parent_id and parent_id not in families:
            errors.append(
                f"catalog.json: family {family_id!r} references unknown parent family {parent_id!r}"
            )
        elif parent_id and families[parent_id].get("manufacturer_id") != manufacturer_id:
            errors.append(
                f"catalog.json: family {family_id!r} and parent {parent_id!r} have different manufacturers"
            )

    for product_id, product in products.items():
        manufacturer_id = product.get("manufacturer_id")
        family_id = product.get("family_id")
        platform_id = product.get("platform_id")
        status = product.get("status")
        if manufacturer_id not in manufacturers:
            errors.append(
                f"catalog.json: product {product_id!r} references unknown manufacturer {manufacturer_id!r}"
            )
        if family_id not in families:
            errors.append(
                f"catalog.json: product {product_id!r} references unknown family {family_id!r}"
            )
        elif families[family_id].get("manufacturer_id") != manufacturer_id:
            errors.append(
                f"catalog.json: product {product_id!r} and family {family_id!r} have different manufacturers"
            )
        if platform_id and platform_id not in platforms:
            errors.append(
                f"catalog.json: product {product_id!r} references unknown platform {platform_id!r}"
            )
        elif not platform_id and status not in {"research", "not_bootable"}:
            errors.append(
                f"catalog.json: product {product_id!r} without a platform must be research or not_bootable"
            )
        if status not in VALID_STATUSES:
            errors.append(f"catalog.json: product {product_id!r} has invalid status {status!r}")
        implementation = product.get("implementation")
        if implementation is not None:
            if not isinstance(implementation, dict):
                errors.append(
                    f"catalog.json: product {product_id!r}.implementation must be an object"
                )
            else:
                document = implementation.get("document")
                language = implementation.get("language")
                if not isinstance(document, str) or not IMPLEMENTATION_DOCUMENT.fullmatch(document):
                    errors.append(
                        f"catalog.json: product {product_id!r}.implementation.document "
                        "must be a simple *-implementation.md filename"
                    )
                elif document != f"{product_id}-implementation.md":
                    errors.append(
                        f"catalog.json: product {product_id!r}.implementation.document "
                        f"must be {product_id + '-implementation.md'!r}"
                    )
                if not isinstance(language, str) or not re.fullmatch(r"[a-z]{2}", language):
                    errors.append(
                        f"catalog.json: product {product_id!r}.implementation.language "
                        "must be a two-letter lowercase language code"
                    )

    validate_facets(catalog, products, errors)

    references: set[str] = set()
    for location, key in translation_references(catalog):
        if not TRANSLATION_KEY.fullmatch(key):
            errors.append(f"{location}: invalid translation key {key!r}")
        references.add(key)
    references.update(f"status.{product.get('status')}" for product in products.values())
    return references


def validate_locales(
    locale_dir: Path, references: set[str], errors: list[str]
) -> list[str]:
    locale_files = sorted(locale_dir.glob("*.json"))
    if not locale_files:
        errors.append(f"{locale_dir}: no locale files found")
        return []

    locales: dict[str, dict[str, str]] = {}
    for path in locale_files:
        document = load_json(path, errors)
        if document is None:
            continue
        if not isinstance(document, dict):
            errors.append(f"{path}: root must be an object")
            continue
        for key, value in document.items():
            if not TRANSLATION_KEY.fullmatch(key):
                errors.append(f"{path}: invalid translation key {key!r}")
            if not isinstance(value, str) or not value.strip():
                errors.append(f"{path}: translation {key!r} must be a non-empty string")
        locales[path.stem] = document

    validate_translation_sets(locales, references, errors, str(locale_dir))
    return list(locales)


def validate_translation_sets(
    locales: dict[str, dict[str, str]],
    references: set[str],
    errors: list[str],
    source: str = "locales",
) -> None:
    english = locales.get("en")
    if english is None:
        errors.append(f"{source}: en.json is required as the fallback locale")
        return

    missing_references = sorted(references - set(english))
    for key in missing_references:
        errors.append(f"en.json: missing referenced translation {key!r}")

    english_keys = set(english)
    for locale, translations in locales.items():
        if locale == "en":
            continue
        locale_keys = set(translations)
        for key in sorted(english_keys - locale_keys):
            errors.append(f"{locale}.json: missing English key {key!r}")
        for key in sorted(locale_keys - english_keys):
            errors.append(f"{locale}.json: key {key!r} is not present in en.json")
        for key in sorted(english_keys & locale_keys):
            expected = Counter(PLACEHOLDER.findall(english[key]))
            actual = Counter(PLACEHOLDER.findall(translations[key]))
            if expected != actual:
                errors.append(
                    f"{locale}.json: placeholders for {key!r} are {dict(actual)}, expected {dict(expected)}"
                )


def validate_urls(value: Any, errors: list[str], path: str = "catalog.json") -> None:
    if isinstance(value, list):
        for position, child in enumerate(value):
            validate_urls(child, errors, f"{path}[{position}]")
    elif isinstance(value, dict):
        for key, child in value.items():
            child_path = f"{path}.{key}"
            if key.endswith("url"):
                if not isinstance(child, str) or urlparse(child).scheme not in {"http", "https"}:
                    errors.append(f"{child_path}: URL must use http or https")
            validate_urls(child, errors, child_path)


def validate_resources(catalog_dir: Path, locales: list[str], errors: list[str]) -> None:
    resource_file = catalog_dir.parents[1] / "qt_resources.qrc"
    try:
        root = ET.parse(resource_file).getroot()
    except (FileNotFoundError, ET.ParseError) as exc:
        errors.append(f"{resource_file}: {exc}")
        return
    resources = next(
        (item for item in root.findall("qresource") if item.get("prefix") == "/blumach/catalog"),
        None,
    )
    if resources is None:
        errors.append(f"{resource_file}: missing /blumach/catalog resource group")
        return
    aliases = {item.get("alias", item.text or "") for item in resources.findall("file")}
    required = {"catalog.json", *(f"locales/{locale}.json" for locale in locales)}
    for alias in sorted(required - aliases):
        errors.append(f"{resource_file}: missing resource alias {alias!r}")


def audit(catalog_dir: Path) -> list[str]:
    errors: list[str] = []
    catalog = load_json(catalog_dir / "catalog.json", errors)
    if catalog is None:
        return errors
    references = validate_catalog(catalog, errors)
    validate_urls(catalog, errors)
    locales = validate_locales(catalog_dir / "locales", references, errors)
    validate_resources(catalog_dir, locales, errors)
    return errors


def main() -> int:
    default_catalog = Path(__file__).resolve().parents[1] / "src" / "qt" / "catalog"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "catalog_dir",
        nargs="?",
        type=Path,
        default=default_catalog,
        help="catalog directory (default: src/qt/catalog)",
    )
    args = parser.parse_args()
    catalog_dir = args.catalog_dir.resolve()
    errors = audit(catalog_dir)
    if errors:
        print(f"BluMach catalogue audit failed with {len(errors)} error(s):", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(f"BluMach catalogue audit passed: {catalog_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
