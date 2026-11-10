#pragma once
/*
 * Library mode of WMP 12: address band (◀▶, breadcrumbs, Play/Burn/Sync tabs), command bar (Organize, Stream,
 * Create playlist, view options, search), navigation pane, the details/icon views and the list pane.
 */
#include "library.h"
#include "res.h"
#include "transport.h"

#include <KIO/PreviewJob>

#include <QApplication>
#include <QFileDialog>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainterPath>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace Wmp
{
const QColor TEXT(0x1e, 0x1e, 0x1e);
const QColor GROUP(0x2e, 0x3d, 0xa8);
const QColor HEADER_TEXT(0x6d, 0x6d, 0x6d);
const QColor LINE(0xc9, 0xd7, 0xe8);
const QColor BAND_TOP(0xe1, 0xeb, 0xf7);
const QColor BAND_BOTTOM(0xe6, 0xef, 0xf9);
const QColor PLAYING(0x94, 0x00, 0x98); // the item being played is purple in the library
inline QString playingPath;

/** the light swoosh background of the library (library_background5.png stretched over the window) */
inline void paintBackground(QPainter &p, const QRect &r, const QWidget *w)
{
    const QWidget *win = w->window();
    const QPoint off = w->mapTo(win, QPoint(0, 0));
    p.drawPixmap(QRect(-off.x(), -off.y(), win->width(), win->height()), Res::px(QStringLiteral("library_background5")));
    Q_UNUSED(r)
}
}

/** flat WMP column header: grey text, thin separators, no gradient */
class WmpHeader : public QHeaderView
{
public:
    explicit WmpHeader(QWidget *parent)
        : QHeaderView(Qt::Horizontal, parent)
    {
        setSectionsClickable(true);
        setHighlightSections(false);
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setFixedHeight(22);
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QPainter p(viewport());
        Wmp::paintBackground(p, rect(), viewport());
        p.end();
        QHeaderView::paintEvent(e);
        QPainter q(viewport());
        q.setPen(Wmp::LINE);
        q.drawLine(0, height() - 1, width(), height() - 1);
    }
    void paintSection(QPainter *p, const QRect &rect, int index) const override
    {
        const QPoint m = viewport()->mapFromGlobal(QCursor::pos());
        if (rect.contains(m) && viewport()->rect().contains(m)) {
            QLinearGradient g(rect.topLeft(), rect.bottomLeft());
            g.setColorAt(0, QColor(255, 255, 255, 200));
            g.setColorAt(1, QColor(0xe3, 0xee, 0xfa, 200));
            p->fillRect(rect.adjusted(0, 0, -1, -1), g);
        }
        p->setPen(QColor(0xd6, 0xe2, 0xef));
        p->drawLine(rect.right(), rect.top() + 1, rect.right(), rect.bottom());
        p->setPen(Wmp::HEADER_TEXT);
        const QString t = model()->headerData(index, Qt::Horizontal).toString();
        p->drawText(rect.adjusted(6, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(t, Qt::ElideRight, rect.width() - 10));
    }
};

/** ★ rating painter (stars.png: 429x16 strip) */
inline void paintStars(QPainter *p, const QRect &r, int rating, int hover = -1)
{
    // stars.png: 13px stars in groups of five - 0 empty, 1 set, 3 hot
    const QPixmap &s = Res::px(QStringLiteral("stars"));
    for (int i = 0; i < 5; ++i) {
        int group = 0;
        if (hover >= 0) {
            group = i < hover ? 3 : 0;
        } else if (rating > 0) {
            group = i < rating ? 1 : 0;
        } else {
            // unrated: WMP's automatic three blue-outlined stars
            group = i < 3 ? 5 : 0;
        }
        p->drawPixmap(QPoint(r.x() + i * 13, r.y() + (r.height() - 16) / 2), s, QRect((group * 5 + i) * 13, 0, 13, 16));
    }
}

// ----------------------------------------------------------------------------------------------------------------

/** ◀▶ breadcrumbs ... Play Burn Sync */
class AddressBand : public QWidget
{
    Q_OBJECT
public:
    explicit AddressBand(QWidget *parent)
        : QWidget(parent)
    {
        setFixedHeight(38);
        setMouseTracking(true);
    }
    void setCrumbs(const QStringList &c)
    {
        m_crumbs = c;
        update();
    }
    void setNav(bool back, bool forward)
    {
        m_back = back;
        m_forward = forward;
        update();
    }
    void setTab(int t)
    {
        m_tab = t;
        update();
    }
    int tab() const
    {
        return m_tab;
    }
Q_SIGNALS:
    void backClicked();
    void forwardClicked();
    void crumbClicked(int index);
    void tabClicked(int tab);

protected:
    QRect tabRect(int i) const
    {
        static const QStringList tabs{QStringLiteral("Відтворити"), QStringLiteral("Записати"), QStringLiteral("Синхронізувати")};
        auto w = [&](int k) {
            return qMax(82, fontMetrics().horizontalAdvance(tabs[k]) + 26);
        };
        int x = width() - 8;
        for (int k = 2; k >= i; --k) {
            x -= w(k) + 2;
        }
        return QRect(x + 2, 8, w(i), height() - 8);
    }
    QList<QRect> crumbRects() const
    {
        QList<QRect> out;
        int x = 66;
        for (const QString &c : m_crumbs) {
            const int w = fontMetrics().horizontalAdvance(c) + 22;
            out << QRect(x, 7, w, 22);
            x += w;
        }
        return out;
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, Wmp::BAND_TOP);
        g.setColorAt(1, Wmp::BAND_BOTTOM);
        p.fillRect(rect(), g);
        p.setPen(QColor(0xbf, 0xc7, 0xcf));
        p.drawLine(0, height() - 1, width(), height() - 1);

