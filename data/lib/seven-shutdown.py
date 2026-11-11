#!/usr/bin/env python3
"""Windows 7 style shut down / restart: the desktop fades into the logon background with the busy ring and
"Завершення роботи..." (the same picture the Plymouth theme shows afterwards, so the hand-over is seamless),
then KDE is asked to log out and shut down / reboot.
If the session does not end (a program asked to save something), the screen fades away again so the
question can be answered.   Usage: seven-shutdown.py shutdown|reboot"""
import ctypes, os, signal, subprocess, sys

os.environ.pop('SESSION_MANAGER', None)        # stay out of the session: KDE must not close this window

# Started from the Start menu this would be a child of plasmashell and get killed with it while the session
# closes - then the windows of the programs still closing show up. Run as an own transient user service instead,
# which lives until the user manager itself stops (the X server keeps the last picture after that).
if os.environ.get('SEVEN_SHUTDOWN_DETACHED') != '1' and len(sys.argv) > 1 and sys.argv[1] != 'test':
    env = ['-E', 'SEVEN_SHUTDOWN_DETACHED=1']
    for k in ('DISPLAY', 'XAUTHORITY', 'WAYLAND_DISPLAY', 'XDG_SESSION_TYPE', 'QT_QPA_PLATFORM', 'XDG_RUNTIME_DIR',
              'DBUS_SESSION_BUS_ADDRESS', 'QT_QPA_PLATFORMTHEME'):
        if os.environ.get(k):
            env += ['-E', '%s=%s' % (k, os.environ[k])]
    try:
        subprocess.run(['systemd-run', '--user', '--quiet', '--collect', '--unit=seven-shutdown-screen-%d' % os.getpid(),
                        '-p', 'KillMode=process', '-p', 'TimeoutStopSec=6'] + env + [sys.executable, os.path.abspath(__file__)] + sys.argv[1:],
                       check=True, timeout=10)
        sys.exit(0)
    except Exception:
        pass                                   # no systemd user manager: run here as before
from PyQt6.QtCore import Qt, QTimer, QPropertyAnimation, QEasingCurve
from PyQt6.QtGui import QPixmap, QPainter, QGuiApplication, QColor
from PyQt6.QtWidgets import QApplication, QWidget


class XWindowAttributes(ctypes.Structure):
    _fields_ = [('x', ctypes.c_int), ('y', ctypes.c_int), ('width', ctypes.c_int), ('height', ctypes.c_int),
                ('border_width', ctypes.c_int), ('depth', ctypes.c_int), ('visual', ctypes.c_void_p),
                ('root', ctypes.c_ulong), ('c_class', ctypes.c_int), ('bit_gravity', ctypes.c_int),
                ('win_gravity', ctypes.c_int), ('backing_store', ctypes.c_int), ('backing_planes', ctypes.c_ulong),
                ('backing_pixel', ctypes.c_ulong), ('save_under', ctypes.c_int), ('colormap', ctypes.c_ulong),
                ('map_installed', ctypes.c_int), ('map_state', ctypes.c_int), ('all_event_masks', ctypes.c_long),
                ('your_event_mask', ctypes.c_long), ('do_not_propagate_mask', ctypes.c_long),
                ('override_redirect', ctypes.c_int), ('screen', ctypes.c_void_p)]


class OtherWindows:
    """are windows of other programs still visible on the X screen (our own excepted)?"""
    def __init__(self):
        self.x = None
        try:
            self.x = ctypes.CDLL('libX11.so.6')
            self.x.XOpenDisplay.restype = ctypes.c_void_p
            self.x.XDefaultRootWindow.restype = ctypes.c_ulong
            self.x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
            self.x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
                                          ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)), ctypes.POINTER(ctypes.c_uint)]
            self.x.XGetWindowAttributes.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(XWindowAttributes)]
            self.x.XFree.argtypes = [ctypes.c_void_p]
            self.dpy = self.x.XOpenDisplay(None)
            if not self.dpy:
                self.x = None
        except OSError:
            self.x = None

    def any(self, own, screen_w, screen_h):
        if not self.x:
            return False
        root, parent = ctypes.c_ulong(), ctypes.c_ulong()
        kids, n = ctypes.POINTER(ctypes.c_ulong)(), ctypes.c_uint()
        if not self.x.XQueryTree(self.dpy, self.x.XDefaultRootWindow(self.dpy), ctypes.byref(root), ctypes.byref(parent),
                                 ctypes.byref(kids), ctypes.byref(n)):
            return False
        found = False
        a = XWindowAttributes()
        for i in range(n.value):
            w = kids[i]
            if w in own or not self.x.XGetWindowAttributes(self.dpy, w, ctypes.byref(a)):
                continue
            # viewable, bigger than a tiny helper window and at least partly on the screen
            if a.map_state == 2 and a.width * a.height > 400 and a.x < screen_w and a.y < screen_h \
                    and a.x + a.width > 0 and a.y + a.height > 0:
                found = True
                break
        if kids:
            self.x.XFree(kids)
        return found

