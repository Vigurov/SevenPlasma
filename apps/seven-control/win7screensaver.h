/*
 * Windows 7 screen savers and the "Screen Saver Settings" dialog (desk.cpl): (None), 3D Text, Blank, Bubbles,
 * Mystify, Photos, Ribbons; Wait, "On resume, display logon screen", Settings / Preview, the little monitor
 * running the chosen one. The savers are drawn here (the .scr files are Windows programs); a small resident
 * part (--screensaver-daemon, KIdleTime) starts the chosen one after the wait, unless something inhibits it.
 * Settings in ~/.config/seven-screensaverrc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "win7chrome.h"

#include <KConfig>
#include <KConfigGroup>
#include <KIdleTime>
#include <KSharedConfig>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace Win7ScreenSaver
{
inline KConfigGroup config()
{
    static KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("seven-screensaverrc"));
    cfg->reparseConfiguration();
    return cfg->group(QStringLiteral("General"));
}

struct Kind {
    const char *id;
    const char *name;
};

/** Windows 7's list, in its (alphabetical) order */
inline const QList<Kind> &kinds()
{
    static const QList<Kind> list{{"none", "(Немає)"},        {"text3d", "Тривимірний текст"}, {"blank", "Пустий екран"}, {"bubbles", "Бульбашки"},
                                  {"mystify", "Містифікація"}, {"photos", "Фотографії"},        {"ribbons", "Стрічки"}};
    return list;
}

inline double rnd(double a, double b)
{
    return a + QRandomGenerator::global()->generateDouble() * (b - a);
}

/* ---------------- the savers ---------------- */

class Saver : public QWidget
{
public:
    explicit Saver(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_OpaquePaintEvent);
        m_timer.setInterval(16);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            const double dt = qMin(0.05, m_clock.restart() / 1000.0);
            step(dt);
            update();
        });
        m_clock.start();
        m_timer.start();
    }
    /** the picture behind (Bubbles floats over the desktop) */
    QPixmap backdrop;

protected:
    virtual void step(double dt)
    {
        Q_UNUSED(dt)
    }
    double scale() const
    {
        return qMax(0.08, width() / 1920.0);
    }
    QTimer m_timer;
    QElapsedTimer m_clock;
};

class BlankSaver : public Saver
{
public:
    using Saver::Saver;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter(this).fillRect(rect(), Qt::black);
    }
};

