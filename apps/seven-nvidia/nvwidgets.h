#pragma once
/* The NVIDIA Control Panel's own pieces, measured on the Windows 7 screenshots (~/Desktop/win7, 10-18) */
#include "nvbackend.h"

#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include "nvscene.h"
#include <QOpenGLWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QSlider>
#include <QWidget>

namespace Nv
{
inline QString asset(const QString &name)
{
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-nvidia/") + name);
}

/* ---------------- "Select a Task..." tree: the classic tree with dotted lines and [-] boxes ---------------- */

class TaskTree : public QWidget
{
public:
    struct Item {
        QString text;
        int page = -1; // -1: a category
    };
    std::function<void(int)> activated;

    explicit TaskTree(const QList<Item> &items)
        : m_items(items)
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("background: white;"));
        setMinimumHeight(30 + m_items.size() * m_row);
    }
    void select(int page)
    {
        m_page = page;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        QPen dots(QColor(0xa0, 0xa0, 0xa0), 1, Qt::DotLine);
        const QFontMetrics fm(font());
        int lastCatY = -1;
        int lastChildY = -1;
        for (int i = 0; i < m_items.size(); ++i) {
            const Item &it = m_items.at(i);
            const int y = 4 + i * m_row;
            const int cy = y + m_row / 2;
            if (it.page < 0) {
                // [-] box 9x9 at x 6, the vertical line of the previous category's children ends at its last child
                if (lastCatY >= 0 && lastChildY >= 0) {
                    p.setPen(dots);
                    p.drawLine(10, lastCatY + 5, 10, lastChildY);
                }
                p.setPen(QColor(0x91, 0x91, 0x91));
                p.setBrush(Qt::white);
                p.drawRect(6, cy - 4, 8, 8);
                p.setPen(QColor(0x40, 0x40, 0x40));
                p.drawLine(8, cy, 12, cy);
                p.setPen(dots);
                p.drawLine(15, cy, 20, cy);
                p.setPen(QColor(0x1e, 0x1e, 0x1e));
                p.drawText(QRect(22, y, width() - 24, m_row), Qt::AlignVCenter, it.text);
                lastCatY = cy;
                lastChildY = -1;
            } else {
                p.setPen(dots);
                p.drawLine(28, cy, 38, cy);
                lastChildY = cy;
                const int tw = fm.horizontalAdvance(it.text) + 4;
                const QRect tr(40, y + 1, tw, m_row - 2);
                const bool sel = it.page == m_page;
                if (sel) {
                    p.fillRect(tr, QColor(0x33, 0x99, 0xff));
                }
                QFont f = font();
                f.setUnderline(!sel && i == m_hover);
                p.setFont(f);
                p.setPen(sel ? Qt::white : QColor(0x00, 0x66, 0xcc));
                p.drawText(tr.adjusted(2, 0, 0, 0), Qt::AlignVCenter, it.text);
                p.setFont(font());
            }
        }
        if (lastCatY >= 0 && lastChildY >= 0) {
            p.setPen(dots);
            p.drawLine(28, lastCatY + 5, 28, lastChildY);
        }
        // children of every category: the vertical line from the box down to the last child
        p.setPen(dots);
        int cat = -1;
        for (int i = 0; i <= m_items.size(); ++i) {
            if (i == m_items.size() || m_items.at(i).page < 0) {
                if (cat >= 0 && i - 1 > cat) {
                    p.drawLine(28, 4 + cat * m_row + m_row / 2 + 1, 28, 4 + (i - 1) * m_row + m_row / 2);
                }
                cat = i;
            }
        }
        // between the categories
        int prev = -1;
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items.at(i).page < 0) {
                if (prev >= 0) {
                    p.drawLine(10, 4 + prev * m_row + m_row / 2 + 5, 10, 4 + i * m_row + m_row / 2 - 5);
                }
                prev = i;
            }
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int i = itemAt(e->position().toPoint());
        const bool link = i >= 0 && m_items.at(i).page >= 0;
        setCursor(link ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (i != m_hover) {
            m_hover = link ? i : -1;
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
        const int i = itemAt(e->position().toPoint());
        if (i >= 0 && m_items.at(i).page >= 0 && activated) {
            activated(m_items.at(i).page);
        }
    }

private:
    int itemAt(const QPoint &pos) const
    {
        const int i = (pos.y() - 4) / m_row;
        if (pos.y() < 4 || i < 0 || i >= m_items.size()) {
            return -1;
        }
        if (m_items.at(i).page >= 0 && pos.x() > 40 + QFontMetrics(font()).horizontalAdvance(m_items.at(i).text) + 6) {
            return -1;
        }
        return i;
    }
    QList<Item> m_items;
    const int m_row = 16;
    int m_page = -1;
    int m_hover = -1;
};

