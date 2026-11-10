/*
 * "Enhancements" tab of the Windows 7 playback device properties (SysFxUI.dll dialog 200) and its
 * settings dialogs (Bass Boost 202, Headphone Virtualization 204, Loudness Equalization 210).
 *
 *   effects            -> a PipeWire filter-chain per device, run by the user service seven-soundfx
 *                         (~/.config/seven-soundfx/filter-chain.conf, generated here)
 *   placement          -> WirePlumber smart filter in front of the device, so the device stays the
 *                         one you pick everywhere and only the sound sent to it is processed
 *   Bass Boost         -> builtin bq_lowshelf
 *   Headphone Virt.    -> bs2b crossfeed (bs2b-ladspa)
 *   Loudness Equal.    -> SC4 compressor (swh-plugins)
 *   choices            -> ~/.config/seven-soundfxrc
 * Room Correction (needs a calibration microphone run), Speaker Fill, Bass Management and Virtual
 * Surround (matrix encoding for receivers) are left out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace Win7SoundFx
{
inline const QString bs2bPlugin = QStringLiteral("/usr/lib/x86_64-linux-gnu/ladspa/bs2b.so");
inline const QString sc4Plugin = QStringLiteral("/usr/lib/ladspa/sc4_1882.so");

struct Fx {
    bool disabled = false; // "Disable all enhancements"
    bool bass = false;
    int bassFreq = 80; // Hz
    int bassLevel = 6; // dB
    bool phones = false;
    int phonesMode = 0; // index into phonesModes()
    bool loud = false;
    int loudRelease = 2; // 0 (short) .. 4 (long)

    bool any() const
    {
        return !disabled && (bass || phones || loud);
    }
};

struct PhonesMode {
    QString title;
    int freq;
    double feed;
};

/** Crossfeed strengths, the presets of bs2b */
inline const QList<PhonesMode> &phonesModes()
{
    static const QList<PhonesMode> m{
        {QStringLiteral("Легка"), 700, 4.5},
        {QStringLiteral("Середня"), 700, 6.0},
        {QStringLiteral("Сильна"), 650, 9.5},
    };
    return m;
}

inline QSettings &store()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/seven-soundfxrc"), QSettings::IniFormat);
    return s;
}

inline QString group(const QString &node)
{
    return QString(node).replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char(':'), QLatin1Char('_'));
}

inline Fx load(const QString &node)
{
    Fx f;
    QSettings &s = store();
    s.beginGroup(group(node));
    f.disabled = s.value(QStringLiteral("Disabled"), false).toBool();
    f.bass = s.value(QStringLiteral("BassBoost"), false).toBool();
    f.bassFreq = s.value(QStringLiteral("BassFrequency"), 80).toInt();
    f.bassLevel = s.value(QStringLiteral("BassLevel"), 6).toInt();
    f.phones = s.value(QStringLiteral("HeadphoneVirtualization"), false).toBool();
    f.phonesMode = qBound(0, s.value(QStringLiteral("HeadphoneMode"), 0).toInt(), int(phonesModes().size()) - 1);
    f.loud = s.value(QStringLiteral("LoudnessEqualization"), false).toBool();
    f.loudRelease = qBound(0, s.value(QStringLiteral("LoudnessRelease"), 2).toInt(), 4);
    s.endGroup();
    return f;
}

/* ---------------- PipeWire ---------------- */

/** WirePlumber match rules for the node that really plays the sound of `node` */
inline QString smartTarget(const QString &node)
{
    QProcess p;
    p.start(QStringLiteral("pw-cli"), {QStringLiteral("info"), node});
    p.waitForFinished(3000);
    const QString info = QString::fromUtf8(p.readAllStandardOutput());
    // the Bluetooth sink is itself a smart filter (a loopback) in front of the real, internal node
    static const QRegularExpression address(QStringLiteral("bluez_output\\.([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})"));
    const auto m = address.match(node);
    if (m.hasMatch() && info.contains(QLatin1String("filter.smart = \"true\""))) {
        // Bluetooth sinks are themselves a smart filter in front of an internal node: go next to it
        return QStringLiteral("{ \"api.bluez5.address\" = \"%1\" \"bluez5.sink-loopback-target\" = \"true\" }").arg(m.captured(1));
    }
    return QStringLiteral("{ \"node.name\" = \"%1\" }").arg(node);
}

