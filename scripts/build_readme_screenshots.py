#!/usr/bin/env python3
#
# NPS Log Viewer
# (C) 2026, Eugene Nesterenko - eenest@eenest.net
#
# Generate public-safe README screenshots from sanitized sample data.

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "doc" / "screenshots"
OUT.mkdir(parents=True, exist_ok=True)


def font(size, bold=False, mono=False):
    if mono:
        paths = [
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
        ]
    elif bold:
        paths = [
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
        ]
    else:
        paths = [
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        ]
    for path in paths:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


F_TITLE = font(22, bold=True)
F_UI = font(17)
F_UI_BOLD = font(17, bold=True)
F_SMALL = font(14)
F_MONO = font(15, mono=True)
F_MONO_BOLD = font(15, bold=True, mono=True)


BLUE = "#0078d4"
BORDER = "#c6cbd2"
TEXT = "#17212b"
MUTED = "#5b6673"
BG = "#f4f6f8"
PANEL = "#ffffff"
HEADER = "#eef1f4"
GRID = "#d8dde3"
REJECT_BG = "#fde7e7"
REJECT_TEXT = "#c80000"


def text(draw, xy, value, fill=TEXT, fnt=F_UI):
    draw.text(xy, value, fill=fill, font=fnt)


def trim_to_width(draw, value, fnt, max_w):
    if draw.textlength(value, font=fnt) <= max_w:
        return value
    ell = "..."
    while value and draw.textlength(value + ell, font=fnt) > max_w:
        value = value[:-1]
    return value + ell


def window(draw, title, size):
    w, h = size
    draw.rounded_rectangle([0, 0, w - 1, h - 1], radius=10, fill=PANEL, outline=BORDER)
    draw.rectangle([1, 1, w - 2, 46], fill="#f7f8fa")
    text(draw, (20, 13), title, fnt=F_TITLE)
    for x, label in [(20, "File"), (68, "Edit"), (112, "Settings"), (202, "Help")]:
        text(draw, (x, 56), label, fnt=F_SMALL)
    draw.line([0, 82, w, 82], fill=BORDER)


def draw_filter(draw, y, rejected=False):
    text(draw, (20, y + 7), "Filter:", fnt=F_UI_BOLD)
    draw.rectangle([86, y, 1060, y + 34], fill=PANEL, outline=BORDER)
    placeholder = "User, account, MAC, IP, or any value"
    text(draw, (100, y + 8), placeholder, fill="#8b96a3", fnt=F_SMALL)
    box_x = 1085
    draw.rectangle([box_x, y + 8, box_x + 18, y + 26], fill=PANEL, outline="#9aa4af")
    if rejected:
        draw.rectangle([box_x + 3, y + 11, box_x + 15, y + 23], fill=BLUE)
        text(draw, (box_x + 28, y + 8), "Rejected only", fnt=F_SMALL)
    else:
        text(draw, (box_x + 28, y + 8), "Rejected only", fnt=F_SMALL)
    draw.rounded_rectangle([1232, y, 1320, y + 34], radius=4, fill="#f9fafb", outline=BORDER)
    text(draw, (1260, y + 8), "Clear", fnt=F_SMALL)


def draw_grid(draw, x, y, rows, widths, selected=None):
    headers = ["Time", "User", "Packet", "Result", "NAS IP", "Client", "Calling Station", "Auth Type", "Policy"]
    row_h = 30
    total_w = sum(widths)
    draw.rectangle([x, y, x + total_w, y + row_h], fill=HEADER, outline=BORDER)
    cx = x
    for header, width in zip(headers, widths):
        text(draw, (cx + 8, y + 7), header, fnt=F_UI_BOLD)
        draw.line([cx, y, cx, y + row_h * (len(rows) + 1)], fill=GRID)
        cx += width
    draw.line([x + total_w, y, x + total_w, y + row_h * (len(rows) + 1)], fill=GRID)

    for idx, row in enumerate(rows):
        ry = y + row_h * (idx + 1)
        rejected = row.get("rejected", False)
        selected_row = selected == idx
        fill = BLUE if selected_row else (REJECT_BG if rejected else PANEL)
        fg = "#ffffff" if selected_row else (REJECT_TEXT if rejected else TEXT)
        draw.rectangle([x, ry, x + total_w, ry + row_h], fill=fill, outline=GRID)
        cx = x
        values = [
            row["time"], row["user"], row["packet"], row["result"], row["nas_ip"],
            row["client"], row["calling"], row["auth"], row["policy"],
        ]
        for value, width in zip(values, widths):
            text(draw, (cx + 8, ry + 7), trim_to_width(draw, value, F_SMALL, width - 12), fill=fg, fnt=F_SMALL)
            cx += width


def draw_details(draw, x, y, w, h, lines, red=False):
    draw.rectangle([x, y, x + w, y + h], fill=PANEL, outline=BORDER)
    fill = REJECT_TEXT if red else TEXT
    yy = y + 10
    for line in lines:
        text(draw, (x + 12, yy), line, fill=fill, fnt=F_MONO)
        yy += 22


