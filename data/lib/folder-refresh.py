#!/usr/bin/env python3
"""Windows 7 folder icons follow their content: when a file appears in or disappears from a folder, the
folder is touched, so the window or desktop showing that folder redraws its preview (empty folder ->
folder with papers / pictures). Plasma and Dolphin only notice the folder's own change, not its content's."""
import ctypes, os, struct, time, select

libc = ctypes.CDLL("libc.so.6", use_errno=True)
IN_CREATE, IN_DELETE, IN_MOVED_FROM, IN_MOVED_TO, IN_ISDIR, IN_DELETE_SELF = 0x100, 0x200, 0x40, 0x80, 0x40000000, 0x400
MASK = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF
HOME = os.path.expanduser("~")
SKIP = ("node_modules", "snap", "aerothemeplasma", "src", "__pycache__", "build")
DEPTH = 4

fd = libc.inotify_init1(os.O_NONBLOCK | os.O_CLOEXEC)
wd2path = {}

def depth(p):
    return os.path.relpath(p, HOME).count(os.sep) + 1

def watch(path):
    if depth(path) > DEPTH:
        return
    wd = libc.inotify_add_watch(fd, path.encode(), MASK)
    if wd < 0:
        return
    wd2path[wd] = path
    try:
        for e in os.scandir(path):
            if e.is_dir(follow_symlinks=False) and not e.name.startswith(".") and e.name not in SKIP:
                watch(e.path)
    except OSError:
        pass

watch(HOME)
pending = {}
while True:
    r, _, _ = select.select([fd], [], [], 0.3)
    if r:
        try:
            data = os.read(fd, 65536)
        except BlockingIOError:
            data = b""
        i = 0
        while i < len(data):
            wd, mask, cookie, ln = struct.unpack_from("iIII", data, i)
            name = data[i + 16:i + 16 + ln].rstrip(b"\0").decode(errors="replace")
            i += 16 + ln
            path = wd2path.get(wd)
            if not path:
                continue
            if mask & IN_DELETE_SELF:
                wd2path.pop(wd, None)
                continue
            if mask & IN_ISDIR and mask & (IN_CREATE | IN_MOVED_TO) and not name.startswith("."):
                watch(os.path.join(path, name))
            if path != HOME and not name.startswith(".~") :
                pending[path] = time.monotonic()
    now = time.monotonic()
    for path, t in list(pending.items()):
        if now - t > 0.5:  # after a burst of changes settles
            pending.pop(path)
            try:
                os.utime(path)
            except OSError:
                pass
