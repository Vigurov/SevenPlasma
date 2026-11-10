#!/usr/bin/env python3
"""«Ножиці» — the Windows 7 Snipping Tool for the Win7-look KDE desktop.

Small main window (Створити ▾ / Скасувати / Параметри and a hint), snips of four kinds (free-form, rectangle,
window, full screen) taken over a whitened screen with a red ink outline, then the mark-up window with the pen,
highlighter and eraser, save / copy. Genuine toolbar pictures and icon from Windows 7 SnippingTool.exe
(~/.local/share/seven-snip, icon seven-snippingtool)."""
import ctypes, ctypes.util, os, sys, time
from PyQt6.QtCore import Qt, QPoint, QProcess, QPointF, QRect, QRectF, QSettings, QSize, QTimer
from PyQt6.QtGui import (QAction, QActionGroup, QColor, QCursor, QGuiApplication, QIcon, QImage, QKeySequence,
                         QPainter, QPainterPath, QPen, QPixmap, QPolygonF)
from PyQt6.QtNetwork import QLocalServer, QLocalSocket
from PyQt6.QtWidgets import (QApplication, QCheckBox, QColorDialog, QDialog, QDialogButtonBox, QFileDialog,
                             QGroupBox, QHBoxLayout, QLabel, QMainWindow, QMenu, QScrollArea, QToolBar,
                             QToolButton, QVBoxLayout, QWidget)

ASSETS = os.path.expanduser('~/.local/share/seven-snip')
SETTINGS = QSettings('win7', 'snip')
SOCKET = os.path.join(os.environ.get('XDG_RUNTIME_DIR') or '/tmp', 'seven-snip.sock')

MODES = [('free', 'Довільна форма'), ('rect', 'Прямокутник'), ('window', 'Вікно'), ('full', 'Весь екран')]
HINTS = {'free': 'Проведіть курсором навколо області, яку потрібно захопити.',
         'rect': 'Перетягніть курсор навколо області, яку потрібно захопити.',
         'window': 'Клацніть вікно, яке потрібно захопити.',
         'full': 'Натисніть кнопку «Створити», щоб захопити весь екран.'}
INK = QColor(255, 0, 0)


def strip(name, n, size=24):
    """the icons of a genuine Snipping Tool toolbar bitmap, cut into n pieces"""
    pm = QPixmap(os.path.join(ASSETS, name))
    return [QIcon(pm.copy(i * size, 0, size, size)) for i in range(n)]


def strip2(n, size=24):
    """normal + disabled pictures of the main window's buttons"""
    on, off = QPixmap(os.path.join(ASSETS, 'bmp_130.png')), QPixmap(os.path.join(ASSETS, 'bmp_131.png'))
    icons = []
    for i in range(n):
        ic = QIcon(on.copy(i * size, 0, size, size))
        ic.addPixmap(off.copy(i * size, 0, size, size), QIcon.Mode.Disabled)
        icons.append(ic)
    return icons


# ---------------- the windows on screen, topmost first (X11, _NET_CLIENT_LIST_STACKING) ----------------
def client_windows():
    out = []
    try:
        x11 = ctypes.cdll.LoadLibrary(ctypes.util.find_library('X11'))
        x11.XOpenDisplay.restype = ctypes.c_void_p
        x11.XInternAtom.restype = ctypes.c_ulong
        d = ctypes.c_void_p(x11.XOpenDisplay(None))
        root = ctypes.c_ulong(x11.XDefaultRootWindow(d))
        def prop(win, name, typ):
            atom = x11.XInternAtom(d, name, False)
            at, af, n, after = ctypes.c_ulong(), ctypes.c_int(), ctypes.c_ulong(), ctypes.c_ulong()
            data = ctypes.POINTER(ctypes.c_ulong)()
            if x11.XGetWindowProperty(d, win, ctypes.c_ulong(atom), 0, 4096, False, ctypes.c_ulong(typ),
                                      ctypes.byref(at), ctypes.byref(af), ctypes.byref(n), ctypes.byref(after),
                                      ctypes.byref(data)) != 0 or not data:
                return []
            vals = [data[i] for i in range(n.value)]
            x11.XFree(data)
            return vals
        XA_WINDOW, XA_CARDINAL = 33, 6
        hidden = x11.XInternAtom(d, b'_NET_WM_STATE_HIDDEN', False)
        for w in reversed(prop(root, b'_NET_CLIENT_LIST_STACKING', XA_WINDOW)):
            win = ctypes.c_ulong(w)
            if hidden in prop(win, b'_NET_WM_STATE', 4):
                continue
            g_root = ctypes.c_ulong(); gx, gy = ctypes.c_int(), ctypes.c_int()
            ww, hh, bw, dep = (ctypes.c_uint() for _ in range(4))
            if not x11.XGetGeometry(d, win, ctypes.byref(g_root), ctypes.byref(gx), ctypes.byref(gy), ctypes.byref(ww),
                                    ctypes.byref(hh), ctypes.byref(bw), ctypes.byref(dep)):
                continue
            tx, ty, ch = ctypes.c_int(), ctypes.c_int(), ctypes.c_ulong()
            x11.XTranslateCoordinates(d, win, root, 0, 0, ctypes.byref(tx), ctypes.byref(ty), ctypes.byref(ch))
            ext = prop(win, b'_NET_FRAME_EXTENTS', XA_CARDINAL) or [0, 0, 0, 0]
            l, r, t, b = (ext + [0, 0, 0, 0])[:4]
            out.append(QRect(tx.value - l, ty.value - t, ww.value + l + r, hh.value + t + b))
        x11.XCloseDisplay(d)
    except Exception:
        pass
    return out


