#!/usr/bin/env python3
"""Lists a folder for the toolbar menu as JSON: name, icon, path, dir (desktop entries use their Name/Icon)."""
import json, mimetypes, os, sys, configparser

def entry(path):
    name = os.path.basename(path)
    if os.path.isdir(path):
        return {'name': name, 'icon': 'folder', 'path': path, 'dir': True}
    if name.endswith('.desktop'):
        cp = configparser.RawConfigParser(strict=False, interpolation=None)
        cp.optionxform = str
        try:
            cp.read(path, encoding='utf-8')
            d = cp['Desktop Entry']
            if d.get('NoDisplay') == 'true' or d.get('Hidden') == 'true':
                return None
            return {'name': d.get('Name[uk]') or d.get('Name') or name[:-8], 'icon': d.get('Icon', 'application-x-executable'), 'path': path, 'dir': False}
        except Exception:
            pass
    mime = mimetypes.guess_type(path)[0]
    return {'name': name, 'icon': mime.replace('/', '-') if mime else 'unknown', 'path': path, 'dir': False}

folder = os.path.expanduser(sys.argv[1]) if len(sys.argv) > 1 else os.path.expanduser('~/Desktop')
items = []
try:
    for n in os.listdir(folder):
        if n.startswith('.'):
            continue
        e = entry(os.path.join(folder, n))
        if e:
            items.append(e)
except OSError:
    pass
items.sort(key=lambda e: (not e['dir'], e['name'].lower()))
print(json.dumps(items, ensure_ascii=False))
