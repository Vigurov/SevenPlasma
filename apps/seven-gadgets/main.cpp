#include "win7i18n.h"
/*
 * Windows 7 Desktop Gadgets: the gadget gallery window and the Weather gadget.
 *
 * One process keeps all gadgets on the desktop; "seven-gadgets --gallery" asks the running
 * instance (or starts one) to show the gallery. Weather data: Open-Meteo (CC BY 4.0).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "win7chrome.h"

#include <AeroQt/insetwindow.h>
#include <AeroQt/stylesheet.h>

#include <KWindowSystem>
#include <KX11Extras>

#include <QApplication>
#include <KWindowEffects>
#include <QDesktopServices>
#include <QDirIterator>
#include <QGraphicsDropShadowEffect>
#include <QFileDialog>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QVariantAnimation>
#include <QWindow>
#include <QXmlStreamReader>
#include <QListWidget>
#include <QPointer>
#include <QCheckBox>
#include <QComboBox>
#include <QTimeZone>
#include <QCollator>
#include <QProcess>
#include <QButtonGroup>
#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>

#include <cmath>
#include <numeric>
#include <QTextDocumentFragment>
#include <QNetworkRequest>

namespace
{
const QString kServerName = QStringLiteral("seven-gadgets");

QSettings &store()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-gadgetsrc"), QSettings::IniFormat);
    return s;
}

QNetworkAccessManager &net()
{
    static QNetworkAccessManager m;
    return m;
}

/** A picture of the genuine Windows 7 gadgets, ~/.local/share/seven-gadgets/<rel> */
QString gadgetData(const QString &rel)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/seven-gadgets/") + rel;
}

const QPixmap &gadgetPixmap(const QString &rel)
{
    static QHash<QString, QPixmap> cache;
    auto it = cache.find(rel);
    if (it == cache.end()) {
        it = cache.insert(rel, QPixmap(gadgetData(rel)));
    }
    return *it;
}

/** One state of a vertical strip (normal / hover / pressed ... stacked like in sidebar.exe) */
QPixmap stripFrame(const QPixmap &strip, int states, int index)
{
    const int h = strip.height() / states;
    return strip.copy(0, h * qBound(0, index, states - 1), strip.width(), h);
}

/** Paints a pixmap stretched over r keeping its m-pixel border unscaled */
void paintNineSlice(QPainter &p, const QRectF &r, const QPixmap &pm, int m)
{
    const qreal w = pm.width();
    const qreal h = pm.height();
    const qreal xs[4] = {r.left(), r.left() + m, r.right() - m, r.right()};
    const qreal ys[4] = {r.top(), r.top() + m, r.bottom() - m, r.bottom()};
    const qreal sx[4] = {0, qreal(m), w - m, w};
    const qreal sy[4] = {0, qreal(m), h - m, h};
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            p.drawPixmap(QRectF(xs[x], ys[y], xs[x + 1] - xs[x], ys[y + 1] - ys[y]), pm, QRectF(sx[x], sy[y], sx[x + 1] - sx[x], sy[y + 1] - sy[y]));
        }
    }
}

/** Black text with the white glow Windows 7 puts under captions on glass */
void drawGlowText(QPainter &p, const QRect &r, int flags, const QString &text)
{
    p.setPen(QColor(255, 255, 255, 120));
    for (QPoint o : {QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1), QPoint(-1, -1), QPoint(1, 1), QPoint(-1, 1), QPoint(1, -1)}) {
        p.drawText(r.translated(o), flags, text);
    }
    p.setPen(Qt::black);
    p.drawText(r, flags, text);
}

/** Text with the soft dark halo the gadgets put under white captions */
void drawShadowText(QPainter &p, const QRectF &r, int flags, const QString &text, const QColor &color, const QColor &shadow = QColor(0, 0, 0, 110))
{
    p.setPen(shadow);
    p.drawText(r.translated(1, 1), flags, text);
    p.setPen(color);
    p.drawText(r, flags, text);
}

/* ---------------- weather data ---------------- */

enum class Sky {
    Clear,
    PartlyCloudy,
    Cloudy,
    Fog,
    Drizzle,
    Rain,
    Snow,
    Thunder,
};

Sky skyFor(int code)
{
    switch (code) {
    case 0:
    case 1:
        return Sky::Clear;
    case 2:
        return Sky::PartlyCloudy;
    case 3:
        return Sky::Cloudy;
    case 45:
    case 48:
        return Sky::Fog;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
        return Sky::Drizzle;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
        return Sky::Snow;
    case 95:
    case 96:
    case 99:
        return Sky::Thunder;
    default:
        return Sky::Rain;
    }
}

QString conditionText(int code)
{
    switch (code) {
    case 0:
        return tr7("Clear");
    case 1:
        return tr7("Mostly Clear");
    case 2:
        return tr7("Partly Cloudy");
    case 3:
        return tr7("Cloudy");
    case 45:
    case 48:
        return tr7("Fog");
    case 51:
    case 53:
    case 55:
        return tr7("Drizzle");
    case 56:
    case 57:
        return tr7("Freezing Drizzle");
    case 61:
        return tr7("Light Rain");
    case 63:
        return tr7("Rain");
    case 65:
        return tr7("Heavy Rain");
    case 66:
    case 67:
        return tr7("Freezing Rain");
    case 71:
        return tr7("Light Snow");
    case 73:
        return tr7("Snow");
    case 75:
        return tr7("Heavy Snow");
    case 77:
        return tr7("Snow Grains");
    case 80:
    case 81:
        return tr7("Showers");
    case 82:
        return tr7("Heavy Showers");
    case 85:
    case 86:
        return tr7("Snow Showers");
    case 95:
        return tr7("Thunderstorm");
    default:
        return tr7("Thunderstorm with Hail");
    }
}

struct Day {
    QDate date;
    int code = 0;
    double max = 0, min = 0;
};

struct Weather {
    bool valid = false;
    double temp = 0;
    int code = 0;
    bool day = true;
    QList<Day> days;
};

struct Place {
    QString name; // "Київ, Місто Київ"
    double lat = 0, lon = 0;
};

/* ---------------- painting ---------------- */

void drawCloud(QPainter &p, QPointF c, qreal s, int alpha)
{
    p.setPen(Qt::NoPen);
    const QList<QRectF> puffs{{-1.0, -0.2, 1.1, 0.8}, {-0.45, -0.55, 1.0, 1.0}, {0.25, -0.35, 0.95, 0.85}, {-0.8, 0.1, 1.9, 0.55}};
    for (const QRectF &r : puffs) {
        const QRectF rr(c.x() + r.x() * s, c.y() + r.y() * s, r.width() * s, r.height() * s);
        QRadialGradient g(rr.center(), rr.width() / 1.6);
        g.setColorAt(0, QColor(255, 255, 255, alpha));
        g.setColorAt(0.7, QColor(235, 240, 245, alpha * 2 / 3));
        g.setColorAt(1, QColor(220, 228, 235, 0));
        p.setBrush(g);
        p.drawEllipse(rr);
    }
}

/** Background of the gadget like the Windows 7 weather pictures: dark blue glass + the weather */
void paintSky(QPainter &p, const QRectF &r, Sky sky, bool day, quint32 seed)
{
    QLinearGradient base(r.topLeft(), r.bottomLeft());
    const bool dark = !day || sky == Sky::Rain || sky == Sky::Thunder || sky == Sky::Drizzle;
    if (sky == Sky::Clear && day) {
        base.setColorAt(0, QColor(0x2f, 0x7f, 0xc8));
        base.setColorAt(1, QColor(0x8c, 0xc4, 0xec));
    } else if (dark) {
        base.setColorAt(0, QColor(0x0f, 0x24, 0x3a));
        base.setColorAt(1, QColor(0x22, 0x44, 0x62));
    } else {
        base.setColorAt(0, QColor(0x45, 0x5e, 0x78));
        base.setColorAt(1, QColor(0x86, 0x9a, 0xae));
    }
    p.fillRect(r, base);
    QRandomGenerator rnd(seed);
    const qreal w = r.width(), h = r.height();

    switch (sky) {
    case Sky::Clear:
        if (day) {
            QRadialGradient g(QPointF(r.left() + w * 0.28, r.top() + h * 0.35), h * 0.9);
            g.setColorAt(0, QColor(255, 248, 200, 255));
            g.setColorAt(0.18, QColor(255, 220, 90, 240));
            g.setColorAt(0.45, QColor(255, 200, 80, 60));
            g.setColorAt(1, QColor(255, 200, 80, 0));
            p.fillRect(r, g);
        } else {
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 40; ++i) {
                p.setBrush(QColor(255, 255, 255, 80 + rnd.bounded(150)));
                const qreal s = 0.6 + rnd.bounded(1.4);
                p.drawEllipse(QPointF(r.left() + rnd.bounded(w), r.top() + rnd.bounded(h)), s / 2, s / 2);
            }
            QRadialGradient g(QPointF(r.left() + w * 0.25, r.top() + h * 0.35), h * 0.35);
            g.setColorAt(0, QColor(255, 255, 240));
            g.setColorAt(0.45, QColor(240, 240, 220));
            g.setColorAt(0.5, QColor(240, 240, 220, 60));
            g.setColorAt(1, QColor(240, 240, 220, 0));
            p.fillRect(r, g);
        }
        break;
    case Sky::PartlyCloudy:
        if (day) {
            QRadialGradient g(QPointF(r.left() + w * 0.3, r.top() + h * 0.3), h * 0.8);
            g.setColorAt(0, QColor(255, 240, 170, 230));
            g.setColorAt(0.25, QColor(255, 215, 100, 140));
            g.setColorAt(1, QColor(255, 200, 80, 0));
            p.fillRect(r, g);
        }
        drawCloud(p, QPointF(r.left() + w * 0.42, r.top() + h * 0.55), h * 0.45, 200);
        drawCloud(p, QPointF(r.left() + w * 0.12, r.top() + h * 0.78), h * 0.3, 150);
        break;
    case Sky::Cloudy:
    case Sky::Fog:
        for (int i = 0; i < 6; ++i) {
            drawCloud(p, QPointF(r.left() + rnd.bounded(w), r.top() + h * 0.2 + rnd.bounded(h * 0.7)), h * (0.3 + rnd.bounded(0.3)), sky == Sky::Fog ? 90 : 170);
        }
        if (sky == Sky::Fog) {
            p.fillRect(r, QColor(220, 225, 230, 90));
        }
        break;
    case Sky::Drizzle:
    case Sky::Rain:
    case Sky::Thunder: {
        // long slanted streaks with a few bright drops, like the Windows 7 rain picture
        const int n = sky == Sky::Drizzle ? 45 : 80;
        for (int i = 0; i < n; ++i) {
            const qreal x = r.left() + rnd.bounded(w * 1.3) - w * 0.15;
            const qreal y = r.top() + rnd.bounded(h * 1.2) - h * 0.2;
            const qreal len = h * (0.15 + rnd.bounded(0.35));
            QLinearGradient sg(QPointF(x, y), QPointF(x + len * 0.25, y + len));
            sg.setColorAt(0, QColor(170, 210, 240, 0));
            sg.setColorAt(1, QColor(190, 225, 250, 60 + rnd.bounded(90)));
            p.setPen(QPen(QBrush(sg), 0.8 + rnd.bounded(1.2)));
            p.drawLine(QPointF(x, y), QPointF(x + len * 0.25, y + len));
        }
        p.setPen(Qt::NoPen);
        for (int i = 0; i < n / 3; ++i) {
            const QPointF c(r.left() + rnd.bounded(w), r.top() + rnd.bounded(h));
            const qreal s = 1.2 + rnd.bounded(2.4);
            QRadialGradient g(c, s);
            g.setColorAt(0, QColor(210, 235, 255, 200));
            g.setColorAt(1, QColor(120, 180, 230, 0));
            p.setBrush(g);
            p.drawEllipse(c, s, s * 1.3);
        }
        if (sky == Sky::Thunder) {
            QPainterPath bolt;
            const qreal x0 = r.left() + w * 0.3, y0 = r.top();
            bolt.moveTo(x0, y0);
            bolt.lineTo(x0 - w * 0.04, y0 + h * 0.35);
            bolt.lineTo(x0 + w * 0.02, y0 + h * 0.33);
            bolt.lineTo(x0 - w * 0.03, y0 + h * 0.7);
            p.setPen(QPen(QColor(230, 240, 255, 210), 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawPath(bolt);
        }
        break;
    }
    case Sky::Snow:
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 70; ++i) {
            const QPointF c(r.left() + rnd.bounded(w), r.top() + rnd.bounded(h));
            const qreal s = 0.8 + rnd.bounded(2.2);
            p.setBrush(QColor(255, 255, 255, 120 + rnd.bounded(135)));
            p.drawEllipse(c, s, s);
        }
        break;
    }
    // darker bottom so the text stays readable
    QLinearGradient shade(r.topLeft(), r.bottomLeft());
    shade.setColorAt(0, QColor(0, 0, 0, 0));
    shade.setColorAt(1, QColor(0, 10, 25, 110));
    p.fillRect(r, shade);
}