def main_grid():
    img = Image.new("RGB", (1400, 900), BG)
    d = ImageDraw.Draw(img)
    window(d, "NPS Log Viewer v0.22", img.size)
    draw_filter(d, 98)
    rows = [
        {"time": "04/30/2026 08:15:22.103", "user": "CORPN\\alice", "packet": "1 (Access-Request)", "result": "0 (IAS_SUCCESS)", "nas_ip": "192.0.2.10", "client": "CORPN-WiFi", "calling": "02-00-00-00-00-01", "auth": "EAP-MSCHAP v2", "policy": "CORPN-DOT1X-Policy"},
        {"time": "04/30/2026 08:15:22.203", "user": "CORPN\\alice", "packet": "2 (Access-Accept)", "result": "0 (IAS_SUCCESS)", "nas_ip": "", "client": "CORPN-WiFi", "calling": "", "auth": "EAP-MSCHAP v2", "policy": "CORPN-DOT1X-Policy"},
        {"time": "04/30/2026 08:22:45.891", "user": "CORPN\\bob", "packet": "1 (Access-Request)", "result": "0 (IAS_SUCCESS)", "nas_ip": "192.0.2.11", "client": "CORPN-VPN", "calling": "203.0.113.77", "auth": "5 (EAP)", "policy": "CORPN-VPN-Policy"},
        {"time": "04/30/2026 08:22:45.991", "user": "CORPN\\bob", "packet": "2 (Access-Accept)", "result": "0 (IAS_SUCCESS)", "nas_ip": "", "client": "CORPN-VPN", "calling": "", "auth": "5 (EAP)", "policy": "CORPN-VPN-Policy"},
        {"time": "04/30/2026 08:30:12.356", "user": "CORPN\\denied-user", "packet": "1 (Access-Request)", "result": "0 (IAS_SUCCESS)", "nas_ip": "192.0.2.10", "client": "CORPN-WiFi", "calling": "02-00-00-00-00-02", "auth": "7 (None)", "policy": "CORPN-DOT1X-Policy", "rejected": True},
        {"time": "04/30/2026 08:30:12.456", "user": "CORPN\\denied-user", "packet": "3 (Access-Reject)", "result": "16 (IAS_AUTH_FAILURE)", "nas_ip": "", "client": "CORPN-WiFi", "calling": "", "auth": "7 (None)", "policy": "CORPN-DOT1X-Policy", "rejected": True},
    ]
    widths = [210, 170, 170, 185, 145, 150, 190, 150, 215]
    draw_grid(d, 20, 145, rows, widths, selected=1)
    draw_details(d, 20, 570, 1360, 250, [
        "=== Record Details ===",
        "Timestamp: 04/30/2026 08:15:22.203",
        "Packet-Type: 2 (Access-Accept)",
        "Reason-Code: 0 (IAS_SUCCESS)",
        "MS-CHAP-Domain: 01434F52504E (prefix 0x01; text \"CORPN\")",
        "Class: 311 1 203.0.113.10 04/30/2026 08:15:22 1001",
    ])
    d.rectangle([0, 860, 1400, 899], fill="#f7f8fa", outline=BORDER)
    text(d, (20, 872), "sample-nps.log | Records: 5/5 | Load: 3 ms | Size: 6 KiB", fnt=F_SMALL)
    img.save(OUT / "main-grid.png")


def rejected_filter():
    img = Image.new("RGB", (1400, 820), BG)
    d = ImageDraw.Draw(img)
    window(d, "NPS Log Viewer v0.22", img.size)
    draw_filter(d, 98, rejected=True)
    rows = [
        {"time": "04/30/2026 08:30:12.356", "user": "CORPN\\denied-user", "packet": "1 (Access-Request)", "result": "0 (IAS_SUCCESS)", "nas_ip": "192.0.2.10", "client": "CORPN-WiFi", "calling": "02-00-00-00-00-02", "auth": "7 (None)", "policy": "CORPN-DOT1X-Policy", "rejected": True},
        {"time": "04/30/2026 08:30:12.456", "user": "CORPN\\denied-user", "packet": "3 (Access-Reject)", "result": "16 (IAS_AUTH_FAILURE)", "nas_ip": "", "client": "CORPN-WiFi", "calling": "", "auth": "7 (None)", "policy": "CORPN-DOT1X-Policy", "rejected": True},
    ]
    widths = [210, 180, 175, 205, 145, 150, 200, 150, 210]
    draw_grid(d, 20, 145, rows, widths, selected=1)
    draw_details(d, 20, 300, 1360, 370, [
        "=== Record Details ===",
        "Timestamp: 04/30/2026 08:30:12.456",
        "Packet-Type: 3 (Access-Reject)",
        "Reason-Code: 16 (IAS_AUTH_FAILURE)",
        "User-Name: CORPN\\denied-user",
        "Calling-Station-Id: 02-00-00-00-00-02",
        "Client-Friendly-Name: CORPN-WiFi",
        "NP-Policy-Name: CORPN-DOT1X-Policy",
    ], red=True)
    d.rectangle([0, 780, 1400, 819], fill="#f7f8fa", outline=BORDER)
    text(d, (20, 792), "sample-nps.log | Records: 2/5 visible | Rejected only", fnt=F_SMALL)
    img.save(OUT / "rejected-filter.png")


