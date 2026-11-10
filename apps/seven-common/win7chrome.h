/*
 * Shared pieces of the Windows 7 style Control Panel windows ("System", "Control Panel"):
 * address bar crumbs, flat buttons, section headers, links and the glass header.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <AeroQt/insetwindow.h>
#include <AeroQt/navbuttons.h>
#include <AeroQt/sidebar.h>
#include <AeroQt/stylesheet.h>
#include <KConfig>
#include <KConfigGroup>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <qdrawutil.h>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHostInfo>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStyle>
#include <QSysInfo>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <functional>
#include <AeroQt/navbuttons.h>

namespace
{
void launch(const QString &program, const QStringList &args = {})
{
    QProcess::startDetached(program, args);
}

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll()); // /proc files report size 0, so read everything
}

QString keyValue(const QString &text, const QString &key, QChar sep)
{
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const int i = line.indexOf(sep);
        if (i > 0 && line.left(i).trimmed().compare(key, Qt::CaseInsensitive) == 0) {
            QString v = line.mid(i + 1).trimmed();
            if (v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"'))) {
                v = v.mid(1, v.size() - 2);
            }
            return v;
        }
    }
    return {};
}

QString gb(double value)
{
    return QLocale(QLocale::Ukrainian).toString(value, 'f', value >= 10 ? 1 : 2) + QStringLiteral(" ГБ");
}

/* ---------------- Windows 7 address bar pieces (same look as in the Explorer) ---------------- */

void drawArrow(QPainter &p, qreal cx, qreal cy, const QColor &color)
{
    const qreal x = std::floor(cx);
    const qreal y = std::floor(cy) + 0.5;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(QPolygonF({QPointF(x - 1.5, y - 3.5), QPointF(x + 2.5, y), QPointF(x - 1.5, y + 3.5)}));
    p.restore();
}

void drawHover(QPainter &p, const QRectF &r, bool pressed)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(pressed ? QColor(60, 127, 177, 170) : QColor(60, 127, 177, 110));
    p.setBrush(pressed ? QColor(196, 229, 246, 200) : QColor(229, 243, 251, 160));
    p.drawRoundedRect(r.adjusted(0.5, 1.5, -0.5, -1.5), 2, 2);
    p.restore();
}

/** Looks of a Windows 7 address bar cell (the same as in our Explorer, measured on Windows 7 screenshots) */
enum class Win7Cell {
    Hot, // the crumb part under the mouse, and the rest of that crumb when its name is hovered
    Pressed,
    Neighbour, // the name part of a crumb while its "▸" is hovered: grey
    BandHot, // the "▾" and refresh buttons at the right end of the bar
    BandPressed,
};

/** A Windows 7 address bar cell from column @p x0 to @p x1 (inclusive) over the rows @p top..@p bottom:
    light inner lines, a two-part glossy fill and dark side edges where @p leftEdge / @p rightEdge */