/* ---------------- the page banner: icon, big title, "Restore Defaults" ---------------- */

class Banner : public QWidget
{
public:
    std::function<void()> restore;

    Banner(const QString &title, const QString &icon)
        : m_title(title)
        , m_icon(asset(icon))
    {
        setFixedHeight(50);
        setMouseTracking(true);
    }
    void setRestoreVisible(bool on)
    {
        m_restoreVisible = on;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QLinearGradient g(0, 0, width(), 0);
        g.setColorAt(0, QColor(0x96, 0xb4, 0xd5));
        g.setColorAt(0.45, QColor(0x9e, 0xba, 0xd8));
        g.setColorAt(1, QColor(0xb6, 0xcf, 0xeb));
        p.fillRect(rect(), g);
        p.drawImage(10, (height() - m_icon.height()) / 2 - 2, m_icon);
        QFont f(QStringLiteral("Trebuchet MS"));
        f.setPixelSize(23);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0x0a, 0x0a, 0x0e));
        // a long title gives way to "Restore Defaults" under its right end
        const int room = width() - 76 - (m_restoreVisible ? 0 : 0);
        p.drawText(QRect(66, 0, room, height() - 6), Qt::AlignVCenter, QFontMetrics(f).elidedText(m_title, Qt::ElideRight, room));
        if (m_restoreVisible) {
            QFont r = font();
            r.setUnderline(m_hot);
            p.setFont(r);
            p.setPen(QColor(0x1e, 0x1e, 0x1e));
            p.drawText(restoreRect(), Qt::AlignRight | Qt::AlignVCenter, restoreText());
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const bool hot = m_restoreVisible && restoreRect().contains(e->position().toPoint());
        if (hot != m_hot) {
            m_hot = hot;
            setCursor(hot ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hot = false;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (m_restoreVisible && restoreRect().contains(e->position().toPoint()) && restore) {
            restore();
        }
    }

private:
    static QString restoreText()
    {
        return QStringLiteral("Відновити стандартні параметри");
    }
    QRect restoreRect() const
    {
        const int w = QFontMetrics(font()).horizontalAdvance(restoreText()) + 2;
        return QRect(width() - w - 12, height() - 20, w, 18);
    }
    QString m_title;
    QImage m_icon;
    bool m_restoreVisible = true;
    bool m_hot = false;
};

/** "I would like to use the following 3D settings:" — a rounded panel fading from grey to white */
class RoundPanel : public QWidget
{
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, width(), 0);
        g.setColorAt(0, QColor(0xef, 0xef, 0xef));
        g.setColorAt(0.7, QColor(0xfa, 0xfa, 0xfa));
        g.setColorAt(1, QColor(0xff, 0xff, 0xff));
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), 12, 12);
        p.fillPath(path, g);
    }
};

/* ---------------- the settings list: Feature | Setting, a drop-down on the selected row ---------------- */

class SettingsTable : public QTreeWidget
{
public:
    std::function<int(const QString &)> value;           // choice index, -1 = global
    std::function<void(const QString &, int)> changed;
    std::function<QString(const QString &)> globalText;  // per program: "Use global setting (X)"
    std::function<void(const Feature &)> hovered;
    bool perProgram = false;

