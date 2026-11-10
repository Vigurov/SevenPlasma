#pragma once
#include "win7i18n.h"
/*
 * The "Display" and "Video" pages of the NVIDIA Control Panel (driver 474.30 for Windows 7: the layouts of
 * nvDispSR.dll / nvViTvSR.dll dialogs 33521, 6000, 367, 23, 33501, 34612, 32795, 1500, 1393 and the Russian
 * screenshots of nvworld.ru/articles/cpanel). Pictures: ~/.local/share/seven-nvidia/disp (resources of those DLLs).
 *
 * What changes on Linux:
 *  - resolution, refresh rate, rotation, the displays in use and the primary one: kscreen-doctor, each change asks
 *    "keep these settings?" and goes back after 20 s like the driver;
 *  - desktop colour: brightness / contrast / gamma as XRandR gamma ramps (shared with Catalyst Control Center's
 *    page, Ccc::applyColor), digital vibrance and hue as the output's colour matrix (RandR "CTM" property);
 *  - scaling mode and underscan: RandR "scaling mode" / "underscan" properties, output colour depth: "max bpc";
 *  - HDCP: the "Content Protection" property; digital audio: the profile of the GPU's HD Audio card (pactl);
 *  - video colour / image: options for mpv-based players (~/.config/mpv/mpv.conf), Linux has no driver-level
 *    video processing.
 * Everything is reapplied at login by `seven-nvidia --apply-display` (autostart).
 */
#include "cccbackend.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>
#include <QRadioButton>
#include <QSpinBox>
#include <QToolButton>
#include <array>
#include <QDialog>
#include <QGroupBox>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QTabWidget>
#include <QTimer>

namespace NvDisp
{
inline QString pic(const QString &name)
{
    return asset(QStringLiteral("disp/") + name + QStringLiteral(".png"));
}

inline QSettings &settings()
{
    static QSettings s(QDir::homePath() + QStringLiteral("/.config/seven-nvidia/display.conf"), QSettings::IniFormat);
    return s;
}

/** the name NVIDIA shows under a display: the monitor's EDID name, "Дисплей ноутбука" for the built-in panel */
inline QString displayName(const Ccc::Output &o)
{
    if (o.builtIn()) {
        return tr7("Laptop display");
    }
    for (const QString &card : QDir(QStringLiteral("/sys/class/drm")).entryList({QStringLiteral("card?")}, QDir::AllEntries | QDir::System)) {
        const QString n = Ccc::edidName(card, o.name);
        if (!n.isEmpty()) {
            return n;
        }
    }
    return o.name;
}

/** "HDMI", "DisplayPort", "DVI"... for the connector of an output */
inline QString connectorWord(const QString &output)
{
    if (output.startsWith(QLatin1String("eDP")) || output.startsWith(QLatin1String("LVDS"))) {
        return tr7("Laptop display");
    }
    if (output.startsWith(QLatin1String("HDMI"))) {
        return QStringLiteral("HDMI");
    }
    if (output.startsWith(QLatin1String("DP"))) {
        return QStringLiteral("DisplayPort");
    }
    if (output.startsWith(QLatin1String("DVI"))) {
        return QStringLiteral("DVI");
    }
    return QStringLiteral("VGA");
}

/* ---------------- RandR output properties (xrandr for reading, libXrandr for the 64-bit CTM) ---------------- */

inline QString outputProperty(const QString &output, const QString &prop)
{
    const QStringList lines = Ccc::run(QStringLiteral("xrandr"), {QStringLiteral("--prop")}).split(QLatin1Char('\n'));
    bool in = false;
    for (const QString &l : lines) {
        if (!l.startsWith(QLatin1Char(' ')) && !l.startsWith(QLatin1Char('\t'))) {
            in = l.startsWith(output + QLatin1Char(' '));
            continue;
        }
        const QString t = l.trimmed();
        if (in && t.startsWith(prop + QLatin1Char(':'))) {
            return t.mid(prop.size() + 1).trimmed();
        }
    }
    return {};
}
inline bool hasOutputProperty(const QString &output, const QString &prop)
{
    return !outputProperty(output, prop).isNull();
}
inline bool setOutputProperty(const QString &output, const QString &prop, const QString &value)
{
    QProcess p;
    p.start(QStringLiteral("xrandr"), {QStringLiteral("--output"), output, QStringLiteral("--set"), prop, value});
    p.waitForFinished(5000);
    return p.exitCode() == 0;
}

/** digital vibrance (0..100, 50 = unchanged) and hue (degrees) as a 3x3 colour matrix, rows = output R, G, B */
inline std::array<double, 9> colorMatrix(int vibrance, int hue)
{
    const double s = vibrance / 50.0;
    // luma weights of BT.709
    const double lr = 0.2126, lg = 0.7152, lb = 0.0722;
    std::array<double, 9> sat{lr * (1 - s) + s, lg * (1 - s), lb * (1 - s), lr * (1 - s), lg * (1 - s) + s, lb * (1 - s), lr * (1 - s), lg * (1 - s), lb * (1 - s) + s};
    // hue: rotation about the grey axis
    const double a = hue * M_PI / 180.0, c = std::cos(a), sn = std::sin(a);
    const double k = 1.0 / 3.0, sq = std::sqrt(k);
    std::array<double, 9> rot{c + (1 - c) * k,           k * (1 - c) - sq * sn,     k * (1 - c) + sq * sn,
                              k * (1 - c) + sq * sn,     c + k * (1 - c),           k * (1 - c) - sq * sn,
                              k * (1 - c) - sq * sn,     k * (1 - c) + sq * sn,     c + k * (1 - c)};
    std::array<double, 9> m{};
    for (int r = 0; r < 3; ++r) {
        for (int col = 0; col < 3; ++col) {
            for (int i = 0; i < 3; ++i) {
                m[r * 3 + col] += rot[r * 3 + i] * sat[i * 3 + col];
            }
        }
    }
    return m;
}

/** writes the RandR "CTM" property: 9 sign-magnitude S31.32 numbers as 18 CARD32 (xrandr --set mangles fractions) */
inline bool applyMatrix(const QString &output, const std::array<double, 9> &m)
{
    void *x11 = dlopen("libX11.so.6", RTLD_LAZY);
    void *xr = dlopen("libXrandr.so.2", RTLD_LAZY);
    if (!x11 || !xr) {
        return false;
    }
    using OpenDisplay = void *(*)(const char *);
    using InternAtom = unsigned long (*)(void *, const char *, int);
    using RootWindow = unsigned long (*)(void *);
    using Flush = int (*)(void *);
    using CloseDisplay = int (*)(void *);
    struct ScreenResources {
        unsigned long timestamp, configTimestamp;
        int ncrtc;
        unsigned long *crtcs;
        int noutput;
        unsigned long *outputs;
    };
    struct OutputInfo {
        unsigned long timestamp, crtc;
        char *name;
        int nameLen;
    };
    using GetRes = ScreenResources *(*)(void *, unsigned long);
    using GetInfo = OutputInfo *(*)(void *, ScreenResources *, unsigned long);
    using Change = void (*)(void *, unsigned long, unsigned long, unsigned long, int, int, const unsigned char *, int);
    auto open = reinterpret_cast<OpenDisplay>(dlsym(x11, "XOpenDisplay"));
    auto atom = reinterpret_cast<InternAtom>(dlsym(x11, "XInternAtom"));
    auto root = reinterpret_cast<RootWindow>(dlsym(x11, "XDefaultRootWindow"));
    auto flush = reinterpret_cast<Flush>(dlsym(x11, "XFlush"));
    auto close = reinterpret_cast<CloseDisplay>(dlsym(x11, "XCloseDisplay"));
    auto getRes = reinterpret_cast<GetRes>(dlsym(xr, "XRRGetScreenResourcesCurrent"));
    auto getInfo = reinterpret_cast<GetInfo>(dlsym(xr, "XRRGetOutputInfo"));
    auto change = reinterpret_cast<Change>(dlsym(xr, "XRRChangeOutputProperty"));
    if (!open || !atom || !root || !getRes || !getInfo || !change) {
        return false;
    }
    void *dpy = open(nullptr);
    if (!dpy) {
        return false;
    }
    long data[18];
    for (int i = 0; i < 9; ++i) {
        const double mag = std::abs(m[i]);
        quint64 hi = quint64(mag);
        quint64 lo = quint64(std::llround((mag - double(hi)) * 4294967296.0));
        if (lo >= 4294967296ULL) {
            lo = 0;
            ++hi;
        }
        if (m[i] < 0) {
            hi |= 0x80000000ULL;
        }
        data[i * 2] = long(lo);
        data[i * 2 + 1] = long(hi);
    }
    bool done = false;
    ScreenResources *res = getRes(dpy, root(dpy));
    const unsigned long ctm = atom(dpy, "CTM", 0);
    for (int i = 0; res && i < res->noutput; ++i) {
        OutputInfo *oi = getInfo(dpy, res, res->outputs[i]);
        if (oi && QString::fromLatin1(oi->name, oi->nameLen) == output) {
            change(dpy, res->outputs[i], ctm, 19 /* XA_INTEGER */, 32, 0 /* PropModeReplace */, reinterpret_cast<const unsigned char *>(data), 18);
            done = true;
        }
    }
    flush(dpy);
    close(dpy);
    return done;
}

inline int vibrance(const QString &output)
{
    return settings().value(Ccc::colorKey(output) + QStringLiteral("/vibrance"), 50).toInt();
}
inline int hue(const QString &output)
{
    return settings().value(Ccc::colorKey(output) + QStringLiteral("/hue"), 0).toInt();
}

/** the login autostart that puts the display settings back */
inline void setLoginApply()
{
    const QString path = QDir::homePath() + QStringLiteral("/.config/autostart/seven-nvidia-display.desktop");
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write("[Desktop Entry]\nType=Application\nName=NVIDIA Control Panel: display settings\nExec=seven-nvidia --apply-display\nNoDisplay=true\nX-KDE-autostart-phase=2\n");
    }
}

