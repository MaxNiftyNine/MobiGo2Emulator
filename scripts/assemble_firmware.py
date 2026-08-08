#!/usr/bin/env python3
"""Reassemble and verify the release NAND from Git-friendly parts."""

from __future__ import annotations

import hashlib
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"
OUTPUT = FIRMWARE / "nand.bin"
PARTS = [
    FIRMWARE / "nand.us-stitched.bin.part00",
    FIRMWARE / "nand.us-stitched.bin.part01",
]
EXPECTED_SIZE = 138_412_032
EXPECTED_SHA256 = "66e686225f709e07ca0d76b78b82374cb6fd27296c7a3d8b98c765da66442e7a"


def main() -> int:
    missing = [str(path) for path in PARTS if not path.is_file()]
    if missing:
        raise SystemExit("missing NAND part(s): " + ", ".join(missing))
    temporary = OUTPUT.with_suffix(".bin.partial")
    digest = hashlib.sha256()
    size = 0
    with temporary.open("wb") as destination:
        for part in PARTS:
            with part.open("rb") as source:
                while chunk := source.read(1024 * 1024):
                    destination.write(chunk)
                    digest.update(chunk)
                    size += len(chunk)
    if size != EXPECTED_SIZE or digest.hexdigest() != EXPECTED_SHA256:
        temporary.unlink(missing_ok=True)
        raise SystemExit(
            f"NAND verification failed: size={size} sha256={digest.hexdigest()}"
        )
    temporary.replace(OUTPUT)
    print(f"Verified {OUTPUT} ({size} bytes, sha256={EXPECTED_SHA256})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

