#!/usr/bin/env python3
"""Find where in Windows 7 each asset of the installed Win7 look came from.

    win7match.py WINDOWS_ROOT INDEX.json OUT.json ASSET_DIR...

Pictures are compared by decoded pixels against INDEX.json (made by win7index.py: resources inside PE files)
and against loose image files of the Windows tree; other files (sounds, fonts) by their bytes. Icons are
matched per frame. The result lists, for every asset file, its source - or nothing when the file was made
or edited by hand (those need their own recipe).
"""
import hashlib, json, os, sys
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from win7index import pixhash

IMG = ('.png', '.gif', '.jpg', '.jpeg', '.bmp')


def loose_index(root):
    """pictures and other files that sit in the Windows tree as files"""
    pix, raw = {}, {}
    for d, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(d, f)
            rel = os.path.relpath(p, root)
            low = f.lower()
            if low.endswith(('.dll', '.exe', '.cpl', '.ocx', '.msstyles', '.mui', '.sys')):
                continue
            try:
                data = open(p, 'rb').read()
            except OSError:
                continue
            raw.setdefault(hashlib.md5(data).hexdigest(), rel)
            if low.endswith(('.wmz', '.zip')):
                import zipfile, io
                try:
                    z = zipfile.ZipFile(p)
                    for name in z.namelist():
                        if name.lower().endswith(IMG):
                            try:
                                zi = Image.open(io.BytesIO(z.read(name)))
                                zi.load()
                                pix.setdefault(pixhash(zi), rel + '!' + name)
                            except Exception:
                                pass
                except Exception:
                    pass
            if low.endswith(IMG):
                try:
                    im = Image.open(p)
                    im.load()
                    pix.setdefault(pixhash(im), rel)
                except Exception:
                    pass
    return pix, raw


def main():
    root, idxp, outp, dirs = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    index = json.load(open(idxp))
    pix, raw = loose_index(root)
    result, stats = {}, {'resource': 0, 'loose': 0, 'bytes': 0, 'none': 0}
    for base in dirs:
        for d, _, fs in os.walk(base):
            for f in fs:
                p = os.path.join(d, f)
                data = open(p, 'rb').read()
                entry = None
                h = hashlib.md5(data).hexdigest()
                if h in raw:
                    entry = {'from': 'file', 'path': raw[h]}
                    stats['bytes'] += 1
                elif f.lower().endswith(IMG):
                    try:
                        im = Image.open(p)
                        im.load()
                        ph = pixhash(im)
                        if ph in index:
                            src = index[ph]
                            # prefer the source that is not a .mui and has the shortest path
                            src.sort(key=lambda s: (s[0].lower().endswith('.mui'), len(s[0])))
                            rel, kind, rid, extra, w, hh = src[0]
                            entry = {'from': kind, 'path': rel, 'id': rid, 'extra': extra, 'size': [w, hh]}
                            stats['resource'] += 1
                        elif ph in pix:
                            entry = {'from': 'file', 'path': pix[ph]}
                            stats['loose'] += 1
                    except Exception:
                        pass
                if entry is None:
                    stats['none'] += 1
                result[p] = entry
    json.dump(result, open(outp, 'w'), ensure_ascii=False, indent=1)
    print(stats)


if __name__ == '__main__':
    main()
