#!/usr/bin/env python3
"""
Create version files for PlayBuoy firmware
This script creates version files that the buoys will check before downloading firmware
"""

import os
import json
from pathlib import Path

# Define your buoys (matching your exact format)
BUOYS = [
    {"id": "vatna", "name": "Vatnakvamsvatnet", "node_id": "playbuoy-vatna"},
    {"id": "grinde", "name": "Litla Grindevatnet", "node_id": "playbuoy-grinde"},
]

# Current firmware version (update this when you release new firmware)
CURRENT_VERSION = "1.1.0"

def create_version_files():
    """Create version files for each buoy"""
    print("🚀 Creating version files for PlayBuoy firmware...")
    
    # Create firmware directory if it doesn't exist
    firmware_dir = Path("firmware")
    firmware_dir.mkdir(exist_ok=True)
    
    for buoy in BUOYS:
        print(f"\n📝 Creating version file for {buoy['name']}...")
        
        # Create JSON version file
        version_json = {
            "version": CURRENT_VERSION,
            "url": f"http://trondve.ddns.net/{buoy['node_id']}.bin",
            "name": buoy['name'],
            "node_id": buoy['node_id'],
            "description": f"Firmware for {buoy['name']} buoy"
        }
        
        json_file = firmware_dir / f"{buoy['node_id']}.version.json"
        with open(json_file, 'w') as f:
            json.dump(version_json, f, indent=2)
        
        # Create simple text version file (fallback)
        text_file = firmware_dir / f"{buoy['node_id']}.version"
        with open(text_file, 'w') as f:
            f.write(CURRENT_VERSION)
        
        print(f"✅ Created {json_file}")
        print(f"✅ Created {text_file}")
    
    print(f"\n🎯 Version files created with version: {CURRENT_VERSION}")
    print(f"📁 Files saved in: {firmware_dir}/")
    
    print(f"\n📋 Generated version files:")
    for file in firmware_dir.glob("*.version*"):
        print(f"  - {file.name}")

if __name__ == "__main__":
    create_version_files()
