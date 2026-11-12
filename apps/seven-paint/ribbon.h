#pragma once
/*
 * The Scenic Ribbon of Windows 7 Paint, painted after the real one (VM screenshots ~/VMs/sevenplasma/refs/paint*.png):
 * tab row 24 px (#dfe9f5, blue app button, white selected tab), body 90 px fading #fbfdff -> #e5f0fb, group labels
 * #738399 at the bottom, #e0ebf7 separators, buttons with the yellow/orange hot and checked states.
 * Pictures: mspaint.exe resources in ~/.local/share/seven-paint/<id>.png.
 */
#include <QLinearGradient>
#include <QPolygonF>
#include <QDir>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>

namespace Rib
{
inline QPixmap px(int id)
{
    static QHash<int, QPixmap> cache;
    auto it = cache.find(id);
    if (it == cache.end()) {
        it = cache.insert(id, QPixmap(QDir::homePath() + QStringLiteral("/.local/share/seven-paint/%1.png").arg(id)));
    }
    return *it;
}
/** our drawings of the few ribbon parts that have no picture in Windows 7's files (UIRibbon tints its own at
 *  run time): the application button, the zoom slider thumb and the size grip */
inline QPixmap drawn(const QString &name)
{
    if (name == QLatin1String("appbtn")) { // 56 x 22: glossy blue, a document glyph and a small arrow
        QImage im(56, 22, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&im);
        QLinearGradient top(0, 1, 0, 10), bottom(0, 10, 0, 21);
        top.setColorAt(0, QColor(125, 170, 214));
        top.setColorAt(0.12, QColor(73, 139, 198));
        top.setColorAt(1, QColor(47, 104, 179));
        bottom.setColorAt(0, QColor(26, 65, 136));
        bottom.setColorAt(0.5, QColor(36, 89, 156));
        bottom.setColorAt(1, QColor(64, 155, 207));
        p.fillRect(0, 0, 56, 22, QColor(65, 106, 189));
        p.fillRect(QRect(0, 1, 56, 9), top);
        p.fillRect(QRect(0, 10, 56, 11), bottom);
        p.fillRect(QRect(0, 21, 56, 1), QColor(82, 120, 195));
        p.setRenderHint(QPainter::Antialiasing);
        // the page: white, a grey strip on the left, blue lines (Paint's own glyph)
        const QRectF page(14.5, 4.5, 15, 13);
        p.setPen(QColor(150, 165, 185));
        p.setBrush(QColor(250, 251, 252));
        p.drawRect(page);
        p.fillRect(QRectF(15, 5, 4, 12), QColor(200, 206, 214));
        p.setPen(QPen(QColor(55, 100, 205), 1));
        for (int y = 7; y <= 15; y += 2) {
            p.drawLine(QPointF(21, y + 0.5), QPointF(28, y + 0.5));
        }
        // the arrow
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawPolygon(QPolygonF() << QPointF(35.5, 9) << QPointF(42.5, 9) << QPointF(39, 12.5));
        return QPixmap::fromImage(im);
    }
    if (name == QLatin1String("zoomthumb")) { // 9 x 17: grey, rounded top, pointed bottom
        QImage im(9, 17, QImage::Format_ARGB32_Premultiplied);
        im.fill(Qt::transparent);
        QPainter p(&im);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.moveTo(1, 0.5);
        path.lineTo(8, 0.5);
        path.lineTo(8.5, 12);
        path.lineTo(4.5, 16);
        path.lineTo(0.5, 12);
        path.closeSubpath();
        QLinearGradient g(0, 0, 0, 16);
        g.setColorAt(0, QColor(242, 242, 242));
        g.setColorAt(0.45, QColor(235, 235, 235));
        g.setColorAt(0.5, QColor(219, 219, 219));
        g.setColorAt(1, QColor(212, 212, 212));
        p.setBrush(g);
        p.setPen(QColor(112, 112, 112));
        p.drawPath(path);
        return QPixmap::fromImage(im);
    }
    if (name == QLatin1String("grip")) { // 13 x 14: the three-step dots of a Windows 7 size grip
        QImage im(13, 14, QImage::Format_ARGB32_Premultiplied);
        im.fill(Qt::transparent);
        QPainter p(&im);
        const int dots[][2] = {{10, 5}, {7, 8}, {10, 8}, {4, 11}, {7, 11}, {10, 11}};
        for (const auto &d : dots) {
            p.fillRect(d[0], d[1], 1, 1, Qt::white);
            p.fillRect(d[0] + 1, d[1], 1, 1, QColor(207, 207, 207));
            p.fillRect(d[0], d[1] + 1, 1, 1, QColor(231, 231, 231));
            p.fillRect(d[0] + 1, d[1] + 1, 1, 1, QColor(175, 175, 175));
        }
        return QPixmap::fromImage(im);
    }
    return {};
}

inline QPixmap named(const QString &name)
{
    QPixmap pm(QDir::homePath() + QStringLiteral("/.local/share/seven-paint/") + name + QStringLiteral(".png"));
    return pm.isNull() ? drawn(name) : pm;
}

const QColor TEXT(0x1e, 0x39, 0x5b);
const QColor GROUP_LABEL(0x73, 0x83, 0x99);

/** the hot / pressed / checked backgrounds of ribbon buttons */
inline void paintButtonBack(QPainter &p, const QRectF &r, bool hot, bool down, bool checked, int splitY = -1)
{
    if (!hot && !down && !checked) {
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    QColor border;
    if (down || (checked && hot)) {
        g.setColorAt(0, QColor(0xfb, 0xd0, 0x8f));
        g.setColorAt(0.4, QColor(0xf5, 0xb4, 0x51));
        g.setColorAt(1, QColor(0xfd, 0xd1, 0x7c));
        border = QColor(0xc2, 0x9b, 0x29);
    } else if (checked) {
        g.setColorAt(0, QColor(0xff, 0xdb, 0x9f));
        g.setColorAt(0.4, QColor(0xff, 0xc4, 0x5d));
        g.setColorAt(1, QColor(0xff, 0xd6, 0x7f));
        border = QColor(0xc2, 0x9b, 0x29);
    } else {
        g.setColorAt(0, QColor(0xff, 0xfd, 0xdb));
        g.setColorAt(0.4, QColor(0xff, 0xe7, 0x93));
        g.setColorAt(1, QColor(0xff, 0xf4, 0xb6));
        border = QColor(0xdb, 0xce, 0x99);
    }
    QPainterPath path;
    path.addRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 2.5, 2.5);
    p.fillPath(path, g);
    p.setPen(border);
    p.drawPath(path);
    p.setPen(QColor(255, 255, 255, 120));
    p.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), 2, 2);
    if (splitY > 0) {
        p.setPen(QColor(0xd6, 0x9e, 0x22, checked || down ? 255 : 120));
        p.drawLine(QPointF(r.left() + 1, splitY + 0.5), QPointF(r.right() - 1, splitY + 0.5));
    }
    p.restore();
}

