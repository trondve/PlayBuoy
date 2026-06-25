#!/usr/bin/env python3
"""
Build firmware for all PlayBuoy buoys.

Runs `pio run` for each buoy environment defined in platformio.ini.
NODE_ID and NAME are injected by each environment's build_flags — no
config.h manipulation needed.

After each build, deploy_firmware.py (extra_scripts) automatically copies
the firmware to /home/labpc/playbuoy/.

Usage (from repo root):
    python tools/scripts/build_all_buoys.py
"""

import os
import shutil
import subprocess
import sys

ENVS = [
    ("playbuoy_grinde", "Litla Grindevatnet"),
    ("playbuoy_vatna",  "Vatnakvamsvatnet"),
]

OUTPUT_DIR = "/home/labpc/playbuoy"


def find_platformio() -> str:
    candidates = [
        os.path.expanduser("~/.platformio/penv/bin/platformio"),
        os.path.expanduser("~/.local/bin/platformio"),
        shutil.which("platformio"),
    ]
    pio = next((p for p in candidates if p and os.path.isfile(p)), None)
    if not pio:
        sys.exit("❌ platformio not found — install via: pip install platformio")
    return pio


def main() -> None:
    pio = find_platformio()
    success = 0

    for env_name, display_name in ENVS:
        print(f"\n{'='*55}")
        print(f"  Building: {display_name}  ({env_name})")
        print(f"{'='*55}")

        result = subprocess.run([pio, "run", "-e", env_name], cwd=".")
        if result.returncode == 0:
            success += 1
        else:
            print(f"❌ Build failed for {display_name}")

    print(f"\n{'='*55}")
    print(f"🏁  Built {success}/{len(ENVS)} buoys")
    print(f"📁  Deployed to: {OUTPUT_DIR}/")
    print(f"{'='*55}")

    if success < len(ENVS):
        sys.exit(1)


if __name__ == "__main__":
    main()
