#!/usr/bin/env python3
"""Converts the NVIDIA logo mesh (DirectX .x from nvGameS.dll) into a flat triangle list for seven-nvidia:
float32 x, y, z, nx, ny, nz per corner, frame transforms applied."""
import struct, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from xparse import parse

def mat(f):  # .x matrices are row-major, row vectors (v * M)
    return [f[0:4], f[4:8], f[8:12], f[12:16]]

def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

def apply(v, m, w=1.0):
    x, y, z = v
    return tuple(x * m[0][j] + y * m[1][j] + z * m[2][j] + w * m[3][j] for j in range(3))

I = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
tris = []

def walk(o, m):
    local = I
    for c in o.children:
        if c.kind == 'FrameTransformMatrix':
            local = mat(c.floats)
    world = mul(local, m) if o.kind == 'Frame' else m
    for c in o.children:
        if c.kind == 'Frame':
            walk(c, world)
        elif c.kind == 'Mesh':
            mesh(c, world)

def mesh(o, m):
    ints = o.ints
    nv = ints[0]
    verts = [tuple(o.floats[3 * i:3 * i + 3]) for i in range(nv)]
    nf = ints[1]
    faces, p = [], 2
    for _ in range(nf):
        k = ints[p]; faces.append(ints[p + 1:p + 1 + k]); p += 1 + k
    nrm = next(c for c in o.children if c.kind == 'MeshNormals')
    nn = nrm.ints[0]
    normals = [tuple(nrm.floats[3 * i:3 * i + 3]) for i in range(nn)]
    nfaces, p = [], 2
    for _ in range(nrm.ints[1]):
        k = nrm.ints[p]; nfaces.append(nrm.ints[p + 1:p + 1 + k]); p += 1 + k
    for f, nf_ in zip(faces, nfaces):
        for i in range(1, len(f) - 1):   # fan
            for a in (0, i, i + 1):
                v = apply(verts[f[a]], m)
                n = apply(normals[nf_[a]], m, 0.0)
                tris.append(v + n)

root = parse(sys.argv[1])
for c in root.children:
    if c.kind == 'Frame':
        walk(c, I)
    if c.kind == 'Material':
        print('material', [round(x, 3) for x in c.floats])
xs = [t[0] for t in tris]; ys = [t[1] for t in tris]; zs = [t[2] for t in tris]
print('corners', len(tris), 'bbox', (min(xs), max(xs)), (min(ys), max(ys)), (min(zs), max(zs)))
with open(sys.argv[2], 'wb') as f:
    for t in tris:
        f.write(struct.pack('<6f', *t))
