/*
 * "Mouse Properties" of Windows 7: Buttons, Pointers, Pointer Options, Wheel and Hardware tabs.
 *
 *   switch primary and secondary buttons -> libinput left-handed mode (kcminputrc + xinput)
 *   double-click speed, wheel lines      -> kdeglobals [KDE] DoubleClickInterval / WheelScrollLines
 *   pointer scheme                       -> the Xcursor theme (plasma-apply-cursortheme)
 *   pointer speed, enhance precision     -> libinput acceleration speed and the adaptive profile
 *   show location when pressing CTRL     -> the KWin "Track Mouse" effect
 *   hide pointer while typing            -> xbanish, started at login (installed on first use)
 * ClickLock, Snap To, pointer trails, scrolling "one screen at a time" and horizontal scrolling
 * have no counterpart in Plasma: they are shown like in Windows 7 but greyed out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7chrome.h"

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QGroupBox>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QtEndian>

namespace Win7Mouse
{
/* ---------------- settings ---------------- */

struct Settings {
    bool leftHanded = false;
    int doubleClick = 400; // ms
    QString cursorTheme;
    double speed = 0; // libinput -1..1
    bool precision = true; // adaptive acceleration
    bool ctrlLocate = false;
    bool hideTyping = false;
    int wheelLines = 3;
};

inline QString xbanishAutostart()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart/seven-xbanish.desktop");
}

inline Settings load()
{
    Settings s;
    KConfig input(QStringLiteral("kcminputrc"));
    const KConfigGroup mouse(&input, QStringLiteral("Mouse"));
    s.leftHanded = mouse.readEntry("XLbInptLeftHanded", false);
    s.speed = mouse.readEntry("XLbInptPointerAcceleration", 0.0);
    s.precision = !mouse.readEntry("X11LibInputXAccelProfileFlat", mouse.readEntry("XLbInptAccelProfileFlat", false));
    s.cursorTheme = mouse.readEntry("cursorTheme", QStringLiteral("breeze_cursors"));
    KConfig globals(QStringLiteral("kdeglobals"));
    const KConfigGroup kde(&globals, QStringLiteral("KDE"));
    s.doubleClick = kde.readEntry("DoubleClickInterval", 400);
    s.wheelLines = kde.readEntry("WheelScrollLines", 3);
    KConfig kwin(QStringLiteral("kwinrc"));
    const KConfigGroup tm(&kwin, QStringLiteral("Effect-trackmouse"));
    s.ctrlLocate = KConfigGroup(&kwin, QStringLiteral("Plugins")).readEntry("trackmouseEnabled", false) && tm.readEntry("Control", true)
        && !tm.readEntry("Meta", true);
    s.hideTyping = QFile::exists(xbanishAutostart());
    return s;
}

/** libinput pointer devices of the X server: id -> name */
inline QList<QPair<QString, QString>> pointerDevices()
{
    QList<QPair<QString, QString>> list;
    QProcess p;
    p.start(QStringLiteral("xinput"), {QStringLiteral("list"), QStringLiteral("--short")});
    p.waitForFinished(2000);
    static const QRegularExpression re(QStringLiteral("[↳⎜\\s]*(.+?)\\s+id=(\\d+)\\s+\\[slave\\s+pointer"));
    for (const QString &line : QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'))) {
        const auto m = re.match(line);
        if (m.hasMatch() && !m.captured(1).contains(QLatin1String("XTEST"))) {
            list.append({m.captured(2), m.captured(1).trimmed()});
        }
    }
    return list;
}

inline void applyToDevices(const Settings &s)
{
    for (const auto &dev : pointerDevices()) {
        QProcess p;
        p.start(QStringLiteral("xinput"), {QStringLiteral("list-props"), dev.first});
        p.waitForFinished(2000);
        const QString props = QString::fromUtf8(p.readAllStandardOutput());
        auto set = [&dev](const QString &prop, const QStringList &values) {
            QProcess::execute(QStringLiteral("xinput"), QStringList{QStringLiteral("set-prop"), dev.first, prop} + values);
        };
        if (props.contains(QLatin1String("libinput Left Handed Enabled ("))) {
            set(QStringLiteral("libinput Left Handed Enabled"), {s.leftHanded ? QStringLiteral("1") : QStringLiteral("0")});
        }
        if (dev.second.contains(QLatin1String("Touchpad"), Qt::CaseInsensitive)) {
            continue; // the touchpad has its own speed in Plasma
        }
        if (props.contains(QLatin1String("libinput Accel Speed ("))) {
            set(QStringLiteral("libinput Accel Speed"), {QString::number(s.speed, 'f', 2)});
        }
        static const QRegularExpression avail(QStringLiteral("libinput Accel Profiles Available \\(\\d+\\):\\s*([0-9, ]+)"));
        const auto m = avail.match(props);
        if (m.hasMatch()) {
            const int n = m.captured(1).split(QLatin1Char(',')).size();
            QStringList v(n, QStringLiteral("0"));
            v[s.precision ? 0 : 1] = QStringLiteral("1");
            set(QStringLiteral("libinput Accel Profile Enabled"), v);
        }
    }
}