/** Glossy soap bubbles drifting over the desktop and bouncing off the edges and each other */
class BubblesSaver : public Saver
{
public:
    explicit BubblesSaver(QWidget *parent = nullptr)
        : Saver(parent)
    {
    }

protected:
    struct Bubble {
        QPointF p, v;
        double r, hue;
    };
    void ensure()
    {
        if (!m_bubbles.isEmpty() || width() < 10) {
            return;
        }
        const double s = scale();
        for (int i = 0; i < 14; ++i) {
            Bubble b;
            b.r = rnd(40, 110) * s;
            b.p = QPointF(rnd(b.r, width() - b.r), rnd(b.r, height() - b.r));
            const double a = rnd(0, 2 * M_PI);
            b.v = QPointF(std::cos(a), std::sin(a)) * rnd(60, 140) * s;
            b.hue = rnd(0, 360);
            m_bubbles << b;
        }
    }
    void step(double dt) override
    {
        ensure();
        for (Bubble &b : m_bubbles) {
            b.p += b.v * dt;
            b.hue = std::fmod(b.hue + dt * 20, 360);
            if (b.p.x() < b.r || b.p.x() > width() - b.r) {
                b.v.setX(-b.v.x());
                b.p.setX(qBound(b.r, b.p.x(), width() - b.r));
            }
            if (b.p.y() < b.r || b.p.y() > height() - b.r) {
                b.v.setY(-b.v.y());
                b.p.setY(qBound(b.r, b.p.y(), height() - b.r));
            }
        }
        for (int i = 0; i < m_bubbles.size(); ++i) {
            for (int j = i + 1; j < m_bubbles.size(); ++j) {
                Bubble &a = m_bubbles[i], &b = m_bubbles[j];
                const QPointF d = b.p - a.p;
                const double dist = std::hypot(d.x(), d.y());
                if (dist > 0 && dist < a.r + b.r) {
                    const QPointF n = d / dist;
                    const double va = QPointF::dotProduct(a.v, n), vb = QPointF::dotProduct(b.v, n);
                    if (va - vb > 0) {
                        a.v += (vb - va) * n;
                        b.v += (va - vb) * n;
                    }
                }
            }
        }
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        if (!backdrop.isNull()) {
            p.drawPixmap(rect(), backdrop);
        } else {
            p.fillRect(rect(), Qt::black);
        }
        p.setRenderHint(QPainter::Antialiasing);
        for (const Bubble &b : std::as_const(m_bubbles)) {
            // see-through middle, an iridescent rim, a soft dark edge and the window reflections
            QRadialGradient g(b.p, b.r);
            const QColor rim = QColor::fromHsv(int(b.hue), 120, 255, 150);
            const QColor rim2 = QColor::fromHsv(int(std::fmod(b.hue + 120, 360)), 140, 255, 110);
            g.setColorAt(0, QColor(255, 255, 255, 8));
            g.setColorAt(0.70, QColor(255, 255, 255, 14));
            g.setColorAt(0.86, rim2);
            g.setColorAt(0.95, rim);
            g.setColorAt(1, QColor(30, 40, 60, 90));
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawEllipse(b.p, b.r, b.r);
            QRadialGradient h(b.p + QPointF(-b.r * 0.38, -b.r * 0.42), b.r * 0.42);
            h.setColorAt(0, QColor(255, 255, 255, 200));
            h.setColorAt(1, QColor(255, 255, 255, 0));
            p.setBrush(h);
            p.drawEllipse(b.p + QPointF(-b.r * 0.38, -b.r * 0.42), b.r * 0.30, b.r * 0.22);
            QRadialGradient l(b.p + QPointF(b.r * 0.35, b.r * 0.45), b.r * 0.35);
            l.setColorAt(0, QColor(255, 255, 255, 90));
            l.setColorAt(1, QColor(255, 255, 255, 0));
            p.setBrush(l);
            p.drawEllipse(b.p + QPointF(b.r * 0.35, b.r * 0.45), b.r * 0.30, b.r * 0.14);
        }
    }

private:
    QList<Bubble> m_bubbles;
};

/** Two bouncing quadrilaterals leaving trails of their past shapes, slowly changing colour */
class MystifySaver : public Saver
{
public:
    using Saver::Saver;

protected:
    struct Shape {
        QPointF pts[4], vel[4];
        double hue;
        QList<QPolygonF> trail;
    };
    void ensure()
    {
        if (!m_shapes.isEmpty() || width() < 10) {
            return;
        }
        for (int s = 0; s < 2; ++s) {
            Shape sh;
            for (int i = 0; i < 4; ++i) {
                sh.pts[i] = QPointF(rnd(0, width()), rnd(0, height()));
                sh.vel[i] = QPointF(rnd(-1, 1), rnd(-1, 1)) * 260 * scale();
            }
            sh.hue = s * 180;
            m_shapes << sh;
        }
    }
    void step(double dt) override
    {
        ensure();
        m_acc += dt;
        for (Shape &sh : m_shapes) {
            for (int i = 0; i < 4; ++i) {
                sh.pts[i] += sh.vel[i] * dt;
                if (sh.pts[i].x() < 0 || sh.pts[i].x() > width()) {
                    sh.vel[i].setX(-sh.vel[i].x());
                }
                if (sh.pts[i].y() < 0 || sh.pts[i].y() > height()) {
                    sh.vel[i].setY(-sh.vel[i].y());
                }
            }
            sh.hue = std::fmod(sh.hue + dt * 25, 360);
            if (m_acc > 0.05) {
                sh.trail.prepend(QPolygonF({sh.pts[0], sh.pts[1], sh.pts[2], sh.pts[3], sh.pts[0]}));
                while (sh.trail.size() > 7) {
                    sh.trail.removeLast();
                }
            }
        }
        if (m_acc > 0.05) {
            m_acc = 0;
        }
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setRenderHint(QPainter::Antialiasing);
        for (const Shape &sh : std::as_const(m_shapes)) {
            for (int i = 0; i < sh.trail.size(); ++i) {
                QColor c = QColor::fromHsv(int(sh.hue), 230, 255);
                c.setAlphaF(1.0 - i / double(sh.trail.size() + 1));
                p.setPen(QPen(c, qMax(1.0, 1.6 * scale() * 2)));
                p.drawPolyline(sh.trail.at(i));
            }
        }
    }

private:
    QList<Shape> m_shapes;
    double m_acc = 0;
};

