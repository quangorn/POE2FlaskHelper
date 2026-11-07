#!/usr/bin/python
# Generates the Stage 0 stub assets: app.ico, enable.wav, disable.wav.
# Pure stdlib, fully deterministic. Run from the repo root:
#   python res/gen_assets.py
import math
import struct
import pathlib

HERE = pathlib.Path(__file__).resolve().parent


def gen_ico() -> bytes:
    """32x32 32bpp ICO: dark disc with a white rim (flask-shaped stub)."""
    w = h = 32
    px = bytearray()
    for y in range(h):  # bottom-up rows of the XOR bitmap
        for x in range(w):
            dx, dy = x - 15.5, y - 15.5
            d = math.sqrt(dx * dx + dy * dy)
            if d <= 13.5:
                inner = d <= 5.0
                # BGRA: blue disc, white ring and core
                r, g, b = (240, 240, 240) if inner else (40, 90, 200)
                px += bytes((b, g, r, 255))
            elif d <= 15.5:
                px += bytes((255, 255, 255, 255))
            else:
                px += bytes((0, 0, 0, 0))  # transparent
    and_mask = bytes(h * ((w + 31) // 32 * 4))  # all zero = fully opaque
    bmp_header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    image = bmp_header + bytes(px) + and_mask
    icon_dir = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(image), 22)
    return icon_dir + entry + image


def gen_wav(freqs: tuple[int, int]) -> bytes:
    """Short WAV: PCM 16-bit mono 44100 Hz, two tones of 60 ms, 10 ms fades."""
    rate = 44100
    seg = int(0.06 * rate)
    fade = int(0.01 * rate)
    samples = []
    for tone, f in enumerate(freqs):
        for i in range(seg):
            env = 1.0
            if tone == 0 and i < fade:
                env = i / fade
            if tone == len(freqs) - 1 and i >= seg - fade:
                env = (seg - 1 - i) / fade
            samples.append(int(8000 * env * math.sin(2 * math.pi * f * i / rate)))
    data = struct.pack("<" + "h" * len(samples), *samples)
    fmt = b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    body = fmt + b"data" + struct.pack("<I", len(data)) + data
    return b"RIFF" + struct.pack("<I", len(body) + 4) + b"WAVE" + body


def main() -> None:
    (HERE / "app.ico").write_bytes(gen_ico())
    (HERE / "enable.wav").write_bytes(gen_wav((660, 990)))
    (HERE / "disable.wav").write_bytes(gen_wav((990, 660)))
    for name in ("app.ico", "enable.wav", "disable.wav"):
        print(f"{name}: {(HERE / name).stat().st_size} bytes")


if __name__ == "__main__":
    main()