inline void save(const Settings &s, const Settings &old)
{
    KConfig input(QStringLiteral("kcminputrc"));
    KConfigGroup mouse(&input, QStringLiteral("Mouse"));
    mouse.writeEntry("XLbInptLeftHanded", s.leftHanded);
    mouse.writeEntry("XLbInptPointerAcceleration", s.speed);
    mouse.writeEntry("X11LibInputXAccelProfileFlat", !s.precision);
    mouse.writeEntry("XLbInptAccelProfileFlat", !s.precision);
    input.sync();
    applyToDevices(s);

    KConfig globals(QStringLiteral("kdeglobals"));
    KConfigGroup kde(&globals, QStringLiteral("KDE"));
    kde.writeEntry("DoubleClickInterval", s.doubleClick, KConfig::Notify | KConfig::Normal);
    kde.writeEntry("WheelScrollLines", s.wheelLines, KConfig::Notify | KConfig::Normal);
    globals.sync();
    QApplication::setDoubleClickInterval(s.doubleClick);
    QApplication::setWheelScrollLines(s.wheelLines);

    if (s.cursorTheme != old.cursorTheme && !s.cursorTheme.isEmpty()) {
        QProcess::startDetached(QStringLiteral("plasma-apply-cursortheme"), {s.cursorTheme});
    }
    if (s.hideTyping != old.hideTyping) {
        if (s.hideTyping) {
            if (QStandardPaths::findExecutable(QStringLiteral("xbanish")).isEmpty()) {
                QProcess::execute(QStringLiteral("pkexec"), {QStringLiteral("apt-get"), QStringLiteral("install"), QStringLiteral("-y"), QStringLiteral("xbanish")});
            }
            if (!QStandardPaths::findExecutable(QStringLiteral("xbanish")).isEmpty()) {
                QDir().mkpath(QFileInfo(xbanishAutostart()).path());
                QFile f(xbanishAutostart());
                if (f.open(QIODevice::WriteOnly)) {
                    f.write("[Desktop Entry]\nType=Application\nName=Hide pointer while typing\nExec=xbanish\nNoDisplay=true\nX-KDE-autostart-phase=2\n");
                }
                QProcess::startDetached(QStringLiteral("xbanish"), {});
            }
        } else {
            QFile::remove(xbanishAutostart());
            QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-x"), QStringLiteral("xbanish")});
        }
    }
    if (s.ctrlLocate != old.ctrlLocate) {
        KConfig kwin(QStringLiteral("kwinrc"));
        KConfigGroup(&kwin, QStringLiteral("Plugins")).writeEntry("trackmouseEnabled", s.ctrlLocate);
        KConfigGroup tm(&kwin, QStringLiteral("Effect-trackmouse"));
        tm.writeEntry("Control", true);
        tm.writeEntry("Meta", false);
        tm.writeEntry("Alt", false);
        tm.writeEntry("Shift", false);
        kwin.sync();
        QProcess::execute(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                                                     s.ctrlLocate ? QStringLiteral("loadEffect") : QStringLiteral("unloadEffect"), QStringLiteral("trackmouse")});
        QProcess::execute(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("reconfigure")});
    }
}

/* ---------------- Xcursor themes ---------------- */

inline QStringList iconDirs()
{
    return {QDir::homePath() + QStringLiteral("/.local/share/icons"), QDir::homePath() + QStringLiteral("/.icons"), QStringLiteral("/usr/share/icons")};
}