/** `seven-nvidia --apply-display` */
inline void applyAll()
{
    Ccc::applySavedColors();
    for (const Ccc::Output &o : Ccc::outputs()) {
        if (vibrance(o.name) != 50 || hue(o.name) != 0) {
            applyMatrix(o.name, colorMatrix(vibrance(o.name), hue(o.name)));
        }
        const QString scaling = settings().value(Ccc::colorKey(o.name) + QStringLiteral("/scaling")).toString();
        if (!scaling.isEmpty()) {
            setOutputProperty(o.name, QStringLiteral("scaling mode"), scaling);
        }
    }
}

/* ---------------- "1. Select the display you would like to change." ---------------- */

/** the row of display pictures: green tile = selected; the name under it (green when selected) */
class DisplayPicker : public QWidget
{
public:
    std::function<void(const QString &)> changed;

    DisplayPicker()
    {
        setMouseTracking(true);
        reload();
    }
    void reload()
    {
        m_outputs.clear();
        for (const Ccc::Output &o : Ccc::outputs()) {
            if (o.enabled || !m_enabledOnly) {
                m_outputs << o;
            }
        }
        if (m_current.isEmpty() || std::none_of(m_outputs.cbegin(), m_outputs.cend(), [this](const Ccc::Output &o) {
                return o.name == m_current;
            })) {
            m_current = m_outputs.isEmpty() ? QString() : m_outputs.first().name;
        }
        setFixedHeight(92);
        update();
    }
    QString current() const
    {
        return m_current;
    }
    Ccc::Output output() const
    {
        for (const Ccc::Output &o : m_outputs) {
            if (o.name == m_current) {
                return o;
            }
        }
        return {};
    }
    void setEnabledOnly(bool on)
    {
        m_enabledOnly = on;
        reload();
    }

protected:
    QRect tile(int i) const
    {
        return QRect(20 + i * 128, 4, 107, 60);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        for (int i = 0; i < m_outputs.size(); ++i) {
            const Ccc::Output &o = m_outputs.at(i);
            const bool sel = o.name == m_current;
            const bool hot = i == m_hot && !sel;
            // the selected / hovered tiles carry their green frame in the picture
            const QString id = o.builtIn() ? (sel ? QStringLiteral("2016") : hot ? QStringLiteral("2017") : QStringLiteral("2018"))
                                           : (sel ? QStringLiteral("2024") : hot ? QStringLiteral("2025") : QStringLiteral("2026"));
            p.drawImage(tile(i).topLeft(), QImage(pic(id)));
            p.setPen(sel ? QColor(0x2f, 0x9e, 0x1e) : QColor(0x1e, 0x1e, 0x1e));
            const QRect tr(tile(i).x() - 10, tile(i).bottom() + 4, tile(i).width() + 20, 16);
            p.drawText(tr, Qt::AlignHCenter | Qt::AlignTop, fontMetrics().elidedText(displayName(o), Qt::ElideRight, tr.width()));
        }
    }
    int hit(const QPoint &pos) const
    {
        for (int i = 0; i < m_outputs.size(); ++i) {
            if (tile(i).adjusted(-6, 0, 6, 20).contains(pos)) {
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
            setToolTip(h >= 0 ? displayName(m_outputs.at(h)) : QString());
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
        if (h >= 0 && m_outputs.at(h).name != m_current) {
            m_current = m_outputs.at(h).name;
            update();
            if (changed) {
                changed(m_current);
            }
        }
    }

private:
    QList<Ccc::Output> m_outputs;
    QString m_current;
    int m_hot = -1;
    bool m_enabledOnly = true;
};

/** "Do you want to keep these settings?" with the 20 s countdown; returns true to keep */
inline bool confirmChange(QWidget *parent)
{
    QDialog d(parent);
    d.setWindowTitle(tr7("Apply Changes"));
    auto *v = new QVBoxLayout(&d);
    auto *row = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(QPixmap(pic(QStringLiteral("info-32"))));
    row->addWidget(icon, 0, Qt::AlignTop);
    auto *text = new QLabel;
    text->setWordWrap(true);
    row->addWidget(text, 1);
    v->addLayout(row);
    auto *bb = new QHBoxLayout;
    bb->addStretch(1);
    auto *yes = new QPushButton(tr7("Yes"));
    auto *no = new QPushButton(tr7("No"));
    bb->addWidget(yes);
    bb->addWidget(no);
    v->addLayout(bb);
    int left = 20;
    auto update = [&] {
        text->setText(tr7("The display settings have changed.\nDo you want to keep these settings?\n\nThe previous settings will be restored in %1 seconds.").arg(left));
    };
    update();
    QTimer t;
    QObject::connect(&t, &QTimer::timeout, &d, [&] {
        if (--left <= 0) {
            d.reject();
        }
        update();
    });
    t.start(1000);
    QObject::connect(yes, &QPushButton::clicked, &d, &QDialog::accept);
    QObject::connect(no, &QPushButton::clicked, &d, &QDialog::reject);
    d.setFixedWidth(380);
    return d.exec() == QDialog::Accepted;
}

/** slider row of the colour pages: icon, "Name:", "-" slider "+", value */
struct SliderRow {
    Win7Slider *slider;
    QLabel *value;
};
inline SliderRow sliderRow(QGridLayout *g, int row, const QString &iconName, const QString &label, int min, int max)
{
    auto *icon = new QLabel;
    icon->setPixmap(QPixmap(pic(iconName)));
    g->addWidget(icon, row, 0);
    g->addWidget(new QLabel(label), row, 1);
    g->addWidget(new QLabel(QStringLiteral("-")), row, 2);
    auto *s = new Win7Slider;
    s->setRange(min, max);
    s->setMinimumWidth(130);
    g->addWidget(s, row, 3);
    g->addWidget(new QLabel(QStringLiteral("+")), row, 4);
    auto *val = new QLabel;
    val->setMinimumWidth(44);
    g->addWidget(val, row, 5);
    return {s, val};
}

/** bold "2. ..." step heading */
inline QLabel *step(const QString &text)
{
    auto *l = boldLabel(text);
    l->setContentsMargins(10, 10, 0, 6);
    return l;
}

/* ---------------- the pages ---------------- */

class Pages : public QObject
{
public:
    std::function<void()> changed; // the Apply / Cancel band follows dirty()

    bool dirty() const
    {
        for (const auto &e : m_editors) {
            if (e.dirty()) {
                return true;
            }
        }
        return false;
    }
    void apply(QWidget *parent)
    {
        for (auto &e : m_editors) {
            if (e.dirty()) {
                e.apply(parent);
            }
        }
        setLoginApply();
        notify();
    }
    void cancel()
    {
        for (auto &e : m_editors) {
            if (e.dirty()) {
                e.cancel();
            }
        }
        notify();
    }

    // ---- Change resolution ----
    QWidget *resolution()
    {
        auto *page = new PageFrame(tr7("Change resolution"),
                                   tr7("You can change the resolution and refresh rate of the display to reduce flicker or change the amount of information on the screen."),
                                   QStringLiteral("disp/banner-display.png"));
        auto *panel = new RoundPanel;
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 4, 16, 16);
        v->addWidget(step(tr7("1. Select the display you would like to change.")));
        auto *picker = new DisplayPicker;
        v->addWidget(picker);
        v->addWidget(step(tr7("2. Choose the resolution.")));
        auto *g = new QGridLayout;
        g->setContentsMargins(22, 0, 0, 0);
        g->setHorizontalSpacing(40);
        g->addWidget(new QLabel(tr7("Connector:")), 0, 0);
        auto *conn = new QLabel;
        g->addWidget(conn, 1, 0);
        g->addWidget(new QLabel(tr7("Resolution:")), 2, 0);
        auto *list = new QListWidget;
        list->setFixedSize(270, 150);
        g->addWidget(list, 3, 0);
        g->addWidget(new QLabel(tr7("Refresh rate:")), 2, 1, Qt::AlignTop);
        auto *rate = new QComboBox;
        rate->setMinimumWidth(170);
        g->addWidget(rate, 3, 1, Qt::AlignTop);
        g->setColumnStretch(2, 1);
        v->addLayout(g);
        v->addWidget(step(tr7("3. Apply the following settings.")));
        auto *colorDefault = new QRadioButton(tr7("Use default color settings"));
        auto *colorNv = new QRadioButton(tr7("Use NVIDIA color settings"));
        auto *cg = new QGridLayout;
        cg->setContentsMargins(22, 0, 0, 0);
        cg->addWidget(colorDefault, 0, 0, 1, 4);
        cg->addWidget(colorNv, 1, 0, 1, 4);
        auto combo = [&](int row, int col, const QString &label, const QStringList &items) {
            cg->addWidget(new QLabel(label), row, col);
            auto *c = new QComboBox;
            c->addItems(items);
            c->setMinimumWidth(170);
            cg->addWidget(c, row + 1, col);
            return c;
        };
        auto *depth = combo(2, 0, tr7("Desktop color depth:"), {tr7("Highest (32-bit)")});
        auto *bpc = combo(2, 1, tr7("Output color depth:"), {QStringLiteral("8 bpc"), QStringLiteral("10 bpc")});
        auto *format = combo(4, 0, tr7("Output color format:"), {QStringLiteral("RGB")});
        auto *range = combo(4, 1, tr7("Output dynamic range:"), {tr7("Full")});
        Q_UNUSED(depth)
        Q_UNUSED(format)
        Q_UNUSED(range)
        v->addLayout(cg);
        page->body->addWidget(panel);
        page->show(tr7("The resolution sets the number of dots on the screen: the higher it is, the smaller the items and the more of them fit. The refresh rate is how many times per second the screen is redrawn."),
                   tr7("Choose the native resolution of the display for the sharpest image."));

        struct State {
            QString output, mode;
            int bpc = 8;
            bool nvColor = false;
        };
        auto st = std::make_shared<State>();
        auto saved = std::make_shared<State>();
        auto fill = std::make_shared<std::function<void()>>();
        auto fillRates = [list, rate, picker, st] {
            const Ccc::Output o = picker->output();
            rate->blockSignals(true);
            rate->clear();
            const QString size = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
            QList<Ccc::Mode> modes;
            for (const Ccc::Mode &m : o.modes) {
                if (QStringLiteral("%1x%2").arg(m.w).arg(m.h) == size) {
                    modes << m;
                }
            }
            std::sort(modes.begin(), modes.end(), [](const Ccc::Mode &a, const Ccc::Mode &b) {
                return a.hz > b.hz;
            });
            for (const Ccc::Mode &m : modes) {
                rate->addItem(tr7("%1 Hz").arg(qRound(m.hz)), m.id);
            }
            rate->setCurrentIndex(qMax(0, rate->findData(st->mode)));
            rate->blockSignals(false);
        };
        *fill = [=] {
            const Ccc::Output o = picker->output();
            conn->setText(QStringLiteral("<img src=\"%1\" width=24 height=24> %2")
                              .arg(pic(o.builtIn() ? QStringLiteral("5097") : o.name.startsWith(QLatin1String("HDMI")) ? QStringLiteral("5095") : QStringLiteral("5096")),
                                   connectorWord(o.name)));
            list->blockSignals(true);
            list->clear();
            auto *hdr = new QListWidgetItem(tr7("PC"), list);
            hdr->setFlags(Qt::NoItemFlags);
            hdr->setForeground(QColor(0x1e, 0x50, 0xa0));
            QStringList seen;
            QList<Ccc::Mode> modes = o.modes;
            std::sort(modes.begin(), modes.end(), [](const Ccc::Mode &a, const Ccc::Mode &b) {
                return a.w * a.h > b.w * b.h || (a.w * a.h == b.w * b.h && a.w > b.w);
            });
            const Ccc::Mode native = o.mode(o.preferredMode);
            const Ccc::Mode cur = o.mode(st->mode);
            for (const Ccc::Mode &m : modes) {
                const QString key = QStringLiteral("%1x%2").arg(m.w).arg(m.h);
                if (seen.contains(key)) {
                    continue;
                }
                seen << key;
                QString text = QStringLiteral("%1 × %2").arg(m.w).arg(m.h);
                if (m.w == native.w && m.h == native.h) {
                    text += tr7(" (native)");
                }
                auto *it = new QListWidgetItem(QStringLiteral("    ") + text, list);
                it->setData(Qt::UserRole, key);
                if (m.w == cur.w && m.h == cur.h) {
                    list->setCurrentItem(it);
                }
            }
            list->blockSignals(false);
            fillRates();
            colorNv->setChecked(st->nvColor);
            colorDefault->setChecked(!st->nvColor);
            bpc->setCurrentIndex(st->bpc >= 10 ? 1 : 0);
            for (QComboBox *c : {depth, bpc, format, range}) {
                c->setEnabled(st->nvColor);
            }
            bpc->setEnabled(st->nvColor && hasOutputProperty(o.name, QStringLiteral("max bpc")));
        };
        auto load = [=] {
            const Ccc::Output o = picker->output();
            st->output = o.name;
            st->mode = o.currentMode;
            const QString b = outputProperty(o.name, QStringLiteral("max bpc"));
            st->bpc = b.section(QLatin1Char(' '), 0, 0).toInt() >= 10 ? 10 : 8;
            st->nvColor = settings().value(Ccc::colorKey(o.name) + QStringLiteral("/nvColorDepth"), false).toBool();
            *saved = *st;
            (*fill)();
        };
        load();
        picker->changed = [=](const QString &) {
            load();
            notify();
        };
        connect(list, &QListWidget::currentItemChanged, this, [=] {
            fillRates();
            st->mode = rate->currentData().toString();
            notify();
        });
        connect(rate, &QComboBox::currentIndexChanged, this, [=] {
            st->mode = rate->currentData().toString();
            notify();
        });
        connect(colorNv, &QRadioButton::toggled, this, [=](bool on) {
            st->nvColor = on;
            (*fill)();
            notify();
        });
        connect(bpc, &QComboBox::currentIndexChanged, this, [=](int i) {
            st->bpc = i == 1 ? 10 : 8;
            notify();
        });
        page->banner->restore = [=] {
            const Ccc::Output o = picker->output();
            st->mode = o.preferredMode;
            st->nvColor = false;
            st->bpc = 8;
            (*fill)();
            notify();
        };
        m_editors << Editor{[=] {
                                return st->mode != saved->mode || st->bpc != saved->bpc || st->nvColor != saved->nvColor;
                            },
                            [=](QWidget *parent) {
                                const Ccc::Output o = picker->output();
                                const QString old = saved->mode;
                                if (st->mode != old) {
                                    Ccc::kscreen({QStringLiteral("output.%1.mode.%2").arg(o.name, st->mode)});
                                    if (!confirmChange(parent)) {
                                        Ccc::kscreen({QStringLiteral("output.%1.mode.%2").arg(o.name, old)});
                                        st->mode = old;
                                    }
                                }
                                if (st->bpc != saved->bpc || st->nvColor != saved->nvColor) {
                                    setOutputProperty(o.name, QStringLiteral("max bpc"), QString::number(st->nvColor ? st->bpc : 8));
                                    settings().setValue(Ccc::colorKey(o.name) + QStringLiteral("/nvColorDepth"), st->nvColor);
                                }
                                picker->reload();
                                load();
                            },
                            [=] {
                                *st = *saved;
                                (*fill)();
                            }};
        return page;
    }

    // ---- Adjust desktop colour settings ----
    QWidget *desktopColor()
    {
        auto *page = new PageFrame(tr7("Adjust desktop color settings"),
                                   tr7("You can adjust the desktop color settings for your displays or improve image quality if you are using a TV."),
                                   QStringLiteral("disp/banner-display.png"));
        auto *panel = new RoundPanel;
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 4, 16, 16);
        v->addWidget(step(tr7("1. Select the display you would like to change.")));
        auto *picker = new DisplayPicker;
        v->addWidget(picker);
        auto *cols = new QHBoxLayout;
        auto *left = new QVBoxLayout;
        left->addWidget(step(tr7("2. Choose how color is set.")));
        auto *other = new QRadioButton(tr7("Other applications control the color settings"));
        auto *nv = new QRadioButton(tr7("Use NVIDIA settings"));
        other->setContentsMargins(20, 0, 0, 0);
        left->addWidget(other);
        left->addWidget(nv);
        auto *box = new QWidget;
        auto *g = new QGridLayout(box);
        g->setContentsMargins(24, 2, 0, 0);
        g->setVerticalSpacing(0);
        auto *chIcon = new QLabel;
        chIcon->setPixmap(QPixmap(pic(QStringLiteral("ch-all"))));
        g->addWidget(chIcon, 0, 0);
        g->addWidget(new QLabel(tr7("Color channel:")), 0, 1);
        auto *channel = new QComboBox;
        channel->addItem(QIcon(pic(QStringLiteral("ch-all"))), tr7("All channels"));
        channel->addItem(QIcon(pic(QStringLiteral("ch-red"))), tr7("Red"));
        channel->addItem(QIcon(pic(QStringLiteral("ch-green"))), tr7("Green"));
        channel->addItem(QIcon(pic(QStringLiteral("ch-blue"))), tr7("Blue"));
        g->addWidget(channel, 0, 3, 1, 2);
        const SliderRow bright = sliderRow(g, 1, QStringLiteral("brightness"), tr7("Brightness:"), 0, 100);
        const SliderRow contrast = sliderRow(g, 2, QStringLiteral("contrast"), tr7("Contrast:"), 0, 100);
        const SliderRow gamma = sliderRow(g, 3, QStringLiteral("gamma"), tr7("Gamma:"), 30, 280);
        auto *note = new QLabel(tr7("Very high and very low brightness and contrast values\nmay limit the gamma range."));
        g->addWidget(note, 4, 1, 1, 5);
        left->addWidget(box);
        left->addWidget(step(tr7("3. Apply the following enhancements:")));
        auto *eg = new QGridLayout;
        eg->setContentsMargins(24, 0, 0, 0);
        const SliderRow vib = sliderRow(eg, 0, QStringLiteral("vibrance"), tr7("Digital vibrance:"), 0, 100);
        const SliderRow hueRow = sliderRow(eg, 1, QStringLiteral("hue"), tr7("Hue:"), 0, 359);
        left->addLayout(eg);
        cols->addLayout(left, 1);
        // the reference picture with the settings applied
        auto *right = new QVBoxLayout;
        right->addSpacing(70);
        auto *ref = new QLabel;
        ref->setFixedSize(263, 176);
        right->addWidget(ref);
        auto *refRow = new QHBoxLayout;
        refRow->addWidget(new QLabel(tr7("Reference image:")));
        auto *refGroup = new QButtonGroup(page);
        for (int i = 0; i < 3; ++i) {
            auto *r = new QRadioButton(QString::number(i + 1));
            refGroup->addButton(r, i);
            refRow->addWidget(r);
        }
        refGroup->button(0)->setChecked(true);
        right->addLayout(refRow);
        right->addStretch(1);
        cols->addLayout(right);
        v->addLayout(cols);
        page->body->addWidget(panel);
        page->describe(vib.slider, tr7("Digital vibrance increases the color saturation of the desktop."),
                       tr7("Brighter colors in games and movies."));
        page->describe(hueRow.slider, tr7("Hue shifts all colors around the color wheel."), {});
        page->show(tr7("These settings affect the colors of the whole desktop on the selected display."), {});

        struct State {
            Ccc::Color color;
            int vibrance = 50, hue = 0;
            bool nvidia = false;
            bool operator==(const State &o) const
            {
                return color == o.color && vibrance == o.vibrance && hue == o.hue && nvidia == o.nvidia;
            }
        };
        auto st = std::make_shared<State>();
        auto saved = std::make_shared<State>();
        auto busy = std::make_shared<bool>(false);
        auto preview = [=] {
            QImage img(pic(QStringLiteral("%1").arg(2150 + refGroup->checkedId())));
            img = img.convertToFormat(QImage::Format_RGB32);
            if (st->nvidia) {
                const auto m = colorMatrix(st->vibrance, st->hue);
                for (int y = 0; y < img.height(); ++y) {
                    QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
                    for (int x = 0; x < img.width(); ++x) {
                        const double in[3] = {qRed(line[x]) / 255.0, qGreen(line[x]) / 255.0, qBlue(line[x]) / 255.0};
                        int out[3];
                        for (int c = 0; c < 3; ++c) {
                            const double mixed = qBound(0.0, m[c * 3] * in[0] + m[c * 3 + 1] * in[1] + m[c * 3 + 2] * in[2], 1.0);
                            out[c] = qRound(Ccc::ramp(st->color, c, mixed) * 255);
                        }
                        line[x] = qRgb(out[0], out[1], out[2]);
                    }
                }
            }
            ref->setPixmap(QPixmap::fromImage(img));
        };
        auto fill = [=] {
            *busy = true;
            const int ch = qMax(0, channel->currentIndex() - 1);
            bright.slider->setValue(st->color.brightness[ch] / 2 + 50);
            contrast.slider->setValue(st->color.contrast[ch] / 2);
            gamma.slider->setValue(qRound(st->color.gamma[ch] * 100));
            vib.slider->setValue(st->vibrance);
            hueRow.slider->setValue(st->hue);
            bright.value->setText(QStringLiteral("%1%").arg(bright.slider->value()));
            contrast.value->setText(QStringLiteral("%1%").arg(contrast.slider->value()));
            gamma.value->setText(QString::number(st->color.gamma[ch], 'f', 2).replace(QLatin1Char('.'), QLatin1Char(',')));
            vib.value->setText(QStringLiteral("%1%").arg(st->vibrance));
            hueRow.value->setText(QStringLiteral("%1°").arg(st->hue));
            nv->setChecked(st->nvidia);
            other->setChecked(!st->nvidia);
            box->setEnabled(st->nvidia);
            for (QWidget *w : {static_cast<QWidget *>(vib.slider), static_cast<QWidget *>(hueRow.slider)}) {
                w->setEnabled(st->nvidia);
            }
            *busy = false;
            preview();
        };
        // live on the screen like the driver; Cancel puts the saved colours back
        auto live = [=] {
            const QString out = picker->current();
            Ccc::applyColor(out, st->nvidia ? st->color : Ccc::Color());
            applyMatrix(out, st->nvidia ? colorMatrix(st->vibrance, st->hue) : colorMatrix(50, 0));
        };
        auto load = [=] {
            const QString out = picker->current();
            st->color = Ccc::loadColor(out);
            st->vibrance = vibrance(out);
            st->hue = hue(out);
            st->nvidia = settings().value(Ccc::colorKey(out) + QStringLiteral("/nvColor"), !st->color.isDefault() || st->vibrance != 50 || st->hue != 0).toBool();
            *saved = *st;
            fill();
        };
        load();
        picker->changed = [=](const QString &) {
            load();
            notify();
        };
        auto edit = [=](std::function<void()> f) {
            if (*busy) {
                return;
            }
            f();
            fill();
            live();
            notify();
        };
        auto perChannel = [=](auto set) {
            const int ch = channel->currentIndex() - 1;
            for (int c = 0; c < 3; ++c) {
                if (ch < 0 || ch == c) {
                    set(c);
                }
            }
        };
        connect(bright.slider, &QSlider::valueChanged, this, [=](int v) {
            edit([=] {
                perChannel([&](int c) {
                    st->color.brightness[c] = (v - 50) * 2;
                });
            });
        });
        connect(contrast.slider, &QSlider::valueChanged, this, [=](int v) {
            edit([=] {
                perChannel([&](int c) {
                    st->color.contrast[c] = v * 2;
                });
            });
        });
        connect(gamma.slider, &QSlider::valueChanged, this, [=](int v) {
            edit([=] {
                perChannel([&](int c) {
                    st->color.gamma[c] = v / 100.0;
                });
            });
        });
        connect(vib.slider, &QSlider::valueChanged, this, [=](int v) {
            edit([=] {
                st->vibrance = v;
            });
        });
        connect(hueRow.slider, &QSlider::valueChanged, this, [=](int v) {
            edit([=] {
                st->hue = v;
            });
        });
        connect(channel, &QComboBox::currentIndexChanged, this, [=] {
            fill();
        });
        connect(nv, &QRadioButton::toggled, this, [=](bool on) {
            edit([=] {
                st->nvidia = on;
            });
        });
        connect(refGroup, &QButtonGroup::idClicked, this, [=] {
            preview();
        });
        page->banner->restore = [=] {
            edit([=] {
                *st = State();
                st->nvidia = true;
            });
        };
        m_editors << Editor{[=] {
                                return !(*st == *saved);
                            },
                            [=](QWidget *) {
                                const QString out = picker->current();
                                Ccc::saveColor(out, st->nvidia ? st->color : Ccc::Color());
                                settings().setValue(Ccc::colorKey(out) + QStringLiteral("/vibrance"), st->nvidia ? st->vibrance : 50);
                                settings().setValue(Ccc::colorKey(out) + QStringLiteral("/hue"), st->nvidia ? st->hue : 0);
                                settings().setValue(Ccc::colorKey(out) + QStringLiteral("/nvColor"), st->nvidia);
                                live();
                                *saved = *st;
                            },
                            [=] {
                                *st = *saved;
                                fill();
                                live();
                            }};
        return page;
    }

