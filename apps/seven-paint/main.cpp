#include "win7i18n.h"
/*
 * Paint of Windows 7 (mspaint.exe 6.1): the Scenic Ribbon (Home / View, the contextual Text tab), the application
 * menu, the work area and the status bar with the zoom slider, after ~/VMs/sevenplasma/refs/paint*.png.
 */
#include "canvas.h"
#include "ribbon.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QScreen>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QImageReader>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPageSetupDialog>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolTip>
#include <QUrl>
#include <QWidgetAction>

using namespace Paint;

namespace
{
const QColor STATUS_BG(241, 237, 237);

QSettings &settings()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-paintrc"), QSettings::IniFormat);
    return s;
}

QIcon icon(int id)
{
    return QIcon(Rib::px(id));
}

/** the 3x7 gallery of shapes with its up / down / more buttons */
class ShapeGallery : public QWidget
{
public:
    std::function<void(int)> picked;
    std::function<void(const QPoint &)> more;
    int current = -1;

    explicit ShapeGallery(const QStringList &tips, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_tips(tips)
    {
        setMouseTracking(true);
        setFixedSize(7 * 23 + 2 + 14, 62);
    }
    static int iconFor(int shape)
    {
        // the resource order differs from the gallery order
        static const int ids[23] = {40701, 40711, 40751, 40741, 40731, 40721, 40761, 40771, 40781, 40791, 40801, 40901,
                                    40891, 40911, 40921, 40841, 40851, 40861, 40831, 40811, 40821, 40871, 40881};
        return ids[shape];
    }

protected:
    QRect cell(int i) const
    {
        const int k = i - m_top * 7;
        return QRect(1 + (k % 7) * 23, 1 + (k / 7) * 20, 23, 20);
    }
    QRect scrollRect(int i) const
    {
        return QRect(width() - 14, i * 20 + 1, 13, i == 2 ? 20 : 20);
    }
    int hit(const QPoint &p) const
    {
        for (int i = m_top * 7; i < qMin(23, m_top * 7 + 21); ++i) {
            if (cell(i).contains(p)) {
                return i;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (scrollRect(i).contains(p)) {
                return 100 + i;
            }
        }
        return -1;
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect inner = rect().adjusted(0, 0, -15, -1);
        p.fillRect(inner, QColor(0xf5, 0xf9, 0xfe));
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor(0xfb, 0xfd, 0xff));
        g.setColorAt(1, QColor(0xee, 0xf4, 0xfb));
        p.fillRect(inner.adjusted(1, 1, 0, 0), g);
        p.setPen(QColor(0xb2, 0xc0, 0xd4));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        for (int i = m_top * 7; i < qMin(23, m_top * 7 + 21); ++i) {
            const QRect r = cell(i);
            if (i == m_hot || i == current) {
                Rib::paintButtonBack(p, r, i == m_hot, false, i == current);
            }
            p.drawPixmap(r.x() + 3, r.y() + 2, Rib::px(iconFor(i)));
        }
        // scroll column
        const QRect col(width() - 15, 0, 15, height());
        QLinearGradient cg(col.topLeft(), col.topRight());
        cg.setColorAt(0, QColor(0xe9, 0xf0, 0xf9));
        cg.setColorAt(1, QColor(0xdb, 0xe6, 0xf4));
        p.fillRect(col.adjusted(0, 1, -1, -1), cg);
        p.setPen(QColor(0xb2, 0xc0, 0xd4));
        p.drawLine(col.left(), 0, col.left(), height() - 1);
        for (int i = 0; i < 3; ++i) {
            const QRect r = scrollRect(i);
            const bool enabled = i == 2 || (i == 0 ? m_top > 0 : m_top * 7 + 21 < 23);
            if (m_hot == 100 + i && enabled) {
                Rib::paintButtonBack(p, r, true, false, false);
            }
            const QColor c = enabled ? QColor(0x3c, 0x3c, 0x3c) : QColor(0xa8, 0xb4, 0xc4);
            p.setRenderHint(QPainter::Antialiasing);
            const QPointF m = QRectF(r).center() + QPointF(0.5, 0.5);
            QPainterPath a;
            if (i == 0) {
                a.moveTo(m + QPointF(-3, 1.5));
                a.lineTo(m + QPointF(3, 1.5));
                a.lineTo(m + QPointF(0, -1.5));
            } else {
                a.moveTo(m + QPointF(-3, i == 2 ? 0 : -1.5));
                a.lineTo(m + QPointF(3, i == 2 ? 0 : -1.5));
                a.lineTo(m + QPointF(0, i == 2 ? 3 : 1.5));
            }
            a.closeSubpath();
            p.fillPath(a, c);
            p.setRenderHint(QPainter::Antialiasing, false);
            if (i == 2) {
                p.fillRect(QRectF(m.x() - 3, m.y() - 3, 7, 1), c);
            }
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = hit(e->position().toPoint());
        if (h != m_hot) {
            m_hot = h;
            update();
            if (h >= 0 && h < 23) {
                QToolTip::showText(e->globalPosition().toPoint() + QPoint(0, 20), m_tips.value(h), this);
            } else {
                QToolTip::hideText();
            }
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hot = -1;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const int h = hit(e->position().toPoint());
        if (h == 100 && m_top > 0) {
            --m_top;
        } else if (h == 101 && m_top * 7 + 21 < 23) {
            ++m_top;
        } else if (h == 102 && more) {
            more(mapToGlobal(QPoint(0, height())));
        } else if (h >= 0 && h < 23) {
            current = h;
            if (picked) {
                picked(h);
            }
        }
        update();
    }

private:
    QStringList m_tips;
    int m_hot = -1;
    int m_top = 0;
};

/** the two framed rows of three tool buttons in the Tools group */
class ToolGrid : public QWidget
{
public:
    std::function<void(int)> picked;
    int current = 0;

    ToolGrid(const QList<int> &icons, const QStringList &tips, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_icons(icons)
        , m_tips(tips)
    {
        setMouseTracking(true);
        setFixedSize(3 * 23 + 2, 2 * 22 + 2 + 7);
    }

protected:
    QRect cell(int i) const
    {
        return QRect(1 + (i % 3) * 23, 1 + (i / 3) * 29, 23, 22);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        for (int row = 0; row < 2; ++row) {
            const QRectF frame(0.5, row * 29 + 0.5, width() - 1, 23);
            QLinearGradient g(frame.topLeft(), frame.bottomLeft());
            g.setColorAt(0, QColor(0xfa, 0xfc, 0xfe));
            g.setColorAt(1, QColor(0xe9, 0xf1, 0xfa));
            QPainterPath path;
            path.addRoundedRect(frame, 2, 2);
            p.fillPath(path, g);
            p.setPen(QColor(0xc5, 0xd2, 0xe2));
            p.drawPath(path);
        }
        p.setRenderHint(QPainter::Antialiasing, false);
        for (int i = 0; i < 6; ++i) {
            const QRect r = cell(i);
            if (i % 3) {
                p.fillRect(QRect(r.left(), r.top() + 1, 1, r.height() - 2), QColor(0xdc, 0xe5, 0xf0));
            }
            Rib::paintButtonBack(p, r, i == m_hot, false, i == current);
            p.drawPixmap(r.x() + 4, r.y() + 3, Rib::px(m_icons[i]));
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        int h = -1;
        for (int i = 0; i < 6; ++i) {
            if (cell(i).contains(e->position().toPoint())) {
                h = i;
            }
        }
        if (h != m_hot) {
            m_hot = h;
            update();
            if (h >= 0) {
                QToolTip::showText(e->globalPosition().toPoint() + QPoint(0, 20), m_tips[h], this);
            }
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hot = -1;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        for (int i = 0; i < 6; ++i) {
            if (cell(i).contains(e->position().toPoint()) && picked) {
                picked(i);
            }
        }
    }

private:
    QList<int> m_icons;
    QStringList m_tips;
    int m_hot = -1;
};

/** Colour 1 / Colour 2: a big swatch over a two-line label, checked when it is the one the palette sets */
class ColorButton : public QWidget
{
public:
    std::function<void()> clicked;
    QColor color;
    bool checked = false;

    ColorButton(const QString &text, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_text(text)
    {
        const QFontMetrics fm(font());
        int w = 0;
        for (const QString &l : text.split(QLatin1Char('\n'))) {
            w = qMax(w, fm.horizontalAdvance(l));
        }
        setFixedSize(qMax(42, w + 10), 66);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        Rib::paintButtonBack(p, rect(), m_hot, false, checked);
        const QRect sw((width() - 30) / 2, 4, 30, 30);
        p.fillRect(sw, QColor(0x80, 0x80, 0x80));
        p.fillRect(sw.adjusted(1, 1, -1, -1), Qt::white);
        p.fillRect(sw.adjusted(2, 2, -2, -2), color);
        p.setPen(Rib::TEXT);
        const QFontMetrics fm(font());
        int y = 38;
        for (const QString &l : m_text.split(QLatin1Char('\n'))) {
            p.drawText(QRect(0, y, width(), fm.height()), Qt::AlignHCenter, l);
            y += fm.height() - 1;
        }
    }
    void enterEvent(QEnterEvent *) override
    {
        m_hot = true;
        update();
    }
    void leaveEvent(QEvent *) override
    {
        m_hot = false;
        update();
    }
    void mousePressEvent(QMouseEvent *) override
    {
        if (clicked) {
            clicked();
        }
    }

private:
    QString m_text;
    bool m_hot = false;
};

/** the 10x3 palette: 20 fixed colours and 10 custom slots */
class Palette : public QWidget
{
public:
    std::function<void(const QColor &)> picked;
    QList<QColor> colors;

    explicit Palette(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        colors = {QColor(0, 0, 0), QColor(127, 127, 127), QColor(136, 0, 21), QColor(237, 28, 36), QColor(255, 127, 39), QColor(255, 242, 0),
                  QColor(34, 177, 76), QColor(0, 162, 232), QColor(63, 72, 204), QColor(163, 73, 164), QColor(255, 255, 255), QColor(195, 195, 195),
                  QColor(185, 122, 87), QColor(255, 174, 201), QColor(255, 201, 14), QColor(239, 228, 176), QColor(181, 230, 29),
                  QColor(153, 217, 234), QColor(112, 146, 190), QColor(200, 191, 231)};
        for (int i = 0; i < 10; ++i) {
            colors.append(QColor());
        }
        setMouseTracking(true);
        setFixedSize(10 * 22, 3 * 22);
    }
    void addCustom(const QColor &c)
    {
        for (int i = 20; i < 30; ++i) {
            if (!colors[i].isValid()) {
                colors[i] = c;
                update();
                return;
            }
        }
        colors.removeAt(20);
        colors.append(c);
        update();
    }

protected:
    QRect cell(int i) const
    {
        return QRect((i % 10) * 22, (i / 10) * 22, 20, 20);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        for (int i = 0; i < 30; ++i) {
            const QRect r = cell(i);
            const bool hot = i == m_hot && colors[i].isValid();
            p.fillRect(r, hot ? QColor(0xf2, 0x94, 0x36) : QColor(160, 160, 160));
            if (colors[i].isValid()) {
                p.fillRect(r.adjusted(1, 1, -1, -1), hot ? QColor(0xff, 0xe2, 0x94) : Qt::white);
                p.fillRect(r.adjusted(2, 2, -2, -2), colors[i]);
            } else {
                p.fillRect(r.adjusted(1, 1, -1, -1), QColor(231, 241, 250));
            }
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        int h = -1;
        for (int i = 0; i < 30; ++i) {
            if (cell(i).contains(e->position().toPoint())) {
                h = i;
            }
        }
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
    void mousePressEvent(QMouseEvent *e) override
    {
        for (int i = 0; i < 30; ++i) {
            if (cell(i).contains(e->position().toPoint()) && colors[i].isValid() && picked) {
                picked(colors[i]);
            }
        }
    }

private:
    int m_hot = -1;
};

/** a drop-down panel of picture cells (brushes 3x3, sizes as a list) */
class PicturePopup : public QWidget
{
public:
    std::function<void(int)> picked;
    const QList<int> *m_lines = nullptr; // Size: thicknesses instead of pictures

    PicturePopup(const QList<QPixmap> &pics, const QStringList &tips, int cols, QSize cellSize, int current)
        : QWidget(nullptr, Qt::Popup)
        , m_pics(pics)
        , m_tips(tips)
        , m_cols(cols)
        , m_cell(cellSize)
        , m_current(current)
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_TranslucentBackground);
        setWindowFlag(Qt::NoDropShadowWindowHint);
        const int rows = (pics.size() + cols - 1) / cols;
        setFixedSize(cols * m_cell.width() + 4 + 5, rows * m_cell.height() + 4 + 5);
    }

protected:
    QRect cell(int i) const
    {
        return QRect(2 + (i % m_cols) * m_cell.width(), 2 + (i / m_cols) * m_cell.height(), m_cell.width(), m_cell.height());
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        Rib::paintPopupFrame(p, QRect(0, 0, width() - 5, height() - 5));
        for (int i = 0; i < m_pics.size(); ++i) {
            const QRect r = cell(i);
            Rib::paintButtonBack(p, r, i == m_hot, false, i == m_current);
            if (m_lines) {
                // Size: a bar across the item, drawn at 3/4 of the real thickness like Paint does
                const qreal t = m_lines->value(i) * 0.75;
                p.setRenderHint(QPainter::Antialiasing);
                p.fillRect(QRectF(r.left() + 11, r.top() + 19.5 - t / 2 + 0.5, r.width() - 22, t), QColor(30, 57, 91));
                p.setRenderHint(QPainter::Antialiasing, false);
                continue;
            }
            const QPixmap &pm = m_pics[i];
            p.drawPixmap(r.x() + (r.width() - pm.width()) / 2, r.y() + (r.height() - pm.height()) / 2, pm);
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        int h = -1;
        for (int i = 0; i < m_pics.size(); ++i) {
            if (cell(i).contains(e->position().toPoint())) {
                h = i;
            }
        }
        if (h != m_hot) {
            m_hot = h;
            update();
            if (h >= 0 && !m_tips.isEmpty()) {
                QToolTip::showText(e->globalPosition().toPoint() + QPoint(0, 20), m_tips.value(h), this);
            }
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        for (int i = 0; i < m_pics.size(); ++i) {
            if (cell(i).contains(e->position().toPoint())) {
                close();
                if (picked) {
                    picked(i);
                }
                return;
            }
        }
        if (!rect().contains(e->position().toPoint())) {
            close();
        }
    }

private:
    QList<QPixmap> m_pics;
    QStringList m_tips;
    int m_cols;
    QSize m_cell;
    int m_hot = -1, m_current;
};

/** a ribbon check box (View tab) */
class RibbonCheck : public QCheckBox
{
public:
    explicit RibbonCheck(const QString &t, QWidget *parent = nullptr)
        : QCheckBox(t, parent)
    {
        setStyleSheet(QStringLiteral("QCheckBox { color: #1e395b; spacing: 6px; }"));
        setFixedHeight(22);
    }
};

/** the Win7 application menu: commands on the left, recent pictures or a submenu on the right */
class AppMenu : public QWidget
{
public:
    struct Item {
        int icon;
        QString text;
        std::function<void()> run;
        QList<std::tuple<int, QString, QString, std::function<void()>>> sub; // icon, title, description, run
        QString subTitle;
        bool enabled = true;
        bool sepAfter = false;
    };
    QList<Item> items;
    QStringList recent;
    std::function<void(const QString &)> openRecent;

    AppMenu()
        : QWidget(nullptr, Qt::Popup | Qt::NoDropShadowWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        setAttribute(Qt::WA_DeleteOnClose);
    }
    // measured on the VM: 22 px strip with the app button, #abb8c9 line, 44 px items, separators #cfdbeb + white,
    // left column #fcfcfc up to x 216, #cfdbeb/white edge, right panel #eff5fa, bottom strip 17 px
    void popupAt(const QPoint &pos)
    {
        const QFontMetrics fm(font());
        m_left = 217;
        for (const Item &i : items) {
            m_left = qMax(m_left, 46 + fm.horizontalAdvance(QString(i.text).remove(QLatin1Char('&'))) + 28);
        }
        int h = 23;
        for (const Item &i : items) {
            h += 44 + (i.sepAfter ? 2 : 0);
        }
        m_frame = QRect(0, 0, m_left + 302, h + 18);
        resize(m_frame.width() + 4, m_frame.height() + 4);
        move(pos);
        show();
    }

protected:
    QRect itemRect(int i) const
    {
        int y = 23;
        for (int k = 0; k < i; ++k) {
            y += 44 + (items[k].sepAfter ? 2 : 0);
        }
        return QRect(1, y, m_left - 1, 44);
    }
    QRect rightRect() const
    {
        return QRect(m_left + 2, 23, m_frame.width() - m_left - 3, m_frame.height() - 23 - 18);
    }
    QRect subRect(int i) const
    {
        const QRect r = rightRect();
        return QRect(r.left() + 2, r.top() + 25 + i * 60, r.width() - 4, 58);
    }
    QRect recentRect(int i) const
    {
        const QRect r = rightRect();
        return QRect(r.left() + 2, r.top() + 25 + i * 21, r.width() - 4, 21);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect f = m_frame.adjusted(0, 0, -1, -1);
        Rib::paintPopupFrame(p, f);
        // top strip, as the tab row behind the button
        QLinearGradient tg(0, 0, 0, 10);
        tg.setColorAt(0, QColor(252, 254, 255));
        tg.setColorAt(1, QColor(220, 230, 244));
        p.fillRect(QRect(1, 0, f.width() - 1, 10), tg);
        p.fillRect(QRect(1, 10, f.width() - 1, 10), QColor(220, 231, 245));
        p.fillRect(QRect(1, 20, f.width() - 1, 1), QColor(221, 233, 247));
        p.fillRect(QRect(1, 21, f.width() - 1, 1), QColor(228, 239, 251));
        p.fillRect(QRect(0, 0, f.width() + 1, 1), QColor(132, 146, 166));
        p.fillRect(QRect(1, 22, f.width() - 1, 1), QColor(171, 184, 201));
        p.drawPixmap(0, 0, Rib::named(QStringLiteral("appbtn")));
        // columns
        p.fillRect(QRect(1, 23, m_left - 1, f.height() - 23 - 17), QColor(252, 252, 252));
        p.fillRect(QRect(m_left, 23, 1, f.height() - 23 - 17), QColor(207, 219, 235));
        p.fillRect(QRect(m_left + 1, 23, 1, f.height() - 23 - 17), Qt::white);
        const QRect right = rightRect();
        p.fillRect(right, QColor(239, 245, 250));
        // bottom strip
        p.fillRect(QRect(1, f.height() - 17, f.width() - 1, 1), QColor(201, 213, 231));
        QLinearGradient bg(0, f.height() - 16, 0, f.height());
        bg.setColorAt(0, QColor(250, 253, 255));
        bg.setColorAt(1, QColor(226, 234, 249));
        p.fillRect(QRect(1, f.height() - 16, f.width() - 1, 16), bg);
        const QFontMetrics fm(font());
        for (int i = 0; i < items.size(); ++i) {
            const QRect r = itemRect(i);
            const bool active = (i == m_hot || i == m_open) && items[i].enabled;
            if (active) {
                Rib::paintButtonBack(p, r.adjusted(1, 0, -1, 0), true, false, i == m_open && i != m_hot);
            }
            QPixmap pm = Rib::px(items[i].icon);
            if (!items[i].enabled) {
                QImage g = pm.toImage().convertToFormat(QImage::Format_ARGB32);
                for (int y = 0; y < g.height(); ++y) {
                    QRgb *l = reinterpret_cast<QRgb *>(g.scanLine(y));
                    for (int x = 0; x < g.width(); ++x) {
                        const int v = qGray(l[x]) / 2 + 110;
                        l[x] = qRgba(v, v, v, qAlpha(l[x]) / 2);
                    }
                }
                pm = QPixmap::fromImage(g);
            }
            p.drawPixmap(r.x() + 5, r.y() + 6, pm);
            p.setPen(items[i].enabled ? QColor(0x1e, 0x1e, 0x1e) : QColor(0x8d, 0x8d, 0x8d));
            p.drawText(QRect(r.x() + 45, r.y(), r.width() - 70, r.height()), Qt::AlignVCenter | Qt::TextShowMnemonic, items[i].text);
            if (!items[i].sub.isEmpty()) {
                QPainterPath a;
                const QPointF c(r.right() - 11, r.center().y() + 0.5);
                a.moveTo(c + QPointF(-1.5, -3.5));
                a.lineTo(c + QPointF(2, 0));
                a.lineTo(c + QPointF(-1.5, 3.5));
                a.closeSubpath();
                p.setRenderHint(QPainter::Antialiasing);
                p.fillPath(a, items[i].enabled ? QColor(0x3c, 0x3c, 0x3c) : QColor(0xa0, 0xa0, 0xa0));
                p.setRenderHint(QPainter::Antialiasing, false);
            }
            if (items[i].sepAfter) {
                p.fillRect(QRect(r.left() + 41, r.bottom() + 1, r.width() - 41, 1), QColor(207, 219, 235));
                p.fillRect(QRect(r.left() + 41, r.bottom() + 2, r.width() - 41, 1), Qt::white);
            }
        }
        // right panel: a submenu, or the recent pictures
        QFont bold = font();
        bold.setBold(true);
        auto header = [&](const QString &t) {
            p.setFont(bold);
            p.setPen(QColor(56, 72, 99));
            p.drawText(QRect(right.x() + 7, right.y(), right.width() - 14, 21), Qt::AlignVCenter, t);
            p.setFont(font());
            p.fillRect(QRect(right.x() + 2, right.y() + 21, right.width() - 4, 1), QColor(207, 219, 235));
            p.fillRect(QRect(right.x() + 2, right.y() + 22, right.width() - 4, 1), Qt::white);
        };
        if (m_open >= 0) {
            const Item &it = items[m_open];
            header(it.subTitle);
            for (int i = 0; i < it.sub.size(); ++i) {
                const QRect r = subRect(i);
                if (i == m_subHot) {
                    Rib::paintButtonBack(p, r, true, false, false);
                }
                p.drawPixmap(r.x() + 4, r.y() + 4, Rib::px(std::get<0>(it.sub[i])));
                p.setFont(bold);
                p.setPen(QColor(0x1e, 0x1e, 0x1e));
                p.drawText(QRect(r.x() + 44, r.y() + 3, r.width() - 50, fm.height()), Qt::TextShowMnemonic, std::get<1>(it.sub[i]));
                p.setFont(font());
                p.setPen(QColor(0x4c, 0x4c, 0x4c));
                p.drawText(QRect(r.x() + 44, r.y() + 3 + fm.height(), r.width() - 50, r.height() - fm.height() - 4), Qt::TextWordWrap, std::get<2>(it.sub[i]));
            }
        } else {
            header(tr7("Recent pictures"));
            for (int i = 0; i < recent.size(); ++i) {
                const QRect r = recentRect(i);
                if (i == m_recentHot) {
                    Rib::paintButtonBack(p, r, true, false, false);
                }
                p.setPen(QColor(0x1e, 0x1e, 0x1e));
                p.drawText(r.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::TextShowMnemonic,
                           QStringLiteral("&%1 %2").arg(i + 1).arg(fm.elidedText(QFileInfo(recent[i]).fileName(), Qt::ElideMiddle, r.width() - 30)));
            }
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPoint pt = e->position().toPoint();
        int h = -1;
        for (int i = 0; i < items.size(); ++i) {
            if (itemRect(i).contains(pt)) {
                h = i;
            }
        }
        if (h >= 0 && h != m_hot) {
            m_open = !items[h].sub.isEmpty() && items[h].enabled ? h : -1;
            m_subHot = -1;
        }
        m_hot = h;
        m_subHot = -1;
        m_recentHot = -1;
        if (m_open >= 0) {
            for (int i = 0; i < items[m_open].sub.size(); ++i) {
                if (subRect(i).contains(pt)) {
                    m_subHot = i;
                }
            }
        } else {
            for (int i = 0; i < recent.size(); ++i) {
                if (recentRect(i).contains(pt)) {
                    m_recentHot = i;
                }
            }
        }
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        const QPoint pt = e->position().toPoint();
        if (!m_frame.contains(pt) || QRect(0, 0, 56, 22).contains(pt)) {
            close();
            return;
        }
        for (int i = 0; i < items.size(); ++i) {
            if (itemRect(i).contains(pt) && items[i].enabled) {
                if (items[i].run) {
                    close();
                    items[i].run();
                } else {
                    m_open = i;
                    update();
                }
                return;
            }
        }
        if (m_open >= 0 && m_subHot >= 0) {
            auto run = std::get<3>(items[m_open].sub[m_subHot]);
            close();
            run();
            return;
        }
        if (m_open < 0 && m_recentHot >= 0 && openRecent) {
            const QString f = recent[m_recentHot];
            close();
            openRecent(f);
        }
    }

private:
    int m_hot = -1, m_open = -1, m_subHot = -1, m_recentHot = -1;
    int m_left = 217;
    QRect m_frame;
};

/** the status bar: position, selection size, picture size, file size, zoom with slider */
class StatusBar : public QWidget
{
public:
    std::function<void(qreal)> zoomTo;
    QString pos, sel, size, file;
    qreal zoom = 1;

    explicit StatusBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(25);
        setMouseTracking(true);
    }

protected:
    // geometry measured on the VM, from the right edge: label -208..-169, minus -161, channel -135..-51, plus -39
    QRect minusRect() const
    {
        return QRect(width() - 161, 6, 16, 16);
    }
    QRect plusRect() const
    {
        return QRect(width() - 39, 6, 16, 16);
    }
    QRect trackRect() const
    {
        return QRect(width() - 135, 11, 85, 4);
    }
    // the slider is linear in steps: 12.5..100 on the left half, 100..800 on the right
    int thumbX() const
    {
        const QRect t = trackRect();
        const qreal f = zoom <= 1 ? (std::log2(zoom) + 3) / 3 * 0.5 : 0.5 + (zoom - 1) / 7 * 0.5;
        return t.left() + qRound(f * (t.width() - 1));
    }
    qreal zoomAt(int x) const
    {
        const QRect t = trackRect();
        const qreal f = qBound(0.0, qreal(x - t.left()) / (t.width() - 1), 1.0);
        qreal z = f <= 0.5 ? std::pow(2.0, f / 0.5 * 3 - 3) : 1 + (f - 0.5) / 0.5 * 7;
        if (qAbs(z - 1) < 0.08) {
            z = 1;
        }
        return z;
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), STATUS_BG);
        p.fillRect(QRect(0, 0, width(), 1), QColor(145, 145, 145));
        p.fillRect(QRect(0, 1, width(), 1), Qt::white);
        const int seps[] = {155, 311, 467, 623, width() - 218};
        for (int x : seps) {
            p.fillRect(QRect(x, 2, 1, height() - 2), QColor(215, 215, 215));
        }
        p.setPen(Qt::black);
        auto panel = [&](int x, const QString &ic, const QString &t) {
            p.drawPixmap(x + 3, 5, Rib::named(ic));
            p.drawText(QRect(x + 22, 3, 130, height() - 4), Qt::AlignVCenter, t);
        };
        panel(0, QStringLiteral("st-pos"), pos);
        panel(155, QStringLiteral("st-sel"), sel);
        panel(311, QStringLiteral("st-size"), size);
        if (!file.isEmpty()) {
            panel(467, QStringLiteral("st-file"), file);
        }
        // zoom label and the trackbar, both on the control face colour
        p.fillRect(QRect(width() - 208, 3, 40, height() - 3), QColor(240, 240, 240));
        p.fillRect(QRect(width() - 164, 3, 143, height() - 3), QColor(240, 240, 240));
        p.drawText(QRect(width() - 208, 3, 38, height() - 4), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("%1%").arg(zoom * 100, 0, 'g', 4));
        auto zoomIcon = [&](const QRect &r, int base, bool hot, bool down, bool enabled) {
            const int id = !enabled ? base + 4 : down ? base + 2 : hot ? base + 1 : base;
            p.drawPixmap(r.topLeft(), Rib::named(QStringLiteral("zoom%1").arg(id)));
        };
        zoomIcon(minusRect(), 6406, m_hot == 1, m_down == 1, zoom > 0.125);
        zoomIcon(plusRect(), 6401, m_hot == 2, m_down == 2, zoom < 8);
        // channel: #b0b0b0 top and left, #e7eaea inside, #fcfcfc bottom, #fdfdfd right
        const QRect t = trackRect();
        p.fillRect(t, QColor(231, 234, 234));
        p.fillRect(QRect(t.left(), t.top(), t.width(), 1), QColor(176, 176, 176));
        p.fillRect(QRect(t.left(), t.top(), 1, t.height()), QColor(176, 176, 176));
        p.fillRect(QRect(t.left(), t.bottom(), t.width(), 1), QColor(252, 252, 252));
        p.fillRect(QRect(t.right(), t.top() + 1, 1, t.height() - 1), QColor(253, 253, 253));
        // the genuine thumb (9 x 17)
        QPixmap thumb = Rib::named(QStringLiteral("zoomthumb"));
        if (m_hot == 3 || m_down == 3) {
            QImage im = thumb.toImage();
            for (int y = 1; y < im.height() - 1; ++y) {
                for (int x = 1; x < im.width() - 1; ++x) {
                    const QColor c = im.pixelColor(x, y);
                    if (c.red() > 150) {
                        im.setPixelColor(x, y, QColor(qMax(0, c.red() - 30), qMax(0, c.green() - 10), c.blue()));
                    }
                }
            }
            thumb = QPixmap::fromImage(im);
        }
        p.drawPixmap(thumbX() - 4, 5, thumb);
        // size grip, the genuine one
        const QPixmap grip = Rib::named(QStringLiteral("grip"));
        p.drawPixmap(width() - 1 - grip.width(), height() - grip.height(), grip);
    }
    int hit(const QPoint &pt) const
    {
        if (minusRect().adjusted(-2, -2, 2, 2).contains(pt)) {
            return 1;
        }
        if (plusRect().adjusted(-2, -2, 2, 2).contains(pt)) {
            return 2;
        }
        if (QRect(thumbX() - 4, 5, 9, 17).contains(pt)) {
            return 3;
        }
        if (trackRect().adjusted(0, -5, 0, 6).contains(pt)) {
            return 4;
        }
        return 0;
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_down == 3 && zoomTo) {
            zoomTo(zoomAt(e->position().toPoint().x()));
            return;
        }
        const int h = hit(e->position().toPoint());
        if (h != m_hot) {
            m_hot = h;
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hot = 0;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        m_down = hit(e->position().toPoint());
        if (m_down == 4 && zoomTo) {
            zoomTo(zoomAt(e->position().toPoint().x()));
            m_down = 3;
        }
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        const int h = hit(e->position().toPoint());
        if (h == m_down && zoomTo) {
            if (h == 1) {
                zoomTo(-1);
            } else if (h == 2) {
                zoomTo(-2);
            }
        }
        m_down = 0;
        update();
    }

private:
    int m_hot = 0, m_down = 0;
};

/** the rulers of View > Rulers */
class Ruler : public QWidget
{
public:
    bool horizontal;
    std::function<int()> offset; // scroll position
    std::function<qreal()> zoom;
    int mark = -1;

    Ruler(bool h, QWidget *parent = nullptr)
        : QWidget(parent)
        , horizontal(h)
    {
        h ? setFixedHeight(20) : setFixedWidth(20);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0xf5, 0xf8, 0xfc));
        p.setPen(QColor(0x9f, 0xae, 0xc2));
        horizontal ? p.drawLine(0, height() - 1, width(), height() - 1) : p.drawLine(width() - 1, 0, width() - 1, height());
        const qreal z = zoom();
        const int off = offset() - (horizontal ? -20 : 0);
        int step = 100;
        for (int s : {5, 10, 20, 50, 100, 200, 500, 1000}) {
            if (s * z >= 50) {
                step = s;
                break;
            }
        }
        QFont f = font();
        f.setPixelSize(10);
        p.setFont(f);
        const int len = horizontal ? width() : height();
        for (int i = 0; i * z + Canvas::M - off < len; i += qMax(1, step / 10)) {
            const int pos = qRound(i * z) + Canvas::M - off + (horizontal ? 0 : 0);
            if (pos < 0) {
                continue;
            }
            const int tick = i % step == 0 ? 10 : i % (step / 2) == 0 ? 6 : 3;
            p.setPen(QColor(0x5a, 0x6d, 0x87));
            if (horizontal) {
                p.drawLine(pos, height() - 1 - tick, pos, height() - 1);
                if (i % step == 0) {
                    p.drawText(pos + 2, 10, QString::number(i));
                }
            } else {
                p.drawLine(width() - 1 - tick, pos, width() - 1, pos);
                if (i % step == 0) {
                    p.save();
                    p.translate(10, pos + 2);
                    p.rotate(90);
                    p.drawText(0, 0, QString::number(i));
                    p.restore();
                }
            }
        }
        if (mark >= 0) {
            p.setPen(Qt::black);
            const int m = qRound(mark * z) + Canvas::M - off;
            horizontal ? p.drawLine(m, 0, m, height()) : p.drawLine(0, m, width(), m);
        }
    }
};

class WorkArea : public QScrollArea
{
public:
    Canvas *canvas = nullptr;
    std::function<void()> resized;

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QScrollArea::resizeEvent(e);
        if (canvas) {
            canvas->updateSizePublic();
        }
        if (resized) {
            resized();
        }
    }
};
}

class PaintWindow : public QWidget
{
public:
    PaintWindow()
    {
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-paint")));
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);

        m_bar = new Rib::Bar({tr7("Home"), tr7("View")}, this);
        v->addWidget(m_bar);

        auto *mid = new QGridLayout;
        mid->setSpacing(0);
        m_area = new WorkArea;
        m_area->setFrameShape(QFrame::NoFrame);
        m_area->setWidgetResizable(false);
        m_canvas = new Canvas(m_area);
        m_area->canvas = m_canvas;
        m_area->resized = [this] {
            if (m_tools) {
                syncTools();
            }
        };
        m_area->setWidget(m_canvas);
        m_hruler = new Ruler(true);
        m_vruler = new Ruler(false);
        for (Ruler *r : {m_hruler, m_vruler}) {
            r->zoom = [this] {
                return m_canvas->zoom();
            };
            r->offset = [this, r] {
                return r->horizontal ? m_area->horizontalScrollBar()->value() : m_area->verticalScrollBar()->value();
            };
            r->hide();
        }
        QObject::connect(m_area->horizontalScrollBar(), &QScrollBar::valueChanged, m_hruler, qOverload<>(&QWidget::update));
        QObject::connect(m_area->verticalScrollBar(), &QScrollBar::valueChanged, m_vruler, qOverload<>(&QWidget::update));
        QObject::connect(m_area->verticalScrollBar(), &QScrollBar::valueChanged, m_canvas, qOverload<>(&QWidget::update));
        mid->addWidget(m_hruler, 0, 1);
        mid->addWidget(m_vruler, 1, 0);
        mid->addWidget(m_area, 1, 1);
        v->addLayout(mid, 1);

        m_status = new StatusBar(this);
        v->addWidget(m_status);

        buildHome();
        buildView();
        buildText();
        for (QWidget *page : {m_home, m_view, m_textPage}) {
            page->setMinimumWidth(page->sizeHint().width());
        }
        // everything in a group hangs from the top, as in the ribbon
        for (Rib::Group *g : std::as_const(m_groups)) {
            for (int i = 0; i < g->row->count(); ++i) {
                g->row->itemAt(i)->setAlignment(Qt::AlignTop);
            }
            g->row->invalidate();
        }
        m_bar->tabChanged = [this](int i) {
            m_home->setVisible(i == 0);
            m_view->setVisible(i == 1);
            m_textPage->setVisible(i == 2);
        };
        m_bar->appMenu = [this](const QPoint &pt) {
            showAppMenu(pt);
        };
        m_bar->setCurrent(0);

        // canvas -> ui
        m_canvas->cursorMoved = [this](const QPoint &p) {
            m_status->pos = p.x() >= 0 ? tr7("%1, %2px").arg(p.x()).arg(p.y()) : QString();
            m_status->update();
            m_hruler->mark = p.x();
            m_vruler->mark = p.y();
            if (m_hruler->isVisible()) {
                m_hruler->update();
                m_vruler->update();
            }
        };
        m_canvas->selectionChanged = [this](const QSize &s) {
            m_status->sel = s.isEmpty() ? QString() : tr7("%1 × %2px").arg(s.width()).arg(s.height());
            m_status->update();
            const bool has = m_canvas->hasSelection();
            m_cut->setEnabled(has);
            m_copy->setEnabled(has);
            m_crop->setEnabled(has);
        };
        m_canvas->imageChanged = [this] {
            m_status->size = tr7("%1 × %2px").arg(m_canvas->image().width()).arg(m_canvas->image().height());
            m_status->update();
            setModified(true);
        };
        m_canvas->colorPicked = [this](bool left, const QColor &c) {
            (left ? m_canvas->color1 : m_canvas->color2) = c;
            syncColors();
        };
        m_canvas->zoomChanged = [this] {
            m_status->zoom = m_canvas->zoom();
            m_status->update();
            m_hruler->update();
            m_vruler->update();
            m_canvas->restyleShape();
            syncTools();
        };
        m_canvas->textEditing = [this](bool on) {
            if (on) {
                QTextEdit *t = m_canvas->textEdit();
                m_textCut->setEnabled(false);
                m_textCopy->setEnabled(false);
                QObject::connect(t, &QTextEdit::copyAvailable, m_textCut, &QWidget::setEnabled);
                QObject::connect(t, &QTextEdit::copyAvailable, m_textCopy, &QWidget::setEnabled);
            }
            m_bar->setTabs(on ? QStringList{tr7("Home"), tr7("View"), tr7("Text")}
                              : QStringList{tr7("Home"), tr7("View")});
            m_bar->setContextual(on ? 2 : -1, tr7("Text Tools"));
            m_bar->setCurrent(on ? 2 : (m_bar->current() == 2 ? 0 : m_bar->current()));
        };
        m_status->zoomTo = [this](qreal z) {
            if (z == -1) {
                m_canvas->zoomOut();
            } else if (z == -2) {
                m_canvas->zoomIn();
            } else {
                m_canvas->setZoom(z);
            }
        };
        m_status->size = tr7("%1 × %2px").arg(m_canvas->image().width()).arg(m_canvas->image().height());

        // keyboard
        auto key = [this](const QKeySequence &k, std::function<void()> f) {
            auto *s = new QShortcut(k, this);
            QObject::connect(s, &QShortcut::activated, f);
        };
        key(QKeySequence::New, [this] {
            newPicture();
        });
        key(QKeySequence::Open, [this] {
            openPicture();
        });
        key(QKeySequence::Save, [this] {
            save();
        });
        key(QKeySequence(Qt::Key_F12), [this] {
            saveAs(QString());
        });
        key(QKeySequence::Print, [this] {
            print();
        });
        key(QKeySequence::Undo, [this] {
            m_canvas->undo();
        });
        key(QKeySequence(Qt::CTRL | Qt::Key_Y), [this] {
            m_canvas->redo();
        });
        key(QKeySequence::Copy, [this] {
            m_canvas->copy();
        });
        key(QKeySequence::Cut, [this] {
            m_canvas->cut();
        });
        key(QKeySequence::Paste, [this] {
            m_canvas->paste();
        });
        key(QKeySequence::SelectAll, [this] {
            m_canvas->selectAll();
        });
        key(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X), [this] {
            m_canvas->crop();
        });
        key(QKeySequence(Qt::CTRL | Qt::Key_W), [this] {
            resizeDialog();
        });
        key(QKeySequence(Qt::CTRL | Qt::Key_E), [this] {
            propertiesDialog();
        });
        key(QKeySequence(Qt::CTRL | Qt::Key_R), [this] {
            toggleRulers(!m_hruler->isVisible());
        });
        key(QKeySequence(Qt::CTRL | Qt::Key_G), [this] {
            m_grid->setChecked(!m_grid->isChecked());
        });
        key(QKeySequence(Qt::Key_F11), [this] {
            fullScreen();
        });
        key(QKeySequence(Qt::CTRL | Qt::Key_PageUp), [this] {
            m_canvas->zoomIn();
        });
        key(QKeySequence(Qt::CTRL | Qt::Key_PageDown), [this] {
            m_canvas->zoomOut();
        });
        key(QKeySequence(Qt::CTRL | Qt::Key_Plus), [this] {
            if (m_canvas->sizeAvailable() && m_canvas->sizeIdx() < 3) {
                ++m_canvas->sizeIdx();
                syncTools();
            }
        });
        key(QKeySequence(Qt::CTRL | Qt::Key_Minus), [this] {
            if (m_canvas->sizeAvailable() && m_canvas->sizeIdx() > 0) {
                --m_canvas->sizeIdx();
                syncTools();
            }
        });

        // Paste is available only while the clipboard holds a picture
        auto clip = [this, paste = m_paste] {
            const QMimeData *md = QApplication::clipboard()->mimeData();
            paste->setTopEnabled(md && md->hasImage());
            m_textPaste->setEnabled(md && (md->hasText() || md->hasImage()));
        };
        QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged, this, clip);
        clip();
        m_recent = settings().value(QStringLiteral("recent")).toStringList();
        syncTools();
        syncColors();
        m_canvas->selectionChanged(QSize());
        setModified(false);
        // Win7 opens Paint at 80 % of the screen (1536 x 864 on 1920 x 1080), the picture at 60 %
        const QSize scr = QGuiApplication::primaryScreen()->size();
        resize(settings().value(QStringLiteral("size"), QSize(scr.width() * 4 / 5 - 16, scr.height() * 4 / 5 - 38)).toSize());
    }