/** Small icon for the 3 day forecast */
void paintSmallIcon(QPainter &p, const QRectF &r, Sky sky)
{
    p.save();
    p.setPen(Qt::NoPen);
    const QPointF c = r.center();
    const qreal s = qMin(r.width(), r.height());
    auto drops = [&](QColor col, bool flakes) {
        for (int row = 0; row < 3; ++row) {
            for (int i = 0; i < 4 - (row % 2); ++i) {
                const QPointF d(r.left() + s * 0.15 + i * s * 0.22 + (row % 2) * s * 0.11, r.top() + s * 0.25 + row * s * 0.22);
                p.setBrush(col);
                if (flakes) {
                    p.drawEllipse(d, s * 0.06, s * 0.06);
                } else {
                    p.drawEllipse(d, s * 0.045, s * 0.075);
                }
            }
        }
    };
    switch (sky) {
    case Sky::Clear: {
        QRadialGradient g(c, s * 0.42);
        g.setColorAt(0, QColor(255, 245, 170));
        g.setColorAt(0.55, QColor(255, 200, 60));
        g.setColorAt(0.6, QColor(255, 200, 60, 80));
        g.setColorAt(1, QColor(255, 200, 60, 0));
        p.setBrush(g);
        p.drawEllipse(c, s * 0.42, s * 0.42);
        break;
    }
    case Sky::PartlyCloudy:
    case Sky::Cloudy:
    case Sky::Fog:
        if (sky == Sky::PartlyCloudy) {
            p.setBrush(QColor(255, 210, 80));
            p.drawEllipse(QPointF(c.x() - s * 0.15, c.y() - s * 0.12), s * 0.2, s * 0.2);
        }
        drawCloud(p, QPointF(c.x() + s * 0.05, c.y() + s * 0.05), s * 0.32, 230);
        break;
    case Sky::Snow:
        drops(QColor(240, 248, 255), true);
        break;
    default:
        drops(QColor(90, 175, 235), false);
        break;
    }
    p.restore();
}

QString dayName(const QDate &d)
{
    return QLocale().standaloneDayName(d.dayOfWeek(), QLocale::LongFormat); // in the user's language
}

/* ---------------- gadget base ---------------- */

/**
 * Frameless gadget window kept below other windows. The hover toolbar
 * (close, size, settings, drag grip) is painted to the right of the body.
 */
class Gadget : public QWidget
{
public:
    std::function<void(Gadget *)> removed;
    QString id;

    explicit Gadget(const QString &gadgetId)
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnBottomHint)
        , id(gadgetId)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);
        m_big = store().value(id + QStringLiteral("/Big"), false).toBool();
        m_hideTimer.setSingleShot(true);
        m_hideTimer.setInterval(350);
        connect(&m_hideTimer, &QTimer::timeout, this, [this] {
            m_hover = false;
            update();
        });
    }

    void place()
    {
        resizeToContent();
        const QPoint saved = store().value(id + QStringLiteral("/Pos"), QPoint(-1, -1)).toPoint();
        const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
        if (saved.x() < 0) {
            move(avail.right() - width() - 20, avail.top() + 20 + store().value(QStringLiteral("General/Count"), 0).toInt() * 30);
        } else {
            move(saved);
        }
        show();
        KX11Extras::setState(winId(), NET::KeepBelow | NET::SkipTaskbar | NET::SkipPager);
        KX11Extras::setOnAllDesktops(winId(), true);
    }

    /** Off-screen rendering: sized, never shown on the desktop */
    void grabForTest()
    {
        resizeToContent();
        m_hover = qEnvironmentVariableIsSet("WIN7_GADGET_HOVER");
    }

protected:
    static constexpr int kBar = 21; // sidebar.exe toolbar buttons are 21x21
    static constexpr int kGap = 2;

    virtual QSize bodySize() const = 0;
    virtual void paintBody(QPainter &p, const QRectF &r) = 0;
    virtual void configure() = 0;
    virtual bool hasSizeButton() const
    {
        return true;
    }
    virtual bool hasSettingsButton() const
    {
        return true;
    }
    /** false: the gadget paints its own shape, without the glass plate */
    virtual bool glassFrame() const
    {
        return true;
    }

    QList<int> buttons() const
    {
        QList<int> order{Close};
        if (hasSizeButton()) {
            order << Size;
        }
        if (hasSettingsButton()) {
            order << Settings;
        }
        order << Grip;
        return order;
    }

    bool isBig() const
    {
        return m_big;
    }

    void resizeToContent()
    {
        const QSize b = bodySize();
        setFixedSize(b.width() + kGap + kBar, qMax(b.height(), barHeight()));
    }

    int barHeight() const
    {
        return int(buttons().size()) * kBar;
    }

    QRectF bodyRect() const
    {
        const QSize b = bodySize();
        return QRectF(0.5, 0.5, b.width() - 1, b.height() - 1);
    }

    QRectF barRect() const
    {
        const QSize b = bodySize();
        return QRectF(b.width() + kGap, 0, kBar, barHeight());
    }

    QRectF buttonRect(int i) const
    {
        const QRectF br = barRect();
        return QRectF(br.left(), br.top() + i * kBar, kBar, kBar);
    }

    enum Button {
        Close,
        Size,
        Settings,
        Grip,
        None,
    };

    Button buttonAt(QPointF pos) const
    {
        if (!m_hover) {
            return None;
        }
        const QList<int> order = buttons();
        for (int i = 0; i < order.size(); ++i) {
            if (buttonRect(i).contains(pos)) {
                return Button(order.at(i));
            }
        }
        return None;
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
        const QRectF body = bodyRect();
        if (!glassFrame()) {
            paintBody(p, body);
            if (m_hover) {
                paintBar(p);
            }
            return;
        }
        QPainterPath clip;
        clip.addRoundedRect(body, 7, 7);
        p.save();
        p.setClipPath(clip);
        paintBody(p, body);
        // glass gloss on top
        QLinearGradient gloss(body.topLeft(), QPointF(body.left(), body.top() + body.height() * 0.5));
        gloss.setColorAt(0, QColor(255, 255, 255, 50));
        gloss.setColorAt(1, QColor(255, 255, 255, 0));
        p.fillRect(body, gloss);
        p.restore();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 120), 1));
        p.drawRoundedRect(body, 7, 7);
        p.setPen(QPen(QColor(170, 210, 230, 170), 1));
        p.drawRoundedRect(body.adjusted(1, 1, -1, -1), 6, 6);
        if (m_hover) {
            paintBar(p);
        }
    }

    /** The genuine toolbar: close, larger/smaller, options and the drag grip stacked into one column */
    void paintBar(QPainter &p)
    {
        const QList<int> order = buttons();
        for (int i = 0; i < order.size(); ++i) {
            const Button b = Button(order.at(i));
            QString name;
            switch (b) {
            case Close:
                name = QStringLiteral("close");
                break;
            case Size:
                name = m_big ? QStringLiteral("smaller") : QStringLiteral("larger");
                break;
            case Settings:
                name = QStringLiteral("settings");
                break;
            default:
                name = QStringLiteral("grip");
                break;
            }
            const QPixmap &strip = gadgetPixmap(QStringLiteral("toolbar/%1.png").arg(name));
            const int states = b == Grip ? 1 : 3;
            const int state = b != m_hot ? 0 : (m_down && m_pressed == b ? 2 : 1);
            p.drawPixmap(buttonRect(i).topLeft(), stripFrame(strip, states, state));
        }
    }

    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::HoverEnter) {
            m_hideTimer.stop();
            m_hover = true;
            update();
        } else if (e->type() == QEvent::HoverLeave) {
            m_hot = None;
            m_hideTimer.start();
        }
        return QWidget::event(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_dragging) {
            move(e->globalPosition().toPoint() - m_dragOffset);
            return;
        }
        const Button hot = buttonAt(e->position());
        if (hot != m_hot) {
            m_hot = hot;
            update();
        }
        setCursor(hot == Grip ? Qt::SizeAllCursor : Qt::ArrowCursor);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        m_pressed = buttonAt(e->position());
        m_down = true;
        update();
        if (m_pressed == None || m_pressed == Grip) {
            if (bodyRect().contains(e->position()) || m_pressed == Grip) {
                m_dragging = true;
                m_dragMoved = false;
                m_dragStart = e->globalPosition().toPoint();
                m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft();
            }
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        m_down = false;
        update();
        if (m_dragging) {
            m_dragging = false;
            if ((e->globalPosition().toPoint() - m_dragStart).manhattanLength() > 3) {
                store().setValue(id + QStringLiteral("/Pos"), pos());
                store().sync();
            } else {
                bodyClicked(e->position());
            }
            return;
        }
        const Button b = buttonAt(e->position());
        if (b != m_pressed) {
            return;
        }
        switch (b) {
        case Close:
            if (removed) {
                removed(this);
            }
            break;
        case Size:
            m_big = !m_big;
            store().setValue(id + QStringLiteral("/Big"), m_big);
            store().sync();
            resizeToContent();
            update();
            break;
        case Settings:
            configure();
            break;
        default:
            break;
        }
    }

    virtual void bodyClicked(QPointF)
    {
    }

private:
    bool m_big = false;
    bool m_hover = false;
    Button m_hot = None;
    Button m_pressed = None;
    bool m_down = false;
    bool m_dragging = false;
    bool m_dragMoved = false;
    QPoint m_dragStart;
    QPoint m_dragOffset;
    QTimer m_hideTimer;
};

/* ---------------- Weather gadget ---------------- */

class WeatherSettings : public QDialog
{
public:
    Place place;
    bool celsius = true;

    WeatherSettings(const Place &current, bool useCelsius, QWidget *parent = nullptr)
        : QDialog(parent)
        , place(current)
        , celsius(useCelsius)
    {
        setWindowTitle(tr7("Weather"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("gadgets")));
        setFixedWidth(340);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 10);
        v->setSpacing(0);

        // dark header with the gadget name, like Windows 7 gadget settings
        auto *header = new QLabel(tr7("Weather"));
        header->setFixedHeight(44);
        header->setContentsMargins(16, 0, 0, 0);
        QFont hf = header->font();
        hf.setPixelSize(17);
        header->setFont(hf);
        header->setStyleSheet(QStringLiteral("QLabel { color: white; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3a4350, stop:1 #12171e); }"));
        v->addWidget(header);