# the pictures of the Plymouth shutdown screen (the installed theme; the development folder otherwise)
ASSETS = next((d for d in ('/usr/share/plymouth/themes/sevenplasma', os.path.expanduser('~/src/seven-common/boot/shutdown'))
               if os.path.exists(os.path.join(d, 'logon_bg.png'))), '/usr/share/plymouth/themes/sevenplasma')
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
        if not self._held and not WAYLAND:
            self.raise_()
            if ending['on']:
                self.raise_timer_fast()

    def raise_timer_fast(self):
        # while the programs close one after another, keep on top every frame, not only every 50 ms
        if not getattr(self, '_fast', None):
            self._fast = QTimer(self)
            self._fast.timeout.connect(self.raise_)
            self._fast.start(10)

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
WAYLAND = QGuiApplication.platformName().startswith('wayland')


def kwin_on_top():
    # Wayland: a client cannot put itself above others or full screen; KWin does it for our windows
    path = os.path.join(os.environ.get('XDG_RUNTIME_DIR', '/tmp'), 'seven-shutdown-%d.js' % os.getpid())
    with open(path, 'w') as f:
        f.write('workspace.windowList().forEach(function (w) { if (w.pid === %d) { w.keepAbove = true; '
                'w.skipTaskbar = true; w.skipSwitcher = true; w.skipPager = true; w.onAllDesktops = true; '
                'w.fullScreen = true; workspace.activeWindow = w; } });' % os.getpid())
    name = 'seven-shutdown-%d' % os.getpid()
    try:
        r = subprocess.run(['qdbus6', 'org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.loadScript', path, name],
                           capture_output=True, text=True, timeout=5)
        subprocess.run(['qdbus6', 'org.kde.KWin', '/Scripting/Script' + r.stdout.strip(), 'org.kde.kwin.Script.run'],
                       capture_output=True, timeout=5)
        subprocess.run(['qdbus6', 'org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting.unloadScript', name],
                       capture_output=True, timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        pass
    try:
        os.remove(path)
    except OSError:
        pass


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
if WAYLAND:
    QTimer.singleShot(200, kwin_on_top)


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
ending = {'on': False, 'polls': 0, 'clear': 0}
others = OtherWindows()


def on_term(*_):
    # The session is ending and systemd stops this screen together with all the programs. Going away now
    # would uncover the windows of the programs that are still closing (Chrome, Discord...), and with
    # "-background none" X keeps that last broken picture until it stops. So stay until no other window is
    # left on the screen (at most ~4 s, systemd gives 6), then quit: X keeps this picture to the end.
    if ending['on']:
        return
    ending['on'] = True
    if WAYLAND:
        # on Wayland the programs' windows go together with KWin (and this one with them): just stay up
        QTimer.singleShot(4000, app.quit)
        return
    QTimer.singleShot(0, wait_others)


def wait_others():
    ending['polls'] += 1
    own = {int(w.winId()) for w, _ in screens}
    g = QGuiApplication.primaryScreen().geometry() if QGuiApplication.primaryScreen() else None
    busy = others.any(own, g.width() if g else 1 << 15, g.height() if g else 1 << 15)
    ending['clear'] = 0 if busy else ending['clear'] + 1
    if ending['clear'] >= 3 or ending['polls'] >= 80:      # clear for 150 ms, or 4 s at most
        try:                                               # how long the programs took (to tune the limit)
            with open(os.path.expanduser('~/.cache/seven-shutdown.log'), 'a') as f:
                f.write('waited %.2f s for the other windows%s\n' % (ending['polls'] * 0.05, '' if ending['clear'] >= 3 else ' (gave up)'))
        except OSError:
            pass
        app.quit()
        return
    QTimer.singleShot(50, wait_others)


signal.signal(signal.SIGTERM, on_term)
signal.signal(signal.SIGHUP, on_term)


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