    /** WIN7_PAINT_TEST=<png>: draws with every tool through real mouse events and saves the window (offscreen) */
    void selfTest(const QString &out)
    {
        auto drag = [this](Qt::MouseButton b, QList<QPointF> pts) {
            auto send = [this, b](QEvent::Type t, QPointF p) {
                QMouseEvent e(t, p, m_canvas->mapToGlobal(p), b, t == QEvent::MouseButtonRelease ? Qt::NoButton : b, Qt::NoModifier);
                QApplication::sendEvent(m_canvas, &e);
            };
            send(QEvent::MouseButtonPress, pts.first());
            for (const QPointF &p : pts) {
                send(QEvent::MouseMove, p);
            }
            send(QEvent::MouseButtonRelease, pts.last());
        };
        auto line = [](QPointF a, QPointF b) {
            QList<QPointF> l;
            for (int i = 0; i <= 20; ++i) {
                l << a + (b - a) * (i / 20.0);
            }
            return l;
        };
        int y = 20;
        setTool(Tool::Pencil);
        drag(Qt::LeftButton, line({20, y}, {200, y + 30}));
        for (int k = 0; k < 9; ++k) {
            m_canvas->brush = BrushKind(k);
            m_canvas->color1 = QColor::fromHsv(k * 40, 220, 220);
            m_canvas->sizeIndex[int(Tool::Brush)] = 3;
            setTool(Tool::Brush);
            QList<QPointF> wave;
            for (int i = 0; i <= 40; ++i) {
                wave << QPointF(20 + i * 5, 80 + k * 30 + 8 * std::sin(i / 3.0));
            }
            drag(Qt::LeftButton, wave);
        }
        m_canvas->color1 = Qt::black;
        m_canvas->color2 = QColor(255, 201, 14);
        m_canvas->fill = Style::Solid;
        for (int s = 0; s < 23; ++s) {
            if (s == int(Shape::Curve) || s == int(Shape::Polygon)) {
                continue;
            }
            m_canvas->shape = Shape(s);
            setTool(Tool::Shape);
            const QPointF o(300 + (s % 6) * 70, 30 + (s / 6) * 70);
            drag(Qt::LeftButton, line(o, o + QPointF(55, 55)));
        }
        m_canvas->settle();
        m_canvas->outline = Style::Crayon;
        m_canvas->fill = Style::Watercolor;
        m_canvas->sizeIndex[int(Tool::Shape)] = 3;
        m_canvas->shape = Shape::Oval;
        setTool(Tool::Shape);
        drag(Qt::LeftButton, line({300, 330}, {460, 420}));
        m_canvas->settle();
        setTool(Tool::Fill);
        m_canvas->color1 = QColor(153, 217, 234);
        drag(Qt::LeftButton, {QPointF(900, 500)});
        setTool(Tool::Select);
        drag(Qt::LeftButton, line({20, 80}, {120, 200}));
        drag(Qt::LeftButton, line({60, 140}, {600, 480}));
        setTool(Tool::Text);
        drag(Qt::LeftButton, line({700, 60}, {950, 120}));
        if (auto *t = m_canvas->textEdit()) {
            t->setPlainText(tr7("Hello, Paint!"));
        }
        {
            // the drop-downs, rendered next to each other on white
            Rib::Menu sel;
            sel.addHeader(tr7("Selection shapes"));
            sel.add(60341, tr7("&Rectangular selection"), nullptr);
            sel.add(60357, tr7("&Free-form selection"), nullptr);
            sel.addHeader(tr7("Selection options"));
            sel.add(60365, tr7("Select &all"), nullptr);
            sel.add(60369, tr7("&Invert selection"), nullptr, false);
            sel.add(60373, tr7("&Delete"), nullptr, false);
            sel.add(0, tr7("&Transparent selection"), nullptr, true, true);
            sel.prepare();
            QList<QPixmap> pics;
            for (int i = 0; i < 9; ++i) {
                pics.append(Rib::px(brushIcon(i)));
            }
            PicturePopup br(pics, brushNames(), 4, QSize(40, 40), 0);
            static QList<int> sz{1, 3, 5, 8};
            PicturePopup si(QList<QPixmap>(4), {}, 1, QSize(128, 40), 1);
            si.m_lines = &sz;
            QImage sheet(700, 300, QImage::Format_ARGB32);
            sheet.fill(Qt::white);
            QPainter sp(&sheet);
            sp.drawPixmap(0, 0, sel.grab());
            sp.drawPixmap(300, 0, br.grab());
            sp.drawPixmap(500, 0, si.grab());
            sp.end();
            sheet.save(QString(out).replace(QStringLiteral(".png"), QStringLiteral("-menus.png")));
            AppMenu *am = buildAppMenu();
            am->recent = {tr7("/home/x/Pictures/cat.png"), tr7("/home/x/Pictures/diagram.bmp")};
            am->popupAt(QPoint(0, 0));
            am->grab().save(QString(out).replace(QStringLiteral(".png"), QStringLiteral("-app.png")));
            am->close();
        }
        QTimer::singleShot(300, this, [this, out] {
            grab().save(out);
            m_modified = false;
            qApp->quit();
        });
    }

