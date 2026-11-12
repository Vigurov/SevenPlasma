#!/usr/bin/env python3
"""Rebuild the Windows 7 pictures, icons, sounds, fonts and wallpapers SevenPlasma uses from the user's own
copy of Windows 7, following recipes.json (made with tools/win7match.py). Nothing of Microsoft's is shipped.

    seven-extract.py --iso Windows7.iso   DEST      # DEST e.g. ~/.local/share
    seven-extract.py --tree WINDOWS_ROOT  DEST      # an already extracted image (C:\\ of an install)

Every recipe names a file of the Windows tree and, for pictures inside DLL/EXE files, the resource:
  icon (group id + frame "W@bpp"), bitmap, image (PNG/GIF/JPEG resource), image-unpremul (aero.msstyles,
  alpha un-premultiplied), cursor, zip member, carved PNG/ICO of .NET assemblies, or the plain file.
"""
import argparse, io, json, os, re, shutil, struct, subprocess, sys, tempfile, zipfile
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
from win7index import PE, dib_to_image, icon_frame, ico_frames, unpremultiply, cursor_image, RT_ICON, RT_GROUP_ICON, RT_BITMAP

# what has to come out of install.wim for the recipes
WIM_PATHS = ['Windows/System32/*.dll', 'Windows/System32/*.exe', 'Windows/System32/*.cpl', 'Windows/System32/*.ocx',
             'Windows/System32/*.ax', 'Windows/*.exe', 'Windows/Resources/*', 'Windows/Fonts/*', 'Windows/Media/*',
             'Windows/Web/*', 'Program Files/*', 'ProgramData/Microsoft/User Account Pictures/*',
             'Users/Public/Pictures/Sample Pictures/*']


def find_ci(root, rel):
    """the file at rel, matching names case-insensitively (WMPLOC.DLL vs wmploc.DLL)"""
    p = os.path.join(root, rel)
    if os.path.exists(p):
        return p
    cur = root
    for part in rel.split('/'):
        try:
            names = {n.lower(): n for n in os.listdir(cur)}
        except OSError:
            return None
        n = names.get(part.lower())
        if n is None:
            return None
        cur = os.path.join(cur, n)
    return cur


def unpack_iso(iso, work):
    """install.wim of the ISO -> the largest edition, only the paths the recipes need"""
    subprocess.run(['7z', 'e', '-y', '-o' + work, iso, 'sources/install.wim'], check=True, stdout=subprocess.DEVNULL)
    wim = os.path.join(work, 'install.wim')
    info = subprocess.run(['7z', 'l', wim], capture_output=True, text=True).stdout
    images = [int(l.split('=')[1]) for l in info.splitlines() if l.startswith('Images = ')]
    idx = str(images[0]) if images else '1'   # the last edition (Ultimate / Professional) has everything
    subprocess.run(['7z', 'x', '-y', '-ssc-', '-o' + work, wim] + [idx + '/' + p for p in WIM_PATHS],
                   check=True, stdout=subprocess.DEVNULL)
    os.remove(wim)
    return os.path.join(work, idx)


