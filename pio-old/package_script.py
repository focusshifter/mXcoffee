Import("env")
import os
import shutil
import platform
import stat
import urllib.request
import json
import zipfile
import tempfile

print("Package script loaded!")  # Debug print

def create_flash_scripts(dist_dir, firmware_name, port_placeholder="COM3"):
    # Create Windows batch script
    batch_path = os.path.join(dist_dir, "flash_complete.bat")
    with open(batch_path, 'w') as f:
        f.write(f'''@echo off
setlocal EnableDelayedExpansion
set /p PORT="Enter port (default: {port_placeholder}): " || set PORT={port_placeholder}
esptool.exe --chip esp32 --port %PORT% --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 0x1000 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
pause''')

    # Create Linux/Mac shell script
    sh_path = os.path.join(dist_dir, "flash_complete.sh")
    with open(sh_path, 'w') as f:
        f.write(f'''#!/bin/bash
read -p "Enter port (default: {port_placeholder}): " PORT
PORT=${{PORT:-{port_placeholder}}}
./esptool --chip esp32 --port $PORT --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 0x1000 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin''')
    
    # Make shell script executable
    st = os.stat(sh_path)
    os.chmod(sh_path, st.st_mode | stat.S_IEXEC)

def create_readme(dist_dir, linux_note=""):
    readme_content = '''mXcoffee Firmware Flashing Instructions

Contents:
- bootloader.bin: ESP32 bootloader
- partitions.bin: Partition table
- firmware.bin: The main firmware
- esptool: The ESP32 flashing tool
- flash_complete.bat: Windows flashing script
- flash_complete.sh: Linux/Mac flashing script

Instructions:
1. Connect your device to your computer via USB
2. Note the COM port (Windows) or device path (Linux/Mac, usually /dev/ttyUSB0 or /dev/ttyACM0)
3. Run the appropriate script for your operating system:
   - Windows: Double-click flash_complete.bat
   - Linux/Mac: Open terminal and run: ./flash_complete.sh
4. Enter the correct port when prompted
5. Wait for the flashing process to complete

Note: On Linux/Mac you may need to grant execute permission to flash_complete.sh:
chmod +x flash_complete.sh
'''
    if linux_note:
        readme_content = linux_note + readme_content
    
    with open(os.path.join(dist_dir, "README.md"), "w") as f:
        f.write(readme_content)

def download_file(url, dest_path):
    print(f"Downloading {url} to {dest_path}")
    urllib.request.urlretrieve(url, dest_path)

def get_latest_esptool_url():
    api_url = "https://api.github.com/repos/espressif/esptool/releases/latest"
    try:
        with urllib.request.urlopen(api_url) as response:
            data = json.loads(response.read())
            for asset in data['assets']:
                if asset['name'].endswith('-win64.zip'):
                    return asset['browser_download_url']
        raise Exception("Could not find win64.zip in latest release")
    except Exception as e:
        print(f"Error getting latest release: {e}")
        # Fallback to known good version
        return "https://github.com/espressif/esptool/releases/download/v4.7.0/esptool-v4.7.0-win64.zip"