    void showTab(int i)
    {
        m_bar->setCurrent(i);
    }

    bool load(const QString &file)
    {
        QImageReader r(file);
        r.setAutoTransform(true);
        QImage img = r.read();
        if (img.isNull()) {
            QMessageBox::warning(this, QStringLiteral("Paint"),
                                 tr7("Paint cannot read this file.\n%1\nThis is not a valid bitmap file, or its format is not currently supported.")
                                     .arg(QFileInfo(file).absoluteFilePath()));
            return false;
        }
        if (img.hasAlphaChannel()) {
            QImage flat(img.size(), QImage::Format_ARGB32_Premultiplied);
            flat.fill(Qt::white);
            QPainter(&flat).drawImage(0, 0, img);
            img = flat;
        }
        m_canvas->setImage(img, true);
        m_canvas->setZoom(1);
        m_file = QFileInfo(file).absoluteFilePath();
        addRecent(m_file);
        setModified(false);
        return true;
    }

protected:
    void closeEvent(QCloseEvent *e) override
    {
        if (!askSave()) {
            e->ignore();
            return;
        }
        if (!qEnvironmentVariableIsSet("WIN7_PAINT_TEST")) {
            settings().setValue(QStringLiteral("size"), size());
        }
        e->accept();
    }

private:
    // ------------------------------------------------------------------ ribbon pages
    Rib::Group *group(QWidget *page, const QString &label)
    {
        auto *g = new Rib::Group(label, page);
        m_groups.append(g);
        static_cast<QHBoxLayout *>(page->layout())->addWidget(g);
        return g;
    }
    QWidget *newPage()
    {
        auto *page = new QWidget(m_bar);
        auto *h = new QHBoxLayout(page);
        h->setContentsMargins(3, 0, 0, 0);
        h->setSpacing(0);
        m_bar->addPage(page);
        return page;
    }

