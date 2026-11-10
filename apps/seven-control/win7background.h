/*
 * Windows 7 "Desktop Background" page of Personalization (themecpl.dll): pick one picture, several (a slide show)
 * or a solid colour; position, interval and shuffle; previewed on the desktop at once, kept by "Save changes".
 * Measured on a real Windows 7; the position pictures and the window colour swatches are the ones from
 * themecpl.dll (~/.local/share/seven-aero/themecpl).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once
#include "win7i18n.h"

#include "win7chrome.h"
#include "win7states.h"

#include <QCheckBox>
#include <QCollator>
#include <QColorDialog>
#include <QComboBox>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include "win7browsefolder.h"
#include <QFileInfo>
#include <QGridLayout>
#include <QHash>
#include <QResizeEvent>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListView>
#include <QStyledItemDelegate>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStyleOptionButton>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <qdrawutil.h>

#include <functional>

namespace Win7Background
{
enum { BackgroundView = 83 };

inline QString assetPath(const QString &name)
{
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-aero/") + name);
}

/* ---------------- the desktop's wallpaper, through Plasma's scripting interface ---------------- */

struct Wallpaper {
    QString plugin;                // org.kde.image, org.kde.slideshow or org.kde.color
    QString image;                 // local path (org.kde.image)
    QStringList slidePaths;        // folders (org.kde.slideshow)
    QStringList unchecked;         // pictures left out of them
    int fillMode = 2;              // Qt Image fill mode: 2 fill, 1 fit, 0 stretch, 3 tile, 6 center
    int interval = 1800;
    bool shuffle = false;
    QColor color = Qt::black;
};

inline QString shellScript(const QString &script)
{
    QDBusInterface shell(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"), QStringLiteral("org.kde.PlasmaShell"));
    QDBusReply<QString> r = shell.call(QStringLiteral("evaluateScript"), script);
    return r.isValid() ? r.value() : QString();
}

inline Wallpaper readWallpaper()
{
    const QString out = shellScript(QStringLiteral(
        "var d = desktops()[0]; var p = d.wallpaperPlugin; d.currentConfigGroup = ['Wallpaper', p, 'General'];"
        "print(p + '\\n' + d.readConfig('Image') + '\\n' + d.readConfig('SlidePaths') + '\\n' + d.readConfig('UncheckedSlides') + '\\n'"
        " + d.readConfig('FillMode') + '\\n' + d.readConfig('SlideInterval') + '\\n' + d.readConfig('SlideshowMode') + '\\n' + d.readConfig('Color'));"));
    const QStringList l = out.split(QLatin1Char('\n'));
    Wallpaper w;
    w.plugin = l.value(0).trimmed();
    w.image = QUrl(l.value(1)).isLocalFile() ? QUrl(l.value(1)).toLocalFile() : l.value(1);
    auto list = [](const QString &s) {
        QStringList out;
        for (const QString &p : s.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QUrl u(p.trimmed());
            out << (u.isLocalFile() ? u.toLocalFile() : p.trimmed());
        }
        return out;
    };
    w.slidePaths = list(l.value(2));
    w.unchecked = list(l.value(3));
    w.fillMode = l.value(4).toInt();
    w.interval = l.value(5).toInt() > 0 ? l.value(5).toInt() : 1800;
    w.shuffle = l.value(6).toInt() == 0;
    const QStringList rgb = l.value(7).split(QLatin1Char(','));
    if (rgb.size() >= 3) {
        w.color = QColor(rgb.at(0).toInt(), rgb.at(1).toInt(), rgb.at(2).toInt());
    } else if (QColor::isValidColorName(l.value(7).trimmed())) {
        w.color = QColor::fromString(l.value(7).trimmed());
    }
    return w;
}

inline QString jsList(const QStringList &l)
{
    QStringList q;
    for (QString s : l) {
        q << QLatin1Char('\'') + s.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('\''), QStringLiteral("\\'")) + QLatin1Char('\'');
    }
    return QLatin1Char('[') + q.join(QLatin1Char(',')) + QLatin1Char(']');
}

inline QString jsStr(QString s)
{
    return QLatin1Char('\'') + s.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('\''), QStringLiteral("\\'")) + QLatin1Char('\'');
}