    SettingsTable()
    {
        setColumnCount(2);
        setHeaderLabels({QStringLiteral("Функція"), QStringLiteral("Параметр")});
        setRootIsDecorated(false);
        setUniformRowHeights(true);
        setAllColumnsShowFocus(true);
        header()->setSectionsMovable(false);
        header()->setStretchLastSection(true);
        setIndentation(0);
        connect(this, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *cur, QTreeWidgetItem *prev) {
            if (prev) {
                removeItemWidget(prev, 1);
            }
            if (cur) {
                const Feature *f = feature(cur->data(0, Qt::UserRole).toString());
                if (f && hovered) {
                    hovered(*f);
                }
                if (f && editable(*f)) {
                    showEditor(cur, *f);
                }
            }
        });
    }
    void reload()
    {
        const QString keep = currentItem() ? currentItem()->data(0, Qt::UserRole).toString() : QString();
        clear();
        for (const Feature &f : features()) {
            auto *it = new QTreeWidgetItem(this, {f.label, text(f)});
            it->setData(0, Qt::UserRole, f.id);
            const bool grey = !f.supported() || (perProgram && f.globalOnly) || (f.id == QLatin1String("aaset") && !editable(f));
            for (int c = 0; c < 2; ++c) {
                it->setForeground(c, grey ? QColor(0x8d, 0x8d, 0x8d) : QColor(Qt::black));
            }
            if (perProgram && value(f.id) >= 0) {
                QFont b = font();
                b.setBold(true);
                it->setFont(1, b); // like the Windows panel: settings of the program itself in bold
            }
            if (f.id == keep) {
                setCurrentItem(it);
            }
        }
        setColumnWidth(0, qMax(240, int(viewport()->width() * 0.52)));
    }

private:
    bool editable(const Feature &f) const
    {
        if (!f.supported() || f.choices.size() < 2 || (perProgram && f.globalOnly)) {
            return false;
        }
        if (f.id == QLatin1String("aaset")) {
            int mode = value(QStringLiteral("aamode"));
            return mode == 1;
        }
        return true;
    }
    QString text(const Feature &f) const
    {
        if (!f.supported()) {
            return perProgram ? QStringLiteral("Не підтримується для цієї програми") : QStringLiteral("Не підтримується в Linux");
        }
        if (perProgram && f.globalOnly) {
            return QStringLiteral("Не підтримується для цієї програми");
        }
        const int v = value(f.id);
        if (v < 0) {
            return globalText(f.id);
        }
        if (f.id == QLatin1String("aaset") && value(QStringLiteral("aamode")) != 1) {
            return perProgram ? globalText(f.id) : f.choices.first().label;
        }
        return f.choices.value(v).label;
    }
    void showEditor(QTreeWidgetItem *it, const Feature &f)
    {
        auto *box = new QComboBox;
        box->setFrame(false);
        if (perProgram) {
            box->addItem(globalText(f.id), -1);
        }
        for (int i = 0; i < f.choices.size(); ++i) {
            if (f.id == QLatin1String("aaset") && i == 0) {
                continue;
            }
            box->addItem(f.choices.at(i).label, i);
        }
        box->setCurrentIndex(qMax(0, box->findData(value(f.id))));
        const QString id = f.id;
        connect(box, &QComboBox::activated, this, [this, box, id] {
            const int v = box->currentData().toInt();
            if (changed) {
                changed(id, v);
            }
            QTimer::singleShot(0, this, [this] {
                reload();
            });
        });
        setItemWidget(it, 1, box);
    }
};

/* ---------------- the preview: NVIDIA's own scene (nvscene.h), turning ---------------- */

class Preview : public QOpenGLWidget
{
public:
    int quality = 2; // 0 performance (no antialiasing), 1 balanced, 2 quality
    double angle = -22;

    Preview()
    {
        m_timer.setInterval(30);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            angle += 1.0;
            if (angle >= 360) {
                angle -= 360;
            }
            update();
        });
        m_timer.start();
    }
    bool paused() const
    {
        return !m_timer.isActive();
    }
    void setPaused(bool p)
    {
        p ? m_timer.stop() : m_timer.start();
    }

protected:
    void initializeGL() override
    {
        m_scene.init();
    }
    void paintGL() override
    {
        const qreal dpr = devicePixelRatioF();
        m_scene.render(int(width() * dpr), int(height() * dpr), angle, quality, defaultFramebufferObject());
    }
    void hideEvent(QHideEvent *) override
    {
        m_wasRunning = m_timer.isActive();
        m_timer.stop();
    }
    void showEvent(QShowEvent *) override
    {
        if (m_wasRunning) {
            m_timer.start();
        }
    }