inline void drawDropArrow(QPainter &p, QPointF c, const QColor &color = QColor(0x3c, 0x3c, 0x3c))
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath a;
    a.moveTo(c + QPointF(-3, -1.5));
    a.lineTo(c + QPointF(3, -1.5));
    a.lineTo(c + QPointF(0, 1.5));
    a.closeSubpath();
    p.fillPath(a, color);
    p.restore();
}


/** the frame every ribbon drop-down shares: #8492a6 border, white inner line, #fcfcfc face, a 4 px soft shadow
 *  (measured on the VM: 192/192/209/236 over white = black at 25/25/18/7 %) */
inline void paintPopupFrame(QPainter &p, const QRect &r)
{
    static const int alpha[4] = {64, 64, 46, 19};
    for (int i = 0; i < 4; ++i) {
        p.fillRect(QRect(r.left() + 4, r.bottom() + 1 + i, r.width() - 2, 1), QColor(0, 0, 0, alpha[i]));
        p.fillRect(QRect(r.right() + 1 + i, r.top() + 4, 1, r.height() - 3 + i), QColor(0, 0, 0, alpha[i]));
    }
    p.fillRect(r, QColor(132, 146, 166));
    p.fillRect(r.adjusted(1, 1, -1, -1), Qt::white);
    p.fillRect(r.adjusted(2, 2, -2, -2), QColor(252, 252, 252));
}