def set_transient(win, parent):
    """WM_TRANSIENT_FOR of an already shown window (Qt only sets it when mapping)"""
    try:
        x11 = ctypes.cdll.LoadLibrary(ctypes.util.find_library('X11'))
        x11.XOpenDisplay.restype = ctypes.c_void_p
        d = ctypes.c_void_p(x11.XOpenDisplay(None))
        if parent:
            x11.XSetTransientForHint(d, ctypes.c_ulong(win), ctypes.c_ulong(parent))
        else:
            x11.XDeleteProperty(d, ctypes.c_ulong(win), ctypes.c_ulong(68))    # XA_WM_TRANSIENT_FOR
        x11.XCloseDisplay(d)
    except Exception:
        pass


def startup_done(sid=None):
    """end the KDE launch feedback (the busy cursor) at once, so it is not caught in the screenshot"""
    if sid is None:
        sid = os.environ.pop('DESKTOP_STARTUP_ID', '')
    if not sid:
        return False
    class Ev(ctypes.Structure):
        _fields_ = [('type', ctypes.c_int), ('serial', ctypes.c_ulong), ('send_event', ctypes.c_int),
                    ('display', ctypes.c_void_p), ('window', ctypes.c_ulong), ('message_type', ctypes.c_ulong),
                    ('format', ctypes.c_int), ('_align', ctypes.c_int), ('data', ctypes.c_char * 20),
                    ('pad', ctypes.c_long * 14)]
    try:
        x11 = ctypes.cdll.LoadLibrary(ctypes.util.find_library('X11'))
        x11.XOpenDisplay.restype = ctypes.c_void_p
        x11.XInternAtom.restype = ctypes.c_ulong
        x11.XCreateSimpleWindow.restype = ctypes.c_ulong
        d = ctypes.c_void_p(x11.XOpenDisplay(None))
        root = ctypes.c_ulong(x11.XDefaultRootWindow(d))
        win = x11.XCreateSimpleWindow(d, root, -100, -100, 1, 1, 0, 0, 0)
        begin, more = x11.XInternAtom(d, b'_NET_STARTUP_INFO_BEGIN', False), x11.XInternAtom(d, b'_NET_STARTUP_INFO', False)
        msg = ('remove: ID="%s"' % sid.replace('"', '\\"')).encode() + b'\0'
        for i in range(0, len(msg), 20):
            ev = Ev(type=33, send_event=1, window=win, message_type=begin if i == 0 else more, format=8)
            ev.data = msg[i:i + 20].ljust(20, b'\0')
            x11.XSendEvent(d, root, False, ctypes.c_long(1 << 22), ctypes.byref(ev))     # PropertyChangeMask
        x11.XDestroyWindow(d, ctypes.c_ulong(win))
        x11.XFlush(d)
        x11.XCloseDisplay(d)
        return True
    except Exception:
        return False