private:
    Scene m_scene;
    QTimer m_timer;
    bool m_wasRunning = true;
};

/* ---------------- Windows 7 trackbar (aero.msstyles TRACKBAR: track, the thumb pointing down, tics) ---------------- */

class Win7Slider : public QSlider
{
public:
    Win7Slider()
        : QSlider(Qt::Horizontal)
    {
        m_thumbs = QImage(asset(QStringLiteral("trackbar-thumb.png")));
        m_track = QImage(asset(QStringLiteral("trackbar-track.png")));
        setMouseTracking(true);
        setFixedHeight(30);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int tw = 11, th = 19;
        const int left = tw / 2 + 1, right = width() - tw / 2 - 2;
        // the track: 4 px, the 3x3 image with a 1 px border
        const QRect track(left - 2, 9, right - left + 5, 4);
        p.drawImage(QRect(track.left(), track.top(), 1, 1), m_track, QRect(0, 0, 1, 1));
        p.drawImage(QRect(track.right(), track.top(), 1, 1), m_track, QRect(2, 0, 1, 1));
        p.drawImage(QRect(track.left(), track.bottom(), 1, 1), m_track, QRect(0, 2, 1, 1));
        p.drawImage(QRect(track.right(), track.bottom(), 1, 1), m_track, QRect(2, 2, 1, 1));
        p.drawImage(QRect(track.left() + 1, track.top(), track.width() - 2, 1), m_track, QRect(1, 0, 1, 1));
        p.drawImage(QRect(track.left() + 1, track.bottom(), track.width() - 2, 1), m_track, QRect(1, 2, 1, 1));
        p.drawImage(QRect(track.left(), track.top() + 1, 1, track.height() - 2), m_track, QRect(0, 1, 1, 1));
        p.drawImage(QRect(track.right(), track.top() + 1, 1, track.height() - 2), m_track, QRect(2, 1, 1, 1));
        p.drawImage(QRect(track.left() + 1, track.top() + 1, track.width() - 2, track.height() - 2), m_track, QRect(1, 1, 1, 1));
        // tics under the track, one per value
        p.setPen(QColor(0xc4, 0xc4, 0xc4));
        const int span = qMax(1, maximum() - minimum());
        for (int v = minimum(); v <= maximum(); ++v) {
            const int x = left + (right - left) * (v - minimum()) / span;
            p.drawLine(x, 24, x, 26);
        }
        // the thumb: normal, hot, pressed, focused, disabled (11x19 each)
        const int x = left + (right - left) * (value() - minimum()) / span;
        const int state = !isEnabled() ? 4 : (isSliderDown() ? 2 : (m_hot ? 1 : (hasFocus() ? 3 : 0)));
        p.drawImage(QPoint(x - tw / 2, 2), m_thumbs, QRect(0, state * th, tw, th));
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
        setValue(valueAt(e->position().x()));
        setSliderDown(true);
        update();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (isSliderDown()) {
            setValue(valueAt(e->position().x()));
        }
    }
    void mouseReleaseEvent(QMouseEvent *) override
    {
        setSliderDown(false);
        update();
    }

private:
    int valueAt(double px) const
    {
        const int left = 6, right = width() - 7;
        const double t = qBound(0.0, (px - left) / qMax(1, right - left), 1.0);
        return minimum() + qRound(t * (maximum() - minimum()));
    }
    QImage m_thumbs, m_track;
    bool m_hot = false;
};

/* ---------------- Set PhysX configuration: the processors and the display ---------------- */

class PhysXDiagram : public QWidget
{
public:
    int choice = 0; // 0 auto (the NVIDIA GPU), 1 the NVIDIA GPU, 2 the CPU

