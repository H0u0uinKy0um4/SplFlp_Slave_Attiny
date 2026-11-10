#!/usr/bin/env python3
"""Flash a slave module's application over MQTT (RS485 OTA).

Companion to install.py: install.py performs the one-time UPDI install of
the bootloader; this tool ships later application updates to ONE module
through the master's <prefix>/command/firmware topic:

  1. {"action":"begin","module":N}
  2. {"action":"chunk","data":"<base64>"}   (image bytes, in order)
  3. {"action":"flash"}                      (blocking session, ~20-25 s)

Progress is published by the master on <prefix>/status as the "ota"
object (state/module/progress/error); --watch keeps a mosquitto_sub open
to show it live.

The image is taken from the ATtiny1616_OTA build's firmware.hex and
validated here exactly like install.py does (it must start at 0x0800; a
plain ATtiny1616 build linked at 0x0000 is refused, because neither the
master nor the bootloader can detect the mistake from raw image bytes).
The bytes sent are the application region [0x0800, end) with gaps as
0xFF - the same image install.py merges into its combined hex, so its
printed CRC-32 and this tool's must match.

Chunks are sent as a multiple of 3 image bytes each, so the base64 of
every chunk is padding-free; the master's decoder ends the stream at the
first '=', which must therefore only appear in the final chunk. (The
master also tolerates chunks that split the image anywhere else: its
base64 bit accumulator carries leftover bits across messages.)

Requires the mosquitto-clients package (mosquitto_pub / mosquitto_sub).

Usage:
  python3 flash_mqtt.py --module 3                       # defaults
  python3 flash_mqtt.py --module 3 --watch               # + live status
  python3 flash_mqtt.py --module 3 --dry-run             # no publishing
  python3 flash_mqtt.py --cancel                         # abort/cancel
  python3 flash_mqtt.py --module 3 --reset               # boot a module
                                                          # stuck in the
                                                          # bootloader
"""

from __future__ import annotations

import argparse
import base64
import shutil
import subprocess
import sys
import zlib
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))
import install  # noqa: E402  (path is prepared above; same directory)

# Image bytes per chunk. Must be a multiple of 3 (padding-free base64) and
# small enough that the JSON message fits the master's 2048-byte MQTT
# buffer: 768 bytes become 1024 base64 characters, well under it.
CHUNK_BYTES = 768

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_HEX = REPO_ROOT / ".pio" / "build" / "ATtiny1616_OTA" / "firmware.hex"


def need_mosquitto(dry_run: bool) -> None:
    if dry_run or shutil.which("mosquitto_pub"):
        return
    raise SystemExit(
        "error: mosquitto_pub not found; install the mosquitto-clients "
        "package (or use --dry-run)")


def publish(args: argparse.Namespace, payload: str) -> bool:
    topic = f"{args.prefix}/command/firmware"
    if args.dry_run:
        shown = payload if len(payload) <= 72 else payload[:69] + "..."
        print(f"  would publish to {topic}: {shown}")
        return True
    cmd = ["mosquitto_pub", "-h", args.host, "-p", str(args.port)]
    if args.user:
        cmd += ["-u", args.user]
    if args.password:
        cmd += ["-P", args.password]
    cmd += ["-t", topic, "-m", payload]
    return subprocess.run(cmd).returncode == 0


