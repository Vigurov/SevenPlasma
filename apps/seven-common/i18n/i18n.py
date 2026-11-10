#!/usr/bin/env python3
"""Make a program translatable: English in the code, Ukrainian in a Qt translation file.

    i18n.py extract APP_DIR STRINGS.json   every string literal with Cyrillic letters (adjacent literals joined)
                                           -> {"uk": ..., "en": "", "where": [...]}; existing "en" values are kept
    i18n.py apply   APP_DIR STRINGS.json   rewrites the code: QStringLiteral("uk") / QString::fromUtf8("uk") /
                                           tr("uk") -> tr7("en"); other literals ("uk" in tables) -> "en" literal
                                           (their use sites call tr7()); writes APP_DIR/i18n/uk.ts
    i18n.py check   APP_DIR                lists Cyrillic left in the code (comments excepted)

Strings are kept exactly as written in the source (escapes like \\n and \\" stay escaped). tr7() is
Win7I18n::tr7 from seven-common/win7i18n.h: QCoreApplication::translate("Win7", en, disambiguation).
An "en" of the form "text||note" gives the disambiguation note (same English, different Ukrainian).
"""
import glob, html, json, os, re, sys

CYR = re.compile(r'[Ѐ-ӿ]')


def lex(src):
    """yield (start, end, kind) of string literals, skipping comments and char literals"""
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith('//', i):
            j = src.find('\n', i)
            i = n if j < 0 else j
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif c == '"':
            j = i + 1
            while j < n and src[j] != '"':
                j += 2 if src[j] == '\\' else 1
            yield i, j + 1
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == '\\' else 1
            i = j + 1
        elif src.startswith('R"', i) and (i == 0 or not (src[i - 1].isalnum() or src[i - 1] == '_')):
            m = re.match(r'R"([^(]*)\(', src[i:])
            j = src.find(')' + m.group(1) + '"', i)
            i = j + len(m.group(1)) + 2
        else:
            i += 1


def groups(src):
    """adjacent literals joined: [(start, end, [(s, e), ...])]"""
    out = []
    for s, e in lex(src):
        if out and not src[out[-1][1]:s].strip():
            out[-1][1] = e
            out[-1][2].append((s, e))
        else:
            out.append([s, e, [(s, e)]])
    return out


def text(src, parts):
    return ''.join(src[s + 1:e - 1] for s, e in parts)


def files(app):
    if os.path.isfile(app):                      # a list of files (KDE parts spread over several trees)
        return [l.strip() for l in open(app) if l.strip()]
    return sorted(glob.glob(os.path.join(app, '*.h')) + glob.glob(os.path.join(app, '*.cpp')))


KDE = os.environ.get('I18N_KDE_DOMAIN')         # set: KDE code / QML -> i18nd(domain, ...), uk.po instead of uk.ts