        auto *body = new QWidget;
        auto *bv = new QVBoxLayout(body);
        bv->setContentsMargins(14, 12, 14, 8);
        bv->setSpacing(4);
        bv->addWidget(new QLabel(tr7("Current location:")));
        m_current = new QLabel(place.name.isEmpty() ? tr7("Not selected") : place.name);
        bv->addWidget(m_current);
        bv->addSpacing(12);
        bv->addWidget(new QLabel(tr7("Select current location")));
        auto *row = new QHBoxLayout;
        row->setSpacing(0);
        m_search = new QLineEdit;
        m_search->setPlaceholderText(tr7("Search for location"));
        row->addWidget(m_search, 1);
        auto *find = new QToolButton;
        find->setIcon(QIcon::fromTheme(QStringLiteral("seven-explorer-search"), QIcon::fromTheme(QStringLiteral("gtk-search"))));
        find->setToolTip(tr7("Search"));
        find->setFixedSize(26, 24);
        row->addWidget(find);
        bv->addLayout(row);
        m_results = new QListWidget;
        m_results->setMaximumHeight(90);
        m_results->hide();
        bv->addWidget(m_results);
        bv->addSpacing(14);
        bv->addWidget(new QLabel(tr7("Show temperature in:")));
        auto *f = new QRadioButton(tr7("Fahrenheit"));
        auto *c = new QRadioButton(tr7("Celsius"));
        (celsius ? c : f)->setChecked(true);
        bv->addWidget(f);
        bv->addWidget(c);
        bv->addSpacing(8);
        m_status = new QLabel;
        m_status->setWordWrap(true);
        bv->addWidget(m_status);
        bv->addStretch(1);
        v->addWidget(body, 1);

        auto *line = new QFrame;
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
        v->addWidget(line);
        v->addSpacing(10);
        auto *btns = new QHBoxLayout;
        btns->setContentsMargins(14, 0, 14, 0);
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        ok->setMinimumWidth(86);
        cancel->setMinimumWidth(86);
        btns->addWidget(ok);
        btns->addWidget(cancel);
        v->addLayout(btns);
        setMinimumHeight(390);

        connect(ok, &QPushButton::clicked, this, [this, c] {
            celsius = c->isChecked();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(find, &QToolButton::clicked, this, [this] {
            search();
        });
        connect(m_search, &QLineEdit::returnPressed, this, [this] {
            search();
        });
        connect(m_results, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            choose(item);
        });
    }

private:
    void search()
    {
        const QString text = m_search->text().trimmed();
        if (text.isEmpty()) {
            return;
        }
        m_status->setText(tr7("Searching..."));
        QUrl url(QStringLiteral("https://geocoding-api.open-meteo.com/v1/search"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("name"), text);
        q.addQueryItem(QStringLiteral("count"), QStringLiteral("8"));
        q.addQueryItem(QStringLiteral("language"), QStringLiteral("uk"));
        url.setQuery(q);
        QNetworkReply *reply = net().get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, text] {
            reply->deleteLater();
            const QJsonArray results = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("results")).toArray();
            m_results->clear();
            if (reply->error() != QNetworkReply::NoError) {
                m_status->setText(tr7("No Internet connection. Try again later."));
                return;
            }
            if (results.isEmpty()) {
                m_status->setText(tr7("\"%1\" not found.").arg(text));
                m_results->hide();
                return;
            }
            for (const QJsonValue &v : results) {
                const QJsonObject o = v.toObject();
                QStringList parts{o.value(QStringLiteral("name")).toString()};
                const QString admin = o.value(QStringLiteral("admin1")).toString();
                if (!admin.isEmpty() && admin != parts.first()) {
                    parts << admin;
                }
                const QString country = o.value(QStringLiteral("country")).toString();
                auto *item = new QListWidgetItem(parts.join(QStringLiteral(", ")) + (country.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(country)), m_results);
                item->setData(Qt::UserRole, parts.join(QStringLiteral(", ")));
                item->setData(Qt::UserRole + 1, o.value(QStringLiteral("latitude")).toDouble());
                item->setData(Qt::UserRole + 2, o.value(QStringLiteral("longitude")).toDouble());
            }
            if (results.size() == 1) {
                m_results->hide();
                choose(m_results->item(0));
            } else {
                m_results->show();
                m_status->setText(tr7("Select a location from the list."));
            }
        });
    }

    void choose(QListWidgetItem *item)
    {
        place.name = item->data(Qt::UserRole).toString();
        place.lat = item->data(Qt::UserRole + 1).toDouble();
        place.lon = item->data(Qt::UserRole + 2).toDouble();
        m_current->setText(place.name);
        m_results->hide();
        m_search->clear();
        m_status->setText(tr7("\"%1\" found.\nClick OK to apply.").arg(place.name));
    }

    QLabel *m_current;
    QLineEdit *m_search;
    QListWidget *m_results;
    QLabel *m_status;
};

/* ---------------- the pictures of the Windows 7 Weather gadget ---------------- */

/** Open-Meteo (WMO) weather code -> the MSN "SkyCode" the Windows 7 gadget pictures are named after */
int msnSkyCode(int wmo, bool day)
{
    switch (wmo) {
    case 0:
        return day ? 32 : 31;
    case 1:
        return day ? 34 : 33;
    case 2:
        return day ? 30 : 29;
    case 3:
        return 26;
    case 45:
    case 48:
        return 20;
    case 51:
    case 53:
    case 55:
        return 9;
    case 56:
    case 57:
        return 8;
    case 61:
    case 63:
        return 11;
    case 65:
        return 12;
    case 66:
    case 67:
        return 10;
    case 71:
        return 14;
    case 73:
        return 16;
    case 75:
        return 43;
    case 77:
        return 13;
    case 80:
        return day ? 39 : 45;
    case 81:
        return 11;
    case 82:
        return 12;
    case 85:
        return 14;
    case 86:
        return 16;
    case 95:
        return 4;
    case 96:
    case 99:
        return 17;
    }
    return day ? 32 : 31;
}

/** weather.js WeatherState(): which weather picture goes with a SkyCode */
QString weatherState(int sky)
{
    switch (sky) {
    case 26: case 27: case 28:
        return QStringLiteral("cloudy");
    case 35: case 39: case 45: case 46:
        return QStringLiteral("few-showers");
    case 19: case 20: case 21: case 22:
        return QStringLiteral("foggy");
    case 29: case 30: case 33:
        return QStringLiteral("partly-cloudy");
    case 5: case 13: case 14: case 15: case 16: case 18: case 25: case 41: case 42: case 43:
        return QStringLiteral("snow");
    case 1: case 2: case 3: case 4: case 37: case 38: case 47:
        return QStringLiteral("thunderstorm");
    case 23: case 24:
        return QStringLiteral("windy");
    case 9: case 10: case 11: case 12: case 40:
        return QStringLiteral("rainy");
    case 6: case 7: case 8: case 17:
        return QStringLiteral("hail");
    }
    return QStringLiteral("sun");
}

/** weather.js backdrop(): BLUE for fine weather, GRAY for bad weather, BLACK at night */
QString backdrop(int sky, bool day)
{
    if (!day) {
        return QStringLiteral("black");
    }
    switch (sky) {
    case 26: case 27: case 28: case 35: case 39: case 45: case 46: case 19: case 20: case 21: case 22:
    case 1: case 2: case 3: case 4: case 5: case 37: case 38: case 47: case 9: case 10: case 11: case 12: case 40:
    case 41: case 42: case 43: case 6: case 7: case 8: case 17: case 13: case 14: case 15: case 16: case 18:
        return QStringLiteral("gray");
    }
    return QStringLiteral("blue");
}

/** the moon picture of tonight (the service told the gadget; we work it out from the date) */
QString moonState(const QDate &date)
{
    // days since a known new moon (6 January 2000), modulo the synodic month
    const double age = std::fmod(QDate(2000, 1, 6).daysTo(date) + 0.75, 29.530588853);
    static const char *names[] = {"new", "waxing-crescent", "first-quarter", "waxing-gibbous", "full", "waning-gibbous", "last-quarter", "waning-crescent"};
    return QString::fromLatin1(names[int(std::floor(age / 29.530588853 * 8 + 0.5)) % 8]);
}

QString weatherImageDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/seven-gadgets/weather/images/");
}

/** "undocked_blue_sun.png", "docked_black_moon-full_partly-cloudy.png", ... like GetBackgroundImageFileName() */
QPixmap weatherBackground(bool big, bool valid, int sky, bool day)
{
    const QString mode = big ? QStringLiteral("undocked") : QStringLiteral("docked");
    if (!valid) {
        return QPixmap(weatherImageDir() + mode + QStringLiteral("-loading.png"));
    }
    QString state = weatherState(sky);
    if (!day && (state == QLatin1String("sun") || state == QLatin1String("partly-cloudy"))) {
        state = QStringLiteral("moon-") + moonState(QDate::currentDate()) + (state == QLatin1String("partly-cloudy") ? QStringLiteral("_partly-cloudy") : QString());
    }
    for (const QString &bd : {backdrop(sky, day), QStringLiteral("gray"), QStringLiteral("blue"), QStringLiteral("black")}) {
        const QPixmap pm(weatherImageDir() + QStringLiteral("%1_%2_%3.png").arg(mode, bd, state));
        if (!pm.isNull()) {
            return pm;
        }
    }
    return QPixmap(weatherImageDir() + mode + QStringLiteral("-loading.png"));
}

class WeatherGadget : public Gadget
{
public:
    explicit WeatherGadget(const QString &gadgetId)
        : Gadget(gadgetId)
    {
        m_place.name = store().value(id + QStringLiteral("/Place"), tr7("Kyiv, Kyiv City")).toString();
        m_place.lat = store().value(id + QStringLiteral("/Lat"), 50.45466).toDouble();
        m_place.lon = store().value(id + QStringLiteral("/Lon"), 30.5238).toDouble();
        m_celsius = store().value(id + QStringLiteral("/Celsius"), true).toBool();
        m_timer.setInterval(20 * 60 * 1000); // like Windows 7, every ~20 minutes
        connect(&m_timer, &QTimer::timeout, this, [this] {
            refresh();
        });
        m_timer.start();
        refresh();
    }

protected:
    QSize bodySize() const override
    {
        return isBig() ? QSize(264, 194) : QSize(130, 67);
    }

    QString deg(double c) const
    {
        const double v = m_celsius ? c : c * 9 / 5 + 32;
        return QStringLiteral("%1°").arg(qRound(v));
    }

    bool glassFrame() const override
    {
        return false; // the Windows 7 pictures bring their own glass, edge and shadow
    }