/** (folder name, shown name) of every installed cursor theme */
inline QList<QPair<QString, QString>> cursorThemes()
{
    QList<QPair<QString, QString>> list;
    QStringList seen;
    for (const QString &dir : iconDirs()) {
        for (const QString &t : QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (seen.contains(t) || !QFileInfo::exists(dir + QLatin1Char('/') + t + QStringLiteral("/cursors"))) {
                continue;
            }
            seen << t;
            const QString name = KConfigGroup(KSharedConfig::openConfig(dir + QLatin1Char('/') + t + QStringLiteral("/index.theme"), KConfig::SimpleConfig), QStringLiteral("Icon Theme"))
                                     .readEntry("Name", t);
            list.append({t, name});
        }
    }
    return list;
}

inline QStringList inherits(const QString &theme)
{
    for (const QString &dir : iconDirs()) {
        const QString index = dir + QLatin1Char('/') + theme + QStringLiteral("/index.theme");
        if (QFileInfo::exists(index)) {
            return KConfigGroup(KSharedConfig::openConfig(index, KConfig::SimpleConfig), QStringLiteral("Icon Theme")).readEntry("Inherits", QStringList());
        }
    }
    return {};
}

inline QString cursorFile(const QString &theme, const QStringList &names, int depth = 0)
{
    for (const QString &dir : iconDirs()) {
        for (const QString &n : names) {
            const QString f = dir + QLatin1Char('/') + theme + QStringLiteral("/cursors/") + n;
            if (QFileInfo::exists(f)) {
                return f;
            }
        }
    }
    if (depth < 3) {
        for (const QString &parent : inherits(theme)) {
            const QString f = cursorFile(parent, names, depth + 1);
            if (!f.isEmpty()) {
                return f;
            }
        }
    }
    return {};
}

/** First frame of an Xcursor file, the nominal size closest to 32 */
inline QImage readXcursor(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray d = f.readAll();
    auto u32 = [&d](int off) -> quint32 {
        return off + 4 <= d.size() ? qFromLittleEndian<quint32>(d.constData() + off) : 0;
    };
    if (!d.startsWith("Xcur")) {
        return {};
    }
    const quint32 ntoc = u32(12);
    quint32 bestPos = 0;
    int bestDiff = 1 << 30;
    for (quint32 i = 0; i < ntoc && i < 1000; ++i) {
        const int e = 16 + int(i) * 12;
        if (u32(e) != 0xfffd0002) {
            continue;
        }
        const int diff = std::abs(int(u32(e + 4)) - 32);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestPos = u32(e + 8);
        }
    }
    if (!bestPos) {
        return {};
    }
    const int w = int(u32(int(bestPos) + 16));
    const int h = int(u32(int(bestPos) + 20));
    const int px = int(bestPos) + 36;
    if (w <= 0 || h <= 0 || w > 512 || h > 512 || px + w * h * 4 > d.size()) {
        return {};
    }
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<quint32 *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            line[x] = u32(px + (y * w + x) * 4);
        }
    }
    return img;
}

struct Role {
    QString title;
    QStringList names;
};

inline const QList<Role> &roles()
{
    static const QList<Role> r{
        {tr7("Normal Select"), {QStringLiteral("left_ptr"), QStringLiteral("default"), QStringLiteral("arrow")}},
        {tr7("Help Select"), {QStringLiteral("help"), QStringLiteral("question_arrow"), QStringLiteral("whats_this"), QStringLiteral("left_ptr_help")}},
        {tr7("Working In Background"), {QStringLiteral("left_ptr_watch"), QStringLiteral("progress")}},
        {tr7("Busy"), {QStringLiteral("watch"), QStringLiteral("wait")}},
        {tr7("Precision Select"), {QStringLiteral("crosshair"), QStringLiteral("cross"), QStringLiteral("tcross")}},
        {tr7("Text Select"), {QStringLiteral("xterm"), QStringLiteral("text"), QStringLiteral("ibeam")}},
        {tr7("Handwriting"), {QStringLiteral("pencil"), QStringLiteral("draft")}},
        {tr7("Unavailable"), {QStringLiteral("not-allowed"), QStringLiteral("forbidden"), QStringLiteral("crossed_circle"), QStringLiteral("circle")}},
        {tr7("Vertical Resize"), {QStringLiteral("sb_v_double_arrow"), QStringLiteral("ns-resize"), QStringLiteral("size_ver"), QStringLiteral("v_double_arrow")}},
        {tr7("Horizontal Resize"), {QStringLiteral("sb_h_double_arrow"), QStringLiteral("ew-resize"), QStringLiteral("size_hor"), QStringLiteral("h_double_arrow")}},
        {tr7("Diagonal Resize 1"), {QStringLiteral("size_fdiag"), QStringLiteral("nwse-resize"), QStringLiteral("bd_double_arrow")}},
        {tr7("Diagonal Resize 2"), {QStringLiteral("size_bdiag"), QStringLiteral("nesw-resize"), QStringLiteral("fd_double_arrow")}},
        {tr7("Move"), {QStringLiteral("fleur"), QStringLiteral("size_all"), QStringLiteral("move"), QStringLiteral("all-scroll")}},
        {tr7("Alternate Select"), {QStringLiteral("up_arrow"), QStringLiteral("center_ptr")}},
        {tr7("Link Select"), {QStringLiteral("hand2"), QStringLiteral("pointer"), QStringLiteral("pointing_hand"), QStringLiteral("hand1")}},
    };
    return r;
}