void drawWin7Cell(QPainter &p, int x0, int x1, int top, int bottom, Win7Cell cell, bool leftEdge, bool rightEdge)
{
    if (x1 < x0 || bottom - top < 3) {
        return;
    }
    QColor edge, lightTop, lightBottom, fill[4];
    switch (cell) {
    case Win7Cell::Hot:
        edge = QColor(0x3c, 0x7f, 0xb1);
        lightTop = QColor(0xfa, 0xfd, 0xfe);
        lightBottom = QColor(0xe8, 0xf5, 0xfc);
        fill[0] = QColor(0xea, 0xf6, 0xfd), fill[1] = QColor(0xd7, 0xef, 0xfc), fill[2] = QColor(0xbd, 0xe6, 0xfd), fill[3] = QColor(0xa6, 0xd9, 0xf4);
        break;
    case Win7Cell::Pressed:
        edge = QColor(0x2c, 0x62, 0x8b);
        lightTop = QColor(0xe5, 0xf4, 0xfc);
        lightBottom = QColor(0x9e, 0xd2, 0xee);
        fill[0] = QColor(0xe5, 0xf4, 0xfc), fill[1] = QColor(0xc4, 0xe5, 0xf6), fill[2] = QColor(0x98, 0xd1, 0xef), fill[3] = QColor(0x68, 0xb3, 0xdb);
        break;
    case Win7Cell::Neighbour:
        edge = QColor(0x8e, 0x8f, 0x8f);
        lightTop = QColor(0xfc, 0xfc, 0xfc);
        lightBottom = QColor(0xf3, 0xf3, 0xf3);
        fill[0] = QColor(0xf2, 0xf2, 0xf2), fill[1] = QColor(0xef, 0xef, 0xef), fill[2] = QColor(0xd5, 0xd5, 0xd5), fill[3] = QColor(0xcf, 0xcf, 0xcf);
        break;
    case Win7Cell::BandHot:
        edge = QColor(0x16, 0x58, 0x8b);
        lightTop = QColor(0xfd, 0xfe, 0xff);
        lightBottom = QColor(0xce, 0xeb, 0xfa);
        fill[0] = QColor(0xd3, 0xed, 0xfb), fill[1] = QColor(0xc6, 0xe9, 0xfa), fill[2] = QColor(0x9c, 0xd8, 0xf7), fill[3] = QColor(0x89, 0xd0, 0xf5);
        break;
    case Win7Cell::BandPressed:
        edge = QColor(0x10, 0x46, 0x70);
        lightTop = QColor(0xc4, 0xe5, 0xf6);
        lightBottom = QColor(0x8c, 0xc8, 0xe8);
        fill[0] = QColor(0xc4, 0xe5, 0xf6), fill[1] = QColor(0xa9, 0xda, 0xf2), fill[2] = QColor(0x7c, 0xc4, 0xea), fill[3] = QColor(0x5d, 0xae, 0xdc);
        break;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    const int innerTop = top + 1, innerBottom = bottom - 1;
    const int mid = innerTop + (innerBottom - innerTop + 1) / 2;
    QLinearGradient upper(0, innerTop, 0, mid - 1), lower(0, mid, 0, innerBottom);
    upper.setColorAt(0, fill[0]);
    upper.setColorAt(1, fill[1]);
    lower.setColorAt(0, fill[2]);
    lower.setColorAt(1, fill[3]);
    p.fillRect(QRect(QPoint(x0, innerTop), QPoint(x1, mid - 1)), upper);
    p.fillRect(QRect(QPoint(x0, mid), QPoint(x1, innerBottom)), lower);
    p.fillRect(QRect(QPoint(x0, top), QPoint(x1, top)), lightTop);
    p.fillRect(QRect(QPoint(x0, bottom), QPoint(x1, bottom)), lightBottom);
    // light inner lines along the sides (inside the edges, if any)
    const int lx = leftEdge ? x0 + 1 : x0, rx = rightEdge ? x1 - 1 : x1;
    for (int x : {lx, rx}) {
        p.fillRect(QRect(QPoint(x, innerTop), QPoint(x, mid - 1)), lightTop);
        p.fillRect(QRect(QPoint(x, mid), QPoint(x, innerBottom)), lightBottom.lighter(106));
    }
    if (leftEdge) {
        p.fillRect(QRect(QPoint(x0, top), QPoint(x0, bottom)), edge);
    }
    if (rightEdge) {
        p.fillRect(QRect(QPoint(x1, top), QPoint(x1, bottom)), edge);
    }
    p.restore();
}

/** The dark line between two cells: the color of the hot one */
void drawWin7Divider(QPainter &p, int x, int top, int bottom, Win7Cell cell)
{
    const QColor c = cell == Win7Cell::Neighbour ? QColor(0x8e, 0x8f, 0x8f)
        : cell == Win7Cell::Pressed              ? QColor(0x2c, 0x62, 0x8b)
        : cell == Win7Cell::BandHot              ? QColor(0x16, 0x58, 0x8b)
        : cell == Win7Cell::BandPressed          ? QColor(0x10, 0x46, 0x70)
                                                 : QColor(0x3c, 0x7f, 0xb1);
    p.fillRect(QRect(QPoint(x, top), QPoint(x, bottom)), c);
}

/** "▸ Control Panel ▸ All Control Panel Items ▸ System ▸"
    Windows 7 highlights the name and the "▸" of a crumb separately: the name hovered → both blue,
    the "▸" hovered → the "▸" blue and the name grey. The cells cover the whole height of the bar. */
class Crumbs : public QWidget
{
public:
    struct Crumb {
        QString text;
        std::function<void()> activate;
    };

    explicit Crumbs(QList<Crumb> crumbs, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_crumbs(std::move(crumbs))
    {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    }

    QSize sizeHint() const override
    {
        layoutItems();
        return QSize(m_width, 20);
    }

    /** Fills the menu shown by the "▸" after crumb @p crumb (-1 = the one before the first crumb) */
    void setArrowMenu(std::function<void(QMenu *, int)> fn)
    {
        m_arrowMenu = std::move(fn);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        layoutItems();
        QPainter p(this);
        const int top = 0, bottom = height() - 1;
        const int active = m_menuOpen >= 0 ? m_menuOpen : m_hover;
        const bool down = m_menuOpen >= 0 || m_pressed;
        const Win7Cell hot = down ? Win7Cell::Pressed : Win7Cell::Hot;
        if (active >= 0) {
            const Item &it = m_items.at(active);
            if (it.crumb < 0) {
                // the "▸" before the first crumb
                drawWin7Cell(p, it.rect.left(), it.rect.right(), top, bottom, hot, true, true);
            } else {
                const Item &name = m_items.at(it.arrow ? active - 1 : active);
                const Item &arrow = m_items.at(it.arrow ? active : active + 1);
                drawWin7Cell(p, name.rect.left(), name.rect.right(), top, bottom, it.arrow ? Win7Cell::Neighbour : hot, true, false);
                drawWin7Divider(p, arrow.rect.left(), top, bottom, hot);
                drawWin7Cell(p, arrow.rect.left() + 1, arrow.rect.right(), top, bottom, hot, false, true);
            }
        }
        const qreal cy = height() / 2.0;
        p.setPen(Qt::black);
        for (const Item &it : std::as_const(m_items)) {
            if (it.arrow) {
                // the "▸" sits in the cell right of the dividing line
                drawArrow(p, it.rect.left() + (it.crumb < 0 ? it.rect.width() / 2.0 : (it.rect.width() + 1) / 2.0) + 0.5, cy, QColor(0x1a, 0x1a, 0x1a));
            } else {
                p.drawText(it.rect.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, m_crumbs.at(it.crumb).text);
            }
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = itemAt(e->position().toPoint());
        if (h != m_hover) {
            m_hover = h;
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        m_pressed = false;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        m_hover = itemAt(e->position().toPoint());
        m_pressed = true;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        m_pressed = false;
        const int i = itemAt(e->position().toPoint());
        update();
        if (i < 0) {
            return;
        }
        const Item &it = m_items.at(i);
        if (!it.arrow && m_crumbs.at(it.crumb).activate) {
            m_crumbs.at(it.crumb).activate();
        } else if (it.arrow) {
            // "▸" after a crumb: places below it; it stays pressed while the menu is open
            QMenu menu(this);
            if (m_arrowMenu) {
                m_arrowMenu(&menu, it.crumb);
            }
            if (!menu.isEmpty()) {
                m_menuOpen = i;
                update();
                menu.exec(mapToGlobal(QPoint(it.rect.left(), height())));
                m_menuOpen = -1;
                m_hover = itemAt(mapFromGlobal(QCursor::pos()));
                update();
            }
        }
    }

private:
    struct Item {
        QRect rect;
        int crumb; // the crumb this part belongs to; -1 = the "▸" before the first crumb
        bool arrow; // the "▸" part (its first column is the line between it and the name)
    };
    int itemAt(const QPoint &pos) const
    {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items.at(i).rect.contains(pos)) {
                return i;
            }
        }
        return -1;
    }
    void layoutItems() const
    {
        m_items.clear();
        const QFontMetrics fm(font());
        int x = 0;
        const int h = qMax(height(), 20);
        m_items.append({QRect(x, 0, 15, h), -1, true});
        x += 15;
        for (int i = 0; i < m_crumbs.size(); ++i) {
            const int w = fm.horizontalAdvance(m_crumbs.at(i).text) + 8;
            m_items.append({QRect(x, 0, w, h), i, false});
            x += w;
            m_items.append({QRect(x, 0, 15, h), i, true});
            x += 15;
        }
        m_width = x;
    }

    QList<Crumb> m_crumbs;
    std::function<void(QMenu *, int)> m_arrowMenu;
    mutable QList<Item> m_items;
    mutable int m_width = 0;
    int m_hover = -1;
    int m_menuOpen = -1;
    bool m_pressed = false;
};

/** The "▾" (previous locations) and Refresh at the right end of the address bar: flat cells over its whole
    height, a grey line between them; hovered a dark blue left edge and a glossy blue fill */
class Win7BandButton : public QToolButton
{
public:
    explicit Win7BandButton(QWidget *parent, bool refresh)
        : QToolButton(parent)
        , m_refresh(refresh)
    {
        setAttribute(Qt::WA_Hover);
        setAutoRaise(true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setFixedWidth(refresh ? 25 : 17); // the refresh button's first column is the line between the two
    }
    Win7BandButton *partner = nullptr;
    bool menuOpen = false;

protected:
    bool event(QEvent *e) override
    {
        if (partner && (e->type() == QEvent::Enter || e->type() == QEvent::Leave || e->type() == QEvent::MouseButtonPress
                        || e->type() == QEvent::MouseButtonRelease)) {
            partner->update();
        }
        return QToolButton::event(e);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int top = 0, bottom = height() - 1;
        const bool down = isDown() || menuOpen;
        const bool hot = (underMouse() && isEnabled()) || down;
        const int x0 = m_refresh ? 1 : 0;
        if (hot) {
            const Win7Cell cell = down ? Win7Cell::BandPressed : Win7Cell::BandHot;
            drawWin7Cell(p, x0, width() - 1, top, bottom, cell, x0 == 0, false);
            drawWin7Divider(p, 0, top, bottom, cell);
        } else if (m_refresh) {
            const bool partnerHot = partner && (partner->underMouse() || partner->isDown() || partner->menuOpen);
            p.fillRect(QRect(QPoint(0, top + 1), QPoint(0, bottom - 1)), partnerHot ? QColor(0x6a, 0x6b, 0x6c) : QColor(0x6a, 0x6b, 0x6c, 150));
        }
        const QPixmap px = icon().pixmap(iconSize(), isEnabled() ? QIcon::Normal : QIcon::Disabled);
        const QSize ps = px.deviceIndependentSize().toSize();
        int y = (top + bottom + 1 - ps.height()) / 2;
        int x = x0 + (width() - x0 - ps.width()) / 2;
        if (!m_refresh && ps.height() == 16) {
            // the genuine glyph: its first row (row 6 of the picture) is the first row of the lower half,
            // and only columns 0..8 of the 16px picture are used
            const int innerTop = top + 1, innerBottom = bottom - 1;
            y = innerTop + (innerBottom - innerTop + 1) / 2 - 6;
            x = (width() + 1 - 9) / 2;
        }
        p.drawPixmap(x, y, px);
    }

private:
    bool m_refresh;
};

QToolButton *flatButton(QWidget *parent, const QIcon &icon, const QString &tip)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icon);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setStyleSheet(QStringLiteral(
        "QToolButton { border-image: none; border: 1px solid transparent; border-radius: 2px; background: transparent; padding: 0px; }"
        "QToolButton:hover { border-image: none; border-color: rgba(60, 127, 177, 110); background: rgba(229, 243, 251, 160); }"
        "QToolButton:pressed { border-image: none; border-color: rgba(60, 127, 177, 170); background: rgba(196, 229, 246, 200); }"));
    return b;
}

QPixmap smallDownArrow()
{
    QPixmap arrow(7, 4);
    arrow.fill(Qt::transparent);
    QPainter p(&arrow);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x1e, 0x1e, 0x28));
    p.drawPolygon(QPolygonF({QPointF(0, 0), QPointF(7, 0), QPointF(3.5, 4)}));
    return arrow;
}

