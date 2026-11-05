#!/usr/bin/env python3
"""
Generator EyeGifs.h dari kumpulan file .gif (animasi mata) untuk ESP32 + OLED SSD1306 128x64.

Pemakaian:   python3 make_eye_gifs.py <folder_berisi_gif> [EyeGifs.h]
Butuh:       pip install pillow

Format data tiap GIF (tersimpan di flash program, ikut masuk merged.bin):
  per frame : [delay/10 ms (1 byte)] [panjang (2 byte, little-endian)] [data RLE dari XOR(frame, frame sebelumnya)]
  RLE token : 0x80|(n-1)  = n byte nol (tidak ada perubahan)   n = 1..128
              0x00..0x7F  = (n-1) lalu n byte literal           n = 1..128
Frame pertama di-XOR terhadap layar kosong.
"""
import sys, os, glob
from PIL import Image

W, H = 128, 64
THRESH = 110        # kecerahan >= THRESH dianggap piksel menyala
STRETCH = True     # True = tarik penuh 128x64 (melebar); False = skala proporsional di tengah layar

def to_page_bytes(img):
    px = img.load()
    buf = bytearray(W * H // 8)
    for y in range(H):
        for x in range(W):
            if px[x, y]:
                buf[x + (y >> 3) * W] |= (1 << (y & 7))
    return bytes(buf)

def gif_frames(path):
    im = Image.open(path)
    out = []
    for i in range(im.n_frames):
        im.seek(i)
        delay = im.info.get('duration', 100)
        g = im.convert('L')
        if STRETCH:
            g = g.resize((W, H), Image.LANCZOS)
        else:
            nw = min(W, max(1, round(g.width * H / g.height)))
            g = g.resize((nw, H), Image.LANCZOS)
            canvas = Image.new('L', (W, H), 0)
            canvas.paste(g, ((W - nw) // 2, 0))
            g = canvas
        g = g.point(lambda v: 255 if v >= THRESH else 0)
        out.append((to_page_bytes(g), delay))
    return out

def rle(data):
    out = bytearray(); i = 0; n = len(data)
    while i < n:
        if data[i] == 0:
            j = i
            while j < n and data[j] == 0 and j - i < 128: j += 1
            out.append(0x80 | (j - i - 1)); i = j
        else:
            j = i
            while j < n and j - i < 128:
                if data[j] == 0 and j + 2 < n and data[j+1] == 0 and data[j+2] == 0: break
                j += 1
            if j == i: j = i + 1
            out.append(j - i - 1); out += data[i:j]; i = j
    return bytes(out)

def encode_gif(frames):
    out = bytearray(); prev = bytes(W * H // 8)
    for data, delay in frames:
        enc = rle(bytes(a ^ b for a, b in zip(data, prev)))
        out.append(min(255, max(1, round(delay / 10))))
        out += len(enc).to_bytes(2, 'little')
        out += enc
        prev = data
    return bytes(out)

def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    folder = sys.argv[1]
    outp = sys.argv[2] if len(sys.argv) > 2 else 'EyeGifs.h'
    files = sorted(glob.glob(os.path.join(folder, '*.gif')))
    if not files: sys.exit('Tidak ada .gif di ' + folder)
    items = []
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        fr = gif_frames(f)
        items.append((name, len(fr), encode_gif(fr)))
        print(f'{name:14s} {len(fr):4d} frame  {len(items[-1][2]):6d} byte')
    total = sum(len(b) for _, _, b in items)
    blank = next((i for i, it in enumerate(items) if it[0] == 'blank'), 0)
    with open(outp, 'w') as h:
        h.write('// AUTO-GENERATED oleh make_eye_gifs.py - jangan diedit manual.\n')
        h.write(f'// {len(items)} animasi, total {total} byte (disimpan di flash program).\n')
        h.write('#pragma once\n#include <Arduino.h>\n#include <stdint.h>\n\n')
        h.write('struct EyeGif { const char* name; const uint8_t* data; uint16_t frames; };\n\n')
        for name, nf, blob in items:
            h.write(f'static const uint8_t EYE_{name}[] PROGMEM = {{\n')
            for i in range(0, len(blob), 32):
                h.write(','.join(str(b) for b in blob[i:i+32]) + ',\n')
            h.write('};\n')
        h.write('\nstatic const EyeGif EYE_GIFS[] = {\n')
        for name, nf, _ in items:
            h.write(f'  {{ "{name}", EYE_{name}, {nf} }},\n')
        h.write('};\n')
        h.write(f'#define EYE_GIF_COUNT {len(items)}\n#define EYE_GIF_BLANK {blank}   // animasi dasar (idle)\n')
    print(f'\n{len(items)} GIF -> {outp} ({total} byte data, {os.path.getsize(outp)//1024} KB sumber)')

if __name__ == '__main__':
    main()