    // ---- Rotate display ----
    QWidget *rotate()
    {
        auto *page = new PageFrame(tr7("Rotate display"),
                                   tr7("NVIDIA NVRotate allows you to change the orientation of your desktop. This is useful if you have rotated your physical monitor and want to change the desktop orientation accordingly."),
                                   QStringLiteral("disp/banner-display.png"));
        auto *panel = new RoundPanel;
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 4, 16, 16);
        v->addWidget(step(tr7("1. Select the display you would like to change.")));
        auto *picker = new DisplayPicker;
        v->addWidget(picker);
        v->addWidget(step(tr7("2. Choose the orientation.")));
        auto *row = new QHBoxLayout;
        row->setContentsMargins(22, 0, 0, 0);
        auto *radios = new QVBoxLayout;
        auto *group = new QButtonGroup(page);
        const QList<QPair<int, QString>> choices{{1, tr7("Landscape")}, {2, tr7("Portrait")}, {4, tr7("Landscape (flipped)")},
                                                 {8, tr7("Portrait (flipped)")}};
        for (const auto &c : choices) {
            auto *r = new QRadioButton(c.second);
            group->addButton(r, c.first);
            radios->addWidget(r);
        }
        radios->addStretch(1);
        row->addLayout(radios);
        row->addSpacing(60);
        auto *ccw = new QToolButton;
        ccw->setText(QStringLiteral("↶"));
        ccw->setAutoRaise(true);
        auto *preview = new QLabel;
        preview->setFixedSize(110, 110);
        preview->setAlignment(Qt::AlignCenter);
        auto *cw = new QToolButton;
        cw->setText(QStringLiteral("↷"));
        cw->setAutoRaise(true);
        QFont af = ccw->font();
        af.setPixelSize(20);
        ccw->setFont(af);
        cw->setFont(af);
        row->addWidget(ccw);
        row->addWidget(preview);
        row->addWidget(cw);
        row->addStretch(1);
        v->addLayout(row);
        page->body->addWidget(panel);
        page->show(tr7("Rotates the image on the selected display by 90, 180 or 270 degrees."),
                   tr7("A monitor rotated for reading long documents."));
        auto st = std::make_shared<int>(1);
        auto saved = std::make_shared<int>(1);
        auto fill = [=] {
            group->button(*st)->setChecked(true);
            const int deg = *st == 2 ? -90 : *st == 4 ? 180 : *st == 8 ? 90 : 0;
            QImage img = QImage(pic(QStringLiteral("2305"))).scaled(100, 75, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            preview->setPixmap(QPixmap::fromImage(img.transformed(QTransform().rotate(deg), Qt::SmoothTransformation)));
        };
        auto load = [=] {
            *st = picker->output().rotation;
            *saved = *st;
            fill();
        };
        load();
        picker->changed = [=](const QString &) {
            load();
            notify();
        };
        connect(group, &QButtonGroup::idClicked, this, [=](int id) {
            *st = id;
            fill();
            notify();
        });
        static const int order[4] = {1, 8, 4, 2}; // clockwise: normal, right, inverted, left
        auto turn = [=](int dir) {
            int i = 0;
            while (order[i] != *st) {
                ++i;
            }
            *st = order[(i + dir + 4) % 4];
            fill();
            notify();
        };
        connect(cw, &QToolButton::clicked, this, [=] {
            turn(1);
        });
        connect(ccw, &QToolButton::clicked, this, [=] {
            turn(-1);
        });
        page->banner->restore = [=] {
            *st = 1;
            fill();
            notify();
        };
        m_editors << Editor{[=] {
                                return *st != *saved;
                            },
                            [=](QWidget *parent) {
                                const QString out = picker->current();
                                Ccc::kscreen({QStringLiteral("output.%1.rotation.%2").arg(out, Ccc::rotationWord(*st))});
                                if (!confirmChange(parent)) {
                                    Ccc::kscreen({QStringLiteral("output.%1.rotation.%2").arg(out, Ccc::rotationWord(*saved))});
                                }
                                picker->reload();
                                load();
                            },
                            [=] {
                                *st = *saved;
                                fill();
                            }};
        return page;
    }

