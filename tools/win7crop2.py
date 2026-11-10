#!/usr/bin/env python3
"""Tolerant search for pictures cut out of bigger Windows pictures and placed on a canvas.

Only the opaque part of each target (its alpha bounding box) is searched, with a small colour tolerance, in
every picture of the given PE files (msstyles also un-premultiplied). Fast pre-filter: 24 characteristic
pixels at every position; the best 20 positions are compared in full. Result per target: source resource,
crop box in it, and where the crop goes on the target canvas.

Needs numpy:  uv run --with numpy --with pillow win7crop2.py WINDOWS_ROOT TARGETS.txt OUT.json PE_FILE...
"""
import io, json, os, sys
from multiprocessing import Pool
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from win7index import PE, dib_to_image, unpremultiply, RT_BITMAP

TARGETS = None


def rgba(im):
    a = np.asarray(im.convert('RGBA'), dtype=np.int16).copy()
    a[a[..., 3] == 0] = 0
    return a


def init(paths):
    global TARGETS
    TARGETS = []
    for p in paths:
        im = Image.open(p).convert('RGBA')
        bb = im.getchannel('A').getbbox()
        if not bb:
            continue
        t = rgba(im.crop(bb))
        h, w = t.shape[:2]
        flat = t.reshape(-1, 4)
        # characteristic pixels: the most opaque and most different from the mean
        score = flat[:, 3] + np.abs(flat[:, :3] - flat[:, :3].mean(0)).sum(1)
        idx = np.argsort(-score)[:24]
        ys, xs = idx // w, idx % w
        TARGETS.append((p, bb, t, ys, xs))


def pictures(root, rel):
    try:
        pe = PE(open(os.path.join(root, rel), 'rb').read())
        res = pe.all()
    except Exception:
        return
    for t, rid, blob in res:
        try:
            if t == RT_BITMAP:
                yield dib_to_image(blob), {'from': 'bitmap', 'id': rid}
            elif blob[:8] == b'\x89PNG\r\n\x1a\n':
                im = Image.open(io.BytesIO(blob))
                im.load()
                yield im, {'from': 'image', 'id': rid, 'extra': str(t)}
                if rel.lower().endswith('.msstyles'):
                    yield unpremultiply(im), {'from': 'image-unpremul', 'id': rid, 'extra': str(t)}
        except Exception:
            continue


def work(args):
    root, rel = args
    out = []
    for im, rec in pictures(root, rel):
        if im.size[0] * im.size[1] > 1200 * 1200:
            continue
        c = rgba(im)
        H, W = c.shape[:2]
        for p, bb, t, ys, xs in TARGETS:
            h, w = t.shape[:2]
            if h > H or w > W:
                continue
            ph, pw = H - h + 1, W - w + 1
            # pre-filter on the characteristic pixels at every position
            d = np.zeros((ph, pw), dtype=np.int32)
            for y, x in zip(ys, xs):
                d += np.abs(c[y:y + ph, x:x + pw] - t[y, x]).sum(2)
            flat = np.argsort(d, axis=None)[:20]
            best = None
            for f in flat:
                y0, x0 = divmod(int(f), pw)
                m = float(np.abs(c[y0:y0 + h, x0:x0 + w] - t).mean())
                if best is None or m < best[0]:
                    best = (m, x0, y0)
            if best and best[0] < 4.0:
                out.append((p, best[0], dict(rec, src=rel, crop=[best[1], best[2], best[1] + w, best[2] + h], place=[bb[0], bb[1]])))
    return out


def main():
    root, tp, outp = sys.argv[1:4]
    files = sys.argv[4:]
    targets = [l.strip() for l in open(tp) if l.strip()]
    found = {}
    with Pool(8, initializer=init, initargs=(targets,)) as pool:
        for items in pool.imap_unordered(work, [(root, f) for f in files]):
            for p, m, rec in items:
                if p not in found or m < found[p][0]:
                    found[p] = (m, rec)
    json.dump({p: dict(r, mad=round(m, 2)) for p, (m, r) in found.items()}, open(outp, 'w'), indent=1, ensure_ascii=False)
    print('targets %d, found %d' % (len(targets), len(found)), file=sys.stderr)


if __name__ == '__main__':
    main()
