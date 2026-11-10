/*
 * The column header of Windows 7 list views outside Explorer (Control Panel lists, Task Manager...): the grey
 * ListView header cut from Windows 7 (header-list.png: normal, hot, pressed, then the sorted column's three),
 * the filter "▾" cell of the hovered column, and the small sort triangle at the top of the sorted column.
 * Explorer's own white header is header-item.png, drawn by the Dolphin fork.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <QHash>
#include <QHeaderView>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardPaths>
#include <QTreeView>

/** A picture from Windows 7's aero.msstyles kept in ~/.local/share/seven-aero (null if missing) */
inline QImage win7AeroImage(const QString &name)
{
    static QHash<QString, QImage> cache;
    auto it = cache.find(name);
    if (it == cache.end()) {
        it = cache.insert(name, QImage(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-aero/") + name)));
    }
    return *it;
}

/** Draws state `state` of a vertical strip the way uxtheme does: fixed borders, stretched middle */
inline void win7DrawStrip(QPainter *p, const QRect &r, const QImage &strip, int states, int state, int left, int right, int top, int bottom)
{
    if (strip.isNull() || r.isEmpty()) {
        return;
    }
    const int sh = strip.height() / states;
    const int sw = strip.width();
    const int y0 = sh * state;
    const int srcTop = qMin(top, sh), srcBottom = qMin(bottom, sh - srcTop); // margins in the picture
    if (top + bottom > r.height()) { // squeeze the fixed parts into a short rectangle
        top = r.height() * top / qMax(1, top + bottom);
        bottom = r.height() - top;
    }
    const int xs[4] = {r.left(), r.left() + left, r.right() + 1 - right, r.right() + 1};
    const int ys[4] = {r.top(), r.top() + top, r.bottom() + 1 - bottom, r.bottom() + 1};
    const int sx[4] = {0, left, sw - right, sw};
    const int sy[4] = {y0, y0 + srcTop, y0 + sh - srcBottom, y0 + sh};
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            const QRect dst(xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j]);
            const QRect src(sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]);
            if (dst.width() > 0 && dst.height() > 0 && src.width() > 0 && src.height() > 0) {
                p->drawImage(dst, strip, src);
            }
        }
    }
}

class Win7HeaderView : public QHeaderView
{
public:
    explicit Win7HeaderView(Qt::Orientation orientation = Qt::Horizontal, QWidget *parent = nullptr)
        : QHeaderView(orientation, parent)
    {
        setMouseTracking(true);
        setHighlightSections(false);
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setMinimumSectionSize(20);
    }

    /** Puts the Windows 7 header on a tree view, keeping its sorting and sizes */
    static Win7HeaderView *install(QTreeView *view)
    {
        auto *old = view->header();
        auto *h = new Win7HeaderView(Qt::Horizontal, view);
        h->setSectionsClickable(old->sectionsClickable());
        h->setSectionsMovable(old->sectionsMovable());
        h->setStretchLastSection(old->stretchLastSection());
        h->setSortIndicatorShown(old->isSortIndicatorShown());
        view->setHeader(h);
        return h;
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        // the empty part to the right of the last column is plain white too
        QPainter p(viewport());
        p.fillRect(viewport()->rect(), Qt::white);
        p.end();
        QHeaderView::paintEvent(e);
        // the empty part to the right of the last column looks like an unsorted column (Windows 7)
        int end = 0;
        for (int i = 0; i < count(); ++i) {
            if (!isSectionHidden(i)) {
                end = qMax(end, sectionViewportPosition(i) + sectionSize(i));
            }
        }
        if (end < viewport()->width()) {
            QPainter q(viewport());
            win7DrawStrip(&q, QRect(end, 0, viewport()->width() - end + 4, viewport()->height()), win7AeroImage(QStringLiteral("header-list.png")), 6, 0, 2, 2, 9, 14);
        }
    }