    // ---- View HDCP status ----
    QWidget *hdcp()
    {
        auto *page = new PageFrame(tr7("View HDCP status"),
                                   tr7("HDCP (High-bandwidth Digital Content Protection) protects high-definition content. On this page you can check whether your computer supports HDCP."),
                                   QStringLiteral("disp/banner-display.png"));
        page->banner->setRestoreVisible(false);
        auto *panel = new RoundPanel;
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 4, 16, 16);
        v->addWidget(step(tr7("Select the display you want to watch movies on.")));
        auto *picker = new DisplayPicker;
        v->addWidget(picker);
        auto *head = step(QString());
        v->addWidget(head);
        auto *status = new QLabel;
        status->setContentsMargins(22, 0, 0, 6);
        v->addWidget(status);
        auto *picture = new QLabel;
        picture->setContentsMargins(22, 0, 0, 0);
        v->addWidget(picture);
        page->body->addWidget(panel);
        page->show(tr7("To play protected movies (Blu-ray, HD broadcasts), both the graphics processor and the display must support HDCP."), {});
        auto fill = [=] {
            const QString out = picker->current();
            const QString prop = outputProperty(out, QStringLiteral("Content Protection"));
            const bool ok = !prop.isNull();
            head->setText(ok ? tr7("This system supports HDCP.") : tr7("This system does not support HDCP."));
            status->setText(QStringLiteral("<img src=\"%1\"> &nbsp;%2")
                                .arg(pic(ok ? QStringLiteral("1852") : QStringLiteral("1853")),
                                     ok ? tr7("The graphics processor and the display support HDCP.") : tr7("The display does not support HDCP mode.")));
            picture->setPixmap(QPixmap(pic(ok ? QStringLiteral("1851") : QStringLiteral("1849"))));
        };
        fill();
        picker->changed = [=](const QString &) {
            fill();
        };
        return page;
    }

    // ---- Set up digital audio ----
    QWidget *digitalAudio()
    {
        auto *page = new PageFrame(tr7("Set up digital audio"),
                                   tr7("The graphics processor can send audio through HDMI and DisplayPort connectors. Here you can choose which displays appear as audio devices."),
                                   QStringLiteral("disp/banner-display.png"));
        auto *panel = new RoundPanel;
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 4, 16, 16);
        v->addWidget(step(tr7("1. Check the displays that will appear as audio devices in the sound settings:")));
        auto *g = new QGridLayout;
        g->setContentsMargins(22, 0, 0, 0);
        g->setHorizontalSpacing(12);
        g->addWidget(boldLabel(gpuName()), 0, 0, 1, 3);
        // the connectors of the NVIDIA card (vendor 0x10de) with sound: HDMI and DisplayPort
        QStringList connectors;
        const QDir drm(QStringLiteral("/sys/class/drm"));
        for (const QString &c : drm.entryList({QStringLiteral("card?-*")}, QDir::AllEntries | QDir::System)) {
            const QString vendor = Ccc::readFile(drm.absoluteFilePath(c.section(QLatin1Char('-'), 0, 0)) + QStringLiteral("/device/vendor")).trimmed();
            if (vendor == QLatin1String("0x10de") && (c.contains(QLatin1String("HDMI")) || c.contains(QLatin1String("-DP-")))) {
                connectors << c;
            }
        }
        const QString card = audioCard();
        auto combos = std::make_shared<QList<QComboBox *>>();
        int row = 1;
        for (const QString &c : std::as_const(connectors)) {
            const bool hdmi = c.contains(QLatin1String("HDMI"));
            auto *icon = new QLabel;
            icon->setPixmap(QPixmap(pic(hdmi ? QStringLiteral("5095") : QStringLiteral("5096"))));
            g->addWidget(icon, row, 0);
            g->addWidget(new QLabel(hdmi ? QStringLiteral("HDMI -") : QStringLiteral("DisplayPort -")), row, 1);
            auto *combo = new QComboBox;
            combo->setMinimumWidth(240);
            const bool connected = Ccc::readFile(drm.absoluteFilePath(c) + QStringLiteral("/status")).trimmed() == QLatin1String("connected");
            if (connected) {
                QString name;
                for (const Ccc::Output &o : Ccc::outputs()) {
                    if (c.endsWith(o.name.section(QLatin1Char('-'), 0, 0)) || c.contains(o.name)) {
                        name = displayName(o);
                    }
                }
                combo->addItem(name.isEmpty() ? tr7("Connected display") : name);
            } else {
                combo->addItem(tr7("Nothing connected"));
            }
            combo->addItem(tr7("Turn off audio"));
            g->addWidget(combo, row, 2);
            *combos << combo;
            ++row;
        }
        if (connectors.isEmpty()) {
            g->addWidget(new QLabel(tr7("This graphics processor has no HDMI or DisplayPort connectors.")), row, 0, 1, 3);
        }
        g->setColumnStretch(3, 1);
        v->addLayout(g);
        v->addWidget(step(tr7("2. Set up the playback devices in the sound settings.")));
        auto *lk = link(tr7("Open sound settings"), [] {
            QProcess::startDetached(QStringLiteral("seven-control"), {QStringLiteral("--sound")});
        });
        lk->setContentsMargins(22, 0, 0, 0);
        v->addWidget(lk);
        page->body->addWidget(panel);
        page->show(tr7("If you turn off audio, the audio part of the graphics processor will not appear as a playback device."),
                   tr7("Sending sound to a TV through an HDMI cable."));
        auto off = std::make_shared<bool>(!card.isEmpty() && Ccc::run(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("cards")}).contains(QLatin1String("Active Profile: off")));
        auto saved = std::make_shared<bool>(*off);
        auto fill = [=] {
            for (QComboBox *c : *combos) {
                c->blockSignals(true);
                c->setCurrentIndex(*off ? 1 : 0);
                c->blockSignals(false);
            }
        };
        fill();
        for (QComboBox *c : *combos) {
            connect(c, &QComboBox::currentIndexChanged, this, [=](int i) {
                *off = i == 1;
                fill();
                notify();
            });
        }
        page->banner->restore = [=] {
            *off = false;
            fill();
            notify();
        };
        m_editors << Editor{[=] {
                                return *off != *saved;
                            },
                            [=](QWidget *) {
                                if (!card.isEmpty()) {
                                    QString profile = QStringLiteral("off");
                                    if (!*off) {
                                        // the first HDMI / DisplayPort output profile the card offers
                                        const QString cards = Ccc::run(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("cards")});
                                        static const QRegularExpression re(QStringLiteral("(output:hdmi-stereo[^:\\s]*)"));
                                        const auto m = re.match(cards.mid(cards.indexOf(card)));
                                        profile = m.hasMatch() ? m.captured(1) : QStringLiteral("output:hdmi-stereo");
                                    }
                                    Ccc::run(QStringLiteral("pactl"), {QStringLiteral("set-card-profile"), card, profile});
                                }
                                *saved = *off;
                            },
                            [=] {
                                *off = *saved;
                                fill();
                            }};
        return page;
    }

    // ---- Adjust desktop size and position ----
    QWidget *sizePosition()
    {
        auto *page = new PageFrame(tr7("Adjust desktop size and position"),
                                   tr7("You can choose the scaling mode for the display and resize the desktop if its edges are cut off on the screen."),
                                   QStringLiteral("disp/banner-display.png"));
        auto *panel = new RoundPanel;
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 4, 16, 16);
        v->addWidget(step(tr7("1. Select the display you would like to change.")));
        auto *picker = new DisplayPicker;
        v->addWidget(picker);
        v->addWidget(step(tr7("2. Apply the following settings.")));
        auto *tabs = new QTabWidget;
        tabs->setMaximumWidth(520);
        auto *scaleTab = new QWidget;
        auto *sv = new QVBoxLayout(scaleTab);
        sv->addWidget(new QLabel(tr7("Select a scaling mode:")));
        auto *group = new QButtonGroup(page);
        const QList<QPair<QString, QString>> modes{{QStringLiteral("Full aspect"), tr7("Aspect ratio")},
                                                   {QStringLiteral("Full"), tr7("Full-screen")},
                                                   {QStringLiteral("Center"), tr7("No scaling")}};
        for (int i = 0; i < modes.size(); ++i) {
            auto *r = new QRadioButton(modes[i].second);
            group->addButton(r, i);
            sv->addWidget(r);
        }
        sv->addSpacing(8);
        sv->addWidget(new QLabel(tr7("Perform scaling on:")));
        auto *where = new QComboBox;
        where->addItems({tr7("GPU"), tr7("Display")});
        where->setMaximumWidth(170);
        sv->addWidget(where);
        auto *override_ = new QCheckBox(tr7("Override the scaling mode set by games and programs"));
        sv->addWidget(override_);
        sv->addStretch(1);
        tabs->addTab(scaleTab, tr7("Scaling"));
        auto *sizeTab = new QWidget;
        auto *zv = new QVBoxLayout(sizeTab);
        auto *resize = new QCheckBox(tr7("Enable desktop resizing"));
        zv->addWidget(resize);
        auto *border = new QGridLayout;
        border->addWidget(new QLabel(tr7("Width:")), 0, 0);
        auto *hb = new QSpinBox;
        hb->setRange(0, 128);
        hb->setSuffix(tr7(" px"));
        border->addWidget(hb, 0, 1);
        border->addWidget(new QLabel(tr7("Height:")), 1, 0);
        auto *vb = new QSpinBox;
        vb->setRange(0, 128);
        vb->setSuffix(tr7(" px"));
        border->addWidget(vb, 1, 1);
        border->setColumnStretch(2, 1);
        zv->addLayout(border);
        zv->addStretch(1);
        tabs->addTab(sizeTab, tr7("Size"));
        auto *row = new QHBoxLayout;
        row->setContentsMargins(22, 0, 0, 0);
        row->addWidget(tabs, 1);
        auto *pv = new QLabel;
        row->addWidget(pv, 0, Qt::AlignTop);
        v->addLayout(row);
        page->body->addWidget(panel);
        page->describe(override_, tr7("The scaling mode will be applied even when a full-screen game sets its own."), {});
        page->show(tr7("Scaling sets how an image with a resolution lower than the native resolution of the display fills the screen."),
                   tr7("Old 4:3 games on a widescreen display without stretching."));
        struct State {
            int mode = 0, h = 0, v = 0;
            bool resize = false, gpu = true, over = false;
            bool operator==(const State &o) const
            {
                return mode == o.mode && h == o.h && v == o.v && resize == o.resize && gpu == o.gpu && over == o.over;
            }
        };
        auto st = std::make_shared<State>();
        auto saved = std::make_shared<State>();
        auto busy = std::make_shared<bool>(false);
        auto fill = [=] {
            *busy = true;
            group->button(st->mode)->setChecked(true);
            where->setCurrentIndex(st->gpu ? 0 : 1);
            override_->setChecked(st->over);
            resize->setChecked(st->resize);
            hb->setValue(st->h);
            vb->setValue(st->v);
            const bool under = hasOutputProperty(picker->current(), QStringLiteral("underscan"));
            hb->setEnabled(st->resize && under);
            vb->setEnabled(st->resize && under);
            resize->setEnabled(under);
            pv->setPixmap(QPixmap(pic(st->mode == 2 ? QStringLiteral("5101") : QStringLiteral("5100"))));
            *busy = false;
        };
        auto load = [=] {
            const QString out = picker->current();
            const QString mode = outputProperty(out, QStringLiteral("scaling mode"));
            st->mode = mode == QLatin1String("Full") ? 1 : mode == QLatin1String("Center") ? 2 : 0;
            st->resize = outputProperty(out, QStringLiteral("underscan")).startsWith(QLatin1String("on"));
            st->h = outputProperty(out, QStringLiteral("underscan hborder")).section(QLatin1Char(' '), 0, 0).toInt();
            st->v = outputProperty(out, QStringLiteral("underscan vborder")).section(QLatin1Char(' '), 0, 0).toInt();
            st->gpu = settings().value(Ccc::colorKey(out) + QStringLiteral("/scaleOnGpu"), true).toBool();
            st->over = settings().value(Ccc::colorKey(out) + QStringLiteral("/scaleOverride"), false).toBool();
            *saved = *st;
            fill();
        };
        load();
        picker->changed = [=](const QString &) {
            load();
            notify();
        };
        auto edit = [=](std::function<void()> f) {
            if (!*busy) {
                f();
                fill();
                notify();
            }
        };
        connect(group, &QButtonGroup::idClicked, this, [=](int id) {
            edit([=] {
                st->mode = id;
            });
        });
        connect(where, &QComboBox::currentIndexChanged, this, [=](int i) {
            edit([=] {
                st->gpu = i == 0;
            });
        });
        connect(override_, &QCheckBox::toggled, this, [=](bool on) {
            edit([=] {
                st->over = on;
            });
        });
        connect(resize, &QCheckBox::toggled, this, [=](bool on) {
            edit([=] {
                st->resize = on;
            });
        });
        connect(hb, &QSpinBox::valueChanged, this, [=](int x) {
            edit([=] {
                st->h = x;
            });
        });
        connect(vb, &QSpinBox::valueChanged, this, [=](int x) {
            edit([=] {
                st->v = x;
            });
        });
        page->banner->restore = [=] {
            edit([=] {
                *st = State();
            });
        };
        m_editors << Editor{[=] {
                                return !(*st == *saved);
                            },
                            [=](QWidget *) {
                                const QString out = picker->current();
                                const QString mode = modes[st->mode].first;
                                // "None" leaves scaling to the display itself
                                const QString value = st->gpu ? mode : QStringLiteral("None");
                                setOutputProperty(out, QStringLiteral("scaling mode"), value);
                                settings().setValue(Ccc::colorKey(out) + QStringLiteral("/scaling"), value);
                                settings().setValue(Ccc::colorKey(out) + QStringLiteral("/scaleOnGpu"), st->gpu);
                                settings().setValue(Ccc::colorKey(out) + QStringLiteral("/scaleOverride"), st->over);
                                if (hasOutputProperty(out, QStringLiteral("underscan"))) {
                                    setOutputProperty(out, QStringLiteral("underscan"), st->resize ? QStringLiteral("on") : QStringLiteral("off"));
                                    setOutputProperty(out, QStringLiteral("underscan hborder"), QString::number(st->h));
                                    setOutputProperty(out, QStringLiteral("underscan vborder"), QString::number(st->v));
                                }
                                *saved = *st;
                            },
                            [=] {
                                *st = *saved;
                                fill();
                            }};
        return page;
    }

    // ---- Set up multiple displays ----
    QWidget *multipleDisplays()
    {
        auto *page = new PageFrame(tr7("Set up multiple displays"),
                                   tr7("NVIDIA nView technology allows you to specify how you use multiple displays."),
                                   QStringLiteral("disp/banner-display.png"));
        auto *panel = new RoundPanel;
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 4, 16, 16);
        v->addWidget(step(tr7("1. Select the displays you want to use.")));
        auto *list = new QListWidget;
        list->setFixedSize(400, 100);
        auto *listRow = new QHBoxLayout;
        listRow->setContentsMargins(22, 0, 0, 0);
        listRow->addWidget(list);
        listRow->addStretch(1);
        v->addLayout(listRow);
        auto *missing = link(tr7("My display is not shown..."), [page] {
            page->show(tr7("Check that the display is turned on and the cable is firmly connected. After it is connected, the display appears in the list."), {});
        });
        missing->setContentsMargins(22, 4, 0, 0);
        v->addWidget(missing);
        v->addWidget(step(tr7("2. Verify the display configuration.")));
        auto *canvas = new Arrangement;
        auto *cr = new QHBoxLayout;
        cr->setContentsMargins(22, 0, 0, 0);
        cr->addWidget(canvas);
        cr->addStretch(1);
        v->addLayout(cr);
        auto *legend = new QLabel(tr7("<img src=\"%1\"> Primary display").arg(pic(QStringLiteral("32947"))));
        legend->setContentsMargins(26, 4, 0, 0);
        v->addWidget(legend);
        page->body->addWidget(panel);
        page->show(tr7("Drag the display icons to arrange them the way the displays stand on your desk. Right-click to make a display primary."),
                   tr7("Extending the desktop to a second monitor or TV."));
        auto st = std::make_shared<QList<Arrangement::Screen>>();
        auto saved = std::make_shared<QList<Arrangement::Screen>>();
        auto fill = [=] {
            list->blockSignals(true);
            list->clear();
            auto *hdr = new QListWidgetItem(gpuName(), list);
            hdr->setFlags(Qt::NoItemFlags);
            hdr->setForeground(QColor(0x1e, 0x50, 0xa0));
            for (int i = 0; i < st->size(); ++i) {
                const auto &s = st->at(i);
                auto *it = new QListWidgetItem(QIcon(pic(QString::number(32851 + i))), s.name, list);
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
                it->setCheckState(s.enabled ? Qt::Checked : Qt::Unchecked);
                it->setData(Qt::UserRole, i);
            }
            list->blockSignals(false);
            canvas->screens = *st;
            canvas->update();
        };
        auto load = [=] {
            st->clear();
            const QString primary = Ccc::run(QStringLiteral("xrandr"), {}).section(QStringLiteral(" connected primary"), 0, 0).section(QLatin1Char('\n'), -1);
            for (const Ccc::Output &o : Ccc::outputs()) {
                Arrangement::Screen s;
                s.output = o.name;
                s.name = displayName(o);
                s.enabled = o.enabled;
                const Ccc::Mode m = o.mode(o.currentMode.isEmpty() ? o.preferredMode : o.currentMode);
                const bool turned = o.rotation == 2 || o.rotation == 8;
                s.geometry = QRect(geometryOf(o.name).topLeft(), turned ? QSize(m.h, m.w) : QSize(m.w, m.h));
                s.primary = o.name == primary;
                *st << s;
            }
            *saved = *st;
            fill();
        };
        load();
        canvas->changed = [=](const QList<Arrangement::Screen> &s) {
            *st = s;
            fill();
            notify();
        };
        connect(list, &QListWidget::itemChanged, this, [=](QListWidgetItem *it) {
            const int i = it->data(Qt::UserRole).toInt();
            const bool on = it->checkState() == Qt::Checked;
            int enabled = 0;
            for (const auto &s : *st) {
                enabled += s.enabled;
            }
            if (!on && enabled <= 1) {
                fill(); // at least one display stays on
                return;
            }
            (*st)[i].enabled = on;
            fill();
            notify();
        });
        page->banner->restore = [=] {
            load();
            notify();
        };
        m_editors << Editor{[=] {
                                return *st != *saved;
                            },
                            [=](QWidget *parent) {
                                auto args = [](const QList<Arrangement::Screen> &l) {
                                    QStringList a;
                                    for (const auto &s : l) {
                                        if (s.enabled) {
                                            a << QStringLiteral("output.%1.enable").arg(s.output)
                                              << QStringLiteral("output.%1.position.%2,%3").arg(s.output).arg(s.geometry.x()).arg(s.geometry.y());
                                            if (s.primary) {
                                                a << QStringLiteral("output.%1.primary").arg(s.output);
                                            }
                                        } else {
                                            a << QStringLiteral("output.%1.disable").arg(s.output);
                                        }
                                    }
                                    return a;
                                };
                                Ccc::kscreen(args(*st));
                                if (!confirmChange(parent)) {
                                    Ccc::kscreen(args(*saved));
                                }
                                load();
                            },
                            [=] {
                                *st = *saved;
                                fill();
                            }};
        return page;
    }

    // ---- Video: colour ----
    QWidget *videoColor()
    {
        auto *page = new PageFrame(tr7("Adjust video color settings"),
                                   tr7("Adjusting the video color settings lets you choose the best display settings for video on your display. It is recommended to play a video while you adjust, so that you can see the changes."),
                                   QStringLiteral("disp/banner-video.png"));
        auto *panel = new RoundPanel;
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 4, 16, 16);
        v->addWidget(step(tr7("1. Select the display you would like to change.")));
        auto *picker = new DisplayPicker;
        v->addWidget(picker);
        v->addWidget(step(tr7("2. How do you make color adjustments?")));
        auto *player = new QRadioButton(tr7("With the video player settings"));
        auto *nv = new QRadioButton(tr7("With the NVIDIA settings"));
        for (auto *r : {player, nv}) {
            auto *h = new QHBoxLayout;
            h->setContentsMargins(22, 0, 0, 0);
            h->addWidget(r);
            v->addLayout(h);
        }
        auto *tabs = new QTabWidget;
        tabs->setMaximumWidth(420);
        auto *colorTab = new QWidget;
        auto *cg = new QGridLayout(colorTab);
        const SliderRow b = sliderRow(cg, 0, QStringLiteral("v-brightness"), tr7("Brightness:"), 0, 100);
        const SliderRow c = sliderRow(cg, 1, QStringLiteral("v-contrast"), tr7("Contrast:"), 0, 100);
        const SliderRow h = sliderRow(cg, 2, QStringLiteral("v-hue"), tr7("Hue:"), 0, 359);
        const SliderRow s = sliderRow(cg, 3, QStringLiteral("v-saturation"), tr7("Saturation:"), 0, 100);
        cg->setRowStretch(4, 1);
        tabs->addTab(colorTab, tr7("Color"));
        auto *gammaTab = new QWidget;
        auto *gg = new QGridLayout(gammaTab);
        const SliderRow gm = sliderRow(gg, 0, QStringLiteral("gamma"), tr7("Gamma:"), 30, 280);
        gg->setRowStretch(1, 1);
        tabs->addTab(gammaTab, tr7("Gamma"));
        auto *advTab = new QWidget;
        auto *ag = new QVBoxLayout(advTab);
        auto *rangeRow = new QHBoxLayout;
        rangeRow->addWidget(new QLabel(tr7("Dynamic range:")));
        auto *range = new QComboBox;
        range->addItems({tr7("Limited (16-235)"), tr7("Full (0-255)")});
        rangeRow->addWidget(range);
        rangeRow->addStretch(1);
        ag->addLayout(rangeRow);
        ag->addWidget(new QLabel(tr7("Apply the following digital image enhancements")));
        auto *dyn = new QCheckBox(tr7("Dynamic contrast enhancement"));
        auto *enh = new QCheckBox(tr7("Color enhancement"));
        ag->addWidget(dyn);
        ag->addWidget(enh);
        ag->addStretch(1);
        tabs->addTab(advTab, tr7("Advanced"));
        auto *tr = new QHBoxLayout;
        tr->setContentsMargins(40, 4, 0, 0);
        tr->addWidget(tabs);
        tr->addStretch(1);
        v->addLayout(tr);
        page->body->addWidget(panel);
        page->describe(nv, tr7("Select this option if you want to use the color settings set in the NVIDIA Control Panel."),
                       tr7("The color settings of the video player are not satisfactory."));
        page->show(tr7("The settings apply to mpv-based video players. On Linux the driver does not process video colors separately from the desktop."),
                   tr7("The same color settings for all video players."));
        return videoPage(page, {{QStringLiteral("brightness"), b, 50, true}, {QStringLiteral("contrast"), c, 50, true}, {QStringLiteral("hue"), h, 0, false},
                                {QStringLiteral("saturation"), s, 50, true}, {QStringLiteral("gamma"), gm, 100, false}},
                         nv, player, range, {{QStringLiteral("dyncontrast"), dyn}, {QStringLiteral("colorenh"), enh}}, tabs);
    }

    // ---- Video: image ----
    QWidget *videoImage()
    {
        auto *page = new PageFrame(tr7("Adjust video image settings"),
                                   tr7("Your graphics processor has PureVideo technology, a dedicated video processing core. For the best results, play a video and watch how the image changes."),
                                   QStringLiteral("disp/banner-video.png"));
        auto *panel = new RoundPanel;
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 4, 16, 16);
        v->addWidget(step(tr7("1. Select the display you would like to change.")));
        auto *picker = new DisplayPicker;
        v->addWidget(picker);
        v->addWidget(step(tr7("2. Apply the following settings to video:")));
        auto *row = new QHBoxLayout;
        row->setContentsMargins(22, 0, 0, 0);
        auto group = [&](const QString &title, const QString &key) {
            auto *box = new QGroupBox(title);
            auto *bv = new QGridLayout(box);
            auto *player = new QRadioButton(tr7("Use the video player setting"));
            auto *nv = new QRadioButton(tr7("Use the NVIDIA setting"));
            bv->addWidget(player, 0, 0, 1, 6);
            bv->addWidget(nv, 1, 0, 1, 6);
            const SliderRow s = sliderRow(bv, 2, QStringLiteral("v-contrast"), QString(), 0, 100);
            auto *bg = new QButtonGroup(box);
            bg->addButton(player, 0);
            bg->addButton(nv, 1);
            const bool on = mpvSetting(key + QStringLiteral("On"), 0) != 0;
            nv->setChecked(on);
            player->setChecked(!on);
            s.slider->setValue(mpvSetting(key, 0));
            s.value->setText(QStringLiteral("%1%").arg(s.slider->value()));
            s.slider->setEnabled(on);
            connect(bg, &QButtonGroup::idClicked, this, [=](int id) {
                s.slider->setEnabled(id == 1);
                setMpvSetting(key + QStringLiteral("On"), id);
                notify();
            });
            connect(s.slider, &QSlider::valueChanged, this, [=](int x) {
                s.value->setText(QStringLiteral("%1%").arg(x));
                setMpvSetting(key, x);
                notify();
            });
            return box;
        };
        auto *left = new QVBoxLayout;
        left->addWidget(group(tr7("Edge enhancement"), QStringLiteral("edge")));
        left->addWidget(group(tr7("Noise reduction"), QStringLiteral("noise")));
        row->addLayout(left);
        auto *deint = new QGroupBox(tr7("Deinterlacing"));
        auto *dv = new QVBoxLayout(deint);
        auto *telecine = new QCheckBox(tr7("Use inverse telecine"));
        telecine->setChecked(mpvSetting(QStringLiteral("telecine"), 1) != 0);
        dv->addWidget(telecine);
        dv->addStretch(1);
        connect(telecine, &QCheckBox::toggled, this, [=](bool on) {
            setMpvSetting(QStringLiteral("telecine"), on);
            notify();
        });
        row->addWidget(deint, 0, Qt::AlignTop);
        row->addStretch(1);
        v->addLayout(row);
        page->body->addWidget(panel);
        page->show(tr7("Edge enhancement makes the image sharper; noise reduction removes the \"grain\". The settings apply to mpv-based video players."),
                   tr7("Watching low-quality video."));
        m_editors << Editor{[=] {
                                return m_videoDirty;
                            },
                            [=](QWidget *) {
                                writeMpvConf();
                                m_videoDirty = false;
                            },
                            [=] {
                                m_videoDirty = false;
                            }};
        return page;
    }