    void buildHome()
    {
        m_home = newPage();
        auto *h = static_cast<QHBoxLayout *>(m_home->layout());

        // Clipboard
        auto *clip = group(m_home, tr7("Clipboard"));
        auto *paste = new Rib::Button(Rib::Button::Large, 60317, tr7("Paste"));
        m_paste = paste;
        paste->clicked = [this] {
            m_canvas->paste();
        };
        paste->dropdown = [this](const QPoint &pt) {
            Rib::Menu m;
            m.add(60321, tr7("&Paste"), [this] {
                m_canvas->paste();
            }, m_paste->topEnabled());
            m.add(60329, tr7("Paste &from"), [this] {
                const QString f = QFileDialog::getOpenFileName(this, tr7("Paste from"), picturesDir(), openFilter());
                if (!f.isEmpty()) {
                    QImageReader r(f);
                    r.setAutoTransform(true);
                    m_canvas->paste(r.read());
                }
            });
            m.exec(pt);
        };
        paste->setSplit(true);
        clip->row->addWidget(paste);
        auto *cc = Rib::column(clip->row);
        m_cut = new Rib::Button(Rib::Button::Small, 60333, tr7("Cut"));
        m_cut->clicked = [this] {
            m_canvas->cut();
        };
        m_copy = new Rib::Button(Rib::Button::Small, 60337, tr7("Copy"));
        m_copy->clicked = [this] {
            m_canvas->copy();
        };
        cc->addWidget(m_cut);
        cc->addWidget(m_copy);
        cc->addStretch();

        // Image
        auto *img = group(m_home, tr7("Image"));
        m_select = new Rib::Button(Rib::Button::Large, 60345, tr7("Select"));
        m_select->clicked = [this] {
            setTool(m_freeSelect ? Tool::FreeSelect : Tool::Select);
        };
        m_select->dropdown = [this](const QPoint &pt) {
            Rib::Menu m;
            m.addHeader(tr7("Selection shapes"));
            m.add(60341, tr7("&Rectangular selection"), [this] {
                m_freeSelect = false;
                setTool(Tool::Select);
            });
            m.add(60357, tr7("&Free-form selection"), [this] {
                m_freeSelect = true;
                setTool(Tool::FreeSelect);
            });
            m.addHeader(tr7("Selection options"));
            m.add(60365, tr7("Select &all"), [this] {
                m_canvas->selectAll();
            });
            m.add(60369, tr7("&Invert selection"), [this] {
                m_canvas->invertSelection();
            }, m_canvas->hasSelection());
            m.add(60373, tr7("&Delete"), [this] {
                m_canvas->deleteSelection();
            }, m_canvas->hasSelection());
            m.add(0, tr7("&Transparent selection"), [this] {
                m_canvas->transparentSelection = !m_canvas->transparentSelection;
                m_canvas->update();
            }, true, m_canvas->transparentSelection);
            m.exec(pt);
        };
        m_select->setSplit(true);
        img->row->addWidget(m_select);
        auto *ic = Rib::column(img->row);
        m_crop = new Rib::Button(Rib::Button::Small, 60377, tr7("Crop"));
        m_crop->clicked = [this] {
            m_canvas->crop();
        };
        auto *resize = new Rib::Button(Rib::Button::Small, 60381, tr7("Resize"));
        resize->clicked = [this] {
            resizeDialog();
        };
        auto *rotate = new Rib::Button(Rib::Button::Small, 60385, tr7("Rotate"));
        rotate->dropdown = [this](const QPoint &pt) {
            Rib::Menu m;
            m.add(60389, tr7("Rotate &right 90°"), [this] {
                m_canvas->transform([](const QImage &i) {
                    return i.transformed(QTransform().rotate(90));
                });
            });
            m.add(60393, tr7("Rotate &left 90°"), [this] {
                m_canvas->transform([](const QImage &i) {
                    return i.transformed(QTransform().rotate(-90));
                });
            });
            m.add(60397, tr7("Rotate &180°"), [this] {
                m_canvas->transform([](const QImage &i) {
                    return i.transformed(QTransform().rotate(180));
                });
            });
            m.add(60401, tr7("Flip &vertical"), [this] {
                m_canvas->transform([](const QImage &i) {
                    return i.flipped(Qt::Vertical);
                });
            });
            m.add(60405, tr7("Flip &horizontal"), [this] {
                m_canvas->transform([](const QImage &i) {
                    return i.flipped(Qt::Horizontal);
                });
            });
            m.exec(pt);
        };
        resize->setText(resize->text()); // keeps the size computed with the dropdown arrow
        rotate->setSplit(false);
        rotate->setText(rotate->text());
        ic->addWidget(m_crop);
        ic->addWidget(resize);
        ic->addWidget(rotate);

        // Tools
        auto *tools = group(m_home, tr7("Tools"));
        m_tools = new ToolGrid({60409, 60413, 60417, 60421, 60425, 60429},
                               {tr7("Pencil"), tr7("Fill with color"), tr7("Text"), tr7("Eraser"),
                                tr7("Color picker"), tr7("Magnifier")});
        m_tools->picked = [this](int i) {
            static const Tool map[6] = {Tool::Pencil, Tool::Fill, Tool::Text, Tool::Eraser, Tool::Picker, Tool::Magnifier};
            setTool(map[i]);
        };
        auto *tv = Rib::column(tools->row);
        tv->addSpacing(7);
        tv->addWidget(m_tools);
        tv->addStretch();

        // Brushes (its own group without a label)
        auto *brushes = group(m_home, QString());
        m_brushes = new Rib::Button(Rib::Button::Large, 40304, tr7("Brushes"));
        m_brushes->clicked = [this] {
            setTool(Tool::Brush);
        };
        m_brushes->dropdown = [this](const QPoint &pt) {
            QList<QPixmap> pics;
            for (int i = 0; i < 9; ++i) {
                pics.append(Rib::px(brushIcon(i)));
            }
            auto *pop = new PicturePopup(pics, brushNames(), 4, QSize(40, 40), m_canvas->tool == Tool::Brush ? int(m_canvas->brush) : -1);
            pop->picked = [this](int i) {
                m_canvas->brush = BrushKind(i);
                setTool(Tool::Brush);
            };
            pop->move(pt);
            pop->show();
        };
        m_brushes->setSplit(true);
        brushes->row->addWidget(m_brushes);

        // Shapes
        auto *shapes = group(m_home, tr7("Shapes"));
        m_gallery = new ShapeGallery(shapeNames());
        m_gallery->picked = [this](int i) {
            m_canvas->shape = Shape(i);
            setTool(Tool::Shape);
        };
        m_gallery->more = [this](const QPoint &pt) {
            QList<QPixmap> pics;
            for (int i = 0; i < 23; ++i) {
                pics.append(Rib::px(ShapeGallery::iconFor(i)));
            }
            auto *pop = new PicturePopup(pics, shapeNames(), 7, QSize(23, 20), m_canvas->tool == Tool::Shape ? int(m_canvas->shape) : -1);
            pop->picked = [this](int i) {
                m_gallery->current = i;
                m_canvas->shape = Shape(i);
                setTool(Tool::Shape);
            };
            pop->move(pt);
            pop->show();
        };
        auto *gv = Rib::column(shapes->row);
        gv->addSpacing(3);
        gv->addWidget(m_gallery);
        gv->addStretch();
        auto *sv = Rib::column(shapes->row);
        m_outline = new Rib::Button(Rib::Button::Small, 60441, tr7("Outline"));
        m_fill = new Rib::Button(Rib::Button::Small, 60445, tr7("Fill"));
        m_outline->dropdown = [this](const QPoint &pt) {
            styleMenu(true, pt);
        };
        m_fill->dropdown = [this](const QPoint &pt) {
            styleMenu(false, pt);
        };
        m_outline->setText(m_outline->text());
        m_fill->setText(m_fill->text());
        sv->addWidget(m_outline);
        sv->addWidget(m_fill);
        sv->addStretch();

        // Size (a group of its own)
        auto *sizeG = group(m_home, QString());
        m_size = new Rib::Button(Rib::Button::Large, 60453, tr7("Size"));
        m_size->dropdown = [this](const QPoint &pt) {
            static QList<int> sz;
            sz = m_canvas->sizes();
            QList<QPixmap> pics(sz.size());
            QStringList tips;
            for (int s : sz) {
                tips << tr7("%1px").arg(s);
            }
            auto *pop = new PicturePopup(pics, tips, 1, QSize(128, 40), m_canvas->sizeIdx());
            pop->m_lines = &sz;
            pop->picked = [this](int i) {
                m_canvas->sizeIdx() = i;
                m_canvas->restyleShape();
                syncTools();
            };
            pop->move(pt);
            pop->show();
        };
        sizeG->row->addWidget(m_size);

        // Colours
        auto *colors = group(m_home, tr7("Colors"));
        buildColors(colors, m_c1, m_c2, m_palette);
        h->addStretch();
    }