def skip_taskbar(win):
    """no taskbar button for the whitened screen (_NET_WM_STATE_SKIP_TASKBAR / SKIP_PAGER)"""
    class Ev(ctypes.Structure):
        _fields_ = [('type', ctypes.c_int), ('serial', ctypes.c_ulong), ('send_event', ctypes.c_int),
                    ('display', ctypes.c_void_p), ('window', ctypes.c_ulong), ('message_type', ctypes.c_ulong),
                    ('format', ctypes.c_int), ('data', ctypes.c_long * 5), ('pad', ctypes.c_long * 12)]
    try:
        x11 = ctypes.cdll.LoadLibrary(ctypes.util.find_library('X11'))
        x11.XOpenDisplay.restype = ctypes.c_void_p
        x11.XInternAtom.restype = ctypes.c_ulong
        d = ctypes.c_void_p(x11.XOpenDisplay(None))
        root = ctypes.c_ulong(x11.XDefaultRootWindow(d))
        atom = lambda n: x11.XInternAtom(d, n, False)
        ev = Ev(type=33, send_event=1, window=win, message_type=atom(b'_NET_WM_STATE'), format=32)
        ev.data[0], ev.data[1], ev.data[2], ev.data[3] = 1, atom(b'_NET_WM_STATE_SKIP_TASKBAR'), atom(b'_NET_WM_STATE_SKIP_PAGER'), 1
        x11.XSendEvent(d, root, False, ctypes.c_long(0x180000), ctypes.byref(ev))   # SubstructureRedirect|Notify
        x11.XFlush(d)
        x11.XCloseDisplay(d)
    except Exception:
        pass


# ---------------- the whitened screen where the snip is taken ----------------
class Overlay(QWidget):
    def __init__(self, shot, mode, done, pressed=None, released=None):
        # a managed (not override-redirect) window, so the small «Ножиці» window can stay above it; a normal
        # one, because KWin lets only those go really full screen (over the panel)
        super().__init__(None, Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowStaysOnTopHint)
        self.shot, self.mode, self.done = shot, mode, done
        self.pressed, self.released = pressed or (lambda: None), released or (lambda: None)
        self.windows = []
        self.setGeometry(QGuiApplication.primaryScreen().virtualGeometry())
        self.setCursor(Qt.CursorShape.CrossCursor)
        self.setMouseTracking(True)
        self.path = []
        self.start = self.end = None
        self.hover = None

    def paintEvent(self, _):
        p = QPainter(self)
        p.drawPixmap(0, 0, self.shot)
        p.fillRect(self.rect(), QColor(255, 255, 255, 105))          # Windows 7 whitens the screen
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        pen = QPen(INK, 2)
        if self.mode == 'rect' and self.start and self.end:
            r = QRect(self.start, self.end).normalized()
            p.drawPixmap(r, self.shot, r)                               # the chosen part shows clear
            p.setPen(pen); p.drawRect(r)
        elif self.mode == 'free' and len(self.path) > 1:
            poly = QPolygonF([QPointF(q) for q in self.path])
            clip = QPainterPath(); clip.addPolygon(poly)
            p.save(); p.setClipPath(clip); p.drawPixmap(0, 0, self.shot); p.restore()
            p.setPen(pen); p.drawPolyline(poly)
        elif self.mode == 'window' and self.hover:
            p.drawPixmap(self.hover, self.shot, self.hover)
            p.setPen(QPen(INK, 3)); p.drawRect(self.hover.adjusted(1, 1, -2, -2))

    def mousePressEvent(self, e):
        if e.button() == Qt.MouseButton.RightButton:
            self.finish(None); return
        if self.mode == 'window':
            r = self.hover
            if r:
                self.finish(self.shot.copy(r), r)
            return
        self.pressed()                                    # Windows 7: the small window gets out of the way
        self.start = self.end = e.position().toPoint()
        self.path = [self.start]

    def mouseMoveEvent(self, e):
        pos = e.position().toPoint()
        if self.mode == 'window':
            self.hover = next((w for w in self.windows if w.contains(pos)), None)
        elif e.buttons() & Qt.MouseButton.LeftButton:
            self.end = pos
            self.path.append(pos)
        self.update()

    def mouseReleaseEvent(self, e):
        if self.mode == 'rect' and self.start:
            r = QRect(self.start, e.position().toPoint()).normalized()
            if r.width() > 2 and r.height() > 2:
                self.finish(self.shot.copy(r), r)
            else:                                         # just a click: keep waiting for a real snip
                self.start = self.end = None; self.update(); self.released()
        elif self.mode == 'free' and len(self.path) > 2:
            poly = QPolygonF([QPointF(q) for q in self.path])
            r = poly.boundingRect().toAlignedRect()
            img = QImage(r.size(), QImage.Format.Format_ARGB32_Premultiplied)
            img.fill(QColor(255, 255, 255))
            p = QPainter(img)
            clip = QPainterPath(); clip.addPolygon(poly.translated(-r.x(), -r.y()))
            p.setClipPath(clip)
            p.drawPixmap(-r.x(), -r.y(), self.shot)
            if SETTINGS.value('ink', False, bool):
                p.setClipping(False); p.setRenderHint(QPainter.RenderHint.Antialiasing)
                p.setPen(QPen(INK, 2)); p.drawPolygon(poly.translated(-r.x(), -r.y()))
            p.end()
            self.finish(QPixmap.fromImage(img), None, inked=True)
        elif self.mode == 'free':
            self.path = []; self.update(); self.released()

    def keyPressEvent(self, e):
        if e.key() == Qt.Key.Key_Escape:
            self.finish(None)

    def set_mode(self, mode):
        self.mode = mode
        self.path, self.start, self.end, self.hover = [], None, None, None
        if mode == 'window' and not self.windows:
            self.windows = client_windows()
        self.update()

    def finish(self, pm, r=None, inked=False):
        self.hide()
        if pm is not None and not inked and SETTINGS.value('ink', False, bool):
            p = QPainter(pm); p.setPen(QPen(INK, 2)); p.drawRect(pm.rect().adjusted(0, 0, -1, -1)); p.end()
        self.done(pm)
        self.deleteLater()


