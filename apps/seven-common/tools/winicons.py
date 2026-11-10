"""Read Windows icons: .ico files and icon groups inside PE files (DLL/EXE), 32-bit frames as Pillow images."""
import struct, io
from PIL import Image

def decode(w, bpp, blob):
    if blob is None:
        return None
    if blob[:8] == b'\x89PNG\r\n\x1a\n':
        return Image.open(io.BytesIO(blob)).convert('RGBA')
    if bpp != 32:
        return None
    hs = struct.unpack_from('<I', blob)[0]
    px = blob[hs:hs + w * w * 4]
    if len(px) < w * w * 4:
        return None
    return Image.frombuffer('RGBA', (w, w), px, 'raw', 'BGRA', 0, -1).copy()

def ico_frames(path):
    """[(size, image)] of the 32-bit frames of an .ico file"""
    d = open(path, 'rb').read()
    out = []
    for i in range(struct.unpack_from('<H', d, 4)[0]):
        w, h, cc, _, pl, bpp, size, off = struct.unpack_from('<BBBBHHII', d, 6 + 16 * i)
        w = w or 256
        im = decode(w, bpp, d[off:off + size])
        if im:
            out.append((w, im))
    return out

class PE:
    def __init__(self, path):
        self.d = d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        optsz = struct.unpack_from('<H', d, pe + 20)[0]
        opt = pe + 24
        ddir = opt + (96 if struct.unpack_from('<H', d, opt)[0] == 0x10b else 112)
        rva = struct.unpack_from('<I', d, ddir + 16)[0]
        self.secs = [struct.unpack_from('<IIII', d, opt + optsz + 40 * i + 8) for i in range(nsec)]
        self.rsrc = self.off(rva)

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

    def resources(self, rtype):
        res = {}
        for name, t in self.entries(self.rsrc):
            if name != rtype:
                continue
            for rid, t2 in self.entries(self.rsrc + (t & 0x7fffffff)):
                for lang, t3 in self.entries(self.rsrc + (t2 & 0x7fffffff)):
                    rva, size = struct.unpack_from('<II', self.d, self.rsrc + t3)
                    o = self.off(rva)
                    res[rid] = self.d[o:o + size]
                    break
        return res

    def icon(self, gid):
        """[(size, image)] of icon group `gid`"""
        icons = self.resources(3)
        data = self.resources(14)[gid]
        out = []
        for i in range(struct.unpack_from('<H', data, 4)[0]):
            w, h, cc, _, pl, bpp, size, iid = struct.unpack_from('<BBBBHHIH', data, 6 + 14 * i)
            im = decode(w or 256, bpp, icons.get(iid))
            if im:
                out.append((w or 256, im))
        return out

def install(name, frames, sizes=(16, 20, 22, 24, 32, 40, 48, 64, 96, 128, 256), context='apps'):
    import os
    base = os.path.expanduser('~/.local/share/icons/hicolor/')
    done = []
    for w, im in frames:
        if w in sizes:
            os.makedirs(f'{base}{w}x{w}/{context}', exist_ok=True)
            im.save(f'{base}{w}x{w}/{context}/{name}.png')
            done.append(w)
    return sorted(set(done))