def copy_esptool(dist_dir):
    success = True
    messages = []

    # Cache directory in the project
    project_dir = os.path.dirname(dist_dir)  # dist_dir is project_dir/dist
    cache_dir = os.path.join(project_dir, '.cache')
    os.makedirs(cache_dir, exist_ok=True)
    
    # Cached esptool path
    cached_esptool = os.path.join(cache_dir, 'esptool.exe')
    
    if os.path.exists(cached_esptool):
        print(f"Using cached esptool from {cached_esptool}")
        shutil.copy2(cached_esptool, os.path.join(dist_dir, "esptool.exe"))
        success = True
    else:
        # Download Windows esptool.exe
        esptool_url = get_latest_esptool_url()
        
        # Create a temporary directory for ZIP extraction
        with tempfile.TemporaryDirectory() as temp_dir:
            zip_path = os.path.join(temp_dir, "esptool.zip")
            try:
                # Download the ZIP file
                print(f"Downloading esptool ZIP from {esptool_url}")
                download_file(esptool_url, zip_path)
                
                # Extract the ZIP
                with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                    zip_ref.extractall(temp_dir)
                
                # Find and copy esptool.exe from the extracted contents, looking in esptool-win64 subdir
                esptool_path = None
                for root, _, files in os.walk(temp_dir):
                    if "esptool-win64" in root and "esptool.exe" in files:
                        esptool_path = os.path.join(root, "esptool.exe")
                        break
                
                if esptool_path:
                    # Copy to cache and dist
                    shutil.copy2(esptool_path, cached_esptool)
                    shutil.copy2(esptool_path, os.path.join(dist_dir, "esptool.exe"))
                    print(f"Successfully cached and copied esptool.exe")
                else:
                    success = False
                    messages.append("Could not find esptool.exe in esptool-win64 directory of the downloaded ZIP")
            except Exception as e:
                success = False
                messages.append(f"Error processing esptool ZIP: {e}")

    # Create Unix wrapper script
    wrapper_path = os.path.join(dist_dir, "esptool")
    try:
        with open(wrapper_path, 'w') as f:
            f.write('''#!/bin/bash
# This script requires esptool to be installed via pip
# If not installed, run: pip install esptool
esptool.py "$@"''')
        st = os.stat(wrapper_path)
        os.chmod(wrapper_path, st.st_mode | stat.S_IEXEC)
        messages.append('''Note for Linux/Mac users:
This script requires esptool to be installed via pip. Install it with:
pip install esptool
''')
    except Exception as e:
        success = False
        messages.append(f"Error creating Unix wrapper script: {e}")

    return success, "\n".join(messages)

def before_build(source, target, env):
    print("Before build called!")  # Debug print
    pass

def after_build(source, target, env):
    print("After build called!")  # Debug print
    print(f"Source: {source}")
    print(f"Target: {target}")
    
    # Get the build directory and create dist if it doesn't exist
    project_dir = env.get("PROJECT_DIR", "")
    build_dir = os.path.join(project_dir, ".pio", "build", "m5stack-core2")
    dist_dir = os.path.join(project_dir, "dist")
    
    # Clean and recreate dist directory
    if os.path.exists(dist_dir):
        shutil.rmtree(dist_dir)
    os.makedirs(dist_dir)

    # Copy firmware files
    firmware_path = os.path.join(build_dir, "firmware.bin")  # Use the .bin file directly
    bootloader_path = os.path.join(build_dir, "bootloader.bin")
    partitions_path = os.path.join(build_dir, "partitions.bin")
    
    # Copy all binary files
    if os.path.exists(firmware_path):
        print(f"Copying {firmware_path} to {dist_dir}/firmware.bin")
        shutil.copy2(firmware_path, os.path.join(dist_dir, "firmware.bin"))
    else:
        print(f"Error: Firmware not found at {firmware_path}")
        return
        
    if os.path.exists(bootloader_path):
        print(f"Copying {bootloader_path} to {dist_dir}/bootloader.bin")
        shutil.copy2(bootloader_path, os.path.join(dist_dir, "bootloader.bin"))
    else:
        print(f"Error: Bootloader not found at {bootloader_path}")
        return
        
    if os.path.exists(partitions_path):
        print(f"Copying {partitions_path} to {dist_dir}/partitions.bin")
        shutil.copy2(partitions_path, os.path.join(dist_dir, "partitions.bin"))
    else:
        print(f"Error: Partitions not found at {partitions_path}")
        return

    # Copy esptool and create flash scripts
    success, linux_note = copy_esptool(dist_dir)
    if success:
        create_flash_scripts(dist_dir, "firmware.bin")
        create_readme(dist_dir, linux_note)
    else:
        print("Failed to copy esptool")
        return

    print(f"Distribution package created in {dist_dir}")

# Only register the post-build action
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)

print("Build actions registered!")  # Debug print