    /** text of weather.html, placed and coloured like weather.css */
    void paintBody(QPainter &p, const QRectF &r) override
    {
        const bool day = m_weather.valid ? m_weather.day : true;
        const int sky = m_weather.valid ? msnSkyCode(m_weather.code, day) : 44;
        const QString bd = m_weather.valid ? backdrop(sky, day) : QStringLiteral("blue");
        p.drawPixmap(QPointF(r.left() - 0.5, r.top() - 0.5), weatherBackground(isBig(), m_weather.valid, sky, day));
        const QPointF o(r.left() - 0.5, r.top() - 0.5);
        const QColor main = bd == QLatin1String("black") ? QColor(0xf0, 0xf0, 0xf0) : Qt::black;
        auto font = [this](int px, bool light = false) {
            QFont f = this->font();
            f.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Noto Sans"), f.family()});
            f.setPixelSize(px);
            if (light) {
                f.setWeight(QFont::Light);
            }
            return f;
        };
        auto text = [&](const QRectF &rect, int flags, const QString &t, const QColor &c, const QFont &f) {
            p.setFont(f);
            p.setPen(c);
            p.drawText(rect.translated(o), flags, QFontMetrics(f).elidedText(t, Qt::ElideRight, int(rect.width())));
        };

        if (!m_weather.valid) {
            text(isBig() ? QRectF(15, 15, 234, 164) : QRectF(6, 4, 115, 57), Qt::AlignCenter | Qt::TextWordWrap,
                 m_error ? tr7("Service not available") : tr7("Retrieving data..."), QColor(0xf0, 0xf0, 0xf0), font(12));
            return;
        }
        if (!isBig()) {
            // docked: #TemperatureCurrent 24 px right edge 124, #PlaceDockedMode 12 px right edge 110
            text(QRectF(4, 1, 120, 32), Qt::AlignRight | Qt::AlignVCenter, deg(m_weather.temp), main, font(24));
            text(QRectF(6, 34, 104, 17), Qt::AlignRight | Qt::AlignVCenter, m_place.name, main, font(12));
            return;
        }
        // undocked: temperature, condition, today's high - low, place
        // (IE placed the 32 px line a little lower than Qt does: +6 keeps the digits inside the glass like there)
        text(QRectF(145, 9, 95, 42), Qt::AlignRight | Qt::AlignVCenter, deg(m_weather.temp), main, font(32));
        const QColor second = bd == QLatin1String("gray") ? QColor(0x5b, 0x60, 0x62) : main;
        text(QRectF(32, 47, 207, 16), Qt::AlignRight | Qt::AlignVCenter, conditionText(m_weather.code), second, font(12));
        if (!m_weather.days.isEmpty()) {
            const Day &t = m_weather.days.first();
            text(QRectF(124, 62, 116, 16), Qt::AlignRight | Qt::AlignVCenter, deg(t.max) + QStringLiteral(" - ") + deg(t.min), second, font(12));
        }
        QFont pf = font(14);
        pf.setUnderline(m_hoverPlace);
        m_placeRect = QRectF(18, 79, 210, 20).translated(o);
        text(QRectF(18, 79, 210, 20), Qt::AlignRight | Qt::AlignVCenter, m_place.name, main, pf);

        // forecast table: 3 x (28 px temperatures + 47 px picture) with 2 px dividers, 230 px wide at x 14
        const QColor dayColor = bd == QLatin1String("gray") ? QColor(0xb5, 0xba, 0xbe) : bd == QLatin1String("black") ? QColor(0xa6, 0xa6, 0xa6) : QColor(0x99, 0xcb, 0xff);
        const QColor highColor = bd == QLatin1String("gray") ? QColor(0xfb, 0xfb, 0xfb) : bd == QLatin1String("black") ? Qt::white : QColor(0xee, 0xee, 0xee);
        const QPixmap vdiv(weatherImageDir() + QStringLiteral("divider-vertical.png"));
        const QPixmap hdiv(weatherImageDir() + QStringLiteral("divider-horizontal.png"));
        const int n = qMin(3, int(m_weather.days.size()) - 1);
        const qreal top = 101;
        for (int i = 0; i < n; ++i) {
            const Day &d = m_weather.days.at(i + 1);
            const qreal x = 14 + i * 77;
            text(QRectF(x + 4, top, 71, 15), Qt::AlignLeft | Qt::AlignVCenter, dayName(d.date), dayColor, font(11));
            text(QRectF(x + 4, top + 19, 26, 16), Qt::AlignLeft | Qt::AlignVCenter, deg(d.max), highColor, font(12));
            text(QRectF(x + 4, top + 35, 26, 16), Qt::AlignLeft | Qt::AlignVCenter, deg(d.min), dayColor, font(12));
            const QPixmap icon(weatherImageDir() + QStringLiteral("%1.png").arg(msnSkyCode(d.code, true)));
            p.drawPixmap(o + QPointF(x + 28 + (47 - icon.width()) / 2.0, top + 17), icon);
            if (i > 0 && !vdiv.isNull()) {
                p.drawTiledPixmap(QRectF(o + QPointF(x - 2, top + 15), QSizeF(2, 38)), vdiv);
            }
        }
        if (!hdiv.isNull()) {
            p.drawTiledPixmap(QRectF(o + QPointF(14, top + 53), QSizeF(228, 2)), hdiv);
        }
        text(QRectF(18, top + 56, 200, 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("© Open-Meteo"), dayColor, font(12, true));
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const bool hp = isBig() && m_placeRect.contains(e->position());
        if (hp != m_hoverPlace) {
            m_hoverPlace = hp;
            update();
        }
        Gadget::mouseMoveEvent(e);
        if (hp) {
            setCursor(Qt::PointingHandCursor);
        }
    }

    void bodyClicked(QPointF pos) override
    {
        if (isBig() && m_placeRect.contains(pos)) {
            // Windows 7 opened the forecast site for the place
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://open-meteo.com/en/docs#latitude=%1&longitude=%2").arg(m_place.lat).arg(m_place.lon)));
        }
    }

    void configure() override
    {
        WeatherSettings dlg(m_place, m_celsius);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        const bool changed = dlg.place.lat != m_place.lat || dlg.place.lon != m_place.lon;
        m_place = dlg.place;
        m_celsius = dlg.celsius;
        store().setValue(id + QStringLiteral("/Place"), m_place.name);
        store().setValue(id + QStringLiteral("/Lat"), m_place.lat);
        store().setValue(id + QStringLiteral("/Lon"), m_place.lon);
        store().setValue(id + QStringLiteral("/Celsius"), m_celsius);
        store().sync();
        if (changed) {
            m_weather.valid = false;
            refresh();
        }
        update();
    }

private:
    void refresh()
    {
        QUrl url(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("latitude"), QString::number(m_place.lat, 'f', 4));
        q.addQueryItem(QStringLiteral("longitude"), QString::number(m_place.lon, 'f', 4));
        q.addQueryItem(QStringLiteral("current"), QStringLiteral("temperature_2m,weather_code,is_day"));
        q.addQueryItem(QStringLiteral("daily"), QStringLiteral("weather_code,temperature_2m_max,temperature_2m_min"));
        q.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
        q.addQueryItem(QStringLiteral("forecast_days"), QStringLiteral("4"));
        url.setQuery(q);
        QNetworkReply *reply = net().get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                m_error = !m_weather.valid;
                QTimer::singleShot(60 * 1000, this, [this] {
                    refresh();
                });
                update();
                return;
            }
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            const QJsonObject cur = o.value(QStringLiteral("current")).toObject();
            const QJsonObject daily = o.value(QStringLiteral("daily")).toObject();
            Weather w;
            w.valid = cur.contains(QStringLiteral("temperature_2m"));
            w.temp = cur.value(QStringLiteral("temperature_2m")).toDouble();
            w.code = cur.value(QStringLiteral("weather_code")).toInt();
            w.day = cur.value(QStringLiteral("is_day")).toInt(1) == 1;
            const QJsonArray dates = daily.value(QStringLiteral("time")).toArray();
            for (int i = 0; i < dates.size(); ++i) {
                w.days.append({QDate::fromString(dates.at(i).toString(), Qt::ISODate), daily.value(QStringLiteral("weather_code")).toArray().at(i).toInt(),
                               daily.value(QStringLiteral("temperature_2m_max")).toArray().at(i).toDouble(),
                               daily.value(QStringLiteral("temperature_2m_min")).toArray().at(i).toDouble()});
            }
            if (w.valid) {
                m_weather = w;
                m_error = false;
            }
            update();
        });
    }

    Place m_place;
    bool m_celsius = true;
    Weather m_weather;
    bool m_error = false;
    QTimer m_timer;
    QRectF m_placeRect;
    bool m_hoverPlace = false;
};

/* ---------------- gallery ---------------- */

/* ---------------- Calendar gadget ---------------- */

QString capitalized(QString s)
{
    if (!s.isEmpty()) {
        s[0] = s.at(0).toUpper();
    }
    return s;
}

QString shortMonth(int m)
{
    QString n = QLocale{}.standaloneMonthName(m, QLocale::ShortFormat);
    n.remove(QLatin1Char('.'));
    return capitalized(n);
}

class CalendarGadget : public Gadget
{
public:
    explicit CalendarGadget(const QString &gadgetId)
        : Gadget(gadgetId)
        , m_today(QDate::currentDate())
        , m_selected(m_today)
        , m_shown(m_today.year(), m_today.month(), 1)
    {
        m_monthPage = store().value(id + QStringLiteral("/Month"), false).toBool();
        // roll over at midnight
        m_clock.setInterval(30 * 1000);
        connect(&m_clock, &QTimer::timeout, this, [this] {
            const QDate now = QDate::currentDate();
            if (now != m_today) {
                if (m_selected == m_today) {
                    m_selected = now;
                    m_shown = QDate(now.year(), now.month(), 1);
                }
                m_today = now;
                update();
            }
        });
        m_clock.start();
    }

protected:
    static constexpr int kW = 130;
    static constexpr int kSheetH = 138; // the orange day sheet
    static constexpr int kPageH = 128;  // the white month page in the larger size

    QSize bodySize() const override
    {
        return isBig() ? QSize(kW, kPageH + 6 + kSheetH) : QSize(kW, 6 + kSheetH);
    }
    bool glassFrame() const override
    {
        return false;
    }
    bool hasSettingsButton() const override
    {
        return false;
    }
    void configure() override
    {
    }

    QRectF pageRect() const
    {
        return isBig() ? QRectF(0.5, 0.5, kW - 1, kPageH) : QRectF(0.5, 6.5, kW - 1, kSheetH - 1);
    }
    QRectF sheetRect() const
    {
        return isBig() ? QRectF(0.5, kPageH + 6.5, kW - 1, kSheetH - 1) : QRectF(0.5, 6.5, kW - 1, kSheetH - 1);
    }
    bool showsPage() const
    {
        return isBig() || m_monthPage;
    }
    // the white page in the smaller size starts below the rings
    qreal pageTop() const
    {
        return pageRect().top() + (isBig() ? 0 : 8);
    }

    void paintBody(QPainter &p, const QRectF &) override
    {
        if (showsPage()) {
            paintPage(p, pageRect());
        }
        if (!showsPage() || isBig()) {
            paintSheet(p, sheetRect());
        }
        if (isBig()) {
            paintRings(p, kPageH - 5, true, true);
        } else {
            paintRings(p, 0, false, true);
        }
    }

