from PIL import Image, ImageSequence

# ========== 参数设置 ==========
GIF_PATH = "deepseek-ni-keng.gif"  # 你的 GIF 文件名
OUT_PATH = "gif_data.h"  # 输出的头文件
TARGET_W = 64  # 原始宽(建议偶数，64放大3倍=192；改80放大3倍=240全屏)
TARGET_H = 64  # 原始高
FRAME_STEP = 4  # 抽帧：每4帧取1帧（78帧->20帧，约40KB）
BG_COLOR = (0, 0, 0)  # 透明底色 (0,0,0)黑 / (255,255,255)白
USE_DITHER = False  # 卡通/表情包建议 False，真人视频可改 True
# ==============================

gif = Image.open(GIF_PATH)
raw_frames = []

for idx, frame in enumerate(ImageSequence.Iterator(gif)):
    if idx % FRAME_STEP != 0:
        continue
    rgba = frame.convert("RGBA")
    bg = Image.new("RGBA", rgba.size, BG_COLOR + (255,))
    bg.paste(rgba, (0, 0), rgba)
    rgb = bg.convert("RGB").resize((TARGET_W, TARGET_H), Image.Resampling.LANCZOS)
    raw_frames.append(rgb)

# 提取全局16色调色板
sheet = Image.new("RGB", (TARGET_W, TARGET_H * len(raw_frames)))
for i, im in enumerate(raw_frames):
    sheet.paste(im, (0, i * TARGET_H))
pal_img = sheet.quantize(colors=16, method=Image.Quantize.MEDIANCUT)

raw_pal = pal_img.getpalette()[:48]
while len(raw_pal) < 48:
    raw_pal.append(0)

palette_rgb565 = []
for i in range(16):
    r, g, b = raw_pal[i * 3], raw_pal[i * 3 + 1], raw_pal[i * 3 + 2]
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    palette_rgb565.append(rgb565)

# 打包为4位索引图（1字节存2个像素）
dither_mode = Image.Dither.FLOYDSTEINBERG if USE_DITHER else Image.Dither.NONE
row_bytes = (TARGET_W + 1) // 2
frames_data = []

for im in raw_frames:
    q = im.quantize(palette=pal_img, dither=dither_mode)
    buf = []
    for y in range(TARGET_H):
        for bx in range(row_bytes):
            x0 = bx * 2
            x1 = x0 + 1
            p0 = q.getpixel((x0, y)) & 0x0F
            p1 = (q.getpixel((x1, y)) & 0x0F) if x1 < TARGET_W else 0
            buf.append((p0 << 4) | p1)
    frames_data.append(buf)

frame_bytes = len(frames_data[0])
total_kb = (len(frames_data) * frame_bytes + 32) / 1024

with open(OUT_PATH, "w", encoding="ascii") as f:
    f.write("#ifndef __GIF_DATA_H\n#define __GIF_DATA_H\n\n")
    f.write(f"#define GIF_WIDTH   {TARGET_W}\n")
    f.write(f"#define GIF_HEIGHT  {TARGET_H}\n")
    f.write(f"#define GIF_FRAMES  {len(frames_data)}\n\n")
    f.write("const unsigned short gif_palette[16] = {\n    ")
    f.write(", ".join(f"0x{c:04X}" for c in palette_rgb565))
    f.write("\n};\n\n")
    f.write(f"const unsigned char gif_frames[GIF_FRAMES][{frame_bytes}] = {{\n")
    for idx, buf in enumerate(frames_data):
        f.write(f"    // Frame {idx}\n    {{")
        f.write(", ".join(f"0x{b:02X}" for b in buf))
        f.write("},\n")
    f.write("};\n\n#endif\n")

print(f"生成成功！共 {len(frames_data)} 帧，总占用 {total_kb:.1f} KB Flash。")
