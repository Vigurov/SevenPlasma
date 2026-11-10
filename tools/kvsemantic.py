#!/usr/bin/env python3
"""Before kvnear.py: pictures still embedded in our Kvantum themes are resolved by the element they belong to.
  - elements named old-* are not used by Kvantum: their pictures are dropped;
  - an element that AeroThemePlasma's own Windows7Aero theme has too (same id) takes ATP's picture ('atp' recipe),
    unless kvnear's cache knows a near-exact cut of aero.msstyles for it (mean difference below 3).

    kvsemantic.py THEME_DIR... ATP_Windows7Aero.svg
"""
import base64, io, json, os, re, sys

TAG = re.compile(r'<g\b[^>]*>|</g>|<image\b[^>]*?/>|<image\b[^>]*>.*?</image>', re.S)
EMB = re.compile(r'((?:xlink:)?href)="data:image/(?:png|jpeg);base64,([^"]+)"', re.I)
STRICT = re.compile(r'((?:xlink:)?href)="data:image/(png|jpeg);base64,([^"]+)"')


def ident(tag):
    m = re.search(r'\bid="([^"]+)"', tag)
    return m.group(1) if m else None


def atp_index(atp, name):
    """index (in kvbuild's list of ATP pictures) of the first picture of ATP's element NAME"""
    m = re.search(r'\bid="%s"' % re.escape(name), atp)
    if not m:
        return None
    start = atp.rfind('<', 0, m.start())
    tagname = re.match(r'<(\w+)', atp[start:]).group(1)
    if tagname == 'image':
        end = atp.find('>', m.end()) + 1
    else:
        depth, end = 0, None
        for t in re.finditer(r'<g\b[^>]*?(/?)>|</g>', atp[start:]):
            if t.group(0).startswith('</'):
                depth -= 1
            elif not t.group(1):
                depth += 1
            if depth == 0:
                end = start + t.end()
                break
        if end is None:
            return None
    for i, s in enumerate(STRICT.finditer(atp)):
        if start <= s.start() < end:
            return i
    return None


def main():
    atpp, tdirs = sys.argv[-1], sys.argv[1:-1]
    atp = open(atpp, encoding='utf-8').read()
    cachep = os.path.expanduser('~/.cache/sevenplasma-kvnear2.json')
    cache = json.load(open(cachep)) if os.path.exists(cachep) else {}
    for tdir in tdirs:
        name = os.path.basename(os.path.normpath(tdir))
        svgp, recp = os.path.join(tdir, name + '.svg'), os.path.join(tdir, name + '.images.json')
        svg = open(svgp, encoding='utf-8').read()
        recipes = json.load(open(recp))
        free = max(r['n'] for r in recipes) + 1
        out, pos, stack, stats = [], 0, [], {'dropped': 0, 'atp': 0, 'left': 0}
        for m in TAG.finditer(svg):
            t = m.group(0)
            if t.startswith('</g'):
                stack and stack.pop()
                continue
            if t.startswith('<g'):
                if not t.endswith('/>'):
                    stack.append(ident(t))
                continue
            e = EMB.search(t)
            if not e:
                continue
            names = [n for n in (ident(t), stack[-1] if stack else None) if n]
            new = None
            if any(n.startswith('old-') for n in names):
                new = ''
                stats['dropped'] += 1
            else:
                cut = cache.get(e.group(2))
                exact = cut and cut[1][0] < 3
                idx = None if exact else next((i for i in (atp_index(atp, n) for n in names) if i is not None), None)
                if idx is not None:
                    recipes.append({'n': free, 'from': 'atp', 'index': idx})
                    new = t[:e.start()] + '%s="sevenplasma:%d"' % (e.group(1), free) + t[e.end():]
                    free += 1
                    stats['atp'] += 1
                else:
                    stats['left'] += 1
            if new is not None:
                out.append(svg[pos:m.start()])
                out.append(new)
                pos = m.end()
        out.append(svg[pos:])
        open(svgp, 'w', encoding='utf-8').write(''.join(out))
        json.dump(recipes, open(recp, 'w'), indent=0)
        print(name, stats)


if __name__ == '__main__':
    main()