/** Flowing ribbons that twist as they turn, fading out slowly (each piece drawn with its age, so it fades to black) */
class RibbonsSaver : public Saver
{
public:
    using Saver::Saver;

protected:
    struct Ribbon {
        QPointF p, prevA, prevB;
        double angle, turn, speed, hue, phase;
        bool started = false;
    };
    struct Piece {
        QPolygonF quad;
        QColor color;
        double born;
    };
    void ensure()
    {
        if (!m_ribbons.isEmpty() || width() < 10) {
            return;
        }
        for (int i = 0; i < 5; ++i) {
            Ribbon r;
            r.p = QPointF(rnd(0, width()), rnd(0, height()));
            r.angle = rnd(0, 2 * M_PI);
            r.turn = rnd(-1.5, 1.5);
            r.speed = rnd(220, 340) * scale();
            r.hue = rnd(0, 360);
            r.phase = rnd(0, 6);
            m_ribbons << r;
        }
    }
    void step(double dt) override
    {
        ensure();
        m_now += dt;
        const double w = 26 * scale() * 2;
        for (Ribbon &r : m_ribbons) {
            r.turn += rnd(-2.5, 2.5) * dt;
            r.turn = qBound(-2.2, r.turn, 2.2);
            r.angle += r.turn * dt;
            r.phase += dt * 1.7;
            r.hue = std::fmod(r.hue + dt * 18, 360);
            r.p += QPointF(std::cos(r.angle), std::sin(r.angle)) * r.speed * dt;
            if (r.p.x() < -50 || r.p.x() > width() + 50 || r.p.y() < -50 || r.p.y() > height() + 50) {
                r.angle += M_PI;
                r.p.setX(qBound(-40.0, r.p.x(), width() + 40.0));
                r.p.setY(qBound(-40.0, r.p.y(), height() + 40.0));
            }
            // the ribbon's width twists with its phase: a band across the direction of travel
            const QPointF n(-std::sin(r.angle), std::cos(r.angle));
            const double half = w * std::sin(r.phase);
            const QPointF a = r.p + n * half, b = r.p - n * half;
            if (r.started) {
                m_pieces.append({QPolygonF({r.prevA, a, b, r.prevB}), QColor::fromHsv(int(r.hue), 200, int(160 + 95 * std::abs(std::sin(r.phase)))), m_now});
            }
            r.prevA = a;
            r.prevB = b;
            r.started = true;
        }
        while (!m_pieces.isEmpty() && m_now - m_pieces.first().born > m_life) {
            m_pieces.removeFirst();
        }
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        for (const Piece &pc : std::as_const(m_pieces)) {
            QColor c = pc.color;
            c.setAlphaF(qBound(0.0, 1.0 - (m_now - pc.born) / m_life, 1.0));
            p.setBrush(c);
            p.drawPolygon(pc.quad);
        }
    }

private:
    QList<Ribbon> m_ribbons;
    QList<Piece> m_pieces;
    double m_now = 0;
    const double m_life = 6.0;
};

