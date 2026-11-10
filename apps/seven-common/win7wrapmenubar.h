// Windows 7 menu bars wrap onto more rows when the window gets narrow (no "»" overflow arrow).
// win7WrapMenuBar(window) turns a QMainWindow's menu bar into such a bar, keeping its menus and style sheet.
#pragma once

#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QProxyStyle>
#include <QResizeEvent>
#include <QStyleOptionMenuItem>
#include <QVBoxLayout>

/** Wrapped menu-bar rows are 17 px high on Windows 7 */
class WrapRowStyle : public QProxyStyle
{
public:
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const override
    {
        QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == CT_MenuBarItem) {
            s.setHeight(17);
        }
        return s;
    }
};

/** One row of WrapMenuBar: draws only its items (the background comes from WrapMenuBar) */
class WrapMenuRow : public QMenuBar
{
public:
    using QMenuBar::QMenuBar;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        for (QAction *a : actions()) {
            QStyleOptionMenuItem opt;
            initStyleOption(&opt, a);
            opt.rect = actionGeometry(a);
            // an app's own style sheet with item rules draws its items (transparent at rest) with its own colours
            if (styleSheet().contains(QLatin1String("::item")) || (opt.state & (QStyle::State_Selected | QStyle::State_Sunken))) {
                style()->drawControl(QStyle::CE_MenuBarItem, &opt, &p, this);
            } else {
                // at rest just the text, on the shared background
                style()->drawItemText(&p, opt.rect, Qt::AlignCenter | Qt::TextHideMnemonic | Qt::TextSingleLine,
                                      opt.palette, opt.state & QStyle::State_Enabled, opt.text, QPalette::ButtonText);
            }
        }
    }
};

/**
 * Windows 7's menu bar wraps its items onto more rows when the window gets narrow (there is no "»" overflow
 * arrow). Several QMenuBar rows, refilled on every resize; one continuous menu-bar background behind them.
 */