inline QPixmap cursorPixmap(const QString &theme, const Role &role, int box = 32)
{
    QPixmap pm(box, box);
    pm.fill(Qt::transparent);
    const QImage img = readXcursor(cursorFile(theme, role.names));
    if (!img.isNull()) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QImage s = img.width() > box || img.height() > box ? img.scaled(box, box, Qt::KeepAspectRatio, Qt::SmoothTransformation) : img;
        p.drawImage((box - s.width()) / 2, (box - s.height()) / 2, s);
    }
    return pm;
}

/* ---------------- pictures ---------------- */

/** The mouse bitmap of main.cpl; the primary button is the blue one */
class MousePicture : public QWidget
{
public:
    explicit MousePicture(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(126, 116);
    }
    bool left = false;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0xf0, 0xf0, 0xf0));
        p.setPen(QColor(0xe3, 0xe3, 0xe3));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        // drawn from the 256 px picture at double resolution so the edges stay smooth
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPixmap pm = QIcon::fromTheme(left ? QStringLiteral("seven-mouse-buttons-swapped") : QStringLiteral("seven-mouse-buttons")).pixmap(QSize(128, 128), 2.0);
        p.drawPixmap(QRectF((width() - 128) / 2.0, (height() - 128) / 2.0, 128, 128), pm, QRectF(QPointF(0, 0), QSizeF(pm.size())));
    }
};

/** Folder that opens on a double click at the chosen speed */
class TestFolder : public QLabel
{
public:
    explicit TestFolder(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setFixedSize(64, 64);
        setAlignment(Qt::AlignCenter);
        setFrameShape(QFrame::NoFrame);
        setStyleSheet(QStringLiteral("QLabel { background: #ffffff; border: 1px solid #dcdcdc; }"));
        show(false);
    }
    int interval = 400;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        if (m_clock.isValid() && m_clock.elapsed() <= interval) {
            m_open = !m_open;
            show(m_open);
            m_clock.invalidate();
        } else {
            m_clock.start();
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        mousePressEvent(e);
    }

private:
    void show(bool open)
    {
        setPixmap(QIcon::fromTheme(open ? QStringLiteral("seven-mouse-folder-open") : QStringLiteral("seven-mouse-folder")).pixmap(32, 32));
    }
    QElapsedTimer m_clock;
    bool m_open = false;
};

/* ---------------- the dialog ---------------- */

