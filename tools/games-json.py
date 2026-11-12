#!/usr/bin/env python3
"""The Games folder's list of the Windows 7 games (Control Panel reads it): name, description, genre, developer,
publisher and the Windows Experience Index they ask for, from each game's __GDF_XML resource (Games Explorer).

    games-json.py "WINDOWS/Program Files/Microsoft Games" OUT.json
"""
import json, os, sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from win7index import PE

# the games that get a menu entry (data/applications/seven_<folder>.desktop)
GAMES = ['Chess', 'FreeCell', 'Hearts', 'Mahjong', 'Minesweeper', 'Purble Place', 'Solitaire', 'SpiderSolitaire']
NS = {'g': 'urn:schemas-microsoft-com:GameDescription.v1'}


def gdf(folder):
    """the GDF of the game in folder: in the .mui of the image's language (or in the program itself)"""
    for d, _, fs in sorted(os.walk(folder)):
        for f in sorted(fs):
            if not f.lower().endswith(('.exe', '.dll', '.mui')):
                continue
            try:
                res = PE(open(os.path.join(d, f), 'rb').read()).all()
            except Exception:
                continue
            for t, i, b in res:
                if str(i) == '__GDF_XML':
                    text = b.decode('utf-16') if b[:2] in (b'\xff\xfe', b'<\x00') else b.decode('utf-8', 'replace')
                    return ET.fromstring(text.lstrip('﻿').split('?>', 1)[1])
    return None


def main():
    root, out = sys.argv[1:3]
    games = []
    for name in GAMES:
        g = gdf(os.path.join(root, name))
        if g is None:
            continue
        d = g.find('g:GameDefinition', NS)
        text = lambda path: (d.findtext(path, '', NS) or '').strip()
        perf = d.find('g:WindowsSystemPerformanceRating', NS)
        games.append({
            'desktop': 'seven_' + name.replace(' ', '_'),
            'name': text('g:Name'),
            'description': text('g:Description'),
            'publisher': text('g:Publishers/g:Publisher'),
            'developer': text('g:Developers/g:Developer'),
            'genre': text('g:Genres/g:Genre'),
            'min': float(perf.get('minimum', 0)) if perf is not None else 0.0,
            'rec': float(perf.get('recommended', 0)) if perf is not None else 0.0,
            'icon': 'seven-game-' + name.lower().replace(' ', '_'),
        })
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    json.dump(games, open(out, 'w'), ensure_ascii=False, indent=1)
    print('games: %d' % len(games))


if __name__ == '__main__':
    main()
