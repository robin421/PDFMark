import os
import math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

SIZE = 1024
img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))

# Create macOS squircle / rounded rect container
# Standard macOS icon mask is ~824x824 inside 1024x1024
CANVAS_PAD = 100
BOX = (CANVAS_PAD, CANVAS_PAD, SIZE - CANVAS_PAD, SIZE - CANVAS_PAD)
RADIUS = 185

# 1. Base Drop Shadow
shadow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
sdraw = ImageDraw.Draw(shadow)
sdraw.rounded_rectangle(
    (CANVAS_PAD, CANVAS_PAD + 25, SIZE - CANVAS_PAD, SIZE - CANVAS_PAD + 25),
    radius=RADIUS,
    fill=(0, 0, 0, 110)
)
shadow = shadow.filter(ImageFilter.GaussianBlur(32))
img.paste(shadow, (0, 0), shadow)

# 2. Base Squircle with Gradient (Deep Indigo to Vibrant Blue)
base = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
bdraw = ImageDraw.Draw(base)

# Draw squircle mask
mask = Image.new("L", (SIZE, SIZE), 0)
mdraw = ImageDraw.Draw(mask)
mdraw.rounded_rectangle(BOX, radius=RADIUS, fill=255)

# Gradient background
grad = Image.new("RGBA", (SIZE, SIZE))
for y in range(SIZE):
    ratio = y / SIZE
    # Gradient: #1E293B (dark slate) -> #0F172A (navy) or #1E40AF -> #3B82F6
    r = int(30 + (59 - 30) * ratio)
    g = int(58 + (130 - 58) * ratio)
    b = int(138 + (246 - 138) * ratio)
    for x in range(SIZE):
        pass # fast line fill
    ImageDraw.Draw(grad).line([(0, y), (SIZE, y)], fill=(r, g, b, 255))

base.paste(grad, (0, 0), mask)

# Add subtle inner border
border_draw = ImageDraw.Draw(base)
border_draw.rounded_rectangle(BOX, radius=RADIUS, outline=(255, 255, 255, 50), width=4)

# 3. Document Paper Shape (White with shadow)
DOC_LEFT = 280
DOC_TOP = 220
DOC_RIGHT = 744
DOC_BOTTOM = 800
DOC_CORNER = 36
FOLD = 90

doc_mask = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
ddraw = ImageDraw.Draw(doc_mask)

# Paper shadow
doc_shadow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
ds_draw = ImageDraw.Draw(doc_shadow)
ds_draw.rounded_rectangle((DOC_LEFT, DOC_TOP + 12, DOC_RIGHT, DOC_BOTTOM + 12), radius=DOC_CORNER, fill=(0, 0, 0, 70))
doc_shadow = doc_shadow.filter(ImageFilter.GaussianBlur(16))
base.paste(doc_shadow, (0, 0), doc_shadow)

# Paper sheet
paper = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
pdraw = ImageDraw.Draw(paper)
# Main paper body
pdraw.rounded_rectangle((DOC_LEFT, DOC_TOP, DOC_RIGHT, DOC_BOTTOM), radius=DOC_CORNER, fill=(248, 250, 252, 255))

# Subtle paper border
pdraw.rounded_rectangle((DOC_LEFT, DOC_TOP, DOC_RIGHT, DOC_BOTTOM), radius=DOC_CORNER, outline=(226, 232, 240, 255), width=2)

# Folded corner at top-right
# Cut corner
pdraw.polygon([(DOC_RIGHT - FOLD, DOC_TOP), (DOC_RIGHT, DOC_TOP + FOLD), (DOC_RIGHT, DOC_TOP)], fill=(30, 58, 138, 255)) # hide with bg
pdraw.polygon([(DOC_RIGHT - FOLD, DOC_TOP), (DOC_RIGHT - FOLD, DOC_TOP + FOLD), (DOC_RIGHT, DOC_TOP + FOLD)], fill=(203, 213, 225, 255))
pdraw.line([(DOC_RIGHT - FOLD, DOC_TOP), (DOC_RIGHT - FOLD, DOC_TOP + FOLD), (DOC_RIGHT, DOC_TOP + FOLD)], fill=(148, 163, 184, 255), width=2)

# 4. Watermark pattern on the paper (diagonal repetitive marks)
wm_layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
wdraw = ImageDraw.Draw(wm_layer)

