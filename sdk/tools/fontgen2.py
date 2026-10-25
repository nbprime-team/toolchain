#!/usr/bin/env python3
"""fontgen2.py -- rasterize TTF fonts into 4-BIT GRAYSCALE (alpha) C
glyph tables for the HP Prime graphics library (rt/hp_fonts.c).

Two families:
  * prop  -- proportional, Montserrat (OFL): per-glyph advance.
             Per-size weight (optical sizing): 12px uses Medium (500)
             because Regular's ~1px strokes break into specks at that
             size (23 orphan pixels over the ASCII range vs 1); 16px and
             up use Regular -- there the 2px strokes render cleanly and
             Medium just looks chunky.
  * mono  -- monospace, Cascadia Code (OFL): every glyph is centered in
             a fixed cell so all advances are equal.

Glyphs 0x20..0x7E at sizes 12/16/24/32 px.  Each glyph stores 4-bit
alpha coverage (16 levels, PIL's antialiased coverage quantized),
rows top-to-bottom, two pixels per byte, HIGH nibble = LEFT pixel,
every row padded to a byte boundary.  Per-glyph metrics: xoff/yoff are
pixel offsets from the pen position (yoff is the glyph top relative to
the LINE top, i.e. screen_y = line_y + yoff + row).

Why grayscale: 1-bit rendering forces a weight/legibility tradeoff --
thin strokes break up (coverage dips below any threshold along their
length), thick strokes look chunky on a 320x240 panel, and stroke width
inside ONE string jumps between 1px and 2px with subpixel phase (the
"uneven strokes" complaint).  Alpha maps bridge all three: partial
coverage draws as gray, so light weights stay connected and widths
blend smoothly.

Weights were re-tuned for grayscale (AA keeps light strokes connected):
  * mono  = Cascadia Code LIGHT (wght=350) -- narrower than JetBrains
            Mono (cell 7px @12 / 9px @16), so more columns on a 320px line
  * prop 12px = Montserrat wght=450;  16/24/32px = Regular (400)

Usage:  python3 fontgen2.py Montserrat-w450.ttf Montserrat-Regular.ttf \
            CascadiaCode-Light.ttf > hp_fonts_data.c
"""
import sys
from PIL import Image, ImageDraw, ImageFont

SIZES = [12, 16, 24, 32]
FIRST, LAST = 0x20, 0x7E

# per-size prop font files (optical sizing; see docstring)
PROP_PATH_BY_SIZE = {
    12: None,   # filled in main(): Montserrat Medium
    16: None,   # Montserrat Regular
    24: None,
    32: None,
}


def render_glyph(font, size, ch, adv):
    """plain render at the target size + threshold."""
    PAD = 8
    W = size * 2 + PAD * 2
    H = font.getmetrics()[0] + size + PAD * 2
    img = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(img)
    d.text((PAD, PAD), chr(ch), font=font, fill=255, anchor='la')
    return _finish(img, PAD, adv)


