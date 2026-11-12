#pragma once
/*
 * Window states a Wayland client cannot set on itself (always on top, kept below, no taskbar button, on all
 * desktops): KWin sets them through a small script for this program's windows. On X11 the window flags and
 * NETWM states keep doing it, so callers only use this on Wayland (Win7KWin::wayland()).
 * Windows are found by process and title (Qt on Wayland adds " — <program name>" to titles).
 * Needs Qt6::DBus.
 */
#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPixmap>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>

namespace Win7KWin
{
inline bool wayland()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}

/** runs body (JavaScript, the window is `w`) for this process's windows titled caption (any title if empty) */
inline void forOwnWindows(const QString &caption, const QString &body)
{
    static int serial = 0;
    const QString name = QStringLiteral("seven-%1-%2").arg(QCoreApplication::applicationPid()).arg(++serial);
    const QString path = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QLatin1Char('/') + name + QStringLiteral(".js");
    QString title = caption;
    title.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return;
    }
    f.write(QStringLiteral("const t = \"%1\";\n"
                           "workspace.windowList().forEach(function (w) {\n"
                           "    if (w.pid === %2 && !w.specialWindow && (t === \"\" || w.caption === t || w.caption.startsWith(t + \" \\u2014 \"))) { %3 }\n"
                           "});\n")
                .arg(title)
                .arg(QCoreApplication::applicationPid())
                .arg(body)
                .toUtf8());
    f.close();
    QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"));
    const QDBusReply<int> id = scripting.call(QStringLiteral("loadScript"), path, name);
    if (id.isValid()) {
        QDBusInterface script(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting/Script%1").arg(id.value()), QStringLiteral("org.kde.kwin.Script"));
        script.call(QStringLiteral("run"));
    }
    scripting.call(QStringLiteral("unloadScript"), name);
    QFile::remove(path);
}

/** a script that stays loaded (for signal handlers, e.g. saving the place after a move); unload() removes it */
inline void keepScript(const QString &name, const QString &caption, const QString &body)
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QLatin1Char('/') + name + QStringLiteral(".js");
    QString title = caption;
    title.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return;
    }
    // for the window if it is already there, or as soon as it appears (its title may come a moment later)
    f.write(QStringLiteral("const t = \"%1\";\n"
                           "const done = [];\n"
                           "function matches(w) { return w.caption === t || w.caption.startsWith(t + \" \\u2014 \"); }\n"
                           "function run(w) {\n"
                           "    if (done.indexOf(w.internalId.toString()) >= 0 || !matches(w)) return;\n"
                           "    done.push(w.internalId.toString());\n"
                           "    %3\n"
                           "}\n"
                           "function apply(w) {\n"
                           "    if (w.pid !== %2) return;\n"
                           "    run(w);\n"
                           "    w.captionChanged.connect(function () { run(w); });\n"
                           "}\n"
                           "workspace.windowList().forEach(apply);\n"
                           "workspace.windowAdded.connect(apply);\n")
                .arg(title)
                .arg(QCoreApplication::applicationPid())
                .arg(body)
                .toUtf8());
    f.close();
    QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"));
    scripting.call(QStringLiteral("unloadScript"), name);
    const QDBusReply<int> id = scripting.call(QStringLiteral("loadScript"), path, name);
    if (id.isValid()) {
        QDBusInterface script(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting/Script%1").arg(id.value()), QStringLiteral("org.kde.kwin.Script"));
        script.call(QStringLiteral("run"));
    }
}

inline void unload(const QString &name)
{
    QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"));
    scripting.call(QStringLiteral("unloadScript"), name);
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QLatin1Char('/') + name + QStringLiteral(".js"));
}

/** the same once the window is on screen (a Wayland window reaches KWin a moment after show()) */
inline void forWindow(QWidget *w, const QString &body)
{
    if (w->windowTitle().isEmpty()) { // an untitled window would match all of this program's windows
        w->setWindowTitle(QStringLiteral("seven-window-%1").arg(quintptr(w), 0, 16));
    }
    QTimer::singleShot(300, w, [w, body]() {
        forOwnWindows(w->windowTitle(), body);
    });
}

/** the whole screen as a picture: grabWindow on X11; on Wayland KWin gives the screen only to programs that
 *  ask for it in their desktop file, so Spectacle takes it (an empty picture if it is not installed) */
inline QPixmap screenShot(QScreen *screen = nullptr)
{
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!wayland()) {
        return screen ? screen->grabWindow(0) : QPixmap();
    }
    const QString path = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
        + QStringLiteral("/seven-shot-%1.png").arg(QCoreApplication::applicationPid());
    QProcess p;
    p.start(QStringLiteral("spectacle"), {QStringLiteral("--background"), QStringLiteral("--nonotify"), QStringLiteral("--fullscreen"),
                                          QStringLiteral("--output"), path});
    p.waitForFinished(15000);
    QPixmap all(path);
    QFile::remove(path);
    if (all.isNull() || !screen) {
        return all;
    }
    // the picture covers all screens: this screen's part (scaled to its logical size)
    const QRect virt = screen->virtualGeometry(), g = screen->geometry();
    const double sx = double(all.width()) / virt.width(), sy = double(all.height()) / virt.height();
    return all.copy(QRect(int((g.x() - virt.x()) * sx), int((g.y() - virt.y()) * sy), int(g.width() * sx), int(g.height() * sy)))
        .scaled(g.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

/** places a top-level window: move() on X11; on Wayland clients cannot place their windows, so KWin puts
 *  it there once it is shown (call before or right after show()) */
inline void moveTo(QWidget *w, const QPoint &pos)
{
    if (!wayland()) {
        w->move(pos);
        return;
    }
    forWindow(w, QStringLiteral("w.frameGeometry = {x: %1, y: %2, width: w.frameGeometry.width, height: w.frameGeometry.height};")
                     .arg(pos.x())
                     .arg(pos.y()));
}

/** "Always on top": the flag on X11, KWin's keepAbove on Wayland (the state is kept in a property there) */
inline void setKeepAbove(QWidget *w, bool on)
{
    if (!wayland()) {
        const bool visible = w->isVisible();
        w->setWindowFlag(Qt::WindowStaysOnTopHint, on);
        if (visible) {
            w->show();
        }
        return;
    }
    w->setProperty("win7KeepAbove", on);
    forWindow(w, QStringLiteral("w.keepAbove = %1;").arg(on ? QStringLiteral("true") : QStringLiteral("false")));
}

inline bool keepAbove(const QWidget *w)
{
    return wayland() ? w->property("win7KeepAbove").toBool() : bool(w->windowFlags() & Qt::WindowStaysOnTopHint);
}
}