inline QString quoted(const QString &s)
{
    return QLatin1Char('"') + QString(s).replace(QLatin1Char('"'), QStringLiteral("\\\"")) + QLatin1Char('"');
}

/** One filter-chain module for a device, empty when nothing is on */
inline QString moduleFor(const QString &node, const QString &title, const QString &target, const Fx &f)
{
    if (!f.any()) {
        return {};
    }
    struct Stage {
        QString l, r, outL, outR;
    };
    QStringList nodes;
    QList<Stage> stages;
    if (f.bass) {
        for (const char *n : {"bassL", "bassR"}) {
            nodes << QStringLiteral("{ type = builtin name = %1 label = bq_lowshelf control = { \"Freq\" = %2 \"Q\" = 0.7 \"Gain\" = %3 } }")
                         .arg(QLatin1String(n))
                         .arg(f.bassFreq)
                         .arg(f.bassLevel);
        }
        stages.append({QStringLiteral("bassL:In"), QStringLiteral("bassR:In"), QStringLiteral("bassL:Out"), QStringLiteral("bassR:Out")});
    }
    if (f.phones && QFileInfo::exists(bs2bPlugin)) {
        const PhonesMode &pm = phonesModes().at(f.phonesMode);
        nodes << QStringLiteral("{ type = ladspa name = phones plugin = %1 label = bs2b control = { \"Lowpass filter cut frequency (Hz)\" = %2 \"Feeding level (dB)\" = %3 } }")
                     .arg(quoted(bs2bPlugin))
                     .arg(pm.freq)
                     .arg(pm.feed, 0, 'f', 1);
        stages.append({QStringLiteral("phones:Input left"), QStringLiteral("phones:Input right"), QStringLiteral("phones:Output left"), QStringLiteral("phones:Output right")});
    }
    if (f.loud && QFileInfo::exists(sc4Plugin)) {
        static const int release[] = {80, 200, 400, 600, 800};
        nodes << QStringLiteral("{ type = ladspa name = loud plugin = %1 label = sc4 control = { \"RMS/peak\" = 1.0 \"Attack time (ms)\" = 10.0 "
                                "\"Release time (ms)\" = %2 \"Threshold level (dB)\" = -24.0 \"Ratio (1:n)\" = 4.0 \"Knee radius (dB)\" = 6.0 "
                                "\"Makeup gain (dB)\" = 9.0 } }")
                     .arg(quoted(sc4Plugin))
                     .arg(release[f.loudRelease]);
        stages.append({QStringLiteral("loud:Left input"), QStringLiteral("loud:Right input"), QStringLiteral("loud:Left output"), QStringLiteral("loud:Right output")});
    }
    if (stages.isEmpty()) {
        return {};
    }
    QStringList links;
    for (int i = 1; i < stages.size(); ++i) {
        links << QStringLiteral("{ output = %1 input = %2 }").arg(quoted(stages.at(i - 1).outL), quoted(stages.at(i).l));
        links << QStringLiteral("{ output = %1 input = %2 }").arg(quoted(stages.at(i - 1).outR), quoted(stages.at(i).r));
    }
    const QString name = QStringLiteral("win7fx.") + group(node);
    return QStringLiteral(
               "    { name = libpipewire-module-filter-chain\n"
               "      args = {\n"
               "        node.description = %1\n"
               "        media.name = %1\n"
               "        filter.graph = {\n"
               "          nodes = [\n            %2\n          ]\n"
               "          links = [ %3 ]\n"
               "          inputs = [ %4 %5 ]\n"
               "          outputs = [ %6 %7 ]\n"
               "        }\n"
               "        audio.channels = 2\n"
               "        audio.position = [ FL FR ]\n"
               "        capture.props = {\n"
               "          node.name = %8\n"
               "          media.class = Audio/Sink/Internal\n" // not a device of its own in the sound settings
               "          filter.smart = true\n"
               "          filter.smart.name = %8\n"
               "          filter.smart.target = %9\n"
               "        }\n"
               "        playback.props = {\n"
               "          node.name = %10\n"
               "          node.passive = true\n"
               "          stream.dont-remix = true\n"
               // never fall back to the default sink: that is this filter itself (the BT smart loopback),
               // and the loop stalls every new stream (Winamp, Wine games) until the filter restarts
               "          node.dont-fallback = true\n"
               "        }\n"
               "      }\n"
               "    }\n")
        .arg(quoted(title + QStringLiteral(" (покращення)")), nodes.join(QStringLiteral("\n            ")), links.join(QLatin1Char(' ')),
             quoted(stages.first().l), quoted(stages.first().r), quoted(stages.last().outL), quoted(stages.last().outR), quoted(name), target,
             quoted(name + QStringLiteral(".out")));
}