# ---------------- the mark-up window ----------------
class Canvas(QWidget):
    def __init__(self, pm):
        super().__init__()
        self.base = pm
        self.strokes = []          # (kind, color, width, [QPointF])
        self.setFixedSize(pm.size())
        self.set_tool(('pen', QColor(255, 0, 0), 2))

    def set_tool(self, tool):
        """Windows 7 (Tablet PC ink) shows the tip of the tool as the pointer: a dot of the pen's colour,
        the yellow chisel of the highlighter, a small white eraser"""
        self.tool = tool
        kind, color, width = tool
        if kind == 'pen':
            d = max(width + 1, 4)
            pm = QPixmap(d + 2, d + 2); pm.fill(Qt.GlobalColor.transparent)
            p = QPainter(pm); p.setRenderHint(QPainter.RenderHint.Antialiasing)
            p.setPen(QPen(QColor(255, 255, 255, 200), 1)); p.setBrush(color); p.drawEllipse(QRectF(0.5, 0.5, d + 1, d + 1)); p.end()
        elif kind == 'marker':
            pm = QPixmap(8, 16); pm.fill(Qt.GlobalColor.transparent)
            p = QPainter(pm); p.setPen(QColor(150, 150, 0)); p.setBrush(QColor(255, 255, 0)); p.drawRect(0, 0, 7, 15); p.end()
        else:
            pm = QPixmap(12, 12); pm.fill(Qt.GlobalColor.transparent)
            p = QPainter(pm); p.setPen(QColor(80, 80, 80)); p.setBrush(QColor(255, 255, 255)); p.drawRect(0, 0, 11, 11); p.end()
        self.setCursor(QCursor(pm, pm.width() // 2, pm.height() // 2))

    def image(self):
        out = self.base.copy()
        p = QPainter(out); self.paint_strokes(p); p.end()
        return out

    def paint_strokes(self, p):
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        for kind, color, width, pts in self.strokes:
            if len(pts) < 2:
                continue
            pen = QPen(color, width, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap if kind == 'pen' else Qt.PenCapStyle.SquareCap,
                       Qt.PenJoinStyle.RoundJoin)
            if kind == 'marker':
                # highlighter ink: yellow over light parts, still a yellow tint over dark ones
                p.setCompositionMode(QPainter.CompositionMode.CompositionMode_Multiply)
                p.setPen(pen); p.drawPolyline(QPolygonF(pts))
                p.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceOver)
                pen.setColor(QColor(255, 255, 0, 80))
            p.setPen(pen)
            p.drawPolyline(QPolygonF(pts))

    def paintEvent(self, _):
        p = QPainter(self)
        p.drawPixmap(0, 0, self.base)
        self.paint_strokes(p)

    def mousePressEvent(self, e):
        pos = e.position()
        if self.tool[0] == 'eraser':
            self.erase(pos); return
        kind, color, width = self.tool
        self.strokes.append((kind, color, width, [pos]))
        self.update()

    def mouseMoveEvent(self, e):
        if not (e.buttons() & Qt.MouseButton.LeftButton):
            return
        if self.tool[0] == 'eraser':
            self.erase(e.position()); return
        if self.strokes:
            self.strokes[-1][3].append(e.position())
            self.update()

    def erase(self, pos):
        # Windows 7: the eraser removes a whole stroke it touches
        def near(a, b):                                   # distance from the pointer to a piece of the stroke
            ab = b - a
            l2 = ab.x() ** 2 + ab.y() ** 2
            t = 0 if l2 == 0 else max(0.0, min(1.0, QPointF.dotProduct(pos - a, ab) / l2))
            q = a + ab * t - pos
            return q.x() ** 2 + q.y() ** 2 < 7 ** 2
        for s in list(self.strokes):
            pts = s[3]
            if any(near(pts[i], pts[min(i + 1, len(pts) - 1)]) for i in range(len(pts))):
                self.strokes.remove(s)
        self.update()


class Editor(QMainWindow):
    def __init__(self, app, pm):
        super().__init__()
        self.app = app
        self.setWindowTitle('Ножиці')
        self.setWindowIcon(QIcon.fromTheme('seven-snippingtool'))
        self.canvas = Canvas(pm)
        area = QScrollArea(); area.setWidget(self.canvas); area.setAlignment(Qt.AlignmentFlag.AlignCenter)
        area.setStyleSheet('QScrollArea { background: white; border: none; }')
        self.setCentralWidget(area)
        ic = strip('bmp_134.png', 7)

        mb = self.menuBar()
        f = mb.addMenu('&Файл')
        a_new = f.addAction(ic[0], 'Створити фрагмент', self.new_snip); a_new.setShortcut(QKeySequence('Ctrl+N'))
        a_save = f.addAction(ic[1], 'Зберегти як...', self.save); a_save.setShortcut(QKeySequence('Ctrl+S'))
        f.addSeparator()
        f.addAction('Вихід', self.close)
        e = mb.addMenu('&Правка')
        a_copy = e.addAction(ic[2], 'Копіювати', self.copy); a_copy.setShortcut(QKeySequence('Ctrl+C'))
        t = mb.addMenu('&Сервіс')
        h = mb.addMenu('&Довідка')
        h.addAction('Про програму «Ножиці»', lambda: None).setEnabled(False)

        tb = QToolBar(); tb.setMovable(False); tb.setIconSize(QSize(24, 24))
        tb.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonIconOnly)
        self.addToolBar(tb)
        nb = QToolButton(); nb.setIcon(ic[0]); nb.setText('Створити фрагмент'); nb.setToolTip('Створити фрагмент')
        nb.setPopupMode(QToolButton.ToolButtonPopupMode.MenuButtonPopup)
        nb.setMenu(mode_menu(self, lambda m: self.new_snip(m)))
        nb.clicked.connect(lambda: self.new_snip())
        tb.addWidget(nb)
        tb.addAction(a_save); a_save.setToolTip('Зберегти фрагмент')
        tb.addAction(a_copy)
        send = tb.addAction(ic[3], 'Надіслати фрагмент'); send.setEnabled(False)
        tb.addSeparator()
        group = QActionGroup(self)
        pen_btn = QToolButton(); pen_btn.setIcon(ic[4]); pen_btn.setToolTip('Перо'); pen_btn.setCheckable(True)
        pen_btn.setPopupMode(QToolButton.ToolButtonPopupMode.MenuButtonPopup)
        pm_ = QMenu(self)
        for name, col in (('Червоне перо', QColor(255, 0, 0)), ('Синє перо', QColor(0, 0, 255)), ('Чорне перо', QColor(0, 0, 0))):
            pm_.addAction(name, lambda c=col: self.set_tool(('pen', c, 2), pen_btn))
        pm_.addSeparator()
        pm_.addAction('Настроїти...', lambda: self.custom_pen(pen_btn))
        pen_btn.setMenu(pm_)
        pen_btn.clicked.connect(lambda: self.set_tool(('pen', self.pen_color, 2), pen_btn))
        mk_btn = QToolButton(); mk_btn.setIcon(ic[5]); mk_btn.setToolTip('Маркер'); mk_btn.setCheckable(True)
        mk_btn.clicked.connect(lambda: self.set_tool(('marker', QColor(255, 255, 0), 14), mk_btn))
        er_btn = QToolButton(); er_btn.setIcon(ic[6]); er_btn.setToolTip('Гумка'); er_btn.setCheckable(True)
        er_btn.clicked.connect(lambda: self.set_tool(('eraser', None, 0), er_btn))
        for b in (pen_btn, mk_btn, er_btn):
            tb.addWidget(b)
        self.tool_buttons = (pen_btn, mk_btn, er_btn)
        self.pen_color = QColor(255, 0, 0)
        pen_btn.setChecked(True)
        t.addAction(ic[4], 'Перо', lambda: self.set_tool(('pen', self.pen_color, 2), pen_btn))
        t.addAction(ic[5], 'Маркер', lambda: self.set_tool(('marker', QColor(255, 255, 0), 14), mk_btn))
        t.addAction(ic[6], 'Гумка', lambda: self.set_tool(('eraser', None, 0), er_btn))
        t.addSeparator()
        t.addAction('Параметри...', lambda: Options(self).exec())

        scr = QGuiApplication.primaryScreen().availableGeometry()
        w = min(max(pm.width() + 40, 480), scr.width() - 80)
        h = min(pm.height() + 110, scr.height() - 80)
        self.resize(w, h)

    def set_tool(self, tool, btn):
        if tool[0] == 'pen':
            self.pen_color = tool[1]
        self.canvas.set_tool(tool)
        for b in self.tool_buttons:
            b.setChecked(b is btn)

    def custom_pen(self, btn):
        c = QColorDialog.getColor(self.pen_color, self, 'Колір пера')
        if c.isValid():
            self.set_tool(('pen', c, 2), btn)

    def copy(self):
        QGuiApplication.clipboard().setPixmap(self.canvas.image())

    def save(self):
        # the KDE «Зберегти як» dialog (the Windows 7 one, like in every other program), not Qt's own
        default = os.path.join(os.path.expanduser('~/Pictures'), 'Знімок.png')
        filters = '\n'.join(['*.png|Файл PNG (*.png)', '*.gif|Файл GIF (*.gif)', '*.jpg *.jpeg|Файл JPEG (*.jpg)',
                             '*.bmp|Файл BMP (*.bmp)'])
        self.dlg = QProcess(self)
        self.dlg.finished.connect(self.saved)
        self.dlg.start('kdialog', ['--title', 'Зберегти як', '--attach', str(int(self.winId())),
                                   '--getsavefilename', default, filters])

    def saved(self, code, _status):
        fn = bytes(self.dlg.readAllStandardOutput()).decode().strip()
        if code != 0 or not fn:
            return
        if os.path.splitext(fn)[1].lower() not in ('.png', '.gif', '.jpg', '.jpeg', '.bmp'):
            fn += '.png'
        self.canvas.image().save(fn)

    def new_snip(self, mode=None):
        self.hide()
        self.app.take(mode or self.app.mode, after=self.close)

    def closeEvent(self, e):
        self.app.editors.discard(self)
        super().closeEvent(e)
        if not self.app.editors and not self.app.taking:
            self.app.done()                               # closing the last mark-up window closes «Ножиці»


