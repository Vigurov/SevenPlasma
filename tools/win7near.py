#!/usr/bin/env python3
"""Second pass for the assets win7match.py could not trace: approximate matching.

Every picture resource of the Windows tree - and every square cell of toolbar strips (bitmaps that are N
icons wide) - gets a perceptual hash (dHash of the picture composited on grey, colour-key pixels treated as
transparent). Each unmatched asset is compared with them; the nearest candidates are written out so the
recipe (resource, cell, scale) can be checked and recorded.

    win7near.py WINDOWS_ROOT MATCH.json OUT.json
"""
import io, json, os, struct, sys
from multiprocessing import Pool
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from win7index import PE, dib_to_image, icon_frame, RT_BITMAP, RT_ICON, RT_GROUP_ICON, EXTS

KEYS = {(255, 0, 255), (192, 192, 192), (0, 128, 128)}   # usual colour keys of old toolbar strips


def keyed(im):
    """RGBA with the colour key (or the corner colour of an opaque bitmap) made transparent"""
    im = im.convert('RGBA')
    px = im.load()
    w, h = im.size
    if all(px[x, y][3] == 255 for x, y in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1))):
        corner = px[0, 0][:3]
        keys = KEYS | {corner}
        def is_key(r, g, b):
            return (r, g, b) in keys or (r > 240 and g < 16 and b > 240)   # magenta, also slightly off
        data = [(r, g, b, 0) if is_key(r, g, b) else (r, g, b, a) for r, g, b, a in im.getdata()]
        im = Image.new('RGBA', (w, h))
        im.putdata(data)
    return im


def dhash(im, n=8):
    im = keyed(im)
    bg = Image.new('RGBA', im.size, (128, 128, 128, 255))
    bg.alpha_composite(im)
    g = bg.convert('L').resize((n + 1, n), Image.LANCZOS)
    p = list(g.getdata())
    bits = 0
    for y in range(n):
        for x in range(n):
            bits = bits << 1 | (p[y * (n + 1) + x] > p[y * (n + 1) + x + 1])
    return bits


def pictures(root, rel):
    """(picture, description) for every picture of a PE file, strip cells included"""
    try:
        pe = PE(open(os.path.join(root, rel), 'rb').read())
        res = pe.all()
    except Exception:
        return
    icons = {rid: b for t, rid, b in res if t == RT_ICON}
    raw = pe.d
    if b'BSJB' in raw:
        # .NET assemblies: serialized bitmaps / PNGs (MMC snap-ins keep their toolbar pictures so)
        pos = 0
        while True:
            i = raw.find(b'BM', pos)
            if i < 0 or i + 30 > len(raw):
                break
            pos = i + 2
            size, _, off = struct.unpack_from('<IIi', raw, i + 2)
            hs = struct.unpack_from('<I', raw, i + 14)[0]
            if hs not in (40, 108, 124) or not 0 < size < 4000000 or not 0 < off < size:
                continue
            try:
                im = Image.open(io.BytesIO(raw[i:i + size]))
                im.load()
                yield im, ['carved-bmp', i, '']
                w, h = im.size
                for cell in (16, 20, 24, 32):
                    if h == cell and w > cell and w % cell == 0 and w // cell <= 64:
                        for k in range(w // cell):
                            yield im.crop((k * cell, 0, (k + 1) * cell, cell)), ['carved-bmp-cell', i, '%d:%d' % (cell, k)]
            except Exception:
                pass
    for t, rid, blob in res:
        try:
            if t == RT_GROUP_ICON:
                for i in range(struct.unpack_from('<H', blob, 4)[0]):
                    w, h, cc, _, pl, bpp, size, iid = struct.unpack_from('<BBBBHHIH', blob, 6 + 14 * i)
                    b = icons.get(iid)
                    if b:
                        im = icon_frame(b)
                        yield im, ['icon', rid, '%d@%d' % (w or 256, bpp)]
            elif t == RT_BITMAP or blob[:4] == b'\x89PNG':
                im = dib_to_image(blob) if t == RT_BITMAP else Image.open(io.BytesIO(blob))
                im.load()
                kind = 'bitmap' if t == RT_BITMAP else 'image'
                yield im, [kind, rid, str(t)]
                w, h = im.size
                # a strip of square cells: cut it
                for cell in (16, 20, 24, 32, 48):
                    if h == cell and w > cell and w % cell == 0 and w // cell <= 64:
                        for k in range(w // cell):
                            yield im.crop((k * cell, 0, (k + 1) * cell, cell)), [kind + '-cell', rid, '%d:%d' % (cell, k)]
        except Exception:
            continue


def work(args):
    root, rel = args
    out = []
    for im, desc in pictures(root, rel):
        try:
            out.append((dhash(im), im.size[0], im.size[1], rel, desc))
        except Exception:
            pass
    return out


def main():
    root, matchp, outp = sys.argv[1:4]
    match = json.load(open(matchp))
    missing = [p for p, e in match.items() if not e and p.lower().endswith(('.png', '.gif', '.jpg', '.bmp'))]
    targets = []
    for p in missing:
        try:
            im = Image.open(p)
            im.load()
            targets.append((p, dhash(im), im.size))
        except Exception:
            pass
    files = []
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.lower().endswith(EXTS):
                files.append((root, os.path.relpath(os.path.join(d, f), root)))
    best = {p: [] for p, _, _ in targets}
    with Pool(8) as pool:
        for n, items in enumerate(pool.imap_unordered(work, files, chunksize=4)):
            for h, w, hh, rel, desc in items:
                for p, th, (tw, tht) in targets:
                    d = bin(h ^ th).count('1')
                    if d <= 10 and abs(w / max(hh, 1) - tw / max(tht, 1)) < 0.15:
                        best[p].append((d, rel, desc, [w, hh]))
            if n % 300 == 0:
                print('%d/%d' % (n, len(files)), file=sys.stderr)
    out = {}
    for p, l in best.items():
        l.sort(key=lambda x: (x[0], abs(x[3][0] - Image.open(p).size[0])))
        out[p] = l[:5]
    json.dump(out, open(outp, 'w'), ensure_ascii=False, indent=1)
    found = sum(1 for l in out.values() if l and l[0][0] <= 4)
    print('missing %d, close candidate (<=4 bits) for %d' % (len(out), found), file=sys.stderr)


if __name__ == '__main__':
    main()
