#pragma once
/*
 * Win+P, "Connect to a projector" (DisplaySwitch.exe of Windows 7): a sheet of glass in the middle of the
 * screen with Computer only / Duplicate / Extend / Projector only, the current one marked. Arrows, Tab or
 * Win+P again move the mark, Enter or a click switches, Esc or a click elsewhere closes. The pictures are
 * DisplaySwitch.exe's own bitmaps 200-203 (~/.local/share/seven-control/displayswitch); the switch goes
 * through kscreen-doctor (Win7Display::outputs).
 */
#include "win7display.h"
#include "win7i18n.h"
#include "win7kwin.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <KWindowEffects>
#include <KX11Extras>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>

namespace Win7DisplaySwitch
{
using Win7I18n::tr7;

enum Choice { ComputerOnly, Duplicate, Extend, ProjectorOnly };

/** the computer's own screen comes first: a laptop's panel, else the one that was the main screen when the
 *  switcher was first used (remembered, so it stays "the computer" after Projector only made another one
 *  the main screen) */
inline QList<Win7Display::Output> orderedOutputs()
{
    QList<Win7Display::Output> outs = Win7Display::outputs();
    KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("seven-displayswitchrc")), QStringLiteral("General"));
    int first = -1;
    for (int i = 0; i < outs.size() && first < 0; ++i) {
        if (Win7Display::isLaptopPanel(outs, i)) {
            first = i;
        }
    }
    const QString saved = g.readEntry("Computer", QString());
    for (int i = 0; i < outs.size() && first < 0; ++i) {
        if (outs.at(i).name == saved) {
            first = i;
        }
    }
    if (first < 0 && !outs.isEmpty()) {
        first = 0; // outputs() puts the main screen first
        if (outs.size() > 1) {
            g.writeEntry("Computer", outs.first().name);
            g.sync();
        }
    }
    if (first > 0) {
        outs.move(first, 0);
    }
    return outs;
}

inline Choice current(const QList<Win7Display::Output> &outs)
{
    if (outs.size() < 2) {
        return ComputerOnly;
    }
    QList<int> on;
    for (int i = 0; i < outs.size(); ++i) {
        if (outs.at(i).enabled) {
            on << i;
        }
    }
    if (on.size() == 1) {
        return on.first() == 0 ? ComputerOnly : ProjectorOnly;
    }
    for (int i : std::as_const(on)) {
        if (outs.at(i).pos != outs.at(on.first()).pos) {
            return Extend;
        }
    }
    return Duplicate;
}

/** the mode for an output: its current one, else its preferred one, else the biggest */
inline QString bestMode(const Win7Display::Output &o)
{
    if (o.enabled && !o.modeId.isEmpty() && !o.modeId.startsWith(QLatin1String("scale:"))) {
        return o.modeId;
    }
    if (!o.preferredModes.isEmpty()) {
        return o.preferredModes.first(); // what the screen asks for, as Windows 7 picks for a projector
    }
    QString id;
    qint64 best = 0;
    double hz = 0;
    for (const Win7Display::Mode &m : o.modes) {
        const qint64 px = qint64(m.size.width()) * m.size.height();
        if (!m.id.startsWith(QLatin1String("scale:")) && (px > best || (px == best && m.refresh > hz))) {
            id = m.id;
            best = px;
            hz = m.refresh;
        }
    }
    return id;
}

inline QSize modeSize(const Win7Display::Output &o, const QString &id)
{
    for (const Win7Display::Mode &m : o.modes) {
        if (m.id == id) {
            return m.size;
        }
    }
    return o.size;
}