def mode_menu(parent, pick):
    m = QMenu(parent)
    group = QActionGroup(m)                               # Windows 7 marks the current kind with a dot
    group.setExclusive(True)
    for key, name in MODES:
        a = m.addAction(name, lambda k=key: pick(k))
        a.setCheckable(True)
        a.setActionGroup(group)
        a.setChecked(key == SETTINGS.value('mode', 'rect'))
    return m


class Options(QDialog):
    def __init__(self, parent):
        super().__init__(parent)
        self.setWindowTitle('Параметри ножиць')
        lay = QVBoxLayout(self)
        g = QGroupBox('Програма'); gl = QVBoxLayout(g)
        self.copy = QCheckBox('Завжди копіювати фрагменти до буфера обміну'); self.copy.setChecked(SETTINGS.value('copy', True, bool))
        gl.addWidget(self.copy)
        g2 = QGroupBox('Виділення'); g2l = QVBoxLayout(g2)
        self.ink = QCheckBox('Показувати контур виділення після захоплення фрагментів'); self.ink.setChecked(SETTINGS.value('ink', False, bool))
        g2l.addWidget(self.ink)
        lay.addWidget(g); lay.addWidget(g2)
        bb = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        bb.button(QDialogButtonBox.StandardButton.Ok).setText('ОК')
        bb.button(QDialogButtonBox.StandardButton.Cancel).setText('Скасувати')
        bb.accepted.connect(self.accept); bb.rejected.connect(self.reject)
        lay.addWidget(bb)

    def accept(self):
        SETTINGS.setValue('copy', self.copy.isChecked())
        SETTINGS.setValue('ink', self.ink.isChecked())
        super().accept()