inline QString configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/seven-soundfx");
}

/**
 * Writes the filter-chain file for every device (with `override` in place of the saved choices for
 * one device, used by Preview) and (re)starts or stops the service.
 */
inline void rebuild(const QString &overrideNode = QString(), const Fx *override = nullptr, const QString &overrideTitle = QString(),
                    const QString &overrideTarget = QString())
{
    QString modules;
    bool seenOverride = false;
    QSettings &s = store();
    for (const QString &g : s.childGroups()) {
        s.beginGroup(g);
        const QString node = s.value(QStringLiteral("Node")).toString();
        const QString title = s.value(QStringLiteral("Title"), node).toString();
        QString target = s.value(QStringLiteral("Target")).toString();
        s.endGroup();
        // look again: an old or wrong saved target must not keep the effects off
        const QString fresh = smartTarget(node);
        if (fresh.contains(QLatin1String("bluez5")) || target.isEmpty()) {
            target = fresh;
        }
        if (node.isEmpty() || target.isEmpty()) {
            continue;
        }
        if (override && node == overrideNode) {
            seenOverride = true;
            modules += moduleFor(node, overrideTitle, overrideTarget, *override);
        } else {
            modules += moduleFor(node, title, target, load(node));
        }
    }
    if (override && !seenOverride) {
        modules += moduleFor(overrideNode, overrideTitle, overrideTarget, *override);
    }
    QDir().mkpath(configDir());
    const QString unitDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/systemd/user");
    QDir().mkpath(unitDir);
    QFile unit(unitDir + QStringLiteral("/seven-soundfx.service"));
    if (!unit.exists() && unit.open(QIODevice::WriteOnly | QIODevice::Text)) {
        unit.write("[Unit]\nDescription=Sound enhancements (Windows 7 Sound > Enhancements)\nAfter=pipewire.service wireplumber.service\n"
                   "Requires=pipewire.service\n\n[Service]\nExecStart=/usr/bin/pipewire -c %h/.config/seven-soundfx/filter-chain.conf\n"
                   "Restart=on-failure\n\n[Install]\nWantedBy=pipewire.service\n");
        unit.close();
        QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("daemon-reload")});
    }
    if (modules.isEmpty()) {
        QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("disable"), QStringLiteral("--now"), QStringLiteral("seven-soundfx.service")});
        return;
    }
    QFile f(configDir() + QStringLiteral("/filter-chain.conf"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(QStringLiteral("# Generated by seven-control (Sound > Properties > Enhancements). Do not edit.\n"
                               "context.properties = { log.level = 0 }\n"
                               "context.spa-libs = {\n    audio.convert.* = audioconvert/libspa-audioconvert\n    support.* = support/libspa-support\n}\n"
                               "context.modules = [\n"
                               "    { name = libpipewire-module-rt args = { } flags = [ ifexists nofail ] }\n"
                               "    { name = libpipewire-module-protocol-native }\n"
                               "    { name = libpipewire-module-client-node }\n"
                               "    { name = libpipewire-module-adapter }\n")
                    .toUtf8());
        f.write(modules.toUtf8());
        f.write("]\n");
        f.close();
    }
    QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("enable"), QStringLiteral("seven-soundfx.service")});
    QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("restart"), QStringLiteral("seven-soundfx.service")});
}