    /** the spiral binding: silver loops over dark punched holes */
    void paintRings(QPainter &p, qreal top, bool holeAbove, bool holeBelow)
    {
        p.save();
        const int n = 11;
        const qreal step = (kW - 18) / qreal(n - 1);
        for (int i = 0; i < n; ++i) {
            const qreal x = 9 + i * step;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(40, 20, 10, 200));
            if (holeAbove) {
                p.drawEllipse(QPointF(x + 0.5, top + 2.5), 2, 1.6);
            }
            if (holeBelow) {
                p.drawEllipse(QPointF(x + 0.5, top + (holeAbove ? 15.5 : 11)), 2, 1.6);
            }
            const QRectF loop(x - 1.5, top + 1, 3.6, holeAbove ? 15 : 10.5);
            QLinearGradient g(loop.topLeft(), loop.topRight());
            g.setColorAt(0, QColor(150, 150, 150));
            g.setColorAt(0.45, QColor(255, 255, 255));
            g.setColorAt(1, QColor(120, 120, 120));
            p.setBrush(g);
            p.setPen(QPen(QColor(60, 60, 60, 200), 0.7));
            p.drawRoundedRect(loop, 1.6, 1.6);
        }
        p.restore();
    }

    void paintSheet(QPainter &p, const QRectF &r)
    {
        p.save();
        QLinearGradient g(r.topLeft(), r.bottomRight());
        g.setColorAt(0, QColor(248, 112, 22));
        g.setColorAt(0.55, QColor(232, 88, 8));
        g.setColorAt(1, QColor(204, 70, 0));
        p.setBrush(g);
        p.setPen(QPen(QColor(150, 50, 0), 1));
        p.drawRoundedRect(r, 3, 3);
        // faint light edge along the top like the printed paper
        p.setPen(QPen(QColor(255, 170, 110, 120), 1));
        p.drawLine(r.topLeft() + QPointF(3, 1), r.topRight() + QPointF(-3, 1));

        const QLocale uk;
        auto text = [&](const QRectF &rect, const QFont &f, const QString &t) {
            p.setFont(f);
            p.setPen(QColor(120, 35, 0, 110));
            p.drawText(rect.translated(0, 1), Qt::AlignCenter, t);
            p.setPen(Qt::white);
            p.drawText(rect, Qt::AlignCenter, t);
        };
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(15);
        text(QRectF(r.left(), r.top() + 13, r.width(), 20), f, capitalized(uk.standaloneDayName(m_selected.dayOfWeek(), QLocale::LongFormat)));
        QFont big = font();
        big.setPixelSize(80);
        big.setWeight(QFont::Normal);
        text(QRectF(r.left(), r.top() + 30, r.width(), 80), big, QString::number(m_selected.day()));
        f.setPixelSize(14);
        text(QRectF(r.left(), r.bottom() - 28, r.width(), 20), f,
             QStringLiteral("%1 %2").arg(capitalized(uk.standaloneMonthName(m_selected.month(), QLocale::LongFormat))).arg(m_selected.year()));
        p.restore();
    }

    // ---- month / year page geometry (shared by painting and clicks)
    QRectF headerRect() const
    {
        return QRectF(24, pageTop() + 5, kW - 48, 17);
    }
    QRectF prevRect() const
    {
        return QRectF(4, pageTop() + 5, 18, 17);
    }
    QRectF nextRect() const
    {
        return QRectF(kW - 22, pageTop() + 5, 18, 17);
    }
    qreal colWidth() const
    {
        return (kW - 8) / 7.0;
    }
    QRectF dayCell(int row, int col) const
    {
        return QRectF(4 + col * colWidth(), pageTop() + 39 + row * 14, colWidth(), 14);
    }
    QDate gridStart() const
    {
        return m_shown.addDays(1 - m_shown.dayOfWeek()); // weeks start on Monday
    }
    int gridRows() const
    {
        const int days = gridStart().daysTo(m_shown.addMonths(1)) ;
        return (days + 6) / 7;
    }
    QRectF monthCell(int i) const
    {
        const qreal w = (kW - 8) / 4.0;
        return QRectF(4 + (i % 4) * w, pageTop() + 27 + (i / 4) * 31, w, 31);
    }

    void paintArrow(QPainter &p, const QRectF &r, bool left)
    {
        const QPointF c = r.center();
        QPolygonF tri;
        if (left) {
            tri << c + QPointF(-3, 0) << c + QPointF(2, -4.5) << c + QPointF(2, 4.5);
        } else {
            tri << c + QPointF(3, 0) << c + QPointF(-2, -4.5) << c + QPointF(-2, 4.5);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(140, 140, 140));
        p.drawPolygon(tri);
    }

    void paintPage(QPainter &p, const QRectF &r)
    {
        p.save();
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0, QColor(255, 255, 255));
        g.setColorAt(0.6, QColor(246, 246, 246));
        g.setColorAt(1, QColor(222, 222, 222));
        p.setBrush(g);
        p.setPen(QPen(QColor(120, 120, 120), 1));
        p.drawRoundedRect(r, 4, 4);
        p.setPen(QPen(QColor(150, 215, 230, 150), 1)); // the glassy edge on the right
        p.drawLine(r.topRight() + QPointF(-1, 4), r.bottomRight() + QPointF(-1, -4));

        paintArrow(p, prevRect(), true);
        paintArrow(p, nextRect(), false);
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(QColor(40, 40, 40));
        if (m_yearView) {
            p.drawText(headerRect(), Qt::AlignCenter, QString::number(m_shown.year()));
            QFont mf = font();
            mf.setPixelSize(11);
            mf.setBold(true);
            p.setFont(mf);
            for (int i = 0; i < 12; ++i) {
                const QRectF c = monthCell(i);
                const bool current = m_shown.year() == m_today.year() && i + 1 == m_today.month();
                // tiny dotted month sheet
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 6; ++x) {
                        p.fillRect(QRectF(c.center().x() - 8 + x * 3, c.top() + 3 + y * 3, 1.6, 1.6), current ? QColor(232, 88, 8) : QColor(120, 120, 120));
                    }
                }
                p.setPen(current ? QColor(220, 80, 0) : QColor(40, 40, 40));
                p.drawText(QRectF(c.left(), c.top() + 15, c.width(), 14), Qt::AlignCenter, shortMonth(i + 1));
            }
            p.restore();
            return;
        }
        p.drawText(headerRect(), Qt::AlignCenter, QStringLiteral("%1 %2").arg(shortMonth(m_shown.month()), m_shown.toString(QStringLiteral("yy"))));

        // weekday letters, today's column in orange
        static QStringList letters; // M T W T F S S - the day letters of the user's language
        if (letters.isEmpty()) {
            for (int d = 1; d <= 7; ++d) {
                letters << QLocale().standaloneDayName(d, QLocale::NarrowFormat).toUpper();
            }
        }
        QFont wf = font();
        wf.setPixelSize(11);
        wf.setBold(true);
        p.setFont(wf);
        const bool thisMonth = m_shown.year() == m_today.year() && m_shown.month() == m_today.month();
        for (int c = 0; c < 7; ++c) {
            const QRectF cell(4 + c * colWidth(), pageTop() + 24, colWidth(), 14);
            p.setPen(thisMonth && c + 1 == m_today.dayOfWeek() ? QColor(232, 88, 8) : QColor(110, 110, 110));
            p.drawText(cell, Qt::AlignCenter, letters.at(c));
            if (c > 0) { // faint column rules
                p.setPen(QPen(QColor(0, 0, 0, 18), 1));
                p.drawLine(QPointF(cell.left(), cell.top() + 2), QPointF(cell.left(), pageTop() + 39 + 6 * 14 - 2));
            }
        }
        QFont df = font();
        df.setPixelSize(12);
        const QDate start = gridStart();
        const int rows = gridRows();
        for (int row = 0; row < rows; ++row) {
            for (int c = 0; c < 7; ++c) {
                const QDate d = start.addDays(row * 7 + c);
                const QRectF cell = dayCell(row, c);
                df.setBold(false);
                QColor ink = d.month() == m_shown.month() ? QColor(20, 20, 20) : QColor(185, 185, 185);
                if (d == m_today) {
                    p.fillRect(cell.adjusted(1, 0, -1, 0), QColor(240, 100, 16));
                    ink = Qt::white;
                    df.setBold(true);
                } else if (d == m_selected) {
                    p.setPen(QPen(QColor(240, 100, 16), 1));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(cell.adjusted(1.5, 0.5, -1.5, -0.5));
                }
                p.setFont(df);
                p.setPen(ink);
                p.drawText(cell, Qt::AlignCenter, QString::number(d.day()));
            }
        }
        p.restore();
    }

    void bodyClicked(QPointF pos) override
    {
        if (!showsPage() || !pageRect().contains(pos)) {
            return;
        }
        if (prevRect().contains(pos) || nextRect().contains(pos)) {
            const int dir = prevRect().contains(pos) ? -1 : 1;
            m_shown = m_yearView ? m_shown.addYears(dir) : m_shown.addMonths(dir);
        } else if (headerRect().contains(pos)) {
            m_yearView = true;
        } else if (m_yearView) {
            for (int i = 0; i < 12; ++i) {
                if (monthCell(i).contains(pos)) {
                    m_shown = QDate(m_shown.year(), i + 1, 1);
                    m_yearView = false;
                }
            }
        } else {
            for (int row = 0; row < gridRows(); ++row) {
                for (int c = 0; c < 7; ++c) {
                    if (dayCell(row, c).contains(pos)) {
                        m_selected = gridStart().addDays(row * 7 + c);
                        if (m_selected.month() != m_shown.month()) {
                            m_shown = QDate(m_selected.year(), m_selected.month(), 1);
                        }
                    }
                }
            }
        }
        update();
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton || !bodyRect().contains(e->position())) {
            return;
        }
        if (isBig()) {
            if (sheetRect().contains(e->position())) { // back to today
                m_selected = m_today;
                m_shown = QDate(m_today.year(), m_today.month(), 1);
                m_yearView = false;
                update();
            }
            return;
        }
        if (m_monthPage && (m_yearView || prevRect().contains(e->position()) || nextRect().contains(e->position()) || headerRect().contains(e->position()))) {
            return;
        }
        // the smaller size flips between the day and the month
        m_monthPage = !m_monthPage;
        m_yearView = false;
        m_shown = QDate(m_selected.year(), m_selected.month(), 1);
        store().setValue(id + QStringLiteral("/Month"), m_monthPage);
        store().sync();
        update();
    }

private:
    QDate m_today;
    QDate m_selected;
    QDate m_shown; // first day of the month on the page
    bool m_monthPage = false;
    bool m_yearView = false;
    QTimer m_clock;
};

/* ---------------- Clock gadget ---------------- */

constexpr int kClockThemes = 8;