class Sources:
    def __init__(self, root):
        self.root = root
        self.pe = {}

    def resources(self, rel):
        if rel not in self.pe:
            p = find_ci(self.root, rel)
            data = open(p, 'rb').read() if p else None
            pe = PE(data) if data else None
            res = pe.all() if pe else []
            self.pe[rel] = (data, res)
        return self.pe[rel]

    def picture(self, r):
        if r['from'].endswith('-cell'):
            # one icon of a toolbar strip; the strip's colour key (or corner colour) becomes transparent
            base = dict(r, **{'from': r['from'][:-5]})
            cell, k = (int(v) for v in r['cell'].split(':'))
            im = self.picture(base).crop((k * cell, 0, (k + 1) * cell, cell))
            sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
            from win7near import keyed
            return keyed(im)
        if r.get('layers'):
            # pictures drawn over each other (each with its own crop / place), e.g. a button and its glyph
            base = self.picture(r['layers'][0]).convert('RGBA')
            for l in r['layers'][1:]:
                part = self.picture({k: v for k, v in l.items() if k != 'place'}).convert('RGBA')
                base.alpha_composite(part, tuple(l.get('place', [0, 0])))
            return base
        if r.get('key'):
            from win7near import keyed
            return keyed(self.picture(dict(r, key=False)))
        if r.get('crop'):
            # a part of a bigger picture, optionally placed on a transparent canvas of the destination's size
            part = self.picture({k: v for k, v in r.items() if k not in ('crop', 'place', 'canvas')}).convert('RGBA').crop(tuple(r['crop']))
            if r.get('canvas'):
                c = Image.new('RGBA', tuple(r['canvas']))
                c.alpha_composite(part, tuple(r.get('place', [0, 0])))
                return c
            return part
        data, res = self.resources(r['src'])
        kind, rid = r['from'], r.get('id')
        if kind.startswith('carved'):
            blob = data[rid:]
            if kind == 'carved-png':
                return Image.open(io.BytesIO(blob[:blob.find(b'IEND') + 8]))
            if kind == 'carved-bmp':
                return Image.open(io.BytesIO(blob[:struct.unpack_from('<I', blob, 2)[0]]))
            if '@' in r['extra']:            # "32x32@32": that frame of the carved ICO
                size, bpp = r['extra'].split('@')
                w = int(size.split('x')[0])
                for fw, fb, im in ico_frames(data, rid):
                    if fw == w and fb == int(bpp):
                        return im
                raise KeyError(r['extra'])
            ico = Image.open(io.BytesIO(blob[:400000]))
            ico.size = tuple(int(v) for v in r['extra'].split('x'))
            return ico.copy()
        typ = {'icon': RT_GROUP_ICON, 'bitmap': RT_BITMAP, 'cursor': 1}.get(kind)
        for t, i, blob in res:
            if i != rid or (typ is not None and t != typ) or (typ is None and str(t) != r.get('extra', str(t)) and kind != 'zip'):
                continue
            if kind == 'icon':
                icons = {i2: b for t2, i2, b in res if t2 == RT_ICON}
                want = r['extra']
                for k in range(struct.unpack_from('<H', blob, 4)[0]):
                    w, h, cc, _, pl, bpp, size, iid = struct.unpack_from('<BBBBHHIH', blob, 6 + 14 * k)
                    if '%d@%d' % (w or 256, bpp) == want:
                        b = icons[iid]
                        return icon_frame(b)
            elif kind == 'bitmap':
                return dib_to_image(blob)
            elif kind == 'cursor':
                return cursor_image(blob)
            elif kind == 'zip':
                return Image.open(io.BytesIO(zipfile.ZipFile(io.BytesIO(blob)).read(r['extra'])))
            elif kind in ('image', 'image-unpremul'):
                im = Image.open(io.BytesIO(blob))
                im.load()
                return unpremultiply(im) if kind == 'image-unpremul' else im
        raise KeyError('%s %s %s' % (r['src'], kind, rid))

    def file_bytes(self, r):
        src = r['src']
        if r['from'] == 'resource':        # a raw resource (e.g. the WAVE of the startup sound)
            data, res = self.resources(src)
            for t, i, blob in res:
                if i == r['id'] and str(t) == r['extra']:
                    return blob
            raise KeyError(src)
        if '!' in src:
            arc, member = src.split('!', 1)
            return zipfile.ZipFile(find_ci(self.root, arc)).read(member)
        return open(find_ci(self.root, src), 'rb').read()


def save_picture(im, dest):
    # an icon is stored in the folder of its size (icons/hicolor/32x32/...): a bigger source frame is scaled
    m = re.search(r'/(\d+)x(\d+)/', dest)
    if m and '/icons/' in '/' + dest.replace(os.sep, '/') and im.size != (int(m.group(1)), int(m.group(2))):
        im = im.convert('RGBA').resize((int(m.group(1)), int(m.group(2))), Image.LANCZOS)
    ext = os.path.splitext(dest)[1].lower()
    if ext in ('.jpg', '.jpeg'):
        im.convert('RGB').save(dest, quality=95)
    elif ext == '.gif':
        im.save(dest)
    else:
        if im.mode not in ('RGBA', 'RGB', 'L', 'LA', 'P'):
            im = im.convert('RGBA')
        im.save(dest)


def main():
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--iso')
    g.add_argument('--tree')
    ap.add_argument('dest')
    ap.add_argument('--recipes', default=os.path.join(HERE, 'recipes.json'))
    a = ap.parse_args()
    recipes = json.load(open(a.recipes))
    work = None
    if a.iso:
        work = tempfile.mkdtemp(prefix='sevenplasma-')
        print('Читаю образ Windows 7...', flush=True)
        root = unpack_iso(a.iso, work)
    else:
        root = a.tree
    src = Sources(root)
    done, failed = 0, []
    for n, r in enumerate(recipes):
        dest = os.path.join(a.dest, r['dest'])
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        try:
            if r['from'] == 'resource' or r['from'] == 'file' and not dest.lower().endswith(('.png', '.gif', '.jpg', '.jpeg')) or \
               r['from'] == 'file' and os.path.splitext(dest)[1].lower() == os.path.splitext(r['src'])[1].lower():
                open(dest, 'wb').write(src.file_bytes(r))
            elif r['from'] == 'file':
                save_picture(Image.open(io.BytesIO(src.file_bytes(r))), dest)
            else:
                save_picture(src.picture(r), dest)
            done += 1
        except Exception as e:
            failed.append((r['dest'], str(e)))
        if n % 500 == 0:
            print('%d / %d' % (n, len(recipes)), flush=True)
    if work:
        shutil.rmtree(work, ignore_errors=True)
    print('Готово: %d файлів, не вдалося: %d' % (done, len(failed)))
    for d, e in failed[:20]:
        print('  ', d, e)
    return 0 if not failed else 1


if __name__ == '__main__':
    sys.exit(main())