inline void save(const QString &node, const QString &title, const Fx &f)
{
    QSettings &s = store();
    s.beginGroup(group(node));
    s.setValue(QStringLiteral("Node"), node);
    s.setValue(QStringLiteral("Title"), title);
    s.setValue(QStringLiteral("Target"), smartTarget(node));
    s.setValue(QStringLiteral("Disabled"), f.disabled);
    s.setValue(QStringLiteral("BassBoost"), f.bass);
    s.setValue(QStringLiteral("BassFrequency"), f.bassFreq);
    s.setValue(QStringLiteral("BassLevel"), f.bassLevel);
    s.setValue(QStringLiteral("HeadphoneVirtualization"), f.phones);
    s.setValue(QStringLiteral("HeadphoneMode"), f.phonesMode);
    s.setValue(QStringLiteral("LoudnessEqualization"), f.loud);
    s.setValue(QStringLiteral("LoudnessRelease"), f.loudRelease);
    s.endGroup();
    s.sync();
    rebuild();
}

/* ---------------- settings dialogs ---------------- */

/** Dialog units of the SysFxUI templates -> pixels (Segoe UI 9) */
inline QRect dlu(int x, int y, int w, int h)
{
    auto px = [](int v) { return int(std::lround(v * 7 / 4.0)); };
    auto py = [](int v) { return int(std::lround(v * 15 / 8.0)); };
    return QRect(px(x), py(y), px(w), py(h));
}

template<typename W>
W *place(QWidget *parent, W *w, const QRect &r)
{
    w->setParent(parent);
    w->setGeometry(r);
    return w;
}

inline QDialog *settingsDialog(QWidget *parent, const QString &title, const QRect &size)
{
    auto *d = new QDialog(parent);
    d->setWindowTitle(title);
    d->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    d->setFixedSize(size.size());
    return d;
}

inline void okCancel(QDialog *d, int x1, int x2, int y)
{
    auto *ok = place(d, new QPushButton(QStringLiteral("OK")), dlu(x1, y, 50, 14));
    auto *cancel = place(d, new QPushButton(QStringLiteral("Скасувати")), dlu(x2, y, 50, 14));
    ok->setDefault(true);
    QObject::connect(ok, &QPushButton::clicked, d, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, d, &QDialog::reject);
}

/** "Bass Boost Settings": Frequency / Boost Level (dialog 202) */
inline bool bassDialog(QWidget *parent, Fx &f)
{
    QDialog *d = settingsDialog(parent, QStringLiteral("Параметри посилення низьких частот"), dlu(0, 0, 216, 80));
    place(d, new QLabel(QStringLiteral("Частота:")), dlu(8, 14, 54, 10));
    auto *freq = place(d, new QComboBox, dlu(70, 12, 140, 12));
    for (int hz : {50, 60, 70, 80, 90, 100, 120, 150, 200, 250, 300}) {
        freq->addItem(QStringLiteral("%1 Гц").arg(hz), hz);
    }
    freq->setCurrentIndex(qMax(0, freq->findData(f.bassFreq)));
    place(d, new QLabel(QStringLiteral("Рівень посилення:")), dlu(8, 35, 62, 8));
    auto *level = place(d, new QComboBox, dlu(70, 32, 140, 12));
    for (int db = 3; db <= 12; db += 3) {
        level->addItem(QStringLiteral("%1 дБ").arg(db), db);
    }
    level->setCurrentIndex(qMax(0, level->findData(f.bassLevel)));
    okCancel(d, 100, 158, 60);
    const bool ok = d->exec() == QDialog::Accepted;
    if (ok) {
        f.bassFreq = freq->currentData().toInt();
        f.bassLevel = level->currentData().toInt();
    }
    delete d;
    return ok;
}

