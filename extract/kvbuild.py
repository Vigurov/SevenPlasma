#!/usr/bin/env python3
"""Put the pictures back into SevenPlasma's Kvantum themes (see tools/kvstrip.py).

    kvbuild.py THEME_DIR AERO.msstyles ATP_THEME.svg OUT_DIR

THEME_DIR holds NAME.svg (pictures replaced by href="sevenplasma:N"), NAME.images.json and NAME.kvconfig.
Pictures come from the user's aero.msstyles (cuts, alpha optionally un-premultiplied) or from
AeroThemePlasma's own Windows7Aero theme. Pictures still embedded in the SVG ("keep"/"missing") stay as they are.
"""
import base64, io, json, os, re, shutil, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
from win7index import PE, unpremultiply

HREF = re.compile(r'((?:xlink:)?href)="data:image/(png|jpeg);base64,([^"]+)"')


def main():
    tdir, msp, atpp, outd = sys.argv[1:5]
    name = os.path.basename(os.path.normpath(tdir))
    svg = open(os.path.join(tdir, name + '.svg'), encoding='utf-8').read()
    recipes = {r['n']: r for r in json.load(open(os.path.join(tdir, name + '.images.json')))}
    atp = [m.group(3) for m in HREF.finditer(open(atpp, encoding='utf-8').read())]
    res = {(str(t), rid): blob for t, rid, blob in PE(open(msp, 'rb').read()).all()}
    cache = {}

    def picture(r):
        if r['from'] == 'atp':
            return atp[r['index']]
        key = (r['extra'], r['id'], r['from'])
        if key not in cache:
            im = Image.open(io.BytesIO(res[(r['extra'], r['id'])])).convert('RGBA')
            cache[key] = unpremultiply(im) if r['from'] == 'image-unpremul' else im
        buf = io.BytesIO()
        cache[key].crop(tuple(r['crop'])).save(buf, 'PNG')
        return base64.b64encode(buf.getvalue()).decode()

    def repl(m):
        return '%s="data:image/png;base64,%s"' % (m.group(1), picture(recipes[int(m.group(2))]))

    out = re.sub(r'((?:xlink:)?href)="sevenplasma:(\d+)"', repl, svg)
    os.makedirs(os.path.join(outd, name), exist_ok=True)
    open(os.path.join(outd, name, name + '.svg'), 'w', encoding='utf-8').write(out)
    shutil.copy(os.path.join(tdir, name + '.kvconfig'), os.path.join(outd, name, name + '.kvconfig'))
    print(name, 'ok')


if __name__ == '__main__':
    main()
