/*
 * The chrome of the AMD Catalyst Control Center (14.x, the Windows 7 era): frameless window with the black
 * banner (radial red glow, AMD Radeon logo, round glossy minimise/close buttons, "Preferences" pill), the
 * left navigation (search box, grey gradient section rows with chevrons and icons, plain sub-items) and the
 * right column (page header with pin and help, white page, Discard / Apply bar). Sizes, colours and gradients
 * are taken from the decompiled WPF resources of CCC (Dashboard.Shared.Private dictionaries, red colour set).
 */
#pragma once

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <functional>

namespace Ccc
{

inline QString assets()
{
    return QDir::homePath() + QStringLiteral("/.local/share/seven-ccc/");
}

/* icon from the converted CCC vector icons (assets/icons/<key>.svg) */
inline QPixmap icon(const QString &key, int size)
{
    static QHash<QString, QPixmap> cache;
    const QString id = key + QString::number(size);
    if (cache.contains(id)) {
        return cache.value(id);
    }
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm(QSize(size, size) * dpr);
    pm.fill(Qt::transparent);
    if (key == QLatin1String("IconFavourites")) {
        QPixmap p(assets() + QStringLiteral("pin.png"));
        QPainter g(&pm);
        g.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSizeF s = QSizeF(p.size()).scaled(QSizeF(pm.size()), Qt::KeepAspectRatio);
        g.drawPixmap(QRectF(QPointF((pm.width() - s.width()) / 2, (pm.height() - s.height()) / 2), s), p, p.rect());
    } else {
        QSvgRenderer r(assets() + QStringLiteral("icons/") + key + QStringLiteral(".svg"));
        QPainter g(&pm);
        r.render(&g, QRectF(QPointF(), QSizeF(pm.size())));
    }
    pm.setDevicePixelRatio(dpr);
    cache.insert(id, pm);
    return pm;
}

inline QFont uiFont(int px, int weight = QFont::Normal)
{
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(px);
    f.setWeight(QFont::Weight(weight));
    return f;
}

/* ---------------------------------------------------------------- banner */

/* round glossy black button of the banner (HeaderCloseButton / HeaderMinimizeButton) */
class RoundButton : public QAbstractButton
{
public:
    enum Glyph { Close, Minimize };
    RoundButton(Glyph g, QWidget *parent)
        : QAbstractButton(parent)
        , m_glyph(g)
    {
        setFixedSize(21, 21);
        setCursor(Qt::ArrowCursor);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(QColor(0x5A, 0x5A, 0x5A), 1.4));
        p.setBrush(Qt::black);
        p.drawEllipse(r);
        // glow (radial, white centre at 25 %)
        QRadialGradient glow(r.center(), r.width() / 2);
        glow.setColorAt(0.03, QColor(255, 255, 255, int(0xBF * 0.25)));
        glow.setColorAt(0.8, Qt::transparent);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(r);
        if (underMouse() || isDown()) {
            QLinearGradient hl(r.topLeft(), r.bottomLeft());
            hl.setColorAt(0, Qt::transparent);
            hl.setColorAt(1, QColor(255, 255, 255, isDown() ? 0x50 : 0x80));
            p.setBrush(hl);
            p.drawEllipse(r);
        }
        // gloss: ellipse in the upper part
        const QRectF gr(r.left() + r.width() * 0.143, r.top() + r.height() * 0.095, r.width() * 0.714, r.height() * 0.524);
        QLinearGradient gl(gr.topLeft(), gr.bottomLeft());
        gl.setColorAt(0, QColor(255, 255, 255, 0x7F));
        gl.setColorAt(1, Qt::transparent);
        p.setBrush(gl);
        p.drawEllipse(gr);
        // glyph
        QPen pen(QColor(0xC8, 0xC8, 0xC8), 1.5);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        const QRectF g = QRectF(rect()).adjusted(8, 8, -8, -8);
        if (m_glyph == Close) {
            p.drawLine(g.topLeft(), g.bottomRight());
            p.drawLine(g.bottomLeft(), g.topRight());
        } else {
            p.drawLine(QPointF(g.left(), g.bottom() - 0.5), QPointF(g.right(), g.bottom() - 0.5));
        }
    }

private:
    Glyph m_glyph;
};