def _finish(img, pad, adv):
    """crop to ink, pack 4-bit alpha rows; returns a glyph dict
    (no ink -> empty rows)."""
    bbox = img.getbbox()
    if bbox is None:
        return dict(w=0, h=0, xoff=0, yoff=0, adv=adv, rows=[])
    crop = img.crop(bbox)
    px = crop.load()
    bw, bh = crop.size
    rows = []
    for y in range(bh):
        row = []
        for x in range(bw):
            row.append((px[x, y] * 15 + 127) // 255)   # 0..15
        rows.append(row)
    return dict(w=bw, h=bh,
                xoff=bbox[0] - pad,
                yoff=bbox[1] - pad,     # ink top vs the LINE top
                adv=adv, rows=rows)


def render_glyphs(font, size, mono):
    """return list of dicts for FIRST..LAST; None entries = no ink"""
    out = []
    max_adv = 0
    for ch in range(FIRST, LAST + 1):
        adv = max(1, int(round(font.getlength(chr(ch)))))
        max_adv = max(max_adv, adv)
        out.append(render_glyph(font, size, ch, adv))   # ch is an int
    if mono:
        # fixed cell: every glyph advances by the SAME width.  Use the max
        # of the natural advance and the widest ink (Source Code Pro's 'w'
        # ink is wider than its advance at 12/24px -- a smaller cell would
        # make it overlap the next glyph), then center each glyph in it.
        cell = max(max_adv, max(g['w'] for g in out))
        for g in out:
            g['adv'] = cell
            if g['w']:
                g['xoff'] = (cell - g['w']) // 2
    return out


def pack_rows(rows, width):
    """rows: list of alpha lists (0..15 per pixel) -> packed bytes,
    two pixels per byte, HIGH nibble = LEFT pixel, every row padded to
    (width+1)//2 bytes (the C renderer indexes rows by glyph width, so
    rows must be fixed-stride)."""
    nbytes = (width + 1) // 2
    out = []
    for row in rows:
        base = len(out)
        out += [0] * nbytes
        for x, a in enumerate(row):
            if a:
                out[base + (x >> 1)] |= a << (4 - (x & 1) * 4)
    return out


def emit_family(name, font, size, ascent, descent, mono):
    glyphs = render_glyphs(font, size, mono)
    line_h = ascent + descent
    cell = None
    if mono:
        cell = max(g['adv'] for g in glyphs)   # fixed cell (== adv)
    pool = []
    gdefs = []
    for i, g in enumerate(glyphs):
        off = len(pool)
        pool += pack_rows(g['rows'], g['w'])
        gdefs.append('    {%d, %d, %d, %d, %d, %d}'
                     % (g['w'], g['h'], g['xoff'], g['yoff'], g['adv'], off))
    ng = LAST - FIRST + 1
    print('static const hp_glyph hp_glyphs_%s_%d[%d] = {' % (name, size, ng))
    print(',\n'.join(gdefs))
    print('};')
    print('static const unsigned char hp_bits_%s_%d[%d] = {' % (name, size, len(pool)))
    for i in range(0, len(pool), 16):
        print('    ' + ', '.join('0x%02X' % b for b in pool[i:i + 16]) + ',')
    print('};')
    return line_h, cell


def main():
    # argv: <mont-medium.ttf> <mont-regular.ttf> <jetbrains-mono.ttf>
    med_path, reg_path, mono_path = sys.argv[1], sys.argv[2], sys.argv[3]
    mono_cells = {}
    print('/* AUTOGENERATED by rt/tools/fontgen2.py -- do not edit.')
    print(' * prop = Montserrat (OFL-1.1), per-size weight: 12px wght=450,')
    print(' * 16/24/32px Regular.  mono = Cascadia Code Light (wght=350,')
    print(' * OFL-1.1).  Glyphs 0x20-0x7E stored as 4-bit alpha coverage')
    print(' * (16 levels), high nibble = left pixel, rows byte-padded. */')
    print('#include "hp_fonts.h"')
    print()
    print('/* NOTE: tables are STATIC and reached through the accessor')
    print(' * functions below: TCC\x27s ARM linker cannot relocate a GOT access')
    print(' * to a non-static global (it emits R_ARM_ABS32, which neither')
    print(' * the emulator nor the device loader applies) -- function calls')
    print(' * and PC-relative literals are the only safe cross-TU routes. */')
    # ---- prop (Montserrat, per-size weight) ----
    print('/* ---------------- Montserrat (proportional) ---------------- */')
    prop_tables = []
    for size in SIZES:
        path = med_path if size == 12 else reg_path
        f = ImageFont.truetype(path, size)
        ascent, descent = f.getmetrics()
        line_h, _cell = emit_family('prop', f, size, ascent, descent,
                                    mono=False)
        prop_tables.append((size, line_h, ascent))
        print()
    print('static const hp_font hp_font_prop_table[%d] = {' % len(SIZES))
    for (size, lh, asc), _ in zip(prop_tables, SIZES):
        print('    {%d, %d, %d, 0x%02X, 0x%02X, hp_glyphs_prop_%d,'
              ' hp_bits_prop_%d},' % (size, lh, asc, FIRST, LAST, size, size))
    print('};')
    print('const hp_font *hp_font_prop_data(int i)')
    print('{')
    print('    if (i < 0 || i >= %d) return 0;' % len(SIZES))
    print('    return &hp_font_prop_table[i];')
    print('}')
    print()
    # ---- mono (Cascadia Code) ----
    print('/* ---------------- Cascadia Code (monospace) --------- */')
    mono_tables = []
    for size in SIZES:
        f = ImageFont.truetype(mono_path, size)
        ascent, descent = f.getmetrics()
        line_h, cell = emit_family('mono', f, size, ascent, descent,
                                   mono=True)
        mono_tables.append((size, line_h, ascent))
        mono_cells[size] = cell
        print()
    print('static const hp_font hp_font_mono_table[%d] = {' % len(SIZES))
    for (size, lh, asc), _ in zip(mono_tables, SIZES):
        print('    {%d, %d, %d, 0x%02X, 0x%02X, hp_glyphs_mono_%d,'
              ' hp_bits_mono_%d},' % (size, lh, asc, FIRST, LAST, size, size))
    print('};')
    print('const hp_font *hp_font_mono_data(int i)')
    print('{')
    print('    if (i < 0 || i >= %d) return 0;' % len(SIZES))
    print('    return &hp_font_mono_table[i];')
    print('}')
    # ---- test widths (stdout notes) ----
    print()
    print('/* test widths (px): sum of per-glyph advance, exactly what')
    print(' * hp_font_w computes.  For mono the advance is the fixed cell. */')
    for fam, path in (('prop', None), ('mono', mono_path)):
        for size in SIZES:
            p = med_path if (fam == 'prop' and size == 12) else reg_path \
                if fam == 'prop' else path
            f = ImageFont.truetype(p, size)
            def wsum(s):
                return sum(int(round(f.getlength(c))) for c in s)
            if fam == 'mono':
                cell = mono_cells.get(size)
                if cell:
                    def wsum(s, _c=cell):
                        return len(s) * _c
            print('/* %s %d: "Hello, World!" = %d, "ABC" = %d, "iii" = %d */'
                  % (fam, size, wsum('Hello, World!'), wsum('ABC'), wsum('iii')))


if __name__ == '__main__':
    main()
