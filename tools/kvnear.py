#!/usr/bin/env python3
"""Last pass for our Kvantum themes: pictures kvstrip.py could not find exactly (our compositions of aero.msstyles
parts, e.g. a scroll bar button with its arrow) get the closest cut of aero.msstyles of the same size instead, so no
picture stays embedded in the repository. Prints the mean difference of every replacement (0-255 per channel).

    uv run --with numpy --with pillow kvnear.py THEME_DIR... ATP_Windows7Aero.svg AERO.msstyles
"""
import base64, io, json, os, re, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from win7index import PE, unpremultiply

TOKEN = re.compile(r'((?:xlink:)?href)="(?:sevenplasma:(\d+)|data:image/(?:png|jpeg);base64,([^"]+))"', re.I)


def b64clean(b):
    return re.sub(r'&#10;|\s', '', b)


def grey(im):
    """RGBA on mid grey, as float array (transparent and coloured pixels compare fairly)"""
    a = np.asarray(im.convert('RGBA'), dtype=np.float32)
    al = a[..., 3:4] / 255.0
    return a[..., :3] * al + 128.0 * (1 - al)


def best_cut(target, sources):
    t = grey(target)
    h, w = t.shape[:2]
    best = (1e9, None, None)
    for key, s in sources:
        H, W = s.shape[:2]
        if h > H or w > W:
            continue
        acc = np.zeros((H - h + 1, W - w + 1), dtype=np.float32)
        for j in range(h):
            for i in range(w):
                acc += np.abs(s[j:j + H - h + 1, i:i + W - w + 1] - t[j, i]).sum(axis=2)
        k = int(acc.argmin())
        y, x = divmod(k, acc.shape[1])
        d = float(acc.flat[k]) / (h * w * 3)
        if d < best[0]:
            best = (d, key, [x, y, x + w, y + h])
    return best


SOURCES = None


def init(sources, atp):
    global SOURCES
    SOURCES = sources
    ATP[:] = atp


ATP = []


def work(b64):
    im = Image.open(io.BytesIO(base64.b64decode(b64clean(b64)))).convert('RGBA')
    cut = best_cut(im, SOURCES)
    t = grey(im)
    for i, a in ATP:   # a whole picture of AeroThemePlasma's own Kvantum theme (installed with ATP)
        if a.shape == t.shape:
            d = float(np.abs(a - t).mean())
            if d < cut[0]:
                cut = (d, ['atp', i, ''], None)
    return b64, im.size, cut


def main():
    from multiprocessing import Pool
    atpp, msp, tdirs = sys.argv[-2], sys.argv[-1], sys.argv[1:-2]
    # the same list kvbuild.py indexes ('atp' recipes)
    strict = re.compile(r'((?:xlink:)?href)="data:image/(png|jpeg);base64,([^"]+)"')
    for i, m in enumerate(strict.finditer(open(atpp, encoding='utf-8').read())):
        try:
            ATP.append((i, grey(Image.open(io.BytesIO(base64.b64decode(m.group(3)))))))
        except Exception:
            pass
    sources = []
    for t, rid, blob in PE(open(msp, 'rb').read()).all():
        if blob[:8] == b'\x89PNG\r\n\x1a\n':
            im = Image.open(io.BytesIO(blob)).convert('RGBA')
            for kind, pic in (('image', im), ('image-unpremul', unpremultiply(im))):
                sources.append(((kind, rid, str(t)), grey(pic)))
    themes = []
    for tdir in tdirs:
        name = os.path.basename(os.path.normpath(tdir))
        svgp, recp = os.path.join(tdir, name + '.svg'), os.path.join(tdir, name + '.images.json')
        themes.append((name, svgp, recp, open(svgp, encoding='utf-8').read(), json.load(open(recp))))
    pics = sorted({m.group(3) for th in themes for m in TOKEN.finditer(th[3]) if m.group(3)})
    print(len(pics), 'different pictures', flush=True)
    cachep = os.path.join(os.path.expanduser('~/.cache'), 'sevenplasma-kvnear2.json')
    found = json.load(open(cachep)) if os.path.exists(cachep) else {}
    todo = [b for b in pics if b not in found]
    if todo:
        with Pool(os.cpu_count(), initializer=init, initargs=(sources, ATP)) as pool:
            for b, size, cut in pool.imap_unordered(work, todo):
                found[b] = (size, cut)
        json.dump(found, open(cachep, 'w'))
    for name, svgp, recp, svg, recipes in themes:
        free = [max(r['n'] for r in recipes) + 1]

        def repl(m):
            if m.group(2) is not None:
                return m.group(0)
            n = free[0]
            free[0] += 1
            size, (d, key, box) = found[m.group(3)]
            if key is None:
                print('  %s #%d %s: no cut of that size' % (name, n, size))
                return m.group(0)
            kind, rid, typ = key
            if kind == 'atp':
                recipes.append({'n': n, 'from': 'atp', 'index': rid, 'approx': round(d, 1)})
            else:
                recipes.append({'n': n, 'from': kind, 'id': rid, 'extra': typ, 'crop': box, 'approx': round(d, 1)})
            print('  %s #%d %s -> %s %s %s diff %.1f' % (name, n, size, kind, rid, box, d))
            return '%s="sevenplasma:%d"' % (m.group(1), n)

        out = TOKEN.sub(repl, svg)
        recipes[:] = [r for r in recipes if r['from'] not in ('missing', 'keep')]
        open(svgp, 'w', encoding='utf-8').write(out)
        json.dump(recipes, open(recp, 'w'), indent=0)
        print(name, 'embedded left:', len(re.findall(r'data:image', out, re.I)), flush=True)


if __name__ == '__main__':
    main()