        // ◀▶ orb pair (5350 normal, 5351 back hot, 5352 forward hot, 5353 disabled) - drawn per half
        const QPixmap &dis = Res::px(5353), &nor = Res::px(5350);
        const QPoint o(8, 5);
        const int half = 27;
        auto halfPm = [&](bool enabled, bool hot, int side) -> const QPixmap & {
            if (!enabled) {
                return dis;
            }
            if (hot) {
                Q_UNUSED(side)
                return Res::px(5352);
            }
            return nor;
        };
        p.drawPixmap(o, halfPm(m_back, m_hover == -10, 0), QRect(0, 0, half, 27));
        p.drawPixmap(o + QPoint(half, 0), halfPm(m_forward, m_hover == -11, 1), QRect(half, 0, 27, 27));

        // crumbs: ▸ Name ▸ Name
        const auto rects = crumbRects();
        for (int i = 0; i < m_crumbs.size(); ++i) {
            const QRect r = rects[i];
            if (m_hover == i) {
                p.setPen(QColor(0x9a, 0xb4, 0xd4));
                p.setBrush(QColor(255, 255, 255, 120));
                p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
            }
            // small black triangle
            QPainterPath tri;
            const int tx = r.x() + 6, ty = r.center().y() + 1;
            tri.moveTo(tx, ty - 4);
            tri.lineTo(tx + 4, ty);
            tri.lineTo(tx, ty + 4);
            tri.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x1e, 0x1e, 0x1e));
            p.setRenderHint(QPainter::Antialiasing);
            p.drawPath(tri);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QColor(0x1e, 0x39, 0x5b));
            p.drawText(r.adjusted(16, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, m_crumbs[i]);
        }

        // tabs
        const QStringList tabs{QStringLiteral("Відтворити"), QStringLiteral("Записати"), QStringLiteral("Синхронізувати")};
        for (int i = 0; i < 3; ++i) {
            const QRect r = tabRect(i);
            const bool active = m_tab == i;
            const bool hot = m_hover == 100 + i;
            QPainterPath path;
            path.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, 4), 3, 3);
            QLinearGradient tg(r.topLeft(), r.bottomLeft());
            if (active) {
                tg.setColorAt(0, QColor(0xfb, 0xfd, 0xff));
                tg.setColorAt(1, QColor(0xf4, 0xf8, 0xfd));
            } else if (hot) {
                tg.setColorAt(0, QColor(0xf2, 0xf8, 0xff));
                tg.setColorAt(1, QColor(0xdc, 0xea, 0xfa));
            } else {
                tg.setColorAt(0, QColor(0xe6, 0xec, 0xf3));
                tg.setColorAt(1, QColor(0xe9, 0xf0, 0xf9));
            }
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QColor(0xa9, 0xb8, 0xc9));
            p.setBrush(tg);
            p.save();
            p.setClipRect(QRect(r.x() - 1, r.y() - 1, r.width() + 2, r.height() + (active ? 1 : 0)));
            p.drawPath(path);
            p.restore();
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QColor(0x1e, 0x39, 0x5b));
            p.drawText(r.adjusted(0, 1, 0, 0), Qt::AlignCenter, tabs[i]);
        }
    }
    int hitTest(const QPoint &pos) const
    {
        if (QRect(8, 5, 27, 27).contains(pos)) {
            return -10;
        }
        if (QRect(35, 5, 27, 27).contains(pos)) {
            return -11;
        }
        const auto rects = crumbRects();
        for (int i = 0; i < rects.size(); ++i) {
            if (rects[i].contains(pos)) {
                return i;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (tabRect(i).contains(pos)) {
                return 100 + i;
            }
        }
        return -1;
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = hitTest(e->pos());
        if (h != m_hover) {
            m_hover = h;
            setToolTip(h == -10 ? QStringLiteral("Назад") : h == -11 ? QStringLiteral("Вперед") : h == 100 ? QStringLiteral("Створення списків відтворення, керування вмістом і обмін ним")
                       : h == 101 ? QStringLiteral("Запис аудіо-компакт-дисків або дисків даних")
                       : h == 102 ? QStringLiteral("Синхронізація з переносними пристроями") : QString());
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        const int h = hitTest(e->pos());
        if (h == -10 && m_back) {
            Q_EMIT backClicked();
        } else if (h == -11 && m_forward) {
            Q_EMIT forwardClicked();
        } else if (h >= 0 && h < 100) {
            Q_EMIT crumbClicked(h);
        } else if (h >= 100) {
            Q_EMIT tabClicked(h - 100);
        }
    }

private:
    QStringList m_crumbs;
    bool m_back = false, m_forward = false;
    int m_tab = -1;
    int m_hover = -1;
};

/** Organize ▾  Stream ▾  Create playlist ▾ ............ [view ▾] [Search ▾] (?) */
class CommandBar : public QWidget
{
    Q_OBJECT
public:
    explicit CommandBar(QWidget *parent)
        : QWidget(parent)
    {
        setFixedHeight(29);
        setMouseTracking(true);
        auto *l = new QHBoxLayout(this);
        l->setContentsMargins(0, 2, 6, 2);
        l->addStretch(1);
        m_search = new QLineEdit(this);
        m_search->setPlaceholderText(QStringLiteral("Пошук"));
        m_search->setFixedSize(240, 22);
        m_search->setStyleSheet(QStringLiteral("QLineEdit { border: 1px solid #a3b5ca; background: rgba(255,255,255,0.75); padding-left: 3px; font-style: italic; }"
                                               "QLineEdit:focus { background: white; font-style: normal; }"));
        // the magnifier of the Start menu search box
        m_search->addAction(QIcon::fromTheme(QStringLiteral("gtk-search")), QLineEdit::TrailingPosition);
        l->addWidget(m_search);
        l->addSpacing(6);
        m_help = new QToolButton(this);
        m_help->setAutoRaise(true);
        m_help->setIcon(Res::icon(29601));
        m_help->setIconSize(QSize(16, 16));
        m_help->setToolTip(QStringLiteral("Довідка"));
        l->addWidget(m_help);
    }
    QLineEdit *search() const
    {
        return m_search;
    }
    QToolButton *help() const
    {
        return m_help;
    }
    void setPlaylistMode(bool on)
    {
        m_items[2] = on ? QStringLiteral("Створити список відтворення") : QStringLiteral("Створити список відтворення");
        update();
    }
Q_SIGNALS:
    void menuRequested(int index, const QPoint &global);
    void viewMenuRequested(const QPoint &global);

protected:
    QList<QRect> itemRects() const
    {
        QList<QRect> out;
        int x = 6;
        for (int i = 0; i < m_items.size(); ++i) {
            const int w = fontMetrics().horizontalAdvance(m_items[i]) + 30;
            out << QRect(x, 2, w, height() - 4);
            x += w + 6;
        }
        return out;
    }
    QRect viewRect() const
    {
        return QRect(m_search->x() - 40, 2, 34, height() - 4);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // command_bar_bg.png: 20x32, stretched horizontally
        p.drawPixmap(rect(), Res::px(QStringLiteral("command_bar_bg")), QRect(0, 0, 20, 32));
        const auto rects = itemRects();
        for (int i = 0; i <= rects.size(); ++i) {
            const QRect r = i < rects.size() ? rects[i] : viewRect();
            if (m_hover == i || m_down == i) {
                // WMP toolbar button picture: 51x22, 3 states? use the light hot frame
                const QPixmap &b = Res::px(QStringLiteral("wmp_toolbarbutton"));
                const int sw = b.width() / 3;
                const int st = m_down == i ? 2 : 1;
                QPixmap part = b.copy(st * sw, 0, sw, b.height());
                Res::hslice(p, r, part, 4, 4);
            }
            if (i < rects.size()) {
                p.setPen(QColor(0x1e, 0x39, 0x5b));
                p.drawText(r.adjusted(8, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, m_items[i]);
                drawArrow(p, r.right() - 10, r.center().y() + 1);
            } else {
                p.drawPixmap(r.x() + 2, r.center().y() - 8, Res::icon(7702).pixmap(16, 16));
                drawArrow(p, r.right() - 6, r.center().y() + 1);
            }
        }
    }
    static void drawArrow(QPainter &p, int cx, int cy)
    {
        QPainterPath a;
        a.moveTo(cx - 3.5, cy - 2);
        a.lineTo(cx + 3.5, cy - 2);
        a.lineTo(cx, cy + 2);
        a.closeSubpath();
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x1e, 0x1e, 0x1e));
        p.drawPath(a);
        p.restore();
    }
    int hitTest(const QPoint &pos) const
    {
        const auto rects = itemRects();
        for (int i = 0; i < rects.size(); ++i) {
            if (rects[i].contains(pos)) {
                return i;
            }
        }
        if (viewRect().contains(pos)) {
            return int(rects.size());
        }
        return -1;
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = hitTest(e->pos());
        if (h != m_hover) {
            m_hover = h;
            setToolTip(h == 3 ? QStringLiteral("Параметри перегляду") : QString());
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const int h = hitTest(e->pos());
        if (h < 0) {
            return;
        }
        m_down = h;
        update();
        const auto rects = itemRects();
        const QRect r = h < rects.size() ? rects[h] : viewRect();
        if (h < rects.size()) {
            Q_EMIT menuRequested(h, mapToGlobal(r.bottomLeft() + QPoint(0, 1)));
        } else {
            Q_EMIT viewMenuRequested(mapToGlobal(r.bottomLeft() + QPoint(0, 1)));
        }
        m_down = -1;
        update();
    }

private:
    QStringList m_items{QStringLiteral("Упорядкувати"), QStringLiteral("Потік"), QStringLiteral("Створити список відтворення")};
    QLineEdit *m_search;
    QToolButton *m_help;
    int m_hover = -1;
    int m_down = -1;
};

// ----------------------------------------------------------------------------------------------------------------

/** delegate of the music details view: album art + album lines in the Album column, stars, group headers */
class MusicDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    int hoverRow = -1;   // stars under the mouse in the Rating column
    int hoverStars = 0;
    enum Roles { KindRole = Qt::UserRole + 1, PathRole, LineRole, InfoRole, RatingRole, FirstPathRole };
    enum RowKind { GroupRow = 1, TrackRow = 2, PadRow = 3 };

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        Q_UNUSED(opt)
        return QSize(40, idx.data(KindRole).toInt() == GroupRow ? 24 : 20);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &idx) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, idx);
        const int kind = idx.data(KindRole).toInt();
        if (kind == GroupRow) {
            if (idx.column() != 0) {
                return;
            }
            p->save();
            p->setPen(Wmp::GROUP);
            const QString t = idx.data(Qt::DisplayRole).toString();
            const int tw = opt.fontMetrics.horizontalAdvance(t);
            p->drawText(opt.rect.adjusted(8, 4, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, t);
            p->setPen(Wmp::LINE);
            const int y = opt.rect.center().y() + 2;
            const QWidget *view = opt.widget;
            p->drawLine(opt.rect.x() + 14 + tw, y, view ? view->width() : opt.rect.right(), y);
            p->restore();
            return;
        }
        // selection / hover (WMP: light blue rows)
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hover = opt.state & QStyle::State_MouseOver;
        if (kind == TrackRow && (selected || hover) && idx.column() != 0) {
            p->fillRect(opt.rect, selected ? QColor(0xcc, 0xe4, 0xfc, 200) : QColor(0xe5, 0xf1, 0xfd, 160));
        }
        if (idx.column() == 0) {
            // album art spans the first rows of the group, the album lines next to it
            const int line = idx.data(LineRole).toInt();
            const QString first = idx.data(FirstPathRole).toString();
            if (!first.isEmpty() && line < 5) {
                QImage art = Library::albumArt(first);
                QPixmap pm = art.isNull() ? Res::px(QStringLiteral("album_0")) : QPixmap::fromImage(art);
                const QRect artRect(opt.rect.x() + 4, opt.rect.y() - line * opt.rect.height() + 4, 96, 96);
                p->save();
                p->setClipRect(opt.rect);
                p->setRenderHint(QPainter::SmoothPixmapTransform);
                // drop shadow
                p->fillRect(artRect.translated(2, 2), QColor(0, 0, 0, 40));
                p->drawPixmap(artRect, pm);
                p->restore();
            }
            const QString info = idx.data(InfoRole).toString();
            if (!info.isEmpty()) {
                p->setPen(Wmp::TEXT);
                p->drawText(opt.rect.adjusted(108, 2, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, opt.fontMetrics.elidedText(info, Qt::ElideRight, opt.rect.width() - 112));
            }
            return;
        }
        // column separators run through the whole list like in WMP
        p->save();
        p->setPen(QColor(0xdd, 0xe7, 0xf2));
        p->drawLine(opt.rect.right(), opt.rect.top(), opt.rect.right(), opt.rect.bottom());
        p->restore();
        if (kind != TrackRow) {
            return;
        }
        if (idx.data(RatingRole).isValid()) {
            paintStars(p, opt.rect.adjusted(4, 0, 0, 0), idx.data(RatingRole).toInt(), idx.row() == hoverRow ? hoverStars : -1);
            return;
        }
        // title-like columns black, the others (artist, composer, size...) grey
        const bool playing = !Wmp::playingPath.isEmpty() && idx.model()->index(idx.row(), 0).data(PathRole).toString() == Wmp::playingPath;
        p->setPen(playing ? Wmp::PLAYING : idx.column() >= 5 ? QColor(0x6d, 0x6d, 0x6d) : Wmp::TEXT);
        const bool right = idx.data(Qt::TextAlignmentRole).toInt() & Qt::AlignRight;
        const QString t = idx.data(Qt::DisplayRole).toString();
        p->drawText(opt.rect.adjusted(6, 2, -6, 0), (right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                    opt.fontMetrics.elidedText(t, Qt::ElideRight, opt.rect.width() - 12));
    }
};

/** the navigation tree: Win7 triangles instead of +/- boxes and lines */
template<class Base>
class Win7Branches : public Base
{
public:
    using Base::Base;

protected:
    void drawBranches(QPainter *p, const QRect &rect, const QModelIndex &index) const override
    {
        if (!this->model()->hasChildren(index)) {
            return;
        }
        const bool open = this->isExpanded(index);
        const QPoint c(rect.right() - 7, rect.center().y());
        QPainterPath tri;
        if (open) {
            // ◢ filled
            tri.moveTo(c.x() + 3, c.y() - 3);
            tri.lineTo(c.x() + 3, c.y() + 3);
            tri.lineTo(c.x() - 3, c.y() + 3);
        } else {
            // ▷ outline
            tri.moveTo(c.x() - 2, c.y() - 4);
            tri.lineTo(c.x() + 2, c.y());
            tri.lineTo(c.x() - 2, c.y() + 4);
        }
        tri.closeSubpath();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (open) {
            p->setPen(QColor(0x26, 0x26, 0x26));
            p->setBrush(QColor(0x59, 0x59, 0x59));
        } else {
            p->setPen(QColor(0xa6, 0xa6, 0xa6));
            p->setBrush(Qt::white);
        }
        p->drawPath(tri);
        p->restore();
    }
};

/** a view with the library background behind transparent rows */
template<class Base>
class Transparent : public Base
{
public:
    explicit Transparent(QWidget *parent)
        : Base(parent)
    {
        this->viewport()->setAutoFillBackground(false);
        this->setFrameShape(QFrame::NoFrame);
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        {
            QPainter p(this->viewport());
            Wmp::paintBackground(p, this->viewport()->rect(), this->viewport());
        }
        Base::paintEvent(e);
    }
};

/** Videos / Pictures: thumbnails (videos in a film strip frame) with title, length and rating */
class ThumbDelegate : public QStyledItemDelegate
{
public:
    ThumbDelegate(bool video, QObject *parent)
        : QStyledItemDelegate(parent)
        , m_video(video)
    {
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return m_video ? QSize(250, 76) : QSize(130, 110);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &idx) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, idx);
        const QRect r = opt.rect;
        if (opt.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(opt.state & QStyle::State_Selected ? QColor(0x84, 0xac, 0xdd) : QColor(0xb8, 0xd6, 0xfb));
            p->setBrush(opt.state & QStyle::State_Selected ? QColor(0xcc, 0xe4, 0xfc, 210) : QColor(0xe5, 0xf1, 0xfd, 170));
            p->drawRoundedRect(QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5), 3, 3);
            p->restore();
        }
        const QPixmap thumb = idx.data(Qt::DecorationRole).value<QPixmap>();
        if (m_video) {
            // film frame 120x68: sprockets_256.png scaled, the picture inside
            const QRect frame(r.x() + 4, r.y() + 4, 122, 68);
            p->save();
            p->setRenderHint(QPainter::SmoothPixmapTransform);
            p->fillRect(frame, Qt::black);
            const QRect pic = frame.adjusted(12, 4, -12, -4);
            if (!thumb.isNull()) {
                const QPixmap s = thumb.scaled(pic.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                p->drawPixmap(pic, s, QRect((s.width() - pic.width()) / 2, (s.height() - pic.height()) / 2, pic.width(), pic.height()));
            }
            p->drawPixmap(frame, Res::px(QStringLiteral("sprockets_256")));
            p->restore();
            p->setPen(idx.data(Qt::UserRole + 1).toString() == Wmp::playingPath ? Wmp::PLAYING : Wmp::TEXT);
            const int tx = frame.right() + 6;
            p->drawText(QRect(tx, r.y() + 14, r.right() - tx, 16), Qt::AlignLeft | Qt::AlignVCenter,
                        opt.fontMetrics.elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, r.right() - tx));
            p->setPen(QColor(0x6d, 0x6d, 0x6d));
            p->drawText(QRect(tx, r.y() + 30, r.right() - tx, 16), Qt::AlignLeft | Qt::AlignVCenter, idx.data(Qt::UserRole + 2).toString());
            const int rating = idx.data(Qt::UserRole + 3).toInt();
            p->drawText(QRect(tx, r.y() + 46, r.right() - tx, 16), Qt::AlignLeft | Qt::AlignVCenter,
                        rating ? QString(rating, QChar(0x2605)) : QStringLiteral("Без оцінки"));
        } else {
            const QRect pic(r.x() + 5, r.y() + 5, r.width() - 10, r.height() - 28);
            if (!thumb.isNull()) {
                const QPixmap s = thumb.scaled(pic.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                const QRect sr(pic.x() + (pic.width() - s.width()) / 2, pic.bottom() - s.height(), s.width(), s.height());
                p->fillRect(sr.translated(2, 2), QColor(0, 0, 0, 40));
                p->drawPixmap(sr, s);
            }
            p->setPen(Wmp::TEXT);
            p->drawText(QRect(r.x() + 2, r.bottom() - 20, r.width() - 4, 18), Qt::AlignCenter,
                        opt.fontMetrics.elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, r.width() - 6));
        }
    }

