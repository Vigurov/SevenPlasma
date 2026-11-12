#!/usr/bin/env python3
"""Global shortcuts as in Windows 7: every key KDE binds that Windows 7 does not have is turned off (Win+Esc
opened KDE's System Monitor, Win+I its System Settings, Alt+Space its search, Win+W/G/Q/A/V/. its overview,
grid, activities, clipboard, emoji...), a few get their Windows 7 meaning. Works on whatever KDE registers,
so new KDE shortcuts are caught too. Through kglobalaccel's D-Bus (it overwrites kglobalshortcutsrc edits).

    seven-shortcuts.py            apply
    seven-shortcuts.py --list     print what is bound now
"""
import ast, re, subprocess, sys

# Windows 7's own keys: component -> actions kept (None = the whole component: hardware keys of
# volume, media, brightness, power, touchpad)
KEEP = {
    'kmix': None,
    'mediacontrol': None,
    'org_kde_powerdevil': None,
    'org.kde.touchpadshortcuts.desktop': None,
    'ksmserver': {'Lock Session', 'Log Out'},                         # Win+L, Ctrl+Alt+Del
    'kwin': {'Show Desktop',                                         # Win+D
             'Win7 Maximize', 'Win7 Maximize Vertically', 'Win7 Restore or Minimize', 'Win7 Shake',
             'Window Quick Tile Left', 'Window Quick Tile Right',      # Win+arrows, Win+Home
             'Window to Next Screen', 'Window to Previous Screen',     # Win+Shift+Left/Right
             'Window Close',                                         # Alt+F4
             'Walk Through Windows', 'Walk Through Windows (Reverse)',  # Alt+Tab
             'Walk Through Windows Alternative', 'Walk Through Windows Alternative (Reverse)',  # Win+Tab
             'view_zoom_in', 'view_zoom_out',                         # Win++ / Win+- (Magnifier)
             'disableInputCapture', 'Window Operations Menu'},
    'plasmashell': {'activate application launcher'} | {'activate task manager entry %d' % i for i in range(1, 10)},
    'org.kde.konsole.desktop': {'_launch'},                          # Ctrl+Alt+T stays for the terminal
}
# keys set to their Windows 7 meaning: (component, action) -> key codes (QKeySequence ints)
SET = {
    ('kwin', 'Window Operations Menu'): [134217760],                 # Alt+Space: the window menu
    ('plasmashell', 'activate application launcher'): [16777250],    # Win alone opens Start (not Alt+F1)
    ('org.kde.krunner.desktop', '_launch'): [16777362],              # only the Search key
    ('systemsettings.desktop', '_launch'): [16777457],               # Tools key -> Control Panel (wrapper)
    ('org_kde_powerdevil', 'powerProfile'): [16777470],              # the Battery key, not Win+B
    # KDE has these first on a fresh system (Quick Tile Top / Bottom): freed above, then given back
    ('kwin', 'Win7 Maximize'): [285212691],                          # Win+Up
    ('kwin', 'Win7 Restore or Minimize'): [285212693],               # Win+Down
    ('kwin', 'Win7 Maximize Vertically'): [318767123],               # Win+Shift+Up
    ('kwin', 'Win7 Shake'): [285212688],                             # Win+Home
    ('kwin', 'Window Quick Tile Left'): [285212690],                 # Win+Left
    ('kwin', 'Window Quick Tile Right'): [285212692],                # Win+Right
}
OURS = ('seven-', 'seven_', 'win7-', 'win7_')
# ours, bound here so they do not depend on what KDE had first
OWN = {
    'calc': ('calc', [16777419]),                                    # Calculator key
    'ease-key': ('ease-key', [268435541]),                           # Win+U: Ease of Access Center
    'display-switch': ('display-switch', [268435536, 16777425]),     # Win+P and the Display key
    'computer-key': ('computer-key', [268435525]),                   # Win+E (Dolphin has it first)
    'run': ('run', [268435538]),                                     # Win+R
    'snip': ('snip', [16777225]),                                    # Print
    'taskmgr': ('taskmgr', [117440512]),                             # Ctrl+Shift+Esc
    'system': ('system', [285212680]),                               # Win+Pause
}


def gdbus(path, method, *args):
    return subprocess.run(['gdbus', 'call', '--session', '--dest', 'org.kde.kglobalaccel', '--object-path', path,
                           '--method', method, *args], capture_output=True, text=True).stdout.strip()


def shortcuts():
    comps = re.findall(r"'(/component/[^']+)'", gdbus('/kglobalaccel', 'org.kde.KGlobalAccel.allComponents'))
    for c in comps:
        out = re.sub(r'@a?\(?[a-z]+\)? ', '', gdbus(c, 'org.kde.kglobalaccel.Component.allShortcutInfos'))
        try:
            infos = ast.literal_eval(out)[0]
        except (ValueError, SyntaxError, IndexError):
            continue
        for t in infos:
            yield t[2], t[0], [k for k in t[6] if k]   # component, action, active keys


def set_keys(comp, action, keys):
    seqs = '[' + ', '.join('([%d, 0, 0, 0],)' % k for k in keys) + ']' if keys else '@a(ai) []'
    gdbus('/kglobalaccel', 'org.kde.KGlobalAccel.setForeignShortcutKeys', str([comp, action, '', '']).replace("'", '"'), seqs)


def main():
    if '--list' in sys.argv:
        for comp, action, keys in sorted(shortcuts()):
            if keys:
                print('%-36s %-44s %s' % (comp, action, keys))
        return
    prefix = 'win7-' if any(c.startswith('win7-') for c, _, _ in shortcuts()) else 'seven-'
    for comp, action, keys in list(shortcuts()):
        if (comp, action) in SET:
            set_keys(comp, action, SET[comp, action])
            continue
        if not keys or comp.startswith(OURS):
            continue
        keep = KEEP.get(comp, set())
        if keep is None or action in keep:
            continue
        set_keys(comp, action, [])
    for name, keys in OWN.values():
        set_keys('%s%s.desktop' % (prefix, name), '_launch', keys)
    # again: a key freed above (Alt+Space from KRunner, Win+E from Dolphin) could not be given away before
    for (comp, action), keys in SET.items():
        set_keys(comp, action, keys)
    for name, keys in OWN.values():
        set_keys('%s%s.desktop' % (prefix, name), '_launch', keys)


if __name__ == '__main__':
    main()