inline void apply(Choice c)
{
    const QList<Win7Display::Output> outs = orderedOutputs();
    if (outs.size() < 2) {
        return; // one screen: nothing to switch, like Windows 7
    }
    QStringList args;
    QString primary;
    int x = 0;
    for (int i = 0; i < outs.size(); ++i) {
        const Win7Display::Output &o = outs.at(i);
        const QString p = QStringLiteral("output.%1.").arg(o.name);
        const bool on = c == Duplicate || c == Extend || (c == ComputerOnly ? i == 0 : i > 0);
        if (!on) {
            args << p + QStringLiteral("disable");
            continue;
        }
        const QString mode = bestMode(o);
        args << p + QStringLiteral("enable");
        if (!mode.isEmpty()) {
            args << p + QStringLiteral("mode.") + mode;
        }
        // Duplicate: every screen at the same place shows the same picture; Extend: side by side
        args << p + QStringLiteral("position.%1,0").arg(c == Extend ? x : 0);
        if ((c == ProjectorOnly && i == 1) || (c != ProjectorOnly && i == 0)) {
            primary = p + QStringLiteral("primary");
        }
        x += modeSize(o, mode).width();
    }
    // the main screen in a second call: KScreen makes a screen it has just turned on the main one (X11)
    QProcess::startDetached(QStringLiteral("sh"), QStringList{QStringLiteral("-c"), QStringLiteral("kscreen-doctor \"$@\"; sleep 1; kscreen-doctor %1").arg(primary),
                                                              QStringLiteral("sh")} + args);
}

class Panel : public QWidget
{
public:
    Panel()
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint)
    {
        setWindowTitle(tr7("Connect to a projector"));
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        const QString dir = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-control/displayswitch"),
                                                   QStandardPaths::LocateDirectory);
        for (const char *n : {"computer", "duplicate", "extend", "projector"}) {
            m_pics << QPixmap(dir + QLatin1Char('/') + QLatin1String(n) + QStringLiteral(".png"));
        }
        m_texts = {tr7("Computer only"), tr7("Duplicate"), tr7("Extend"), tr7("Projector only")};
        QFont f = font();
        f.setPixelSize(12);
        setFont(f);
        m_selected = current(orderedOutputs());
        setFixedSize(kMargin * 2 + 4 * kTileW + 3 * kGap, kMargin * 2 + kTileH);
        if (QScreen *s = QGuiApplication::primaryScreen()) {
            const QRect g = s->availableGeometry();
            const QPoint at(g.center().x() - width() / 2, g.center().y() - height() / 2);
            Win7KWin::wayland() ? Win7KWin::moveTo(this, at) : move(at);
        }
        // a second Win+P moves the mark, like Windows 7 (the second process only pokes this one)
        m_server.listen(socketPath());
        connect(&m_server, &QLocalServer::newConnection, this, [this] {
            while (QLocalSocket *c = m_server.nextPendingConnection()) {
                c->deleteLater();
            }
            select((m_selected + 1) % 4);
            raise();
            activateWindow();
        });
    }
    ~Panel() override
    {
        m_server.close();
        QLocalServer::removeServer(socketPath());
    }

    static QString socketPath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/seven-displayswitch.sock");
    }