/* ---------------- Page pieces ---------------- */

/** "Windows edition ──────────" */
class SectionHeader : public QWidget
{
public:
    explicit SectionHeader(const QString &text, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_text(text)
    {
        setFixedHeight(QFontMetrics(font()).height() + 6);
    }
    /** the blue variant (AutoPlay and other task dialogs): #003399 text, light blue line */
    void setBlue(bool on)
    {
        m_blue = on;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(m_blue ? QColor(0x00, 0x33, 0x99) : QColor(0x3b, 0x3b, 0x3b));
        p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, m_text);
        const int x = QFontMetrics(font()).horizontalAdvance(m_text) + 6;
        const qreal y = height() / 2.0 + 1.5;
        if (m_blue) {
            p.setPen(QColor(0xb2, 0xc1, 0xe0));
        } else {
            QLinearGradient g(x, 0, width(), 0);
            g.setColorAt(0, QColor(0xe2, 0xe2, 0xe2));
            g.setColorAt(1, QColor(0xf4, 0xf4, 0xf4));
            p.setPen(QPen(QBrush(g), 1));
        }
        p.drawLine(QPointF(x, y), QPointF(width(), y));
    }

private:
    QString m_text;
    bool m_blue = false;
};

/** Blue link that underlines on hover, like in the Windows 7 Control Panel */
class Link : public QLabel
{
public:
    Link(const QString &text, std::function<void()> action, const QString &color = QStringLiteral("#0066cc"), QWidget *parent = nullptr)
        : QLabel(text, parent)
        , m_action(std::move(action))
        , m_color(color)
    {
        setCursor(Qt::PointingHandCursor);
        setWordWrap(true);
        apply(false);
    }

protected:
    void enterEvent(QEnterEvent *) override
    {
        apply(true);
    }
    void leaveEvent(QEvent *) override
    {
        apply(false);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && m_action) {
            m_action();
        }
    }