# ---------------- the small main window ----------------
class Main(QMainWindow):
    def __init__(self, app):
        super().__init__(None, Qt.WindowType.Window | Qt.WindowType.WindowStaysOnTopHint)
        self.app = app
        self.setWindowTitle('Ножиці')
        self.setWindowIcon(QIcon.fromTheme('seven-snippingtool'))
        tb = QToolBar(); tb.setMovable(False); tb.setIconSize(QSize(24, 24))
        tb.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        self.addToolBar(tb)
        ic = strip2(3)
        self.new_btn = QToolButton(); self.new_btn.setIcon(ic[0]); self.new_btn.setText('&Створити')
        self.new_btn.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        self.new_btn.setPopupMode(QToolButton.ToolButtonPopupMode.MenuButtonPopup)
        self.new_btn.setMenu(mode_menu(self, self.pick))
        self.new_btn.clicked.connect(lambda: app.overlay.set_mode(app.mode) if app.overlay else app.take(app.mode))
        self.new_btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        tb.addWidget(self.new_btn)
        self.cancel = tb.addAction(ic[1], '&Скасувати', app.cancel); self.cancel.setEnabled(False)
        tb.addAction(ic[2], '&Параметри', lambda: Options(self).exec())
        body = QWidget(); bl = QHBoxLayout(body); bl.setContentsMargins(10, 6, 8, 10)
        self.hint = QLabel(HINTS[app.mode]); self.hint.setWordWrap(True)
        bl.addWidget(self.hint, 1)
        help_ = QLabel(); help_.setPixmap(QPixmap(os.path.join(ASSETS, 'icon_166_16.png'))); help_.setAlignment(Qt.AlignmentFlag.AlignTop)
        bl.addWidget(help_)
        self.setCentralWidget(body)
        self.setFixedWidth(max(self.sizeHint().width(), 330))
        self.setFixedHeight(self.sizeHint().height() + 6)

    def closeEvent(self, e):
        super().closeEvent(e)
        if self.app.overlay:                              # closing the small window ends the snip too
            ov, self.app.overlay = self.app.overlay, None
            ov.hide(); ov.deleteLater()
        self.app.taking = False
        self.cancel.setEnabled(False)
        if not self.app.editors:
            self.app.done()

    def pick(self, mode):
        self.app.mode = mode
        SETTINGS.setValue('mode', mode)
        self.hint.setText(HINTS[mode])
        self.new_btn.setMenu(mode_menu(self, self.pick))
        ov = self.app.overlay
        if ov is not None and mode != 'full':
            ov.set_mode(mode)                             # the screen is already whitened: just switch
        else:
            self.app.take(mode)