    void buildColors(Rib::Group *g, ColorButton *&c1, ColorButton *&c2, Palette *&pal)
    {
        c1 = new ColorButton(tr7("Color\n1"));
        c2 = new ColorButton(tr7("Color\n2"));
        c1->clicked = [this] {
            m_which = 1;
            syncColors();
        };
        c2->clicked = [this] {
            m_which = 2;
            syncColors();
        };
        g->row->addWidget(c1);
        g->row->addWidget(c2);
        pal = new Palette;
        pal->picked = [this](const QColor &c) {
            (m_which == 1 ? m_canvas->color1 : m_canvas->color2) = c;
            syncColors();
        };
        auto *pv = Rib::column(g->row);
        pv->addSpacing(1);
        pv->addWidget(pal);
        pv->addStretch();
        auto *edit = new Rib::Button(Rib::Button::Large, 60461, tr7("Edit\ncolors"));
        edit->clicked = [this] {
            QColorDialog dlg(m_which == 1 ? m_canvas->color1 : m_canvas->color2, this);
            dlg.setWindowTitle(tr7("Edit Colors"));
            dlg.setOption(QColorDialog::DontUseNativeDialog);
            if (dlg.exec() == QDialog::Accepted) {
                (m_which == 1 ? m_canvas->color1 : m_canvas->color2) = dlg.selectedColor();
                for (Palette *p : m_palettes) {
                    p->addCustom(dlg.selectedColor());
                }
                syncColors();
            }
        };
        g->row->addWidget(edit);
        m_colorButtons.append(std::make_pair(c1, c2));
        m_palettes.append(pal);
    }

    void buildView()
    {
        m_view = newPage();
        auto *h = static_cast<QHBoxLayout *>(m_view->layout());
        auto *zoom = group(m_view, tr7("Zoom"));
        auto *zin = new Rib::Button(Rib::Button::Large, 60469, tr7("Zoom in"));
        auto *zout = new Rib::Button(Rib::Button::Large, 60477, tr7("Zoom out"));
        auto *z100 = new Rib::Button(Rib::Button::Large, 60485, QStringLiteral("100\n%"));
        zin->clicked = [this] {
            m_canvas->zoomIn();
        };
        zout->clicked = [this] {
            m_canvas->zoomOut();
        };
        z100->clicked = [this] {
            m_canvas->setZoom(1);
        };
        m_zoomIn = zin;
        m_zoomOut = zout;
        zoom->row->addWidget(zin);
        zoom->row->addWidget(zout);
        zoom->row->addWidget(z100);

        auto *show = group(m_view, tr7("Show or hide"));
        auto *cv = Rib::column(show->row);
        m_rulers = new RibbonCheck(tr7("Rulers"));
        m_grid = new RibbonCheck(tr7("Gridlines"));
        m_statusCheck = new RibbonCheck(tr7("Status bar"));
        m_statusCheck->setChecked(true);
        QObject::connect(m_rulers, &QCheckBox::toggled, [this](bool on) {
            toggleRulers(on);
        });
        QObject::connect(m_grid, &QCheckBox::toggled, [this](bool on) {
            m_canvas->gridlines = on;
            m_canvas->update();
        });
        QObject::connect(m_statusCheck, &QCheckBox::toggled, [this](bool on) {
            m_status->setVisible(on);
        });
        cv->addSpacing(2);
        cv->addWidget(m_rulers);
        cv->addWidget(m_grid);
        cv->addWidget(m_statusCheck);
        cv->addStretch();

        auto *disp = group(m_view, tr7("Display"));
        auto *full = new Rib::Button(Rib::Button::Large, 60501, tr7("Full\nscreen"));
        full->clicked = [this] {
            fullScreen();
        };
        m_thumb = new Rib::Button(Rib::Button::Large, 60493, tr7("Thumbnail"));
        m_thumb->clicked = [this] {
            toggleThumbnail();
        };
        disp->row->addWidget(full);
        disp->row->addWidget(m_thumb);
        h->addStretch();
    }