private:
    void apply(bool hover)
    {
        // blue links turn light blue on hover, green titles stay green (only underlined), like Windows 7
        const bool green = QColor(m_color).green() > QColor(m_color).blue() + 40;
        setStyleSheet(QStringLiteral("QLabel { color: %1; text-decoration: %2; background: transparent; }")
                          .arg(hover && !green ? QStringLiteral("#3399ff") : m_color, hover ? QStringLiteral("underline") : QStringLiteral("none")));
    }
    std::function<void()> m_action;
    QString m_color;
};

QLabel *grey(const QString &text)
{
    auto *l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("color: #555555;"));
    return l;
}

QLabel *value(const QString &text)
{
    auto *l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("color: #000000;"));
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setWordWrap(true);
    return l;
}


/**
 * Windows 7 search box behaviour: grey italic hint that disappears as soon as the box gets the focus,
 * the magnifier turns into a small ✕ (clear) once something is typed.
 */
class Win7SearchBox : public QObject
{
public:
    Win7SearchBox(QLineEdit *edit, const QString &hint)
        : QObject(edit)
        , m_edit(edit)
        , m_hint(hint)
    {
        m_search = QIcon::fromTheme(QStringLiteral("seven-explorer-search"), QIcon::fromTheme(QStringLiteral("gtk-search")));
        QPixmap x(16, 16);
        x.fill(Qt::transparent);
        {
            QPainter p(&x);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(0x5a, 0x5a, 0x5a), 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(5, 5), QPointF(11, 11));
            p.drawLine(QPointF(11, 5), QPointF(5, 11));
        }
        m_clear = QIcon(x);
        m_action = edit->addAction(m_search, QLineEdit::TrailingPosition);
        QObject::connect(m_action, &QAction::triggered, edit, [this] {
            if (m_edit->hasFocus() || !m_edit->text().isEmpty()) {
                // ✕: back to the untouched state (empty, hint, magnifier)
                m_edit->clear();
                m_edit->clearFocus();
                update();
            } else {
                m_edit->setFocus();
            }
        });
        // Stay white while typing, like the hovered look (Aero draws unfocused boxes translucent)
        edit->setStyleSheet(QStringLiteral("QLineEdit:focus { border-image: url(:/AeroQt/transpbg/entry/hover.png) 4 4 4 4 repeat; border-width: 4px; }"));
        QObject::connect(edit, &QLineEdit::textChanged, this, [this] {
            update();
        });
        edit->installEventFilter(this);
        update();
    }

