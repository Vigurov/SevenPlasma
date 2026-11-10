#!/usr/bin/env python3
"""recipes.json for extract/seven-extract.py from the automatic matches (win7match.py) and the hand-checked
recipes (MANUAL.json: toolbar cells, nearest genuine pictures). Destination names use seven-.

    win7recipes.py MATCH.json MANUAL.json OUT.json
"""
import json, os, re, sys

home = os.path.expanduser('~')
match, manual, outp = sys.argv[1:4]
recipes = {}
for p, e in json.load(open(match)).items():
    if not e:
        continue
    if 'matchset/icons' in p:
        dest = 'icons/hicolor/' + p.split('/hicolor/')[1]
    elif p.startswith(home + '/.local/share/'):
        dest = p[len(home + '/.local/share/'):]
    elif p.startswith('/usr/share/sounds/'):
        dest = 'sounds/' + p[len('/usr/share/sounds/'):]
    else:
        continue
    if re.search(r'win7-(office|winrar)', dest):
        continue          # not part of SevenPlasma
    dest = dest.replace('win7-', 'seven-')
    r = {'dest': dest, 'src': e['path'], 'from': e['from']}
    if 'id' in e:
        r['id'] = e['id']
        r['extra'] = e.get('extra', '')
    recipes[dest] = r
for r in json.load(open(manual)):
    recipes.setdefault(r['dest'], r)
out = sorted(recipes.values(), key=lambda r: r['dest'])
json.dump(out, open(outp, 'w'), ensure_ascii=False, indent=0)
print(len(out), 'recipes')
