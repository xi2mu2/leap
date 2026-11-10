"""生成 Inno Setup 安装向导用的横幅图片（左侧大图 164x314、右上小图 55x55）。"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "installer", "assets")
os.makedirs(OUT, exist_ok=True)

VERSION = "2.14.10"
LOGO = os.path.join(ROOT, "leap.png")
FONT_BD = "C:/Windows/Fonts/msyhbd.ttc"
FONT_RG = "C:/Windows/Fonts/msyh.ttc"

W, H = 164, 314
SS = 4                      # 超采样倍数
BW, BH = W * SS, H * SS
TOP = (9, 14, 28)           # 深海军蓝
BOT = (26, 43, 96)          # 稍亮的蓝，整体压暗保证白字可读

banner = Image.new("RGB", (BW, BH), TOP)
d = ImageDraw.Draw(banner, "RGBA")

for y in range(BH):
    t = (y / (BH - 1)) ** 1.8
    c = tuple(int(TOP[i] + (BOT[i] - TOP[i]) * t) for i in range(3))
    d.line([(0, y), (BW, y)], fill=c)

# 顶部虚化的节点连线，作为思维导图的意象
for (x1, y1), (x2, y2) in [
    ((0.08, 0.05), (0.52, 0.13)), ((0.52, 0.13), (0.94, 0.07)),
    ((0.52, 0.13), (0.26, 0.20)), ((0.52, 0.13), (0.82, 0.19)),
]:
    d.line([(x1 * BW, y1 * BH), (x2 * BW, y2 * BH)], fill=(147, 197, 253, 45), width=3 * SS)
for (x, y) in [(0.08, 0.05), (0.52, 0.13), (0.94, 0.07), (0.26, 0.20), (0.82, 0.19)]:
    r = 2.6 * SS
    d.ellipse([x * BW - r, y * BH - r, x * BW + r, y * BH + r], fill=(147, 197, 253, 110))

# 图标：白色圆角卡片 + 应用 logo
CARD = 92 * SS
cx = (BW - CARD) // 2
cy = 30 * SS
d.rounded_rectangle([cx, cy, cx + CARD, cy + CARD], radius=20 * SS, fill=(255, 255, 255, 255))
if os.path.exists(LOGO):
    pad = 10 * SS
    logo = Image.open(LOGO).convert("RGBA").resize((CARD - 2 * pad, CARD - 2 * pad), Image.LANCZOS)
    banner.paste(logo, (cx + pad, cy + pad), logo)

f_leap = ImageFont.truetype(FONT_BD, 46 * SS)
f_sub = ImageFont.truetype(FONT_RG, 19 * SS)
f_ver = ImageFont.truetype(FONT_RG, 12 * SS)
f_foot = ImageFont.truetype(FONT_RG, 10 * SS)


def center(text, font, y, fill):
    box = d.textbbox((0, 0), text, font=font)
    d.text(((BW - (box[2] - box[0])) / 2 - box[0], y), text, font=font, fill=fill)


center("leap", f_leap, 146 * SS, (255, 255, 255, 255))
center("思维导图", f_sub, 208 * SS, (191, 219, 254, 255))

# 分隔线
d.line([(48 * SS, 244 * SS), (116 * SS, 244 * SS)], fill=(96, 132, 200, 140), width=1 * SS)
center("v" + VERSION, f_ver, 252 * SS, (147, 197, 253, 255))
center("GPL-3.0 · 开源", f_foot, 276 * SS, (120, 148, 190, 255))

banner = banner.resize((W, H), Image.LANCZOS).convert("RGB")
banner.save(os.path.join(OUT, "wizard-large.bmp"))
banner.save(os.path.join(OUT, "wizard-large.png"))

# 右上角小图：透明底，只放 logo
if os.path.exists(LOGO):
    S = 55
    logo = Image.open(LOGO).convert("RGBA")
    logo = logo.resize((S - 6, S - 6), Image.LANCZOS)
    small = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    small.alpha_composite(logo, (3, 3))
    small.save(os.path.join(OUT, "wizard-small.png"))
    small.convert("RGB").save(os.path.join(OUT, "wizard-small.bmp"))

for f in ("wizard-large.bmp", "wizard-small.bmp"):
    p = os.path.join(OUT, f)
    print(f, Image.open(p).size, os.path.getsize(p))
