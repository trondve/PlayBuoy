#!/usr/bin/env python3
"""
Build firmware for multiple PlayBuoy devices
This script builds .bin files for each buoy by temporarily modifying config.h
Works both as a standalone script and as a PlatformIO extra script.
"""

env = None
try:
    from SCons.Script import Import  # type: ignore
except Exception:
    def Import(_):  # type: ignore
        return None

try:
    Import("env")  # Provided by PlatformIO when used as an extra script
except Exception:
    env = None

import os
import shutil
import subprocess
import tempfile
import json
from pathlib import Path
import re

# Define your buoys (matching your exact format)
BUOYS = [
    {"id": "vatna", "name": "Vatnakvamsvatnet", "node_id": "playbuoy_vatna"},
    {"id": "grinde", "name": "Litla Grindevatnet", "node_id": "playbuoy_grinde"},
    # Add more buoys as needed
]

# Re-entry guard to avoid infinite recursion when this script triggers nested PIO builds
ENV_GUARD_FLAG = "PB_MULTI_BUILD"

def get_version_from_config():
    """Parse FIRMWARE_VERSION from src/config.h."""
    try:
        with open("src/config.h", "r", encoding="utf-8") as f:
            content = f.read()
        match = re.search(r"#define\s+FIRMWARE_VERSION\s+\"([^\"]+)\"", content)
        if match:
            return match.group(1)
    except Exception:
        pass
    return "0.0.0"

# Current firmware version discovered from config.h
CURRENT_VERSION = get_version_from_config()

def backup_config():
    """Backup the current config.h (always refresh to avoid stale values)."""
    shutil.copy("src/config.h", "src/config.h.backup")
    print("✅ Backed up (refreshed) src/config.h -> src/config.h.backup")

def restore_config():
    """Restore the backed up config.h and remove the backup to prevent staleness."""
    if os.path.exists("src/config.h.backup"):
        shutil.copy("src/config.h.backup", "src/config.h")
        print("✅ Restored src/config.h")
        try:
            os.remove("src/config.h.backup")
            print("🧹 Removed src/config.h.backup to avoid stale restores")
        except Exception:
            pass

def update_config(buoy):
    """Update only NODE_ID, NAME and FIRMWARE_VERSION in config.h, preserving all other settings."""
    # Always start from the backed-up original to avoid cumulative edits
    source_path = "src/config.h.backup" if os.path.exists("src/config.h.backup") else "src/config.h"
    with open(source_path, "r", encoding="utf-8") as f:
        content = f.read()

    def _replace_define(text, key, value):
        # Replace lines like: #define KEY "..."
        pattern = re.compile(rf"(?m)^\s*#define\s+{re.escape(key)}\s+\"[^\"]*\"")
        replaced, count = pattern.subn(f'#define {key} "{value}"', text)
        if count == 0:
            # Prepend define if not found
            return f'#define {key} "{value}"\n' + text
        return replaced

    content = _replace_define(content, "NODE_ID", buoy["node_id"])
    content = _replace_define(content, "NAME", buoy["name"]) 
    content = _replace_define(content, "FIRMWARE_VERSION", CURRENT_VERSION)

    with open("src/config.h", "w", encoding="utf-8") as f:
        f.write(content)
    print(f"✅ Updated config.h for {buoy['name']} (ID: {buoy['node_id']})")

def create_version_files():
    """Create version files for each buoy"""
    print("📝 Creating version files...")
    
    for buoy in BUOYS:
        # Create JSON version file
        version_json = {
            "version": CURRENT_VERSION,
            "url": f"http://trondve.ddns.net/{buoy['node_id']}.bin",
            "name": buoy['name'],
            "node_id": buoy['node_id'],
            "description": f"Firmware for {buoy['name']} buoy"
        }
        
        json_file = f"firmware/{buoy['node_id']}.version.json"
        with open(json_file, 'w') as f:
            json.dump(version_json, f, indent=2)
        
        # Create simple text version file (fallback)
        text_file = f"firmware/{buoy['node_id']}.version"
        with open(text_file, 'w') as f:
            f.write(CURRENT_VERSION)
        
        print(f"✅ Created version files for {buoy['name']}")