def load_image(path: Path) -> bytes:
    """Read the OTA build's hex and materialise the app image bytes."""
    if not path.is_file():
        raise SystemExit(
            f"error: {path} not found.\n"
            "  Build it first:  pio run -e ATtiny1616_OTA\n"
            "  (a plain ATtiny1616 build cannot be flashed over RS485: it is\n"
            "   linked at 0x0000 and would corrupt the application)")
    try:
        records = install.parse_ihex(path)
        install.validate_app(records, path.name)   # refuses 0x0000-linked builds
        app_lo, app_hi = install.image_extent(records)
        image = install.image_bytes(records, app_lo, app_hi)
    except install.InstallError as exc:
        raise SystemExit(f"error: {exc}") from exc
    app_max = install.FLASH_END - install.APP_START
    if not 0 < len(image) <= app_max:
        raise SystemExit(f"error: image is {len(image)} bytes, the "
                         f"application region holds {app_max}")
    return image


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Flash a slave module over MQTT through the master.")
    parser.add_argument("--module", type=int, default=0,
                        help="target module id (required for begin/reset)")
    parser.add_argument("--image", type=Path, default=DEFAULT_HEX,
                        help="OTA build firmware.hex (default: %(default)s)")
    parser.add_argument("--host", default="localhost", help="MQTT broker host")
    parser.add_argument("--port", type=int, default=1883, help="MQTT broker port")
    parser.add_argument("--user", default=None, help="MQTT user")
    parser.add_argument("--password", default=None, help="MQTT password")
    parser.add_argument("--prefix", default="splitflap", help="topic prefix")
    parser.add_argument("--chunk-bytes", type=int, default=CHUNK_BYTES,
                        help="image bytes per chunk, multiple of 3 "
                             "(default: %(default)s)")
    parser.add_argument("--watch", action="store_true",
                        help="after flashing, show the status topic until "
                             "Ctrl-C")
    parser.add_argument("--cancel", action="store_true",
                        help="cancel a running session or discard the image")
    parser.add_argument("--reset", action="store_true",
                        help="boot a module stuck in the bootloader back "
                             "into its application")
    parser.add_argument("--dry-run", action="store_true",
                        help="show what would be published, touch nothing")
    args = parser.parse_args()

    if args.chunk_bytes <= 0 or args.chunk_bytes % 3 != 0:
        raise SystemExit("error: --chunk-bytes must be a positive multiple "
                         "of 3 (padding-free base64 per chunk)")

    need_mosquitto(args.dry_run)

    if args.cancel:
        if not publish(args, '{"action":"cancel"}'):
            return 1
        print("Cancellation requested; the master reports the outcome on "
              f"{args.prefix}/status.")
        return 0

    if args.reset:
        if args.module <= 0 or args.module > 255:
            raise SystemExit("error: --module 1..255 is required for --reset")
        if not publish(args, f'{{"action":"reset","module":{args.module}}}'):
            return 1
        print(f"Reset requested for module {args.module}.")
        return 0

    if args.module <= 0 or args.module > 255:
        raise SystemExit("error: --module 1..255 is required")
    image = load_image(args.image)

    crc = zlib.crc32(image) & 0xFFFFFFFF
    pages = (len(image) + 63) // 64
    print(f"Image  : {args.image}")
    print(f"         {len(image)} bytes at 0x{install.APP_START:04X}, "
          f"{pages} pages, crc32 {crc:08X}")
    print(f"Module : {args.module}")
    print(f"Broker : {args.host}:{args.port}, topic {args.prefix}/command/firmware")
    print(f"Chunks : {(len(image) + args.chunk_bytes - 1) // args.chunk_bytes} "
          f"x {args.chunk_bytes} bytes (padding-free base64)")
    if not args.dry_run:
        print("A flash session blocks the display for ~20-25 s.")

    if not publish(args, f'{{"action":"begin","module":{args.module}}}'):
        return 1

    sent = 0
    for offset in range(0, len(image), args.chunk_bytes):
        chunk = image[offset:offset + args.chunk_bytes]
        data = base64.b64encode(chunk).decode("ascii")
        if "=" in data and offset + len(chunk) < len(image):
            raise SystemExit("internal error: mid-stream base64 padding")
        if not publish(args, f'{{"action":"chunk","data":"{data}"}}'):
            return 1
        sent += 1

    if not publish(args, '{"action":"flash"}'):
        return 1

    if args.dry_run:
        print("Dry run: nothing was published.")
        return 0

    print(f"Sent {sent} chunk(s); the flash session is running.")
    if args.watch:
        if not shutil.which("mosquitto_sub"):
            print("mosquitto_sub not found, cannot watch (install "
                  "mosquitto-clients).")
            return 0
        cmd = ["mosquitto_sub", "-h", args.host, "-p", str(args.port),
               "-t", f"{args.prefix}/status"]
        if args.user:
            cmd += ["-u", args.user]
        if args.password:
            cmd += ["-P", args.password]
        print("Watching the status topic (Ctrl-C to stop)...")
        subprocess.run(cmd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
