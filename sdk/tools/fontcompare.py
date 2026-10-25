#!/usr/bin/env python3
"""Experiment: compare native-threshold vs 2x-supersampled+downscale font
rasterization, quantifying stroke-thickness evenness."""
import sys
from PIL import Image, ImageDraw, ImageFont

SIZES = [12, 16, 24, 32]
THRESH = 64
TEST = "HEOlM"

def render_native(font, size, ascent, ch):
    PAD = 8
    W = size * 2 + PAD * 2
    H = ascent + size + PAD * 2
    img = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(img)
    d.text((PAD, PAD), ch, font=font, fill=255, anchor='la')
    bbox = img.getbbox()
    if bbox is None:
        return None
    crop = img.crop(bbox)
    px = crop.load()
    bw, bh = crop.size
    bits = [[1 if px[x, y] >= THRESH else 0 for x in range(bw)] for y in range(bh)]
    return bits

def render_supersample(font, size, ascent, ch, cover):
    """render with a 2x-sized font (font is already truetype(path, 2*size)),
    threshold, downscale 2x2 blocks: ink if >= cover of 4 subpixels are ink.
    ascent here is the 2x ascent."""
    PAD = 8
    W = size * 4 + PAD * 4
    H = ascent + size * 2 + PAD * 4
    img = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(img)
    d.text((PAD * 2, PAD * 2), ch, font=font, fill=255, anchor='la')
    bbox = img.getbbox()
    if bbox is None:
        return None
    crop = img.crop(bbox)
    px = crop.load()
    bw, bh = crop.size
    # ensure even dims for 2x2 blocks (pad bottom/right with 0)
    bw2 = (bw + 1) // 2 * 2
    bh2 = (bh + 1) // 2 * 2
    bits = []
    for y in range(0, bh2, 2):
        row = []
        for x in range(0, bw2, 2):
            n = 0
            for dy in (0, 1):
                for dx in (0, 1):
                    if x + dx < bw and y + dy < bh and px[x + dx, y + dy] >= THRESH:
                        n += 1
            row.append(1 if n >= cover else 0)
        bits.append(row)
    return bits

def stroke_stats(bits):
    """per-row and per-column ink counts -> (mean, stdev, min, max)."""
    if not bits:
        return None
    rows = [sum(r) for r in bits]
    cols = [sum(bits[y][x] for y in range(len(bits))) for x in range(len(bits[0]))]
    def stat(v):
        n = len(v)
        m = sum(v) / n
        var = sum((x - m) ** 2 for x in v) / n
        return m, var ** 0.5, min(v), max(v)
    return stat(rows), stat(cols), sum(rows), len(bits), len(bits[0])

def main():
    mono_path = sys.argv[1] if len(sys.argv) > 1 else 'rt/tools/fonts/SourceCodePro-Semibold.ttf'
    prop_path = sys.argv[2] if len(sys.argv) > 2 else 'rt/tools/fonts/Montserrat-Regular.ttf'
    for fam, path in (('SCP mono', mono_path), ('Montserrat', prop_path)):
        print('=== %s ===' % fam)
        for size in SIZES:
            f = ImageFont.truetype(path, size)
            asc, desc = f.getmetrics()
            print('  size %d:' % size)
            for ch in TEST:
                a = render_native(f, size, asc, ch)
                f2 = ImageFont.truetype(path, size * 2)
                asc2, _ = f2.getmetrics()
                b2 = render_supersample(f2, size, asc2, ch, 2)
                b3 = render_supersample(f2, size, asc2, ch, 3)
                sa = stroke_stats(a)
                s2 = stroke_stats(b2)
                s3 = stroke_stats(b3)
                if not sa or not s2:
                    continue
                # ink totals
                print('    %s: native ink=%3d  ss(2) ink=%3d  ss(3) ink=%3d'
                      % (ch, sa[2], s2[2], s3[2]))
                # row stdev (horizontal evenness) and col stdev (vertical)
                print('        row-stdev: nat=%.2f ss2=%.2f ss3=%.2f | '
                      'col-stdev: nat=%.2f ss2=%.2f ss3=%.2f'
                      % (sa[0][1], s2[0][1], s3[0][1], sa[1][1], s2[1][1], s3[1][1]))

if __name__ == '__main__':
    main()
