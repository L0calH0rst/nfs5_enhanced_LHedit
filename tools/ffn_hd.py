"""Renders an HD atlas for an NFS5 bitmap font (.ffn) from a TrueType font.

Usage: python -I tools/ffn_hd.py <font.ffn> <out_dir> [--scale 4] [--ttf C:/Windows/Fonts/arialbd.ttf]

.ffn layout (FNTF/FNTP): 0x0A u16 glyph count, 0x1C u32 bitmap offset, glyph records from 0x20,
11 bytes each: u16 char, u8 w, u8 h, u16 x, u16 y, u8 advance, s8 xoff, s8 yoff. The bitmap is an
FSH record (type 0x7A: 4 bit alpha, 16 byte header, u16 w at +4, u16 h at +6).

Each glyph keeps its rectangle (x, y, w, h) times scale and gets the TTF glyph at one size for all (cap
height of the original), on the original baseline. The .lvl carries a layout table, nfs5_enhanced moves
the glyph quads to the TTF advances with it.
Glyphs the TTF does not have keep the upscaled original.
Writes <checksum>_<scale>.lvl for nfs5_enhanced (copy to <game>\fonts), <name>_hd.png (alpha in
white), <name>_orig.png and <name>_compare.png.
"""
import argparse
import os
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont


def read_ffn(path):
    data = open(path, 'rb').read()
    count = struct.unpack_from('<H', data, 0x0A)[0]
    bmp_off = struct.unpack_from('<I', data, 0x1C)[0]
    glyphs = []
    for i in range(count):
        c, w, h, x, y, adv, xo, yo = struct.unpack_from('<HBBHHBbb', data, 0x20 + 11 * i)
        glyphs.append(dict(c=c, w=w, h=h, x=x, y=y, adv=adv, xo=xo, yo=yo))
    kind = data[bmp_off]
    if kind != 0x7A:
        raise SystemExit('bitmap type %02X not supported' % kind)
    w, h = struct.unpack_from('<HH', data, bmp_off + 4)
    raw = np.frombuffer(data, np.uint8, ((w + 1) // 2) * h, bmp_off + 16)
    pix = np.empty(w * h, np.uint8)
    pix[0::2] = raw >> 4
    pix[1::2] = raw & 15
    return glyphs, pix.reshape(h, w) * 17, fnv(raw.tobytes())


def fnv(data):
    """Checksum nfs5_enhanced TextQuality keys a glyph sheet by (FNV-1a 32 over the 4 bit rows)."""
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def ink_box(a):
    ys, xs = np.nonzero(a > 40)
    if len(xs) == 0:
        return None
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def glyph_ink(font, ch):
    """Ink of ch with its baseline origin: (array, left, top) relative to the pen on the baseline."""
    l, t, r, b = font.getbbox(ch, anchor='ls')
    if r <= l or b <= t:
        return None
    img = Image.new('L', (r - l, b - t), 0)
    ImageDraw.Draw(img).text((-l, -t), ch, font=font, fill=255, anchor='ls')
    a = np.asarray(img, np.float32)
    # getbbox can return the advance box, crop to the real ink.
    ys, xs = np.nonzero(a > 0)
    if len(xs) == 0:
        return None
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1], l + xs.min(), t + ys.min()


def metrics(glyphs, atlas):
    """Baseline (texels below the line top) and cap height of the original font, from A-Z."""
    bases, caps = [], {}
    for g in glyphs:
        if not (0x41 <= g['c'] <= 0x5A) or g['h'] == 0:
            continue
        box = ink_box(atlas[g['y']:g['y'] + g['h'], g['x']:g['x'] + g['w']])
        if box:
            bases.append(g['yo'] + box[3])
            caps[chr(g['c'])] = box[3] - box[1]
    return max(set(bases), key=bases.count), caps.get('H') or max(caps.values())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ffn')
    ap.add_argument('out')
    ap.add_argument('--scale', type=int, default=4)
    ap.add_argument('--ttf', default='C:/Windows/Fonts/arialbd.ttf')
    ap.add_argument('--soften', type=float, default=0.8, help='edge blur radius in HD pixels, 0 = off')
    args = ap.parse_args()
    s = args.scale

    glyphs, atlas, checksum = read_ffn(args.ffn)
    H, W = atlas.shape
    hd = np.zeros((H * s, W * s), np.float32)
    # One size for every glyph: the TTF cap height matches the original 'H' times scale.
    baseline, cap = metrics(glyphs, atlas)
    probe = ImageFont.truetype(args.ttf, 1000)
    l, t, r, b = probe.getbbox('H', anchor='ls')
    size = max(1, round(1000 * cap * s / (b - t)))
    font = ImageFont.truetype(args.ttf, size)

    margin = 1
    # Every rectangle has at least one free texel left and right in the atlas; nfs5_enhanced widens each
    # quad by half a texel per side, so the glyph may use that half too.
    grow = s // 2
    # nfs5_enhanced sets the glyphs at their TTF advances (layout table below), so each glyph only has to
    # sit inside its rectangle. Too wide for the game's rectangles: every glyph and advance gets narrower
    # by 'narrow', else they get squeezed one by one. The quad edge is filtered against nothing, so
    # 'margin' stays free inside the rectangle.
    ratios = []
    for g in glyphs:
        ch = chr(g['c'])
        if g['c'] < 128 and ch.isalnum() and g['w'] > 0:
            ink = glyph_ink(font, ch)
            if ink:
                ratios.append(ink[0].shape[1] / max(1, g['w'] * s + 2 * grow - 2 * margin))
    narrow = 1.0 / max(1.0, float(np.percentile(ratios, 90))) if ratios else 1.0

    fallback, layout, space = [], [], (0, 0.0)
    for g in glyphs:
        ch = bytes([g['c']]).decode('cp1252', 'ignore') if g['c'] < 256 else chr(g['c'])
        usable = ch and ch.isprintable() and ch != chr(0xAD)
        if g['c'] == 0x20:
            space = (g['adv'], font.getlength(' ') / s)
        if g['w'] == 0 or g['h'] == 0:
            continue
        orig = atlas[g['y']:g['y'] + g['h'], g['x']:g['x'] + g['w']].astype(np.float32)
        box = ink_box(orig)
        if box is None:
            continue
        rx, ry, rw, rh = g['x'] * s, g['y'] * s, g['w'] * s, g['h'] * s
        ink = glyph_ink(font, ch) if usable else None
        if ink is None:
            fallback.append(g['c'])
            big = np.asarray(Image.fromarray(orig.astype(np.uint8)).resize((rw, rh), Image.Resampling.LANCZOS), np.float32)
            hd[ry:ry + rh, rx:rx + rw] = big
            continue
        img, il, it = ink
        img_width = img.shape[1]
        # Vertical: the font baseline lies 'baseline - yo' texels below the rectangle top.
        # Horizontal: centred in the rectangle; the layout table says where the rectangle goes.
        inner = max(1, rw + 2 * grow - 2 * margin)
        width = min(inner, max(1, round(img.shape[1] * narrow)))
        left = rx + (rw - width) // 2
        top = ry + (baseline - g['yo']) * s + it
        img = np.asarray(Image.fromarray(img.astype(np.uint8)).resize((width, img.shape[0]), Image.Resampling.LANCZOS), np.float32)
        # The narrowed ink sits in the middle of where the full TTF ink would be, advances stay natural.
        offset = (il + (img_width - width) / 2 - (left - rx)) / s
        if args.soften > 0:
            # Softer edge: blur inside a padded copy, the cell keeps its 'margin' so nothing reaches a neighbour.
            pad = int(np.ceil(args.soften * 2))
            img = np.pad(img, ((pad, pad), (pad, pad)))
            left -= pad
            top -= pad
            width += 2 * pad
            img = np.asarray(Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(args.soften)), np.float32)
            lo, hi = rx - grow + margin, rx + rw + grow - margin
            cut0, cut1 = max(0, lo - left), max(0, left + width - hi)
            img = img[:, cut0:img.shape[1] - cut1]
            left += cut0
            width = img.shape[1]
        y0, y1 = max(top, ry), min(top + img.shape[0], ry + rh)
        if y1 > y0:
            hd[y0:y1, left:left + width] = np.maximum(hd[y0:y1, left:left + width], img[y0 - top:y1 - top])
        # Rectangle left relative to the pen, and pen advance, in texels of the TTF layout.
        layout.append((g, font.getlength(ch) / s, offset))

    # The TTF advances are scaled once per font so that letters and digits cover the game's advances on
    # average: a line keeps its length and start without scaling each run (clipped list rows would then
    # each get a different factor).
    pairs = [(g['adv'], adv) for g, adv, _ in layout if g['c'] < 128 and chr(g['c']).isalnum()]
    fit = sum(a for a, _ in pairs) / sum(t for _, t in pairs) if pairs else 1.0
    layout = [(g, adv * fit, off) for g, adv, off in layout]
    space = (space[0], space[1] * fit)

    name = os.path.splitext(os.path.basename(args.ffn))[0]
    os.makedirs(args.out, exist_ok=True)

    def rgba(a):
        a = np.clip(a, 0, 255).astype(np.uint8)
        return Image.merge('RGBA', [Image.new('L', (a.shape[1], a.shape[0]), 255)] * 3 + [Image.fromarray(a)])

    lvl = np.clip(np.rint(hd), 0, 255).astype(np.uint8)
    lvl_path = os.path.join(args.out, '%08X_%d.lvl' % (checksum, s))
    with open(lvl_path, 'wb') as f:
        f.write(b'LVL2' + struct.pack('<HH', W * s, H * s) + lvl.tobytes())
        # Layout: count, space advance (game, TTF), then per glyph rectangle x, y, game advance, xoff,
        # TTF advance and rectangle offset from the pen.
        f.write(struct.pack('<Hff', len(layout), *space))
        for g, adv, off in layout:
            f.write(struct.pack('<HHBbxxff', g['x'], g['y'], g['adv'], g['xo'], adv, off))
    rgba(hd).save(os.path.join(args.out, name + '_hd.png'))
    rgba(atlas).save(os.path.join(args.out, name + '_orig.png'))
    up = Image.fromarray(atlas).resize((W * s, H * s), Image.Resampling.NEAREST)
    cmp_img = Image.new('L', (W * s, H * s * 2 + 8), 64)
    cmp_img.paste(up, (0, 0))
    cmp_img.paste(Image.fromarray(np.clip(hd, 0, 255).astype(np.uint8)), (0, H * s + 8))
    cmp_img.save(os.path.join(args.out, name + '_compare.png'))
    print('%s -> %s' % (name, os.path.basename(lvl_path)))
    print('%s: %d glyphs, atlas %dx%d -> %dx%d, fallback %s' % (
        name, len(glyphs), W, H, W * s, H * s, ' '.join('%02X' % c for c in fallback) or '-'))


if __name__ == '__main__':
    main()