inline void applyWallpaper(const Wallpaper &w)
{
    QString body;
    if (w.plugin == QLatin1String("org.kde.color")) {
        body = QStringLiteral("d.writeConfig('Color', %1);").arg(jsStr(w.color.name()));
    } else if (w.plugin == QLatin1String("org.kde.slideshow")) {
        body = QStringLiteral("d.writeConfig('SlidePaths', %1); d.writeConfig('UncheckedSlides', %2); d.writeConfig('SlideInterval', %3);"
                              " d.writeConfig('SlideshowMode', %4); d.writeConfig('FillMode', %5);")
                   .arg(jsList(w.slidePaths), jsList(w.unchecked))
                   .arg(w.interval)
                   .arg(w.shuffle ? 0 : 1)
                   .arg(w.fillMode);
    } else {
        body = QStringLiteral("d.writeConfig('Image', %1); d.writeConfig('FillMode', %2); d.writeConfig('Color', '#000000');")
                   .arg(jsStr(QUrl::fromLocalFile(w.image).toString()))
                   .arg(w.fillMode);
    }
    shellScript(QStringLiteral("var all = desktops(); for (var i = 0; i < all.length; ++i) { var d = all[i]; d.wallpaperPlugin = %1;"
                               " d.currentConfigGroup = ['Wallpaper', %1, 'General']; %2 }")
                    .arg(jsStr(w.plugin), body));
}

/* ---------------- picture tiles ---------------- */

/** A picture: 88 x 55 thumbnail with a soft shadow; hover / checked frames and the check box of Windows 7 */
class PictureTile : public QWidget
{
public:
    QString file;
    std::function<void(PictureTile *, bool checkBoxClicked)> clicked;

    explicit PictureTile(const QString &path, QWidget *parent = nullptr)
        : QWidget(parent)
        , file(path)
    {
        setFixedSize(109, 71);
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);
    }
    void setChecked(bool on)
    {
        if (m_checked != on) {
            m_checked = on;
            update();
        }
    }
    bool isChecked() const
    {
        return m_checked;
    }
    void setThumbnail(const QImage &img)
    {
        m_thumb = QPixmap::fromImage(img);
        update();
    }

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::HoverEnter || e->type() == QEvent::HoverLeave) {
            update();
        } else if (e->type() == QEvent::ToolTip) {
            showInfo(static_cast<QHelpEvent *>(e)->globalPos());
            return true;
        }
        return QWidget::event(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && clicked) {
            clicked(this, checkBoxRect().adjusted(-2, -2, 2, 2).contains(e->position().toPoint()));
        }
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const bool hot = underMouse();
        const bool active = window()->isActiveWindow();
        QString state;
        if (m_checked && hot) {
            state = QStringLiteral("item-hotselected");
        } else if (m_checked) {
            state = active ? QStringLiteral("item-selected") : QStringLiteral("item-selected-inactive");
        } else if (hot) {
            state = QStringLiteral("item-hot");
        }
        if (!state.isEmpty()) {
            const QPixmap frame(Win7States::picture(state));
            if (!frame.isNull()) {
                qDrawBorderPixmap(&p, rect(), QMargins(3, 3, 3, 3), frame);
            }
        }
        // picture 8 px in, 6 px down, with a 2 px shadow to the right and below
        const QRect pic(8, 6, 88, 55);
        p.fillRect(pic.translated(2, 2), QColor(0, 0, 0, 22));
        p.fillRect(pic.translated(1, 1), QColor(0, 0, 0, 40));
        if (!m_thumb.isNull()) {
            p.drawPixmap(pic, m_thumb);
        } else {
            p.fillRect(pic, QColor(0xf0, 0xf0, 0xf0));
        }
        if (m_checked || hot) {
            QStyleOptionButton cb;
            cb.initFrom(this);
            cb.rect = checkBoxRect();
            cb.state = QStyle::State_Enabled | (m_checked ? QStyle::State_On : QStyle::State_Off);
            if (checkBoxRect().contains(mapFromGlobal(QCursor::pos()))) {
                cb.state |= QStyle::State_MouseOver;
            }
            style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &cb, &p, this);
        }
    }