    void buildText()
    {
        m_textPage = newPage();
        auto *h = static_cast<QHBoxLayout *>(m_textPage->layout());
        auto *clip = group(m_textPage, tr7("Clipboard"));
        auto *paste = new Rib::Button(Rib::Button::Large, 60317, tr7("Paste"));
        paste->clicked = [this] {
            if (auto *t = m_canvas->textEdit()) {
                t->paste();
            }
        };
        clip->row->addWidget(paste);
        auto *cc = Rib::column(clip->row);
        auto *cut = new Rib::Button(Rib::Button::Small, 60333, tr7("Cut"));
        auto *copy = new Rib::Button(Rib::Button::Small, 60337, tr7("Copy"));
        m_textCut = cut;
        m_textCopy = copy;
        m_textPaste = paste;
        cut->clicked = [this] {
            if (auto *t = m_canvas->textEdit()) {
                t->cut();
            }
        };
        copy->clicked = [this] {
            if (auto *t = m_canvas->textEdit()) {
                t->copy();
            }
        };
        cc->addWidget(cut);
        cc->addWidget(copy);
        cc->addStretch();

        auto *font = group(m_textPage, tr7("Font"));
        auto *fv = Rib::column(font->row);
        m_fontBox = new Rib::Combo;
        m_fontBox->fontPreview = true;
        m_fontBox->items = [] {
            return QFontDatabase::families();
        };
        m_fontBox->setText(m_canvas->textFont.family());
        m_fontBox->changed = [this](const QString &f) {
            m_canvas->textFont.setFamily(f);
            m_canvas->styleText();
        };
        m_sizeBox = new Rib::Combo;
        m_sizeBox->items = [] {
            QStringList l;
            for (int s : {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72}) {
                l << QString::number(s);
            }
            return l;
        };
        m_sizeBox->setText(QStringLiteral("11"));
        m_sizeBox->changed = [this](const QString &t) {
            const double s = t.toDouble();
            if (s > 0) {
                m_canvas->textFont.setPointSizeF(s);
                m_canvas->styleText();
            }
        };
        auto *stylesFrame = new Rib::FrameRow;
        auto *styles = stylesFrame->row;
        auto styleBtn = [&](const QString &glyph, std::function<void(QFont &, bool)> apply) {
            auto *b = new Rib::Button(Rib::Button::IconOnly, 0, QString());
            QPixmap pm(16, 16);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            QFont f(QStringLiteral("Times New Roman"));
            f.setPixelSize(15);
            f.setBold(glyph != QLatin1String("abc"));
            apply(f, true);
            p.setFont(f);
            p.setPen(QColor(0x1e, 0x39, 0x5b));
            p.drawText(pm.rect(), Qt::AlignCenter, glyph);
            p.end();
            b->setPixmap(pm);
            b->clicked = [this, b, apply] {
                b->setChecked(!b->isChecked());
                apply(m_canvas->textFont, b->isChecked());
                m_canvas->styleText();
            };
            styles->addWidget(b);
        };
        styleBtn(tr7("B"), [](QFont &f, bool on) {
            f.setBold(on);
        });
        styleBtn(tr7("I"), [](QFont &f, bool on) {
            f.setItalic(on);
        });
        styleBtn(tr7("U"), [](QFont &f, bool on) {
            f.setUnderline(on);
        });
        styleBtn(QStringLiteral("abc"), [](QFont &f, bool on) {
            f.setStrikeOut(on);
        });
        // fixed places measured on the VM: font 139 x 22 at the top, size 42 wide and the B I U abc frame below
        auto *box = new QWidget;
        box->setFixedSize(140, 60);
        m_fontBox->setParent(box);
        m_fontBox->setGeometry(0, 8, 139, 22);
        m_sizeBox->setGeometry(0, 37, 43, 22);
        m_sizeBox->setParent(box);

        stylesFrame->setParent(box);
        stylesFrame->setGeometry(46, 37, 94, 24);
        fv->addWidget(box);
        auto *bg = group(m_textPage, tr7("Background"));
        auto *bv = Rib::column(bg->row);
        m_opaque = new Rib::Button(Rib::Button::Small, 60509, tr7("Opaque"));
        m_transparent = new Rib::Button(Rib::Button::Small, 60505, tr7("Transparent"));
        m_opaque->clicked = [this] {
            m_canvas->textOpaque = true;
            m_canvas->styleText();
            syncTools();
        };
        m_transparent->clicked = [this] {
            m_canvas->textOpaque = false;
            m_canvas->styleText();
            syncTools();
        };
        bv->addWidget(m_opaque);
        bv->addWidget(m_transparent);
        bv->addStretch();

        auto *colors = group(m_textPage, tr7("Colors"));
        ColorButton *c1, *c2;
        Palette *pal;
        buildColors(colors, c1, c2, pal);
        h->addStretch();
    }

    static int brushIcon(int kind)
    {
        static const int res[9] = {0, 1, 2, 3, 5, 6, 7, 8, 4};
        return 40304 + res[kind] * 10;
    }
    static QStringList brushNames()
    {
        return {tr7("Brush"), tr7("Calligraphy brush 1"), tr7("Calligraphy brush 2"), tr7("Airbrush"),
                tr7("Oil brush"), tr7("Crayon"), tr7("Marker"), tr7("Natural pencil"),
                tr7("Watercolor brush")};
    }
    static QStringList shapeNames()
    {
        return {tr7("Line"), tr7("Curve"), tr7("Oval"), tr7("Rectangle"),
                tr7("Rounded rectangle"), tr7("Polygon"), tr7("Triangle"),
                tr7("Right triangle"), tr7("Diamond"), tr7("Pentagon"), tr7("Hexagon"),
                tr7("Right arrow"), tr7("Left arrow"), tr7("Up arrow"), tr7("Down arrow"),
                tr7("Four-point star"), tr7("Five-point star"), tr7("Six-point star"),
                tr7("Rounded rectangular callout"), tr7("Oval callout"), tr7("Cloud callout"), tr7("Heart"),
                tr7("Lightning")};
    }

    void styleMenu(bool outline, const QPoint &pt)
    {
        Rib::Menu m;
        const Style cur = outline ? m_canvas->outline : m_canvas->fill;
        struct Entry {
            Style s;
            int icon;
            QString text;
        };
        const QList<Entry> entries = {
            {Style::None, outline ? 41001 : 41021, outline ? tr7("&No outline") : tr7("&No fill")},
            {Style::Solid, outline ? 41011 : 41031, tr7("&Solid color")},
            {Style::Crayon, 41051, tr7("&Crayon")},
            {Style::Marker, 41061, tr7("&Marker")},
            {Style::Oil, 41041, tr7("&Oil")},
            {Style::NaturalPencil, 41071, tr7("&Natural pencil")},
            {Style::Watercolor, 41081, tr7("&Watercolor")},
        };
        for (const Entry &e : entries) {
            m.add(e.icon, e.text, [this, outline, e] {
                (outline ? m_canvas->outline : m_canvas->fill) = e.s;
                m_canvas->restyleShape();
            }, true, cur == e.s);
        }
        m.exec(pt);
    }

    // ------------------------------------------------------------------ state sync
    void setTool(Tool t)
    {
        m_canvas->setTool(t);
        syncTools();
    }
    void syncTools()
    {
        const Tool t = m_canvas->tool;
        static const Tool map[6] = {Tool::Pencil, Tool::Fill, Tool::Text, Tool::Eraser, Tool::Picker, Tool::Magnifier};
        m_tools->current = -1;
        for (int i = 0; i < 6; ++i) {
            if (map[i] == t) {
                m_tools->current = i;
            }
        }
        m_tools->update();
        m_select->setChecked(t == Tool::Select || t == Tool::FreeSelect);
        m_select->setIcon(m_freeSelect ? 60361 : 60345);
        m_brushes->setChecked(t == Tool::Brush);
        m_brushes->setIcon(brushIcon(int(m_canvas->brush)));
        m_gallery->current = t == Tool::Shape ? int(m_canvas->shape) : -1;
        m_gallery->update();
        const bool shapeTool = t == Tool::Shape;
        m_outline->setEnabled(shapeTool);
        m_fill->setEnabled(shapeTool && !isOpenShape(m_canvas->shape));
        m_size->setEnabled(m_canvas->sizeAvailable());
        m_zoomIn->setEnabled(m_canvas->zoom() < 8);
        m_zoomOut->setEnabled(m_canvas->zoom() > 0.125);
        m_opaque->setChecked(m_canvas->textOpaque);
        m_transparent->setChecked(!m_canvas->textOpaque);
        const QSizeF pic = QSizeF(m_canvas->image().size()) * m_canvas->zoom() + QSizeF(2 * Canvas::M + 8, 2 * Canvas::M + 8);
        const QSize vp = m_area->viewport()->size();
        m_thumb->setEnabled(pic.width() > vp.width() || pic.height() > vp.height() || m_thumbWin);
        m_canvas->update();
    }
    void syncColors()
    {
        for (auto [c1, c2] : m_colorButtons) {
            c1->color = m_canvas->color1;
            c2->color = m_canvas->color2;
            c1->checked = m_which == 1;
            c2->checked = m_which == 2;
            c1->update();
            c2->update();
        }
        m_canvas->restyleShape();
    }

