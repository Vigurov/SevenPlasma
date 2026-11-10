#!/usr/bin/env python3
"""Windows 7 style shut down / restart: the desktop fades into the logon background with the busy ring and
"Завершення роботи..." (the same picture the Plymouth theme shows afterwards, so the hand-over is seamless),
then KDE is asked to log out and shut down / reboot.
If the session does not end (a program asked to save something), the screen fades away again so the
question can be answered.   Usage: seven-shutdown.py shutdown|reboot"""
import os, subprocess, sys

os.environ.pop('SESSION_MANAGER', None)        # stay out of the session: KDE must not close this window

# Started from the Start menu this would be a child of plasmashell and get killed with it while the session
# closes - then the windows of the programs still closing show up. Run as an own transient user service instead,
# which lives until the user manager itself stops (the X server keeps the last picture after that).
if os.environ.get('SEVEN_SHUTDOWN_DETACHED') != '1' and len(sys.argv) > 1 and sys.argv[1] != 'test':
    env = ['-E', 'SEVEN_SHUTDOWN_DETACHED=1']
    for k in ('DISPLAY', 'XAUTHORITY', 'XDG_RUNTIME_DIR', 'DBUS_SESSION_BUS_ADDRESS', 'QT_QPA_PLATFORMTHEME'):
        if os.environ.get(k):
            env += ['-E', '%s=%s' % (k, os.environ[k])]
    try:
        subprocess.run(['systemd-run', '--user', '--quiet', '--collect', '--unit=seven-shutdown-screen-%d' % os.getpid(),
                        '-p', 'KillMode=process', '-p', 'TimeoutStopSec=60'] + env + [sys.executable, os.path.abspath(__file__)] + sys.argv[1:],
                       check=True, timeout=10)
        sys.exit(0)
    except Exception:
        pass                                   # no systemd user manager: run here as before
from PyQt6.QtCore import Qt, QTimer, QPropertyAnimation, QEasingCurve
from PyQt6.QtGui import QPixmap, QPainter, QGuiApplication, QColor
from PyQt6.QtWidgets import QApplication, QWidget

ASSETS = os.path.expanduser('~/src/seven-common/boot/shutdown')
ACTION = sys.argv[1] if len(sys.argv) > 1 else 'shutdown'


class Screen(QWidget):
    def __init__(self, geometry):
        super().__init__(None, Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowStaysOnTopHint
                         | Qt.WindowType.X11BypassWindowManagerHint | Qt.WindowType.Tool)
        self.setGeometry(geometry)
        self.setCursor(Qt.CursorShape.BlankCursor)
        self.bg = QPixmap(os.path.join(ASSETS, 'logon_bg.png'))
        self.text = QPixmap(os.path.join(ASSETS, 'shutdown_text.png'))
        self.wm = QPixmap(os.path.join(ASSETS, 'watermark.png'))
        self.spins = [QPixmap(os.path.join(ASSETS, 'spin%02d.png' % i)) for i in range(18)]
        self.frame = 0
        t = QTimer(self)
        t.timeout.connect(self.tick)
        t.start(50)                                # one turn of the ring in 0.9 s, as in Plymouth

    def tick(self):
        self.frame = (self.frame + 1) % 18
        self.update()
        # once KWin has quit nothing keeps the stacking order: a program that maps or raises a window while
        # it closes (Steam, Discord, a "save changes?" question) would pop up over the shutdown screen
        if not self._held:
            self.raise_()

    _held = False

    def paintEvent(self, _):
        p = QPainter(self)
        W, H = self.width(), self.height()
        p.fillRect(self.rect(), QColor(29, 95, 122))
        p.drawPixmap(self.rect(), self.bg)
        row_w = 20 + 8 + self.text.width() - 16
        x = W // 2 - row_w // 2
        y = H // 2 - 36
        p.drawPixmap(x, y - 10, self.spins[self.frame])
        p.drawPixmap(x + 20, y - self.text.height() // 2, self.text)
        p.drawPixmap(W // 2 - self.wm.width() // 2, H - self.wm.height() - (96 - self.wm.height()) // 2, self.wm)


app = QApplication(sys.argv)
app.setQuitOnLastWindowClosed(False)
screens = []
for s in QGuiApplication.screens():
    w = Screen(s.geometry())
    w.setWindowOpacity(0.0)
    w.show()
    a = QPropertyAnimation(w, b'windowOpacity', w)
    a.setDuration(600)
    a.setStartValue(0.0)
    a.setEndValue(1.0)
    a.setEasingCurve(QEasingCurve.Type.InOutQuad)
    a.start()
    screens.append((w, a))


def leave():
    method = 'logoutAndReboot' if ACTION == 'reboot' else 'logoutAndShutdown'
    subprocess.Popen(['qdbus6', 'org.kde.Shutdown', '/Shutdown', 'org.kde.Shutdown.' + method])


def give_up():
    # still here: the shutdown was held up (e.g. a program wants its work saved); get out of the way
    for w, _ in screens:
        w._held = True
        a = QPropertyAnimation(w, b'windowOpacity', w)
        a.setDuration(400)
        a.setStartValue(w.windowOpacity())
        a.setEndValue(0.0)
        a.finished.connect(app.quit)
        a.start()
        w._fade = a


state = {'seen': False, 'polls': 0, 'gone': False}


def watch():
    # KDE says whether the logout is still going on; when a program cancels it (asks to save work)
    # the shutdown screen must make way at once instead of covering the question
    if state['gone']:
        return
    try:
        r = subprocess.run(['qdbus6', 'org.kde.ksmserver', '/KSMServer', 'org.kde.KSMServerInterface.isShuttingDown'],
                           capture_output=True, text=True, timeout=3)
    except (OSError, subprocess.TimeoutExpired):
        r = None
    if r is None or r.returncode != 0:
        # ksmserver itself is gone: the session is ending as it should, keep the screen up
        QTimer.singleShot(500, watch)
        return
    busy = r.stdout.strip() == 'true'
    state['polls'] += 1
    if busy:
        state['seen'] = True
    elif state['seen'] or state['polls'] > 10:
        # cancelled (or never started within ~5 s)
        state['gone'] = True
        give_up()
        return
    QTimer.singleShot(500, watch)


if ACTION == 'test':                           # just show the screen for a moment
    QTimer.singleShot(3000, app.quit)
else:
    QTimer.singleShot(700, leave)
    QTimer.singleShot(1200, watch)
    QTimer.singleShot(60000, give_up)
sys.exit(app.exec())
