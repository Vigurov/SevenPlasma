#!/usr/bin/env python3
"""Index every picture inside the PE files (DLL / EXE / CPL / OCX / MSSTYLES) of an extracted Windows 7 tree.

For each icon frame, bitmap and embedded PNG / GIF / JPEG the decoded pixels are hashed, so a picture taken
from Windows once (and possibly re-saved as PNG) can be traced back to the exact file and resource it came
from.  Used to write the extraction recipe of install.sh; the index itself is not shipped.

    win7index.py WINDOWS_ROOT OUT.json
"""
import hashlib, io, json, os, struct, sys
from multiprocessing import Pool
from PIL import Image

Image.MAX_IMAGE_PIXELS = None
EXTS = ('.dll', '.exe', '.cpl', '.ocx', '.msstyles', '.mui', '.ax', '.scr', '.sys')
RT_BITMAP, RT_ICON, RT_GROUP_ICON = 2, 3, 14


def pixhash(im):
    """hash of the picture as RGBA pixels"""
    im = im.convert('RGBA')
    return hashlib.md5(b'%dx%d:' % im.size + im.tobytes()).hexdigest()


def unpremultiply(im):
    """aero.msstyles stores premultiplied alpha; the copies we use were un-premultiplied"""
    im = im.convert('RGBA')
    px = bytearray(im.tobytes())
    for i in range(0, len(px), 4):
        a = px[i + 3]
        if 0 < a < 255:
            for k in range(3):
                px[i + k] = min(255, px[i + k] * 255 // a)
    return Image.frombytes('RGBA', im.size, bytes(px))


class PE:
    def __init__(self, data):
        self.d = d = data
        if d[:2] != b'MZ':
            raise ValueError('not PE')
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        if d[pe:pe + 4] != b'PE\0\0':
            raise ValueError('not PE')
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        optsz = struct.unpack_from('<H', d, pe + 20)[0]
        opt = pe + 24
        ddir = opt + (96 if struct.unpack_from('<H', d, opt)[0] == 0x10b else 112)
        rva = struct.unpack_from('<I', d, ddir + 16)[0]
        self.secs = [struct.unpack_from('<IIII', d, opt + optsz + 40 * i + 8) for i in range(nsec)]
        self.rsrc = self.off(rva) if rva else None

    def off(self, rva):
        for vs, va, rs, ro in self.secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + ro
        raise ValueError(rva)

    def entries(self, off):
        n1, n2 = struct.unpack_from('<HH', self.d, off + 12)
        out = []
        for i in range(n1 + n2):
            name, target = struct.unpack_from('<II', self.d, off + 16 + 8 * i)
            if name & 0x80000000:
                p = self.rsrc + (name & 0x7fffffff)
                ln = struct.unpack_from('<H', self.d, p)[0]
                name = self.d[p + 2:p + 2 + 2 * ln].decode('utf-16le')
            out.append((name, target))
        return out

    def all(self):
        """[(type, id, bytes)] of every resource (first language)"""
        if self.rsrc is None:
            return []
        res = []
        for tname, t in self.entries(self.rsrc):
            if not t & 0x80000000:
                continue
            for rid, t2 in self.entries(self.rsrc + (t & 0x7fffffff)):
                if not t2 & 0x80000000:
                    continue
                for lang, t3 in self.entries(self.rsrc + (t2 & 0x7fffffff)):
                    rva, size = struct.unpack_from('<II', self.d, self.rsrc + t3)
                    try:
                        o = self.off(rva)
                    except ValueError:
                        break
                    res.append((tname, rid, self.d[o:o + size]))
                    break
        return res


def rle8_image(blob, hs, w, h, ncol):
    """a BI_RLE8 DIB as a palette image; pixels the stream never reaches stay 0 (as Windows and older Pillow)"""
    pal = blob[hs:hs + ncol * 4]
    px = bytearray(w * abs(h))
    i, x, y = hs + ncol * 4, 0, 0
    while i + 1 < len(blob) and y < abs(h):
        n, v = blob[i], blob[i + 1]
        i += 2
        if n:
            for _ in range(n):
                if x < w:
                    px[y * w + x] = v
                x += 1
        elif v == 0:      # end of line
            x, y = 0, y + 1
        elif v == 1:      # end of bitmap
            break
        elif v == 2:      # delta
            if i + 1 >= len(blob):
                break
            x, y = x + blob[i], y + blob[i + 1]
            i += 2
        else:             # absolute run of v pixels, padded to a word
            for k in range(v):
                if i + k < len(blob) and x < w:
                    px[y * w + x] = blob[i + k]
                x += 1
            i += v + (v & 1)
    im = Image.frombytes('P', (w, abs(h)), bytes(px))
    if h > 0:
        im = im.transpose(Image.FLIP_TOP_BOTTOM)
    palette = bytearray()
    for k in range(0, len(pal), 4):
        palette += bytes((pal[k + 2], pal[k + 1], pal[k]))
    im.putpalette(bytes(palette) + bytes(768 - len(palette)))
    return im


def dib_to_image(blob, icon=False):
    """a DIB (BITMAPINFOHEADER + pixels) as a PIL image; icons have a doubled height with an AND mask"""
    hs = struct.unpack_from('<I', blob)[0]
    w, h, planes, bpp = struct.unpack_from('<iiHH', blob, 4)
    if icon:
        h //= 2
    if bpp == 32 and icon:
        px = blob[hs:hs + w * abs(h) * 4]
        return Image.frombuffer('RGBA', (w, abs(h)), px, 'raw', 'BGRA', 0, -1).copy()
    ncol = struct.unpack_from('<I', blob, 32)[0] if hs >= 36 else 0
    if bpp <= 8 and ncol == 0:
        ncol = 1 << bpp
    pal = ncol * 4
    comp = struct.unpack_from('<I', blob, 16)[0]
    if comp == 3:   # BI_BITFIELDS masks follow the header
        pal += 12
    # a few bitmaps in Windows end early (Windows draws them anyway): the missing rows are zeros, as older
    # Pillow assumed (Pillow 13 refuses short data)
    if comp in (0, 3):
        need = hs + pal + ((w * bpp + 31) // 32) * 4 * abs(h)
        if len(blob) < need:
            blob = blob + bytes(need - len(blob))
    elif comp == 1:   # RLE8: decoded here (Pillow 13 refuses the ones in Windows that end without a marker)
        return rle8_image(blob, hs, w, h, ncol)
    fh = struct.pack('<2sIHHI', b'BM', 14 + len(blob), 0, 0, 14 + hs + pal)
    if icon:
        hdr = bytearray(blob[:hs])
        struct.pack_into('<i', hdr, 8, h)
        blob = bytes(hdr) + blob[hs:]
    im = Image.open(io.BytesIO(fh + blob))
    im.load()
    if bpp == 32 and not icon:
        # 32-bit bitmaps carry alpha in the 4th byte (PIL reads them as RGB)
        raw = blob[hs + pal:hs + pal + w * abs(h) * 4]
        if len(raw) == w * abs(h) * 4:
            im = Image.frombuffer('RGBA', (w, abs(h)), raw, 'raw', 'BGRA', 0, -1 if h > 0 else 1).copy()
    return im


def icon_frame(blob):
    """an RT_ICON frame as RGBA: PNG, 32-bit DIB with alpha, or a palette DIB made transparent by its AND mask"""
    if blob[:8] == b'\x89PNG\r\n\x1a\n':
        return Image.open(io.BytesIO(blob))
    bpp = struct.unpack_from('<H', blob, 14)[0]
    if bpp == 32:
        return dib_to_image(blob, icon=True)
    return cursor_image(b'\0\0\0\0' + blob)


def ico_frames(data, off):
    """[(w, bpp, RGBA image)] of an ICO file that starts at data[off] (all frames, decoded like icon groups)"""
    out = []
    n = struct.unpack_from('<H', data, off + 4)[0]
    for k in range(n):
        w, h, cc, _, pl, bpp, size, o = struct.unpack_from('<BBBBHHII', data, off + 6 + 16 * k)
        # random bytes that only look like an ICO header: sizes and offsets must make sense
        if w != h or not 0 < size < 300000 or not 6 + 16 * n <= o < 300000 or off + o + size > len(data):
            continue
        blob = data[off + o:off + o + size]
        if blob[:8] != b'\x89PNG\r\n\x1a\n' and (len(blob) < 40 or struct.unpack_from('<I', blob)[0] != 40):
            continue
        try:
            if blob[:8] != b'\x89PNG\r\n\x1a\n':
                bpp = struct.unpack_from('<H', blob, 14)[0]
            out.append((w or 256, bpp, icon_frame(blob).convert('RGBA')))
        except Exception:
            pass
    return out


def cursor_image(blob):
    """RT_CURSOR: hotspot (2 x WORD) + DIB of doubled height (XOR image, then the 1-bit AND mask)"""
    dib = blob[4:]
    hs = struct.unpack_from('<I', dib)[0]
    w, h2, planes, bpp = struct.unpack_from('<iiHH', dib, 4)
    h = h2 // 2
    if bpp == 32:
        return dib_to_image(dib, icon=True)
    im = dib_to_image(dib, icon=True).convert('RGBA')
    ncol = struct.unpack_from('<I', dib, 32)[0] or (1 << bpp if bpp <= 8 else 0)
    xor_row = ((w * bpp + 31) // 32) * 4
    and_row = ((w + 31) // 32) * 4
    and_off = hs + ncol * 4 + xor_row * h
    mask = dib[and_off:and_off + and_row * h]
    px = im.load()
    for y in range(h):
        row = mask[(h - 1 - y) * and_row:(h - y) * and_row]
        for x in range(w):
            if row and row[x // 8] >> (7 - x % 8) & 1:
                r, g, b, a = px[x, y]
                px[x, y] = (r, g, b, 0) if (r, g, b) == (0, 0, 0) else (255 - r, 255 - g, 255 - b, 255)
    return im


def index_file(args):
    root, rel = args
    out = []
    try:
        pe = PE(open(os.path.join(root, rel), 'rb').read())
        res = pe.all()
    except Exception:
        return out
    icons = {rid: blob for t, rid, blob in res if t == RT_ICON}

    def add(im, kind, rid, extra=''):
        try:
            out.append((pixhash(im), rel, kind, rid, extra, im.size[0], im.size[1]))
        except Exception:
            pass

    # .NET assemblies keep their pictures in managed .resources, not in the Win32 resource tree:
    # carve every embedded PNG stream (and ICO file) out of the raw file
    raw = pe.d
    if b'BSJB' in raw:
        pos = 0
        while True:
            i = raw.find(b'\x89PNG\r\n\x1a\n', pos)
            if i < 0:
                break
            j = raw.find(b'IEND', i)
            if j < 0:
                break
            try:
                im = Image.open(io.BytesIO(raw[i:j + 8]))
                im.load()
                add(im, 'carved-png', i)
            except Exception:
                pass
            pos = j + 8
        # serialized System.Drawing.Bitmap: a whole BMP file
        pos = 0
        while True:
            i = raw.find(b'BM', pos)
            if i < 0 or i + 30 > len(raw):
                break
            pos = i + 2
            size, _, off = struct.unpack_from('<IIi', raw, i + 2)
            hs = struct.unpack_from('<I', raw, i + 14)[0]
            if hs not in (40, 108, 124) or not 0 < size < 4000000 or not 0 < off < size:
                continue
            try:
                im = Image.open(io.BytesIO(raw[i:i + size]))
                im.load()
                add(im, 'carved-bmp', i)
            except Exception:
                pass
        pos = 0
        while True:
            i = raw.find(b'\x00\x00\x01\x00', pos)
            if i < 0:
                break
            pos = i + 4
            n = struct.unpack_from('<H', raw, i + 4)[0] if i + 6 <= len(raw) else 0
            if not 0 < n < 16:
                continue
            try:
                for w, bpp, fr in ico_frames(raw, i):
                    add(fr, 'carved-ico', i, '%dx%d@%d' % (w, w, bpp))
            except Exception:
                pass

    for t, rid, blob in res:
        try:
            if t == RT_GROUP_ICON:
                for i in range(struct.unpack_from('<H', blob, 4)[0]):
                    w, h, cc, _, pl, bpp, size, iid = struct.unpack_from('<BBBBHHIH', blob, 6 + 14 * i)
                    b = icons.get(iid)
                    if not b:
                        continue
                    add(icon_frame(b), 'icon', rid, '%d@%d' % (w or 256, bpp))
            elif t == RT_BITMAP:
                im = dib_to_image(blob)
                add(im, 'bitmap', rid)
            elif t == 1:   # RT_CURSOR
                add(cursor_image(blob), 'cursor', rid)
            elif blob[:4] == b'PK\x03\x04':   # a zip inside a resource (skins, gadgets)
                import zipfile
                z = zipfile.ZipFile(io.BytesIO(blob))
                for name in z.namelist():
                    if name.lower().endswith(('.png', '.gif', '.jpg', '.bmp')):
                        try:
                            zi = Image.open(io.BytesIO(z.read(name)))
                            zi.load()
                            add(zi, 'zip', rid, name)
                        except Exception:
                            pass
            elif blob[:8] == b'\x89PNG\r\n\x1a\n' or blob[:6] in (b'GIF87a', b'GIF89a') or blob[:3] == b'\xff\xd8\xff':
                im = Image.open(io.BytesIO(blob))
                im.load()
                add(im, 'image', rid, str(t))
                if rel.lower().endswith('.msstyles') and blob[:4] == b'\x89PNG':
                    add(unpremultiply(im), 'image-unpremul', rid, str(t))
        except Exception:
            continue
    return out


def main():
    root, outp = sys.argv[1], sys.argv[2]
    files = []
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.lower().endswith(EXTS):
                files.append((root, os.path.relpath(os.path.join(d, f), root)))
    index = {}
    with Pool(8) as pool:
        for n, items in enumerate(pool.imap_unordered(index_file, files, chunksize=4)):
            for h, rel, kind, rid, extra, w, hh in items:
                index.setdefault(h, []).append([rel, kind, rid, extra, w, hh])
            if n % 200 == 0:
                print('%d/%d files, %d pictures' % (n, len(files), len(index)), file=sys.stderr)
    json.dump(index, open(outp, 'w'))
    print('done: %d files, %d distinct pictures' % (len(files), len(index)), file=sys.stderr)


if __name__ == '__main__':
    main()
