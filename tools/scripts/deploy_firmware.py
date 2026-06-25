"""
PlatformIO extra_script: copies the just-built firmware to the HTTP server
directory (/home/labpc/playbuoy) after every build, even on cache hits.

Registered as post:tools/scripts/deploy_firmware.py in platformio.ini.
The environment name (e.g. playbuoy_grinde) becomes the output filename stem.
"""

Import("env")  # type: ignore  # provided by PlatformIO/SCons

import hashlib
import json
import os
import re
import shutil
from pathlib import Path

OUTPUT_DIR = "/home/labpc/playbuoy"


def _sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def _get_version() -> str:
    try:
        with open("src/config.h", "r", encoding="utf-8") as f:
            content = f.read()
        m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', content)
        if m:
            return m.group(1)
    except Exception:
        pass
    return "0.0.0"


def _deploy(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    src_bin = os.path.join(build_dir, "firmware.bin")

    if not os.path.isfile(src_bin):
        print(f"❌ Deploy skipped: firmware.bin not found at {src_bin}")
        return

    node_id = env.subst("$PIOENV")
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    dst_bin = os.path.join(OUTPUT_DIR, f"{node_id}.bin")
    shutil.copy(src_bin, dst_bin)

    sha256 = _sha256_file(dst_bin)
    Path(dst_bin.replace(".bin", ".sha256")).write_text(sha256 + "\n", encoding="ascii")

    version = _get_version()
    with open(dst_bin.replace(".bin", ".version.json"), "w") as f:
        json.dump({
            "version": version,
            "url": f"http://trondve.ddns.net/{node_id}.bin",
            "node_id": node_id,
        }, f, indent=2)
    Path(dst_bin.replace(".bin", ".version")).write_text(version)

    size = os.path.getsize(dst_bin)
    print(f"\n📦 Deployed  : {dst_bin}  ({size:,} bytes)")
    print(f"   SHA-256  : {sha256[:16]}...")
    print(f"   Version  : {version}")
    print(f"   URL      : http://trondve.ddns.net/{node_id}.bin\n")


# AlwaysBuild on firmware.bin ensures the deploy fires even when the
# compiler cache is fully up-to-date (nothing recompiled).
firmware_bin = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")
env.AddPostAction(firmware_bin, _deploy)
env.AlwaysBuild(env.File(firmware_bin))