/* black glossy pill (PreferenceMenuButton): "Параметри" */
class PillButton : public QAbstractButton
{
public:
    PillButton(const QString &text, QWidget *parent)
        : QAbstractButton(parent)
    {
        setText(text);
        setFont(uiFont(12));
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::ArrowCursor);
        setFixedSize(fontMetrics().horizontalAdvance(text) + 22, 20);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal rad = r.height() / 2;
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawRoundedRect(r, rad, rad);
        QLinearGradient o1(r.topLeft(), r.bottomLeft());
        o1.setColorAt(0, Qt::transparent);
        o1.setColorAt(1, QColor(255, 255, 255, underMouse() || isDown() ? 0xA0 : 0x60));
        p.setBrush(o1);
        p.drawRoundedRect(r, rad, rad);
        QLinearGradient o3(r.topLeft(), QPointF(r.left(), r.center().y()));
        o3.setColorAt(0, QColor(255, 255, 255, 0x7F));
        o3.setColorAt(1, Qt::transparent);
        p.setBrush(o3);
        p.drawRoundedRect(QRectF(r.left() + r.width() * 0.05, r.top() + 1, r.width() * 0.9, r.height() / 2), 5, rad / 2);
        p.setPen(QPen(QColor(0x33, 0x33, 0x33), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, rad, rad);
        p.setPen(Qt::white);
        p.setFont(font());
        p.drawText(rect().adjusted(0, 0, 0, -2), Qt::AlignCenter, text());
    }
};

class Banner : public QWidget
{
public:
    Banner(const QString &title, QWidget *parent)
        : QWidget(parent)
        , m_title(title)
        , m_logo(assets() + QStringLiteral("amd_radeon.png"))
    {
        setFixedHeight(100);
        setMouseTracking(true);
        minimize = new RoundButton(RoundButton::Minimize, this);
        close = new RoundButton(RoundButton::Close, this);
        minimize->setToolTip(QStringLiteral("Згорнути"));
        close->setToolTip(QStringLiteral("Закрити"));
        preferences = new PillButton(QStringLiteral("Параметри"), this);
    }
    RoundButton *minimize, *close;
    PillButton *preferences;
    std::function<void()> logoClicked;

protected:
    void resizeEvent(QResizeEvent *) override
    {
        close->move(width() - 9 - 21, 9);
        minimize->move(close->x() - 3 - 21, 9);
        preferences->move(width() - 10 - preferences->width(), height() - 10 - preferences->height());
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x08, 0x08, 0x08));
        // DBMainHeaderGradient: radial, origin (1.059, 0.495), radii (0.854, 0.624), rotated 90.356°:
        // the red comes up from the middle of the bottom edge
        p.save();
        p.translate(width() * 0.5, height() * 0.5);
        p.scale(width() * 0.624, height() * 0.854);
        QRadialGradient g(QPointF(0, 0), 1, QPointF((0.505 - 0.5) / 0.624, (1.059 - 0.5) / 0.854));
        g.setColorAt(0, QColor(0x80, 0x80, 0x80)); // banner colour set "white": Gray
        g.setColorAt(0.795, QColor(0x08, 0x08, 0x08));
        g.setColorAt(1, QColor(0x08, 0x08, 0x08));
        p.fillRect(QRectF(-1.6, -1.6, 3.2, 3.2), g);
        p.restore();
        // logo: 140x90 picture centred in the 160x100 thumb (Viewbox MaxWidth 140, Margin 5)
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (m_hover) {
            // DBBannerLogoGlow: red glow when the mouse is over the logo
            QRadialGradient glow(QPointF(80, 50), 80);
            glow.setColorAt(0, QColor(0xF1, 0x2B, 0x33, 120));
            glow.setColorAt(1, Qt::transparent);
            p.fillRect(QRect(0, 0, 160, 100), glow);
        }
        p.drawPixmap(QRect(10, 5, 140, 90), m_logo);
        p.setPen(Qt::white);
        p.setFont(uiFont(20, QFont::DemiBold));
        p.drawText(QRect(165, 0, width() - 165 - 50, height()), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const bool h = QRect(0, 0, 160, 100).contains(e->position().toPoint());
        if (h != m_hover) {
            m_hover = h;
            setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = false;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        if (m_hover && logoClicked) {
            logoClicked();
            return;
        }
        window()->windowHandle()->startSystemMove();
    }
    void mouseDoubleClickEvent(QMouseEvent *) override
    {
        window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
    }

private:
    QString m_title;
    QPixmap m_logo;
    bool m_hover = false;
};

