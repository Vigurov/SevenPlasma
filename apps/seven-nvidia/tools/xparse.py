#!/usr/bin/env python3
"""Reads a binary DirectX .x file (as in nvGameS.dll, resource X/1000) into a tree of data objects."""
import struct, sys

def tokens(b):
    pos = 16
    while pos < len(b):
        t, = struct.unpack_from('<H', b, pos); pos += 2
        if t == 1:
            n, = struct.unpack_from('<I', b, pos); pos += 4
            yield ('NAME', b[pos:pos + n].decode('latin1')); pos += n
        elif t == 2:
            n, = struct.unpack_from('<I', b, pos); pos += 4
            s = b[pos:pos + n].decode('latin1'); pos += n
            pos += 2  # terminator token
            yield ('STRING', s)
        elif t == 3:
            v, = struct.unpack_from('<I', b, pos); pos += 4
            yield ('INT', v)
        elif t == 5:
            yield ('GUID', b[pos:pos + 16]); pos += 16
        elif t == 6:
            n, = struct.unpack_from('<I', b, pos); pos += 4
            yield ('INTS', list(struct.unpack_from('<%dI' % n, b, pos))); pos += 4 * n
        elif t == 7:
            n, = struct.unpack_from('<I', b, pos); pos += 4
            yield ('FLOATS', list(struct.unpack_from('<%df' % n, b, pos))); pos += 4 * n
        else:
            yield ('SYM', t)

class Obj:
    def __init__(s, kind, name):
        s.kind, s.name, s.children, s.ints, s.floats, s.strings = kind, name, [], [], [], []
    def __repr__(s):
        return '%s(%s) ints=%d floats=%d strings=%s children=%d' % (s.kind, s.name, len(s.ints), len(s.floats), s.strings[:3], len(s.children))

def parse(path):
    b = open(path, 'rb').read()
    root = Obj('root', '')
    stack = [root]
    pending = []
    in_template = 0
    for tok in tokens(b):
        k, v = tok
        if k == 'SYM' and v == 31:          # template
            in_template = 1
            continue
        if in_template:
            if k == 'SYM' and v == 10:
                in_template += 1
            elif k == 'SYM' and v == 11:
                in_template -= 1
                if in_template == 1:
                    in_template = 0
            continue
        if k == 'NAME':
            pending.append(v)
        elif k == 'SYM' and v == 10:          # {
            if pending:
                o = Obj(pending[0], pending[1] if len(pending) > 1 else '')
                stack[-1].children.append(o)
                stack.append(o)
            else:
                stack.append(Obj('ref', ''))  # { name } reference
            pending = []
        elif k == 'SYM' and v == 11:          # }
            if pending and stack[-1].kind == 'ref':
                stack[-1].name = pending[0]
                stack[-2].children.append(stack[-1]) if len(stack) > 1 else None
            pending = []
            stack.pop()
        elif k == 'INTS':
            stack[-1].ints += v
        elif k == 'INT':
            stack[-1].ints.append(v)
        elif k == 'FLOATS':
            stack[-1].floats += v
        elif k == 'STRING':
            stack[-1].strings.append(v)
    return root

def dump(o, d=0):
    print('  ' * d + repr(o))
    for c in o.children:
        dump(c, d + 1)

if __name__ == '__main__':
    dump(parse(sys.argv[1]))