private:
    QRect checkBoxRect() const
    {
        return QRect(4, 2, 13, 13);
    }
    void showInfo(const QPoint &at)
    {
        // Explorer's info tip: name, type, rating, dimensions, size
        const QFileInfo fi(file);
        QImageReader r(file);
        const QSize s = r.size();
        QString type = fi.suffix().toUpper();
        if (type == QLatin1String("JPG") || type == QLatin1String("JPEG")) {
            type = tr7("JPEG image");
        } else {
            type = tr7("%1 image").arg(type);
        }
        QString text = fi.completeBaseName() + tr7("\nItem type: ") + type + tr7("\nRating: Unrated");
        if (s.isValid()) {
            text += tr7("\nDimensions: %1 x %2").arg(s.width()).arg(s.height());
        }
        text += tr7("\nSize: ") + QLocale{}.formattedDataSize(fi.size(), 2, QLocale::DataSizeTraditionalFormat)
                                                  .replace(QStringLiteral("MiB"), tr7("MB"))
                                                  .replace(QStringLiteral("KiB"), tr7("KB"));
        QToolTip::showText(at, text, this);
    }

    bool m_checked = false;
    QPixmap m_thumb;
};

/** "Architecture (6) ─────" with the collapse triangle */
class GroupHeader : public QWidget
{
public:
    std::function<void(bool)> toggled;
    explicit GroupHeader(const QString &text, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_text(text)
    {
        setFixedHeight(21);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::HoverEnter || e->type() == QEvent::HoverLeave) {
            update();
        }
        return QWidget::event(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_open = !m_open;
            update();
            if (toggled) {
                toggled(m_open);
            }
        }
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // the triangle (open: pointing down-right, closed: pointing right) 7 px in
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x26, 0x26, 0x26));
        if (m_open) {
            p.drawPolygon(QPolygonF({QPointF(13, 3.5), QPointF(13, 10.5), QPointF(6, 10.5)}));
        } else {
            p.setBrush(Qt::NoBrush);
            p.setPen(QColor(0x8e, 0x8e, 0x8e));
            p.drawPolygon(QPolygonF({QPointF(8.5, 2.5), QPointF(12.5, 6.5), QPointF(8.5, 10.5)}));
        }
        p.setRenderHint(QPainter::Antialiasing, false);
        QFont f = font();
        f.setPixelSize(15);
        p.setFont(f);
        p.setPen(underMouse() ? QColor(0x33, 0x99, 0xff) : QColor(0x1e, 0x32, 0x87));
        const QRect tr(23, -3, width(), 20);
        p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, m_text);
        const int x = 23 + QFontMetrics(f).horizontalAdvance(m_text) + 7;
        p.setPen(QColor(0xe2, 0xe2, 0xe2));
        p.drawLine(x, 7, width() - 8, 7);
    }

private:
    QString m_text;
    bool m_open = true;
};

/** Solid colour square (60 x 60), framed while selected */
class ColorTile : public QWidget
{
public:
    QColor color;
    std::function<void(ColorTile *)> clicked;
    bool selected = false;
    explicit ColorTile(const QColor &c, QWidget *parent = nullptr)
        : QWidget(parent)
        , color(c)
    {
        setFixedSize(84, 84);
        setAttribute(Qt::WA_Hover);
    }

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::HoverEnter || e->type() == QEvent::HoverLeave) {
            update();
        }
        return QWidget::event(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && clicked) {
            clicked(this);
        }
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect sq(13, 3, 60, 60);
        QString state;
        if (selected) {
            state = underMouse() ? QStringLiteral("item-hotselected") : QStringLiteral("item-selected");
        } else if (underMouse()) {
            state = QStringLiteral("item-hot");
        }
        if (!state.isEmpty()) {
            qDrawBorderPixmap(&p, sq.adjusted(-4, -3, 4, 4), QMargins(3, 3, 3, 3), QPixmap(Win7States::picture(state)));
        }
        p.fillRect(sq, color);
    }
};