/* ---------------------------------------------------------------- left navigation */

struct NavItem {
    QString id, text;
};

struct NavSection {
    QString id, text, icon;
    QList<NavItem> items;
};

class NavRow;

/* the blue magnifier of the search box (SearchIconGradient) */
inline QPixmap magnifier()
{
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pm(QSize(16, 16) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 2, 0, 11);
    g.setColorAt(0, QColor(0x35, 0xA7, 0xD3));
    g.setColorAt(1, QColor(0x21, 0x39, 0x68));
    QPen ring(QBrush(g), 2.2);
    p.setPen(ring);
    p.setBrush(QColor(255, 255, 255, 200));
    p.drawEllipse(QRectF(5, 1.5, 9, 9));
    QPen handle(QColor(0x21, 0x39, 0x68), 3);
    handle.setCapStyle(Qt::RoundCap);
    p.setPen(handle);
    p.drawLine(QPointF(6.3, 9.7), QPointF(2.5, 13.5));
    return pm;
}

/* framed little button with the double chevron (NavigationCollapseToggleButton) */
class CollapseButton : public QAbstractButton
{
public:
    bool collapsed = false;
    explicit CollapseButton(QWidget *p)
        : QAbstractButton(p)
    {
        setAttribute(Qt::WA_Hover);
        setToolTip(QStringLiteral("Згорнути область переходів"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0, underMouse() ? QColor(0xEA, 0xF6, 0xFD) : QColor(0xFC, 0xFC, 0xFC));
        g.setColorAt(1, underMouse() ? QColor(0xBE, 0xE6, 0xFD) : QColor(0xE3, 0xE3, 0xE3));
        p.setPen(QPen(underMouse() ? QColor(0x3C, 0x7F, 0xB1) : QColor(0xA4, 0xA4, 0xA4), 1));
        p.setBrush(g);
        p.drawRoundedRect(r, 2, 2);
        QPen pen(QColor(0x33, 0x33, 0x33), 1.1);
        p.setPen(pen);
        const qreal cx = width() / 2.0, cy = height() / 2.0;
        const qreal d = collapsed ? -1 : 1; // « or »
        for (qreal off : {-2.0, 2.0}) {
            const qreal x = cx + off - d * 1.0;
            p.drawPolyline(QPolygonF{QPointF(x + d * 1.5, cy - 3), QPointF(x - d * 1.5, cy), QPointF(x + d * 1.5, cy + 3)});
        }
    }
};

class Navigation : public QWidget
{
public:
    explicit Navigation(QWidget *parent)
        : QWidget(parent)
    {
        setFixedWidth(165);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        auto *top = new QWidget(this);
        top->setFixedHeight(29);
        auto *th = new QHBoxLayout(top);
        th->setContentsMargins(5, 4, 3, 3);
        th->setSpacing(4);
        search = new QLineEdit(top);
        search->setPlaceholderText(QStringLiteral("Пошук"));
        search->setFixedHeight(20);
        QFont f = uiFont(12);
        search->setFont(f);
        // the magnifier of the Windows 7 Start menu search box (gtk-search of the Windows 7 Aero icon theme)
        search->addAction(QIcon::fromTheme(QStringLiteral("gtk-search"), QIcon(magnifier())), QLineEdit::TrailingPosition);
        auto italic = [this] {
            QFont fi = uiFont(12);
            fi.setItalic(search->text().isEmpty()); // "Search" placeholder in italics, typed text upright
            search->setFont(fi);
        };
        italic();
        QObject::connect(search, &QLineEdit::textChanged, this, italic);
        th->addWidget(search);
        collapse = new CollapseButton(top);
        collapse->setFixedSize(17, 20);
        collapse->setToolTip(QStringLiteral("Згорнути область переходів"));
        th->addWidget(collapse);
        v->addWidget(top);
        m_scroll = new QScrollArea(this);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setWidgetResizable(true);
        m_body = new QWidget;
        m_body->setAutoFillBackground(true);
        QPalette pal = m_body->palette();
        pal.setColor(QPalette::Window, QColor(0xF1, 0xF1, 0xF1));
        m_body->setPalette(pal);
        m_list = new QVBoxLayout(m_body);
        m_list->setContentsMargins(0, 0, 0, 0);
        m_list->setSpacing(0);
        m_list->addStretch(1);
        m_scroll->setWidget(m_body);
        v->addWidget(m_scroll, 1);
        QObject::connect(search, &QLineEdit::textChanged, this, [this] { rebuild(); });
        QObject::connect(collapse, &QAbstractButton::clicked, this, [this] { setCollapsed(!m_collapsed); });
    }