def about_box():
    img = Image.new("RGB", (980, 620), BG)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([20, 20, 960, 590], radius=10, fill=PANEL, outline=BORDER)
    d.rectangle([21, 21, 959, 64], fill="#f7f8fa")
    text(d, (350, 34), "About NPS Log Viewer", fnt=F_TITLE)
    d.rounded_rectangle([70, 115, 220, 265], radius=16, fill="#0067c5")
    text(d, (98, 150), "NPS", fill="#ffffff", fnt=font(42, bold=True))
    text(d, (98, 205), "LOG", fill="#ffe600", fnt=font(38, bold=True))
    x = 310
    lines = [
        ("NPS Log Viewer v0.22", F_UI_BOLD),
        ("(C) 2026, Eugene Nesterenko - eenest@eenest.net", F_UI),
        ("", F_UI),
        ("Decodes Microsoft NPS/IAS RADIUS logs.", F_UI),
        ("", F_UI),
        ("Supports XML and CSV log formats. RADIUS attribute codes", F_UI),
        ("and common option values are translated to readable text.", F_UI),
        ("", F_UI),
        ("Coffee-money tips are appreciated but never required:", F_UI),
        ("https://ko-fi.com/eenest", F_UI_BOLD),
        ("", F_UI),
        ("Settings file:", F_UI_BOLD),
        ("/home/user/.config/nps-logview/nps-logview.ini", F_MONO),
        ("", F_UI),
        ("The GPL license remains unchanged.", F_UI),
    ]
    yy = 112
    for line, fnt in lines:
        if line.startswith("https://"):
            text(d, (x, yy), line, fill=BLUE, fnt=fnt)
        else:
            text(d, (x, yy), line, fnt=fnt)
        yy += 24
    d.rounded_rectangle([430, 530, 550, 565], radius=5, fill="#f9fafb", outline=BORDER)
    text(d, (474, 539), "OK", fnt=F_UI)
    img.save(OUT / "about-settings.png")


def help_reference():
    img = Image.new("RGB", (1200, 820), BG)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([20, 20, 1180, 790], radius=10, fill=PANEL, outline=BORDER)
    d.rectangle([21, 21, 1179, 66], fill="#f7f8fa")
    text(d, (40, 34), "Help References", fnt=F_TITLE)
    panels = [
        (45, 95, 540, 650, "RADIUS Attribute Codes", [
            "  1 = User-Name",
            "  4 = NAS-IP-Address",
            "  6 = Service-Type",
            " 25 = Class",
            " 26 = Vendor-Specific",
            " 30 = Called-Station-Id",
            " 31 = Calling-Station-Id",
            " 61 = NAS-Port-Type",
            "",
            "Common values are decoded automatically:",
            "Packet-Type: 3 (Access-Reject)",
            "Reason-Code: 16 (IAS_AUTH_FAILURE)",
            "NAS-Port-Type: 19 (Wireless - IEEE 802.11)",
        ]),
        (615, 95, 540, 650, "Vendor-Specific Attribute Codes", [
            "Microsoft (311)",
            "  1 = MS-CHAP-Response",
            " 10 = MS-CHAP-Domain",
            " 16 = MS-MPPE-Send-Key",
            "",
            "Cisco (9)",
            "  1 = Cisco-AVPair",
            "",
            "Juniper (2636), Fortinet (12356),",
            "Avaya (6889), Meraki (29671),",
            "Palo Alto Networks (25461)",
            "",
            "VSA payloads show vendor ID, vendor name,",
            "sub-attribute type, and known names.",
        ]),
    ]
    for px, py, pw, ph, title, lines in panels:
        d.rectangle([px, py, px + pw, py + ph], fill="#fbfcfd", outline=BORDER)
        d.rectangle([px, py, px + pw, py + 42], fill=HEADER, outline=BORDER)
        text(d, (px + 16, py + 11), title, fnt=F_UI_BOLD)
        yy = py + 64
        for line in lines:
            text(d, (px + 18, yy), line, fnt=F_MONO if line[:1] in (" ", "") else F_MONO_BOLD)
            yy += 28
        d.rectangle([px + pw - 18, py + 45, px + pw - 8, py + ph - 8], fill="#eef1f4", outline=GRID)
        d.rounded_rectangle([px + pw - 18, py + 80, px + pw - 8, py + 210], radius=4, fill="#c8d0da")
    d.rounded_rectangle([540, 735, 660, 770], radius=5, fill="#f9fafb", outline=BORDER)
    text(d, (584, 744), "OK", fnt=F_UI)
    img.save(OUT / "radius-vendor-help.png")


def main():
    main_grid()
    rejected_filter()
    about_box()
    help_reference()
    print(f"Wrote screenshots to {OUT}")


if __name__ == "__main__":
    main()