class MouseDialog : public QDialog
{
public:
    explicit MouseDialog(int tab = 0, QWidget *parent = nullptr)
        : QDialog(parent)
        , m_old(load())
        , m_s(m_old)
    {
        setWindowTitle(tr7("Mouse Properties"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cpi-mouse")));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_DeleteOnClose);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 9, 7, 9);
        auto *tabs = new QTabWidget;
        tabs->addTab(buttonsTab(), tr7("Buttons"));
        tabs->addTab(pointersTab(), tr7("Pointers"));
        tabs->addTab(optionsTab(), tr7("Pointer Options"));
        tabs->addTab(wheelTab(), tr7("Wheel"));
        tabs->addTab(hardwareTab(), tr7("Hardware"));
        tabs->setCurrentIndex(tab);
        tabs->setUsesScrollButtons(false);
        tabs->tabBar()->setElideMode(Qt::ElideNone);
        tabs->tabBar()->setExpanding(false);
        v->addWidget(tabs, 1);
        auto *btns = new QHBoxLayout;
        btns->setContentsMargins(0, 4, 0, 0);
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        m_apply = new QPushButton(tr7("Apply"));
        for (QPushButton *b : {ok, cancel, m_apply}) {
            b->setMinimumWidth(75);
            btns->addWidget(b);
        }
        ok->setDefault(true);
        m_apply->setEnabled(false);
        v->addLayout(btns);
        connect(ok, &QPushButton::clicked, this, [this] {
            apply();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] {
            apply();
        });
        // as wide as the five tabs need (Ukrainian names are longer than the English ones)
        setFixedSize(qMax(420, tabs->tabBar()->sizeHint().width() + 30), 500);
    }

private:
    void changed()
    {
        m_apply->setEnabled(true);
    }

    void apply()
    {
        save(m_s, m_old);
        m_old = m_s;
        m_apply->setEnabled(false);
    }

    static QLabel *wrapped(const QString &text)
    {
        auto *l = new QLabel(text);
        l->setWordWrap(true);
        return l;
    }

    static QWidget *page(QVBoxLayout *&v)
    {
        auto *w = new QWidget;
        v = new QVBoxLayout(w);
        v->setContentsMargins(10, 12, 10, 10);
        v->setSpacing(10);
        return w;
    }

    /** Slider with tick marks under it and "Нижче" / "Вище" at the ends */
    static QSlider *ticks(int max)
    {
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(0, max);
        s->setPageStep(1);
        s->setTickPosition(QSlider::TicksBelow);
        s->setTickInterval(1);
        s->setFixedWidth(130);
        return s;
    }

    QWidget *buttonsTab()
    {
        QVBoxLayout *v;
        QWidget *w = page(v);
        auto *config = new QGroupBox(tr7("Button configuration"));
        auto *cg = new QHBoxLayout(config);
        auto *left = new QVBoxLayout;
        auto *swap = new QCheckBox(tr7("Switch primary and secondary buttons"));
        swap->setChecked(m_s.leftHanded);
        left->addWidget(swap);
        left->addWidget(wrapped(tr7("Select this check box to make the button on the right the one you use for primary functions such as selecting and dragging.")));
        left->addStretch(1);
        cg->addLayout(left, 1);
        auto *pic = new MousePicture;
        pic->left = m_s.leftHanded;
        cg->addWidget(pic, 0, Qt::AlignTop);
        v->addWidget(config);
        connect(swap, &QCheckBox::toggled, this, [this, pic](bool on) {
            m_s.leftHanded = on;
            pic->left = on;
            pic->update();
            changed();
        });

        auto *dbl = new QGroupBox(tr7("Double-click speed"));
        auto *dg = new QGridLayout(dbl);
        dg->addWidget(wrapped(tr7("Double-click the folder to test your setting. If the folder does not open or close, try using a slower setting.")), 0, 0, 1, 4);
        auto *folder = new TestFolder;
        folder->interval = m_s.doubleClick;
        dg->addWidget(folder, 0, 4, 2, 1, Qt::AlignCenter);
        dg->addWidget(new QLabel(tr7("Speed:")), 1, 0);
        dg->addWidget(new QLabel(tr7("Slow", "repeat rate")), 1, 1);
        auto *speed = ticks(10);
        // Windows goes from 900 ms (slowest) to 200 ms (fastest)
        speed->setValue(qBound(0, qRound((900 - m_s.doubleClick) / 70.0), 10));
        dg->addWidget(speed, 1, 2);
        dg->addWidget(new QLabel(tr7("Fast", "repeat rate")), 1, 3);
        dg->setColumnStretch(3, 1);
        v->addWidget(dbl);
        connect(speed, &QSlider::valueChanged, this, [this, folder](int val) {
            m_s.doubleClick = 900 - val * 70;
            folder->interval = m_s.doubleClick;
            changed();
        });

        // ClickLock: no counterpart in Plasma, shown greyed out
        auto *lock = new QGroupBox(tr7("ClickLock"));
        auto *lg = new QGridLayout(lock);
        auto *lockOn = new QCheckBox(tr7("Turn on ClickLock"));
        lockOn->setEnabled(false);
        lockOn->setToolTip(unavailable());
        auto *lockSettings = new QPushButton(tr7("Options..."));
        lockSettings->setEnabled(false);
        lockSettings->setMinimumWidth(110);
        lg->addWidget(lockOn, 0, 0);
        lg->addWidget(lockSettings, 0, 1, Qt::AlignRight);
        lg->addWidget(wrapped(tr7("Enables you to highlight or drag without holding down the mouse button. To set, briefly press the mouse button. To release, click the mouse button again.")),
                      1, 0, 1, 2);
        lg->setColumnStretch(0, 1);
        v->addWidget(lock);
        v->addStretch(1);
        return w;
    }

    static QString unavailable()
    {
        return tr7("This setting is not available on this system.");
    }

    /** icon of main.cpl at the left of a group row */
    static QLabel *groupIcon(const QString &name)
    {
        auto *l = new QLabel;
        l->setPixmap(QIcon::fromTheme(name).pixmap(32, 32));
        l->setFixedSize(40, 34);
        l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        return l;
    }

    QWidget *pointersTab()
    {
        QVBoxLayout *v;
        QWidget *w = page(v);
        auto *scheme = new QGroupBox(tr7("Scheme"));
        auto *sg = new QHBoxLayout(scheme);
        auto *combo = new QComboBox;
        const auto themes = cursorThemes();
        for (const auto &t : themes) {
            combo->addItem(t.second, t.first);
            if (t.first == m_s.cursorTheme) {
                combo->setCurrentIndex(combo->count() - 1);
            }
        }
        sg->addWidget(combo, 1);
        auto *preview = new QLabel;
        preview->setFixedSize(56, 56);
        preview->setAlignment(Qt::AlignCenter);
        preview->setStyleSheet(QStringLiteral("QLabel { background: #ffffff; border: 1px solid #a0a0a0; }"));
        auto *top = new QHBoxLayout;
        top->addWidget(scheme, 1);
        top->addWidget(preview, 0, Qt::AlignBottom);
        v->addLayout(top);
        v->addWidget(new QLabel(tr7("Customize:")));
        auto *list = new QTreeWidget;
        list->setRootIsDecorated(false);
        list->setHeaderHidden(true);
        list->setColumnCount(2);
        list->setIconSize(QSize(32, 32));
        list->header()->setStretchLastSection(false);
        list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        list->header()->setSectionResizeMode(1, QHeaderView::Fixed);
        list->setColumnWidth(1, 44);
        v->addWidget(list, 1);
        auto *defaults = new QPushButton(tr7("Default"));
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        row->addWidget(defaults);
        v->addLayout(row);
        auto fill = [list, preview](const QString &theme) {
            list->clear();
            for (const Role &r : roles()) {
                auto *it = new QTreeWidgetItem(list, {r.title, QString()});
                it->setIcon(1, QIcon(cursorPixmap(theme, r)));
                it->setSizeHint(0, QSize(0, 36));
            }
            preview->setPixmap(cursorPixmap(theme, roles().first(), 48));
        };
        fill(m_s.cursorTheme);
        connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, fill] {
            m_s.cursorTheme = combo->currentData().toString();
            fill(m_s.cursorTheme);
            changed();
        });
        connect(defaults, &QPushButton::clicked, this, [combo] {
            const int i = combo->findData(QStringLiteral("breeze_cursors"));
            if (i >= 0) {
                combo->setCurrentIndex(i);
            }
        });
        return w;
    }

