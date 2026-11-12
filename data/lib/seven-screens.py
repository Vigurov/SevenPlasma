#!/usr/bin/env python3
"""A screen connected later (a second monitor, a projector) gets the desktop background of the main screen,
like Windows 7, instead of Plasma's own default ("Next" / "Kubuntu"). Runs from the logon on (autostart):
once at the start and each time a screen is added. Only desktops still showing a KDE default are changed.
"""
import subprocess, sys
from PyQt6.QtCore import QTimer
from PyQt6.QtGui import QGuiApplication

SCRIPT = r"""
var all = desktops(), src = null;
for (var i = 0; i < all.length; ++i) { if (all[i].screen == 0) { src = all[i]; } }
if (!src && all.length) { src = all[0]; }
if (src) {
    var p = src.wallpaperPlugin;
    src.currentConfigGroup = ['Wallpaper', p, 'General'];
    var keys = src.configKeys, vals = {};
    for (var k = 0; k < keys.length; ++k) { vals[keys[k]] = src.readConfig(keys[k]); }
    for (var i = 0; i < all.length; ++i) {
        var d = all[i];
        if (d == src) { continue; }
        d.currentConfigGroup = ['Wallpaper', 'org.kde.image', 'General'];
        var img = String(d.readConfig('Image') || '');
        var kdeDefault = d.wallpaperPlugin == 'org.kde.image' && (img == '' || img.indexOf('/usr/share/wallpapers/') >= 0);
        if (!kdeDefault) { continue; }
        d.wallpaperPlugin = p;
        d.currentConfigGroup = ['Wallpaper', p, 'General'];
        for (var k = 0; k < keys.length; ++k) { d.writeConfig(keys[k], vals[keys[k]]); }
        d.reloadConfig();
    }
}
"""


def sync():
    subprocess.run(['qdbus6', 'org.kde.plasmashell', '/PlasmaShell', 'org.kde.PlasmaShell.evaluateScript', SCRIPT],
                   capture_output=True)


def main():
    app = QGuiApplication(sys.argv)
    later = lambda: QTimer.singleShot(3000, sync)   # Plasma makes the new screen's desktop a moment later
    app.screenAdded.connect(lambda _s: later())
    QTimer.singleShot(8000, sync)                   # after the logon (the shell is up by then)
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
