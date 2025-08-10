#!/usr/bin/env python3
"""
Build firmware for multiple PlayBuoy devices
This script builds .bin files for each buoy by temporarily modifying config.h
"""

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

# Define your buoys
BUOYS = [
    {"id": "vatna", "name": "Vatna", "node_id": "PB001"},
    {"id": "fjord", "name": "Fjord", "node_id": "PB002"},
    {"id": "havet", "name": "Havet", "node_id": "PB003"},
    # Add more buoys as needed
]

def backup_config():
    """Backup the original config.h"""
    if os.path.exists("src/config.h.backup"):
        print("Backup already exists, skipping...")
        return
    shutil.copy("src/config.h", "src/config.h.backup")
    print("✅ Backed up src/config.h")

def restore_config():
    """Restore the original config.h"""
    if os.path.exists("src/config.h.backup"):
        shutil.copy("src/config.h.backup", "src/config.h")
        print("✅ Restored src/config.h")

def update_config(buoy):
    """Update config.h with buoy-specific settings"""
    config_content = f'''#ifndef CONFIG_H
#define CONFIG_H

#define NODE_ID "{buoy['node_id']}"
#define NAME "{buoy['name']}"
#define FIRMWARE_VERSION "1.0.0"
#define GPS_SYNC_INTERVAL_SECONDS (24 * 3600)  // 24 hours

#endif // CONFIG_H
'''
    
    with open("src/config.h", "w") as f:
        f.write(config_content)
    print(f"✅ Updated config.h for {buoy['name']} (ID: {buoy['node_id']})")

def build_firmware(buoy):
    """Build firmware for a specific buoy"""
    print(f"\n🔨 Building firmware for {buoy['name']}...")
    
    try:
        # Run PlatformIO build
        result = subprocess.run([
            "C:\\Users\\trond\\.platformio\\penv\\Scripts\\platformio.exe",
            "run",
            "--environment", "lilygo-t-sim7000g",
            "--target", "build"
        ], capture_output=True, text=True, cwd=".")
        
        if result.returncode == 0:
            # Copy the built firmware to the output directory
            source_bin = ".pio/build/lilygo-t-sim7000g/firmware.bin"
            output_dir = "firmware_builds"
            os.makedirs(output_dir, exist_ok=True)
            
            target_bin = f"{output_dir}/playbuoy-{buoy['id']}.bin"
            shutil.copy(source_bin, target_bin)
            
            # Get file size
            size = os.path.getsize(target_bin)
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
    output_dir = "firmware_builds"
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
    
    print(f"\n🎯 Next steps:")
    print(f"1. Upload the .bin files to your server")
    print(f"2. Each buoy will automatically check for its specific firmware")
    print(f"3. URLs will be: http://your-server.com/firmware/playbuoy-{buoy['id']}.bin")

if __name__ == "__main__":
    main()