    QLineEdit *search;
    QAbstractButton *collapse;
    std::function<void(const QString &)> activated;

    void setSections(const QList<NavSection> &s)
    {
        m_sections = s;
        rebuild();
    }
    void setCurrent(const QString &id)
    {
        m_current = id;
        for (const NavSection &s : m_sections) {
            for (const NavItem &i : s.items) {
                if (i.id == id) {
                    m_open.insert(s.id);
                }
            }
        }
        rebuild();
    }
    QString current() const { return m_current; }
    void toggle(const QString &section)
    {
        if (m_open.contains(section)) {
            m_open.remove(section);
        } else {
            m_open.insert(section);
        }
        rebuild();
    }
    void setCollapsed(bool c)
    {
        m_collapsed = c;
        setFixedWidth(c ? 46 : 165);
        search->setVisible(!c);
        static_cast<CollapseButton *>(collapse)->collapsed = c;
        collapse->update();
        rebuild();
    }
    bool collapsed() const { return m_collapsed; }

    void rebuild();

private:
    QList<NavSection> m_sections;
    QSet<QString> m_open;
    QString m_current;
    QScrollArea *m_scroll;
    QWidget *m_body;
    QVBoxLayout *m_list;
    bool m_collapsed = false;
};

/* section header row: NavigationGradient, chevron, text (wrapped), icon on the right */
class NavRow : public QWidget
{
public:
    NavRow(Navigation *nav, const NavSection &s, bool open, bool collapsed)
        : QWidget(nullptr)
        , m_nav(nav)
        , m_s(s)
        , m_open(open)
        , m_collapsed(collapsed)
    {
        setFixedHeight(29);
        setAttribute(Qt::WA_Hover);
        setToolTip(collapsed ? s.text : QString());
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0.011, QColor(0xF1, 0xF1, 0xF3));
        g.setColorAt(1, QColor(0xD0, 0xD7, 0xDD));
        p.fillRect(rect(), g);
        if (underMouse()) {
            // HighlightGradient over the row
            QLinearGradient h(0, 0, 0, height());
            h.setColorAt(0, QColor(255, 255, 255, 0x19));
            h.setColorAt(1, QColor(255, 255, 255, 0xD8));
            p.fillRect(rect(), h);
        }
        p.setPen(QColor(0xFF, 0xFF, 0xFF));
        p.drawLine(0, 0, width(), 0);
        p.setPen(QColor(0xA4, 0xA4, 0xA4));
        p.drawLine(0, height() - 1, width(), height() - 1);
        const int isz = 24;
        const QRect ir(width() - isz - 6, (height() - isz) / 2, isz, isz);
        if (m_collapsed) {
            p.drawPixmap(QRect((width() - isz) / 2, (height() - isz) / 2, isz, isz), icon(m_s.icon, isz));
            return;
        }
        p.drawPixmap(ir, icon(m_s.icon, isz));
        // chevron
        QPen pen(QColor(0x33, 0x33, 0x33), 1.3);
        p.setPen(pen);
        const QPointF c(9, height() / 2.0);
        if (m_open) {
            p.drawPolyline(QPolygonF{c + QPointF(-3, -1.5), c + QPointF(0, 1.5), c + QPointF(3, -1.5)});
        } else {
            p.drawPolyline(QPolygonF{c + QPointF(-1.5, -3), c + QPointF(1.5, 0), c + QPointF(-1.5, 3)});
        }
        p.setPen(QColor(0x1E, 0x1E, 0x1E));
        QFont f = uiFont(12);
        p.setFont(f);
        const QRect tr(17, 0, ir.left() - 19, height());
        QFontMetrics fm(f);
        if (fm.horizontalAdvance(m_s.text) > tr.width()) {
            QFont f2 = uiFont(11);
            p.setFont(f2);
            QTextOption o(Qt::AlignLeft | Qt::AlignVCenter);
            o.setWrapMode(QTextOption::WordWrap);
            QRectF br = tr.adjusted(0, 1, 0, -1);
            p.drawText(br, m_s.text, o);
        } else {
            p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, m_s.text);
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        if (m_collapsed) {
            m_nav->setCollapsed(false);
        }
        if (m_s.items.isEmpty()) {
            if (m_nav->activated) {
                m_nav->activated(m_s.id);
            }
            return;
        }
        QTimer::singleShot(0, m_nav, [n = m_nav, id = m_s.id] { n->toggle(id); });
    }

private:
    Navigation *m_nav;
    NavSection m_s;
    bool m_open, m_collapsed;
};

