#!/usr/bin/env python3
"""First pass before kvsemantic.py / kvnear.py: an element of our Kvantum theme that is a picture made from Windows
parts which no Windows resource gives back (kvnear's mean difference above LIMIT), while AeroThemePlasma's own
Windows7Aero theme draws the same element (same id) as vectors, takes ATP's vector drawing instead. ATP's drawing
is AGPL like this project, so it is copied into the repository with the gradients and clip paths it uses (their
ids get an "atpv-" prefix so they cannot clash with ours).

    kvvector.py THEME_DIR... ATP_Windows7Aero.svg
"""
import json, os, re, sys

LIMIT = 10
EMB = re.compile(r'href="data:image/(?:png|jpeg);base64,([^"]+)"', re.I)


def element(svg, name):
    """(start, end) of the element with id NAME"""
    m = re.search(r'\bid="%s"' % re.escape(name), svg)
    if not m:
        return None
    start = svg.rfind('<', 0, m.start())
    tag = re.match(r'<([\w:]+)', svg[start:]).group(1)
    head_end = svg.find('>', m.end())
    if svg[head_end - 1] == '/':
        return start, head_end + 1
    depth = 0
    for t in re.finditer(r'<(/?)%s\b[^>]*?(/?)>' % re.escape(tag), svg[start:]):
        if t.group(1):
            depth -= 1
        elif not t.group(2):
            depth += 1
        if depth == 0:
            return start, start + t.end()
    return None


def refs(text):
    return set(re.findall(r'url\(#([^)]+)\)', text)) | set(re.findall(r'href="#([^"]+)"', text))


def main():
    atpp, tdirs = sys.argv[-1], sys.argv[1:-1]
    atp = open(atpp, encoding='utf-8').read()
    cache = json.load(open(os.path.expanduser('~/.cache/sevenplasma-kvnear2.json')))
    for tdir in tdirs:
        name = os.path.basename(os.path.normpath(tdir))
        svgp = os.path.join(tdir, name + '.svg')
        svg = open(svgp, encoding='utf-8').read()
        # elements (innermost <g> with an id, or the <image> itself) holding a badly matched picture
        todo = []
        for m in EMB.finditer(svg):
            cut = cache.get(m.group(1))
            if cut and cut[1][0] <= LIMIT:
                continue
            tagstart = svg.rfind('<image', 0, m.start())
            iid = re.search(r'\bid="([^"]+)"', svg[tagstart:m.start()])
            cands = [iid.group(1)] if iid else []
            # enclosing <g id=...>
            depth, pos = 0, tagstart
            for t in reversed(list(re.finditer(r'<g\b[^>]*>|</g>', svg[:tagstart]))):
                if t.group(0).startswith('</'):
                    depth += 1
                elif depth:
                    depth -= 1
                else:
                    g = re.search(r'\bid="([^"]+)"', t.group(0))
                    if g:
                        cands.append(g.group(1))
                    break
            for c in cands:
                if c.startswith('old-'):
                    break
                span = element(atp, c)
                if span and not EMB.search(atp[span[0]:span[1]]) and element(svg, c):
                    todo.append(c)
                    break
        todo = list(dict.fromkeys(todo))
        defs, seen = [], set()
        for c in todo:
            s0, s1 = element(atp, c)
            body = atp[s0:s1]
            stack = list(refs(body))
            while stack:
                r = stack.pop()
                if r in seen:
                    continue
                seen.add(r)
                span = element(atp, r)
                if span:
                    d = atp[span[0]:span[1]]
                    defs.append(d)
                    stack.extend(refs(d))
            o0, o1 = element(svg, c)
            svg = svg[:o0] + body + svg[o1:]
        if todo:
            block = '<defs id="atpv-defs">' + ''.join(defs) + '</defs>'
            svg = re.sub(r'(<svg\b[^>]*>)', lambda m: m.group(1) + block, svg, count=1)
            # prefix the copied ids and their references
            def pre(text):
                for r in seen:
                    text = re.sub(r'\bid="%s"' % re.escape(r), 'id="atpv-%s"' % r, text)
                    text = text.replace('url(#%s)' % r, 'url(#atpv-%s)' % r).replace('href="#%s"' % r, 'href="#atpv-%s"' % r)
                return text
            a = svg.index('<defs id="atpv-defs">')
            b = svg.index('</defs>', a) + len('</defs>')
            svg = svg[:a] + pre(svg[a:b]) + svg[b:]
            for c in todo:
                s0, s1 = element(svg, c)
                svg = svg[:s0] + pre(svg[s0:s1]) + svg[s1:]
        open(svgp, 'w', encoding='utf-8').write(svg)
        print(name, 'vector from ATP:', todo)


if __name__ == '__main__':
    main()
