#!/usr/bin/env python3
"""
generate_sha256.py — Generate SHA-256 checksum files for PlayBuoy firmware binaries.

The buoy's OTA updater downloads the .sha256 file and verifies the firmware binary
against it before committing to flash. Without a matching .sha256, the OTA is aborted.
This prevents a corrupt download from bricking a permanently sealed buoy.

Usage:
    python tools/scripts/generate_sha256.py                        # all .bin in firmware/
    python tools/scripts/generate_sha256.py firmware/             # all .bin in directory
    python tools/scripts/generate_sha256.py firmware/playbuoy_grinde.bin   # single file

Output:
    <name>.sha256 next to each .bin, containing the 64 hex-char SHA-256 digest.
    Compatible with sha256sum(1) and the buoy OTA parser (which reads the first 64 chars).

Typical publish workflow:
    python tools/scripts/build_all_buoys.py   # builds .bin and calls this automatically
    # Then upload firmware/*.bin, firmware/*.sha256, firmware/*.version to the OTA server
"""

import hashlib
import sys
from pathlib import Path


def sha256_of_file(path: Path) -> str:
    """Stream a file through SHA-256 without loading it all into memory."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def process_bin(bin_path: Path) -> bool:
    """Compute and write the .sha256 file for a single .bin. Returns True on success."""
    if not bin_path.is_file():
        print(f"ERROR: not a file: {bin_path}")
        return False

    digest = sha256_of_file(bin_path)
    sha256_path = bin_path.with_suffix(".sha256")
    sha256_path.write_text(digest + "\n", encoding="ascii")

    size_kb = bin_path.stat().st_size / 1024
    print(f"  {digest}  {bin_path.name}  ({size_kb:.1f} KB)  ->  {sha256_path.name}")
    return True


def collect_targets(argv: list) -> list:
    """Resolve CLI arguments to a list of .bin paths."""
    if not argv:
        # Default: everything in firmware/
        candidates = sorted(Path("firmware").glob("*.bin"))
        if not candidates:
            print("No .bin files found in firmware/")
            sys.exit(1)
        return candidates

    targets = []
    for arg in argv:
        p = Path(arg)
        if p.is_dir():
            found = sorted(p.glob("*.bin"))
            if not found:
                print(f"No .bin files found in {p}/")
                sys.exit(1)
            targets.extend(found)
        elif p.suffix == ".bin":
            targets.append(p)
        else:
            print(f"ERROR: expected a .bin file or directory, got: {arg}")
            sys.exit(1)
    return targets


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]

    targets = collect_targets(argv)
    print(f"Generating SHA-256 for {len(targets)} firmware file(s):")

    ok = all(process_bin(p) for p in targets)
    if ok:
        print("Done.")
    else:
        print("One or more files failed.")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
