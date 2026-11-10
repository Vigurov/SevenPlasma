#!/usr/bin/env python3
"""Third pass: icons that are Windows icons scaled down (e.g. the 256 px frame of imageres 149 made 32 px).

The largest frame (256, else 48, else 32) of every icon group is resized (Lanczos) to the size of each still
unmatched icon and compared pixel by pixel (mean absolute difference over a grey background).

    win7scaled.py WINDOWS_ROOT NEAR.json OUT.json
"""
import io, json, os, struct, sys
from multiprocessing import Pool
from PIL import Image, ImageChops, ImageStat

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from win7index import PE, dib_to_image, icon_frame, RT_ICON, RT_GROUP_ICON, EXTS

TARGETS = None


def arr(im):
    """the picture on a mid-grey background (so transparent and coloured pixels compare fairly)"""
    im = im.convert('RGBA')
    bg = Image.new('RGBA', im.size, (128, 128, 128, 255))
    bg.alpha_composite(im)
    return bg.convert('RGB')


def mad(a, b):
    return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3


def init(targets):
    global TARGETS
    TARGETS = targets


def work(args):
    root, rel = args
    out = []
    try:
        pe = PE(open(os.path.join(root, rel), 'rb').read())
        res = pe.all()
    except Exception:
        return out
    icons = {i: b for t, i, b in res if t == RT_ICON}
    for t, gid, blob in res:
        if t != RT_GROUP_ICON:
            continue
        best = None
        try:
            for k in range(struct.unpack_from('<H', blob, 4)[0]):
                w, h, cc, _, pl, bpp, size, iid = struct.unpack_from('<BBBBHHIH', blob, 6 + 14 * k)
                w = w or 256
                if bpp < 32 or iid not in icons:
                    continue
                if best is None or w > best[0]:
                    best = (w, iid)
            if best is None:
                continue
            b = icons[best[1]]
            big = icon_frame(b)
            big = big.convert('RGBA')
        except Exception:
            continue
        cache = {}
        for name, size, tarr in TARGETS:
            if size >= best[0]:
                continue
            if size not in cache:
                cache[size] = arr(big.resize((size, size), Image.LANCZOS))
            d = mad(cache[size], tarr)
            if d < 6.0:
                out.append((name, d, rel, gid, best[0], size))
    return out


def main():
    root, nearp, outp = sys.argv[1:4]
    near = json.load(open(nearp))
    targets = []
    for p, l in near.items():
        if 'matchset' not in p or (l and l[0][0] <= 4):
            continue
        im = Image.open(p)
        if im.size[0] != im.size[1]:
            continue
        targets.append((p, im.size[0], arr(im)))
    files = []
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.lower().endswith(EXTS) and not f.lower().endswith('.mui'):
                files.append((root, os.path.relpath(os.path.join(d, f), root)))
    best = {}
    with Pool(8, initializer=init, initargs=(targets,)) as pool:
        for items in pool.imap_unordered(work, files, chunksize=4):
            for name, d, rel, gid, src, size in items:
                if name not in best or d < best[name][0]:
                    best[name] = (d, rel, gid, src, size)
    json.dump(best, open(outp, 'w'), indent=1)
    print('targets %d, scaled match for %d' % (len(targets), len(best)), file=sys.stderr)


if __name__ == '__main__':
    main()
