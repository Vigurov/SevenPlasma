#pragma once
/*
 * The window class KWin's decoration exceptions (smodrc) and window rules match, for one window of a program:
 *   X11:     the WM_CLASS instance ("seven-dialog seven-control"), set on the window before it is mapped
 *   Wayland: the app id, which Qt takes from the desktop file name when the window is shown: the name is
 *            switched for that moment (a hidden desktop file of that name gives the taskbar its icon)
 * Programs that link xcb define WIN7_WINDOWCLASS_XCB before including this.
 */
#include <QGuiApplication>
#include <QTimer>
#include <QWidget>
#ifdef WIN7_WINDOWCLASS_XCB
#include <xcb/xcb.h>
#endif

namespace Win7WindowClass
{
/** shows w with its own class; instance = smodrc pattern, cls = the program (X11 class / taskbar entry) */
inline void show(QWidget *w, const QByteArray &instance, const QByteArray &cls)
{
#ifdef WIN7_WINDOWCLASS_XCB
    if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
        const QByteArray v = instance + '\0' + cls + '\0';
        xcb_change_property(x11->connection(), XCB_PROP_MODE_REPLACE, w->winId(), XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, v.size(), v.constData());
        w->show();
        return;
    }
#endif
    const QString old = qGuiApp->desktopFileName();
    qGuiApp->setDesktopFileName(QString::fromLatin1(instance));
    w->show(); // the Wayland toplevel (with its app id) is made here
    qGuiApp->setDesktopFileName(old);
}

/** the same for a modal dialog run with exec(): the class is set, the name switched back once it shows */
template<class Dialog>
inline int exec(Dialog *d, const QByteArray &instance, const QByteArray &cls)
{
#ifdef WIN7_WINDOWCLASS_XCB
    if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
        const QByteArray v = instance + '\0' + cls + '\0';
        xcb_change_property(x11->connection(), XCB_PROP_MODE_REPLACE, d->winId(), XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, v.size(), v.constData());
        return d->exec();
    }
#endif
    const QString old = qGuiApp->desktopFileName();
    qGuiApp->setDesktopFileName(QString::fromLatin1(instance));
    QTimer::singleShot(0, d, [old]() {
        qGuiApp->setDesktopFileName(old);
    });
    return d->exec();
}
}