/** "Windows 7" (or the user's text) turning in 3D, shiny, moving about the screen */
class Text3DSaver : public Saver
{
public:
    explicit Text3DSaver(QWidget *parent = nullptr)
        : Saver(parent)
    {
        m_text = config().readEntry("Text3D", QStringLiteral("Windows 7"));
    }

protected:
    void step(double dt) override
    {
        m_t += dt;
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setRenderHint(QPainter::Antialiasing);
        QFont f(QStringLiteral("Segoe UI"));
        f.setBold(true);
        f.setPixelSize(qMax(8, int(170 * scale())));
        QPainterPath path;
        path.addText(0, 0, f, m_text);
        const QRectF br = path.boundingRect();
        const QPointF c(width() / 2.0 + std::sin(m_t * 0.31) * width() * 0.18, height() / 2.0 + std::sin(m_t * 0.23) * height() * 0.18);
        QTransform t;
        t.translate(c.x(), c.y());
        t.rotate(std::sin(m_t * 0.6) * 55, Qt::YAxis);
        t.rotate(std::sin(m_t * 0.4) * 18, Qt::XAxis);
        t.rotate(std::sin(m_t * 0.27) * 10, Qt::ZAxis);
        t.translate(-br.center().x(), -br.center().y());
        // depth: the same shape a few times, darker, behind
        for (int d = 6; d >= 1; --d) {
            QTransform td = t;
            td.translate(d * 2.0 * scale() * 2, d * 2.0 * scale() * 2);
            p.setTransform(td);
            p.fillPath(path, QColor(40 + d * 6, 60 + d * 8, 110 + d * 10));
        }
        p.setTransform(t);
        QLinearGradient g(br.topLeft(), br.bottomLeft());
        const double shine = 0.5 + 0.5 * std::sin(m_t * 1.3);
        g.setColorAt(0, QColor(250, 250, 255));
        g.setColorAt(qBound(0.05, shine, 0.95), QColor(150, 190, 255));
        g.setColorAt(1, QColor(40, 80, 170));
        p.fillPath(path, g);
    }

private:
    QString m_text;
    double m_t = 0;
};

/** The pictures of a folder (Pictures by default), fading one into the next */
class PhotosSaver : public Saver
{
public:
    explicit PhotosSaver(QWidget *parent = nullptr)
        : Saver(parent)
    {
        const KConfigGroup g = config();
        const QString dir = g.readEntry("PhotosFolder", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
        m_seconds = g.readEntry("PhotosSeconds", 6);
        QDirIterator it(dir, {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"), QStringLiteral("*.bmp")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext() && m_files.size() < 2000) {
            m_files << it.next();
        }
        if (g.readEntry("PhotosShuffle", true)) {
            std::shuffle(m_files.begin(), m_files.end(), *QRandomGenerator::global());
        }
        next();
    }

protected:
    void next()
    {
        m_prev = m_cur;
        m_cur = QImage();
        for (int tries = 0; tries < 10 && m_cur.isNull() && !m_files.isEmpty(); ++tries) {
            QImageReader r(m_files.at(m_index % m_files.size()));
            r.setAutoTransform(true);
            m_cur = r.read();
            ++m_index;
        }
        m_fade = 0;
        m_shown = 0;
    }
    void step(double dt) override
    {
        m_fade = qMin(1.0, m_fade + dt / 1.2);
        m_shown += dt;
        if (m_shown > m_seconds) {
            next();
        }
    }
    void drawFit(QPainter &p, const QImage &img, qreal opacity)
    {
        if (img.isNull()) {
            return;
        }
        const QSize s = img.size().scaled(size(), Qt::KeepAspectRatio);
        p.setOpacity(opacity);
        p.drawImage(QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s), img);
        p.setOpacity(1);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), Qt::black);
        drawFit(p, m_prev, 1 - m_fade);
        drawFit(p, m_cur, m_fade);
        if (m_files.isEmpty()) {
            p.setPen(Qt::white);
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Фотографій не знайдено"));
        }
    }