/** One of the eight Windows 7 clock faces, drawn into the square r */
void paintClock(QPainter &p, const QRectF &r, int theme, const QTime &t, bool seconds, const QString &name)
{
    p.save();
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    const QPointF c = r.center();
    const qreal R = qMin(r.width(), r.height()) / 2 - 2;
    p.translate(c);
    p.scale(R / 62.0, R / 62.0); // everything below is drawn on a 124px clock

    QColor hourInk = QColor(20, 20, 20), minuteInk = hourInk, secondInk = QColor(200, 30, 30), tickInk = QColor(30, 30, 30), textInk = QColor(30, 30, 30);
    qreal hourW = 3.4, minuteW = 2.2, hourL = 30, minuteL = 46;
    bool numbers = false, ticks = true, dots = true;
    QFont nf;
    nf.setPixelSize(12);

    auto ring = [&](qreal rad, const QColor &a, const QColor &b) {
        QLinearGradient g(QPointF(0, -rad), QPointF(0, rad));
        g.setColorAt(0, a);
        g.setColorAt(1, b);
        p.setBrush(g);
        p.setPen(QPen(QColor(0, 0, 0, 110), 1));
        p.drawEllipse(QPointF(0, 0), rad, rad);
    };
    auto face = [&](qreal rad, const QColor &centre, const QColor &edge) {
        QRadialGradient g(QPointF(0, -rad * 0.35), rad * 1.3);
        g.setColorAt(0, centre);
        g.setColorAt(1, edge);
        p.setBrush(g);
        p.setPen(QPen(QColor(0, 0, 0, 80), 0.8));
        p.drawEllipse(QPointF(0, 0), rad, rad);
    };

    qreal tickOuter = 50;
    switch (theme) {
    case 0: // traditional: cream face, black ring, numbers
        ring(62, QColor(70, 70, 70), QColor(10, 10, 10));
        face(57, QColor(255, 252, 235), QColor(235, 226, 196));
        numbers = true;
        ticks = false;
        nf.setPixelSize(13);
        tickOuter = 54;
        break;
    case 1: // the silver system clock (default)
        ring(62, QColor(250, 250, 250), QColor(130, 130, 130));
        ring(57, QColor(150, 150, 150), QColor(245, 245, 245));
        face(54, QColor(250, 252, 250), QColor(200, 204, 204));
        break;
    case 2: // chronometer: black face
        ring(62, QColor(235, 235, 235), QColor(110, 110, 110));
        face(55, QColor(70, 70, 75), QColor(15, 15, 18));
        hourInk = minuteInk = tickInk = textInk = QColor(240, 240, 240);
        secondInk = QColor(255, 140, 0);
        numbers = true;
        nf.setBold(true);
        nf.setPixelSize(11);
        break;
    case 3: // diner: red ring, white face, bold numbers
        ring(62, QColor(240, 70, 60), QColor(150, 10, 10));
        face(53, QColor(255, 255, 255), QColor(230, 230, 230));
        numbers = true;
        ticks = false;
        nf.setBold(true);
        nf.setPixelSize(14);
        secondInk = QColor(210, 20, 20);
        hourW = 4.5;
        minuteW = 3;
        tickOuter = 50;
        break;
    case 4: { // flower
        p.setPen(QPen(QColor(170, 40, 110, 150), 1));
        for (int i = 0; i < 12; ++i) {
            p.save();
            p.rotate(i * 30);
            QRadialGradient g(QPointF(0, -46), 18);
            g.setColorAt(0, QColor(255, 190, 225));
            g.setColorAt(1, QColor(235, 90, 160));
            p.setBrush(g);
            p.drawEllipse(QPointF(0, -46), 15, 15);
            p.restore();
        }
        face(46, QColor(255, 255, 255), QColor(250, 225, 238));
        hourInk = minuteInk = QColor(150, 20, 90);
        tickInk = QColor(210, 70, 140);
        secondInk = QColor(240, 120, 0);
        dots = false;
        tickOuter = 42;
        hourL = 24;
        minuteL = 36;
        break;
    }
    case 5: // modern: plain white, thin ring, four marks
        face(60, QColor(255, 255, 255), QColor(240, 240, 240));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(60, 60, 60), 1.5));
        p.drawEllipse(QPointF(0, 0), 60, 60);
        ticks = false;
        dots = false;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 40, 40));
        for (int i = 0; i < 4; ++i) {
            p.save();
            p.rotate(i * 90);
            p.drawEllipse(QPointF(0, -50), 3, 3);
            p.restore();
        }
        hourW = 2.6;
        minuteW = 1.6;
        break;
    case 6: { // square
        QLinearGradient g(QPointF(0, -60), QPointF(0, 60));
        g.setColorAt(0, QColor(110, 110, 115));
        g.setColorAt(1, QColor(35, 35, 40));
        p.setBrush(g);
        p.setPen(QPen(QColor(0, 0, 0, 150), 1));
        p.drawRoundedRect(QRectF(-60, -60, 120, 120), 14, 14);
        QLinearGradient f(QPointF(0, -52), QPointF(0, 52));
        f.setColorAt(0, QColor(60, 60, 64));
        f.setColorAt(1, QColor(20, 20, 24));
        p.setBrush(f);
        p.drawRoundedRect(QRectF(-52, -52, 104, 104), 9, 9);
        hourInk = minuteInk = tickInk = textInk = QColor(235, 235, 235);
        secondInk = QColor(120, 200, 255);
        dots = false;
        tickOuter = 46;
        break;
    }
    default: // novelty: light blue, colourful numbers
        ring(62, QColor(120, 190, 250), QColor(30, 90, 190));
        face(55, QColor(245, 252, 255), QColor(195, 228, 250));
        numbers = true;
        ticks = false;
        nf.setBold(true);
        nf.setPixelSize(14);
        hourInk = minuteInk = QColor(20, 60, 140);
        secondInk = QColor(250, 150, 0);
        hourW = 4.5;
        minuteW = 3;
        tickOuter = 52;
        break;
    }

    // marks
    for (int i = 0; i < 60; ++i) {
        p.save();
        p.rotate(i * 6);
        if (i % 5 == 0) {
            if (ticks) {
                p.setPen(QPen(tickInk, theme == 6 ? 3 : 2.4, Qt::SolidLine, Qt::FlatCap));
                p.drawLine(QPointF(0, -tickOuter), QPointF(0, -tickOuter + 8));
            }
        } else if (dots) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme == 2 ? QColor(200, 200, 200) : QColor(120, 120, 120));
            p.drawEllipse(QPointF(0, -tickOuter + 1.5), 0.7, 0.7);
        }
        p.restore();
    }
    if (numbers) {
        static const QColor fun[] = {QColor(220, 40, 40), QColor(240, 140, 0), QColor(40, 160, 40), QColor(30, 110, 220)};
        p.setFont(nf);
        for (int h = 1; h <= 12; ++h) {
            if (theme == 2 && h % 3 != 0) {
                continue;
            }
            const qreal a = h * M_PI / 6;
            const qreal rad = tickOuter - (theme == 2 ? 17 : 9);
            const QPointF pt(std::sin(a) * rad, -std::cos(a) * rad);
            p.setPen(theme == 7 ? fun[h % 4] : textInk);
            p.drawText(QRectF(pt.x() - 12, pt.y() - 9, 24, 18), Qt::AlignCenter, QString::number(h));
        }
    }
    if (!name.isEmpty()) {
        QFont f;
        f.setPixelSize(9);
        f.setBold(true);
        f.setCapitalization(QFont::AllUppercase);
        p.setFont(f);
        p.setPen(theme == 2 || theme == 6 ? QColor(200, 200, 200) : QColor(110, 110, 110));
        p.drawText(QRectF(-40, 14, 80, 14), Qt::AlignCenter, QFontMetrics(f).elidedText(name.toUpper(), Qt::ElideRight, 78));
    }

    auto hand = [&](qreal angle, qreal len, qreal w, const QColor &col, qreal tail) {
        p.save();
        p.rotate(angle);
        p.setPen(QPen(QColor(0, 0, 0, 50), w, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(1.2, tail + 1.2), QPointF(1.2, -len + 1.2)); // soft shadow
        p.setPen(QPen(col, w, Qt::SolidLine, theme == 1 || theme == 5 ? Qt::FlatCap : Qt::RoundCap));
        p.drawLine(QPointF(0, tail), QPointF(0, -len));
        p.restore();
    };
    hand((t.hour() % 12 + t.minute() / 60.0) * 30, hourL, hourW, hourInk, 6);
    hand((t.minute() + t.second() / 60.0) * 6, minuteL, minuteW, minuteInk, 8);
    if (seconds) {
        hand(t.second() * 6, minuteL + 2, 1, secondInk, 12);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(seconds ? secondInk : hourInk);
    p.drawEllipse(QPointF(0, 0), 2.6, 2.6);
    // glass reflection over the round faces
    if (theme != 6) {
        QLinearGradient gl(QPointF(0, -58), QPointF(0, 0));
        gl.setColorAt(0, QColor(255, 255, 255, 70));
        gl.setColorAt(1, QColor(255, 255, 255, 0));
        p.setBrush(gl);
        p.drawEllipse(QRectF(-44, -56, 88, 52));
    }
    p.restore();
}

QString zoneLabel(const QByteArray &zid)
{
    const QTimeZone z(zid);
    const int off = z.standardTimeOffset(QDateTime::currentDateTimeUtc()) / 60;
    const QString sign = off < 0 ? QStringLiteral("-") : QStringLiteral("+");
    QString city = QString::fromLatin1(zid).section(QLatin1Char('/'), -1).replace(QLatin1Char('_'), QLatin1Char(' '));
    return QStringLiteral("(UTC%1%2:%3) %4, %5")
        .arg(sign)
        .arg(qAbs(off) / 60, 2, 10, QLatin1Char('0'))
        .arg(qAbs(off) % 60, 2, 10, QLatin1Char('0'))
        .arg(city, z.displayName(QTimeZone::GenericTime, QTimeZone::LongName, QLocale{}));
}

QTime zoneTime(const QByteArray &zone)
{
    if (zone.isEmpty()) {
        return QTime::currentTime();
    }
    return QDateTime::currentDateTime().toTimeZone(QTimeZone(zone)).time();
}

class ClockPreview : public QWidget
{
public:
    int theme = 1;
    bool seconds = false;
    QString name;
    QByteArray zone;

    ClockPreview()
    {
        setFixedHeight(150);
        auto *t = new QTimer(this);
        connect(t, &QTimer::timeout, this, qOverload<>(&QWidget::update));
        t->start(1000);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setPen(QColor(200, 200, 200));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        paintClock(p, QRectF(width() / 2 - 65, 10, 130, 130), theme, zoneTime(zone), seconds, name);
    }
};

class ClockSettings : public QDialog
{
public:
    ClockSettings(int theme, const QString &name, const QByteArray &zone, bool seconds, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr7("Clock"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("gadgets")));
        setFixedWidth(300);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 10);
        v->setSpacing(0);
        auto *header = new QLabel(tr7("Clock"));
        header->setFixedHeight(44);
        header->setContentsMargins(16, 0, 0, 0);
        QFont hf = header->font();
        hf.setPixelSize(17);
        header->setFont(hf);
        header->setStyleSheet(QStringLiteral("QLabel { color: white; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3a4350, stop:1 #12171e); }"));
        v->addWidget(header);

        auto *body = new QWidget;
        auto *bv = new QVBoxLayout(body);
        bv->setContentsMargins(14, 12, 14, 8);
        bv->setSpacing(4);
        m_preview = new ClockPreview;
        m_preview->theme = theme;
        m_preview->name = name;
        m_preview->zone = zone;
        m_preview->seconds = seconds;
        bv->addWidget(m_preview);
        auto *nav = new QHBoxLayout;
        nav->addStretch(1);
        auto *prev = new QToolButton;
        auto *next = new QToolButton;
        prev->setArrowType(Qt::LeftArrow);
        next->setArrowType(Qt::RightArrow);
        prev->setToolTip(tr7("Previous", "clock"));
        next->setToolTip(tr7("Next"));
        m_count = new QLabel;
        nav->addWidget(prev);
        nav->addSpacing(8);
        nav->addWidget(m_count);
        nav->addSpacing(8);
        nav->addWidget(next);
        nav->addStretch(1);
        bv->addLayout(nav);
        bv->addSpacing(10);
        bv->addWidget(new QLabel(tr7("Clock name:")));
        m_name = new QLineEdit(name);
        m_name->setMaxLength(24);
        bv->addWidget(m_name);
        bv->addSpacing(8);
        bv->addWidget(new QLabel(tr7("Time zone:")));
        m_zone = new QComboBox;
        m_zone->addItem(tr7("Current computer time"), QByteArray());
        QList<QPair<int, QByteArray>> zones;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (const QByteArray &zid : QTimeZone::availableTimeZoneIds()) {
            if (!zid.contains('/') || zid.startsWith("Etc/") || zid.startsWith("SystemV/") || zid.startsWith("US/") || zid.startsWith("posix/") || zid.startsWith("right/")) {
                continue;
            }
            zones.append({QTimeZone(zid).standardTimeOffset(now), zid});
        }
        std::sort(zones.begin(), zones.end());
        for (const auto &z : std::as_const(zones)) {
            m_zone->addItem(zoneLabel(z.second), z.second);
        }
        m_zone->setCurrentIndex(qMax(0, m_zone->findData(zone)));
        m_zone->setMaxVisibleItems(16);
        m_zone->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_zone->setMinimumContentsLength(20);
        bv->addWidget(m_zone);
        bv->addSpacing(8);
        m_seconds = new QCheckBox(tr7("Show the second hand"));
        m_seconds->setChecked(seconds);
        bv->addWidget(m_seconds);
        bv->addStretch(1);
        v->addWidget(body, 1);

        auto *line = new QFrame;
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
        v->addWidget(line);
        v->addSpacing(10);
        auto *btns = new QHBoxLayout;
        btns->setContentsMargins(14, 0, 14, 0);
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        ok->setMinimumWidth(86);
        cancel->setMinimumWidth(86);
        ok->setDefault(true);
        btns->addWidget(ok);
        btns->addWidget(cancel);
        v->addLayout(btns);
        updateCount();

        connect(prev, &QToolButton::clicked, this, [this] {
            m_preview->theme = (m_preview->theme + kClockThemes - 1) % kClockThemes;
            updateCount();
        });
        connect(next, &QToolButton::clicked, this, [this] {
            m_preview->theme = (m_preview->theme + 1) % kClockThemes;
            updateCount();
        });
        connect(m_name, &QLineEdit::textChanged, this, [this](const QString &t) {
            m_preview->name = t;
            m_preview->update();
        });
        connect(m_zone, &QComboBox::currentIndexChanged, this, [this] {
            m_preview->zone = m_zone->currentData().toByteArray();
            m_preview->update();
        });
        connect(m_seconds, &QCheckBox::toggled, this, [this](bool on) {
            m_preview->seconds = on;
            m_preview->update();
        });
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    int theme() const
    {
        return m_preview->theme;
    }
    QString name() const
    {
        return m_name->text().trimmed();
    }
    QByteArray zone() const
    {
        return m_zone->currentData().toByteArray();
    }
    bool seconds() const
    {
        return m_seconds->isChecked();
    }

private:
    void updateCount()
    {
        m_count->setText(tr7("%1 of %2").arg(m_preview->theme + 1).arg(kClockThemes));
        m_preview->update();
    }

    ClockPreview *m_preview;
    QLabel *m_count;
    QLineEdit *m_name;
    QComboBox *m_zone;
    QCheckBox *m_seconds;
};