    // ------------------------------------------------------------------ files
    static QSize defaultSize()
    {
        const QSize scr = QGuiApplication::primaryScreen()->size();
        return QSize(scr.width() * 3 / 5, scr.height() * 3 / 5);
    }
    static QString picturesDir()
    {
        return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    static QString openFilter()
    {
        return tr7("All Picture Files (*.bmp *.dib *.jpg *.jpeg *.jpe *.jfif *.gif *.tif *.tiff *.png *.ico *.webp);;Bitmap Files (*.bmp *.dib);;JPEG (*.jpg *.jpeg *.jpe *.jfif);;GIF (*.gif);;TIFF (*.tif *.tiff);;PNG (*.png);;ICO (*.ico);;All Files (*)");
    }
    QString title() const
    {
        return m_file.isEmpty() ? tr7("Untitled") : QFileInfo(m_file).fileName();
    }
    void setModified(bool on)
    {
        m_modified = on;
        setWindowTitle(title() + QStringLiteral(" - Paint"));
        if (!m_file.isEmpty() && QFileInfo::exists(m_file)) {
            m_status->file = tr7("Size: %1").arg(QLocale().formattedDataSize(QFileInfo(m_file).size(), 1, QLocale::DataSizeTraditionalFormat));
        } else {
            m_status->file.clear();
        }
        m_status->update();
    }
    bool askSave()
    {
        if (!m_modified) {
            return true;
        }
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("Paint"));
        box.setText(tr7("Do you want to save changes to %1?").arg(title()));
        auto *save = box.addButton(tr7("Save"), QMessageBox::AcceptRole);
        auto *no = box.addButton(tr7("Don't Save"), QMessageBox::DestructiveRole);
        box.addButton(tr7("Cancel"), QMessageBox::RejectRole);
        box.setDefaultButton(save);
        box.exec();
        if (box.clickedButton() == save) {
            return this->save();
        }
        return box.clickedButton() == no;
    }
    void newPicture()
    {
        if (!askSave()) {
            return;
        }
        QImage img(defaultSize(), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);
        m_canvas->setImage(img, true);
        m_canvas->setZoom(1);
        m_file.clear();
        setModified(false);
    }
    void openPicture()
    {
        if (!askSave()) {
            return;
        }
        const QString f = QFileDialog::getOpenFileName(this, tr7("Open"), m_file.isEmpty() ? picturesDir() : QFileInfo(m_file).path(), openFilter());
        if (!f.isEmpty()) {
            load(f);
        }
    }
    bool save()
    {
        if (m_file.isEmpty()) {
            return saveAs(QString());
        }
        return writeTo(m_file);
    }
    bool saveAs(const QString &format)
    {
        const QStringList filters = {tr7("Monochrome Bitmap (*.bmp *.dib)"),
                                     tr7("16 Color Bitmap (*.bmp *.dib)"),
                                     tr7("256 Color Bitmap (*.bmp *.dib)"),
                                     tr7("24-bit Bitmap (*.bmp *.dib)"),
                                     QStringLiteral("JPEG (*.jpg *.jpeg *.jpe *.jfif)"),
                                     QStringLiteral("GIF (*.gif)"),
                                     QStringLiteral("TIFF (*.tif *.tiff)"),
                                     QStringLiteral("PNG (*.png)")};
        QString sel = filters[7];
        if (format == QLatin1String("jpg")) {
            sel = filters[4];
        } else if (format == QLatin1String("bmp")) {
            sel = filters[3];
        } else if (format == QLatin1String("gif")) {
            sel = filters[5];
        }
        QString base = m_file.isEmpty() ? picturesDir() + tr7("/Untitled") : QFileInfo(m_file).path() + QLatin1Char('/') + QFileInfo(m_file).completeBaseName();
        QString f = QFileDialog::getSaveFileName(this, tr7("Save As"), base, filters.join(QStringLiteral(";;")), &sel);
        if (f.isEmpty()) {
            return false;
        }
        if (QFileInfo(f).suffix().isEmpty()) {
            const QString ext = sel.contains(QLatin1String("JPEG")) ? QStringLiteral("jpg")
                : sel.contains(QLatin1String("GIF"))                 ? QStringLiteral("gif")
                : sel.contains(QLatin1String("TIFF"))                ? QStringLiteral("tif")
                : sel.contains(QLatin1String("PNG"))                 ? QStringLiteral("png")
                                                                     : QStringLiteral("bmp");
            f += QLatin1Char('.') + ext;
        }
        m_saveDepth = sel.startsWith(tr7("Monochrome")) ? 1 : sel.startsWith(QLatin1String("16-")) ? 4 : sel.startsWith(QLatin1String("256")) ? 8 : 24;
        if (!writeTo(f)) {
            return false;
        }
        m_file = QFileInfo(f).absoluteFilePath();
        addRecent(m_file);
        setModified(false);
        return true;
    }
    bool writeTo(const QString &f)
    {
        QImage img = m_canvas->flattened().convertToFormat(QImage::Format_RGB32);
        const QString ext = QFileInfo(f).suffix().toLower();
        if ((ext == QLatin1String("bmp") || ext == QLatin1String("dib")) && m_saveDepth < 24) {
            img = img.convertToFormat(m_saveDepth == 1 ? QImage::Format_Mono : QImage::Format_Indexed8);
        }
        QImageWriter w(f, ext == QLatin1String("dib") ? QByteArray("bmp") : ext == QLatin1String("jpe") || ext == QLatin1String("jfif") ? QByteArray("jpg") : QByteArray());
        if (ext.startsWith(QLatin1String("jp"))) {
            w.setQuality(90);
        }
        if (!w.write(img)) {
            QMessageBox::warning(this, QStringLiteral("Paint"), tr7("%1 could not be saved.\n%2").arg(f, w.errorString()));
            return false;
        }
        m_modified = false;
        setModified(false);
        return true;
    }
    void addRecent(const QString &f)
    {
        m_recent.removeAll(f);
        m_recent.prepend(f);
        while (m_recent.size() > 9) {
            m_recent.removeLast();
        }
        settings().setValue(QStringLiteral("recent"), m_recent);
    }