private:
    QStringList m_files;
    QImage m_cur, m_prev;
    int m_index = 0, m_seconds = 6;
    double m_fade = 1, m_shown = 0;
};

inline Saver *createSaver(const QString &id, QWidget *parent = nullptr)
{
    if (id == QLatin1String("bubbles")) {
        return new BubblesSaver(parent);
    }
    if (id == QLatin1String("mystify")) {
        return new MystifySaver(parent);
    }
    if (id == QLatin1String("ribbons")) {
        return new RibbonsSaver(parent);
    }
    if (id == QLatin1String("text3d")) {
        return new Text3DSaver(parent);
    }
    if (id == QLatin1String("photos")) {
        return new PhotosSaver(parent);
    }
    return new BlankSaver(parent);
}

/* ---------------- running full screen ---------------- */

/** Covers every screen until the mouse moves or a key is pressed; then, if wanted, the logon (lock) screen */
class FullScreen : public QObject
{
public:
    explicit FullScreen(const QString &id, bool lockAfter)
        : m_lock(lockAfter)
    {
        for (QScreen *s : QGuiApplication::screens()) {
            Saver *w = createSaver(id);
            if (id == QLatin1String("bubbles")) {
                w->backdrop = s->grabWindow(0); // the bubbles float over the desktop
            }
            w->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint);
            w->setGeometry(s->geometry());
            w->setCursor(Qt::BlankCursor);
            w->setMouseTracking(true);
            w->installEventFilter(this);
            w->show();
            w->raise();
            w->activateWindow();
            w->grabKeyboard();
            m_windows << w;
        }
        m_started.start();
    }

