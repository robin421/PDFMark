import os
import subprocess
from PIL import Image

src_png = "resources/app_icon.png"
img = Image.open(src_png)

# 1. Generate macOS .icns using iconutil
iconset_dir = "resources/app_icon.iconset"
os.makedirs(iconset_dir, exist_ok=True)

# Standard macOS icon sizes
sizes = [
    (16, "icon_16x16.png"),
    (32, "icon_16x16@2x.png"),
    (32, "icon_32x32.png"),
    (64, "icon_32x32@2x.png"),
    (128, "icon_128x128.png"),
    (256, "icon_128x128@2x.png"),
    (256, "icon_256x256.png"),
    (512, "icon_256x256@2x.png"),
    (512, "icon_512x512.png"),
    (1024, "icon_512x512@2x.png"),
]

for sz, name in sizes:
    resized = img.resize((sz, sz), Image.Resampling.LANCZOS)
    resized.save(os.path.join(iconset_dir, name))

# Run iconutil to create .icns
subprocess.run(["iconutil", "-c", "icns", iconset_dir, "-o", "resources/app_icon.icns"], check=True)
print("Created resources/app_icon.icns successfully")

# 2. Generate Windows .ico with multi-resolutions
ico_sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
img.save("resources/app_icon.ico", format="ICO", sizes=ico_sizes)
print("Created resources/app_icon.ico successfully")