class WrapMenuBar : public QWidget
{
public:
    explicit WrapMenuBar(const QList<QMenu *> &menus, QWidget *parent = nullptr, const QString &styleSheet = QString())
        : QWidget(parent)
        , m_menus(menus)
        , m_qss(styleSheet)
    {
        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_probe = new QMenuBar(this);
        m_probe->setStyleSheet(m_qss);
        m_probe->hide();
        relayout(400);
    }
    bool hasHeightForWidth() const override
    {
        return true;
    }
    int heightForWidth(int w) const override
    {
        // Windows 7 (measured): one row 19 px; wrapped, 17 px a row plus the bottom line
        const int rows = qMax(1, int(rowsFor(w).size()));
        if (!m_qss.isEmpty()) {
            return rows * rowHeight(); // an app's own menu-bar look: its own row height
        }
        return rows == 1 ? rowHeight() : rows * 17 + 1;
    }
    QSize sizeHint() const override
    {
        return QSize(200, heightForWidth(width() > 0 ? width() : 400));
    }
    QSize minimumSizeHint() const override
    {
        return QSize(0, 0);
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        relayout(width());
    }
    void paintEvent(QPaintEvent *) override
    {
        // one background for all the rows, as on Windows 7: the single-row bar's top gradient, its middle
        // stretched, its bottom gradient and line
        QPainter p(this);
        const int rh = rowHeight();
        m_probe->resize(width(), rh);
        const QPixmap bar = m_probe->grab();
        const qreal dpr = bar.devicePixelRatio();
        const int top = 9, bottom = rh - top; // rows of the single bar kept at each end
        if (height() <= rh) {
            p.drawPixmap(0, 0, bar);
            return;
        }
        p.drawPixmap(QRect(0, 0, width(), top), bar, QRectF(0, 0, width() * dpr, top * dpr));
        p.drawPixmap(QRect(0, top, width(), height() - top - bottom), bar, QRectF(0, top * dpr, width() * dpr, 1 * dpr));
        p.drawPixmap(QRect(0, height() - bottom, width(), bottom), bar, QRectF(0, top * dpr, width() * dpr, bottom * dpr));
    }

private:
    int rowHeight() const
    {
        // as high as the items (with the app's own style sheet they can be taller than an empty bar)
        int h = m_probe->sizeHint().height();
        if (!m_menus.isEmpty()) {
            QStyleOptionMenuItem opt;
            opt.initFrom(m_probe);
            opt.text = m_menus.first()->title();
            opt.menuItemType = QStyleOptionMenuItem::Normal;
            const QSize sz = m_probe->fontMetrics().size(Qt::TextShowMnemonic, opt.text);
            QStyle *st = m_probe->style();
            const int item = st->sizeFromContents(QStyle::CT_MenuBarItem, &opt, sz, m_probe).height()
                + 2 * (st->pixelMetric(QStyle::PM_MenuBarVMargin, nullptr, m_probe) + st->pixelMetric(QStyle::PM_MenuBarPanelWidth, nullptr, m_probe));
            h = qMax(h, item);
        }
        return h > 0 ? h : 19;
    }
    int itemWidth(QMenu *m) const
    {
        QStyleOptionMenuItem opt;
        opt.initFrom(m_probe);
        opt.text = m->title();
        opt.menuItemType = QStyleOptionMenuItem::Normal;
        const QSize sz = m_probe->fontMetrics().size(Qt::TextShowMnemonic, m->title());
        return m_probe->style()->sizeFromContents(QStyle::CT_MenuBarItem, &opt, sz, m_probe).width();
    }
    QList<QList<QMenu *>> rowsFor(int w) const
    {
        const int spacing = m_probe->style()->pixelMetric(QStyle::PM_MenuBarItemSpacing, nullptr, m_probe);
        const int margin = m_probe->style()->pixelMetric(QStyle::PM_MenuBarHMargin, nullptr, m_probe)
            + m_probe->style()->pixelMetric(QStyle::PM_MenuBarPanelWidth, nullptr, m_probe);
        QList<QList<QMenu *>> rows;
        int x = 0;
        for (QMenu *m : m_menus) {
            const int iw = itemWidth(m);
            if (rows.isEmpty() || (x + spacing + iw + margin > w && !rows.last().isEmpty())) {
                rows.append(QList<QMenu *>());
                x = margin;
            }
            rows.last().append(m);
            x += spacing + iw;
        }
        return rows;
    }
    void relayout(int w)
    {
        const QList<QList<QMenu *>> rows = rowsFor(w);
        if (rows == m_current) {
            return;
        }
        m_current = rows;
        qDeleteAll(m_bars);
        m_bars.clear();
        for (const QList<QMenu *> &row : rows) {
            auto *bar = new WrapMenuRow(this);
            bar->setNativeMenuBar(false);
            bar->setStyleSheet(m_qss);
            if (rows.size() > 1 && m_qss.isEmpty()) {
                static WrapRowStyle *rowStyle = new WrapRowStyle;
                bar->setStyle(rowStyle);
                bar->setFixedHeight(17);
            }
            for (QMenu *m : row) {
                bar->addMenu(m);
            }
            layout()->addWidget(bar);
            m_bars.append(bar);
        }
        updateGeometry();
        if (QWidget *win = window()) {
            if (win->layout()) {
                win->layout()->invalidate();
            }
        }
    }
    QList<QMenu *> m_menus;
    QList<QList<QMenu *>> m_current;
    QList<QMenuBar *> m_bars;
    QMenuBar *m_probe = nullptr;
    QString m_qss;
};

/** Replaces @p w's menu bar by a wrapping one (call after the menus and the menu bar's style sheet are set) */
inline void win7WrapMenuBar(QMainWindow *w)
{
    QMenuBar *old = w->menuBar();
    const QString qss = old->styleSheet();
    QList<QMenu *> menus;
    for (QAction *a : old->actions()) {
        if (QMenu *m = a->menu()) {
            m->setParent(w, m->windowFlags());
            menus.append(m);
        }
    }
    w->setMenuWidget(new WrapMenuBar(menus, w, qss));
}

