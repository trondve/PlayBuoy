#!/usr/bin/env python3
"""
Build firmware for multiple PlayBuoy devices
This script builds .bin files for each buoy by temporarily modifying config.h
"""

import os
import shutil
import subprocess
import tempfile
import json
from pathlib import Path

# Define your buoys (matching your exact format)
BUOYS = [
    {"id": "vatna", "name": "Vatnakvamsvatnet", "node_id": "playbuoy-vatna"},
    {"id": "grinde", "name": "Litla Grindevatnet", "node_id": "playbuoy-grinde"},
    # Add more buoys as needed
]

# Current firmware version (update this when you release new firmware)
CURRENT_VERSION = "1.0.0"

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
    """Update config.h with buoy-specific settings (matching your exact format)"""
    config_content = f'''// Configuration - Update these values for your specific setup
#define NODE_ID "{buoy['node_id']}"
#define NAME "{buoy['name']}"
#define FIRMWARE_VERSION "{CURRENT_VERSION}"
#define GPS_SYNC_INTERVAL_SECONDS (24 * 3600)  // 24 hours

// API Configuration
#define API_SERVER "playbuoyapi.no"
#define API_PORT 80
#define API_ENDPOINT "/upload"
#define API_KEY "super-secret-key-123"

// OTA Configuration
#define OTA_SERVER "raw.githubusercontent.com"
#define OTA_PATH "/vladdus/PlayBuoy/main/firmware"

// Network Configuration
#define NETWORK_PROVIDER "telenor"

// Time Configuration
#define NTP_SERVER "no.pool.ntp.org"
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
'''
    
    with open("src/config.h", "w") as f:
        f.write(config_content)
    print(f"✅ Updated config.h for {buoy['name']} (ID: {buoy['node_id']})")

def create_version_files():
    """Create version files for each buoy"""
    print("📝 Creating version files...")
    
    for buoy in BUOYS:
        # Create JSON version file
        version_json = {
            "version": CURRENT_VERSION,
            "url": f"https://raw.githubusercontent.com/vladdus/PlayBuoy/main/firmware/{buoy['node_id']}.bin",
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
        result = subprocess.run([
            "C:\\Users\\trond\\.platformio\\penv\\Scripts\\platformio.exe",
            "run",
            "--environment", "lilygo-t-sim7000g"
        ], capture_output=True, text=True, cwd=".")
        
        if result.returncode == 0:
            # Copy the built firmware to the output directory
            source_bin = ".pio/build/lilygo-t-sim7000g/firmware.bin"
            output_dir = "firmware"
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
    print(f"4. URLs will be: https://raw.githubusercontent.com/vladdus/PlayBuoy/main/firmware/")

if __name__ == "__main__":
    main()