protected:
    bool eventFilter(QObject *, QEvent *e) override
    {
        if (e->type() == QEvent::FocusIn || e->type() == QEvent::FocusOut) {
            update();
        }
        return false;
    }

private:
    void update()
    {
        const bool empty = m_edit->text().isEmpty();
        const bool active = m_edit->hasFocus() || !empty;
        m_action->setIcon(active ? m_clear : m_search);
        m_action->setToolTip(active ? QStringLiteral("Очистити") : QString());
        // the hint is only shown while the box is empty and not focused
        m_edit->setPlaceholderText(m_edit->hasFocus() ? QString() : m_hint);
        QFont f = m_edit->font();
        f.setItalic(empty && !m_edit->hasFocus());
        m_edit->setFont(f);
    }

    QLineEdit *m_edit;
    QString m_hint;
    QIcon m_search, m_clear;
    QAction *m_action;
};

/** The address bar's frame: the Aero text box picture (lighter while the mouse is over the bar). Painted here and
    not with a style sheet border, so the crumbs' cells can cover the white inner line like in Windows 7. */
class Win7AddressFrame : public QFrame
{
public:
    explicit Win7AddressFrame(QWidget *parent)
        : QFrame(parent)
    {
        setAttribute(Qt::WA_Hover);
    }

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::HoverEnter || e->type() == QEvent::HoverLeave) {
            update();
        }
        return QFrame::event(e);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QPixmap pm(underMouse() ? QStringLiteral(":/AeroQt/transpbg/entry/hover.png") : QStringLiteral(":/AeroQt/transpbg/entry/normal.png"));
        qDrawBorderPixmap(&p, rect(), QMargins(4, 4, 4, 4), pm);
    }
};

