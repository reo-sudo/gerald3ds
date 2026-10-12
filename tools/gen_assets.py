#!/usr/bin/env python3
"""Generates Gerald's icon, banner, banner jingle and in-game sound effects.
Run from the project root:  python3 tools/gen_assets.py
Needs Pillow only (sound is pure python)."""
import math, random, struct, wave, os
from PIL import Image, ImageDraw, ImageFont

os.makedirs("romfs", exist_ok=True)
os.makedirs("meta", exist_ok=True)

# ---------------------------------------------------------------- sprites
FROG = [
    "................",
    "..ggg......ggg..",
    ".gwwwg....gwwwg.",
    ".gwkkg....gwkkg.",
    ".gggggggggggggg.",
    "gggggggggggggggg",
    "grrggggggggggrrg",
    "ggggkkkkkkkkgggg",
    "ggllllllllllllgg",
    "ggllllllllllllgg",
    ".ggllllllllllgg.",
    "..gggggggggggg..",
    "dddd........dddd",
]
PAL = {
    "g": (88, 200, 70), "d": (40, 120, 50), "l": (200, 240, 150),
    "k": (20, 20, 30), "w": (255, 255, 255), "r": (230, 90, 110),
}

def frog_image(scale=1):
    img = Image.new("RGBA", (16, len(FROG)), (0, 0, 0, 0))
    for y, row in enumerate(FROG):
        for x, ch in enumerate(row):
            if ch in PAL:
                img.putpixel((x, y), PAL[ch] + (255,))
    return img.resize((16 * scale, len(FROG) * scale), Image.NEAREST)

# ---------------------------------------------------------------- icon 48x48
icon = Image.new("RGBA", (48, 48), (120, 200, 235, 255))
d = ImageDraw.Draw(icon)
d.rectangle([0, 30, 47, 47], fill=(50, 110, 190, 255))
d.rectangle([4, 38, 43, 44], fill=(60, 150, 70, 255))
f = frog_image(2)  # 32x26
icon.alpha_composite(f, (8, 13))
icon.save("meta/icon.png")

# ---------------------------------------------------------------- banner 256x128
W, H = 64, 32  # drawn small, then scaled x4 for chunky pixels
b = Image.new("RGBA", (W, H), (120, 200, 235, 255))
d = ImageDraw.Draw(b)
for y in range(0, 16):  # sky bands
    c = (120 + y, 200 + y // 2, 235)
    d.line([(0, y), (W, y)], fill=c)
d.rectangle([0, 20, W, H], fill=(50, 110, 190, 255))
d.rectangle([2, 26, 30, 30], fill=(60, 150, 70, 255))
b.alpha_composite(frog_image(1), (7, 13))
for cx, cy in [(40, 6), (52, 4), (58, 9)]:  # clouds
    d.rectangle([cx, cy, cx + 5, cy + 1], fill=(255, 255, 255, 255))
FONT = {
    "G": ["111", "100", "101", "101", "111"], "E": ["111", "100", "110", "100", "111"],
    "R": ["110", "101", "110", "101", "101"], "A": ["010", "101", "111", "101", "101"],
    "L": ["100", "100", "100", "100", "111"], "D": ["110", "101", "101", "101", "110"],
    "T": ["111", "010", "010", "010", "010"], "H": ["101", "101", "111", "101", "101"],
    "F": ["111", "100", "110", "100", "100"], "O": ["111", "101", "101", "101", "111"],
    " ": ["000"] * 5,
}

def pixel_text(img, x, y, txt, color, shadow=(30, 70, 120, 255)):
    for dx, dy, col in ((1, 1, shadow), (0, 0, color)):
        cx = x
        for ch in txt:
            for ry, row in enumerate(FONT[ch]):
                for rx, bit in enumerate(row):
                    if bit == "1":
                        img.putpixel((cx + rx + dx, y + ry + dy), col)
            cx += 4

pixel_text(b, 28, 8, "GERALD", (255, 255, 255, 255))
pixel_text(b, 28, 17, "THE FROG", (255, 230, 90, 255))
b = b.resize((256, 128), Image.NEAREST).convert("RGB")
b.save("meta/banner.png")

# ---------------------------------------------------------------- audio
RATE = 22050

def save_pcm(name, samples):
    data = b"".join(struct.pack("<h", max(-32767, min(32767, int(s * 32767)))) for s in samples)
    with open(f"romfs/{name}.pcm", "wb") as fh:
        fh.write(data)
    return samples

def tone(freq, dur, vol=0.5, shape="square", f_end=None):
    n = int(RATE * dur)
    out, phase = [], 0.0
    for i in range(n):
        t = i / n
        fr = freq + ((f_end - freq) * t if f_end else 0)
        phase += fr / RATE
        if shape == "square":
            s = 1.0 if (phase % 1.0) < 0.5 else -1.0
        elif shape == "tri":
            s = 4 * abs((phase % 1.0) - 0.5) - 1
        else:
            s = math.sin(2 * math.pi * phase)
        env = min(1.0, i / 120) * (1 - t) ** 0.7
        out.append(s * env * vol)
    return out

def silence(dur):
    return [0.0] * int(RATE * dur)

def croak(dur=0.2):
    n = int(RATE * dur)
    out = []
    for i in range(n):
        t = i / RATE
        f = 150 + 40 * math.sin(t * 40)
        s = math.tanh(3 * math.sin(2 * math.pi * f * t))
        s *= 0.55 + 0.45 * math.sin(2 * math.pi * 55 * t)
        env = math.sin(math.pi * i / n)
        out.append(s * env * 0.6)
    return out

ribbit = croak(0.18) + silence(0.07) + croak(0.26)
eat = tone(600, 0.09, 0.4, "square", 250) + silence(0.03) + tone(500, 0.09, 0.4, "square", 200) + tone(180, 0.12, 0.4, "tri")
happy = []
for fr in (523, 659, 784, 1047):
    happy += tone(fr, 0.09, 0.35, "square")
happy += tone(1047, 0.2, 0.35, "square")
click = tone(1100, 0.04, 0.35, "square")
sleep = []
for fr in (392, 330, 262):
    sleep += tone(fr, 0.35, 0.35, "sine")
random.seed(7)
clean = []
for _ in range(6):
    f0 = random.randint(500, 900)
    clean += tone(f0, 0.07, 0.3, "sine", f0 + 500) + silence(0.02)

for nm, s in [("ribbit", ribbit), ("eat", eat), ("happy", happy),
              ("click", click), ("sleep", sleep), ("clean", clean)]:
    save_pcm(nm, s)

# banner jingle (WAV, must be < 3 seconds)
jingle = ribbit[: int(RATE * 0.45)] + silence(0.1)
for fr in (392, 523, 659, 784):
    jingle += tone(fr, 0.12, 0.35, "square")
jingle += tone(1047, 0.35, 0.35, "square")
jingle = jingle[: int(RATE * 2.9)]
with wave.open("meta/banner.wav", "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 32767)) for s in jingle))

print("assets generated")