    QWidget *optionsTab()
    {
        QVBoxLayout *v;
        QWidget *w = page(v);
        auto *motion = new QGroupBox(tr7("Move"));
        auto *mg = new QGridLayout(motion);
        mg->addWidget(groupIcon(QStringLiteral("seven-mouse-motion")), 0, 0, 2, 1, Qt::AlignTop);
        mg->addWidget(new QLabel(tr7("Select a pointer speed:")), 0, 1, 1, 3);
        auto *slow = new QLabel(tr7("Slow", "repeat rate"));
        mg->addWidget(slow, 1, 1);
        auto *speed = ticks(10);
        speed->setFixedWidth(150);
        speed->setValue(qBound(0, qRound((m_s.speed + 1) * 5), 10));
        mg->addWidget(speed, 1, 2);
        mg->addWidget(new QLabel(tr7("Fast", "repeat rate")), 1, 3);
        auto *precision = new QCheckBox(tr7("Enhance pointer precision"));
        precision->setChecked(m_s.precision);
        mg->addWidget(precision, 2, 1, 1, 3);
        mg->setColumnStretch(3, 1);
        v->addWidget(motion);
        connect(speed, &QSlider::valueChanged, this, [this](int val) {
            m_s.speed = val / 5.0 - 1;
            changed();
        });
        connect(precision, &QCheckBox::toggled, this, [this](bool on) {
            m_s.precision = on;
            changed();
        });

        auto *snap = new QGroupBox(tr7("Snap To"));
        auto *sg = new QGridLayout(snap);
        sg->addWidget(groupIcon(QStringLiteral("seven-mouse-snapto")), 0, 0, Qt::AlignTop);
        auto *snapOn = new QCheckBox(tr7("Automatically move pointer to the default button in a dialog box"));
        snapOn->setEnabled(false);
        snapOn->setToolTip(unavailable());
        sg->addWidget(snapOn, 0, 1, Qt::AlignTop);
        sg->setColumnStretch(1, 1);
        v->addWidget(snap);

        auto *vis = new QGroupBox(tr7("Visibility"));
        auto *vg = new QGridLayout(vis);
        vg->setVerticalSpacing(8);
        vg->addWidget(groupIcon(QStringLiteral("seven-mouse-trails")), 0, 0, 2, 1, Qt::AlignTop);
        auto *trails = new QCheckBox(tr7("Display pointer trails"));
        trails->setEnabled(false);
        trails->setToolTip(unavailable());
        vg->addWidget(trails, 0, 1, 1, 3);
        auto *shortL = new QLabel(tr7("Short", "trails"));
        auto *trailLen = ticks(6);
        trailLen->setFixedWidth(120);
        trailLen->setValue(6);
        auto *longL = new QLabel(tr7("Long", "trails"));
        for (QWidget *x : {static_cast<QWidget *>(shortL), static_cast<QWidget *>(trailLen), static_cast<QWidget *>(longL)}) {
            x->setEnabled(false);
        }
        vg->addWidget(shortL, 1, 1);
        vg->addWidget(trailLen, 1, 2);
        vg->addWidget(longL, 1, 3);
        vg->addWidget(groupIcon(QStringLiteral("seven-mouse-hide")), 2, 0, Qt::AlignTop);
        auto *hide = new QCheckBox(tr7("Hide pointer while typing"));
        hide->setChecked(m_s.hideTyping);
        vg->addWidget(hide, 2, 1, 1, 3, Qt::AlignTop);
        vg->addWidget(groupIcon(QStringLiteral("seven-mouse-ctrl")), 3, 0, Qt::AlignTop);
        auto *ctrl = new QCheckBox(tr7("Show location of pointer when I\npress the CTRL key"));
        ctrl->setChecked(m_s.ctrlLocate);
        vg->addWidget(ctrl, 3, 1, 1, 3, Qt::AlignTop);
        vg->setColumnStretch(3, 1);
        v->addWidget(vis);
        connect(hide, &QCheckBox::toggled, this, [this](bool on) {
            m_s.hideTyping = on;
            changed();
        });
        connect(ctrl, &QCheckBox::toggled, this, [this](bool on) {
            m_s.ctrlLocate = on;
            changed();
        });
        v->addStretch(1);
        return w;
    }