/** The tall "Picture position" combo of Windows 7 (64 x 44 pictures from themecpl.dll) */
inline QComboBox *positionCombo()
{
    auto *c = new QComboBox;
    // themecpl.dll bitmap 9: stretch, center, fit, tile, fill
    const QPixmap strip(assetPath(QStringLiteral("themecpl/2_9.png")));
    auto frame = [strip](int i) {
        return strip.isNull() ? QIcon() : QIcon(strip.copy(i * 64, 0, 64, 44));
    };
    // Windows 7 order: Fill, Fit, Stretch, Tile, Center -> Qt fill modes 2, 1, 0, 3, 6
    c->addItem(frame(4), tr7("Fill"), 2);
    c->addItem(frame(2), tr7("Fit"), 1);
    c->addItem(frame(0), tr7("Stretch"), 0);
    c->addItem(frame(3), tr7("Tile"), 3);
    c->addItem(frame(1), tr7("Center"), 6);
    c->setIconSize(QSize(64, 44));
    c->setFixedSize(170, 50);
    // Windows 7's list: the picture and the name in rows as tall as the pictures, the chosen one in blue
    struct Rows : QStyledItemDelegate {
        using QStyledItemDelegate::QStyledItemDelegate;
        QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
        {
            return QSize(168, 44);
        }
        void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &i) const override
        {
            // the row under the mouse; with the mouse outside the list, the current one (as Windows 7 opens it)
            auto *view = qobject_cast<QAbstractItemView *>(parent());
            const bool mouseIn = view && view->viewport()->rect().contains(view->viewport()->mapFromGlobal(QCursor::pos()));
            const bool sel = (o.state & QStyle::State_MouseOver) || (!mouseIn && view && view->currentIndex() == i);
            p->fillRect(o.rect, sel ? QColor(0x33, 0x99, 0xff) : QColor(Qt::white));
            const QIcon icon = i.data(Qt::DecorationRole).value<QIcon>();
            p->drawPixmap(o.rect.left() + 2, o.rect.top(), icon.pixmap(64, 44));
            p->setPen(sel ? Qt::white : Qt::black);
            p->drawText(o.rect.adjusted(74, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, i.data().toString());
        }
    };
    auto *view = new QListView;
    view->setItemDelegate(new Rows(view));
    view->setMouseTracking(true);
    view->viewport()->setAttribute(Qt::WA_Hover);
    QObject::connect(c, &QComboBox::activated, view, [view] {
        view->viewport()->update();
    });
    view->setStyleSheet(QStringLiteral("QListView { border: 1px solid #646464; background: white; outline: 0; padding: 0px; margin: 0px; }"));
    c->setView(view);
    c->setStyleSheet(QStringLiteral("QComboBox { combobox-popup: 0; }"));
    c->setMaxVisibleItems(5);
    return c;
}

/* ---------------- the page ---------------- */

inline QHash<QString, QImage> &thumbCache()
{
    static QHash<QString, QImage> cache;
    return cache;
}

class BackgroundPage : public QWidget
{
public:
    std::function<void()> back; // to Personalization