def build_firmware(buoy):
    """Build firmware for a specific buoy"""
    print(f"\n🔨 Building firmware for {buoy['name']}...")
    
    try:
        # Run PlatformIO build
        # Ensure nested PIO runs do not re-enter this multi-build hook
        nested_env = os.environ.copy()
        nested_env[ENV_GUARD_FLAG] = "1"

        result = subprocess.run([
            "C:\\Users\\trond\\.platformio\\penv\\Scripts\\platformio.exe",
            "run",
            "--environment", "lilygo-t-sim7000g"
        ], capture_output=True, text=True, cwd=".", env=nested_env)
        
        if result.returncode == 0:
            # Copy the built firmware to the output directory
            source_bin = ".pio/build/lilygo-t-sim7000g/firmware.bin"
            output_dir = "firmware"
            os.makedirs(output_dir, exist_ok=True)
            
            target_bin = f"{output_dir}/playbuoy_{buoy['id']}.bin"
            shutil.copy(source_bin, target_bin)
            
            # Get file size
            size = os.path.getsize(target_bin)
            print(result.stdout)
            print(f"✅ Built: {target_bin} ({size:,} bytes)")
            return True
        else:
            print(f"❌ Build failed for {buoy['name']}:")
            print(result.stderr)
            return False
            
    except Exception as e:
        print(f"❌ Error building {buoy['name']}: {e}")
        return False

def main():
    """Main build process"""
    print("🚀 Starting PlayBuoy firmware build process...")
    
    # Create output directory
    output_dir = "firmware"
    os.makedirs(output_dir, exist_ok=True)
    
    # Backup original config
    backup_config()
    
    successful_builds = 0
    total_buoys = len(BUOYS)
    
    try:
        for buoy in BUOYS:
            print(f"\n{'='*50}")
            print(f"Processing: {buoy['name']} ({buoy['id']})")
            print(f"{'='*50}")
            
            # Update config for this buoy
            update_config(buoy)
            
            # Build firmware
            if build_firmware(buoy):
                successful_builds += 1
            
            print(f"Progress: {successful_builds}/{total_buoys} buoys built")
        
        # Create version files after all builds are complete
        if successful_builds > 0:
            create_version_files()
    
    finally:
        # Always restore the original config
        print(f"\n{'='*50}")
        print("Cleaning up...")
        restore_config()
    
    # Summary
    print(f"\n{'='*50}")
    print("🏁 BUILD SUMMARY")
    print(f"{'='*50}")
    print(f"✅ Successfully built: {successful_builds}/{total_buoys} buoys")
    print(f"📁 Firmware files saved in: {output_dir}/")
    
    if successful_builds > 0:
        print("\n📋 Generated files:")
        for file in os.listdir(output_dir):
            if file.endswith('.bin'):
                size = os.path.getsize(os.path.join(output_dir, file))
                print(f"  - {file} ({size:,} bytes)")
        
        print("\n📋 Version files:")
        for file in os.listdir(output_dir):
            if file.endswith('.version') or file.endswith('.version.json'):
                print(f"  - {file}")
    
        print(f"\n🎯 Next steps:")
        print(f"1. Upload the .bin files to your server")
        print(f"2. Upload the .version files to your server")
        print(f"3. Each buoy will check version before downloading firmware")
        print(f"4. URLs will be: http://trondve.ddns.net/")


def _run_multi_build(source, target, env=None, **kwargs):
    """SCons hook: run multi-buoy build. Skips during nested PlatformIO runs."""
    print("🔧 Multi-buoy hook invoked")
    if os.environ.get(ENV_GUARD_FLAG) == "1":
        print("⏭️  Skipping multi-buoy build in nested invocation.")
        return
    main()

if __name__ == "__main__":
    main()
else:
    # When used as a PlatformIO extra script, attach a post-build action
    if env is not None:
        print("🔧 Registering multi-buoy post-build action")
        # Run after final program image is produced
        env.AddPostAction("$PROG_PATH", _run_multi_build)
