#!/usr/bin/env python3
"""dlgdump.py <pe-or-mui> [dialog-id] : print DIALOG / DIALOGEX templates (dialog units)"""
import sys, struct
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import winicons

CLS = {0x80: 'BUTTON', 0x81: 'EDIT', 0x82: 'STATIC', 0x83: 'LISTBOX', 0x84: 'SCROLLBAR', 0x85: 'COMBOBOX'}

def sz_or_ord(d, o):
    if struct.unpack_from('<H', d, o)[0] == 0xFFFF:
        return struct.unpack_from('<H', d, o + 2)[0], o + 4
    e = o
    while struct.unpack_from('<H', d, e)[0] != 0:
        e += 2
    return d[o:e].decode('utf-16le'), e + 2

def align(o):
    return (o + 3) & ~3

def parse(d):
    out = {}
    ver, sig = struct.unpack_from('<HH', d, 0)
    if sig != 0xFFFF:
        raise ValueError('not DIALOGEX')
    helpid, exstyle, style, n, x, y, cx, cy = struct.unpack_from('<IIIHhhhh', d, 4)
    o = 26
    menu, o = sz_or_ord(d, o)
    cls, o = sz_or_ord(d, o)
    title, o = sz_or_ord(d, o)
    font = None
    if style & 0x40 or style & 0x48:
        pt, weight, italic, charset = struct.unpack_from('<HHBB', d, o)
        o += 6
        face, o = sz_or_ord(d, o)
        font = (face, pt)
    out = {'title': title, 'rect': (x, y, cx, cy), 'style': hex(style), 'font': font, 'items': []}
    for _ in range(n):
        o = align(o)
        helpid, exs, st, x, y, cx, cy, cid = struct.unpack_from('<IIIhhhhI', d, o)
        o += 24
        c, o = sz_or_ord(d, o)
        t, o = sz_or_ord(d, o)
        extra = struct.unpack_from('<H', d, o)[0]
        o += 2 + extra
        out['items'].append({'class': CLS.get(c, c), 'text': t, 'id': cid, 'rect': (x, y, cx, cy), 'style': hex(st)})
    return out

if __name__ == '__main__':
    pe = winicons.PE(sys.argv[1])
    dl = pe.resources(5)
    for k, v in dl.items():
        if len(sys.argv) > 2 and str(k) != sys.argv[2]:
            continue
        try:
            t = parse(v)
        except Exception as e:
            print(k, 'error', e); continue
        print('DIALOG', k, repr(t['title']), t['rect'], t['font'], t['style'])
        for i in t['items']:
            print('   %-9s %-6s %-22s %r %s' % (i['class'], i['id'], i['rect'], i['text'], i['style']))