private:
    bool m_video;
};

/** album / artist / genre tiles: cover with the name and the second line under it */
class TileDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(132, 170);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &idx) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, idx);
        const QRect r = opt.rect;
        if (opt.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(opt.state & QStyle::State_Selected ? QColor(0x84, 0xac, 0xdd) : QColor(0xb8, 0xd6, 0xfb));
            p->setBrush(opt.state & QStyle::State_Selected ? QColor(0xcc, 0xe4, 0xfc, 210) : QColor(0xe5, 0xf1, 0xfd, 170));
            p->drawRoundedRect(QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5), 3, 3);
            p->restore();
        }
        const QString first = idx.data(Qt::UserRole + 1).toString();
        QImage art = first.isEmpty() ? QImage() : Library::albumArt(first);
        QPixmap pm = art.isNull() ? Res::px(QStringLiteral("album_0")) : QPixmap::fromImage(art);
        const QRect a(r.x() + 16, r.y() + 8, 100, 100);
        p->save();
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        if (idx.data(Qt::UserRole + 4).toBool()) {
            // artists / genres: a stack of covers
            p->fillRect(a.translated(6, -4), QColor(0xd0, 0xd8, 0xe2));
            p->fillRect(a.translated(3, -2), QColor(0xe4, 0xea, 0xf1));
        }
        p->fillRect(a.translated(2, 2), QColor(0, 0, 0, 40));
        p->drawPixmap(a, pm);
        p->restore();
        p->setPen(Wmp::TEXT);
        p->drawText(QRect(r.x() + 6, a.bottom() + 6, r.width() - 12, 16), Qt::AlignCenter,
                    opt.fontMetrics.elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, r.width() - 12));
        p->setPen(QColor(0x6d, 0x6d, 0x6d));
        p->drawText(QRect(r.x() + 6, a.bottom() + 22, r.width() - 12, 16), Qt::AlignCenter,
                    opt.fontMetrics.elidedText(idx.data(Qt::UserRole + 2).toString(), Qt::ElideRight, r.width() - 12));
        p->drawText(QRect(r.x() + 6, a.bottom() + 38, r.width() - 12, 16), Qt::AlignCenter, idx.data(Qt::UserRole + 3).toString());
    }
};
