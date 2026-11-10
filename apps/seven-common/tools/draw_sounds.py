"""Redraw of the Windows 7 Personalization "Sounds" picture: blue notes over a silver keyboard."""
from PIL import Image, ImageDraw, ImageFilter
import os
N = 288
img = Image.new('RGBA', (N, N), (0, 0, 0, 0))

def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))

def poly_grad(pts, c1, c2, vertical=True):
    mask = Image.new('L', (N, N), 0)
    ImageDraw.Draw(mask).polygon(pts, fill=255)
    g = Image.new('RGBA', (N, N))
    d = ImageDraw.Draw(g)
    ys = [p[1] for p in pts]; xs = [p[0] for p in pts]
    a, b = (min(ys), max(ys)) if vertical else (min(xs), max(xs))
    for i in range(N):
        t = min(1, max(0, (i - a) / max(1, b - a)))
        c = lerp(c1, c2, t)
        if vertical:
            d.line([(0, i), (N, i)], fill=c)
        else:
            d.line([(i, 0), (i, N)], fill=c)
    layer = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    layer.paste(g, (0, 0), mask)
    img.alpha_composite(layer)

# shadow
sh = Image.new('RGBA', (N, N), (0, 0, 0, 0))
ImageDraw.Draw(sh).polygon([(40, 250), (250, 262), (272, 214), (90, 196)], fill=(0, 0, 0, 90))
img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(8)))
# keyboard body (perspective slab): top face and front edge
top = [(30, 186), (92, 140), (262, 176), (226, 232)]
poly_grad(top, (250, 250, 252, 255), (196, 200, 206, 255))
front = [(30, 186), (226, 232), (226, 248), (30, 200)]
poly_grad(front, (150, 154, 160, 255), (92, 96, 102, 255))
side = [(226, 232), (262, 176), (262, 190), (226, 248)]
poly_grad(side, (170, 174, 180, 255), (120, 124, 130, 255))
d = ImageDraw.Draw(img)
d.line(top + [top[0]], fill=(110, 114, 120, 255), width=2)
# keys area: a row of white keys with black keys, in perspective
def P(u, v):  # u along keyboard (0..1), v across (0 front .. 1 back) inside the top face
    fl, fr, br, bl = (44, 190), (220, 226), (250, 180), (90, 150)
    x = (fl[0] * (1 - u) + fr[0] * u) * (1 - v) + (bl[0] * (1 - u) + br[0] * u) * v
    y = (fl[1] * (1 - u) + fr[1] * u) * (1 - v) + (bl[1] * (1 - u) + br[1] * u) * v
    return (x, y)
n = 14
for i in range(n):
    u0, u1 = i / n, (i + 1) / n
    pts = [P(u0 + .004, 0.05), P(u1 - .004, 0.05), P(u1 - .004, 0.62), P(u0 + .004, 0.62)]
    d.polygon(pts, fill=(246, 247, 249, 255), outline=(150, 154, 160, 255))
for i in range(n):
    if i % 7 in (2, 6):
        continue
    u = (i + 1) / n
    pts = [P(u - .022, 0.30), P(u + .022, 0.30), P(u + .022, 0.62), P(u - .022, 0.62)]
    d.polygon(pts, fill=(34, 36, 40, 255))
# control strip at the back
strip = [P(0, 0.70), P(1, 0.70), P(1, 0.92), P(0, 0.92)]
d.polygon(strip, fill=(60, 64, 70, 255))
for k in range(6):
    u = 0.08 + k * 0.15
    d.polygon([P(u, 0.74), P(u + .08, 0.74), P(u + .08, 0.88), P(u, 0.88)], fill=(150, 156, 164, 255))

# notes
def note(cx, cy, r, stem, flag=True):
    lay = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    ld = ImageDraw.Draw(lay)
    head = (cx - r * 1.25, cy - r * .85, cx + r * 1.25, cy + r * .85)
    ld.ellipse(head, fill=(30, 120, 220, 255))
    sx = cx + r * 1.05
    ld.rectangle((sx - r * .28, cy - stem, sx + r * .1, cy), fill=(30, 120, 220, 255))
    if flag:
        ld.polygon([(sx - r * .2, cy - stem), (sx + r * 1.4, cy - stem + r * 1.6), (sx + r * 1.1, cy - stem + r * 2.8),
                    (sx + r * .4, cy - stem + r * 1.8), (sx - r * .2, cy - stem + r * 1.4)], fill=(30, 120, 220, 255))
    lay = lay.rotate(-12, center=(cx, cy), resample=Image.BICUBIC)
    # gloss: lighter top-left
    a = lay.getchannel('A')
    g = Image.new('RGBA', (N, N))
    gd = ImageDraw.Draw(g)
    top_y = cy - stem
    for y in range(N):
        t = min(1, max(0, (y - top_y) / max(1, (cy + r) - top_y)))
        gd.line([(0, y), (N, y)], fill=lerp((120, 200, 255, 255), (16, 92, 196, 255), t))
    out = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    out.paste(g, (0, 0), a)
    shadow = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    shadow.paste((0, 40, 90, 90), (0, 0), a)
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(3)), (3, 4))
    img.alpha_composite(out)
    hl = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(hl).ellipse((cx - r * .9, cy - r * .7, cx + r * .1, cy - r * .05), fill=(255, 255, 255, 120))
    img.alpha_composite(hl.filter(ImageFilter.GaussianBlur(2)))

note(78, 132, 15, 58)
note(140, 96, 18, 72)
note(212, 128, 22, 86)

out = os.path.expanduser('~/.local/share/icons/hicolor/')
bbox = img.getbbox()
w = max(bbox[2] - bbox[0], bbox[3] - bbox[1]) + 8
sq = Image.new('RGBA', (w, w), (0, 0, 0, 0))
sq.alpha_composite(img.crop(bbox), ((w - (bbox[2] - bbox[0])) // 2, (w - (bbox[3] - bbox[1])) // 2))
for s in (16, 24, 32, 48, 64, 128):
    os.makedirs(f'{out}{s}x{s}/apps', exist_ok=True)
    sq.resize((s, s), Image.LANCZOS).save(f'{out}{s}x{s}/apps/seven-sounds-theme.png')
sq.resize((64, 64), Image.LANCZOS).save('/tmp/claude-1000/-home--viguro-/af7886b2-0d25-4924-807b-f0dcf1ed1806/scratchpad/snd_new.png')