    QWidget *wheelTab()
    {
        QVBoxLayout *v;
        QWidget *w = page(v);
        auto *vert = new QGroupBox(tr7("Vertical Scrolling"));
        auto *vg = new QGridLayout(vert);
        vg->setVerticalSpacing(8);
        vg->addWidget(groupIcon(QStringLiteral("seven-mouse-wheel-v")), 0, 0, 4, 1, Qt::AlignTop);
        vg->addWidget(wrapped(tr7("Roll the wheel one notch to scroll:")), 0, 1, 1, 2);
        auto *byLines = new QRadioButton(tr7("The following number of lines at a time:"));
        byLines->setChecked(true);
        vg->addWidget(byLines, 1, 1, 1, 2);
        auto *lines = new QSpinBox;
        lines->setRange(1, 100);
        lines->setValue(m_s.wheelLines);
        lines->setFixedWidth(70);
        auto *row = new QHBoxLayout;
        row->setContentsMargins(40, 0, 0, 0);
        row->addWidget(lines);
        row->addStretch(1);
        vg->addLayout(row, 2, 1, 1, 2);
        auto *byScreen = new QRadioButton(tr7("One screen at a time"));
        byScreen->setEnabled(false);
        byScreen->setToolTip(unavailable());
        vg->addWidget(byScreen, 3, 1, 1, 2);
        vg->setColumnStretch(2, 1);
        v->addWidget(vert);
        connect(lines, &QSpinBox::valueChanged, this, [this](int n) {
            m_s.wheelLines = n;
            changed();
        });

        auto *hor = new QGroupBox(tr7("Horizontal Scrolling"));
        auto *hg = new QGridLayout(hor);
        hg->setVerticalSpacing(8);
        hg->addWidget(groupIcon(QStringLiteral("seven-mouse-wheel-h")), 0, 0, 2, 1, Qt::AlignTop);
        hg->addWidget(wrapped(tr7("Tilt the wheel to scroll the following number of characters at a time:")), 0, 1);
        auto *chars = new QSpinBox;
        chars->setRange(1, 100);
        chars->setValue(3);
        chars->setFixedWidth(70);
        chars->setEnabled(false);
        chars->setToolTip(unavailable());
        auto *hrow = new QHBoxLayout;
        hrow->setContentsMargins(40, 0, 0, 0);
        hrow->addWidget(chars);
        hrow->addStretch(1);
        hg->addLayout(hrow, 1, 1);
        hg->setColumnStretch(1, 1);
        v->addWidget(hor);
        v->addStretch(1);
        return w;
    }

