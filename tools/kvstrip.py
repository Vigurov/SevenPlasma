#!/usr/bin/env python3
"""Make our Kvantum themes shippable: take every embedded picture out of the SVG and write how to get it back.

A picture is either one of AeroThemePlasma's own Kvantum theme (Windows7Aero, installed with ATP) or an exact
cut of an aero.msstyles image (plain or un-premultiplied). The stripped SVG keeps href="sevenplasma:N"
placeholders; extract/kvbuild.py puts the pictures back on the user's machine.

    kvstrip.py THEME.svg ATP_THEME.svg AERO.msstyles OUT_DIR
"""
import base64, io, json, os, re, sys
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from win7index import PE, unpremultiply, pixhash

HREF = re.compile(r'((?:xlink:)?href)="data:image/(png|jpeg);base64,([^"]+)"')


def norm(im):
    im = im.convert('RGBA')
    return bytes(b for px in im.getdata() for b in (px if px[3] else (0, 0, 0, 0)))


def decode(b64):
    im = Image.open(io.BytesIO(base64.b64decode(b64)))
    im.load()
    return im.convert('RGBA')


def find_crop(target, sources):
    """exact position of target inside one of the sources: (source key, box) or None"""
    w, h = target.size
    rows = [norm(target.crop((0, y, w, y + 1))) for y in range(h)]
    k = next((y for y in range(h) if any(rows[y][3::4])), 0)
    for key, im, raw in sources:
        W, H = im.size
        if w > W or h > H:
            continue
        stride = W * 4
        for y in range(k, H - h + k + 1):
            line = raw[y * stride:(y + 1) * stride]
            x = line.find(rows[k])
            while x >= 0:
                if x % 4 == 0:
                    x0, y0 = x // 4, y - k
                    if all(raw[(y0 + j) * stride + x0 * 4:(y0 + j) * stride + (x0 + w) * 4] == rows[j] for j in range(h)):
                        return key, [x0, y0, x0 + w, y0 + h]
                x = line.find(rows[k], x + 1)
    return None


def main():
    svgp, atpp, msp, outd = sys.argv[1:5]
    svg = open(svgp, encoding='utf-8').read()
    atp = {}
    for n, m in enumerate(HREF.finditer(open(atpp, encoding='utf-8').read())):
        try:
            atp.setdefault(pixhash(decode(m.group(3))), n)
        except Exception:
            pass
    pe = PE(open(msp, 'rb').read())
    sources = []
    for t, rid, blob in pe.all():
        if blob[:8] == b'\x89PNG\r\n\x1a\n':
            im = Image.open(io.BytesIO(blob)).convert('RGBA')
            for kind, pic in (('image', im), ('image-unpremul', unpremultiply(im))):
                sources.append(((kind, rid, str(t)), pic, norm(pic)))
    recipes, missing = [], 0
    counter = [0]

    def repl(m):
        n = counter[0]
        counter[0] += 1
        try:
            im = decode(m.group(3))
        except Exception:
            recipes.append({'n': n, 'from': 'keep'})
            return m.group(0)
        ph = pixhash(im)
        if ph in atp:
            recipes.append({'n': n, 'from': 'atp', 'index': atp[ph]})
        else:
            hit = find_crop(im, sources)
            if hit is None:
                recipes.append({'n': n, 'from': 'missing', 'size': list(im.size)})
                nonlocal_missing.append(n)
                return m.group(0)
            (kind, rid, typ), box = hit
            recipes.append({'n': n, 'from': kind, 'id': rid, 'extra': typ, 'crop': box})
        return '%s="sevenplasma:%d"' % (m.group(1), n)

    nonlocal_missing = []
    stripped = HREF.sub(repl, svg)
    os.makedirs(outd, exist_ok=True)
    name = os.path.splitext(os.path.basename(svgp))[0]
    open(os.path.join(outd, name + '.svg'), 'w', encoding='utf-8').write(stripped)
    json.dump(recipes, open(os.path.join(outd, name + '.images.json'), 'w'), indent=0)
    from collections import Counter
    print(name, Counter(r['from'] for r in recipes), 'missing:', nonlocal_missing[:20], file=sys.stderr)


if __name__ == '__main__':
    main()
