#!/usr/bin/env python3
"""Generate ``firmware.json`` for a published slave OTA application image.

Every descriptive field comes from a source of truth in this repository; the
only inputs are the image bytes, the channel and the commit being built:

  fw_bus_compat        layout/persisted_layout.json -> protocol.fw_bus_compat
  fw_release           include/FirmwareVersion.h    -> FW_RELEASE (trailing number)
  ota_protocol_version layout/persisted_layout.json -> protocol.bootloader_version
  bootloader_version   layout/persisted_layout.json -> protocol.bootloader_version
  sha256               the published HEX bytes themselves

The bootloader image is deliberately not a release asset: OTA updates only
carry the application at or above the boot section.

    python3 tools/release_manifest.py \
        --image .pio/build/ATtiny1616_OTA/firmware.hex \
        --out firmware.json --channel dev --source-commit "$GITHUB_SHA"
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RECORD_PATH = ROOT / "layout" / "persisted_layout.json"
VERSION_HEADER = ROOT / "include" / "FirmwareVersion.h"
CHANNELS = ("dev", "stable")
RELEASE_PATTERN = re.compile(r"^\d+\.\d+$")
DEFINE_PATTERN = re.compile(r'^\s*#define\s+FW_RELEASE\s+"([^"]+)"', re.MULTILINE)


def load_record() -> dict:
    """Read and validate the canonical persisted-layout record."""
    try:
        record = json.loads(RECORD_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"error: cannot read {RECORD_PATH}: {exc}") from exc
    if record.get("schema") != 1:
        raise SystemExit(f"error: unsupported layout schema in {RECORD_PATH}")
    protocol = record.get("protocol", {})
    for key in ("fw_bus_compat", "bootloader_version"):
        if not isinstance(protocol.get(key), int) or protocol[key] < 1:
            raise SystemExit(f"error: layout protocol.{key} must be a positive integer")
    return record


def read_fw_release() -> str:
    """Read the ``FW_RELEASE`` string from the firmware version header."""
    try:
        text = VERSION_HEADER.read_text(encoding="utf-8")
    except OSError as exc:
        raise SystemExit(f"error: cannot read {VERSION_HEADER}: {exc}") from exc
    match = DEFINE_PATTERN.search(text)
    if not match or not RELEASE_PATTERN.match(match.group(1)):
        raise SystemExit(f"error: FW_RELEASE in {VERSION_HEADER} must look like \"0.2\"")
    return match.group(1)


def release_counter(release: str) -> int:
    """The trailing release number, e.g. ``0.2`` -> ``2``."""
    return int(release.rsplit(".", 1)[1])


def current_version(record: dict, release: str) -> str:
    """The stable wire version composed from the two source constants."""
    return f"{record['protocol']['fw_bus_compat']}.{release}"


def build_manifest(image: bytes, channel: str, source_commit: str,
                   record: dict, release: str,
                   built_at: str | None = None,
                   tag: str | None = None) -> dict:
    """Build the manifest dict, validating the tag for a stable channel."""
    if channel not in CHANNELS:
        raise ValueError(f"unknown channel {channel!r}; expected one of {CHANNELS}")
    if not source_commit or any(c not in "0123456789abcdefABCDEF" for c in source_commit):
        raise ValueError("source_commit must be a hexadecimal commit SHA")

    if channel == "stable":
        version = current_version(record, release)
        expected_tag = f"v{version}"
        if tag is not None and tag != expected_tag:
            raise ValueError(f"tag {tag!r} does not match version {version!r} "
                             f"(expected {expected_tag})")
    else:
        version = f"dev-{source_commit[:7]}"

    if built_at is None:
        built_at = datetime.now(timezone.utc).isoformat(timespec="seconds").replace(
            "+00:00", "Z")

    protocol = record["protocol"]
    return {
        "schema": 1,
        "kind": "slave-app",
        "channel": channel,
        "version": version,
        "fw_bus_compat": protocol["fw_bus_compat"],
        "fw_release": release_counter(release),
        "ota_protocol_version": protocol["bootloader_version"],
        "bootloader_version": protocol["bootloader_version"],
        "sha256": hashlib.sha256(image).hexdigest(),
        "source_commit": source_commit.lower(),
        "built_at": built_at,
        "asset": "firmware.hex",
    }


def git_commit() -> str:
    """Fallback commit for local use; CI passes the authoritative GITHUB_SHA."""
    result = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"],
                            capture_output=True, text=True, check=True)
    return result.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True, type=Path,
                        help="the built OTA application HEX")
    parser.add_argument("--out", type=Path,
                        help="write the manifest here (default: stdout)")
    parser.add_argument("--channel", required=True, choices=CHANNELS)
    parser.add_argument("--source-commit",
                        help="full commit SHA (default: git HEAD)")
    parser.add_argument("--tag", help="stable release tag, validated against sources")
    parser.add_argument("--built-at", help="override build timestamp (for tests)")
    args = parser.parse_args()

    try:
        image = args.image.read_bytes()
    except OSError as exc:
        raise SystemExit(f"error: cannot read {args.image}: {exc}") from exc
    if not image:
        raise SystemExit(f"error: {args.image} is empty")

    record = load_record()
    release = read_fw_release()
    source_commit = args.source_commit or git_commit()
    try:
        manifest = build_manifest(image, args.channel, source_commit, record, release,
                                  args.built_at, args.tag)
    except ValueError as exc:
        raise SystemExit(f"error: {exc}") from exc

    text = json.dumps(manifest, indent=2) + "\n"
    if args.out:
        args.out.write_text(text, encoding="utf-8")
        print(f"wrote {args.out} ({manifest['channel']} {manifest['version']}, "
              f"sha256 {manifest['sha256'][:12]}...)", file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