    QWidget *hardwareTab()
    {
        QVBoxLayout *v;
        QWidget *w = page(v);
        v->addWidget(new QLabel(tr7("Devices:")));
        auto *tree = new QTreeWidget;
        tree->setRootIsDecorated(false);
        tree->setHeaderLabels({tr7("Name"), tr7("Type")});
        tree->setColumnWidth(0, 220);
        tree->setIconSize(QSize(16, 16));
        for (const auto &d : pointerDevices()) {
            const bool pad = d.second.contains(QLatin1String("Touchpad"), Qt::CaseInsensitive);
            auto *it = new QTreeWidgetItem(tree, {d.second, tr7("Mice and other pointing devices")});
            Q_UNUSED(pad)
            it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-devmgr-mouse"))); // Windows 7 shows every pointing device as a mouse
        }
        v->addWidget(tree, 1);
        auto *props = new QGroupBox(tr7("Device Properties"));
        auto *pg = new QGridLayout(props);
        pg->setHorizontalSpacing(16);
        auto *maker = new QLabel;
        auto *where = new QLabel;
        pg->addWidget(new QLabel(tr7("Manufacturer:")), 0, 0);
        pg->addWidget(maker, 0, 1);
        pg->addWidget(new QLabel(tr7("Location:")), 1, 0);
        pg->addWidget(where, 1, 1);
        pg->addWidget(new QLabel(tr7("Device status:")), 2, 0);
        pg->addWidget(new QLabel(tr7("This device is working properly.")), 2, 1);
        pg->setColumnStretch(1, 1);
        auto *btn = new QPushButton(tr7("Properties"));
        pg->addWidget(btn, 3, 1, Qt::AlignRight);
        v->addWidget(props);
        const QString devices = readFile(QStringLiteral("/proc/bus/input/devices"));
        connect(tree, &QTreeWidget::currentItemChanged, this, [maker, where, devices](QTreeWidgetItem *it) {
            if (!it) {
                return;
            }
            for (const QString &block : devices.split(QStringLiteral("\n\n"))) {
                if (!block.contains(QStringLiteral("Name=\"%1\"").arg(it->text(0)))) {
                    continue;
                }
                static const QRegularExpression bus(QStringLiteral("Bus=(\\w+)"));
                static const QRegularExpression physRe(QStringLiteral("P: Phys=(.*)"));
                const QString b = bus.match(block).captured(1);
                const QString phys = physRe.match(block).captured(1).trimmed();
                where->setText(b == QLatin1String("0005") ? QStringLiteral("Bluetooth") : b == QLatin1String("0003") ? QStringLiteral("USB")
                                       : b == QLatin1String("0018")                                    ? tr7("I2C bus")
                                                                                                       : (phys.isEmpty() ? tr7("Unknown") : phys));
            }
            maker->setText(it->text(0).section(QLatin1Char(' '), 0, 0).section(QLatin1Char(':'), 0, 0));
        });
        if (tree->topLevelItemCount() > 0) {
            tree->setCurrentItem(tree->topLevelItem(0));
        }
        connect(btn, &QPushButton::clicked, this, [tree] {
            if (tree->currentItem()) {
                QProcess::startDetached(QDir::homePath() + QStringLiteral("/.local/bin/seven-devmgr"), {QStringLiteral("--properties"), tree->currentItem()->text(0)});
            }
        });
        return w;
    }

    Settings m_old;
    Settings m_s;
    QPushButton *m_apply = nullptr;
};
}