    void paintSection(QPainter *p, const QRect &rect, int logical) const override
    {
        if (!rect.isValid()) {
            return;
        }
        p->save();
        p->fillRect(rect, Qt::white);
        const bool hot = logical == m_hot && sectionsClickable();
        const bool pressed = logical == m_pressed && sectionsClickable();
        const bool sorted = isSortIndicatorShown() && sortIndicatorSection() == logical;
        // ItemsView::Header: normal, hot, pressed, then the same three for the sorted column
        const QImage item = win7AeroImage(QStringLiteral("header-list.png"));
        win7DrawStrip(p, rect, item, 6, (sorted ? 3 : 0) + (pressed ? 2 : (hot ? 1 : 0)), 2, 2, 9, 14);
        if (sorted) {
            const QImage arrows = win7AeroImage(QStringLiteral("header-sort.png"));
            const bool up = sortIndicatorOrder() == Qt::AscendingOrder;
            if (!arrows.isNull()) {
                p->drawImage(QPoint(rect.left() + (rect.width() - arrows.width()) / 2, rect.top()), arrows, QRect(0, up ? 0 : 5, arrows.width(), 5));
            }
        }
        const QVariant align = model() ? model()->headerData(logical, orientation(), Qt::TextAlignmentRole) : QVariant();
        const int flags = align.isValid() ? align.toInt() : int(Qt::AlignLeft | Qt::AlignVCenter);
        const QString text = model() ? model()->headerData(logical, orientation(), Qt::DisplayRole).toString() : QString();
        p->setPen(QColor(0x4c, 0x60, 0x7a));
        p->setFont(font());
        QRect tr = rect.adjusted(6, 1, -5, 0);
        if (hot && rect.width() > 40) {
            // Windows 7 Explorer: the hovered column shows its filter "▾" in a cell of its own on the right
            // (measured: a light line 15 px from the edge, the 7x4 arrow 5 px from the edge, 10 px down)
            const int lx = rect.right() - 15;
            QLinearGradient line(0, rect.top(), 0, rect.bottom());
            // measured on Windows 7: a darker pair of lines on the blue hot box
            line.setColorAt(0, QColor(0xc9, 0xe7, 0xf4));
            line.setColorAt(1, QColor(0xaa, 0xd9, 0xef));
            p->fillRect(QRect(lx, rect.top() + 2, 1, rect.height() - 4), line);
            const int ax = rect.right() - 11, ay = rect.top() + 10;
            for (int i = 0; i < 4; ++i) {
                p->fillRect(QRect(ax + i, ay + i, 7 - 2 * i, 1), QColor(0x1e, 0x1e, 0x28));
            }
            tr.setRight(lx - 2);
        }
        p->drawText(tr, (flags & Qt::AlignHorizontal_Mask) | Qt::AlignVCenter, fontMetrics().elidedText(text, Qt::ElideRight, tr.width()));
        p->restore();
    }

    QSize sizeHint() const override
    {
        QSize s = QHeaderView::sizeHint();
        s.setHeight(qMax(s.height(), fontMetrics().height() + 9));
        return s;
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int hot = logicalIndexAt(e->position().toPoint());
        if (hot != m_hot) {
            m_hot = hot;
            viewport()->update();
        }
        QHeaderView::mouseMoveEvent(e);
    }
    void leaveEvent(QEvent *e) override
    {
        m_hot = -1;
        viewport()->update();
        QHeaderView::leaveEvent(e);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        m_pressed = logicalIndexAt(e->position().toPoint());
        viewport()->update();
        QHeaderView::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        m_pressed = -1;
        viewport()->update();
        QHeaderView::mouseReleaseEvent(e);
    }

private:
    int m_hot = -1;
    int m_pressed = -1;
};

#include <QApplication>
#include <QTabBar>

/** Kvantum draws the right edge of the last tab one pixel outside the tab bar, so it gets cut off (photo 32).
 *  Installed on the application, this gives every tab bar the two pixels it needs when it is shown. */
class Win7TabEdgeFix : public QObject
{
public:
    static void install(QApplication *app)
    {
        app->installEventFilter(new Win7TabEdgeFix(app));
    }

protected:
    using QObject::QObject;
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::Show) {
            if (auto *bar = qobject_cast<QTabBar *>(o)) {
                bar->setMinimumWidth(bar->sizeHint().width() + 2);
            }
        }
        return false;
    }
};
