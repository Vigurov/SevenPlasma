#!/usr/bin/env python3
"""Draws the small pieces of the Windows 7 look that have no picture in Windows 7's files: on the developer's
machine they were cut from screenshots, which cannot be shipped. Each is drawn only when it is missing.

    draw-missing.py DEST      (DEST = the folder the extractor wrote, e.g. ~/.local/share)
"""
import os, sys
from PIL import Image, ImageDraw

SS = 4   # drawn 4 times bigger, then scaled down (antialiasing)


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def vgrad(w, h, stops):
    """a vertical gradient: stops = [(y 0..1, colour), ...]"""
    im = Image.new('RGBA', (w, h))
    px = im.load()
    for y in range(h):
        t = y / max(1, h - 1)
        for k in range(len(stops) - 1):
            (t0, c0), (t1, c1) = stops[k], stops[k + 1]
            if t0 <= t <= t1:
                c = lerp(c0, c1, (t - t0) / max(1e-6, t1 - t0))
                break
        else:
            c = stops[-1][1]
        c = c if len(c) == 4 else c + (255,)
        for x in range(w):
            px[x, y] = c
    return im


def masked(fill, mask):
    out = Image.new('RGBA', fill.size)
    out.paste(fill, (0, 0), mask)
    return out


def rounded(w, h, r, border, stops, inner=None):
    """a rounded box with a 1 px border, filled with a vertical gradient (and an inner highlight line)"""
    W, H = w * SS, h * SS
    outer = Image.new('L', (W, H))
    ImageDraw.Draw(outer).rounded_rectangle((0, 0, W - 1, H - 1), r * SS, fill=255)
    im = masked(Image.new('RGBA', (W, H), border + (255,)), outer)
    if inner:
        m = Image.new('L', (W, H))
        ImageDraw.Draw(m).rounded_rectangle((SS, SS, W - 1 - SS, H - 1 - SS), (r - 1) * SS, fill=255)
        im.alpha_composite(masked(Image.new('RGBA', (W, H), inner + (255,)), m))
        o = 2
    else:
        o = 1
    m = Image.new('L', (W, H))
    ImageDraw.Draw(m).rounded_rectangle((o * SS, o * SS, W - 1 - o * SS, H - 1 - o * SS), max(1, r - o) * SS, fill=255)
    im.alpha_composite(masked(vgrad(W, H, stops), m))
    return im.resize((w, h), Image.LANCZOS)


def circle_button(w, h, down):
    """the round grey button with a blue triangle ("Hide Folders", the colour mixer of Window Color)"""
    W, H = w * SS, h * SS
    d = min(w, h) - 1.6
    x0, y0 = (w - d) / 2 * SS, (h - d) / 2 * SS - 0.3 * SS
    box = (x0, y0, x0 + d * SS, y0 + d * SS)
    im = Image.new('RGBA', (W, H))
    m = Image.new('L', (W, H))
    ImageDraw.Draw(m).ellipse(box, fill=255)
    im.alpha_composite(masked(Image.new('RGBA', (W, H), (176, 186, 214, 255)), m))
    m = Image.new('L', (W, H))
    ImageDraw.Draw(m).ellipse((box[0] + SS, box[1] + SS, box[2] - SS, box[3] - SS), fill=255)
    body = vgrad(W, H, [(0, (254, 254, 254)), (0.3, (236, 238, 240)), (0.55, (214, 219, 216)),
                        (0.62, (195, 200, 198)), (0.85, (204, 210, 214)), (1, (238, 242, 240))])
    im.alpha_composite(masked(body, m))
    # the triangle, dark blue getting lighter to its top
    cx, cy = W / 2, H / 2 - 0.2 * SS
    tw, th = 3.6 * SS, 2.2 * SS
    tri = [(cx - tw, cy + th), (cx + tw, cy + th), (cx, cy - th)] if not down else \
          [(cx - tw, cy - th), (cx + tw, cy - th), (cx, cy + th)]
    m = Image.new('L', (W, H))
    ImageDraw.Draw(m).polygon(tri, fill=255)
    g = vgrad(W, H, [(0, (80, 105, 214)), (0.45, (60, 86, 176)), (0.55, (29, 54, 104)), (1, (29, 54, 104))])
    im.alpha_composite(masked(g, m))
    return im.resize((w, h), Image.LANCZOS)


HOT = dict(border=(168, 168, 168), stops=[(0, (251, 251, 251)), (0.6, (232, 232, 232)), (0.62, (220, 220, 220)), (1, (247, 247, 247))],
           inner=(250, 250, 250))