    // ------------------------------------------------------------------ print, wallpaper, mail
    void print(int what = 0)
    {
        static QPrinter printer(QPrinter::HighResolution);
        const QImage img = m_canvas->flattened();
        auto render = [img](QPrinter *pr) {
            QPainter p(pr);
            const QRect page = pr->pageLayout().paintRectPixels(pr->resolution());
            QSizeF s = QSizeF(img.size()) * (pr->resolution() / 96.0);
            if (s.width() > page.width() || s.height() > page.height()) {
                s.scale(page.size(), Qt::KeepAspectRatio);
            }
            p.drawImage(QRectF(QPointF(0, 0), s), img);
        };
        if (what == 1) {
            QPageSetupDialog d(&printer, this);
            d.setWindowTitle(tr7("Page setup"));
            d.exec();
        } else if (what == 2) {
            QPrintPreviewDialog d(&printer, this);
            d.setWindowTitle(tr7("Print preview"));
            QObject::connect(&d, &QPrintPreviewDialog::paintRequested, render);
            d.exec();
        } else {
            QPrintDialog d(&printer, this);
            d.setWindowTitle(tr7("Print"));
            if (d.exec() == QDialog::Accepted) {
                render(&printer);
            }
        }
    }
    void setWallpaper(const QString &mode)
    {
        if (m_file.isEmpty() || m_modified) {
            QMessageBox::information(this, QStringLiteral("Paint"), tr7("Before you can set the picture as your desktop background, you must save it."));
            if (!save()) {
                return;
            }
        }
        // FillMode: 2 = PreserveAspectCrop (fill), 3 = Tile, 6 = Pad (center)
        const int fillMode = mode == QLatin1String("tile") ? 3 : mode == QLatin1String("center") ? 6 : 2;
        const QString js = QStringLiteral(
                               "var all = desktops(); for (var i = 0; i < all.length; ++i) { var d = all[i]; d.wallpaperPlugin = 'org.kde.image';"
                               " d.currentConfigGroup = ['Wallpaper', 'org.kde.image', 'General']; d.writeConfig('Image', 'file://%1');"
                               " d.writeConfig('FillMode', %2); }")
                               .arg(m_file)
                               .arg(fillMode);
        QProcess::startDetached(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
                                                           QStringLiteral("org.kde.PlasmaShell.evaluateScript"), js});
    }
    void sendMail()
    {
        QString f = m_file;
        if (f.isEmpty() || m_modified) {
            f = QDir::tempPath() + QLatin1Char('/') + (m_file.isEmpty() ? tr7("Untitled.png") : QFileInfo(m_file).completeBaseName() + QStringLiteral(".png"));
            m_canvas->flattened().save(f);
        }
        QProcess::startDetached(QStringLiteral("xdg-email"), {QStringLiteral("--attach"), f});
    }

    // ------------------------------------------------------------------ app menu
    void showAppMenu(const QPoint &pt)
    {
        AppMenu *m = buildAppMenu();
        QObject::connect(m, &QObject::destroyed, m_bar, [this] {
            m_bar->appMenuClosed();
        });
        m->popupAt(pt - QPoint(0, 21));
    }
    AppMenu *buildAppMenu()
    {
        auto *m = new AppMenu;
        m->recent = m_recent;
        m->openRecent = [this](const QString &f) {
            if (askSave()) {
                load(f);
            }
        };
        using Sub = QList<std::tuple<int, QString, QString, std::function<void()>>>;
        const bool saved = !m_file.isEmpty();
        m->items = {
            {60005, tr7("&New"), [this] { newPicture(); }, {}, {}},
            {60013, tr7("&Open"), [this] { openPicture(); }, {}, {}},
            {60021, tr7("&Save"), [this] { save(); }, {}, {}},
            {60029, tr7("Save &as"), nullptr,
             Sub{{60053, tr7("&PNG picture"), tr7("Save a photograph or drawing with high quality and use it on your computer or on the web."), [this] { saveAs(QStringLiteral("png")); }},
                 {60061, tr7("&JPEG picture"), tr7("Save a photograph with good quality and use it on your computer, in e-mail or on the web."), [this] { saveAs(QStringLiteral("jpg")); }},
                 {60069, tr7("&BMP picture"), tr7("Save any kind of picture with high quality and use it on your computer."), [this] { saveAs(QStringLiteral("bmp")); }},
                 {60077, tr7("&GIF picture"), tr7("Save a simple drawing and use it in e-mail or on a website."), [this] { saveAs(QStringLiteral("gif")); }},
                 {60085, tr7("&Other formats"), tr7("Open the Save As dialog box to select from all possible file types."), [this] { saveAs(QString()); }}},
             tr7("Save a copy of the document"), true, true},
            {60093, tr7("&Print"), nullptr,
             Sub{{60101, tr7("&Print"), tr7("Select a printer, number of copies, and other printing options before printing."), [this] { print(0); }},
                 {60109, tr7("Page set&up"), tr7("Change the layout of the picture."), [this] { print(1); }},
                 {60125, tr7("Print pre&view"), tr7("Preview and make changes before printing."), [this] { print(2); }}},
             tr7("Preview and print the document"), true, false},
            {60133, tr7("Fro&m scanner or camera"), [] {}, {}, {}, false, false},
            {60141, tr7("Sen&d in e-mail"), [this] { sendMail(); }, {}, {}, true, true},
            {60149, tr7("Set as desktop &background"), nullptr,
             Sub{{60189, tr7("&Fill"), QString(), [this] { setWallpaper(QStringLiteral("fill")); }},
                 {60173, tr7("&Tile"), QString(), [this] { setWallpaper(QStringLiteral("tile")); }},
                 {60181, tr7("&Center"), QString(), [this] { setWallpaper(QStringLiteral("center")); }}},
             tr7("Set as desktop background"), saved, true},
            {60197, tr7("Prop&erties"), [this] { propertiesDialog(); }, {}, {}, true, true},
            {60205, tr7("Abou&t Paint"), [this] { about(); }, {}, {}, true, true},
            {60213, tr7("E&xit"), [this] { close(); }, {}, {}},
        };
        return m;
    }

    // ------------------------------------------------------------------ dialogs
    void resizeDialog()
    {
        QDialog d(this);
        d.setWindowTitle(tr7("Resize and Skew"));
        auto *v = new QVBoxLayout(&d);
        auto *rs = new QGroupBox(tr7("Resize", "dialog"));
        auto *rg = new QGridLayout(rs);
        auto *by = new QHBoxLayout;
        auto *pct = new QRadioButton(tr7("P&ercentage"));
        auto *pix = new QRadioButton(tr7("Pi&xels"));
        pct->setChecked(true);
        by->addWidget(new QLabel(tr7("By:")));
        by->addWidget(pct);
        by->addWidget(pix);
        by->addStretch();
        rg->addLayout(by, 0, 0, 1, 3);
        const QSize src = m_canvas->hasSelection() ? QSize() : m_canvas->image().size();
        auto *hw = new QSpinBox;
        auto *vw = new QSpinBox;
        for (QSpinBox *s : {hw, vw}) {
            s->setRange(1, 50000);
            s->setValue(100);
            s->setButtonSymbols(QAbstractSpinBox::NoButtons);
            s->setFixedWidth(60);
        }
        auto pic = [](const QString &n) {
            auto *l = new QLabel;
            l->setPixmap(Rib::named(n));
            return l;
        };
        rg->addWidget(pic(QStringLiteral("rsz-h")), 1, 0);
        rg->addWidget(new QLabel(tr7("&Horizontal:")), 1, 1);
        rg->addWidget(hw, 1, 2);
        rg->addWidget(pic(QStringLiteral("rsz-v")), 2, 0);
        rg->addWidget(new QLabel(tr7("&Vertical:")), 2, 1);
        rg->addWidget(vw, 2, 2);
        auto *keep = new QCheckBox(tr7("&Maintain aspect ratio"));
        keep->setChecked(true);
        rg->addWidget(keep, 3, 0, 1, 3);
        v->addWidget(rs);
        auto *sk = new QGroupBox(tr7("Skew (Degrees)"));
        auto *sg = new QGridLayout(sk);
        auto *sh = new QSpinBox;
        auto *sv = new QSpinBox;
        for (QSpinBox *s : {sh, sv}) {
            s->setRange(-89, 89);
            s->setButtonSymbols(QAbstractSpinBox::NoButtons);
            s->setFixedWidth(60);
        }
        sg->addWidget(pic(QStringLiteral("skw-h")), 0, 0);
        sg->addWidget(new QLabel(tr7("H&orizontal:")), 0, 1);
        sg->addWidget(sh, 0, 2);
        sg->addWidget(pic(QStringLiteral("skw-v")), 1, 0);
        sg->addWidget(new QLabel(tr7("V&ertical:")), 1, 1);
        sg->addWidget(sv, 1, 2);
        v->addWidget(sk);
        auto *bb = new QDialogButtonBox;
        bb->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
        bb->addButton(tr7("Cancel"), QDialogButtonBox::RejectRole);
        QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        v->addWidget(bb);
        QSize base = src.isValid() ? src : QSize(100, 100);
        bool guard = false;
        QObject::connect(pct, &QRadioButton::toggled, [&](bool on) {
            guard = true;
            hw->setValue(on ? 100 : base.width());
            vw->setValue(on ? 100 : base.height());
            guard = false;
        });
        QObject::connect(hw, &QSpinBox::valueChanged, [&](int x) {
            if (guard || !keep->isChecked()) {
                return;
            }
            guard = true;
            vw->setValue(pct->isChecked() ? x : qMax(1, qRound(x * qreal(base.height()) / base.width())));
            guard = false;
        });
        QObject::connect(vw, &QSpinBox::valueChanged, [&](int y) {
            if (guard || !keep->isChecked()) {
                return;
            }
            guard = true;
            hw->setValue(pct->isChecked() ? y : qMax(1, qRound(y * qreal(base.width()) / base.height())));
            guard = false;
        });
        d.setFixedSize(d.sizeHint());
        if (d.exec() != QDialog::Accepted) {
            return;
        }
        const bool percent = pct->isChecked();
        const int a = hw->value(), b = vw->value(), kx = sh->value(), ky = sv->value();
        const QColor bgc = m_canvas->color2;
        m_canvas->transform([=](const QImage &i) {
            QImage r = i;
            const QSize ns = percent ? QSize(qMax(1, i.width() * a / 100), qMax(1, i.height() * b / 100)) : QSize(a, b);
            if (ns != i.size()) {
                r = i.scaled(ns, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            if (kx || ky) {
                const QTransform t(1, std::tan(qDegreesToRadians(qreal(ky))), std::tan(qDegreesToRadians(qreal(kx))), 1, 0, 0);
                const QRect bounds = t.mapRect(QRectF(r.rect())).toAlignedRect();
                QImage out(bounds.size(), QImage::Format_ARGB32_Premultiplied);
                out.fill(bgc);
                QPainter p(&out);
                p.translate(-bounds.topLeft());
                p.setTransform(t, true);
                p.drawImage(0, 0, r);
                p.end();
                r = out;
            }
            return r;
        });
    }

    void propertiesDialog()
    {
        QDialog d(this);
        d.setWindowTitle(tr7("Image Properties"));
        auto *v = new QVBoxLayout(&d);
        auto *attrs = new QGroupBox(tr7("File attributes"));
        auto *af = new QFormLayout(attrs);
        const QFileInfo fi(m_file);
        af->addRow(tr7("Last Saved:"), new QLabel(m_file.isEmpty() ? tr7("Not Available") : QLocale().toString(fi.lastModified(), QLocale::ShortFormat)));
        af->addRow(tr7("Size on disk:"), new QLabel(m_file.isEmpty() ? tr7("Not Available") : QLocale().formattedDataSize(fi.size(), 1, QLocale::DataSizeTraditionalFormat)));
        af->addRow(tr7("Resolution:"), new QLabel(tr7("96 Dots Per Inch")));
        v->addWidget(attrs);
        auto *row = new QHBoxLayout;
        auto *units = new QGroupBox(tr7("Units"));
        auto *uv = new QVBoxLayout(units);
        auto *in = new QRadioButton(tr7("&Inches"));
        auto *cm = new QRadioButton(tr7("C&entimeters"));
        auto *px = new QRadioButton(tr7("Pi&xels"));
        px->setChecked(true);
        uv->addWidget(in);
        uv->addWidget(cm);
        uv->addWidget(px);
        auto *colors = new QGroupBox(tr7("Colors"));
        auto *cv = new QVBoxLayout(colors);
        auto *bw = new QRadioButton(tr7("&Black and white"));
        auto *col = new QRadioButton(tr7("&Color"));
        col->setChecked(true);
        cv->addWidget(bw);
        cv->addWidget(col);
        cv->addStretch();
        row->addWidget(units);
        row->addWidget(colors);
        v->addLayout(row);
        auto *sz = new QHBoxLayout;
        auto *w = new QLineEdit(QString::number(m_canvas->image().width()));
        auto *h = new QLineEdit(QString::number(m_canvas->image().height()));
        w->setFixedWidth(60);
        h->setFixedWidth(60);
        sz->addWidget(new QLabel(tr7("&Width:")));
        sz->addWidget(w);
        sz->addSpacing(12);
        sz->addWidget(new QLabel(tr7("&Height:")));
        sz->addWidget(h);
        auto *def = new QPushButton(tr7("&Default"));
        sz->addSpacing(12);
        sz->addWidget(def);
        v->addLayout(sz);
        // units conversion of the two fields
        int unit = 2;
        auto toPx = [&](double val) {
            return unit == 0 ? val * 96 : unit == 1 ? val * 96 / 2.54 : val;
        };
        auto fromPx = [&](double p, int u) {
            return u == 0 ? QString::number(p / 96, 'f', 2) : u == 1 ? QString::number(p * 2.54 / 96, 'f', 2) : QString::number(qRound(p));
        };
        auto setUnit = [&](int u) {
            const double wp = toPx(QLocale::c().toDouble(w->text())), hp = toPx(QLocale::c().toDouble(h->text()));
            unit = u;
            w->setText(fromPx(wp, u));
            h->setText(fromPx(hp, u));
        };
        QObject::connect(in, &QRadioButton::toggled, [&](bool on) {
            if (on) {
                setUnit(0);
            }
        });
        QObject::connect(cm, &QRadioButton::toggled, [&](bool on) {
            if (on) {
                setUnit(1);
            }
        });
        QObject::connect(px, &QRadioButton::toggled, [&](bool on) {
            if (on) {
                setUnit(2);
            }
        });
        QObject::connect(def, &QPushButton::clicked, [&] {
            w->setText(fromPx(defaultSize().width(), unit));
            h->setText(fromPx(defaultSize().height(), unit));
        });
        auto *bb = new QDialogButtonBox;
        bb->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
        bb->addButton(tr7("Cancel"), QDialogButtonBox::RejectRole);
        QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        v->addWidget(bb);
        d.setFixedSize(d.sizeHint());
        if (d.exec() != QDialog::Accepted) {
            return;
        }
        const QSize ns(qMax(1, qRound(toPx(QLocale::c().toDouble(w->text())))), qMax(1, qRound(toPx(QLocale::c().toDouble(h->text())))));
        if (ns != m_canvas->image().size()) {
            m_canvas->resizeCanvas(ns);
        }
        if (bw->isChecked()) {
            m_canvas->transform([](const QImage &i) {
                return i.convertToFormat(QImage::Format_Mono, Qt::ThresholdDither).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            });
        }
    }

    void about()
    {
        // the Windows "About" box (shell32 ShellAbout) as the rest of the desktop shows it
        if (!QProcess::startDetached(QStringLiteral("seven-control"), {QStringLiteral("--winver"), QStringLiteral("Paint")})) {
            QMessageBox::about(this, tr7("About Paint"), tr7("Paint\nVersion 6.1"));
        }
    }

    // ------------------------------------------------------------------ view
    void toggleRulers(bool on)
    {
        m_hruler->setVisible(on);
        m_vruler->setVisible(on);
        if (m_rulers->isChecked() != on) {
            m_rulers->setChecked(on);
        }
    }
    void fullScreen()
    {
        auto *w = new QLabel;
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->setStyleSheet(QStringLiteral("background: black;"));
        w->setAlignment(Qt::AlignCenter);
        QPixmap pm = QPixmap::fromImage(m_canvas->flattened());
        const QSize scr = screen()->size();
        if (pm.width() > scr.width() || pm.height() > scr.height()) {
            pm = pm.scaled(scr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        w->setPixmap(pm);
        struct Closer : QObject {
            bool eventFilter(QObject *o, QEvent *e) override
            {
                if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::KeyPress) {
                    static_cast<QWidget *>(o)->close();
                    return true;
                }
                return false;
            }
        };
        w->installEventFilter(new Closer);
        w->showFullScreen();
    }
    void toggleThumbnail()
    {
        if (m_thumbWin) {
            m_thumbWin->close();
            return;
        }
        auto *t = new QLabel(nullptr);
        t->setWindowFlags(Qt::Tool);
        t->setAttribute(Qt::WA_DeleteOnClose);
        t->setWindowTitle(tr7("Thumbnail"));
        t->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        t->resize(200, 150);
        m_thumbWin = t;
        QObject::connect(t, &QObject::destroyed, [this] {
            m_thumbWin = nullptr;
            m_thumb->setChecked(false);
        });
        auto refresh = [this, t] {
            const qreal z = qMax(1.0, m_canvas->zoom());
            const QRect vis(int((m_area->horizontalScrollBar()->value() - Canvas::M) / m_canvas->zoom()),
                            int((m_area->verticalScrollBar()->value() - Canvas::M) / m_canvas->zoom()), int(t->width() / 1.0 * z / z), int(t->height()));
            t->setPixmap(QPixmap::fromImage(m_canvas->image().copy(vis)));
        };
        auto *timer = new QTimer(t);
        QObject::connect(timer, &QTimer::timeout, t, refresh);
        timer->start(200);
        refresh();
        t->move(mapToGlobal(QPoint(width() - 230, 150)));
        t->show();
        m_thumb->setChecked(true);
    }

    Rib::Bar *m_bar;
    WorkArea *m_area;
    Canvas *m_canvas;
    Ruler *m_hruler, *m_vruler;
    StatusBar *m_status;
    QWidget *m_home = nullptr, *m_view = nullptr, *m_textPage = nullptr;
    Rib::Button *m_cut, *m_copy, *m_crop, *m_select, *m_brushes, *m_outline, *m_fill, *m_size, *m_zoomIn, *m_zoomOut, *m_thumb;
    Rib::Button *m_opaque, *m_transparent, *m_paste = nullptr, *m_textCut = nullptr, *m_textCopy = nullptr, *m_textPaste = nullptr;
    ToolGrid *m_tools = nullptr;
    ShapeGallery *m_gallery;
    ColorButton *m_c1, *m_c2;
    Palette *m_palette;
    QList<std::pair<ColorButton *, ColorButton *>> m_colorButtons;
    QList<Palette *> m_palettes;
    QList<Rib::Group *> m_groups;
    QCheckBox *m_rulers, *m_grid, *m_statusCheck;
    Rib::Combo *m_fontBox, *m_sizeBox;
    QLabel *m_thumbWin = nullptr;
    int m_which = 1;
    bool m_freeSelect = false;
    QString m_file;
    bool m_modified = false;
    int m_saveDepth = 24;
    QStringList m_recent;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Win7I18n::install(&app);
    app.setApplicationName(QStringLiteral("seven-paint"));
    app.setApplicationDisplayName(QString());
    app.setDesktopFileName(QStringLiteral("seven-paint"));
    PaintWindow w;
    QString file = app.arguments().value(1);
    if (file.startsWith(QLatin1String("file://"))) {
        file = QUrl(file).toLocalFile();
    }
    if (!file.isEmpty()) {
        w.load(file);
    }
    w.show();
    if (qEnvironmentVariableIsSet("WIN7_PAINT_SHOT")) {
        // WIN7_PAINT_SHOT=<png>:<tab> grabs the window with that ribbon tab open
        QTimer::singleShot(400, &w, [&w] {
            const QStringList a = qEnvironmentVariable("WIN7_PAINT_SHOT").split(QLatin1Char(':'));
            w.showTab(a.value(1).toInt());
            QTimer::singleShot(300, &w, [&w, a] {
                w.grab().save(a.value(0));
                qApp->exit();
            });
        });
    }
    if (qEnvironmentVariableIsSet("WIN7_PAINT_TEST")) {
        QTimer::singleShot(200, &w, [&w] {
            w.selfTest(qEnvironmentVariable("WIN7_PAINT_TEST"));
        });
    }
    return app.exec();
}
