#!/usr/bin/env python3
"""batch.py STRINGS.json [N]   -> the next N untranslated strings as "index<TAB>file<TAB>uk" (file order)
   batch.py STRINGS.json --put EN.tsv   -> stores "index<TAB>en" lines"""
import json, sys
p = sys.argv[1]
d = json.load(open(p, encoding='utf-8'))
if len(sys.argv) > 2 and sys.argv[2] == '--put':
    n = 0
    for line in open(sys.argv[3], encoding='utf-8'):
        line = line.rstrip('\n')
        if not line.strip():
            continue
        i, en = line.split('\t', 1)
        d[int(i)]['en'] = en
        n += 1
    json.dump(d, open(p, 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    print(n, 'stored,', sum(1 for x in d if not x['en']), 'left')
else:
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    todo = sorted((i for i, x in enumerate(d) if not x['en']), key=lambda i: (d[i]['where'][0].split(':')[0], int(d[i]['where'][0].split(':')[1])))
    for i in todo[:n]:
        print('%d\t%s\t%s' % (i, d[i]['where'][0], d[i]['uk']))