PRESSED = dict(border=(84, 84, 84), stops=[(0, (156, 156, 156)), (0.2, (201, 201, 201)), (0.5, (228, 228, 228)), (1, (229, 229, 229))])


def toolbar_button(w, h, state):
    s = HOT if state == 'hot' else PRESSED
    return rounded(w, h, 3, s['border'], s['stops'], s.get('inner'))


def header_list():
    """5 x 144: six 24 px frames (normal, hot, pressed, then the sorted column's normal, hot, pressed)"""
    out = Image.new('RGBA', (5, 144))
    normal = vgrad(5, 24, [(0, (239, 241, 245)), (0.04, (255, 255, 255)), (0.35, (255, 255, 255)), (0.9, (246, 247, 249)), (1, (246, 247, 249))])
    sorted_ = vgrad(5, 24, [(0, (228, 236, 242)), (0.04, (242, 249, 252)), (0.35, (242, 249, 252)), (0.38, (225, 241, 249)), (0.9, (216, 236, 246)), (1, (216, 236, 246))])
    for f, base, div in ((normal, 0, (226, 227, 229)), (sorted_, 72, (188, 229, 250))):
        d = ImageDraw.Draw(f)
        d.line((0, 22, 4, 22), fill=(226, 227, 227, 255) if base == 0 else (176, 224, 248, 255))
        d.line((0, 23, 4, 23), fill=(239, 239, 239, 255) if base == 0 else (222, 243, 253, 255))
        d.line((4, 1, 4, 21), fill=div + (255,))
        out.paste(f, (0, base))
    hot = Image.new('RGBA', (5, 24), (161, 212, 238, 255))
    hot.paste(vgrad(3, 22, [(0, (227, 247, 255)), (0.38, (223, 246, 255)), (0.4, (189, 237, 255)), (1, (177, 226, 247))]), (1, 0))
    d = ImageDraw.Draw(hot)
    d.line((0, 22, 4, 22), fill=(188, 220, 237, 255))
    d.line((0, 23, 4, 23), fill=(244, 247, 250, 255))
    pressed = Image.new('RGBA', (5, 24), (52, 89, 135, 77))
    pressed.paste(Image.new('RGBA', (3, 22), (63, 95, 127, 8)), (1, 1))
    pressed.paste(Image.new('RGBA', (5, 1), (0, 0, 0, 0)), (0, 23))
    for base in (0, 72):
        out.paste(hot, (0, base + 24))
        out.paste(pressed, (0, base + 48))
    return out


def menubar(state):
    if state == 'hot':
        return rounded(11, 20, 3, (147, 152, 165), [(0, (253, 254, 254)), (0.35, (225, 229, 237)), (0.37, (208, 214, 229)),
                                                    (0.55, (208, 214, 229)), (0.6, (194, 200, 216)), (1, (232, 236, 246))], (242, 243, 244))
    return rounded(11, 20, 3, (74, 76, 83), [(0, (146, 147, 148)), (0.2, (202, 205, 209)), (0.4, (213, 217, 228)),
                                             (0.42, (199, 205, 223)), (1, (201, 208, 226))])


def pieces():
    yield 'seven-aero/header-list.png', header_list
    yield 'seven-aero/menubar/hot.png', lambda: menubar('hot')
    yield 'seven-aero/menubar/pressed.png', lambda: menubar('pressed')
    for st in ('hot', 'pressed'):
        b = toolbar_button(19, 19, st)
        yield 'seven-aero/folders/edge-%s.png' % st, lambda b=b: b.crop((16, 0, 19, 19))
        yield 'seven-aero/folders/fill-%s.png' % st, lambda b=b: b.crop((9, 0, 10, 19))
    for name, down in (('up', False), ('down', True)):
        yield 'seven-aero/hide-folders-%s.png' % name, lambda down=down: circle_button(20, 21, down)
        yield 'seven-aero/themecpl/mixer-%s.png' % name, lambda down=down: circle_button(19, 20, down)
        for st in ('normal', 'hot', 'pressed'):
            def f(down=down, st=st):
                im = Image.new('RGBA', (19, 20), (240, 240, 240, 255))
                if st != 'normal':
                    im.alpha_composite(toolbar_button(19, 20, st))
                im.alpha_composite(circle_button(19, 20, down), (0, 1 if st == 'pressed' else 0))
                return im
            yield 'seven-aero/folders/%s-%s.png' % (name, st), f


def main():
    dest = sys.argv[1]
    n = 0
    for rel, draw in pieces():
        p = os.path.join(dest, rel)
        if os.path.exists(p):
            continue
        os.makedirs(os.path.dirname(p), exist_ok=True)
        draw().save(p)
        n += 1
    print('drawn: %d' % n)


if __name__ == '__main__':
    main()