/** "Headphone Virtualization Settings" (dialog 204) */
inline bool phonesDialog(QWidget *parent, Fx &f)
{
    QDialog *d = settingsDialog(parent, QStringLiteral("Параметри віртуалізації навушників"), dlu(0, 0, 260, 92));
    auto *head = place(d, new QLabel(QStringLiteral("Налаштування віртуалізації навушників")), dlu(8, 8, 249, 18));
    QFont hf = head->font();
    hf.setPixelSize(15);
    head->setFont(hf);
    head->setStyleSheet(QStringLiteral("color: #1e3287;"));
    auto *l = place(d, new QLabel(QStringLiteral("Віртуалізація навушників:")), dlu(8, 34, 112, 8));
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *mode = place(d, new QComboBox, dlu(124, 32, 111, 12));
    for (const PhonesMode &m : phonesModes()) {
        mode->addItem(m.title);
    }
    mode->setCurrentIndex(f.phonesMode);
    okCancel(d, 134, 188, 70);
    const bool ok = d->exec() == QDialog::Accepted;
    if (ok) {
        f.phonesMode = mode->currentIndex();
    }
    delete d;
    return ok;
}

/** "Loudness Equalization Settings": Release Time, Short - Long (dialog 210) */
inline bool loudDialog(QWidget *parent, Fx &f)
{
    QDialog *d = settingsDialog(parent, QStringLiteral("Параметри вирівнювання гучності"), dlu(0, 0, 216, 106));
    auto *head = place(d, new QLabel(QStringLiteral("Налаштування вирівнювання гучності")), dlu(8, 8, 208, 18));
    QFont hf = head->font();
    hf.setPixelSize(15);
    head->setFont(hf);
    head->setStyleSheet(QStringLiteral("color: #1e3287;"));
    auto *t = place(d, new QLabel(QStringLiteral("Час відновлення")), dlu(52, 30, 112, 8));
    t->setAlignment(Qt::AlignCenter);
    auto *slider = place(d, new QSlider(Qt::Horizontal), dlu(62, 40, 92, 24));
    slider->setRange(0, 4);
    slider->setPageStep(1);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(1);
    slider->setValue(f.loudRelease);
    auto *s = place(d, new QLabel(QStringLiteral("Короткий")), dlu(46, 68, 50, 10));
    s->setAlignment(Qt::AlignCenter);
    auto *lg = place(d, new QLabel(QStringLiteral("Довгий")), dlu(120, 68, 50, 10));
    lg->setAlignment(Qt::AlignCenter);
    okCancel(d, 100, 158, 84);
    const bool ok = d->exec() == QDialog::Accepted;
    if (ok) {
        f.loudRelease = slider->value();
    }
    delete d;
    return ok;
}

/* ---------------- the tab ---------------- */

class EnhancementsTab : public QWidget
{
public:
    std::function<void()> changed;