private:
    struct Editor {
        std::function<bool()> dirty;
        std::function<void(QWidget *)> apply;
        std::function<void()> cancel;
    };

    /** the canvas of "Verify your display configuration": green numbered tiles on dark grey, drag to arrange */
    class Arrangement : public QWidget
    {
    public:
        struct Screen {
            QString output, name;
            QRect geometry;
            bool enabled = true, primary = false;
            bool operator==(const Screen &o) const
            {
                return output == o.output && geometry == o.geometry && enabled == o.enabled && primary == o.primary;
            }
        };
        QList<Screen> screens;
        std::function<void(const QList<Screen> &)> changed;

        Arrangement()
        {
            setFixedSize(400, 180);
        }

    protected:
        double scale() const
        {
            QRect all;
            for (const auto &s : screens) {
                if (s.enabled) {
                    all |= s.geometry;
                }
            }
            return all.isEmpty() ? 0.1 : qMin(0.8 * width() / all.width(), 0.8 * height() / all.height());
        }
        QRect tileRect(const Screen &s) const
        {
            QRect all;
            for (const auto &x : screens) {
                if (x.enabled) {
                    all |= x.geometry;
                }
            }
            const double k = scale();
            const QPoint origin(qRound((width() - all.width() * k) / 2 - all.x() * k), qRound((height() - all.height() * k) / 2 - all.y() * k));
            return QRect(origin + QPoint(qRound(s.geometry.x() * k), qRound(s.geometry.y() * k)), QSize(qRound(s.geometry.width() * k), qRound(s.geometry.height() * k)));
        }
        void paintEvent(QPaintEvent *) override
        {
            QPainter p(this);
            p.fillRect(rect(), QColor(0x6e, 0x6e, 0x6e));
            for (int i = 0; i < screens.size(); ++i) {
                const Screen &s = screens.at(i);
                if (!s.enabled) {
                    continue;
                }
                QRect r = tileRect(s);
                if (i == m_drag) {
                    r.translate(m_dragDelta);
                }
                QLinearGradient g(r.topLeft(), r.bottomRight());
                g.setColorAt(0, QColor(0x55, 0xa8, 0x6a));
                g.setColorAt(1, QColor(0x3b, 0x8e, 0x8c));
                p.fillRect(r, g);
                p.setPen(QPen(Qt::black, 2));
                p.drawRect(r.adjusted(1, 1, -1, -1));
                QFont f = font();
                f.setPixelSize(qMax(14, r.height() * 2 / 3));
                f.setBold(true);
                p.setFont(f);
                p.setPen(Qt::white);
                p.drawText(r, Qt::AlignCenter, QString::number(i + 1));
                if (s.primary) {
                    p.drawImage(r.topRight() + QPoint(-24, 4), QImage(pic(QStringLiteral("32949"))));
                }
            }
        }
        int hit(const QPoint &pt) const
        {
            for (int i = screens.size() - 1; i >= 0; --i) {
                if (screens.at(i).enabled && tileRect(screens.at(i)).contains(pt)) {
                    return i;
                }
            }
            return -1;
        }
        void mousePressEvent(QMouseEvent *e) override
        {
            const int h = hit(e->position().toPoint());
            if (h < 0) {
                return;
            }
            if (e->button() == Qt::RightButton) {
                QMenu m;
                QAction *prim = m.addAction(tr7("Make this the primary display"));
                prim->setEnabled(!screens.at(h).primary);
                if (m.exec(e->globalPosition().toPoint()) == prim) {
                    for (int i = 0; i < screens.size(); ++i) {
                        screens[i].primary = i == h;
                    }
                    update();
                    if (changed) {
                        changed(screens);
                    }
                }
                return;
            }
            m_drag = h;
            m_dragStart = e->position().toPoint();
        }
        void mouseMoveEvent(QMouseEvent *e) override
        {
            if (m_drag >= 0) {
                m_dragDelta = e->position().toPoint() - m_dragStart;
                update();
            }
        }
        void mouseReleaseEvent(QMouseEvent *) override
        {
            if (m_drag < 0) {
                return;
            }
            // place the dragged display against the nearest edge of another one (no gaps, no overlap)
            const double k = scale();
            Screen &s = screens[m_drag];
            QRect moved = s.geometry.translated(qRound(m_dragDelta.x() / k), qRound(m_dragDelta.y() / k));
            int best = INT_MAX;
            QRect snapped = moved;
            for (int i = 0; i < screens.size(); ++i) {
                if (i == m_drag || !screens.at(i).enabled) {
                    continue;
                }
                const QRect o = screens.at(i).geometry;
                const QList<QPoint> cands{{o.right() + 1, moved.y()}, {o.left() - moved.width(), moved.y()}, {moved.x(), o.bottom() + 1}, {moved.x(), o.top() - moved.height()}};
                for (QPoint c : cands) {
                    if (c.x() == o.right() + 1 || c.x() == o.left() - moved.width()) {
                        c.setY(qBound(o.top() - moved.height() + 1, c.y(), o.bottom()));
                    } else {
                        c.setX(qBound(o.left() - moved.width() + 1, c.x(), o.right()));
                    }
                    const int d = (c - moved.topLeft()).manhattanLength();
                    if (d < best) {
                        best = d;
                        snapped = QRect(c, moved.size());
                    }
                }
            }
            s.geometry = snapped;
            // the top-left display starts at 0,0
            QRect all;
            for (const auto &x : screens) {
                if (x.enabled) {
                    all |= x.geometry;
                }
            }
            for (auto &x : screens) {
                x.geometry.translate(-all.topLeft());
            }
            m_drag = -1;
            m_dragDelta = {};
            update();
            if (changed) {
                changed(screens);
            }
        }

    private:
        int m_drag = -1;
        QPoint m_dragStart, m_dragDelta;
    };

    static QRect geometryOf(const QString &output)
    {
        static const QRegularExpression re(QStringLiteral("(\\d+)x(\\d+)\\+(\\d+)\\+(\\d+)"));
        for (const QString &l : Ccc::run(QStringLiteral("xrandr"), {}).split(QLatin1Char('\n'))) {
            if (l.startsWith(output + QLatin1Char(' '))) {
                const auto m = re.match(l);
                if (m.hasMatch()) {
                    return QRect(m.captured(3).toInt(), m.captured(4).toInt(), m.captured(1).toInt(), m.captured(2).toInt());
                }
            }
        }
        return {};
    }
    /** the HD Audio card of the NVIDIA GPU in PulseAudio ("alsa_card.pci-0000_01_00.1" -> PCI 0000:01:00.1) */
    static QString audioCard()
    {
        for (const QString &l : Ccc::run(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("cards"), QStringLiteral("short")}).split(QLatin1Char('\n'))) {
            const QString name = l.section(QLatin1Char('\t'), 1, 1);
            if (!name.startsWith(QLatin1String("alsa_card.pci-"))) {
                continue;
            }
            QString pci = name.mid(14);
            pci.replace(QLatin1Char('_'), QLatin1Char(':'));
            const int dot = pci.lastIndexOf(QLatin1Char(':'));
            if (dot > 0) {
                pci[dot] = QLatin1Char('.'); // 0000:01:00:1 -> 0000:01:00.1
            }
            if (Ccc::readFile(QStringLiteral("/sys/bus/pci/devices/") + pci + QStringLiteral("/vendor")).trimmed() == QLatin1String("0x10de")) {
                return name;
            }
        }
        return {};
    }

    // ---- video settings for mpv ----
    static QSettings &videoStore()
    {
        static QSettings s(QDir::homePath() + QStringLiteral("/.config/seven-nvidia/video.conf"), QSettings::IniFormat);
        return s;
    }
    int mpvSetting(const QString &key, int def) const
    {
        return videoStore().value(key, def).toInt();
    }
    void setMpvSetting(const QString &key, int v)
    {
        videoStore().setValue(key, v);
        m_videoDirty = true;
    }
    struct VideoSlider {
        QString key;
        SliderRow row;
        int def;
        bool percent;
    };
    QWidget *videoPage(PageFrame *page, const QList<VideoSlider> &sliders, QRadioButton *nv, QRadioButton *player, QComboBox *range,
                       const QList<QPair<QString, QCheckBox *>> &checks, QTabWidget *tabs)
    {
        const bool on = mpvSetting(QStringLiteral("colorOn"), 0) != 0;
        nv->setChecked(on);
        player->setChecked(!on);
        tabs->setEnabled(on);
        for (const VideoSlider &s : sliders) {
            const int v = mpvSetting(s.key, s.def);
            s.row.slider->setValue(v);
            auto label = [s](int x) {
                return s.key == QLatin1String("gamma") ? QString::number(x / 100.0, 'f', 2).replace(QLatin1Char('.'), QLatin1Char(','))
                    : s.percent                          ? QStringLiteral("%1%").arg(x)
                                                         : QStringLiteral("%1°").arg(x);
            };
            s.row.value->setText(label(v));
            connect(s.row.slider, &QSlider::valueChanged, this, [=](int x) {
                s.row.value->setText(label(x));
                setMpvSetting(s.key, x);
                notify();
            });
        }
        range->setCurrentIndex(mpvSetting(QStringLiteral("range"), 0));
        connect(range, &QComboBox::currentIndexChanged, this, [=](int i) {
            setMpvSetting(QStringLiteral("range"), i);
            notify();
        });
        for (const auto &c : checks) {
            c.second->setChecked(mpvSetting(c.first, 0) != 0);
            connect(c.second, &QCheckBox::toggled, this, [=](bool b) {
                setMpvSetting(c.first, b);
                notify();
            });
        }
        connect(nv, &QRadioButton::toggled, this, [=](bool b) {
            tabs->setEnabled(b);
            setMpvSetting(QStringLiteral("colorOn"), b);
            notify();
        });
        page->banner->restore = [=] {
            for (const VideoSlider &s : sliders) {
                s.row.slider->setValue(s.def);
            }
            range->setCurrentIndex(0);
        };
        return page;
    }
    /** writes our block into ~/.config/mpv/mpv.conf */
    void writeMpvConf()
    {
        QStringList block{QStringLiteral("# seven-nvidia: video colour and image (NVIDIA Control Panel)")};
        if (mpvSetting(QStringLiteral("colorOn"), 0)) {
            block << QStringLiteral("brightness=%1").arg((mpvSetting(QStringLiteral("brightness"), 50) - 50) * 2)
                  << QStringLiteral("contrast=%1").arg((mpvSetting(QStringLiteral("contrast"), 50) - 50) * 2)
                  << QStringLiteral("saturation=%1").arg((mpvSetting(QStringLiteral("saturation"), 50) - 50) * 2)
                  << QStringLiteral("hue=%1").arg(qRound((mpvSetting(QStringLiteral("hue"), 0) > 180 ? mpvSetting(QStringLiteral("hue"), 0) - 360 : mpvSetting(QStringLiteral("hue"), 0)) / 1.8))
                  << QStringLiteral("gamma=%1").arg(qBound(-100, qRound(std::log(mpvSetting(QStringLiteral("gamma"), 100) / 100.0) * 100), 100));
            if (mpvSetting(QStringLiteral("range"), 0) == 1) {
                block << QStringLiteral("video-output-levels=full");
            }
        }
        QStringList vf;
        if (mpvSetting(QStringLiteral("edgeOn"), 0)) {
            vf << QStringLiteral("lavfi=[unsharp=5:5:%1]").arg(mpvSetting(QStringLiteral("edge"), 0) / 50.0, 0, 'f', 2);
        }
        if (mpvSetting(QStringLiteral("noiseOn"), 0)) {
            vf << QStringLiteral("lavfi=[hqdn3d=%1]").arg(mpvSetting(QStringLiteral("noise"), 0) / 10.0, 0, 'f', 1);
        }
        if (!vf.isEmpty()) {
            block << QStringLiteral("vf=") + vf.join(QLatin1Char(','));
        }
        if (mpvSetting(QStringLiteral("telecine"), 1)) {
            block << QStringLiteral("deinterlace=auto");
        }
        block << QStringLiteral("# end seven-nvidia");
        const QString path = QDir::homePath() + QStringLiteral("/.config/mpv/mpv.conf");
        QStringList lines = Ccc::readFile(path).split(QLatin1Char('\n'));
        const int a = lines.indexOf(block.first());
        const int b = lines.indexOf(QStringLiteral("# end seven-nvidia"));
        if (a >= 0 && b >= a) {
            lines.erase(lines.begin() + a, lines.begin() + b + 1);
        }
        while (!lines.isEmpty() && lines.last().isEmpty()) {
            lines.removeLast();
        }
        lines << block;
        QDir().mkpath(QFileInfo(path).path());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(lines.join(QLatin1Char('\n')).toUtf8() + "\n");
        }
    }

    void notify()
    {
        if (changed) {
            changed();
        }
    }

    QList<Editor> m_editors;
    bool m_videoDirty = false;
};
}