protected:
    bool eventFilter(QObject *, QEvent *e) override
    {
        if (m_started.elapsed() < 800) {
            return false; // the click or key that started it
        }
        switch (e->type()) {
        case QEvent::MouseMove: {
            const QPoint g = static_cast<QMouseEvent *>(e)->globalPosition().toPoint();
            if (m_firstMouse.isNull()) {
                m_firstMouse = g;
            } else if ((g - m_firstMouse).manhattanLength() > 8) {
                finish();
            }
            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::KeyPress:
        case QEvent::Wheel:
            finish();
            return true;
        default:
            break;
        }
        return false;
    }

private:
    void finish()
    {
        if (m_done) {
            return;
        }
        m_done = true;
        for (QWidget *w : std::as_const(m_windows)) {
            w->releaseKeyboard();
            w->close();
            w->deleteLater();
        }
        if (m_lock) {
            QDBusInterface ss(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"), QStringLiteral("org.freedesktop.ScreenSaver"));
            ss.call(QStringLiteral("Lock"));
        }
        deleteLater();
    }
    QList<QWidget *> m_windows;
    bool m_lock = false, m_done = false;
    QPoint m_firstMouse;
    QElapsedTimer m_started;
};

/** --screensaver-daemon: starts the chosen saver after the wait (KIdleTime), skipped while something
 *  (a video, a presentation) inhibits the screen saver */
class Daemon : public QObject
{
public:
    Daemon()
    {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-screensaverrc");
        m_watch.addPath(QFileInfo(path).absolutePath());
        connect(&m_watch, &QFileSystemWatcher::directoryChanged, this, [this] {
            QTimer::singleShot(300, this, [this] {
                reload();
            });
        });
        connect(KIdleTime::instance(), &KIdleTime::timeoutReached, this, [this](int id, int) {
            if (id == m_timeoutId) {
                start();
            }
        });
        reload();
    }

private:
    void reload()
    {
        const KConfigGroup g = config();
        const QString saver = g.readEntry("Saver", QStringLiteral("none"));
        const int minutes = qMax(1, g.readEntry("Wait", 10));
        if (saver == m_saver && minutes == m_minutes) {
            return;
        }
        m_saver = saver;
        m_minutes = minutes;
        if (m_timeoutId >= 0) {
            KIdleTime::instance()->removeIdleTimeout(m_timeoutId);
            m_timeoutId = -1;
        }
        if (saver != QLatin1String("none")) {
            m_timeoutId = KIdleTime::instance()->addIdleTimeout(minutes * 60 * 1000);
        }
    }
    void start()
    {
        QDBusInterface pm(QStringLiteral("org.freedesktop.PowerManagement.Inhibit"), QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"),
                          QStringLiteral("org.freedesktop.PowerManagement.Inhibit"));
        QDBusReply<bool> inhibited = pm.call(QStringLiteral("HasInhibit"));
        if (inhibited.isValid() && inhibited.value()) {
            return;
        }
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--screensaver"), m_saver});
        // (the lock after it is read from the settings by that process)
    }
    QFileSystemWatcher m_watch;
    QString m_saver;
    int m_minutes = -1;
    int m_timeoutId = -1;
};

/* ---------------- "Screen Saver Settings" ---------------- */

/** The little monitor (cut from a real Windows 7) with the saver running in its screen */
class MonitorPreview : public QWidget
{
public:
    MonitorPreview()
        : m_frame(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-aero/themecpl/monitor.png")))
    {
        setFixedSize(177, 146);
        if (QScreen *s = QGuiApplication::primaryScreen()) {
            m_desktop = s->grabWindow(0).scaled(152, 112, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }
    void show(const QString &id)
    {
        delete m_saver;
        m_saver = nullptr;
        if (id != QLatin1String("none")) {
            m_saver = createSaver(id, this);
            if (id == QLatin1String("bubbles")) {
                m_saver->backdrop = m_desktop;
            }
            m_saver->setGeometry(screenRect());
            m_saver->show();
            m_saver->lower();
        }
        update();
    }

protected:
    QRect screenRect() const
    {
        return QRect(11, 15, 152, 112);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        if (!m_saver) {
            p.drawPixmap(screenRect(), m_desktop);
        }
        p.drawPixmap(0, 0, m_frame);
    }
    void childEvent(QChildEvent *e) override
    {
        QWidget::childEvent(e);
    }

private:
    QPixmap m_frame;
    QPixmap m_desktop;
    QPointer<Saver> m_saver;
};

/** the frame drawn over the running preview (a child above the saver) */
class MonitorFrame : public QWidget
{
public:
    explicit MonitorFrame(QWidget *parent)
        : QWidget(parent)
        , m_frame(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-aero/themecpl/monitor.png")))
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(0, 0, 177, 146);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter(this).drawPixmap(0, 0, m_frame);
    }

private:
    QPixmap m_frame;
};

class SettingsDialog : public QDialog
{
public:
    explicit SettingsDialog(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Параметри заставки"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-desk")));
        setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 7, 7, 7);
        auto *tabs = new QTabWidget;
        auto *page = new QWidget;
        tabs->addTab(page, QStringLiteral("Заставка"));
        v->addWidget(tabs);
        auto *pv = new QVBoxLayout(page);
        pv->setContentsMargins(11, 13, 11, 11);
        pv->setSpacing(10);
        m_monitor = new MonitorPreview;
        new MonitorFrame(m_monitor);
        pv->addWidget(m_monitor, 0, Qt::AlignHCenter);
        pv->addSpacing(14);

        auto *box = new QGroupBox(QStringLiteral("Заставка"));
        auto *bv = new QVBoxLayout(box);
        bv->setSpacing(8);
        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        m_saver = new QComboBox;
        for (const Kind &k : kinds()) {
            m_saver->addItem(QString::fromUtf8(k.name), QLatin1String(k.id));
        }
        m_saver->setMinimumWidth(200);
        row->addWidget(m_saver, 1);
        m_settings = new QPushButton(QStringLiteral("Параметри..."));
        m_preview = new QPushButton(QStringLiteral("Переглянути"));
        for (QPushButton *b : {m_settings, m_preview}) {
            b->setMinimumWidth(87);
            row->addWidget(b);
        }
        bv->addLayout(row);
        auto *wait = new QHBoxLayout;
        wait->setSpacing(6);
        m_waitLabel = new QLabel(QStringLiteral("Чекати:"));
        wait->addWidget(m_waitLabel);
        m_wait = new QSpinBox;
        m_wait->setRange(1, 9999);
        m_wait->setFixedWidth(60);
        wait->addWidget(m_wait);
        m_minutes = new QLabel(QStringLiteral("хв."));
        wait->addWidget(m_minutes);
        m_lock = new QCheckBox(QStringLiteral("Починати з екрана входу в систему"));
        wait->addWidget(m_lock);
        wait->addStretch(1);
        bv->addLayout(wait);
        pv->addWidget(box);

        auto *power = new QGroupBox(QStringLiteral("Керування живленням"));
        auto *pw = new QVBoxLayout(power);
        auto *pt = new QLabel(QStringLiteral("Заощаджуйте енергію або досягайте максимальної продуктивності, налаштовуючи яскравість дисплея та інші параметри живлення."));
        pt->setWordWrap(true);
        pw->addWidget(pt);
        pw->addSpacing(4);
        pw->addWidget(new Link(QStringLiteral("Змінити параметри живлення"), [] {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--power")});
        }));
        pv->addWidget(power);

        auto *buttons = new QDialogButtonBox;
        auto *ok = buttons->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
        buttons->addButton(QStringLiteral("Скасувати"), QDialogButtonBox::RejectRole);
        m_apply = buttons->addButton(QStringLiteral("Застосувати"), QDialogButtonBox::ApplyRole);
        for (QAbstractButton *b : buttons->buttons()) {
            b->setMinimumWidth(75);
        }
        v->addWidget(buttons);
        setFixedSize(478, 518); // as on Windows 7

        const KConfigGroup g = config();
        m_saver->setCurrentIndex(qMax(0, m_saver->findData(g.readEntry("Saver", QStringLiteral("none")))));
        m_wait->setValue(g.readEntry("Wait", 1));
        m_lock->setChecked(g.readEntry("Lock", false));
        m_apply->setEnabled(false);
        sync();

        connect(m_saver, &QComboBox::currentIndexChanged, this, [this] {
            sync();
            m_apply->setEnabled(true);
        });
        connect(m_wait, &QSpinBox::valueChanged, this, [this] {
            m_apply->setEnabled(true);
        });
        connect(m_lock, &QCheckBox::toggled, this, [this] {
            m_apply->setEnabled(true);
        });
        connect(m_preview, &QPushButton::clicked, this, [this] {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--screensaver"), id(), QStringLiteral("--preview")});
        });
        connect(m_settings, &QPushButton::clicked, this, [this] {
            options();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(ok, &QPushButton::clicked, this, [this] {
            if (m_apply->isEnabled()) {
                save();
            }
            accept();
        });
        connect(m_apply, &QPushButton::clicked, this, [this] {
            save();
        });
    }

private:
    QString id() const
    {
        return m_saver->currentData().toString();
    }
    void sync()
    {
        const bool any = id() != QLatin1String("none");
        for (QWidget *w : std::initializer_list<QWidget *>{m_preview, m_wait, m_waitLabel, m_minutes, m_lock}) {
            w->setEnabled(any);
        }
        m_settings->setEnabled(id() == QLatin1String("text3d") || id() == QLatin1String("photos") || id() == QLatin1String("bubbles")
                               || id() == QLatin1String("mystify") || id() == QLatin1String("ribbons"));
        m_monitor->show(id());
    }
    void save()
    {
        KConfigGroup g = config();
        g.writeEntry("Saver", id());
        g.writeEntry("Wait", m_wait->value());
        g.writeEntry("Lock", m_lock->isChecked());
        g.sync();
        // the saver replaces KDE's own lock-after-idle (the lock comes after the saver if asked)
        KConfigGroup locker(KSharedConfig::openConfig(QStringLiteral("kscreenlockerrc")), QStringLiteral("Daemon"));
        locker.writeEntry("Autolock", false, KConfig::Notify);
        locker.sync();
        // the resident part that starts it
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--screensaver-daemon")});
        m_apply->setEnabled(false);
    }
    void options()
    {
        if (id() == QLatin1String("text3d")) {
            QDialog d(this);
            d.setWindowTitle(QStringLiteral("Параметри тривимірного тексту"));
            auto *f = new QFormLayout(&d);
            auto *text = new QLineEdit(config().readEntry("Text3D", QStringLiteral("Windows 7")));
            text->setMaxLength(20);
            f->addRow(QStringLiteral("Власний текст:"), text);
            auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Скасувати"));
            f->addRow(bb);
            connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
            connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
            if (d.exec() == QDialog::Accepted) {
                KConfigGroup g = config();
                g.writeEntry("Text3D", text->text().isEmpty() ? QStringLiteral("Windows 7") : text->text());
                g.sync();
                m_monitor->show(id());
            }
        } else if (id() == QLatin1String("photos")) {
            QDialog d(this);
            d.setWindowTitle(QStringLiteral("Параметри заставки «Фотографії»"));
            auto *f = new QFormLayout(&d);
            KConfigGroup g = config();
            auto *dirRow = new QHBoxLayout;
            auto *dir = new QLineEdit(g.readEntry("PhotosFolder", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)));
            auto *browse = new QPushButton(QStringLiteral("Огляд..."));
            dirRow->addWidget(dir, 1);
            dirRow->addWidget(browse);
            f->addRow(QStringLiteral("Використовувати рисунки з:"), dirRow);
            connect(browse, &QPushButton::clicked, &d, [dir, &d] {
                const QString picked = QFileDialog::getExistingDirectory(&d, QStringLiteral("Огляд папок"), dir->text());
                if (!picked.isEmpty()) {
                    dir->setText(picked);
                }
            });
            auto *speed = new QComboBox;
            speed->addItem(QStringLiteral("Повільна"), 10);
            speed->addItem(QStringLiteral("Середня"), 6);
            speed->addItem(QStringLiteral("Швидка"), 3);
            speed->setCurrentIndex(qMax(0, speed->findData(g.readEntry("PhotosSeconds", 6))));
            f->addRow(QStringLiteral("Швидкість показу слайдів:"), speed);
            auto *shuffle = new QCheckBox(QStringLiteral("Перемішати рисунки"));
            shuffle->setChecked(g.readEntry("PhotosShuffle", true));
            f->addRow(QString(), shuffle);
            auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
            bb->button(QDialogButtonBox::Save)->setText(QStringLiteral("Зберегти"));
            bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Скасувати"));
            f->addRow(bb);
            connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
            connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
            if (d.exec() == QDialog::Accepted) {
                g.writeEntry("PhotosFolder", dir->text());
                g.writeEntry("PhotosSeconds", speed->currentData().toInt());
                g.writeEntry("PhotosShuffle", shuffle->isChecked());
                g.sync();
                m_monitor->show(id());
            }
        } else {
            // what Windows 7 says for the savers without options
            QMessageBox::information(this, QStringLiteral("Параметри заставки"), QStringLiteral("Ця заставка не має параметрів, які можна настроїти."));
        }
    }

    MonitorPreview *m_monitor = nullptr;
    QComboBox *m_saver = nullptr;
    QPushButton *m_settings = nullptr;
    QPushButton *m_preview = nullptr;
    QPushButton *m_apply = nullptr;
    QLabel *m_waitLabel = nullptr;
    QLabel *m_minutes = nullptr;
    QSpinBox *m_wait = nullptr;
    QCheckBox *m_lock = nullptr;
};
}