    EnhancementsTab(const QString &node, const QString &title, bool headphones)
        : m_node(node)
        , m_title(title)
        , m_saved(load(node))
        , m_fx(m_saved)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 12, 12, 12);
        v->setSpacing(8);
        auto *intro = new QLabel(QStringLiteral("Виберіть покращення, які потрібно застосувати до поточної конфігурації динаміків. "
                                                "Зміни можуть набути чинності лише під час наступного відтворення."));
        intro->setWordWrap(true);
        v->addWidget(intro);
        m_disable = new QCheckBox(QStringLiteral("Вимкнути всі покращення"));
        m_disable->setChecked(m_fx.disabled);
        v->addWidget(m_disable);
        m_list = new QListWidget;
        m_list->setFixedHeight(dlu(0, 0, 0, 48).height());
        v->addWidget(m_list);
        struct Row {
            QString key, title, description;
            bool available;
        };
        QList<Row> rows{{QStringLiteral("bass"), QStringLiteral("Посилення низьких частот"), QStringLiteral("Підсилює найнижчі частоти, які може відтворити пристрій."), true}};
        // Windows offers Headphone Virtualization for headphones; crossfeed only makes sense there
        if (headphones) {
            rows.append({QStringLiteral("phones"), QStringLiteral("Віртуалізація навушників"),
                         QStringLiteral("Віртуалізація навушників за допомогою спеціальної обробки створює ілюзію домашнього кінотеатру під час використання стереонавушників."),
                         QFileInfo::exists(bs2bPlugin)});
        }
        rows.append({QStringLiteral("loud"), QStringLiteral("Вирівнювання гучності"),
                     QStringLiteral("Вирівнювання гучності враховує особливості людського слуху, щоб зменшити помітну різницю в гучності."), QFileInfo::exists(sc4Plugin)});
        for (const Row &r : rows) {
            auto *it = new QListWidgetItem(r.title, m_list);
            it->setData(Qt::UserRole, r.key);
            it->setData(Qt::UserRole + 1, r.description);
            it->setData(Qt::UserRole + 2, r.available);
            it->setFlags(r.available ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable) : (Qt::ItemIsSelectable | Qt::ItemIsEnabled));
            it->setCheckState(on(r.key) ? Qt::Checked : Qt::Unchecked);
            if (!r.available) {
                it->setForeground(QColor(0x80, 0x80, 0x80));
            }
        }
        auto *props = new QGroupBox(QStringLiteral("Властивості покращення"));
        auto *g = new QGridLayout(props);
        g->setHorizontalSpacing(10);
        auto label = [](const QString &t) {
            auto *l = new QLabel(t);
            l->setAlignment(Qt::AlignRight | Qt::AlignTop);
            return l;
        };
        g->addWidget(label(QStringLiteral("Опис:")), 0, 0);
        m_desc = new QLabel;
        m_desc->setWordWrap(true);
        m_desc->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_desc->setMinimumHeight(dlu(0, 0, 0, 34).height());
        g->addWidget(m_desc, 0, 1, 1, 2);
        g->addWidget(label(QStringLiteral("Постачальник:")), 1, 0);
        m_provider = new QLabel;
        g->addWidget(m_provider, 1, 1, 1, 2);
        g->addWidget(label(QStringLiteral("Стан:")), 2, 0);
        m_status = new QLabel;
        g->addWidget(m_status, 2, 1);
        m_settings = new QPushButton(QStringLiteral("Параметри..."));
        g->addWidget(m_settings, 2, 2, Qt::AlignRight);
        g->setColumnStretch(1, 1);
        v->addWidget(props);
        v->addStretch(1);
        auto *bottom = new QHBoxLayout;
        auto *restore = new QPushButton(QStringLiteral("Відновити стандартні"));
        bottom->addWidget(restore);
        bottom->addStretch(1);
        // split button: the left part plays, the arrow picks the sample
        m_preview = new QPushButton(QIcon::fromTheme(QStringLiteral("seven-snd-play")), QStringLiteral("Попередній перегляд"));
        auto *arrow = new QPushButton(QStringLiteral("▾"));
        arrow->setFixedWidth(20);
        auto *menu = new QMenu(arrow);
        const QString dir = QStringLiteral("/usr/share/sounds/Windows 7/og/");
        for (const QString &f : {QStringLiteral("Windows Ding"), QStringLiteral("Windows Logon Sound"), QStringLiteral("Windows Notify"), QStringLiteral("tada")}) {
            if (QFileInfo::exists(dir + f + QStringLiteral(".wav"))) {
                menu->addAction(f, this, [this, dir, f] {
                    m_sample = dir + f + QStringLiteral(".wav");
                    preview();
                });
            }
        }
        menu->addSeparator();
        menu->addAction(QStringLiteral("Огляд..."), this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Пошук файлу мультимедіа"), QDir::homePath() + QStringLiteral("/Music"),
                                                           QStringLiteral("Аудіофайли (*.wav *.mp3 *.ogg *.oga *.flac *.m4a)"));
            if (!f.isEmpty()) {
                m_sample = f;
                preview();
            }
        });
        connect(arrow, &QPushButton::clicked, this, [arrow, menu] {
            menu->exec(arrow->mapToGlobal(QPoint(0, arrow->height())));
        });
        m_sample = dir + QStringLiteral("Windows Ding.wav");
        auto *split = new QHBoxLayout;
        split->setSpacing(0);
        split->addWidget(m_preview);
        split->addWidget(arrow);
        bottom->addLayout(split);
        v->addLayout(bottom);

        connect(m_list, &QListWidget::currentItemChanged, this, [this] {
            showProperties();
        });
        connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
            setOn(it->data(Qt::UserRole).toString(), it->checkState() == Qt::Checked);
            showProperties();
            touch();
        });
        connect(m_disable, &QCheckBox::toggled, this, [this](bool d) {
            m_fx.disabled = d;
            m_list->setEnabled(!d);
            showProperties();
            touch();
        });
        connect(m_settings, &QPushButton::clicked, this, [this] {
            const QString k = currentKey();
            Fx f = m_fx;
            const bool ok = k == QLatin1String("bass") ? bassDialog(this, f) : k == QLatin1String("phones") ? phonesDialog(this, f) : loudDialog(this, f);
            if (ok) {
                m_fx = f;
                touch();
            }
        });
        connect(restore, &QPushButton::clicked, this, [this] {
            m_fx = Fx();
            m_disable->setChecked(false);
            m_list->blockSignals(true);
            for (int i = 0; i < m_list->count(); ++i) {
                m_list->item(i)->setCheckState(Qt::Unchecked);
            }
            m_list->blockSignals(false);
            showProperties();
            touch();
        });
        connect(m_preview, &QPushButton::clicked, this, [this] {
            preview();
        });
        m_list->setEnabled(!m_fx.disabled);
        m_list->setCurrentRow(0);
        showProperties();
    }

    ~EnhancementsTab() override
    {
        if (m_previewed) {
            rebuild(); // Cancel after a preview: back to the saved choices
        }
    }

    void apply()
    {
        if (!m_dirty) {
            return;
        }
        save(m_node, m_title, m_fx);
        m_saved = m_fx;
        m_dirty = false;
        m_previewed = false;
    }

