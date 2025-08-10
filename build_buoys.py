Import("env")
import os
import shutil

# Define your buoys (matching your exact format)
BUOYS = [
    {"id": "vatna", "name": "Vatnakvamsvatnet", "node_id": "playbuoy-vatna"},
    {"id": "grinde", "name": "Litla Grindevatnet", "node_id": "playbuoy-grinde"},
]

def build_multiple_buoys(source, target, env):
    """Build firmware for multiple buoys"""
    print("🚀 Building firmware for multiple buoys...")
    
    # Create output directory
    output_dir = "firmware_builds"
    os.makedirs(output_dir, exist_ok=True)
    
    # Get the built firmware path
    firmware_path = env.subst("$BUILD_DIR/firmware.bin")
    
    if os.path.exists(firmware_path):
        # Copy to each buoy's filename
        for buoy in BUOYS:
            target_path = f"{output_dir}/playbuoy-{buoy['id']}.bin"
            shutil.copy(firmware_path, target_path)
            size = os.path.getsize(target_path)
            print(f"✅ Created: {target_path} ({size:,} bytes)")
    
    print(f"📁 All firmware files saved in: {output_dir}/")

# Register the post-build action
env.AddPostAction("$BUILD_DIR/firmware.bin", build_multiple_buoys)