/** Glass header: ◀▶, address bar with crumbs, ▾, Refresh and the search box */
struct Win7Header {
    QWidget *widget = nullptr;
    Aero::NavButtons *navs = nullptr;
    Crumbs *crumbs = nullptr;
    QLabel *icon = nullptr; // location icon left of the crumbs
    QToolButton *history = nullptr;
    QToolButton *refresh = nullptr;
    QLineEdit *search = nullptr;
};

inline Win7Header makeWin7Header(QWidget *parent, const QList<Crumbs::Crumb> &crumbs, const QString &iconName, const QString &searchPlaceholder)
{
    Win7Header h;
    h.widget = new QWidget(parent);
    auto *layout = new QHBoxLayout(h.widget);
    layout->setContentsMargins(4, 4, 6, 4);
    layout->setSpacing(4);

    h.navs = new Aero::NavButtons(h.widget);
    h.navs->back()->setEnabled(false);
    h.navs->forward()->setEnabled(false);
    layout->addWidget(h.navs);

    auto *address = new Win7AddressFrame(h.widget);
    address->setObjectName(QStringLiteral("win7address"));
    address->setFixedHeight(26);
    auto *al = new QHBoxLayout(address);
    // the crumbs and the buttons cover the whole bar inside its border (rows 2..h-3, the white inner line too),
    // the refresh cell up to the right border
    al->setContentsMargins(6, 2, 2, 2);
    al->setSpacing(0);
    auto *icon = new QLabel(address);
    icon->setFixedWidth(22);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(QIcon::fromTheme(iconName).pixmap(16, 16));
    al->addWidget(icon);
    h.icon = icon;
    h.crumbs = new Crumbs(crumbs, address);
    al->addWidget(h.crumbs);
    al->addStretch(1);
    auto *history = new Win7BandButton(address, false);
    if (QIcon::hasThemeIcon(QStringLiteral("seven-address-dropdown"))) {
        history->setIcon(QIcon::fromTheme(QStringLiteral("seven-address-dropdown")));
        history->setIconSize(QSize(16, 16));
    } else {
        history->setIcon(QIcon(smallDownArrow()));
        history->setIconSize(QSize(7, 4));
    }
    history->setToolTip(QStringLiteral("Попередні розташування"));
    h.history = history;
    al->addWidget(history);
    auto *refresh = new Win7BandButton(address, true);
    refresh->setIcon(QIcon::fromTheme(QStringLiteral("seven-address-refresh"), QIcon::fromTheme(QStringLiteral("view-refresh"))));
    refresh->setIconSize(QSize(16, 16));
    refresh->setToolTip(QStringLiteral("Оновити"));
    history->partner = refresh;
    refresh->partner = history;
    h.refresh = refresh;
    al->addWidget(refresh);
    layout->addWidget(address, 1);

    h.search = new QLineEdit(h.widget);
    h.search->setFixedHeight(26);
    h.search->setMinimumWidth(200);
    h.search->setMaximumWidth(260);
    new Win7SearchBox(h.search, searchPlaceholder);
    layout->addWidget(h.search);
    return h;
}

/** Opens our Windows 7 style tool if installed in ~/.local/bin, otherwise the KDE fallback */
inline void launchWin7Tool(const QString &tool, const QString &fallback, const QStringList &fallbackArgs = {})
{
    const QString exe = QStandardPaths::findExecutable(tool, {QDir::homePath() + QStringLiteral("/.local/bin")});
    if (!exe.isEmpty()) {
        launch(exe);
    } else {
        launch(fallback, fallbackArgs);
    }
}
}