font_wm = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 36)
wm_text = "WATERMARK"

# Draw repeated diagonal lines of "WATERMARK" text inside the paper
paper_box = (DOC_LEFT, DOC_TOP, DOC_RIGHT, DOC_BOTTOM)

# Render rotated watermark stamp
stamp = Image.new("RGBA", (500, 300), (0, 0, 0, 0))
s_draw = ImageDraw.Draw(stamp)
font_stamp = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 46)

# Outer stamp border
s_draw.rounded_rectangle((20, 20, 480, 180), radius=16, outline=(239, 68, 68, 180), width=6)
# Stamp text
s_draw.text((250, 100), "SOLIDIFIED", font=font_stamp, fill=(239, 68, 68, 200), anchor="mm")

# Subtle diagonal watermark stripes
for row in range(-4, 6):
    y_pos = 500 + row * 65
    s_draw.text((250, y_pos), "PDFMARK  •  PERMANENT  •  SECURE", font=ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf", 20), fill=(100, 116, 139, 90), anchor="mm")

# Rotate stamp by -30 deg
stamp_rot = stamp.rotate(32, resample=Image.Resampling.BICUBIC, expand=True)
paper.paste(stamp_rot, (DOC_LEFT - 10, DOC_TOP + 120), stamp_rot)

# 5. Red PDF Badge at the bottom-left of the document
badge = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
b_draw = ImageDraw.Draw(badge)
BADGE_LEFT = DOC_LEFT - 30
BADGE_TOP = DOC_BOTTOM - 130
BADGE_RIGHT = DOC_LEFT + 190
BADGE_BOTTOM = DOC_BOTTOM + 20

# Badge shadow
b_shadow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
bs_draw = ImageDraw.Draw(b_shadow)
bs_draw.rounded_rectangle((BADGE_LEFT, BADGE_TOP + 8, BADGE_RIGHT, BADGE_BOTTOM + 8), radius=22, fill=(0, 0, 0, 90))
b_shadow = b_shadow.filter(ImageFilter.GaussianBlur(10))
paper.paste(b_shadow, (0, 0), b_shadow)

# Badge fill: Vibrant Crimson
b_draw.rounded_rectangle((BADGE_LEFT, BADGE_TOP, BADGE_RIGHT, BADGE_BOTTOM), radius=22, fill=(225, 29, 72, 255))
b_draw.rounded_rectangle((BADGE_LEFT, BADGE_TOP, BADGE_RIGHT, BADGE_BOTTOM), radius=22, outline=(255, 255, 255, 80), width=3)

font_badge = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Black.ttf", 64)
b_draw.text(((BADGE_LEFT + BADGE_RIGHT) // 2, (BADGE_TOP + BADGE_BOTTOM) // 2 - 2), "PDF", font=font_badge, fill=(255, 255, 255, 255), anchor="mm")

# Stamp/Seal icon overlay at bottom-right
SEAL_CX = DOC_RIGHT - 10
SEAL_CY = DOC_BOTTOM - 10
SEAL_R = 75
seal_shadow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
ImageDraw.Draw(seal_shadow).ellipse((SEAL_CX - SEAL_R, SEAL_CY - SEAL_R + 8, SEAL_CX + SEAL_R, SEAL_CY + SEAL_R + 8), fill=(0, 0, 0, 80))
seal_shadow = seal_shadow.filter(ImageFilter.GaussianBlur(12))
paper.paste(seal_shadow, (0, 0), seal_shadow)

# Seal circle
b_draw.ellipse((SEAL_CX - SEAL_R, SEAL_CY - SEAL_R, SEAL_CX + SEAL_R, SEAL_CY + SEAL_R), fill=(16, 185, 129, 255), outline=(255, 255, 255, 180), width=5)
# Checkmark / lock symbol in seal
font_seal = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 60)
b_draw.text((SEAL_CX, SEAL_CY - 3), "✓", font=font_seal, fill=(255, 255, 255, 255), anchor="mm")

# Combine everything
base.paste(paper, (0, 0), paper)
base.paste(badge, (0, 0), badge)

final_img = Image.alpha_composite(img, base)

os.makedirs("resources", exist_ok=True)
png_path = "resources/app_icon.png"
final_img.save(png_path, "PNG")
print(f"Generated 1024x1024 app icon: {png_path}")
