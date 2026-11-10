#!/usr/bin/env python3
"""Text of the Windows 7 tooltip of a desktop item (Ukrainian), one line per field."""
import os, sys, time, subprocess, configparser

def size_text(n):
    for unit, div in (("ГБ", 1 << 30), ("МБ", 1 << 20), ("КБ", 1 << 10)):
        if n >= div:
            v = n / div
            return (f"{v:.2f}" if v < 10 else f"{v:.1f}" if v < 100 else f"{v:.0f}").replace(".", ",") + " " + unit
    return f"{n} байт"

def date_text(t):
    return time.strftime("%d.%m.%Y %H:%M", time.localtime(t))

def listing(names, limit=60):
    s = ", ".join(names)
    return s if len(s) <= limit else s[:limit].rsplit(",", 1)[0] + ", ..."

def mime_comment(path):
    try:
        out = subprocess.run(["kmimetypefinder6", path], capture_output=True, text=True, timeout=2).stdout.strip()
    except Exception:
        out = ""
    return out

path = sys.argv[1]
from urllib.parse import unquote
if path.startswith("file://"):
    path = unquote(path[7:])
elif path.startswith("desktop:/"):
    desk = subprocess.run(["xdg-user-dir", "DESKTOP"], capture_output=True, text=True).stdout.strip() or os.path.expanduser("~/Desktop")
    path = os.path.join(desk, unquote(path[len("desktop:/"):]).lstrip("/"))
lines = []
try:
    st = os.stat(path)
except OSError:
    sys.exit(0)

if os.path.isdir(path):
    try:
        born = int(subprocess.run(["stat", "-c", "%W", path], capture_output=True, text=True, timeout=1).stdout.strip() or 0)
    except Exception:
        born = 0
    lines.append("Дата створення: " + date_text(born or st.st_mtime))
    dirs, files, total, count = [], [], 0, 0
    try:
        for e in sorted(os.scandir(path), key=lambda e: e.name.lower()):
            if e.name.startswith("."):
                continue
            (dirs if e.is_dir(follow_symlinks=False) else files).append(e.name)
    except OSError:
        pass
    # size like Explorer: counted only while it stays quick
    deadline = time.monotonic() + 0.4
    complete = True
    for root, ds, fs in os.walk(path):
        for f in fs:
            try:
                total += os.lstat(os.path.join(root, f)).st_size
            except OSError:
                pass
        if time.monotonic() > deadline:
            complete = False
            break
    if not dirs and not files:
        lines.append("Порожня папка")
    else:
        lines.append("Розмір: " + (size_text(total) if complete else "понад " + size_text(total)))
        if dirs:
            lines.append("Папки: " + listing(dirs))
        if files:
            lines.append("Файли: " + listing(files))
elif path.endswith(".desktop") and ("URL=trash:/" in open(path, encoding="utf-8", errors="replace").read()):
    lines.append("Містить файли та папки, які ви видалили.")
elif path.endswith("/seven-computer.desktop"):
    lines.append("Відображає диски та обладнання, підключені до цього комп'ютера.")
elif path.endswith(".desktop"):
    cp = configparser.ConfigParser(interpolation=None, strict=False)
    try:
        cp.read(path, encoding="utf-8")
        de = cp["Desktop Entry"]
        comment = de.get("Comment[uk]") or de.get("Comment") or ""
        execl = (de.get("Exec") or "").split(" ")[0]
    except Exception:
        comment, execl = "", ""
    if comment:
        lines.append(comment)
    if execl:
        where = os.path.dirname(execl) if os.path.isabs(execl) else os.path.dirname(subprocess.run(["sh", "-c", f"command -v {execl}"], capture_output=True, text=True).stdout.strip())
        if where:
            lines.append("Розташування: " + os.path.basename(where.rstrip("/")) + " (" + where + ")")
else:
    t = sys.argv[2] if len(sys.argv) > 2 else ""
    lines.append("Тип елемента: " + (t or "Файл"))
    if t and ("зображення" in t.lower() or "image" in t.lower()):
        try:
            out = subprocess.run(["identify", "-format", "%w x %h", path + "[0]"], capture_output=True, text=True, timeout=2).stdout.strip()
            if out:
                lines.append("Розміри: " + out)
        except Exception:
            pass
    lines.append("Розмір: " + size_text(st.st_size))
    lines.append("Дата зміни: " + date_text(st.st_mtime))
print("\n".join(lines))
