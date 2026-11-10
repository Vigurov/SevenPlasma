#!/usr/bin/env python3
"""PE resource walker that keeps file offsets (for patching icons in place).
resources(path) -> {(type, name, lang): (file_offset, size)}"""
import struct

def resources(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', d, opt)[0]
    dd = opt + (96 if magic == 0x10b else 112)
    rva, size = struct.unpack_from('<II', d, dd + 2 * 8)
    secs = []
    so = opt + optsz
    for i in range(nsec):
        vs, va, rs, rp = struct.unpack_from('<IIII', d, so + 40 * i + 8)
        secs.append((va, vs, rp, rs))

    def off(r):
        for va, vs, rp, rs in secs:
            if va <= r < va + max(vs, rs):
                return r - va + rp

    base = off(rva)

    def name(o):
        n = struct.unpack_from('<H', d, base + o)[0]
        return d[base + o + 2:base + o + 2 + 2 * n].decode('utf-16le')

    out = {}

    def walk(o, path):
        nn, ni = struct.unpack_from('<HH', d, base + o + 12)
        for i in range(nn + ni):
            n, t = struct.unpack_from('<II', d, base + o + 16 + 8 * i)
            key = name(n & 0x7fffffff) if n & 0x80000000 else n
            if t & 0x80000000:
                walk(t & 0x7fffffff, path + [key])
            else:
                r, s = struct.unpack_from('<II', d, base + t)
                out[tuple(path + [key])] = (off(r), s)

    walk(0, [])
    return d, out
