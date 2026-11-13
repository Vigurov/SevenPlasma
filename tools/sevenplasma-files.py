#!/usr/bin/env python3
"""Files SevenPlasma put into the system, for install.sh and uninstall.sh (run as root).

    sevenplasma-files.py unowned < paths          the paths that belong to no distribution package (dpkg, pacman or rpm)
                                                  (install.sh keeps this for the files under /usr before
                                                  the install: generated caches and the like, never removed)
    sevenplasma-files.py remove FILES BEFORE KEEP  removes the files of FILES that belong to no package and were
                                                  not there before (KEEP, gzip); prints the packages whose files
                                                  were replaced (those that were installed before: BEFORE),
                                                  for reinstalling. Folders left empty are removed too.
"""
import glob, gzip, os, shutil, subprocess, sys

_owner = None


def owner(p):
    global _owner
    if _owner is None:
        _owner = {}
        for lst in glob.glob('/var/lib/dpkg/info/*.list'):              # Debian / Ubuntu
            pkg = os.path.basename(lst)[:-5].split(':')[0]
            for line in open(lst, encoding='utf-8', errors='replace'):
                _owner.setdefault(line.rstrip('\n'), pkg)
        for d in glob.glob('/var/lib/pacman/local/*/'):                 # Arch: desc (%NAME%) and files (%FILES%)
            try:
                desc = open(d + 'desc', encoding='utf-8', errors='replace').read().split('\n')
                pkg = desc[desc.index('%NAME%') + 1]
                section = None
                for line in open(d + 'files', encoding='utf-8', errors='replace'):
                    line = line.rstrip('\n')
                    if line.startswith('%'):
                        section = line
                    elif section == '%FILES%' and line:
                        _owner.setdefault('/' + line.rstrip('/'), pkg)
            except (OSError, ValueError, IndexError):
                pass
        if shutil.which('rpm') and not _owner:                          # Fedora
            out = subprocess.run(['rpm', '-qa', '--qf', '[%{FILENAMES}\t%{=NAME}\n]'], capture_output=True,
                                 text=True, errors='replace').stdout
            for line in out.splitlines():
                path, _, pkg = line.rpartition('\t')
                if path:
                    _owner.setdefault(path.rstrip('/') or '/', pkg)
    if p in _owner:
        return _owner[p]
    for a, b in (('/usr/lib/', '/lib/'), ('/usr/lib64/', '/lib64/'), ('/usr/bin/', '/bin/'), ('/usr/sbin/', '/sbin/')):   # merged /usr
        if p.startswith(a) and b + p[len(a):] in _owner:
            return _owner[b + p[len(a):]]
    return None


def owned_by_rpm(p):
    """second opinion before a file is deleted (rpm also knows paths under symlinked folders: /lib64 = /usr/lib64)"""
    if not shutil.which('rpm'):
        return False
    r = subprocess.run(['rpm', '-qf', p], capture_output=True, text=True)
    return r.returncode == 0


def database_ok():
    """the package database was read (a system has tens of thousands of packaged files): without it nothing may be
    treated as ours"""
    owner('/')
    return len(_owner) > 10000


def unowned():
    if not database_ok():
        sys.exit('sevenplasma-files: the package database could not be read')
    for line in sys.stdin:
        p = line.rstrip('\n')
        if p and owner(p) is None:
            print(p)


def remove(files, before, keep):
    if not database_ok():
        sys.exit('sevenplasma-files: the package database could not be read, nothing is removed')
    names = [l.rstrip('\n') for l in open(files, encoding='utf-8', errors='replace') if l.strip()]
    pkgs_before = set(l.strip() for l in open(before)) if os.path.exists(before) else None
    kept = set()
    if os.path.exists(keep):
        kept = set(l.rstrip('\n') for l in gzip.open(keep, 'rt', encoding='utf-8', errors='replace'))
    reinstall, dirs = set(), set()
    for f in names:
        if not (f.startswith('/usr/') or f.startswith('/etc/')) or '/../' in f:
            continue
        pkg = owner(f)
        if pkg:
            if pkgs_before is None or pkg in pkgs_before:
                reinstall.add(pkg)
            continue
        if f in kept:
            continue
        if owned_by_rpm(f):   # never delete a package's file, whatever the paths in the database look like
            continue
        if os.path.lexists(f) and not os.path.isdir(f):
            os.remove(f)
            dirs.add(os.path.dirname(f))
    for d in sorted(dirs, key=len, reverse=True):
        while d.count('/') > 3 and owner(d) is None:
            try:
                os.rmdir(d)
            except OSError:
                break
            d = os.path.dirname(d)
    print('\n'.join(sorted(reinstall)))


if __name__ == '__main__':
    if sys.argv[1] == 'unowned':
        unowned()
    elif sys.argv[1] == 'remove':
        remove(*sys.argv[2:5])