/** a ribbon drop-down menu: section headers on #ebf2f7, a #f3f7fb icon column, 22 px items */
class Menu : public QWidget
{
public:
    struct Entry {
        bool header = false;
        int icon = 0;
        QString text;
        bool enabled = true;
        bool checked = false;
        std::function<void()> run;
    };
    QList<Entry> entries;

    Menu()
        : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
    }
    void addHeader(const QString &t)
    {
        entries.append({true, 0, t, true, false, nullptr});
    }
    void add(int icon, const QString &t, std::function<void()> run, bool enabled = true, bool checked = false)
    {
        entries.append({false, icon, t, enabled, checked, std::move(run)});
    }
    /** shows the menu at `pos` and returns once it is closed, running the chosen entry afterwards */
    void exec(const QPoint &pos)
    {
        prepare();
        move(pos);
        show();
        QEventLoop loop;
        m_loop = &loop;
        loop.exec();
        m_loop = nullptr;
        hide();
        if (m_chosen >= 0 && entries[m_chosen].run) {
            entries[m_chosen].run();
        }
    }
    void prepare()
    {
        const QFontMetrics fm(font());
        int w = 0, h = 4;
        for (const Entry &e : entries) {
            w = qMax(w, fm.horizontalAdvance(QString(e.text).remove(QLatin1Char('&'))) + (e.header ? 20 : 50));
            h += e.header ? 23 : 22;
        }
        m_frame = QRect(0, 0, qMax(w, 120), h);
        resize(m_frame.width() + 4, m_frame.height() + 4);
    }

protected:
    QRect entryRect(int i) const
    {
        int y = 2;
        for (int k = 0; k < i; ++k) {
            y += entries[k].header ? 23 : 22;
        }
        return QRect(2, y, m_frame.width() - 4, entries[i].header ? 23 : 22);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        paintPopupFrame(p, m_frame.adjusted(0, 0, -1, -1));
        // icon column and its #cfdbeb / white edge
        p.fillRect(QRect(2, 2, 24, m_frame.height() - 4), QColor(243, 247, 251));
        p.fillRect(QRect(26, 2, 1, m_frame.height() - 4), QColor(207, 219, 235));
        p.fillRect(QRect(27, 2, 1, m_frame.height() - 4), Qt::white);
        QFont bold = font();
        bold.setBold(true);
        for (int i = 0; i < entries.size(); ++i) {
            const Entry &e = entries[i];
            const QRect r = entryRect(i);
            if (e.header) {
                p.fillRect(r.adjusted(0, 0, 0, -2), QColor(235, 242, 247));
                p.fillRect(QRect(r.left(), r.bottom() - 1, r.width(), 1), QColor(207, 219, 235));
                p.fillRect(QRect(r.left(), r.bottom(), r.width(), 1), Qt::white);
                p.setFont(bold);
                p.setPen(QColor(56, 72, 99));
                p.drawText(r.adjusted(7, 0, 0, -2), Qt::AlignVCenter, e.text);
                p.setFont(font());
                continue;
            }
            if (i == m_hot && e.enabled) {
                paintButtonBack(p, QRectF(r).adjusted(1, 0, -1, 0), true, false, false);
            }
            if (e.checked) {
                QRect box(r.left() + 2, r.top() + 1, 20, 20);
                paintButtonBack(p, box, false, false, true);
            }
            QPixmap pm = e.icon ? px(e.icon) : QPixmap();
            if (pm.isNull() && e.checked) {
                // check mark
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(QPen(QColor(0x1e, 0x39, 0x5b), 1.6));
                p.drawPolyline(QPolygonF({QPointF(r.left() + 7.5, r.top() + 11), QPointF(r.left() + 10.5, r.top() + 14), QPointF(r.left() + 16, r.top() + 7.5)}));
                p.setRenderHint(QPainter::Antialiasing, false);
            } else if (!pm.isNull()) {
                if (!e.enabled) {
                    QImage g = pm.toImage().convertToFormat(QImage::Format_ARGB32);
                    for (int y = 0; y < g.height(); ++y) {
                        QRgb *l = reinterpret_cast<QRgb *>(g.scanLine(y));
                        for (int x = 0; x < g.width(); ++x) {
                            const int v = qGray(l[x]) / 2 + 110;
                            l[x] = qRgba(v, v, v, qAlpha(l[x]) * 2 / 3);
                        }
                    }
                    pm = QPixmap::fromImage(g);
                }
                p.drawPixmap(r.left() + 4, r.top() + 3, pm.scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
            p.setPen(e.enabled ? QColor(30, 57, 91) : QColor(158, 158, 158));
            p.drawText(r.adjusted(31, 0, -6, 0), Qt::AlignVCenter | Qt::TextShowMnemonic, e.text);
        }
    }
    int hit(const QPoint &pt) const
    {
        for (int i = 0; i < entries.size(); ++i) {
            if (!entries[i].header && entryRect(i).contains(pt)) {
                return i;
            }
        }
        return -1;
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = hit(e->position().toPoint());
        if (h != m_hot) {
            m_hot = h;
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        const int h = hit(e->position().toPoint());
        if (h >= 0 && !entries[h].enabled) {
            return;
        }
        if (h < 0 && rect().contains(e->position().toPoint())) {
            return;
        }
        m_chosen = h;
        close();
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape) {
            close();
            return;
        }
        if (e->key() == Qt::Key_Down || e->key() == Qt::Key_Up) {
            const int n = entries.size();
            int i = m_hot;
            for (int k = 0; k < n; ++k) {
                i = (i + (e->key() == Qt::Key_Down ? 1 : n - 1) + (i < 0 && e->key() == Qt::Key_Up ? 1 : 0)) % n;
                if (!entries[i].header && entries[i].enabled) {
                    break;
                }
            }
            m_hot = i;
            update();
            return;
        }
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && m_hot >= 0) {
            m_chosen = m_hot;
            close();
            return;
        }
        // mnemonics
        for (int i = 0; i < entries.size(); ++i) {
            const QString &t = entries[i].text;
            const int a = t.indexOf(QLatin1Char('&'));
            if (!entries[i].header && entries[i].enabled && a >= 0 && a + 1 < t.size() && !e->text().isEmpty()
                && t.at(a + 1).toLower() == e->text().at(0).toLower()) {
                m_chosen = i;
                close();
                return;
            }
        }
    }
    void hideEvent(QHideEvent *) override
    {
        if (m_loop) {
            m_loop->quit();
        }
    }

private:
    QRect m_frame;
    int m_hot = -1, m_chosen = -1;
    QEventLoop *m_loop = nullptr;
};

