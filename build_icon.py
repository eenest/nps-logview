#!/usr/bin/env python3
"""
Build NPS Log Viewer icon
Blue rounded rectangle with NPS (white) and LOG (yellow) text.
NPS is vertically stretched to fill space evenly.
"""

from PIL import Image, ImageDraw, ImageFont
import os
import struct
import io

SIZES = [16, 32, 48, 64, 128, 256]
FONT_NAME = "DejaVuSans-Bold.ttf"

# Fallback font search
font_paths = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf",
]

font_path = None
for fp in font_paths:
    if os.path.exists(fp):
        font_path = fp
        break

if font_path is None:
    font_path = ImageFont.load_default().path if hasattr(ImageFont.load_default(), 'path') else None

output_dir = "icons"
os.makedirs(output_dir, exist_ok=True)

images = []

for size in SIZES:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Margins around blue rectangle
    margin = max(1, size // 32)
    # Padding inside blue rectangle (text stays inside this)
    inner_pad = max(2, size // 14)
    radius = max(2, size // 10)

    # Blue rounded rectangle background
    blue = (0, 102, 204, 255)
    rect = [margin, margin, size - margin - 1, size - margin - 1]
    draw.rounded_rectangle(rect, radius=radius, fill=blue)

    # Base font size
    if font_path:
        font_size = max(6, int(size * 0.28))
        font = ImageFont.truetype(font_path, font_size)
    else:
        font = ImageFont.load_default()

    text_log = "LOG"
    bbox_log = draw.textbbox((0, 0), text_log, font=font)
    w_log = bbox_log[2] - bbox_log[0]
    h_log = bbox_log[3] - bbox_log[1]

    # --- NPS: draw large then stretch vertically ---
    nps_font_size = int(font_size * 1.20)
    if font_path:
        nps_font = ImageFont.truetype(font_path, nps_font_size)
    else:
        nps_font = font

    bbox_nps = ImageDraw.Draw(Image.new("RGBA", (1, 1))).textbbox((0, 0), "NPS", font=nps_font)
    w_nps_raw = bbox_nps[2] - bbox_nps[0]
    h_nps_raw = bbox_nps[3] - bbox_nps[1]

    # Draw NPS on temp image
    tmp = Image.new("RGBA", (w_nps_raw, h_nps_raw), (0, 0, 0, 0))
    tmp_draw = ImageDraw.Draw(tmp)
    white = (255, 255, 255, 255)
    yellow = (255, 220, 0, 255)
    tmp_draw.text((-bbox_nps[0], -bbox_nps[1]), "NPS", font=nps_font, fill=white)

    # Target: same width as LOG, but taller
    target_w = w_log
    target_h = int(h_nps_raw * 1.05)
    nps_img = tmp.resize((target_w, target_h), Image.Resampling.LANCZOS)

    # --- Position with three equal gaps inside padded area ---
    content_top = margin + inner_pad
    content_bottom = size - margin - inner_pad
    content_h = content_bottom - content_top
    total_text_h = target_h + h_log
    gap = (content_h - total_text_h) // 3

    y_nps = content_top + gap
    y_log = y_nps + target_h + gap

    x_nps = (size - target_w) // 2
    x_log = (size - w_log) // 2

    img.paste(nps_img, (x_nps, y_nps), nps_img)
    draw.text((x_log, y_log), text_log, font=font, fill=yellow)

    # Save individual PNG
    img.save(f"{output_dir}/icon_{size}x{size}.png")
    images.append(img)


def build_ico(images, output_path):
    """Build a multi-resolution ICO file with PNG-compressed images.
    Pillow's built-in ICO saver is broken for multi-res; do it manually."""
    header = struct.pack('<HHH', 0, 1, len(images))  # Reserved, Type=icon, Count
    header_size = 6
    entry_size = 16
    data_offset = header_size + len(images) * entry_size
    entries = b''
    data = b''
    for img in images:
        buf = io.BytesIO()
        img.save(buf, format='PNG')
        png_data = buf.getvalue()
        w, h = img.size
        entry_w = w if w < 256 else 0
        entry_h = h if h < 256 else 0
        entries += struct.pack('<BBBBHHII',
            entry_w, entry_h, 0, 0, 1, 32, len(png_data), data_offset)
        data += png_data
        data_offset += len(png_data)
    with open(output_path, 'wb') as f:
        f.write(header + entries + data)

# Save ICO for Windows
build_ico(images, f"{output_dir}/nps-logview.ico")

print(f"Icons saved to {output_dir}/")
print("  nps-logview.ico (Windows)")
for s in SIZES:
    print(f"  icon_{s}x{s}.png")
