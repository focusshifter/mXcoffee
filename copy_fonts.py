import os
import shutil

m5gfx_path = ".pio/libdeps/m5stack-core2/M5GFX/src/lgfx/Fonts/Custom/"
include_path = "include/fonts/"

font_files = ["DejaVu9.h", "DejaVu12.h", "DejaVu18.h", "DejaVu24.h", "DejaVu40.h", "DejaVu56.h", "DejaVu72.h"]

for font_file in font_files:
    src = os.path.join(m5gfx_path, font_file)
    dest = os.path.join(include_path, font_file)
    if os.path.exists(src):
        if os.path.exists(dest):
            os.remove(dest)
        shutil.copy2(src, dest)
        print(f"Copied {font_file} to {include_path}")
    else:
        print(f"Warning: {src} not found")