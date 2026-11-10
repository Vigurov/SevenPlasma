"""Windows 7 "Make the mouse easier to use" pointer schemes as Xcursor themes.

From the Aero cursor theme (aero-drop) it writes six themes to ~/.local/share/icons:
seven-cursors-{white,black}-{1,2,3} = regular (32 px), large (48 px), extra large (64 px).
Black = the grey parts turned dark and the dark ones light; coloured parts (busy ring) stay.
Each file holds one size only, so the cursor keeps that size whatever size is asked for.
"""
import os, struct, colorsys
from PIL import Image

SRC = '/usr/share/icons/aero-drop/cursors'
OUT = os.path.expanduser('~/.local/share/icons')
SIZES = {1: 32, 2: 48, 3: 64}


def read(path):
    b = open(path, 'rb').read()
    ntoc = struct.unpack_from('<I', b, 12)[0]
    frames = []
    for i in range(ntoc):
        typ, sub, pos = struct.unpack_from('<III', b, 16 + 12 * i)
        if typ != 0xfffd0002:
            continue
        _, _, nominal, _, w, h, xh, yh, delay = struct.unpack_from('<IIIIIIIII', b, pos)
        px = b[pos + 36:pos + 36 + w * h * 4]
        im = Image.frombuffer('RGBA', (w, h), px, 'raw', 'BGRA', 0, 1).copy()
        frames.append((nominal, im, xh, yh, delay))
    return frames


def unpremultiply(im):
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if 0 < a < 255:
                px[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a), a)
    return im


def premultiply(im):
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            px[x, y] = (r * a // 255, g * a // 255, b * a // 255, a)
    return im


def mostly_white(im):
    px = im.load()
    total = light = 0
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a > 200 and max(r, g, b) - min(r, g, b) < 40:
                total += 1
                light += (r + g + b) > 384
    return total and light / total > 0.3


def blacken(im):
    """white-filled pointers (arrow, hand...) get a black fill; the I-beam and other dark ones stay"""
    if not mostly_white(im):
        return im
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
            if s < 0.25 and a > 200:  # grey: white fill <-> black outline (the soft shadow stays dark)
                l = 1 - l
                r, g, b = (int(v * 255) for v in colorsys.hls_to_rgb(h, l, s))
                px[x, y] = (r, g, b, a)
    return im


def write(path, frames):
    toc = []
    chunks = b''
    pos = 16 + 12 * len(frames)
    for nominal, im, xh, yh, delay in frames:
        w, h = im.size
        data = struct.pack('<IIIIIIIII', 36, 0xfffd0002, nominal, 1, w, h, xh, yh, delay) + premultiply(im.copy()).tobytes('raw', 'BGRA')
        toc.append(struct.pack('<III', 0xfffd0002, nominal, pos))
        pos += len(data)
        chunks += data
    with open(path, 'wb') as f:
        f.write(b'Xcur' + struct.pack('<III', 16, 0x10000, len(frames)) + b''.join(toc) + chunks)


def build(color, level):
    target = SIZES[level]
    name = f'seven-cursors-{color}-{level}'
    cdir = os.path.join(OUT, name, 'cursors')
    os.makedirs(cdir, exist_ok=True)
    titles = {'white': 'Windows Standard', 'black': 'Windows Black'}
    size_t = {1: '', 2: ' (large)', 3: ' (extra large)'}
    with open(os.path.join(OUT, name, 'index.theme'), 'w') as f:
        f.write(f'[Icon Theme]\nName={titles[color]}{size_t[level]}\nComment=Windows 7 pointer scheme\nInherits=aero-drop\n')
    done = {}
    for n in sorted(os.listdir(SRC)):
        src = os.path.join(SRC, n)
        real = os.path.realpath(src)
        dst = os.path.join(cdir, n)
        if os.path.lexists(dst):
            os.remove(dst)
        if real in done:  # keep the theme's aliases as links
            os.symlink(done[real], dst)
            continue
        frames = read(real)
        if not frames:
            continue
        # the source size closest to the target (48 for large and extra large)
        sizes = sorted({f[0] for f in frames})
        best = min(sizes, key=lambda s: (abs(s - target), -s))
        out = []
        for nominal, im, xh, yh, delay in frames:
            if nominal != best:
                continue
            im = unpremultiply(im)
            if color == 'black':
                im = blacken(im)
            if nominal != target:
                k = target / nominal
                im = im.resize((round(im.width * k), round(im.height * k)), Image.LANCZOS)
                xh, yh = round(xh * k), round(yh * k)
            out.append((target, im, xh, yh, delay))
        write(dst, out)
        done[real] = n


if __name__ == '__main__':
    for c in ('white', 'black'):
        for lvl in (1, 2, 3):
            build(c, lvl)
            print('built', c, lvl)