/** large (32 px icon, label under it) or small (16 px icon, label right) ribbon button */
class Button : public QWidget
{
public:
    enum Kind { Large, Small, IconOnly };
    std::function<void()> clicked;
    std::function<void(const QPoint &)> dropdown; // the arrow part (or the whole button when there is no click)

    Button(Kind kind, int icon, const QString &text, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_kind(kind)
        , m_icon(icon)
        , m_text(text)
    {
        setMouseTracking(true);
        resizeForText();
    }
    void setChecked(bool on)
    {
        m_checked = on;
        update();
    }
    bool isChecked() const
    {
        return m_checked;
    }
    void setIcon(int icon)
    {
        m_icon = icon;
        update();
    }
    void setPixmap(const QPixmap &pm)
    {
        m_pixmap = pm;
        update();
    }
    /** a split button whose top half is disabled on its own (Paste with an empty clipboard) */
    void setTopEnabled(bool on)
    {
        m_topEnabled = on;
        update();
    }
    bool topEnabled() const
    {
        return m_topEnabled;
    }
    void setSplit(bool on)
    {
        m_split = on;
        resizeForText();
    }
    QString text() const
    {
        return m_text;
    }
    void setText(const QString &t)
    {
        m_text = t;
        resizeForText();
        update();
    }

protected:
    void resizeForText()
    {
        const QFontMetrics fm(font());
        if (m_kind == Large) {
            int w = 0;
            for (const QString &l : m_text.split(QLatin1Char('\n'))) {
                w = qMax(w, fm.horizontalAdvance(l));
            }
            setFixedSize(qMax(40, w + 10), 66);
        } else if (m_kind == Small) {
            setFixedSize(22 + fm.horizontalAdvance(m_text) + (dropdown || m_split ? 14 : 6), 22);
        } else {
            setFixedSize(23, 22);
        }
    }
    bool hasArrow() const
    {
        return bool(dropdown);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const bool enabled = isEnabled();
        const bool hot = m_hot && enabled;
        const int splitY = (m_kind == Large && hasArrow() && clicked) ? 38 : -1;
        paintButtonBack(p, rect(), hot, m_down && enabled, m_checked, splitY);
        QPixmap pm = m_pixmap.isNull() ? px(m_icon) : m_pixmap;
        if (!enabled || !m_topEnabled) {
            QImage g = pm.toImage().convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < g.height(); ++y) {
                QRgb *l = reinterpret_cast<QRgb *>(g.scanLine(y));
                for (int x = 0; x < g.width(); ++x) {
                    const int v = qGray(l[x]) / 2 + 110;
                    l[x] = qRgba(v, v, v, qAlpha(l[x]) * 2 / 3);
                }
            }
            pm = QPixmap::fromImage(g);
        }
        p.setPen(enabled && m_topEnabled ? TEXT : QColor(0x8d, 0x99, 0xa8));
        const QFontMetrics fm(font());
        if (m_kind == Large) {
            p.drawPixmap((width() - 32) / 2, 3, pm.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            const QStringList lines = m_text.split(QLatin1Char('\n'));
            int y = 37;
            for (const QString &l : lines) {
                p.drawText(QRect(0, y, width(), fm.height()), Qt::AlignHCenter | Qt::AlignTop, l);
                y += fm.height() - 1;
            }
            if (hasArrow()) {
                drawDropArrow(p, QPointF(width() / 2.0, lines.size() > 1 ? y + 5 : y + 6));
            }
        } else if (m_kind == Small) {
            p.drawPixmap(3, 3, pm.scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            p.drawText(QRect(22, 0, width() - 22, height()), Qt::AlignLeft | Qt::AlignVCenter, m_text);
            if (hasArrow()) {
                drawDropArrow(p, QPointF(width() - 7, height() / 2.0 + 1), enabled ? QColor(0x3c, 0x3c, 0x3c) : QColor(0x9a, 0xa4, 0xb0));
            }
        } else {
            p.drawPixmap((width() - 16) / 2, (height() - 16) / 2, pm.scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        m_down = true;
        update();
        const bool onArrow = hasArrow() && (!clicked || (m_kind == Large ? e->position().y() > 38 : e->position().x() > width() - 14));
        if (onArrow) {
            m_down = false;
            update();
            dropdown(mapToGlobal(QPoint(0, height())));
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (m_down && rect().contains(e->position().toPoint()) && clicked && m_topEnabled) {
            m_down = false;
            update();
            clicked();
            return;
        }
        m_down = false;
        update();
    }

    Kind m_kind;
    int m_icon;
    QPixmap m_pixmap;
    QString m_text;
    bool m_hot = false, m_down = false, m_checked = false, m_split = false, m_topEnabled = true;
};

/** a ribbon group: its controls, the label at the bottom, a separator on the right */
class Group : public QWidget
{
public:
    QHBoxLayout *row;

    explicit Group(const QString &label, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_label(label)
    {
        row = new QHBoxLayout(this);
        row->setContentsMargins(3, 4, 6, 0);
        row->setSpacing(2);
        row->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        setFixedHeight(89);
        // a group is never narrower than its label
        setMinimumWidth(QFontMetrics(font()).horizontalAdvance(label) + 14);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        // measured on the VM: label #738399 with its rows at 78..87 of the page; separator = light | dark | light,
        // y 6..82, the dark line #9db2c9 fading in over 4 px to #b2c3d6 and settling at #a5b7d0
        QPainter p(this);
        p.setPen(GROUP_LABEL);
        p.drawText(QRect(0, 72, width() - 3, 16), Qt::AlignHCenter | Qt::AlignVCenter, m_label);
        const int x = width() - 2;
        QLinearGradient dark(0, 6, 0, 83);
        dark.setColorAt(0, QColor(157, 178, 201));
        dark.setColorAt(0.05, QColor(178, 195, 214));
        dark.setColorAt(0.65, QColor(165, 183, 208));
        dark.setColorAt(1, QColor(165, 184, 208));
        QLinearGradient light(0, 6, 0, 83);
        light.setColorAt(0, QColor(248, 250, 255));
        light.setColorAt(0.65, QColor(236, 241, 250));
        light.setColorAt(1, QColor(236, 241, 250));
        p.fillRect(QRect(x - 1, 6, 1, 77), light);
        p.fillRect(QRect(x + 1, 6, 1, 77), light);
        p.fillRect(QRect(x, 6, 1, 77), dark);
    }

private:
    QString m_label;
};

/** the rounded #c5d2e2 frame that groups a row of small icon buttons (Tools, Font styles) */
class FrameRow : public QWidget
{
public:
    QHBoxLayout *row;
    explicit FrameRow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        row = new QHBoxLayout(this);
        row->setContentsMargins(1, 1, 1, 1);
        row->setSpacing(0);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient g(frame.topLeft(), frame.bottomLeft());
        g.setColorAt(0, QColor(0xfa, 0xfc, 0xfe));
        g.setColorAt(1, QColor(0xe9, 0xf1, 0xfa));
        QPainterPath path;
        path.addRoundedRect(frame, 2, 2);
        p.fillPath(path, g);
        p.setPen(QColor(0xc5, 0xd2, 0xe2));
        p.drawPath(path);
        p.setRenderHint(QPainter::Antialiasing, false);
        for (int i = 1; i < row->count(); ++i) {
            const QRect r = row->itemAt(i)->geometry();
            p.fillRect(QRect(r.left(), r.top() + 1, 1, r.height() - 2), QColor(0xdc, 0xe5, 0xf0));
        }
    }
};


/** the ribbon combo box (Font, Size): #aabbd2 border, #f3f7fc field (white when hot), a small #4c607a arrow */
class Combo : public QWidget
{
public:
    std::function<void(const QString &)> changed;
    std::function<QStringList()> items;
    bool fontPreview = false; // list entries drawn in their own typeface

    explicit Combo(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        m_edit = new QLineEdit(this);
        m_edit->setFrame(false);
        m_edit->setAttribute(Qt::WA_TranslucentBackground);
        m_edit->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0px; margin: 0px; color: #000000; }"));
        QObject::connect(m_edit, &QLineEdit::editingFinished, this, [this] {
            if (changed) {
                changed(m_edit->text());
            }
        });
    }
    QString text() const
    {
        return m_edit->text();
    }
    void setText(const QString &t)
    {
        m_edit->setText(t);
        m_edit->setCursorPosition(0);
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        m_edit->setGeometry(3, 1, width() - 3 - 14, height() - 2);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const bool hot = m_hot || m_edit->hasFocus();
        p.fillRect(rect(), hot ? QColor(126, 162, 203) : QColor(170, 187, 210));
        p.fillRect(rect().adjusted(1, 1, -1, -1), hot ? Qt::white : QColor(243, 247, 252));
        if (hot) {
            p.fillRect(QRect(width() - 13, 1, 1, height() - 2), QColor(197, 210, 226));
        }
        const QPointF c(width() - 7.5 + 0.5, height() / 2.0 + 0.5);
        QPainterPath a;
        a.moveTo(c + QPointF(-3, -1.5));
        a.lineTo(c + QPointF(3, -1.5));
        a.lineTo(c + QPointF(0, 1.5));
        a.closeSubpath();
        p.setRenderHint(QPainter::Antialiasing);
        p.fillPath(a, QColor(76, 96, 122));
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
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->position().x() < width() - 14 || !items) {
            m_edit->setFocus();
            return;
        }
        // the list: a ribbon popup frame around a plain list
        auto *pop = new QWidget(nullptr, Qt::Popup | Qt::NoDropShadowWindowHint);
        pop->setAttribute(Qt::WA_DeleteOnClose);
        pop->setAttribute(Qt::WA_TranslucentBackground);
        auto *v = new QVBoxLayout(pop);
        v->setContentsMargins(2, 2, 2 + 4, 2 + 4);
        auto *list = new QListWidget(pop);
        list->setFrameShape(QFrame::NoFrame);
        list->setStyleSheet(QStringLiteral("QListWidget { background: #fcfcfc; color: #000000; }"
                                           "QListWidget::item { padding: 2px 4px; }"
                                           "QListWidget::item:selected, QListWidget::item:hover { background: #ffe8a6; color: #000000; }"));
        for (const QString &t : items()) {
            auto *it = new QListWidgetItem(t, list);
            if (fontPreview) {
                QFont f(t);
                f.setPointSize(11);
                it->setFont(f);
            }
            if (t == m_edit->text()) {
                list->setCurrentItem(it);
            }
        }
        v->addWidget(list);
        struct Frame : QObject {
            bool eventFilter(QObject *o, QEvent *e) override
            {
                if (e->type() == QEvent::Paint) {
                    auto *w = static_cast<QWidget *>(o);
                    QPainter p(w);
                    paintPopupFrame(p, QRect(0, 0, w->width() - 5, w->height() - 5));
                }
                return false;
            }
        };
        pop->installEventFilter(new Frame);
        QObject::connect(list, &QListWidget::itemClicked, pop, [this, pop](QListWidgetItem *it) {
            setText(it->text());
            pop->close();
            if (changed) {
                changed(it->text());
            }
        });
        const int w = fontPreview ? 260 : width();
        pop->resize(w + 4, fontPreview ? 420 : qMin(380, list->count() * 20 + 10));
        pop->move(mapToGlobal(QPoint(0, height())));
        pop->show();
        if (list->currentItem()) {
            list->scrollToItem(list->currentItem(), QAbstractItemView::PositionAtCenter);
        }
    }

private:
    QLineEdit *m_edit;
    bool m_hot = false;
};

/** a column of two or three small buttons */
inline QVBoxLayout *column(QHBoxLayout *row)
{
    auto *v = new QVBoxLayout;
    v->setSpacing(0);
    v->setContentsMargins(0, 0, 0, 0);
    v->setAlignment(Qt::AlignTop);
    row->addLayout(v);
    return v;
}

/** the tab row and the body background */
class Bar : public QWidget
{
public:
    std::function<void(int)> tabChanged;
    std::function<void(const QPoint &)> appMenu;

    Bar(const QStringList &tabs, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_tabs(tabs)
    {
        setMouseTracking(true);
        setFixedHeight(116);
    }
    int current() const
    {
        return m_current;
    }
    void setCurrent(int i)
    {
        m_current = i;
        update();
        if (tabChanged) {
            tabChanged(i);
        }
    }
    void setTabs(const QStringList &tabs)
    {
        m_tabs = tabs;
        update();
    }
    /** a contextual tab (Text tools) is tinted and named above itself */
    void setContextual(int i, const QString &name)
    {
        m_contextual = i;
        m_contextName = name;
        update();
    }
    void addPage(QWidget *page)
    {
        m_pages.append(page);
        page->setGeometry(0, 24, width(), height() - 24 - 2);
    }
    QRect tabRect(int i) const
    {
        const QFontMetrics fm(font());
        int x = 58;
        for (int k = 0; k < i; ++k) {
            x += fm.horizontalAdvance(m_tabs[k]) + 26;
        }
        return QRect(x, 1, fm.horizontalAdvance(m_tabs[i]) + 26, 23);
    }
    QRect appRect() const
    {
        return QRect(0, 1, 56, 22);
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        for (QWidget *page : m_pages) {
            page->setGeometry(0, 24, width(), height() - 24 - 2);
        }
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // tab row and the line under it
        p.fillRect(QRect(0, 0, width(), 23), QColor(223, 233, 245));
        p.fillRect(QRect(0, 23, width(), 1), QColor(186, 201, 219));
        // body: #fbfdff, then #f7fbff fading to #dce6f4 at 79, flat to 112; three closing lines
        p.fillRect(QRect(0, 24, width(), 1), QColor(251, 253, 255));
        QLinearGradient g(0, 25, 0, 80);
        g.setColorAt(0, QColor(247, 251, 255));
        g.setColorAt(1, QColor(220, 230, 244));
        p.fillRect(QRect(0, 25, width(), 55), g);
        p.fillRect(QRect(0, 80, width(), 33), QColor(220, 231, 245));
        p.fillRect(QRect(0, 113, width(), 1), QColor(229, 240, 251));
        p.fillRect(QRect(0, 114, width(), 1), QColor(206, 219, 235));
        p.fillRect(QRect(0, 115, width(), 1), QColor(159, 174, 194));
        // application button: the genuine one, lighter when hot, darker when its menu is open
        const QRect ar = appRect();
        QImage ab = named(QStringLiteral("appbtn")).toImage().convertToFormat(QImage::Format_RGB32);
        if (m_hot == -2 || m_appDown) {
            for (int y = 0; y < ab.height(); ++y) {
                QRgb *l = reinterpret_cast<QRgb *>(ab.scanLine(y));
                for (int x = 0; x < ab.width(); ++x) {
                    const QColor c(l[x]);
                    l[x] = (m_appDown ? c.darker(112) : c.lighter(115)).rgb();
                }
            }
        }
        p.drawImage(ar.topLeft(), ab);
        // tabs
        for (int i = 0; i < m_tabs.size(); ++i) {
            const QRect r = tabRect(i);
            if (i == m_contextual && i == m_current) {
                // a current contextual tab (Text tools) turns the line under the tab row green (#83ea3f)
                p.fillRect(QRect(0, 23, width(), 1), QColor(131, 234, 63));
            }
            if (i == m_current) {
                // #bac9db frame with 2 px corners, open at the bottom into the body
                const QColor edge = i == m_contextual ? QColor(131, 234, 63) : m_hot == i ? QColor(0xf1, 0xc4, 0x3f) : QColor(186, 201, 219);
                p.fillRect(QRect(r.left() + 1, r.top() + 1, r.width() - 2, r.height()), QColor(251, 253, 255));
                p.fillRect(QRect(r.left() + 3, r.top(), r.width() - 6, 1), edge);
                p.fillRect(QRect(r.left(), r.top() + 3, 1, r.height() - 4), edge);
                p.fillRect(QRect(r.right(), r.top() + 3, 1, r.height() - 4), edge);
                const QColor corner = i == m_contextual ? QColor(162, 223, 134) : QColor(195, 208, 224);
                p.fillRect(QRect(r.left() + 1, r.top() + 1, 2, 2), corner);
                p.fillRect(QRect(r.right() - 2, r.top() + 1, 2, 2), corner);
                p.fillRect(QRect(r.left() + 1, r.top() + 1, 1, 1), edge);
                p.fillRect(QRect(r.right() - 1, r.top() + 1, 1, 1), edge);
                p.fillRect(QRect(r.left() + 2, r.top() + 2, 1, 1), QColor(251, 253, 255));
                p.fillRect(QRect(r.right() - 2, r.top() + 2, 1, 1), QColor(251, 253, 255));
            } else if (m_hot == i) {
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(QColor(0xc6, 0xd5, 0xe8));
                p.setBrush(QColor(255, 255, 255, 90));
                p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, 2), 2, 2);
                p.setBrush(Qt::NoBrush);
                p.setRenderHint(QPainter::Antialiasing, false);
            }
            p.setPen(TEXT);
            p.drawText(r.adjusted(0, 0, 0, -1), Qt::AlignCenter, m_tabs[i]);
        }
    }
    int hit(const QPoint &pt) const
    {
        if (appRect().contains(pt)) {
            return -2;
        }
        for (int i = 0; i < m_tabs.size(); ++i) {
            if (tabRect(i).contains(pt)) {
                return i;
            }
        }
        return -1;
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = hit(e->position().toPoint());
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
        const int h = hit(e->position().toPoint());
        if (h == -2 && appMenu) {
            m_appDown = true;
            update();
            appMenu(mapToGlobal(appRect().bottomLeft()));
        } else if (h >= 0) {
            setCurrent(h);
        }
    }

private:
    QStringList m_tabs;
    QList<QWidget *> m_pages;
    int m_contextual = -1;
    QString m_contextName;
    int m_current = 0;
    int m_hot = -1;

public:
    bool m_appDown = false;
    void appMenuClosed()
    {
        m_appDown = false;
        update();
    }
};
}
