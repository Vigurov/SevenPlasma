#!/usr/bin/env python3
"""Last pass: for the icons still without a recipe, the closest genuine Windows picture of any kind and size.

Every picture of the tree (icon frames, bitmaps, strip cells, pictures carved from .NET assemblies) whose
aspect fits is resized to the target size, its colour key made transparent, and compared on grey
(mean absolute difference). The best three per target are written out with a ready recipe.

    win7best.py WINDOWS_ROOT TARGETS.json OUT.json      (TARGETS: [[path, name], ...])
"""
import json, os, sys
from multiprocessing import Pool
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from win7index import EXTS
from win7near import pictures, keyed
from win7scaled import arr, mad

TARGETS = None


def init(t):
    global TARGETS
    TARGETS = [(p, n, Image.open(p).size, arr(Image.open(p))) for p, n in t]


def work(args):
    root, rel = args
    out = []
    for im, desc in pictures(root, rel):
        try:
            w, h = im.size
            if w < 8 or h < 8 or w > 512 or h > 512:
                continue
            k = None
            cache = {}
            for p, n, (tw, th), ta in TARGETS:
                if abs(w / h - tw / th) > 0.08 or w < tw * 0.45:
                    continue
                if k is None:
                    k = keyed(im)
                if (tw, th) not in cache:
                    cache[(tw, th)] = arr(k if (w, h) == (tw, th) else k.resize((tw, th), Image.LANCZOS))
                d = mad(cache[(tw, th)], ta)
                if d < 25:
                    out.append((p, d, rel, desc, [w, h]))
        except Exception:
            continue
    return out


def main():
    root, tp, outp = sys.argv[1:4]
    targets = json.load(open(tp))
    files = []
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.lower().endswith(EXTS) and not f.lower().endswith('.mui'):
                files.append((root, os.path.relpath(os.path.join(d, f), root)))
    best = {}
    with Pool(8, initializer=init, initargs=(targets,)) as pool:
        for n, items in enumerate(pool.imap_unordered(work, files, chunksize=4)):
            for p, d, rel, desc, size in items:
                l = best.setdefault(p, [])
                l.append((round(d, 2), rel, desc, size))
                l.sort(key=lambda x: x[0])
                del l[3:]
            if n % 500 == 0:
                print('%d/%d' % (n, len(files)), file=sys.stderr)
    json.dump(best, open(outp, 'w'), indent=1)
    print('targets %d, with a candidate %d' % (len(targets), len(best)), file=sys.stderr)


if __name__ == '__main__':
    main()