class App:
    """«Ножиці» stay loaded in the background after the first start (and from the login on, see --daemon):
    the launcher (~/.local/bin/seven-snip) only pokes this socket, so the snip starts at once."""
    def __init__(self, qapp, show=True):
        self.qapp = qapp
        self.mode = SETTINGS.value('mode', 'rect')
        self.editors = set()
        self.taking = False
        self.overlay = None
        self.main = Main(self)
        self.server = QLocalServer()
        QLocalServer.removeServer(SOCKET)
        self.server.listen(SOCKET)
        self.server.newConnection.connect(self.poked)
        if show:
            self.activate(None)

    def poked(self):
        while self.server.hasPendingConnections():
            c = self.server.nextPendingConnection()
            def read(c=c):
                if not c.canReadLine():
                    return
                words = bytes(c.readLine()).decode(errors='replace').split()
                c.disconnectFromServer()
                if words and words[0] == 'show':
                    self.activate(words[1] if len(words) > 1 else '')
            c.readyRead.connect(read)
            read()

    def activate(self, sid):
        """started again (Print Screen, the Start menu): Windows 7 goes straight into snipping"""
        busy = startup_done(sid)
        if self.overlay is not None or self.taking:
            return
        self.mode = SETTINGS.value('mode', 'rect')
        self.main.hint.setText(HINTS[self.mode])
        self.main.new_btn.setMenu(mode_menu(self.main, self.main.pick))
        if self.mode == 'full':
            self.show_main(); self.main.activateWindow()
            return
        ours = self.main.isVisible() or any(e.isVisible() for e in self.editors)
        # (a moment for the busy cursor or our own windows to go)
        self.take(self.mode, wait=250 if ours else 60 if busy else 0)

    def done(self):
        """everything closed: stay in the background, ready for the next snip"""
        self.main.hide()

    def take(self, mode, after=None, wait=250):
        self.taking = True
        self.main.hide()
        for e in list(self.editors):
            e.hide()
        def grab():
            shot = QGuiApplication.primaryScreen().grabWindow(0)
            if mode == 'full':
                self.got(shot, after); return
            ov = Overlay(shot, mode, lambda pm: self.got(pm, after), self.main.hide, self.show_main)
            ov.set_mode(mode)
            ov.setGeometry(QGuiApplication.primaryScreen().geometry())
            ov.showFullScreen(); ov.raise_(); ov.activateWindow()
            skip_taskbar(int(ov.winId()))
            self.overlay = ov
            self.show_main()
        QTimer.singleShot(wait, grab)                     # let our windows disappear first

    def show_main(self):
        ov = self.overlay
        self.main.cancel.setEnabled(ov is not None)
        # while the screen is whitened the small window must not take the focus from it: KWin keeps a
        # full-screen window above the panel only while it is the active one (its buttons still work)
        nofocus = ov is not None
        if bool(self.main.windowFlags() & Qt.WindowType.WindowDoesNotAcceptFocus) != nofocus:
            self.main.setWindowFlag(Qt.WindowType.WindowDoesNotAcceptFocus, nofocus)
        self.main.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating, nofocus)
        self.main.winId()
        self.main.windowHandle().setTransientParent(ov.windowHandle() if ov else None)
        self.main.show()
        set_transient(int(self.main.winId()), int(ov.winId()) if ov else 0)   # keeps it above the whitened screen
        self.main.raise_()
        if ov:
            ov.activateWindow()                           # Esc and the keys go to the snip

    def cancel(self):
        if self.overlay:
            self.overlay.finish(None)

    def got(self, pm, after):
        self.taking = False
        self.overlay = None
        self.main.cancel.setEnabled(False)
        if after:
            after()
        if pm is None:
            if self.editors:
                self.main.hide()
                for e in self.editors:
                    e.show()
            else:
                self.show_main()
            return
        self.main.hide()
        if SETTINGS.value('copy', True, bool):
            QGuiApplication.clipboard().setPixmap(pm)
        ed = Editor(self, pm)
        self.editors.add(ed)
        ed.show(); ed.raise_(); ed.activateWindow()       # Windows 7: the mark-up window takes the small one's place


if __name__ == '__main__':
    qapp = QApplication(sys.argv)
    qapp.setApplicationName('Ножиці')
    qapp.setDesktopFileName('seven-snip')
    qapp.setQuitOnLastWindowClosed(False)
    # Windows 7 toolbars: light grey with a hint of blue at the bottom (not the theme's beige)
    qapp.setStyleSheet('QToolBar { border: none; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, '
                       'stop:0 #f0f0f0, stop:0.55 #efeef4, stop:1 #e6eaf2); }')
    # already running in the background: just wake it up
    probe = QLocalSocket()
    probe.connectToServer(SOCKET)
    if probe.waitForConnected(300):
        probe.write(('show %s\n' % os.environ.get('DESKTOP_STARTUP_ID', '')).encode())
        probe.waitForBytesWritten(300)
        sys.exit(0)
    app = App(qapp, show='--daemon' not in sys.argv)
    sys.exit(qapp.exec())
