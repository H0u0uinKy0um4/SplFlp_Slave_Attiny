#!/usr/bin/env python3
"""Host checks for the OTA release manifest generator.

    python3 test/test_release_manifest.py                  # unit checks
    python3 test/test_release_manifest.py --verify firmware.json \
        --image .pio/build/ATtiny1616_OTA/firmware.hex     # CI publish gate
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import release_manifest as rm  # noqa: E402  (path prepared above)

BUILT_AT = "2026-11-26T18:00:00Z"
COMMIT = "a1b2c3d4e5f60718293a4b5c6d7e8f9012345678"
IMAGE = bytes(range(256)) * 4
FIELDS = {
    "schema", "kind", "channel", "version", "fw_bus_compat", "fw_release",
    "ota_protocol_version", "bootloader_version", "sha256", "source_commit",
    "built_at", "asset",
}


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check_manifest(manifest: dict, record: dict, release: str,
                   channel: str, source_commit: str, image: bytes) -> None:
    check(set(manifest) == FIELDS, f"manifest fields: {sorted(manifest)}")
    check(manifest["schema"] == 1, "schema")
    check(manifest["kind"] == "slave-app", "kind")
    check(manifest["channel"] == channel, "channel")
    check(manifest["fw_bus_compat"] == record["protocol"]["fw_bus_compat"],
          "fw_bus_compat")
    check(manifest["fw_release"] == rm.release_counter(release), "fw_release")
    check(manifest["ota_protocol_version"] == record["protocol"]["bootloader_version"],
          "ota_protocol_version")
    check(manifest["bootloader_version"] == record["protocol"]["bootloader_version"],
          "bootloader_version")
    check(manifest["sha256"] == hashlib.sha256(image).hexdigest(), "sha256")
    check(manifest["source_commit"] == source_commit.lower(), "source_commit")
    datetime.fromisoformat(manifest["built_at"].replace("Z", "+00:00"))
    check(manifest["asset"] == "firmware.hex", "asset")
    if channel == "dev":
        expected = f"dev-{source_commit[:7]}"
    else:
        expected = rm.current_version(record, release)
    check(manifest["version"] == expected, f"version {manifest['version']} != {expected}")


def unit_tests() -> None:
    record = rm.load_record()
    release = rm.read_fw_release()
    check(rm.current_version(record, release) == "1.0.2",
          "composed stable version should be 1.0.2")

    dev = rm.build_manifest(IMAGE, "dev", COMMIT, record, release, BUILT_AT)
    check_manifest(dev, record, release, "dev", COMMIT, IMAGE)

    stable = rm.build_manifest(IMAGE, "stable", COMMIT, record, release,
                               BUILT_AT, "v1.0.2")
    check_manifest(stable, record, release, "stable", COMMIT, IMAGE)

    try:
        rm.build_manifest(IMAGE, "stable", COMMIT, record, release, BUILT_AT, "v9.9.9")
    except ValueError:
        pass
    else:
        raise AssertionError("a mismatched stable tag must be rejected")

    try:
        rm.build_manifest(IMAGE, "nightly", COMMIT, record, release, BUILT_AT)
    except ValueError:
        pass
    else:
        raise AssertionError("an unknown channel must be rejected")

    # The CLI writes the same manifest it builds in-process.
    with tempfile.TemporaryDirectory() as tmp:
        image_path = Path(tmp) / "firmware.hex"
        out_path = Path(tmp) / "firmware.json"
        image_path.write_bytes(IMAGE)
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "release_manifest.py"),
             "--image", str(image_path), "--out", str(out_path),
             "--channel", "dev", "--source-commit", COMMIT,
             "--built-at", BUILT_AT],
            capture_output=True, text=True, check=True)
        check("sha256" in result.stderr, "CLI summary on stderr")
        check_manifest(json.loads(out_path.read_text()), record, release,
                       "dev", COMMIT, IMAGE)

    print("OK: release manifest unit checks")


def verify(manifest_path: Path, image_path: Path) -> None:
    record = rm.load_record()
    release = rm.read_fw_release()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    try:
        image = image_path.read_bytes()
    except OSError as exc:
        raise SystemExit(f"error: cannot read {image_path}: {exc}") from exc
    source_commit = manifest.get("source_commit", "")
    check_manifest(manifest, record, release, manifest.get("channel", ""),
                   source_commit, image)
    check(len(source_commit) == 40, "source_commit must be a full 40-char SHA")
    check(manifest["asset"] == image_path.name, "asset name must match the uploaded file")
    print(f"OK: verified {manifest_path} against {image_path} "
          f"({manifest['channel']} {manifest['version']})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", type=Path, metavar="MANIFEST")
    parser.add_argument("--image", type=Path)
    args = parser.parse_args()
    if args.verify:
        if not args.image:
            parser.error("--verify requires --image")
        verify(args.verify, args.image)
    else:
        unit_tests()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