class ClockGadget : public Gadget
{
public:
    explicit ClockGadget(const QString &gadgetId)
        : Gadget(gadgetId)
    {
        m_theme = store().value(id + QStringLiteral("/Theme"), 1).toInt();
        m_name = store().value(id + QStringLiteral("/Name")).toString();
        m_zone = store().value(id + QStringLiteral("/Zone")).toByteArray();
        m_seconds = store().value(id + QStringLiteral("/Seconds"), false).toBool();
        connect(&m_tick, &QTimer::timeout, this, [this] {
            const QTime t = zoneTime(m_zone);
            setToolTip(t.toString(QStringLiteral("HH:mm:ss")));
            if (m_seconds || t.minute() != m_lastMinute) {
                m_lastMinute = t.minute();
                update();
            }
        });
        m_tick.start(1000);
        setToolTip(zoneTime(m_zone).toString(QStringLiteral("HH:mm:ss")));
    }

protected:
    QSize bodySize() const override
    {
        return QSize(130, 130);
    }
    bool hasSizeButton() const override
    {
        return false;
    }
    bool glassFrame() const override
    {
        return false;
    }
    void paintBody(QPainter &p, const QRectF &r) override
    {
        paintClock(p, r, m_theme, zoneTime(m_zone), m_seconds, m_name);
    }
    void configure() override
    {
        ClockSettings dlg(m_theme, m_name, m_zone, m_seconds);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        m_theme = dlg.theme();
        m_name = dlg.name();
        m_zone = dlg.zone();
        m_seconds = dlg.seconds();
        store().setValue(id + QStringLiteral("/Theme"), m_theme);
        store().setValue(id + QStringLiteral("/Name"), m_name);
        store().setValue(id + QStringLiteral("/Zone"), m_zone);
        store().setValue(id + QStringLiteral("/Seconds"), m_seconds);
        store().sync();
        update();
    }

private:
    int m_theme = 1;
    QString m_name;
    QByteArray m_zone;
    bool m_seconds = false;
    int m_lastMinute = -1;
    QTimer m_tick;
};

#include "gadget-dialog.h"
#include "gadget-cpu.h"
#include "gadget-slideshow.h"
#include "gadget-puzzle.h"
#include "gadget-currency.h"
#include "gadget-feeds.h"

struct GadgetInfo {
    QString type;
    QString name;
    QString description;
};

/** The Windows 7 set, minus Windows Media Center */
QList<GadgetInfo> catalog()
{
    QList<GadgetInfo> list{
        {QStringLiteral("calendar"), tr7("Calendar"), tr7("Browse the calendar to see the date.")},
        {QStringLiteral("clock"), tr7("Clock"), tr7("Shows the time. You can choose one of eight clocks, name it and set its time zone.")},
        {QStringLiteral("cpu"), tr7("CPU Meter"), tr7("Shows the current usage of the central processor and system memory (RAM).")},
        {QStringLiteral("currency"), tr7("Currency"), tr7("Converts amounts from one currency to another at rates from the Internet.")},
        {QStringLiteral("feeds"), tr7("Feed Headlines"), tr7("Shows headlines from frequently updated feeds (RSS) on the desktop.")},
        {QStringLiteral("puzzle"), tr7("Picture Puzzle"), tr7("Move the pieces to put the picture together.")},
        {QStringLiteral("slideshow"), tr7("Slide Show"), tr7("Shows a continuous slide show of your pictures.")},
        {QStringLiteral("weather"), tr7("Weather"), tr7("Shows the current weather conditions for the locations you choose.")},
    };
    return list;
}

class GalleryTile : public QWidget
{
public:
    GadgetInfo info;
    std::function<void(GalleryTile *)> clicked, activated;
    std::function<void(GalleryTile *, QPoint)> dropped; // released outside the gallery: global position of the gadget's corner
    bool selected = false;

    explicit GalleryTile(const GadgetInfo &i, QWidget *parent)
        : QWidget(parent)
        , info(i)
    {
        setFixedSize(104, 110);
        setToolTip(info.description);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
        const bool hot = underMouse();
        if (selected || hot) {
            // tile.png: hover, selected, selected + hover
            paintNineSlice(p, QRectF(rect()), stripFrame(gadgetPixmap(QStringLiteral("gallery/tile.png")), 3, selected ? (hot ? 2 : 1) : 0), 4);
        }
        const QPixmap &icon = gadgetPixmap(QStringLiteral("gallery/%1.png").arg(info.type));
        p.drawPixmap(QPointF((width() - icon.width()) / 2.0, 8 + (64 - icon.height()) / 2.0), icon);
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        // two lines at most, like "Windows Media..." in the real gallery
        const QRect tr(2, 78, width() - 4, 32);
        const QFontMetrics fm(f);
        QString text = info.name;
        if (fm.horizontalAdvance(text) > tr.width()) {
            const int cut = text.lastIndexOf(QLatin1Char(' '), int(text.size()) / 2 + 3);
            text = cut > 0 ? text.left(cut) + QLatin1Char('\n') + fm.elidedText(text.mid(cut + 1), Qt::ElideRight, tr.width()) : fm.elidedText(text, Qt::ElideRight, tr.width());
        }
        drawGlowText(p, tr, Qt::AlignHCenter | Qt::AlignTop, text);
    }
    void enterEvent(QEnterEvent *) override
    {
        update();
    }
    void leaveEvent(QEvent *) override
    {
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_pressPos = e->globalPosition().toPoint();
        }
        if (clicked) {
            clicked(this);
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!(e->buttons() & Qt::LeftButton)) {
            return;
        }
        // drag the gadget's picture out to the desktop, like in Windows 7
        if (!m_ghost && (e->globalPosition().toPoint() - m_pressPos).manhattanLength() > 6) {
            const QPixmap pm(gadgetData(QStringLiteral("gallery/%1-drag.png").arg(info.type)));
            m_ghost = new QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
            m_ghost->setAttribute(Qt::WA_TranslucentBackground);
            m_ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_ghost->setPixmap(pm);
            m_ghost->resize(pm.size());
            m_ghostOffset = QPoint(pm.width() / 2, pm.height() / 2);
            m_ghost->show();
        }
        if (m_ghost) {
            m_ghost->move(e->globalPosition().toPoint() - m_ghostOffset);
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (!m_ghost) {
            return;
        }
        const QPoint corner = e->globalPosition().toPoint() - m_ghostOffset;
        delete m_ghost;
        m_ghost = nullptr;
        if (!window()->frameGeometry().contains(e->globalPosition().toPoint()) && dropped) {
            dropped(this, corner);
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *) override
    {
        if (activated) {
            activated(this);
        }
    }

private:
    QPoint m_pressPos;
    QLabel *m_ghost = nullptr;
    QPoint m_ghostOffset;
};

/** "◀ Сторінка 1 з 1 ▶": the two round halves from sidebar.exe around a glass strip */
class GalleryPager : public QWidget
{
public:
    std::function<void(int)> step;

    explicit GalleryPager(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);
    }

    void setPages(int page, int count)
    {
        m_page = page;
        m_count = count;
        QFont f = font();
        f.setPixelSize(12);
        setFixedSize(2 * 22 + qMax(73, QFontMetrics(f).horizontalAdvance(text()) + 14), 23);
        update();
    }

protected:
    QString text() const
    {
        return tr7("Page %1 of %2").arg(m_page + 1).arg(m_count);
    }
    int partAt(QPoint pos) const
    {
        if (pos.x() < 22) {
            return -1;
        }
        if (pos.x() >= width() - 22) {
            return 1;
        }
        return 0;
    }
    bool enabledPart(int part) const
    {
        return part < 0 ? m_page > 0 : (part > 0 ? m_page < m_count - 1 : false);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPoint cursor = mapFromGlobal(QCursor::pos());
        const int hot = underMouse() ? partAt(cursor) : 0;
        auto state = [&](int part) {
            if (!enabledPart(part)) {
                return 3;
            }
            if (hot == part) {
                return m_down ? 2 : 1;
            }
            return 0;
        };
        p.drawPixmap(QRect(22, 0, width() - 44, 23), gadgetPixmap(QStringLiteral("gallery/page-mid.png")));
        p.drawPixmap(0, 0, stripFrame(gadgetPixmap(QStringLiteral("gallery/page-prev.png")), 4, state(-1)));
        p.drawPixmap(width() - 22, 0, stripFrame(gadgetPixmap(QStringLiteral("gallery/page-next.png")), 4, state(1)));
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(Qt::black);
        p.drawText(QRect(22, 0, width() - 44, 22), Qt::AlignCenter, text());
    }
    void mouseMoveEvent(QMouseEvent *) override
    {
        update();
    }
    void leaveEvent(QEvent *) override
    {
        update();
    }
    void mousePressEvent(QMouseEvent *) override
    {
        m_down = true;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        m_down = false;
        const int part = partAt(e->position().toPoint());
        if (part != 0 && enabledPart(part) && step) {
            step(part);
        }
        update();
    }

private:
    int m_page = 0;
    int m_count = 1;
    bool m_down = false;
};

/** The round "Показати подробиці" / "Сховати подробиці" button */
class DetailsButton : public QWidget
{
public:
    std::function<void()> clicked;
    bool expanded = false;

    explicit DetailsButton(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        QFont f = font();
        f.setPixelSize(12);
        setFont(f);
        setFixedSize(19 + 6 + QFontMetrics(f).horizontalAdvance(tr7("Show details")) + 4, 21);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // details.png: down (normal, hover, pressed), then up (normal, hover, pressed)
        const int state = (expanded ? 3 : 0) + (underMouse() ? (m_down ? 2 : 1) : 0);
        p.drawPixmap(0, 1, stripFrame(gadgetPixmap(QStringLiteral("gallery/details.png")), 6, state));
        drawGlowText(p, QRect(25, 0, width() - 25, height()), Qt::AlignVCenter | Qt::AlignLeft,
                     expanded ? tr7("Hide details") : tr7("Show details"));
    }
    void enterEvent(QEnterEvent *) override
    {
        update();
    }
    void leaveEvent(QEvent *) override
    {
        update();
    }
    void mousePressEvent(QMouseEvent *) override
    {
        m_down = true;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        m_down = false;
        if (rect().contains(e->position().toPoint()) && clicked) {
            clicked();
        }
        update();
    }

private:
    bool m_down = false;
};

/** Desktop Gadget Gallery: one sheet of glass with a lighter well for the tiles */
class Gallery : public QWidget
{
public:
    std::function<void(const QString &, QPoint)> add;

    Gallery()
        : QWidget(nullptr, Qt::Window)
    {
        // the caption and the icon are hidden by the smod exception for this title, like the real gallery
        setWindowTitle(tr7("Gadgets"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("gadgets")));
        setAttribute(Qt::WA_TranslucentBackground);
        setMinimumSize(380, 300);

        m_pager = new GalleryPager(this);
        m_pager->step = [this](int d) {
            m_pageIndex += d;
            fill();
        };
        m_search = new QLineEdit(this);
        m_search->setFixedSize(210, 23);
        new Win7SearchBox(m_search, tr7("Search gadgets"));
        m_search->setStyleSheet(QStringLiteral("QLineEdit { border-image: url(:/AeroQt/transpbg/entry/normal.png) 4 4 4 4 repeat; border-width: 4px; }"
                                               "QLineEdit:hover, QLineEdit:focus { border-image: url(:/AeroQt/transpbg/entry/hover.png) 4 4 4 4 repeat; }"));
        connect(m_search, &QLineEdit::textChanged, this, [this] {
            m_pageIndex = 0;
            fill();
        });

        m_toggle = new DetailsButton(this);
        m_toggle->clicked = [this] {
            m_toggle->expanded = !m_toggle->expanded;
            m_details->setVisible(m_toggle->expanded);
            resize(width(), height() + (m_toggle->expanded ? kDetails : -kDetails));
            updateDetails();
            relayout();
        };
        m_details = new QLabel(this);
        m_details->setWordWrap(true);
        m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_details->setTextFormat(Qt::RichText);
        auto *glow = new QGraphicsDropShadowEffect(m_details);
        glow->setColor(QColor(255, 255, 255, 230));
        glow->setOffset(0, 0);
        glow->setBlurRadius(8);
        m_details->setGraphicsEffect(glow);
        m_details->hide();
        setFocusPolicy(Qt::StrongFocus);

        m_catalog = catalog();
        resize(604, 368);
        relayout();
    }

protected:
    static constexpr int kHeader = 44;
    static constexpr int kFooter = 42;
    static constexpr int kDetails = 96;