    BackgroundPage()
    {
        setObjectName(QStringLiteral("win7page"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
        m_saved = readWallpaper();

        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        // measured: 101 px from the left, the title 19 px under the address bar
        auto *body = new QWidget;
        auto *v = new QVBoxLayout(body);
        v->setContentsMargins(101, 14, 101, 0);
        v->setSpacing(0);
        auto *title = new QLabel(tr7("Choose your desktop background"));
        QFont tf = title->font();
        tf.setPixelSize(16);
        title->setFont(tf);
        title->setStyleSheet(QStringLiteral("color: #1e3287;"));
        v->addWidget(title);
        v->addSpacing(6);
        v->addWidget(new QLabel(tr7("Click a picture to make it your desktop background, or select more than one picture to create a slide show.")));
        v->addSpacing(19);

        auto *top = new QHBoxLayout;
        top->setSpacing(8);
        top->addWidget(new QLabel(tr7("Picture location:")));
        m_location = new QComboBox;
        m_location->setFixedSize(285, 23);
        m_location->addItem(tr7("Windows Desktop Backgrounds"), QStringLiteral(":windows"));
        m_location->addItem(tr7("Pictures Library"), QStringLiteral(":pictures"));
        m_location->addItem(tr7("Top Rated Photos"), QStringLiteral(":rated"));
        m_location->addItem(tr7("Solid Colors"), QStringLiteral(":colors"));
        top->addWidget(m_location);
        auto *browse = new QPushButton(tr7("Browse..."));
        browse->setFixedSize(77, 23);
        top->addWidget(browse);
        top->addStretch(1);
        m_selectAll = new QPushButton(tr7("Select all"));
        m_clearAll = new QPushButton(tr7("Clear all"));
        for (QPushButton *b : {m_selectAll, m_clearAll}) {
            b->setMinimumSize(78, 23);
            top->addWidget(b);
        }
        v->addLayout(top);
        v->addSpacing(10);

        m_scroll = new QScrollArea;
        m_scroll->setWidgetResizable(true);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setStyleSheet(QStringLiteral("QScrollArea { border: 1px solid #828790; background: #ffffff; }"));
        m_scroll->setMinimumHeight(220);
        v->addWidget(m_scroll, 1);
        v->addSpacing(13);

        // position / interval / shuffle, or the "More..." colour for solid colours
        m_bottom = new QWidget;
        auto *bh = new QHBoxLayout(m_bottom);
        bh->setContentsMargins(0, 0, 0, 0);
        bh->setSpacing(20);
        auto *posCol = new QVBoxLayout;
        posCol->setSpacing(7);
        posCol->addWidget(new QLabel(tr7("Picture position:")));
        m_position = positionCombo();
        posCol->addWidget(m_position);
        bh->addLayout(posCol);
        auto *intCol = new QVBoxLayout;
        intCol->setSpacing(7);
        intCol->addWidget(new QLabel(tr7("Change picture every:")));
        auto *intRow = new QHBoxLayout;
        intRow->setSpacing(15);
        m_interval = new QComboBox;
        m_interval->setFixedSize(150, 23);
        const QList<QPair<QString, int>> intervals{{tr7("10 seconds"), 10},   {tr7("30 seconds"), 30},  {tr7("1 minute"), 60},
                                                   {tr7("3 minutes"), 180},  {tr7("5 minutes"), 300},  {tr7("10 minutes"), 600},
                                                   {tr7("15 minutes"), 900},  {tr7("20 minutes"), 1200}, {tr7("30 minutes"), 1800},
                                                   {tr7("1 hour"), 3600},  {tr7("6 hours"), 21600}, {tr7("1 day"), 86400}};
        for (const auto &it : intervals) {
            m_interval->addItem(it.first, it.second);
        }
        intRow->addWidget(m_interval);
        m_shuffle = new QCheckBox(tr7("Shuffle"));
        intRow->addWidget(m_shuffle);
        intRow->addStretch(1);
        intCol->addLayout(intRow);
        intCol->addStretch(1);
        bh->addLayout(intCol);
        bh->addStretch(1);
        v->addWidget(m_bottom);

        m_more = new QWidget;
        auto *mh = new QHBoxLayout(m_more);
        mh->setContentsMargins(0, 0, 0, 0);
        mh->setSpacing(5);
        m_moreSwatch = new QLabel;
        m_moreSwatch->setFixedSize(26, 26);
        mh->addWidget(m_moreSwatch);
        mh->addWidget(new Link(tr7("More..."), [this] {
            const QColor c = QColorDialog::getColor(m_color, this, tr7("Color"));
            if (c.isValid()) {
                chooseColor(c);
            }
        }));
        mh->addStretch(1);
        v->addWidget(m_more);
        v->addSpacing(40);
        outer->addWidget(body, 1);

        // footer: grey band with a line, the buttons at the list's right edge
        auto *footer = new QWidget;
        footer->setObjectName(QStringLiteral("win7footer"));
        footer->setAttribute(Qt::WA_StyledBackground, true);
        footer->setStyleSheet(QStringLiteral("#win7footer { background: #f0f0f0; border-top: 1px solid #cccccc; }"));
        footer->setFixedHeight(51);
        auto *fh = new QHBoxLayout(footer);
        fh->setContentsMargins(101, 14, 101, 14);
        fh->setSpacing(12);
        fh->addStretch(1);
        auto *save = new QPushButton(tr7("Save changes"));
        save->setMinimumWidth(101);
        auto *cancel = new QPushButton(tr7("Cancel"));
        cancel->setMinimumWidth(74);
        fh->addWidget(save);
        fh->addWidget(cancel);
        outer->addWidget(footer);

        // state from the desktop
        m_color = m_saved.color;
        if (m_saved.plugin == QLatin1String("org.kde.slideshow")) {
            for (const QString &dir : m_saved.slidePaths) {
                QDirIterator it(dir, {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"), QStringLiteral("*.bmp")}, QDir::Files,
                                QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    const QString f = it.next();
                    if (!m_saved.unchecked.contains(f)) {
                        m_checked << f;
                    }
                }
            }
        } else if (m_saved.plugin == QLatin1String("org.kde.image") && !m_saved.image.isEmpty()) {
            m_checked << m_saved.image;
        }
        m_position->setCurrentIndex(qMax(0, m_position->findData(m_saved.fillMode)));
        m_interval->setCurrentIndex(qMax(0, m_interval->findData(m_saved.interval)));
        m_shuffle->setChecked(m_saved.shuffle);
        m_solid = m_saved.plugin == QLatin1String("org.kde.color");

        connect(m_location, &QComboBox::currentIndexChanged, this, [this] {
            fill();
        });
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString dir = Win7BrowseFolder::getFolder(this, tr7("Go to the folder that contains the pictures you want to use as your desktop background."), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
            if (dir.isEmpty()) {
                return;
            }
            int i = m_location->findData(dir);
            if (i < 0) {
                m_location->addItem(QDir::toNativeSeparators(dir), dir);
                i = m_location->count() - 1;
            }
            m_location->setCurrentIndex(i);
        });
        connect(m_selectAll, &QPushButton::clicked, this, [this] {
            for (PictureTile *t : std::as_const(m_tiles)) {
                if (!m_checked.contains(t->file)) {
                    m_checked << t->file;
                }
            }
            m_solid = false;
            sync(true);
        });
        connect(m_clearAll, &QPushButton::clicked, this, [this] {
            for (PictureTile *t : std::as_const(m_tiles)) {
                m_checked.removeAll(t->file);
            }
            sync(false);
        });
        connect(m_position, &QComboBox::currentIndexChanged, this, [this] {
            preview();
        });
        connect(m_interval, &QComboBox::currentIndexChanged, this, [this] {
            preview();
        });
        connect(m_shuffle, &QCheckBox::toggled, this, [this] {
            preview();
        });
        connect(save, &QPushButton::clicked, this, [this] {
            m_saved = current();
            applyWallpaper(m_saved);
            if (back) {
                back();
            }
        });
        connect(cancel, &QPushButton::clicked, this, [this] {
            applyWallpaper(m_saved);
            if (back) {
                back();
            }
        });

        m_location->setCurrentIndex(m_solid ? 3 : 0);
        fill();
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        // the picture grid follows the list's width
        QTimer::singleShot(0, this, [this] {
            if (m_location->currentData().toString() != QLatin1String(":colors") && columns() != m_columns) {
                fill();
            }
        });
    }

private:
    int columns() const
    {
        const int w = m_scroll->viewport()->width() > 100 ? m_scroll->viewport()->width() : width() - 202;
        return qMax(1, (w - 30) / 113);
    }

    QStringList images(const QString &dir, bool recursive) const
    {
        QStringList out;
        QDirIterator it(dir, {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"), QStringLiteral("*.bmp")}, QDir::Files,
                        recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
        while (it.hasNext() && out.size() < 400) {
            out << it.next();
        }
        // natural order, like Explorer: img7 ... img12
        QCollator col;
        col.setNumericMode(true);
        std::sort(out.begin(), out.end(), [&col](const QString &a, const QString &b) {
            return col.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
        });
        return out;
    }

    /** groups of pictures for the chosen location: {title, files} */
    QList<QPair<QString, QStringList>> groups() const
    {
        const QString loc = m_location->currentData().toString();
        QList<QPair<QString, QStringList>> out;
        if (loc == QLatin1String(":windows")) {
            const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wallpapers/Windows7/");
            const QList<QPair<QString, QString>> sets{{QStringLiteral("Architecture"), tr7("Architecture")},
                                                      {QStringLiteral("Characters"), tr7("Characters")},
                                                      {QStringLiteral("Landscapes"), tr7("Landscapes")},
                                                      {QStringLiteral("Nature"), tr7("Nature")},
                                                      {QStringLiteral("Scenes"), tr7("Scenes")},
                                                      {QStringLiteral("Windows"), QStringLiteral("Windows")}};
            for (const auto &s : sets) {
                const QStringList files = images(root + s.first, false);
                if (!files.isEmpty()) {
                    out.append({s.second, files});
                }
            }
        } else if (loc == QLatin1String(":pictures") || !loc.startsWith(QLatin1Char(':'))) {
            const QString dir = loc == QLatin1String(":pictures") ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) : loc;
            // grouped by folder, like the Pictures library
            QMap<QString, QStringList> byDir;
            for (const QString &f : images(dir, true)) {
                byDir[QFileInfo(f).absolutePath()] << f;
            }
            for (auto it = byDir.cbegin(); it != byDir.cend(); ++it) {
                out.append({QFileInfo(it.key()).fileName(), it.value()});
            }
        }
        return out;
    }

    void fill()
    {
        m_columns = columns();
        m_tiles.clear();
        m_colorTiles.clear();
        auto *content = new QWidget;
        content->setObjectName(QStringLiteral("win7bglist"));
        content->setStyleSheet(QStringLiteral("#win7bglist { background: #ffffff; }"));
        auto *cv = new QVBoxLayout(content);
        const bool colors = m_location->currentData().toString() == QLatin1String(":colors");
        m_bottom->setVisible(!colors);
        m_more->setVisible(colors);
        m_selectAll->setVisible(!colors);
        m_clearAll->setVisible(!colors);
        if (colors) {
            // Windows 7's 33 colours, 10 a row (measured)
            static const char *list[] = {"000000", "ffffff", "dcdcdc", "787878", "464646", "0a3b76", "4395d1", "99d9ea", "0076a3", "0d686b", "00a99d",
                                         "7accc8", "82ca9c", "74a402", "c4df9b", "84871c", "d9d56f", "fff468", "fff799", "ffc20e", "eb6119", "fbaf5d",
                                         "e57300", "c14000", "9e6b52", "8c6239", "c7b299", "b82832", "d85171", "fedfec", "563f7f", "a186be", "d9cfe5"};
            cv->setContentsMargins(0, 0, 0, 0);
            auto *grid = new QGridLayout;
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setSpacing(0);
            int i = 0;
            for (const char *hex : list) {
                auto *t = new ColorTile(QColor(QLatin1Char('#') + QLatin1String(hex)));
                t->selected = m_solid && t->color == m_color;
                t->clicked = [this](ColorTile *ct) {
                    chooseColor(ct->color);
                };
                m_colorTiles << t;
                grid->addWidget(t, i / 10, i % 10);
                ++i;
            }
            grid->setColumnStretch(10, 1);
            cv->addLayout(grid);
            cv->addStretch(1);
            updateSwatch();
        } else {
            cv->setContentsMargins(0, 11, 0, 10);
            cv->setSpacing(0);
            const auto gs = groups();
            for (const auto &g : gs) {
                auto *header = new GroupHeader(QStringLiteral("%1 (%2)").arg(g.first).arg(g.second.size()));
                cv->addWidget(header);
                auto *gridHost = new QWidget;
                auto *grid = new QGridLayout(gridHost);
                grid->setContentsMargins(14, 2, 0, 8);
                grid->setHorizontalSpacing(4);
                grid->setVerticalSpacing(4);
                const int perRow = columns();
                int i = 0;
                for (const QString &f : g.second) {
                    auto *t = new PictureTile(f);
                    t->setChecked(m_checked.contains(f) && !m_solid);
                    t->clicked = [this](PictureTile *pt, bool box) {
                        tileClicked(pt, box);
                    };
                    m_tiles << t;
                    grid->addWidget(t, i / perRow, i % perRow);
                    ++i;
                }
                grid->setColumnStretch(perRow, 1);
                cv->addWidget(gridHost);
                header->toggled = [gridHost](bool open) {
                    gridHost->setVisible(open);
                };
            }
            cv->addStretch(1);
            loadThumbnails();
        }
        m_scroll->setWidget(content);
        updateControls();
    }

    void loadThumbnails()
    {
        // a few at a time, so the page shows at once
        auto pending = std::make_shared<QList<QPointer<PictureTile>>>();
        for (PictureTile *t : std::as_const(m_tiles)) {
            pending->append(t);
        }
        auto *timer = new QTimer(m_scroll->widget() ? static_cast<QObject *>(this) : this);
        timer->setInterval(0);
        connect(timer, &QTimer::timeout, timer, [pending, timer] {
            for (int n = 0; n < 4 && !pending->isEmpty(); ++n) {
                QPointer<PictureTile> t = pending->takeFirst();
                if (!t) {
                    continue;
                }
                if (thumbCache().contains(t->file)) {
                    t->setThumbnail(thumbCache().value(t->file));
                    ++n;
                    continue;
                }
                QImageReader r(t->file);
                r.setAutoTransform(true);
                const QSize s = r.size();
                if (s.isValid()) {
                    QSize scaled = s.scaled(88 * 2, 55 * 2, Qt::KeepAspectRatioByExpanding);
                    r.setScaledSize(scaled);
                }
                const QImage img = r.read();
                if (!img.isNull()) {
                    const QImage fit = img.scaled(88, 55, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                    const QImage thumb = fit.copy((fit.width() - 88) / 2, (fit.height() - 55) / 2, 88, 55);
                    thumbCache().insert(t->file, thumb);
                    t->setThumbnail(thumb);
                }
            }
            if (pending->isEmpty()) {
                timer->deleteLater();
            }
        });
        timer->start();
    }

    void tileClicked(PictureTile *t, bool checkBox)
    {
        m_solid = false;
        if (checkBox) {
            if (m_checked.contains(t->file)) {
                m_checked.removeAll(t->file);
            } else {
                m_checked << t->file;
            }
        } else {
            m_checked = {t->file};
        }
        sync(true);
    }

    void chooseColor(const QColor &c)
    {
        m_color = c;
        m_solid = true;
        for (ColorTile *t : std::as_const(m_colorTiles)) {
            t->selected = t->color == c;
            t->update();
        }
        updateSwatch();
        preview();
    }

    void updateSwatch()
    {
        QPixmap pm(26, 26);
        pm.fill(m_color);
        m_moreSwatch->setPixmap(pm);
    }

    void sync(bool show)
    {
        for (PictureTile *t : std::as_const(m_tiles)) {
            t->setChecked(m_checked.contains(t->file));
        }
        updateControls();
        if (show) {
            preview();
        }
    }

    void updateControls()
    {
        const bool slideShow = !m_solid && m_checked.size() > 1;
        m_interval->setEnabled(slideShow);
        m_shuffle->setEnabled(slideShow);
    }

    Wallpaper current() const
    {
        Wallpaper w;
        w.fillMode = m_position->currentData().toInt();
        w.interval = m_interval->currentData().toInt();
        w.shuffle = m_shuffle->isChecked();
        if (m_solid || m_checked.isEmpty()) {
            w.plugin = QStringLiteral("org.kde.color");
            w.color = m_color;
            return w;
        }
        if (m_checked.size() == 1) {
            w.plugin = QStringLiteral("org.kde.image");
            w.image = m_checked.first();
            return w;
        }
        w.plugin = QStringLiteral("org.kde.slideshow");
        QStringList dirs;
        for (const QString &f : m_checked) {
            const QString d = QFileInfo(f).absolutePath();
            if (!dirs.contains(d)) {
                dirs << d;
            }
        }
        w.slidePaths = dirs;
        for (const QString &d : std::as_const(dirs)) {
            for (const QString &f : images(d, false)) {
                if (!m_checked.contains(f)) {
                    w.unchecked << f;
                }
            }
        }
        return w;
    }

    void preview()
    {
        // Windows 7 shows the choice on the desktop at once; Cancel puts the old one back
        applyWallpaper(current());
    }

    Wallpaper m_saved;
    QStringList m_checked;
    bool m_solid = false;
    QColor m_color = Qt::black;
    QComboBox *m_location = nullptr;
    QComboBox *m_position = nullptr;
    QComboBox *m_interval = nullptr;
    QCheckBox *m_shuffle = nullptr;
    QPushButton *m_selectAll = nullptr;
    QPushButton *m_clearAll = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_bottom = nullptr;
    QWidget *m_more = nullptr;
    QLabel *m_moreSwatch = nullptr;
    QList<PictureTile *> m_tiles;
    QList<ColorTile *> m_colorTiles;
    int m_columns = 0;
};
}
