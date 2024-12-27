Import("env")
import os
import shutil
import platform
import stat

def create_flash_scripts(dist_dir, firmware_name, port_placeholder="COM3"):
    # Windows batch file
    bat_content = f'''@echo off
echo Please make sure your device is connected and note its COM port
set /p PORT="Enter COM port (default: {port_placeholder}): "
if "!PORT!"=="" set PORT={port_placeholder}
esptool.exe --chip esp32 --port %PORT% --baud 1500000 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 0x0000 {firmware_name}
pause
'''
    with open(os.path.join(dist_dir, "flash.bat"), "w", newline="\r\n") as f:
        f.write(bat_content)

    # Unix shell script
    sh_content = f'''#!/bin/bash
echo "Please make sure your device is connected and note its port"
read -p "Enter port (default: /dev/ttyUSB0): " PORT
PORT=${{PORT:-/dev/ttyUSB0}}
./esptool --chip esp32 --port $PORT --baud 1500000 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 0x0000 {firmware_name}
'''
    sh_path = os.path.join(dist_dir, "flash.sh")
    with open(sh_path, "w", newline="\n") as f:
        f.write(sh_content)
    
    # Make shell script executable
    st = os.stat(sh_path)
    os.chmod(sh_path, st.st_mode | stat.S_IEXEC)

def copy_esptool(dist_dir):
    # Get esptool path from platformio
    if platform.system() == "Windows":
        esptool_name = "esptool.exe"
    else:
        esptool_name = "esptool"
    
    # Try to find esptool in common PlatformIO locations
    possible_paths = [
        os.path.join(env.get("PROJECT_DIR", ""), ".pio", "packages", "tool-esptoolpy", esptool_name),
        os.path.join(os.path.expanduser("~"), ".platformio", "packages", "tool-esptoolpy", esptool_name)
    ]
    
    for path in possible_paths:
        if os.path.exists(path):
            shutil.copy2(path, os.path.join(dist_dir, esptool_name))
            return True
    
    print("Warning: Could not find esptool. Please copy it manually to the dist directory.")
    return False

def before_build(source, target, env):
    pass

def after_build(source, target, env):
    # Get the build directory and create dist if it doesn't exist
    project_dir = env.get("PROJECT_DIR", "")
    dist_dir = os.path.join(project_dir, "dist")
    
    # Clean and recreate dist directory
    if os.path.exists(dist_dir):
        shutil.rmtree(dist_dir)
    os.makedirs(dist_dir)

    # Get the built firmware file
    firmware_path = str(target[0])
    firmware_name = os.path.basename(firmware_path)
    
    # Copy firmware to dist directory
    dest_path = os.path.join(dist_dir, firmware_name)
    print(f"Copying {firmware_path} to {dest_path}")
    shutil.copy2(firmware_path, dest_path)

    # Copy esptool
    copy_esptool(dist_dir)

    # Create flash scripts
    create_flash_scripts(dist_dir, firmware_name)

    # Create README
    readme_content = f'''mXcoffee Firmware Flashing Instructions

Contents:
- {firmware_name}: The firmware binary
- esptool: The ESP32 flashing tool
- flash.bat: Windows flashing script
- flash.sh: Linux/Mac flashing script

Instructions:
1. Connect your device to your computer via USB
2. Note the COM port (Windows) or device path (Linux/Mac)
3. Run the appropriate script for your operating system:
   - Windows: Double-click flash.bat
   - Linux/Mac: Open terminal and run: ./flash.sh
4. Enter the correct port when prompted
5. Wait for the flashing process to complete

Note: On Linux/Mac you may need to grant execute permission to flash.sh:
chmod +x flash.sh
'''
    with open(os.path.join(dist_dir, "README.txt"), "w") as f:
        f.write(readme_content)

    print(f"Distribution package created in {dist_dir}")

# Register the callbacks
env.AddPreAction("buildprog", before_build)
env.AddPostAction("buildprog", after_build)
