#!/usr/bin/env python3
"""Find pictures that were cut out of bigger Windows pictures (msstyles state images, ribbon strips...).

For each target the pixels are searched inside every picture resource of the given PE files (as stored and,
for msstyles, un-premultiplied), comparing RGBA with fully transparent pixels normalised. The hit gives a
recipe: resource + crop box.

    win7crop.py WINDOWS_ROOT TARGETS.txt OUT.json PE_FILE...
"""
import io, json, os, sys
from multiprocessing import Pool
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from win7index import PE, dib_to_image, unpremultiply, icon_frame, RT_BITMAP, RT_ICON, RT_GROUP_ICON

TARGETS = None


def norm(im):
    im = im.convert('RGBA')
    return bytes(b for px in im.getdata() for b in (px if px[3] else (0, 0, 0, 0)))


def init(t):
    global TARGETS
    TARGETS = []
    for p in t:
        im = Image.open(p).convert('RGBA')
        w, h = im.size
        raw = norm(im)
        rows = [raw[y * w * 4:(y + 1) * w * 4] for y in range(h)]
        # the first row that is not empty is the search key
        k = next((y for y in range(h) if any(rows[y][3::4])), 0)
        TARGETS.append((p, w, h, rows, k))


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
        W, H = im.size
        raw = None
        for p, w, h, rows, k in TARGETS:
            if w > W or h > H:
                continue
            if raw is None:
                raw = norm(im)
            stride = W * 4
            key = rows[k]
            for y in range(k, H - h + k + 1):
                line = raw[y * stride:(y + 1) * stride]
                x = line.find(key)
                while x >= 0:
                    if x % 4 == 0:
                        x0, y0 = x // 4, y - k
                        if all(raw[(y0 + j) * stride + x0 * 4:(y0 + j) * stride + (x0 + w) * 4] == rows[j] for j in range(h)):
                            out.append((p, dict(rec, src=rel, crop=[x0, y0, x0 + w, y0 + h])))
                            break
                    x = line.find(key, x + 1)
                else:
                    continue
                break
    return out


def main():
    root, tp, outp = sys.argv[1:4]
    files = sys.argv[4:]
    targets = [l.strip() for l in open(tp) if l.strip()]
    found = {}
    with Pool(8, initializer=init, initargs=(targets,)) as pool:
        for items in pool.imap_unordered(work, [(root, f) for f in files]):
            for p, rec in items:
                found.setdefault(p, rec)
    json.dump(found, open(outp, 'w'), indent=1, ensure_ascii=False)
    print('targets %d, found %d' % (len(targets), len(found)), file=sys.stderr)


if __name__ == '__main__':
    main()