private:
    bool on(const QString &k) const
    {
        return k == QLatin1String("bass") ? m_fx.bass : k == QLatin1String("phones") ? m_fx.phones : m_fx.loud;
    }
    void setOn(const QString &k, bool v)
    {
        (k == QLatin1String("bass") ? m_fx.bass : k == QLatin1String("phones") ? m_fx.phones : m_fx.loud) = v;
    }
    QString currentKey() const
    {
        return m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
    }

    void showProperties()
    {
        QListWidgetItem *it = m_list->currentItem();
        if (!it) {
            m_desc->clear();
            m_provider->clear();
            m_status->clear();
            m_settings->setEnabled(false);
            return;
        }
        const bool available = it->data(Qt::UserRole + 2).toBool();
        m_desc->setText(it->data(Qt::UserRole + 1).toString());
        m_provider->setText(QStringLiteral("PipeWire"));
        if (!available) {
            m_status->setText(QStringLiteral("Не встановлено (пакет %1)").arg(currentKey() == QLatin1String("phones") ? QStringLiteral("bs2b-ladspa") : QStringLiteral("swh-plugins")));
        } else {
            m_status->setText(on(currentKey()) && !m_fx.disabled ? QStringLiteral("Увімкнуто") : QStringLiteral("Вимкнуто"));
        }
        m_settings->setEnabled(available && !m_fx.disabled);
    }

    void touch()
    {
        m_dirty = true;
        if (changed) {
            changed();
        }
    }

    /** Hear the current choices before applying them, like Windows */
    void preview()
    {
        if (m_dirty) {
            rebuild(m_node, &m_fx, m_title, smartTarget(m_node));
            m_previewed = true;
        }
        QTimer::singleShot(m_dirty ? 700 : 0, this, [this] {
            QProcess::startDetached(QStringLiteral("pw-play"), {QStringLiteral("--target"), m_node, m_sample});
        });
    }

    QString m_node;
    QString m_title;
    Fx m_saved;
    Fx m_fx;
    bool m_dirty = false;
    bool m_previewed = false;
    QString m_sample;
    QCheckBox *m_disable;
    QListWidget *m_list;
    QLabel *m_desc;
    QLabel *m_provider;
    QLabel *m_status;
    QPushButton *m_settings;
    QPushButton *m_preview;
};
}