    PhysXDiagram()
    {
        setFixedSize(390, 210);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool cpu = choice == 2;
        // boxes: [CPU] NVIDIA GPU, integrated graphics
        QList<QPair<QString, QRectF>> boxes;
        const double bw = cpu ? 120 : 185, gap = cpu ? 8 : 10;
        double x = 6;
        if (cpu) {
            boxes.append({QStringLiteral("ЦП"), QRectF(x, 116, bw, 96)});
            x += bw + gap;
        }
        boxes.append({gpuName(), QRectF(x, 116, bw, 96)});
        x += bw + gap;
        boxes.append({integratedName(), QRectF(x, 116, bw, 96)});
        const QRectF physxBox = boxes.at(0).second; // the CPU when chosen, otherwise the NVIDIA GPU

        // the PhysX banner with a pointer down to the processor that runs it
        QPainterPath banner;
        banner.addRoundedRect(QRectF(0, 0, width() - 6, 92), 8, 8);
        QPainterPath pointer;
        const double px = physxBox.center().x();
        pointer.moveTo(px - 16, 91);
        pointer.lineTo(px, 106);
        pointer.lineTo(px + 16, 91);
        pointer.closeSubpath();
        banner = banner.united(pointer);
        QLinearGradient bg(0, 0, 0, 100);
        bg.setColorAt(0, QColor(0xe4, 0xef, 0xf9));
        bg.setColorAt(1, QColor(0xc9, 0xdd, 0xf0));
        p.fillPath(banner, bg);
        p.setPen(QColor(0x3a, 0x4a, 0x5a));
        p.drawText(QRectF(0, 6, width() - 6, 18), Qt::AlignHCenter, QStringLiteral("PhysX"));

        for (int i = 0; i < boxes.size(); ++i) {
            const QRectF r = boxes.at(i).second;
            // the card: blue-grey, darker towards the bottom; a light screen with a grey frame
            QLinearGradient cg(0, r.top(), 0, r.bottom());
            cg.setColorAt(0, QColor(0xa9, 0xb6, 0xc8));
            cg.setColorAt(0.6, QColor(0x9a, 0xa7, 0xbb));
            cg.setColorAt(1, QColor(0x8c, 0x98, 0xae));
            p.fillRect(r, cg);
            const QRectF scr(r.center().x() - 47, r.top() + 6, 94, 56);
            QLinearGradient sg(0, scr.top(), 0, scr.bottom());
            sg.setColorAt(0, QColor(0xf2, 0xf3, 0xf5));
            sg.setColorAt(1, QColor(0xdc, 0xe0, 0xe6));
            p.fillRect(scr, sg);
            p.setPen(QColor(0x7c, 0x84, 0x90));
            p.drawRect(scr);
            QFont f = font();
            f.setPixelSize(11);
            f.setBold(true);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(r.adjusted(2, 66, -2, -4), Qt::AlignCenter | Qt::TextWordWrap, boxes.at(i).first);
        }
        // the laptop display, on the integrated graphics (PRIME: the NVIDIA GPU has no screen of its own)
        const QRectF igpu = boxes.last().second;
        const QRectF disp(igpu.center().x() - 36, 28, 72, 50);
        // a dark bezel, a glossy green screen with a light edge (as drawn by the Windows panel)
        p.fillRect(disp, QColor(0x2a, 0x2e, 0x2a));
        const QRectF glass = disp.adjusted(3, 3, -3, -3);
        QLinearGradient dg(0, glass.top(), 0, glass.bottom());
        dg.setColorAt(0, QColor(0x55, 0x82, 0x2c));
        dg.setColorAt(0.5, QColor(0x3e, 0x6a, 0x1c));
        dg.setColorAt(1, QColor(0x2f, 0x58, 0x12));
        p.fillRect(glass, dg);
        p.setPen(QColor(0x9a, 0xc0, 0x78));
        p.drawRect(glass.adjusted(-0.5, -0.5, 0.5, 0.5));
        p.setPen(Qt::white);
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.drawText(disp.adjusted(2, 2, -2, -2), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, QStringLiteral("Дисплей ноутбука"));
        p.setPen(QPen(QColor(0x50, 0x50, 0x50), 1));
        p.drawLine(QPointF(disp.center().x(), disp.bottom()), QPointF(disp.center().x(), igpu.top() + 20));
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(disp.center().x(), igpu.top() + 22), 8, 4);
        p.setBrush(QColor(0x30, 0x30, 0x30));
        p.drawRect(QRectF(disp.center().x() - 5, igpu.top() + 21, 10, 2));
    }
};
}