protected:
    static constexpr int kMargin = 14, kTileW = 133, kTileH = 104, kGap = 4;

    QRect tile(int i) const
    {
        return QRect(kMargin + i * (kTileW + kGap), kMargin, kTileW, kTileH);
    }

    void showEvent(QShowEvent *e) override
    {
        QWidget::showEvent(e);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), 6, 6);
        KWindowEffects::enableBlurBehind(windowHandle(), true, QRegion(path.toFillPolygon().toPolygon()));
        if (Win7KWin::wayland()) {
            Win7KWin::forWindow(this, QStringLiteral("w.skipTaskbar = true; w.skipSwitcher = true; w.skipPager = true; w.keepAbove = true;"));
        }
        activateWindow();
        if (!Win7KWin::wayland()) {
            KX11Extras::forceActiveWindow(winId()); // opened by a key: take the focus from the window in front
        }
        setFocus();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        // the glass in the window colour, lighter at the top, with Aero's dark outer and light inner edge
        QColor glass = Win7Display::currentGlassColor();
        glass.setAlpha(qBound(70, glass.alpha(), 170));
        p.setPen(Qt::NoPen);
        p.setBrush(glass);
        p.drawRoundedRect(r, 6, 6);
        QLinearGradient shine(0, 0, 0, height());
        shine.setColorAt(0, QColor(255, 255, 255, 105));
        shine.setColorAt(0.45, QColor(255, 255, 255, 40));
        shine.setColorAt(0.46, QColor(255, 255, 255, 18));
        shine.setColorAt(1, QColor(255, 255, 255, 34));
        p.setBrush(shine);
        p.drawRoundedRect(r, 6, 6);
        p.setBrush(Qt::NoBrush);
        p.setPen(QColor(0, 0, 0, 140));
        p.drawRoundedRect(r, 6, 6);
        p.setPen(QColor(255, 255, 255, 120));
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 5, 5);

        for (int i = 0; i < 4; ++i) {
            const QRect t = tile(i);
            const bool sel = i == m_selected, hot = i == m_hot && !sel;
            if (sel || hot) {
                // the light box of a selected item on glass
                QLinearGradient g(t.topLeft(), t.bottomLeft());
                g.setColorAt(0, QColor(255, 255, 255, sel ? 150 : 80));
                g.setColorAt(1, QColor(255, 255, 255, sel ? 90 : 45));
                p.setBrush(g);
                p.setPen(QColor(255, 255, 255, sel ? 220 : 150));
                p.drawRoundedRect(QRectF(t).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
                p.setPen(QColor(0, 0, 0, sel ? 90 : 50));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(QRectF(t).adjusted(-0.5, -0.5, 0.5, 0.5), 4, 4);
            }
            const QPixmap &pic = m_pics.at(i);
            if (!pic.isNull()) {
                p.drawPixmap(t.left() + (t.width() - pic.width()) / 2, t.top() + 12, pic);
            }
            const QRect text(t.left(), t.top() + 74, t.width(), 22);
            // black text with the white glow of text on glass
            p.setPen(QColor(255, 255, 255, 60));
            for (int dx = -2; dx <= 2; ++dx) {
                for (int dy = -2; dy <= 2; ++dy) {
                    if (dx * dx + dy * dy <= 5 && (dx || dy)) {
                        p.drawText(text.translated(dx, dy), Qt::AlignCenter, m_texts.at(i));
                    }
                }
            }
            p.setPen(Qt::black);
            p.drawText(text, Qt::AlignCenter, m_texts.at(i));
        }
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        switch (e->key()) {
        case Qt::Key_Left:
        case Qt::Key_Backtab:
            select((m_selected + 3) % 4);
            break;
        case Qt::Key_Right:
        case Qt::Key_Tab:
        case Qt::Key_P:
            select((m_selected + 1) % 4);
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            choose(m_selected);
            break;
        case Qt::Key_Escape:
            close();
            break;
        default:
            QWidget::keyPressEvent(e);
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = tileAt(e->position().toPoint());
        if (h != m_hot) {
            m_hot = h;
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hot = -1;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        const int i = tileAt(e->position().toPoint());
        if (i >= 0) {
            choose(i);
        }
    }
    void changeEvent(QEvent *e) override
    {
        QWidget::changeEvent(e);
        // a click elsewhere closes it (after it was active once)
        if (e->type() == QEvent::ActivationChange) {
            if (isActiveWindow()) {
                m_wasActive = true;
            } else if (m_wasActive) {
                close();
            }
        }
    }

private:
    int tileAt(QPoint pt) const
    {
        for (int i = 0; i < 4; ++i) {
            if (tile(i).contains(pt)) {
                return i;
            }
        }
        return -1;
    }
    void select(int i)
    {
        m_selected = i;
        update();
    }
    void choose(int i)
    {
        select(i);
        apply(Choice(i));
        QTimer::singleShot(150, this, &QWidget::close);
    }

    QList<QPixmap> m_pics;
    QStringList m_texts;
    int m_selected = 0;
    int m_hot = -1;
    bool m_wasActive = false;
    QLocalServer m_server;
};

/** seven-control --display-switch: a running panel only moves its mark */
inline int run()
{
    QLocalSocket probe;
    probe.connectToServer(Panel::socketPath());
    if (probe.waitForConnected(300)) {
        probe.disconnectFromServer();
        return 0;
    }
    QLocalServer::removeServer(Panel::socketPath());
    auto *panel = new Panel;
    // the process ends with the panel (Control Panel keeps running without windows otherwise)
    QObject::connect(panel, &QObject::destroyed, qApp, &QCoreApplication::quit);
    panel->show();
    return qApp->exec();
}
}