    int footerHeight() const
    {
        return kFooter + (m_toggle->expanded ? kDetails : 0);
    }
    QRect well() const
    {
        return QRect(0, kHeader, width(), height() - kHeader - footerHeight());
    }

    void showEvent(QShowEvent *e) override
    {
        QWidget::showEvent(e);
        KWindowEffects::enableBlurBehind(windowHandle(), true);
        // the search box shows its hint until it is clicked
        setFocus();
    }
    void resizeEvent(QResizeEvent *) override
    {
        relayout();
    }
    void wheelEvent(QWheelEvent *e) override
    {
        const int d = e->angleDelta().y() < 0 ? 1 : -1;
        const int before = m_pageIndex;
        m_pageIndex = qBound(0, m_pageIndex + d, m_pages - 1);
        if (m_pageIndex != before) {
            fill();
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        // empty glass moves the window, like the Windows 7 gallery
        if (e->button() == Qt::LeftButton && windowHandle()) {
            windowHandle()->startSystemMove();
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // a sheet of glass; the blur and the colour come from the compositor
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(), QColor(255, 255, 255, 18));
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        const QRect w = well();
        QLinearGradient g(w.topLeft(), w.bottomLeft());
        g.setColorAt(0, QColor(255, 255, 255, 95));
        g.setColorAt(1, QColor(255, 255, 255, 70));
        p.fillRect(w, g);
        p.fillRect(QRect(0, w.top() - 1, width(), 1), QColor(0, 0, 0, 45));
        p.fillRect(QRect(0, w.top(), width(), 1), QColor(255, 255, 255, 150));
        p.fillRect(QRect(0, w.bottom(), width(), 1), QColor(255, 255, 255, 150));
        p.fillRect(QRect(0, w.bottom() + 1, width(), 1), QColor(0, 0, 0, 45));
    }

private:
    void relayout()
    {
        m_pager->move(12, (kHeader - 23) / 2);
        m_search->move(width() - 12 - m_search->width(), (kHeader - 23) / 2);
        const QRect w = well();
        m_toggle->move(12, w.bottom() + 2 + (kFooter - m_toggle->height()) / 2);
        m_details->setGeometry(16, w.bottom() + kFooter, width() - 32, kDetails - 8);
        fill();
    }

    void fill()
    {
        qDeleteAll(m_tiles);
        m_tiles.clear();
        const QString filter = m_search->text().trimmed();
        QList<GadgetInfo> shown;
        for (const GadgetInfo &g : std::as_const(m_catalog)) {
            if (filter.isEmpty() || g.name.contains(filter, Qt::CaseInsensitive) || g.description.contains(filter, Qt::CaseInsensitive)) {
                shown << g;
            }
        }
        const QRect w = well().adjusted(10, 10, -10, -6);
        const int pitchX = 104 + 4;
        const int pitchY = 110 + 4;
        const int cols = qMax(1, (w.width() + 4) / pitchX);
        const int rows = qMax(1, (w.height() + 6) / pitchY);
        const int perPage = cols * rows;
        m_pages = qMax(1, int((shown.size() + perPage - 1) / perPage));
        m_pageIndex = qBound(0, m_pageIndex, m_pages - 1);
        m_pager->setPages(m_pageIndex, m_pages);
        // the grid is centred in the well
        const int x0 = w.left() + (w.width() - (cols * pitchX - 4)) / 2;
        int i = 0;
        for (const GadgetInfo &g : shown.mid(m_pageIndex * perPage, perPage)) {
            auto *tile = new GalleryTile(g, this);
            tile->selected = g.type == m_selected.type;
            tile->clicked = [this](GalleryTile *t) {
                for (GalleryTile *o : std::as_const(m_tiles)) {
                    o->selected = (o == t);
                    o->update();
                }
                m_selected = t->info;
                updateDetails();
            };
            tile->activated = [this](GalleryTile *t) {
                if (add) {
                    add(t->info.type, QPoint(-1, -1));
                }
            };
            tile->dropped = [this](GalleryTile *t, QPoint corner) {
                if (add) {
                    add(t->info.type, corner);
                }
            };
            tile->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(tile, &QWidget::customContextMenuRequested, tile, [this, tile](QPoint pos) {
                QMenu menu;
                QAction *a = menu.addAction(tr7("Add"));
                QFont f = a->font();
                f.setBold(true);
                a->setFont(f);
                if (menu.exec(tile->mapToGlobal(pos)) == a && add) {
                    add(tile->info.type, QPoint(-1, -1));
                }
            });
            tile->move(x0 + (i % cols) * pitchX, w.top() + (i / cols) * pitchY);
            tile->show();
            m_tiles << tile;
            ++i;
        }
    }

    void updateDetails()
    {
        if (m_selected.type.isEmpty()) {
            m_details->setText(tr7("Select a gadget to see information about it."));
            return;
        }
        m_details->setText(tr7("<table width='100%' cellspacing='0' cellpadding='0'><tr><td width='38%' valign='top'><span style='font-size:15px'>%1</span><br>Version: 1.0.0.0<br><span style='color:#1e3287'>Microsoft Corporation</span></td><td valign='top'>%2</td></tr></table>")
                               .arg(m_selected.name.toHtmlEscaped(), m_selected.description.toHtmlEscaped()));
    }

    GalleryPager *m_pager;
    int m_pageIndex = 0;
    int m_pages = 1;
    QList<GadgetInfo> m_catalog;
    QLineEdit *m_search;
    QLabel *m_details;
    DetailsButton *m_toggle;
    QList<GalleryTile *> m_tiles;
    GadgetInfo m_selected;
};

/* ---------------- app ---------------- */

Gadget *makeGadget(const QString &id)
{
    if (id.startsWith(QLatin1String("weather-"))) {
        return new WeatherGadget(id);
    } else if (id.startsWith(QLatin1String("clock-"))) {
        return new ClockGadget(id);
    } else if (id.startsWith(QLatin1String("calendar-"))) {
        return new CalendarGadget(id);
    } else if (id.startsWith(QLatin1String("cpu-"))) {
        return new CpuGadget(id);
    } else if (id.startsWith(QLatin1String("slideshow-"))) {
        return new SlideShowGadget(id);
    } else if (id.startsWith(QLatin1String("puzzle-"))) {
        return new PuzzleGadget(id);
    } else if (id.startsWith(QLatin1String("currency-"))) {
        return new CurrencyGadget(id);
    } else if (id.startsWith(QLatin1String("feeds-"))) {
        return new FeedsGadget(id);
    }
    return nullptr;
}

class Manager : public QObject
{
public:
    Manager()
    {
        const QStringList ids = store().value(QStringLiteral("General/Gadgets")).toStringList();
        for (const QString &id : ids) {
            create(id);
        }
    }

    void showGallery()
    {
        if (!m_gallery) {
            m_gallery = new Gallery;
            m_gallery->setAttribute(Qt::WA_DeleteOnClose);
            m_gallery->add = [this](const QString &type, QPoint pos) {
                addGadget(type, pos);
            };
        }
        m_gallery->show();
        m_gallery->raise();
        m_gallery->activateWindow();
        KX11Extras::forceActiveWindow(m_gallery->winId());
    }

    /** pos: where it was dropped from the gallery, (-1, -1) for the default spot */
    void addGadget(const QString &type, QPoint pos = QPoint(-1, -1))
    {
        const int n = store().value(QStringLiteral("General/Next"), 1).toInt();
        store().setValue(QStringLiteral("General/Next"), n + 1);
        const QString id = QStringLiteral("%1-%2").arg(type).arg(n);
        QStringList ids = store().value(QStringLiteral("General/Gadgets")).toStringList();
        ids << id;
        store().setValue(QStringLiteral("General/Gadgets"), ids);
        store().setValue(QStringLiteral("General/Count"), ids.size() - 1);
        if (pos.x() >= 0) {
            store().setValue(id + QStringLiteral("/Pos"), pos);
        }
        store().sync();
        create(id);
    }

    bool empty() const
    {
        return m_gadgets.isEmpty() && !m_gallery;
    }

private:
    void create(const QString &id)
    {
        Gadget *g = makeGadget(id);
        if (!g) {
            return;
        }
        g->removed = [this](Gadget *gadget) {
            QStringList ids = store().value(QStringLiteral("General/Gadgets")).toStringList();
            ids.removeAll(gadget->id);
            store().setValue(QStringLiteral("General/Gadgets"), ids);
            store().remove(gadget->id);
            store().sync();
            m_gadgets.removeAll(gadget);
            gadget->deleteLater();
        };
        m_gadgets << g;
        g->place();
    }

    QList<Gadget *> m_gadgets;
    QPointer<Gallery> m_gallery;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Win7I18n::install(&app);
    app.setApplicationName(QStringLiteral("seven-gadgets"));
    app.setApplicationDisplayName(tr7("Gadgets"));
    app.setDesktopFileName(QStringLiteral("seven-gadgets"));
    app.setQuitOnLastWindowClosed(false);
    Aero::registerStylesheet(&app);

    // testing: WIN7_GADGET_GRAB=<id>:<out.png> renders one gadget off screen (use a scratch XDG_CONFIG_HOME)
    const QString grab = qEnvironmentVariable("WIN7_GADGET_GRAB");
    if (!grab.isEmpty()) {
        const QString gid = grab.section(QLatin1Char(':'), 0, 0);
        const QString out = grab.section(QLatin1Char(':'), 1);
        Gadget *g = makeGadget(gid);
        if (!g) {
            return 1;
        }
        g->grabForTest();
        QTimer::singleShot(qEnvironmentVariableIntValue("WIN7_GADGET_WAIT") + 50, &app, [g, out] {
            g->grab().save(out);
            QApplication::quit();
        });
        return app.exec();
    }

    const bool gallery = app.arguments().contains(QStringLiteral("--gallery"));
    // "--add clock": Control Panel's "Add the Clock gadget to the desktop"
    const int addArg = app.arguments().indexOf(QStringLiteral("--add"));
    const QString addType = addArg > 0 ? app.arguments().value(addArg + 1) : QString();

    // already running: just ask it to open the gallery
    {
        QLocalSocket probe;
        probe.connectToServer(kServerName);
        if (probe.waitForConnected(300)) {
            if (gallery) {
                probe.write("gallery\n");
            } else if (!addType.isEmpty()) {
                probe.write("add " + addType.toUtf8() + "\n");
            }
            probe.waitForBytesWritten(300);
            return 0;
        }
    }
    // started by a launcher (desktop menu, Control Panel) that waits for us: carry on detached, so the
    // launcher sees us finish — Plasma's executable engine ignores a command whose last run hasn't ended
    if ((gallery || !addType.isEmpty()) && !qEnvironmentVariableIsSet("WIN7_GADGETS_DETACHED")) {
        QProcess p;
        p.setProgram(QCoreApplication::applicationFilePath());
        p.setArguments(app.arguments().mid(1));
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("WIN7_GADGETS_DETACHED"), QStringLiteral("1"));
        p.setProcessEnvironment(env);
        p.setStandardOutputFile(QProcess::nullDevice());
        p.setStandardErrorFile(QProcess::nullDevice());
        if (p.startDetached()) {
            return 0;
        }
    }
    QLocalServer::removeServer(kServerName);
    QLocalServer server;
    server.listen(kServerName);

    Manager manager;
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        QLocalSocket *s = server.nextPendingConnection();
        QObject::connect(s, &QLocalSocket::readyRead, s, [s, &manager] {
            const QByteArray msg = s->readAll().trimmed();
            if (msg.contains("gallery")) {
                manager.showGallery();
            } else if (msg.startsWith("add ")) {
                manager.addGadget(QString::fromUtf8(msg.mid(4)));
            }
            s->deleteLater();
        });
    });
    if (gallery) {
        manager.showGallery();
    } else if (!addType.isEmpty()) {
        manager.addGadget(addType);
    }
    return app.exec();
}
