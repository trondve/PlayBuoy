#!/usr/bin/env python3
"""
Update PlayBuoy firmware version
This script helps you update the firmware version across all files
"""

import os
import re
from pathlib import Path

def update_version_files(new_version):
    """Update version in all relevant files"""
    print(f"🔄 Updating firmware version to {new_version}...")
    
    # Files to update
    files_to_update = [
        "build_all_buoys.py",
        "create_version_files.py"
    ]
    
    for file_path in files_to_update:
        if os.path.exists(file_path):
            with open(file_path, 'r') as f:
                content = f.read()
            
            # Update CURRENT_VERSION
            content = re.sub(
                r'CURRENT_VERSION\s*=\s*["\']([^"\']+)["\']',
                f'CURRENT_VERSION = "{new_version}"',
                content
            )
            
            with open(file_path, 'w') as f:
                f.write(content)
            
            print(f"✅ Updated {file_path}")
    
    print(f"\n🎯 Version updated to {new_version}")
    print("📝 Next steps:")
    print("1. Run: python build_all_buoys.py")
    print("2. Upload the new .bin and .version files to your server")
    print("3. Buoys will automatically detect and install the new version")

def main():
    """Main function"""
    print("🚀 PlayBuoy Firmware Version Updater")
    print("=" * 40)
    
    # Get current version
    try:
        with open("build_all_buoys.py", 'r') as f:
            content = f.read()
            match = re.search(r'CURRENT_VERSION\s*=\s*["\']([^"\']+)["\']', content)
            if match:
                current_version = match.group(1)
                print(f"Current version: {current_version}")
            else:
                current_version = "unknown"
                print("Current version: unknown")
    except FileNotFoundError:
        print("❌ build_all_buoys.py not found")
        return
    
    # Get new version from user
    print("\nEnter new firmware version (e.g., 1.0.1, 1.1.0, 2.0.0):")
    new_version = input("New version: ").strip()
    
    if not new_version:
        print("❌ No version provided")
        return
    
    # Validate version format (simple check)
    if not re.match(r'^\d+\.\d+\.\d+$', new_version):
        print("❌ Invalid version format. Use format: major.minor.patch (e.g., 1.0.1)")
        return
    
    # Confirm update
    print(f"\n⚠️  This will update the firmware version from {current_version} to {new_version}")
    confirm = input("Continue? (y/N): ").strip().lower()
    
    if confirm in ['y', 'yes']:
        update_version_files(new_version)
    else:
        print("❌ Update cancelled")

if __name__ == "__main__":
    main()