/* sub-item: plain text link, bold red bar when it is the current page */
class NavLink : public QWidget
{
public:
    NavLink(Navigation *nav, const NavItem &i, bool current)
        : m_nav(nav)
        , m_i(i)
        , m_current(current)
    {
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        QFont f = uiFont(12, current ? QFont::DemiBold : QFont::Normal);
        QFontMetrics fm(f);
        const int w = 165 - 26 - 6;
        const QRect br = fm.boundingRect(QRect(0, 0, w, 1000), Qt::TextWordWrap, i.text);
        setFixedHeight(qMax(19, br.height() + 4));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0xF1, 0xF1, 0xF1));
        if (m_current) {
            // SectionHighlightGradientBrush
            QLinearGradient g(0, 0, 0, height());
            g.setColorAt(0, QColor(0xC0, 0, 0, 0xD8));
            g.setColorAt(1, QColor(255, 255, 255, 0x0C));
            p.fillRect(QRect(0, 0, 4, height()), QColor(0xA6, 0, 0));
            QLinearGradient g2(0, 0, width(), 0);
            g2.setColorAt(0, QColor(0xA6, 0, 0, 0x30));
            g2.setColorAt(1, QColor(0xA6, 0, 0, 0));
            p.fillRect(rect(), g2);
        } else if (underMouse()) {
            QLinearGradient g2(0, 0, width(), 0);
            g2.setColorAt(0, QColor(0x76, 0, 0, 0x22));
            g2.setColorAt(1, QColor(0xDA, 0, 0x09, 0));
            p.fillRect(rect(), g2);
        }
        QFont f = uiFont(12, m_current ? QFont::DemiBold : QFont::Normal);
        f.setUnderline(underMouse() && !m_current);
        p.setFont(f);
        p.setPen(m_current ? QColor(0x99, 0, 0) : QColor(0x1E, 0x1E, 0x1E));
        p.drawText(rect().adjusted(26, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, m_i.text);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && m_nav->activated) {
            QTimer::singleShot(0, m_nav, [n = m_nav, id = m_i.id] { n->activated(id); });
        }
    }

private:
    Navigation *m_nav;
    NavItem m_i;
    bool m_current;
};

inline void Navigation::rebuild()
{
    while (m_list->count() > 1) {
        QLayoutItem *it = m_list->takeAt(0);
        if (it->widget()) {
            it->widget()->deleteLater();
        }
        delete it;
    }
    const QString q = m_collapsed ? QString() : search->text().trimmed();
    int at = 0;
    if (!q.isEmpty()) {
        // search results: every page whose name (or section name) contains the text
        auto *hdr = new NavRow(this, NavSection{QStringLiteral("search"), QStringLiteral("Результати пошуку"), QStringLiteral("IconSearchResults"), {}}, true, false);
        m_list->insertWidget(at++, hdr);
        for (const NavSection &s : m_sections) {
            for (const NavItem &i : s.items) {
                if (i.text.contains(q, Qt::CaseInsensitive) || s.text.contains(q, Qt::CaseInsensitive)) {
                    m_list->insertWidget(at++, new NavLink(this, i, i.id == m_current));
                }
            }
        }
        return;
    }
    for (const NavSection &s : m_sections) {
        const bool open = m_open.contains(s.id) && !m_collapsed;
        m_list->insertWidget(at++, new NavRow(this, s, open, m_collapsed));
        if (open) {
            for (const NavItem &i : s.items) {
                m_list->insertWidget(at++, new NavLink(this, i, i.id == m_current));
            }
        }
    }
}

