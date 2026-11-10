/*
 * seven-igfx: Intel® HD Graphics Control Panel (driver 15.33 for Windows 7, 2014-2016 look) for the Intel GPU
 * of a Linux system: blue header picture, tiles on the home page (Display, 3D, Power, Options and Support,
 * Profiles), side tabs and the round app-bar buttons. Pictures come from the user's own Intel package
 * (~/.local/share/seven-igfx); what the pages change is the shared backend of seven-ccc (kscreen, XRandR gamma,
 * power-profiles-daemon, Mesa driconf).
 *
 * seven-igfx [--page display|3d|power|options|profiles] [--screenshot FILE] [--apply]
 * WIN7_IGFX_ANY_GPU=1 runs it without an Intel GPU (for testing).
 */
#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDesktopServices>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <cmath>
#include <functional>

#include "cccbackend.h"

using namespace Ccc;

namespace Igfx
{

const QColor Blue(0x00, 0x71, 0xC5), LightBlue(0x00, 0xAE, 0xEF), DarkBlue(0x00, 0x42, 0x80), Grey(0x93, 0x95, 0x98), MidGrey(0xBB, 0xBB, 0xBB),
    LightGrey(0xE2, 0xE2, 0xE2), VeryLightGrey(0xF5, 0xF5, 0xF5);

inline QString assets()
{
    return QDir::homePath() + QStringLiteral("/.local/share/seven-igfx/");
}

inline QPixmap pic(const QString &name)
{
    static QHash<QString, QPixmap> cache;
    if (!cache.contains(name)) {
        cache.insert(name, QPixmap(assets() + name));
    }
    return cache.value(name);
}

inline QFont ufont(int px, int weight = QFont::Normal)
{
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(px);
    f.setWeight(QFont::Weight(weight));
    return f;
}

inline QSettings &settings()
{
    static QSettings s(QDir::homePath() + QStringLiteral("/.config/seven-igfxrc"), QSettings::IniFormat);
    return s;
}

inline Gpu intelGpu()
{
    for (const Gpu &g : gpus()) {
        if (g.vendor == VendorIntel) {
            return g;
        }
    }
    if (qEnvironmentVariableIsSet("WIN7_IGFX_ANY_GPU") && !gpus().isEmpty()) {
        return gpus().first();
    }
    return {};
}

/* ---------------------------------------------------------------- chrome */

/* Marlett-like caption button (0 minimise, 1 maximise, r close) on the white corner of the header */
class CaptionButton : public QAbstractButton
{
public:
    enum Kind { Minimize, Maximize, Close };
    explicit CaptionButton(Kind k, QWidget *p)
        : QAbstractButton(p)
        , m_k(k)
    {
        setFixedWidth(30);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        if (underMouse()) {
            p.fillRect(rect(), m_k == Close ? QColor(0xE8, 0x11, 0x23) : LightGrey);
        }
        p.setPen(QPen(underMouse() && m_k == Close ? Qt::white : DarkBlue, 1.6));
        // a 9 px glyph centred in the white strip
        const QRectF r(width() / 2.0 - 4.5, height() / 2.0 - 4.5, 9, 9);
        if (m_k == Minimize) {
            p.drawLine(QPointF(r.left(), r.bottom()), QPointF(r.right(), r.bottom()));
        } else if (m_k == Maximize) {
            p.drawRect(r);
            p.drawLine(QPointF(r.left(), r.top() + 1.5), QPointF(r.right(), r.top() + 1.5));
        } else {
            p.setRenderHint(QPainter::Antialiasing);
            p.drawLine(r.topLeft(), r.bottomRight());
            p.drawLine(r.bottomLeft(), r.topRight());
        }
    }

private:
    Kind m_k;
};

/* header: the blue picture (columns 0..3 of 5), back arrow on sub pages, title, section menu arrow, caption buttons */
class Header : public QWidget
{
public:
    std::function<void()> back, menu;
    explicit Header(QWidget *p)
        : QWidget(p)
    {
        setMouseTracking(true);
        setMinimumHeight(80);
        minimize = new CaptionButton(CaptionButton::Minimize, this);
        maximize = new CaptionButton(CaptionButton::Maximize, this);
        close = new CaptionButton(CaptionButton::Close, this);
    }
    CaptionButton *minimize, *maximize, *close;
    void set(const QString &title, bool sub)
    {
        m_title = title;
        m_sub = sub;
        update();
    }

protected:
    QRect backRect() const
    {
        const int s = qMin(38, height() / 2 - 4);
        return QRect(int(width() * 0.25 / 13.4) + 4, height() / 2 + (height() / 2 - s) / 2, s, s);
    }
    int notchHeight() const { return qRound(height() * 34.0 / 122.0); }
    QRect menuRect() const { return QRect(m_titleEnd + 8, height() / 2 + (height() / 2 - 27) / 2, 33, 27); }
    void resizeEvent(QResizeEvent *) override
    {
        // the buttons fill the white strip at the top right of the picture (rows 0..33 of 122)
        const int strip = notchHeight();
        for (CaptionButton *b : {close, maximize, minimize}) {
            b->setFixedHeight(strip);
        }
        close->move(width() - 34, 0);
        maximize->move(close->x() - 30, 0);
        minimize->move(maximize->x() - 30, 0);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), Qt::white);
        const int w = int(width() * (0.25 + 0.65 + 10 + 0.5) / 13.4);
        const QPixmap bg = pic(QStringLiteral("header_background.png")).scaled(w, height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap(0, 0, bg);
        // the picture ends with a white notch at the top right: its last column is stretched to the window edge,
        // so the blue and the notch line continue without a step
        p.drawPixmap(QRect(w, 0, width() - w, height()), bg, QRect(bg.width() - 1, 0, 1, bg.height()));
        if (m_sub) {
            p.drawPixmap(backRect(), pic(m_backHover ? QStringLiteral("back_arrow_mo.png") : QStringLiteral("back_arrow.png")));
        }
        p.setPen(Qt::white);
        const int x = int(width() * (0.25 + 0.65) / 13.4) + 10;
        QFont f = ufont(m_sub ? 30 : 34, QFont::Light);
        p.setFont(f);
        const QRect tr(x, height() / 2 - 8, width() - x - 120, height() / 2 + 4);
        p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, m_title);
        m_titleEnd = x + QFontMetrics(f).horizontalAdvance(m_title);
        if (m_sub) {
            p.drawPixmap(menuRect(), pic(m_menuHover ? QStringLiteral("headermenu_arrow_mo.png") : QStringLiteral("headermenu_arrow_rest.png")));
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const bool b = m_sub && backRect().contains(e->position().toPoint());
        const bool m = m_sub && menuRect().contains(e->position().toPoint());
        if (b != m_backHover || m != m_menuHover) {
            m_backHover = b;
            m_menuHover = m;
            setCursor(b || m ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        if (m_sub && backRect().contains(e->position().toPoint()) && back) {
            back();
        } else if (m_sub && menuRect().contains(e->position().toPoint()) && menu) {
            menu();
        } else {
            window()->windowHandle()->startSystemMove();
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *) override { window()->isMaximized() ? window()->showNormal() : window()->showMaximized(); }

private:
    QString m_title;
    bool m_sub = false, m_backHover = false, m_menuHover = false;
    int m_titleEnd = 0;
};

/* home page tile: Apphub_Tile background, the big icon, white title at the top right; dark blue frame on hover */
class Tile : public QAbstractButton
{
public:
    QString icon;
    Tile(const QString &title, const QString &iconFile)
        : icon(iconFile)
    {
        setText(title);
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w * 200 / 280; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QRect r = rect();
        const int h = qMin(r.height(), r.width() * 200 / 280);
        r = QRect(r.x(), r.y() + (r.height() - h) / 2, r.width(), h);
        const QRect in = r.adjusted(7, 7, -7, -7);
        p.drawPixmap(in, pic(QStringLiteral("apphub_tile.png")));
        p.drawPixmap(in, pic(icon));
        p.setPen(Qt::white);
        p.setFont(ufont(qMax(14, in.height() / 9)));
        p.drawText(in.adjusted(5, 10, -10, 0), Qt::AlignRight | Qt::AlignTop, text());
        if (underMouse() || isDown()) {
            p.setPen(QPen(DarkBlue, 5));
            p.drawRect(QRectF(r).adjusted(2.5, 2.5, -2.5, -2.5));
            p.setPen(QPen(MidGrey, 2.5));
            p.drawRect(QRectF(r).adjusted(6.25, 6.25, -6.25, -6.25));
        }
    }
};

/* round app-bar button with caption (AppBar_*_rest/mo/pressed/disable.png) */
class AppButton : public QAbstractButton
{
public:
    AppButton(const QString &key, const QString &text)
        : m_key(key)
    {
        setText(text);
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(qMax(76, QFontMetrics(ufont(11)).horizontalAdvance(text) + 10), 64);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QString state = !isEnabled() ? QStringLiteral("disable") : isDown() ? QStringLiteral("pressed") : underMouse() ? QStringLiteral("mo") : QStringLiteral("rest");
        p.drawPixmap(QRect((width() - 40) / 2, 2, 40, 40), pic(QStringLiteral("appbar_%1_%2.png").arg(m_key, state)));
        p.setPen(isEnabled() ? QColor(0x33, 0x33, 0x33) : MidGrey);
        p.setFont(ufont(11));
        p.drawText(QRect(0, 44, width(), 18), Qt::AlignHCenter | Qt::AlignTop, text());
    }

private:
    QString m_key;
};

/* side tabs: blue text, current one bold with the arrow */
class SideTabs : public QWidget
{
public:
    std::function<void(int)> chosen;
    explicit SideTabs(const QStringList &names)
        : m_names(names)
    {
        setFixedWidth(260);
        setMouseTracking(true);
    }
    void setCurrent(int i)
    {
        m_cur = i;
        update();
    }

protected:
    QRect itemRect(int i) const { return QRect(20, 20 + i * 38, width() - 30, 34); }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        for (int i = 0; i < m_names.size(); ++i) {
            const QRect r = itemRect(i);
            if (i == m_cur) {
                p.fillRect(r, VeryLightGrey);
                p.fillRect(QRect(r.left(), r.top(), 4, r.height()), Blue);
            }
            p.setPen(i == m_cur ? DarkBlue : (i == m_hover ? LightBlue : Blue));
            p.setFont(ufont(15, i == m_cur ? QFont::DemiBold : QFont::Normal));
            p.drawText(r.adjusted(12, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter, m_names.at(i));
        }
        p.setPen(LightGrey);
        p.drawLine(width() - 1, 20, width() - 1, height() - 20);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        int h = -1;
        for (int i = 0; i < m_names.size(); ++i) {
            if (itemRect(i).contains(e->position().toPoint())) {
                h = i;
            }
        }
        if (h != m_hover) {
            m_hover = h;
            setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
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
        for (int i = 0; i < m_names.size(); ++i) {
            if (itemRect(i).contains(e->position().toPoint()) && chosen) {
                chosen(i);
            }
        }
    }

private:
    QStringList m_names;
    int m_cur = 0, m_hover = -1;
};

inline QLabel *heading(const QString &t)
{
    auto *l = new QLabel(t);
    l->setFont(ufont(20, QFont::Light));
    l->setStyleSheet(QStringLiteral("color:#0071c5"));
    return l;
}

inline QLabel *text(const QString &t, bool blue = false)
{
    auto *l = new QLabel(t);
    l->setFont(ufont(13));
    l->setWordWrap(true);
    if (blue) {
        l->setStyleSheet(QStringLiteral("color:#0071c5"));
    }
    return l;
}

/* ---------------------------------------------------------------- tabs (one settings panel each) */

class Tab : public QWidget
{
public:
    QString name;
    std::function<void()> changed;
    virtual void load() {}
    virtual bool dirty() const { return false; }
    virtual bool apply() { return true; }
    virtual void defaults() {}
    virtual bool hasApply() const { return true; }
    QVBoxLayout *v;
    Tab()
    {
        v = new QVBoxLayout(this);
        v->setContentsMargins(30, 20, 30, 10);
        v->setSpacing(8);
    }
    void notify()
    {
        if (changed) {
            changed();
        }
    }
};

inline QString hz(double r)
{
    return QStringLiteral("%1p Гц").arg(qRound(r)).replace(QStringLiteral("p"), QString());
}

inline QString rotName(int r)
{
    return QString::number(r == 2 ? 90 : r == 4 ? 180 : r == 8 ? 270 : 0);
}

class DisplayGeneralTab : public Tab
{
public:
    DisplayGeneralTab()
    {
        name = QStringLiteral("Загальні параметри");
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(30);
        g->setVerticalSpacing(10);
        display = new QComboBox;
        res = new QComboBox;
        rate = new QComboBox;
        g->addWidget(text(QStringLiteral("Вибір дисплея"), true), 0, 0);
        g->addWidget(display, 1, 0);
        g->addWidget(text(QStringLiteral("Роздільна здатність"), true), 2, 0);
        g->addWidget(res, 3, 0);
        g->addWidget(text(QStringLiteral("Частота оновлення"), true), 4, 0);
        g->addWidget(rate, 5, 0);
        g->addWidget(text(QStringLiteral("Поворот"), true), 0, 1);
        auto *rv = new QHBoxLayout;
        rot = new QButtonGroup(this);
        for (int r : {1, 2, 4, 8}) {
            auto *b = new QRadioButton(rotName(r));
            b->setFont(ufont(13));
            rot->addButton(b, r);
            rv->addWidget(b);
        }
        rv->addStretch(1);
        g->addLayout(rv, 1, 1);
        preview = new QLabel;
        preview->setFixedSize(250, 190);
        preview->setAlignment(Qt::AlignCenter);
        g->addWidget(preview, 2, 1, 5, 1, Qt::AlignTop);
        g->setColumnStretch(2, 1);
        g->setRowStretch(6, 1);
        v->addLayout(g);
        v->addStretch(1);
        for (QComboBox *c : {display, res, rate}) {
            c->setMinimumWidth(240);
        }
        QObject::connect(display, &QComboBox::currentIndexChanged, this, [this] { fill(); });
        QObject::connect(res, &QComboBox::currentIndexChanged, this, [this] { fillRates(); notify(); });
        QObject::connect(rate, &QComboBox::currentIndexChanged, this, [this] { notify(); });
        QObject::connect(rot, &QButtonGroup::idClicked, this, [this] { showPreview(); notify(); });
    }
    void load() override
    {
        list.clear();
        QSignalBlocker b(display);
        display->clear();
        for (const Output &o : outputs()) {
            if (o.enabled) {
                list << o;
                display->addItem(o.builtIn() ? QStringLiteral("Вбудований дисплей") : QStringLiteral("Цифровий дисплей (%1)").arg(o.name), o.name);
            }
        }
        fill();
    }
    bool dirty() const override { return rate->currentData().toString() != o.currentMode || rot->checkedId() != o.rotation; }
    bool apply() override
    {
        const Output old = o;
        kscreen({QStringLiteral("output.%1.mode.%2").arg(o.name, rate->currentData().toString()), QStringLiteral("output.%1.rotation.%2").arg(o.name, rotationWord(rot->checkedId()))});
        QMessageBox box(QMessageBox::Question, QStringLiteral("Панель керування графікою Intel® HD"), QStringLiteral("Зберегти нові параметри?"), QMessageBox::Yes | QMessageBox::No, this);
        box.button(QMessageBox::Yes)->setText(QStringLiteral("Так"));
        box.button(QMessageBox::No)->setText(QStringLiteral("Ні"));
        QTimer::singleShot(15000, &box, &QMessageBox::reject);
        if (box.exec() != QMessageBox::Yes) {
            kscreen({QStringLiteral("output.%1.mode.%2").arg(old.name, old.currentMode), QStringLiteral("output.%1.rotation.%2").arg(old.name, rotationWord(old.rotation))});
        }
        load();
        return true;
    }
    void defaults() override
    {
        if (!o.preferredMode.isEmpty()) {
            const Mode m = o.mode(o.preferredMode);
            res->setCurrentIndex(qMax(0, res->findData(QSize(m.w, m.h))));
            rate->setCurrentIndex(qMax(0, rate->findData(o.preferredMode)));
        }
        if (auto *b = rot->button(1)) {
            b->setChecked(true);
        }
        showPreview();
        notify();
    }

private:
    void fill()
    {
        const QString n = display->currentData().toString();
        for (const Output &x : list) {
            if (x.name == n) {
                o = x;
            }
        }
        QSignalBlocker b(res);
        res->clear();
        QList<QSize> seen;
        for (const Mode &m : o.modes) {
            if (!seen.contains(QSize(m.w, m.h))) {
                seen << QSize(m.w, m.h);
            }
        }
        std::sort(seen.begin(), seen.end(), [](QSize a, QSize b) { return a.width() * a.height() > b.width() * b.height(); });
        const Mode cm = o.mode(o.currentMode);
        for (QSize s : seen) {
            res->addItem(QStringLiteral("%1 x %2").arg(s.width()).arg(s.height()), s);
        }
        res->setCurrentIndex(qMax(0, res->findData(QSize(cm.w, cm.h))));
        fillRates();
        rate->setCurrentIndex(qMax(0, rate->findData(o.currentMode)));
        if (auto *btn = rot->button(o.rotation)) {
            btn->setChecked(true);
        }
        showPreview();
        notify();
    }
    void fillRates()
    {
        QSignalBlocker b(rate);
        const QString keep = rate->currentData().toString();
        rate->clear();
        const QSize s = res->currentData().toSize();
        for (const Mode &m : o.modes) {
            if (QSize(m.w, m.h) == s) {
                rate->addItem(QStringLiteral("%1 Гц").arg(qRound(m.hz)), m.id);
            }
        }
        rate->setCurrentIndex(qMax(0, rate->findData(keep)));
    }
    void showPreview()
    {
        // the rotated sample picture of the original panel (unicycle_balloons_360x360_<angle>.bmp)
        const int r = rot->checkedId();
        const QString a = r == 2 ? QStringLiteral("90") : r == 4 ? QStringLiteral("180") : r == 8 ? QStringLiteral("270") : QStringLiteral("0");
        preview->setPixmap(pic(QStringLiteral("unicycle_balloons_360x360_%1.bmp").arg(a)).scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    QList<Output> list;
    Output o;
    QComboBox *display, *res, *rate;
    QButtonGroup *rot;
    QLabel *preview;
};

class ColorTab : public Tab
{
public:
    ColorTab()
    {
        name = QStringLiteral("Колір");
        auto *top = new QHBoxLayout;
        display = new QComboBox;
        display->setMinimumWidth(240);
        top->addWidget(text(QStringLiteral("Вибір дисплея"), true));
        top->addWidget(display);
        top->addStretch(1);
        v->addLayout(top);
        auto *chs = new QHBoxLayout;
        group = new QButtonGroup(this);
        const QStringList cols{QStringLiteral("colorenhance_all.png"), QStringLiteral("colorenhance_red.png"), QStringLiteral("colorenhance_green.png"), QStringLiteral("colorenhance_blue.png")};
        const QStringList tips{QStringLiteral("Усі кольори"), QStringLiteral("Червоний"), QStringLiteral("Зелений"), QStringLiteral("Синій")};
        for (int i = 0; i < 4; ++i) {
            auto *b = new QPushButton;
            b->setCheckable(true);
            b->setIcon(QIcon(pic(cols.at(i))));
            b->setIconSize(QSize(50, 18));
            b->setToolTip(tips.at(i));
            b->setFixedSize(64, 28);
            group->addButton(b, i);
            chs->addWidget(b);
        }
        group->button(0)->setChecked(true);
        chs->addStretch(1);
        v->addLayout(chs);
        auto *g = new QGridLayout;
        g->setVerticalSpacing(4);
        g->setHorizontalSpacing(12);
        auto row = [&](int r, const QString &n, int lo, int hi) {
            g->addWidget(text(n, true), r, 0, 1, 2);
            auto *s = new QSlider(Qt::Horizontal);
            s->setRange(lo, hi);
            auto *sp = new QSpinBox;
            sp->setRange(lo, hi);
            g->addWidget(s, r + 1, 0);
            g->addWidget(sp, r + 1, 1);
            QObject::connect(s, &QSlider::valueChanged, sp, &QSpinBox::setValue);
            QObject::connect(sp, &QSpinBox::valueChanged, s, &QSlider::setValue);
            QObject::connect(sp, &QSpinBox::valueChanged, this, [this] {
                if (!m_loading) {
                    edit();
                }
            });
            return sp;
        };
        bright = row(0, QStringLiteral("Яскравість"), -100, 100);
        contrast = row(2, QStringLiteral("Контраст"), 0, 200);
        gamma = row(4, QStringLiteral("Гамма (×100)"), 30, 280);
        g->setColumnStretch(0, 1);
        auto *h = new QHBoxLayout;
        h->addLayout(g, 1);
        h->addSpacing(30);
        sample = new QLabel;
        sample->setFixedSize(230, 175);
        h->addWidget(sample, 0, Qt::AlignTop);
        v->addLayout(h);
        v->addStretch(1);
        QObject::connect(group, &QButtonGroup::idClicked, this, [this] { show(); });
        QObject::connect(display, &QComboBox::currentIndexChanged, this, [this] { pick(); });
    }
    ~ColorTab() override
    {
        if (!(c == saved) && !out.isEmpty()) {
            applyColor(out, saved);
        }
    }
    void load() override
    {
        QSignalBlocker b(display);
        display->clear();
        for (const Output &o : outputs()) {
            if (o.enabled) {
                display->addItem(o.builtIn() ? QStringLiteral("Вбудований дисплей") : QStringLiteral("Цифровий дисплей (%1)").arg(o.name), o.name);
            }
        }
        pick();
    }
    bool dirty() const override { return !(c == saved); }
    bool apply() override
    {
        saveColor(out, c);
        applyColor(out, c);
        saved = c;
        setLoginApply(true);
        return true;
    }
    void discard()
    {
        c = saved;
        applyColor(out, c);
        show();
    }
    void defaults() override
    {
        c = Color();
        applyColor(out, c);
        show();
    }

private:
    void pick()
    {
        if (!out.isEmpty() && !(c == saved)) {
            applyColor(out, saved);
        }
        out = display->currentData().toString();
        saved = c = loadColor(out);
        show();
    }
    void show()
    {
        const int ch = qMax(0, group->checkedId() - 1);
        m_loading = true; // the sliders follow the spin boxes, but this is not an edit
        bright->setValue(c.brightness[ch]);
        contrast->setValue(c.contrast[ch]);
        gamma->setValue(qRound(c.gamma[ch] * 100));
        m_loading = false;
        // sample photo of the original panel, run through the ramp
        QImage im = pic(QStringLiteral("unicycle_balloons_360x360_0.bmp")).toImage().convertToFormat(QImage::Format_RGB32).scaled(sample->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        uchar lut[3][256];
        for (int k = 0; k < 3; ++k) {
            for (int i = 0; i < 256; ++i) {
                lut[k][i] = uchar(qRound(ramp(c, k, i / 255.0) * 255));
            }
        }
        for (int y = 0; y < im.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(im.scanLine(y));
            for (int x = 0; x < im.width(); ++x) {
                line[x] = qRgb(lut[0][qRed(line[x])], lut[1][qGreen(line[x])], lut[2][qBlue(line[x])]);
            }
        }
        sample->setPixmap(QPixmap::fromImage(im));
        notify();
    }
    void edit()
    {
        const int sel = group->checkedId();
        for (int ch = 0; ch < 3; ++ch) {
            if (sel == 0 || sel - 1 == ch) {
                c.brightness[ch] = bright->value();
                c.contrast[ch] = contrast->value();
                c.gamma[ch] = gamma->value() / 100.0;
            }
        }
        applyColor(out, c);
        show();
    }
    QComboBox *display;
    QButtonGroup *group;
    QSpinBox *bright, *contrast, *gamma;
    QLabel *sample;
    QString out;
    Color c, saved;
    bool m_loading = false;
};

class MultiDisplayTab : public Tab
{
public:
    MultiDisplayTab()
    {
        name = QStringLiteral("Кілька дисплеїв");
        v->addWidget(text(QStringLiteral("Вибір режиму відображення"), true));
        auto *h = new QHBoxLayout;
        group = new QButtonGroup(this);
        auto add = [&](int id, const QString &img, const QString &t) {
            auto *b = new QPushButton;
            b->setCheckable(true);
            b->setIcon(QIcon(pic(img)));
            b->setIconSize(QSize(200, 96));
            b->setFixedSize(220, 112);
            b->setToolTip(t);
            group->addButton(b, id);
            auto *col = new QVBoxLayout;
            col->addWidget(b);
            col->addWidget(text(t));
            h->addLayout(col);
        };
        add(0, QStringLiteral("single.png"), QStringLiteral("Один дисплей"));
        add(1, QStringLiteral("dualclone.png"), QStringLiteral("Розширений робочий стіл"));
        h->addStretch(1);
        v->addLayout(h);
        v->addSpacing(10);
        v->addWidget(text(QStringLiteral("Основний дисплей"), true));
        primary = new QComboBox;
        primary->setMaximumWidth(280);
        v->addWidget(primary);
        info = text(QString());
        v->addWidget(info);
        v->addStretch(1);
        QObject::connect(group, &QButtonGroup::idClicked, this, [this] { notify(); });
        QObject::connect(primary, &QComboBox::currentIndexChanged, this, [this] { notify(); });
    }
    void load() override
    {
        all = outputs();
        QSignalBlocker b(primary);
        primary->clear();
        int on = 0;
        for (const Output &o : all) {
            primary->addItem(o.builtIn() ? QStringLiteral("Вбудований дисплей") : QStringLiteral("Цифровий дисплей (%1)").arg(o.name), o.name);
            on += o.enabled;
            if (o.enabled && m_primary.isEmpty()) {
                m_primary = o.name;
            }
        }
        m_mode = on > 1 ? 1 : 0;
        group->button(m_mode)->setChecked(true);
        primary->setCurrentIndex(qMax(0, primary->findData(m_primary)));
        group->button(1)->setEnabled(all.size() > 1);
        info->setText(all.size() > 1 ? QString() : QStringLiteral("Зараз підключено лише один дисплей. Підключіть ще один, щоб розширити робочий стіл."));
    }
    bool dirty() const override { return group->checkedId() != m_mode || (group->checkedId() == 0 && primary->currentData().toString() != m_primary); }
    bool apply() override
    {
        QStringList args;
        const QString p = primary->currentData().toString();
        for (const Output &o : all) {
            const bool on = group->checkedId() == 1 || o.name == p;
            args << QStringLiteral("output.%1.%2").arg(o.name, on ? QStringLiteral("enable") : QStringLiteral("disable"));
        }
        args << QStringLiteral("output.%1.primary").arg(p);
        kscreen(args);
        m_primary = p;
        load();
        return true;
    }

private:
    QList<Output> all;
    QButtonGroup *group;
    QComboBox *primary;
    QLabel *info;
    int m_mode = 0;
    QString m_primary;
};

class ThreeDTab : public Tab
{
public:
    ThreeDTab()
    {
        name = QStringLiteral("Загальні параметри");
        v->addWidget(text(QStringLiteral("Вертикальна синхронізація"), true));
        vsync = new QComboBox;
        vsync->addItem(QStringLiteral("Використовувати параметри програми"), 1);
        vsync->addItem(QStringLiteral("Використовувати параметри драйвера"), 3);
        vsync->setMaximumWidth(320);
        v->addWidget(vsync);
        v->addWidget(text(QStringLiteral("«Параметри драйвера» завжди синхронізують кадри з частотою оновлення дисплея. Діє для програм OpenGL під час наступного запуску.")));
        v->addStretch(1);
        QObject::connect(vsync, &QComboBox::currentIndexChanged, this, [this] { notify(); });
    }
    void load() override
    {
        QSignalBlocker b(vsync);
        m_v = vblankMode(QStringLiteral("seven-igfx")) >= 3 ? 3 : 1;
        vsync->setCurrentIndex(qMax(0, vsync->findData(m_v)));
    }
    bool dirty() const override { return vsync->currentData().toInt() != m_v; }
    bool apply() override
    {
        const bool ok = setVblankMode(vsync->currentData().toInt(), QStringLiteral("seven-igfx"), {QStringLiteral("iris"), QStringLiteral("i965"), QStringLiteral("crocus")});
        load();
        return ok;
    }
    void defaults() override
    {
        vsync->setCurrentIndex(0);
        notify();
    }

private:
    QComboBox *vsync;
    int m_v = 1;
};

class PowerTab : public Tab
{
public:
    PowerTab()
    {
        name = QStringLiteral("План електроживлення графіки");
        state = text(QString(), true);
        v->addWidget(state);
        v->addSpacing(6);
        group = new QButtonGroup(this);
        const QList<QPair<QString, QString>> modes{{QStringLiteral("performance"), QStringLiteral("Максимальна продуктивність")},
                                                    {QStringLiteral("balanced"), QStringLiteral("Збалансований режим")},
                                                    {QStringLiteral("power-saver"), QStringLiteral("Максимальний час роботи від батареї")}};
        const QStringList have = powerProfiles();
        int i = 0;
        for (const auto &m : modes) {
            auto *r = new QRadioButton(m.second);
            r->setFont(ufont(14));
            r->setProperty("profile", m.first);
            r->setEnabled(have.contains(m.first));
            group->addButton(r, i++);
            v->addWidget(r);
        }
        v->addStretch(1);
        QObject::connect(group, &QButtonGroup::idClicked, this, [this] { notify(); });
    }
    void load() override
    {
        m_p = powerProfile();
        state->setText(onBattery() ? QStringLiteral("Зараз ноутбук працює від батареї") : QStringLiteral("Зараз ноутбук підключено до мережі"));
        for (QAbstractButton *b : group->buttons()) {
            if (b->property("profile").toString() == m_p) {
                b->setChecked(true);
            }
        }
    }
    bool dirty() const override { return group->checkedButton() && group->checkedButton()->property("profile").toString() != m_p; }
    bool apply() override
    {
        const bool ok = setPowerProfile(group->checkedButton()->property("profile").toString());
        load();
        return ok;
    }
    void defaults() override
    {
        for (QAbstractButton *b : group->buttons()) {
            if (b->property("profile").toString() == QLatin1String("balanced")) {
                b->setChecked(true);
            }
        }
        notify();
    }

private:
    QButtonGroup *group;
    QLabel *state;
    QString m_p;
};

class InfoTab : public Tab
{
public:
    InfoTab()
    {
        name = QStringLiteral("Інформаційний центр");
        table = new QTreeWidget;
        table->setHeaderHidden(true);
        table->setColumnCount(2);
        table->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->setFrameShape(QFrame::NoFrame);
        v->addWidget(table);
    }
    bool hasApply() const override { return false; }
    void load() override
    {
        table->clear();
        const Gpu g = intelGpu();
        const QString dev = g.card + QStringLiteral("/device");
        const QStringList gl = glInfo();
        QString cpu;
        for (const QString &l : readFile(QStringLiteral("/proc/cpuinfo")).split(QLatin1Char('\n'))) {
            if (l.startsWith(QLatin1String("model name"))) {
                cpu = l.section(QLatin1Char(':'), 1).trimmed();
                break;
            }
        }
        const auto mem = QRegularExpression(QStringLiteral("MemTotal:\\s+(\\d+)")).match(readFile(QStringLiteral("/proc/meminfo")));
        auto group = [&](const QString &t, const QList<Row> &rows) {
            auto *it = new QTreeWidgetItem(table, {t});
            it->setFont(0, ufont(15, QFont::DemiBold));
            it->setForeground(0, Blue);
            it->setFirstColumnSpanned(true);
            for (const Row &r : rows) {
                if (!r.value.isEmpty()) {
                    new QTreeWidgetItem(it, {r.item, r.value});
                }
            }
            it->setExpanded(true);
        };
        group(QStringLiteral("Система"), {{QStringLiteral("Операційна система"), QSysInfo::prettyProductName()},
                                          {QStringLiteral("Процесор"), cpu},
                                          {QStringLiteral("Фізична пам'ять"), mem.hasMatch() ? QStringLiteral("%1 МБ").arg(mem.captured(1).toLongLong() / 1024) : QString()}});
        group(QStringLiteral("Графіка"), {{QStringLiteral("Графічний пристрій"), g.name},
                                          {QStringLiteral("Код пристрою"), g.deviceId},
                                          {QStringLiteral("Код постачальника"), QStringLiteral("8086")},
                                          {QStringLiteral("Редакція"), g.revision},
                                          {QStringLiteral("Версія драйвера"), QStringLiteral("Mesa ") + mesaVersion()},
                                          {QStringLiteral("Драйвер ядра"), QStringLiteral("i915 / xe, Linux ") + QSysInfo::kernelVersion()},
                                          {QStringLiteral("Версія OpenGL"), gl.value(1).section(QLatin1Char(' '), 0, 0)},
                                          {QStringLiteral("Версія Vulkan"), vulkanVersion()},
                                          {QStringLiteral("Адреса PCI"), g.pci}});
        QList<Row> disp;
        for (const Output &o : outputs()) {
            const Mode m = o.mode(o.currentMode);
            disp << Row{o.builtIn() ? QStringLiteral("Вбудований дисплей") : o.name, o.enabled ? QStringLiteral("%1 x %2, %3 Гц").arg(m.w).arg(m.h).arg(qRound(m.hz)) : QStringLiteral("Вимкнено")};
        }
        group(QStringLiteral("Дисплеї"), disp);
    }

private:
    QTreeWidget *table;
};

class SupportTab : public Tab
{
public:
    SupportTab()
    {
        name = QStringLiteral("Підтримка");
        v->addWidget(text(QStringLiteral("Довідку та оновлення драйверів графіки Intel можна знайти на сайті Intel.")));
        for (const auto &l : QList<QPair<QString, QString>>{{QStringLiteral("Підтримка графіки Intel®"), QStringLiteral("https://www.intel.com/content/www/us/en/support/products/80939/graphics.html")},
                                                             {QStringLiteral("Завантаження драйверів"), QStringLiteral("https://www.intel.com/content/www/us/en/download-center/home.html")},
                                                             {QStringLiteral("Відвідати www.intel.com"), QStringLiteral("https://www.intel.com")}}) {
            auto *b = new QPushButton(l.first);
            b->setFlat(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(QStringLiteral("QPushButton{color:#0071c5;text-align:left;border:none;font-size:14px;padding:3px}QPushButton:hover{color:#00aeef}"));
            const QString url = l.second;
            QObject::connect(b, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
            v->addWidget(b);
        }
        v->addStretch(1);
    }
    bool hasApply() const override { return false; }
};

/* profiles: snapshots of display mode, rotation, colour, power plan, vsync */
class ProfilesTab : public Tab
{
public:
    ProfilesTab()
    {
        name = QStringLiteral("Профілі");
        v->addWidget(text(QStringLiteral("Профілі зберігають поточні параметри дисплея, кольору, живлення та 3D, щоб застосувати їх пізніше одним натисканням.")));
        list = new QListWidget;
        list->setFont(ufont(14));
        v->addWidget(list, 1);
    }
    bool hasApply() const override { return false; }
    void load() override
    {
        list->clear();
        QSettings &s = settings();
        for (const QString &g : s.childGroups()) {
            if (g.startsWith(QLatin1String("Profile-"))) {
                list->addItem(QString::fromUtf8(QByteArray::fromHex(g.mid(8).toLatin1())));
            }
        }
    }
    static QString key(const QString &name) { return QStringLiteral("Profile-") + QString::fromLatin1(name.toUtf8().toHex()); }
    static void save(const QString &name)
    {
        QSettings &s = settings();
        s.beginGroup(key(name));
        s.remove(QString());
        for (const Output &o : outputs()) {
            if (!o.enabled) {
                continue;
            }
            s.setValue(QStringLiteral("mode-") + o.name, o.currentMode);
            s.setValue(QStringLiteral("rotation-") + o.name, o.rotation);
            const Color c = loadColor(o.name);
            QVariantList l;
            for (int i = 0; i < 3; ++i) {
                l << c.gamma[i] << c.brightness[i] << c.contrast[i];
            }
            s.setValue(QStringLiteral("color-") + o.name, l);
        }
        s.setValue(QStringLiteral("power"), powerProfile());
        s.setValue(QStringLiteral("vsync"), vblankMode(QStringLiteral("seven-igfx")));
        s.endGroup();
        s.sync();
    }
    void applyCurrent()
    {
        if (!list->currentItem()) {
            return;
        }
        QSettings &s = settings();
        s.beginGroup(key(list->currentItem()->text()));
        QStringList args;
        for (const QString &k : s.childKeys()) {
            if (k.startsWith(QLatin1String("mode-"))) {
                args << QStringLiteral("output.%1.mode.%2").arg(currentOutputName(k.mid(5)), s.value(k).toString());
            } else if (k.startsWith(QLatin1String("rotation-"))) {
                args << QStringLiteral("output.%1.rotation.%2").arg(currentOutputName(k.mid(9)), rotationWord(s.value(k).toInt()));
            } else if (k.startsWith(QLatin1String("color-"))) {
                const QVariantList l = s.value(k).toList();
                Color c;
                for (int i = 0; i < 3 && l.size() >= 9; ++i) {
                    c.gamma[i] = l.at(i * 3).toDouble();
                    c.brightness[i] = l.at(i * 3 + 1).toInt();
                    c.contrast[i] = l.at(i * 3 + 2).toInt();
                }
                saveColor(currentOutputName(k.mid(6)), c);
                applyColor(currentOutputName(k.mid(6)), c);
            } else if (k == QLatin1String("power")) {
                setPowerProfile(s.value(k).toString());
            } else if (k == QLatin1String("vsync")) {
                setVblankMode(s.value(k).toInt(), QStringLiteral("seven-igfx"), {QStringLiteral("iris"), QStringLiteral("i965"), QStringLiteral("crocus")});
            }
        }
        s.endGroup();
        if (!args.isEmpty()) {
            kscreen(args);
        }
        setLoginApply(true);
    }
    void removeCurrent()
    {
        if (!list->currentItem()) {
            return;
        }
        settings().remove(key(list->currentItem()->text()));
        load();
    }
    void renameCurrent(QWidget *parent)
    {
        if (!list->currentItem()) {
            return;
        }
        const QString old = list->currentItem()->text();
        bool ok = false;
        const QString n = QInputDialog::getText(parent, QStringLiteral("Перейменувати профіль"), QStringLiteral("Нова назва:"), QLineEdit::Normal, old, &ok).trimmed();
        if (!ok || n.isEmpty() || n == old) {
            return;
        }
        QSettings &s = settings();
        s.beginGroup(key(old));
        QVariantMap m;
        for (const QString &k : s.childKeys()) {
            m.insert(k, s.value(k));
        }
        s.endGroup();
        s.remove(key(old));
        s.beginGroup(key(n));
        for (auto it = m.begin(); it != m.end(); ++it) {
            s.setValue(it.key(), it.value());
        }
        s.endGroup();
        load();
    }
    QListWidget *list;
};

/* ---------------------------------------------------------------- section page: header + side tabs + content + app bar */

struct Section {
    QString id, title, icon;
    QList<Tab *> tabs;
};

class Window : public QWidget
{
public:
    Window()
    {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setWindowTitle(QStringLiteral("Панель керування графікою Intel® HD"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-igfx")));
        setMinimumSize(800, 560);
        resize(1024, 660);
        setAutoFillBackground(true);
        QPalette pal = palette();
        pal.setColor(QPalette::Window, Qt::white);
        setPalette(pal);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(2, 2, 2, 2); // BorderStyle: 2 px light blue
        outer->setSpacing(0);
        header = new Header(this);
        outer->addWidget(header, 3);
        pages = new QStackedWidget(this);
        outer->addWidget(pages, 20);
        bar = new QWidget(this);
        auto *bl = new QHBoxLayout(bar);
        bl->setContentsMargins(20, 4, 20, 4);
        bl->addStretch(1);
        apply = new AppButton(QStringLiteral("apply"), QStringLiteral("Застосувати"));
        cancel = new AppButton(QStringLiteral("cancel"), QStringLiteral("Скасувати"));
        restore = new AppButton(QStringLiteral("restore"), QStringLiteral("Відновити типові"));
        saveProfile = new AppButton(QStringLiteral("save"), QStringLiteral("Зберегти профіль"));
        rename = new AppButton(QStringLiteral("rename"), QStringLiteral("Перейменувати"));
        remove = new AppButton(QStringLiteral("delete"), QStringLiteral("Видалити"));
        for (AppButton *b : {saveProfile, rename, remove, restore, cancel, apply}) {
            bl->addWidget(b);
        }
        outer->addWidget(bar, 3);

        QObject::connect(header->close, &QAbstractButton::clicked, this, &QWidget::close);
        QObject::connect(header->minimize, &QAbstractButton::clicked, this, &QWidget::showMinimized);
        QObject::connect(header->maximize, &QAbstractButton::clicked, this, [this] { isMaximized() ? showNormal() : showMaximized(); });
        header->back = [this] { home(); };
        header->menu = [this] { sectionMenu(); };
        QObject::connect(apply, &QAbstractButton::clicked, this, [this] {
            if (m_sec && m_sec->id == QLatin1String("profiles")) {
                profiles->applyCurrent();
            } else if (Tab *t = tab()) {
                t->apply();
            }
            refresh();
        });
        QObject::connect(cancel, &QAbstractButton::clicked, this, [this] {
            if (Tab *t = tab()) {
                if (auto *c = dynamic_cast<ColorTab *>(t)) {
                    c->discard();
                }
                t->load();
            }
            refresh();
        });
        QObject::connect(restore, &QAbstractButton::clicked, this, [this] {
            if (Tab *t = tab()) {
                t->defaults();
            }
            refresh();
        });
        QObject::connect(saveProfile, &QAbstractButton::clicked, this, [this] {
            bool ok = false;
            const QString n = QInputDialog::getText(this, QStringLiteral("Зберегти профіль"), QStringLiteral("Назва профілю:"), QLineEdit::Normal, QString(), &ok).trimmed();
            if (ok && !n.isEmpty()) {
                ProfilesTab::save(n);
                profiles->load();
            }
        });
        QObject::connect(rename, &QAbstractButton::clicked, this, [this] { profiles->renameCurrent(this); });
        QObject::connect(remove, &QAbstractButton::clicked, this, [this] {
            if (profiles->list->currentItem() &&
                QMessageBox::question(this, windowTitle(), QStringLiteral("Видалити профіль «%1»?").arg(profiles->list->currentItem()->text())) == QMessageBox::Yes) {
                profiles->removeCurrent();
            }
        });

        makeHome();
        profiles = new ProfilesTab;
        QObject::connect(profiles->list, &QListWidget::currentRowChanged, this, [this] { refresh(); });
        m_sections = {
            {QStringLiteral("display"), QStringLiteral("Дисплей"), QStringLiteral("displays_icon.png"), {new DisplayGeneralTab, new ColorTab, new MultiDisplayTab}},
            {QStringLiteral("3d"), QStringLiteral("3D"), QStringLiteral("3d_icon.png"), {new ThreeDTab}},
        };
        bool laptop = false;
        for (const Output &o : outputs()) {
            laptop |= o.builtIn();
        }
        if (laptop) {
            m_sections << Section{QStringLiteral("power"), QStringLiteral("Живлення"), QStringLiteral("power_icon.png"), {new PowerTab}};
        }
        m_sections << Section{QStringLiteral("options"), QStringLiteral("Параметри й підтримка"), QStringLiteral("options_icon.png"), {new InfoTab, new SupportTab}};
        m_sections << Section{QStringLiteral("profiles"), QStringLiteral("Профілі"), QStringLiteral("profiles_icon.png"), {profiles}};
        for (Section &s : m_sections) {
            auto *tile = new Tile(s.title, s.icon);
            const QString id = s.id;
            QObject::connect(tile, &QAbstractButton::clicked, this, [this, id] { open(id); });
            m_tiles->addWidget(tile, m_tileCount / 3, m_tileCount % 3);
            ++m_tileCount;
            // section page
            auto *pg = new QWidget;
            auto *h = new QHBoxLayout(pg);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(0);
            QStringList names;
            for (Tab *t : s.tabs) {
                names << t->name;
            }
            auto *side = new SideTabs(names);
            h->addWidget(side);
            auto *stack = new QStackedWidget;
            for (Tab *t : s.tabs) {
                t->changed = [this] { refresh(); };
                stack->addWidget(t);
            }
            h->addWidget(stack, 1);
            side->chosen = [this, side, stack](int i) {
                if (Tab *t = tab(); t && t->dirty() && !askApply(t)) {
                    return;
                }
                side->setCurrent(i);
                stack->setCurrentIndex(i);
                static_cast<Tab *>(stack->currentWidget())->load();
                refresh();
            };
            pages->addWidget(pg);
            m_pageOf.insert(s.id, pg);
            m_stackOf.insert(s.id, stack);
        }
        home();
    }

    void open(const QString &id)
    {
        for (Section &s : m_sections) {
            if (s.id == id) {
                if (Tab *t = tab(); t && t->dirty() && !askApply(t)) {
                    return;
                }
                m_sec = &s;
                header->set(s.title, true);
                pages->setCurrentWidget(m_pageOf.value(id));
                tab()->load();
                bar->show();
                refresh();
                return;
            }
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(QPen(LightBlue, 2));
        p.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
    }
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(e);
            const QPoint pt = me->position().toPoint();
            Qt::Edges ed;
            if (pt.x() < 5) ed |= Qt::LeftEdge;
            if (pt.x() > width() - 5) ed |= Qt::RightEdge;
            if (pt.y() < 5) ed |= Qt::TopEdge;
            if (pt.y() > height() - 5) ed |= Qt::BottomEdge;
            if (ed && !isMaximized()) {
                windowHandle()->startSystemResize(ed);
                return true;
            }
        }
        return QWidget::event(e);
    }
    void closeEvent(QCloseEvent *e) override
    {
        if (Tab *t = tab(); t && t->dirty() && !askApply(t)) {
            e->ignore();
            return;
        }
        e->accept();
    }

private:
    Tab *tab() const
    {
        if (!m_sec) {
            return nullptr;
        }
        return static_cast<Tab *>(m_stackOf.value(m_sec->id)->currentWidget());
    }
    bool askApply(Tab *t)
    {
        const auto r = QMessageBox::question(this, windowTitle(), QStringLiteral("Застосувати внесені зміни?"), QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel) {
            return false;
        }
        if (r == QMessageBox::Yes) {
            t->apply();
        } else {
            if (auto *c = dynamic_cast<ColorTab *>(t)) {
                c->discard();
            }
            t->load();
        }
        return true;
    }
    void home()
    {
        if (Tab *t = tab(); t && t->dirty() && !askApply(t)) {
            return;
        }
        m_sec = nullptr;
        header->set(QStringLiteral("Панель керування графікою Intel® HD"), false);
        pages->setCurrentWidget(m_home);
        bar->hide();
    }
    void sectionMenu()
    {
        QMenu m(this);
        QAction *h = m.addAction(QStringLiteral("Головна"));
        m.addSeparator();
        QHash<QAction *, QString> ids;
        for (const Section &s : m_sections) {
            ids.insert(m.addAction(QIcon(pic(s.icon)), s.title), s.id);
        }
        QAction *a = m.exec(QCursor::pos());
        if (a == h) {
            home();
        } else if (a) {
            open(ids.value(a));
        }
    }
    void refresh()
    {
        const bool prof = m_sec && m_sec->id == QLatin1String("profiles");
        Tab *t = tab();
        const bool has = t && t->hasApply();
        saveProfile->setVisible(m_sec != nullptr);
        rename->setVisible(prof);
        remove->setVisible(prof);
        restore->setVisible(has);
        cancel->setVisible(has);
        apply->setVisible(has || prof);
        apply->setEnabled(prof ? profiles->list->currentItem() != nullptr : (has && t->dirty()));
        cancel->setEnabled(has && t->dirty());
        rename->setEnabled(prof && profiles->list->currentItem());
        remove->setEnabled(prof && profiles->list->currentItem());
    }
    void makeHome()
    {
        m_home = new QWidget;
        auto *h = new QHBoxLayout(m_home);
        h->setContentsMargins(0, 10, 0, 10);
        h->addSpacing(20);
        auto *badge = new QVBoxLayout;
        badge->addStretch(1);
        auto *img = new QLabel;
        img->setPixmap(pic(QStringLiteral("appbadge.png")).scaled(170, 170, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        badge->addWidget(img);
        auto *desc = new QLabel(QStringLiteral("Налаштуйте дисплеї, 3D-графіку та живлення своєї графіки Intel® HD."));
        desc->setWordWrap(true);
        desc->setFont(ufont(15));
        desc->setStyleSheet(QStringLiteral("color:#0071c5"));
        desc->setFixedWidth(220);
        badge->addWidget(desc);
        badge->addStretch(2);
        h->addLayout(badge);
        h->addSpacing(30);
        auto *grid = new QWidget;
        m_tiles = new QGridLayout(grid);
        m_tiles->setContentsMargins(5, 0, 25, 0);
        m_tiles->setSpacing(0);
        h->addWidget(grid, 1);
        pages->addWidget(m_home);
    }

    Header *header;
    QStackedWidget *pages;
    QWidget *bar, *m_home = nullptr;
    QGridLayout *m_tiles = nullptr;
    int m_tileCount = 0;
    AppButton *apply, *cancel, *restore, *saveProfile, *rename, *remove;
    QList<Section> m_sections;
    Section *m_sec = nullptr;
    QHash<QString, QWidget *> m_pageOf;
    QHash<QString, QStackedWidget *> m_stackOf;
    ProfilesTab *profiles;
};

} // namespace Igfx

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("seven-igfx"));
    app.setDesktopFileName(QStringLiteral("seven-igfx"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-igfx")));
    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--apply"))) {
        applySavedColors();
        return 0;
    }
    if (Igfx::intelGpu().card.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("Панель керування графікою Intel® HD"), QStringLiteral("Не вдалося знайти графічний пристрій Intel®."));
        return 1;
    }
    Igfx::Window w;
    const int pi = args.indexOf(QStringLiteral("--page"));
    if (pi > 0 && pi + 1 < args.size()) {
        w.open(args.at(pi + 1));
    }
    const int si = args.indexOf(QStringLiteral("--screenshot"));
    w.show();
    if (si > 0 && si + 1 < args.size()) {
        QTimer::singleShot(1500, &w, [&w, path = args.at(si + 1)] {
            w.grab().save(path);
            qApp->quit();
        });
    }
    return app.exec();
}