def extract(app, out):
    old = {}
    if os.path.exists(out):
        old = {d['uk']: d for d in json.load(open(out, encoding='utf-8'))}
    found = {}
    for f in files(app):
        src = open(f, encoding='utf-8').read()
        for s, e, parts in groups(src):
            t = text(src, parts)
            if CYR.search(t):
                line = src.count('\n', 0, s) + 1
                d = found.setdefault(t, {'uk': t, 'en': old.get(t, {}).get('en', ''), 'where': []})
                d['where'].append('%s:%d' % (f if os.path.isfile(app) else os.path.basename(f), line))
    json.dump(list(found.values()), open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    print(len(found), 'strings,', sum(1 for d in found.values() if not d['en']), 'without English')


WRAP = re.compile(r'(QStringLiteral|QString::fromUtf8|QObject::tr|tr|QLatin1String|QString|i18n|qsTr)\s*\(\s*$')


def apply(app, strings):
    table = {d['uk']: d['en'] for d in json.load(open(strings, encoding='utf-8'))}
    missing = [k for k, v in table.items() if not v]
    if missing:
        sys.exit('%d strings have no English yet' % len(missing))
    seen = {}
    for uk, en in table.items():
        if en != '=':
            seen.setdefault(en, []).append(uk)
    clash = {en: uks for en, uks in seen.items() if len(uks) > 1}
    if clash:   # one English text for two Ukrainian ones: add "||note" to tell them apart
        for en, uks in clash.items():
            print('same English %r for: %s' % (en, ' | '.join(uks)))
        sys.exit('%d clashes' % len(clash))
    pairs, bare = {}, []
    for f in files(app):
        src = open(f, encoding='utf-8').read()
        out, pos = [], 0
        for s, e, parts in groups(src):
            t = text(src, parts)
            if not CYR.search(t):
                continue
            en = table[t]
            if en == '=':   # not text for people (a path, a key): stays as it is
                continue
            note = ''
            if '||' in en:
                en, note = en.split('||', 1)
            pairs.setdefault((en, note), t)
            before = src[pos:s]
            m = WRAP.search(before)
            close = re.match(r'\s*\)', src[e:])
            lit = '"%s"' % re.sub(r'(?<!\\)"', r'\\"', en)   # a " of the English text must be escaped
            qml = f.endswith('.qml') or (f.endswith('.js') and '.pragma library' not in src)   # i18nd() exists there
            if KDE and (m or qml):   # in QML a bare literal can become a call too
                call = ('i18ndc("%s", "%s", %s)' % (KDE, note, lit)) if note else ('i18nd("%s", %s)' % (KDE, lit))
                if m and close:
                    out.append(before[:m.start()] + call)
                    pos = e + close.end()
                else:
                    out.append(before + call)
                    pos = e
                continue
            if m and close:
                call = 'tr7(%s%s)' % (lit, ', "%s"' % note if note else '')
                out.append(before[:m.start()] + call)
                pos = e + close.end()
            else:
                out.append(before + lit)
                pos = e
                bare.append('%s:%d' % (os.path.basename(f), src.count('\n', 0, s) + 1))
                if note:   # shown through tr7(text) without the note
                    pairs.setdefault((en, ''), t)
        out.append(src[pos:])
        new = ''.join(out)
        if new != src and KDE:
            if f.endswith(('.cpp', '.h')) and 'KLocalizedString' not in new:
                new = re.sub(r'((?:#include [<"][^\n]*\n)+)', r'\1#include <KLocalizedString>\n', new, count=1)
            open(f, 'w', encoding='utf-8').write(new)
        elif new != src:
            if 'win7i18n.h' not in new and not f.endswith('win7i18n.h'):
                new = re.sub(r'(#pragma once\n)', r'\1#include "win7i18n.h"\n', new, count=1) \
                    if '#pragma once' in new else '#include "win7i18n.h"\n' + new
            open(f, 'w', encoding='utf-8').write(new)
    if KDE:
        write_po(os.environ['I18N_PO'], pairs)
    else:
        write_ts(os.path.join(app, 'i18n', 'uk.ts'), pairs)
    print(len(pairs), 'messages in i18n/uk.ts')
    if bare:   # literals in tables: the code that shows them must call tr7()
        print('plain literals (check their use):', ' '.join(bare))


ESC = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', "'": "'", '\\': '\\', '?': '?', '0': '\0'}


def unescape(s):
    """the string the C++ literal stands for"""
    return re.sub(r'\\x([0-9a-fA-F]{2})|\\(.)', lambda m: chr(int(m.group(1), 16)) if m.group(1) else ESC.get(m.group(2), m.group(2)), s)


def write_ts(path, pairs):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    old = {}
    if os.path.exists(path):   # keep messages of earlier runs (strings already converted)
        for m in re.finditer(r'<message>(.*?)</message>', open(path, encoding='utf-8').read(), re.S):
            body = m.group(1)
            src = html.unescape(re.search(r'<source>(.*?)</source>', body, re.S).group(1))
            com = re.search(r'<comment>(.*?)</comment>', body, re.S)
            tr = html.unescape(re.search(r'<translation>(.*?)</translation>', body, re.S).group(1))
            old[(src, html.unescape(com.group(1)) if com else '')] = tr
    for (en, note), uk in pairs.items():
        old[(unescape(en), note)] = unescape(uk)
    lines = ['<?xml version="1.0" encoding="utf-8"?>', '<!DOCTYPE TS>', '<TS version="2.1" language="uk_UA">',
             '<context>', '    <name>Win7</name>']
    for (en, note), uk in sorted(old.items()):
        lines.append('    <message>')
        lines.append('        <source>%s</source>' % html.escape(en, quote=False))
        if note:
            lines.append('        <comment>%s</comment>' % html.escape(note, quote=False))
        lines.append('        <translation>%s</translation>' % html.escape(uk, quote=False))
        lines.append('    </message>')
    lines += ['</context>', '</TS>', '']
    open(path, 'w', encoding='utf-8').write('\n'.join(lines))


def po_quote(t):
    return '"' + t.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\t', '\\t') + '"'


def write_po(path, pairs):
    """gettext catalog of the KDE domain (merged with what earlier runs wrote)"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    old = {}
    if os.path.exists(path):
        cur = {}
        for line in open(path, encoding='utf-8').read().split('\n') + ['']:
            m = re.match(r'(msgctxt|msgid|msgstr) (".*")$', line)
            if m:
                cur[m.group(1)] = eval(m.group(2))
            elif not line.strip() and 'msgid' in cur:
                if cur['msgid']:
                    old[(cur['msgid'], cur.get('msgctxt', ''))] = cur.get('msgstr', '')
                cur = {}
    for (en, note), uk in pairs.items():
        old[(unescape(en), note)] = unescape(uk)
    out = ['msgid ""', 'msgstr ""', '"Content-Type: text/plain; charset=UTF-8\\n"', '"Language: uk\\n"',
           '"Plural-Forms: nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);\\n"', '']
    for (en, note), uk in sorted(old.items()):
        if note:
            out.append('msgctxt ' + po_quote(note))
        out += ['msgid ' + po_quote(en), 'msgstr ' + po_quote(uk), '']
    open(path, 'w', encoding='utf-8').write('\n'.join(out))


def check(app, strings=None):
    keep = set()
    if strings:
        keep = {d['uk'] for d in json.load(open(strings, encoding='utf-8')) if d['en'] == '='}
    n = 0
    for f in files(app):
        src = open(f, encoding='utf-8').read()
        for s, e, parts in groups(src):
            if CYR.search(text(src, parts)) and text(src, parts) not in keep:
                n += 1
                print('%s:%d' % (f, src.count('\n', 0, s) + 1))
    print(n, 'left')


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'extract':
        extract(sys.argv[2], sys.argv[3])
    elif cmd == 'apply':
        apply(sys.argv[2], sys.argv[3])
    elif cmd == 'clashes':
        d = json.load(open(sys.argv[2], encoding='utf-8'))
        seen = {}
        for x in d:
            if x['en'] and x['en'] != '=':
                seen.setdefault(x['en'], []).append(x['uk'])
        for en, uks in seen.items():
            if len(uks) > 1:
                print('%r: %s' % (en, ' | '.join(uks)))
    elif cmd == 'check':
        check(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