/* ---------------------------------------------------------------- right column */

/* page title bar: HeaderBackgroundGradient, bold title, pin and help on the right */
class PageHeader : public QWidget
{
public:
    explicit PageHeader(QWidget *parent)
        : QWidget(parent)
    {
        setFixedHeight(31);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(10, 0, 6, 0);
        h->setSpacing(4);
        title = new QLabel(this);
        title->setFont(uiFont(15, QFont::DemiBold));
        title->setStyleSheet(QStringLiteral("color:#1e1e1e;background:transparent"));
        h->addWidget(title, 1);
        pin = new QToolButton(this);
        pin->setAutoRaise(true);
        pin->setFixedSize(22, 22);
        pin->setIconSize(QSize(18, 18));
        h->addWidget(pin);
        help = new QToolButton(this);
        help->setAutoRaise(true);
        help->setFixedSize(22, 22);
        help->setToolTip(QStringLiteral("Довідка"));
        h->addWidget(help);
    }
    QLabel *title;
    QToolButton *pin, *help;
    void setPinned(bool on)
    {
        pin->setIcon(QIcon(QPixmap(assets() + (on ? QStringLiteral("pin.png") : QStringLiteral("pin_add.png")))));
        pin->setToolTip(on ? QStringLiteral("Відкріпити цю сторінку") : QStringLiteral("Закріпити цю сторінку"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor(0xF1, 0xF5, 0xFB));
        g.setColorAt(0.5, Qt::white);
        g.setColorAt(1, QColor(0xD0, 0xD7, 0xDD));
        p.fillRect(rect(), g);
        p.setPen(QColor(0xA4, 0xA4, 0xA4));
        p.drawLine(0, height() - 1, width(), height() - 1);
        // help glyph (IconHelp is drawn with the foreground colour)
        const QRect hr = help->geometry().adjusted(4, 3, -4, -3);
        QPainterPath qp;
        qp.addText(0, 0, uiFont(17, QFont::Bold), QStringLiteral("?"));
        const QRectF b = qp.boundingRect();
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(hr.center() - b.center());
        p.fillPath(qp, QColor(0x1F, 0x5F, 0xA6));
    }
};

/* Discard / Apply bar (ApplyBtnsBG) */
class ApplyBar : public QWidget
{
public:
    explicit ApplyBar(QWidget *parent)
        : QWidget(parent)
    {
        setFixedHeight(44);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(10, 9, 10, 9);
        h->setSpacing(8);
        defaults = new QPushButton(QStringLiteral("За замовчуванням"), this);
        defaults->setMinimumWidth(85);
        h->addWidget(defaults);
        h->addStretch(1);
        discard = new QPushButton(QStringLiteral("Скасувати"), this);
        apply = new QPushButton(QStringLiteral("Застосувати"), this);
        for (QPushButton *b : {discard, apply}) {
            b->setMinimumWidth(85);
            h->addWidget(b);
        }
    }
    QPushButton *defaults, *discard, *apply;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor(0xF1, 0xF1, 0xF1));
        g.setColorAt(0.996, QColor(0xD0, 0xD7, 0xDD));
        p.fillRect(rect(), g);
        p.setPen(QColor(0xA4, 0xA4, 0xA4));
        p.drawLine(0, 0, width(), 0);
    }
};

/* group heading inside a page: bold text on SectionBackgroundGradient with a line under it */
class SectionTitle : public QWidget
{
public:
    explicit SectionTitle(const QString &t, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_t(t)
    {
        setFixedHeight(26);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor(0xF1, 0xF1, 0xF3));
        g.setColorAt(1, Qt::white);
        p.fillRect(rect(), g);
        p.setPen(QColor(0xD0, 0xD7, 0xDD));
        p.drawLine(0, height() - 1, width(), height() - 1);
        p.setPen(QColor(0x1E, 0x1E, 0x1E));
        p.setFont(uiFont(12, QFont::DemiBold));
        p.drawText(rect().adjusted(10, 0, -10, 0), Qt::AlignLeft | Qt::AlignVCenter, m_t);
    }

private:
    QString m_t;
};

} // namespace Ccc
