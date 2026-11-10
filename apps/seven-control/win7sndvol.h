/*
 * Windows 7 "Volume Mixer" (SndVol.exe, dialogs 200 / 202 of sndvol.exe.mui): the device column
 * on the left, one column per program on the right, each with a peak meter, a vertical slider and
 * a mute button.
 *
 *   device / programs        -> PipeWire sinks and sink inputs through pactl
 *   peak meters              -> parec on the sink monitor / --monitor-stream of each program
 *   "System Sounds"          -> the Notification role volume of WirePlumber (route-settings metadata)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7sound.h"

#include <QDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QProcess>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QToolButton>

#include <cmath>

namespace Win7SndVol
{
using Win7Sound::sh;

inline QRect dlu(int x, int y, int w, int h)
{
    auto px = [](int v) { return int(std::lround(v * 7 / 4.0)); };
    auto py = [](int v) { return int(std::lround(v * 15 / 8.0)); };
    return QRect(px(x), py(y), px(w), py(h));
}

/** One program (all its streams on the shown device) */
struct App {
    QString key; // application name
    QString name;
    QString icon;
    QList<int> streams; // sink input indexes
    int volume = 100;
    bool mute = false;
    bool system = false;
};

/** icon of a program from its .desktop file, like the exe icon in Windows */
inline QString iconFor(const QString &appName, const QString &binary, const QString &iconName)
{
    if (!iconName.isEmpty() && QIcon::hasThemeIcon(iconName)) {
        return iconName;
    }
    static QHash<QString, QString> cache;
    const QString k = appName + QLatin1Char('|') + binary;
    if (cache.contains(k)) {
        return cache.value(k);
    }
    QString found;
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        for (const QFileInfo &fi : QDir(dir).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files)) {
            QFile f(fi.filePath());
            if (!f.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QString text = QString::fromUtf8(f.readAll());
            const QString base = fi.completeBaseName();
            const bool match = base.compare(appName, Qt::CaseInsensitive) == 0 || (!binary.isEmpty() && binary != QLatin1String("wine") && base.contains(binary, Qt::CaseInsensitive))
                || text.contains(QStringLiteral("\nName=") + appName + QLatin1Char('\n')) || text.contains(QStringLiteral("StartupWMClass=") + appName, Qt::CaseInsensitive);
            if (!match) {
                continue;
            }
            static const QRegularExpression icon(QStringLiteral("\\nIcon=([^\\n]+)"));
            const auto m = icon.match(text);
            if (m.hasMatch()) {
                found = m.captured(1).trimmed();
                break;
            }
        }
        if (!found.isEmpty()) {
            break;
        }
    }
    if (found.isEmpty()) {
        found = QStringLiteral("application-x-executable");
    }
    cache.insert(k, found);
    return found;
}

/* ---------------- System Sounds (Notification role) ---------------- */

inline QPair<int, bool> systemSounds()
{
    const QString out = sh(QStringLiteral("pw-metadata"), {QStringLiteral("-n"), QStringLiteral("route-settings"), QStringLiteral("0"),
                                                           QStringLiteral("restore.stream.Output/Audio.media.role:Notification")});
    static const QRegularExpression vol(QStringLiteral("\"volumes\":\\s*\\[\\s*([0-9.]+)"));
    const auto m = vol.match(out);
    const double linear = m.hasMatch() ? m.captured(1).toDouble() : 1.0;
    return {qRound(std::cbrt(linear) * 100), out.contains(QLatin1String("\"mute\": true")) || out.contains(QLatin1String("\"mute\":true"))};
}

inline void setSystemSounds(int percent, bool mute)
{
    const double linear = std::pow(percent / 100.0, 3);
    const QString json = QStringLiteral("{ \"mute\": %1, \"volumes\": [ %2, %2 ], \"channels\": [ \"FL\", \"FR\" ] }")
                             .arg(mute ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(linear, 0, 'f', 6);
    QProcess::startDetached(QStringLiteral("pw-metadata"), {QStringLiteral("-n"), QStringLiteral("route-settings"), QStringLiteral("0"),
                                                           QStringLiteral("restore.stream.Output/Audio.media.role:Notification"), json,
                                                           QStringLiteral("Spa:String:JSON")});
}

/* ---------------- one column ---------------- */

/** Peak meter of a sink (monitor) or one program stream */
class Peak : public QObject
{
public:
    explicit Peak(const QStringList &args, QObject *parent)
        : QObject(parent)
    {
        m_proc.start(QStringLiteral("parec"), args + QStringList{QStringLiteral("--raw"), QStringLiteral("--format=s16le"), QStringLiteral("--rate=8000"), QStringLiteral("--channels=1"),
                                                               QStringLiteral("--latency-msec=40"), QStringLiteral("--client-name=seven-sndvol"),
                                                               QStringLiteral("--stream-name=peak")});
        connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this] {
            const QByteArray d = m_proc.readAllStandardOutput();
            const auto *s = reinterpret_cast<const qint16 *>(d.constData());
            int peak = 0;
            for (int i = 0; i < d.size() / 2; ++i) {
                peak = qMax(peak, qAbs(int(s[i])));
            }
            level = qMax(level * 0.75, peak / 32768.0);
        });
    }
    ~Peak() override
    {
        m_proc.kill();
        m_proc.waitForFinished(300);
    }
    double level = 0;

private:
    QProcess m_proc;
};

/** Vertical slider with the green peak meter beside its channel, like the Windows mixer */
class MeterSlider : public QWidget
{
public:
    explicit MeterSlider(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        slider = new QSlider(Qt::Vertical, this);
        slider->setRange(0, 100);
        slider->setPageStep(10);
        slider->setTickPosition(QSlider::NoTicks);
    }
    QSlider *slider;
    double level = 0;
    bool muted = false;

protected:
    void resizeEvent(QResizeEvent *) override
    {
        slider->setGeometry(0, 0, width() - 8, height());
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect bar(width() - 6, 8, 5, height() - 16);
        p.fillRect(bar, QColor(0xd6, 0xd6, 0xd6));
        p.setPen(QColor(0xa0, 0xa0, 0xa0));
        p.drawRect(bar.adjusted(0, 0, -1, -1));
        if (muted || level <= 0.002) {
            return;
        }
        const int h = int((bar.height() - 2) * qBound(0.0, level, 1.0));
        QLinearGradient g(0, bar.bottom(), 0, bar.top());
        g.setColorAt(0, QColor(0x2c, 0xb0, 0x2c));
        g.setColorAt(0.75, QColor(0x8e, 0xe0, 0x3a));
        g.setColorAt(1, QColor(0xf0, 0xd0, 0x20));
        p.fillRect(QRect(bar.left() + 1, bar.bottom() - h, bar.width() - 2, h), g);
    }
};

/** Device or program column: icon, name, slider with meter, mute button */
class Column : public QWidget
{
public:
    std::function<void(int)> volumeChanged;
    std::function<void(bool)> muteToggled;

    Column(const QString &icon, const QString &name, bool device, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        // dialog 202: 63 x 131 dialog units; the device column of dialog 200 is a little taller
        setFixedSize(dlu(0, 0, device ? 58 : 63, 131).size());
        iconButton = new QToolButton(this);
        iconButton->setAutoRaise(true);
        iconButton->setIconSize(QSize(32, 32));
        iconButton->setIcon(QIcon::fromTheme(icon));
        iconButton->setGeometry(dlu(device ? 15 : 19, 0, 25, 20));
        if (device) {
            iconButton->setPopupMode(QToolButton::InstantPopup);
            iconButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
            iconButton->setGeometry(dlu(12, 0, 34, 20));
        }
        label = new QLabel(this);
        label->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        label->setWordWrap(true);
        label->setGeometry(dlu(device ? 0 : 3, 20, device ? 58 : 58, 16));
        meter = new MeterSlider(this);
        meter->setGeometry(dlu(device ? 18 : 21, 37, 17, 75).adjusted(0, 0, 8, 0));
        mute = new QToolButton(this);
        mute->setAutoRaise(true);
        mute->setCheckable(true);
        mute->setIconSize(QSize(24, 24));
        mute->setGeometry(dlu(device ? 17 : 20, 112, 24, 19));
        setName(name);
        setMuteIcon(false);
        connect(meter->slider, &QSlider::valueChanged, this, [this](int v) {
            meter->slider->setToolTip(QString::number(v));
            if (!m_updating && volumeChanged) {
                volumeChanged(v);
            }
        });
        connect(mute, &QToolButton::toggled, this, [this](bool on) {
            setMuteIcon(on);
            if (!m_updating && muteToggled) {
                muteToggled(on);
            }
        });
    }

    void setName(const QString &name)
    {
        m_name = name;
        label->setText(label->fontMetrics().elidedText(name, Qt::ElideRight, label->width() * 2 - 10));
        label->setToolTip(name);
        mute->setToolTip(tr7("Mute %1").arg(name));
    }
    void setState(int volume, bool muted)
    {
        m_updating = true;
        if (!meter->slider->isSliderDown()) {
            meter->slider->setValue(volume);
        }
        mute->setChecked(muted);
        setMuteIcon(muted);
        m_updating = false;
    }

    QToolButton *iconButton;
    QLabel *label;
    MeterSlider *meter;
    QToolButton *mute;

private:
    void setMuteIcon(bool muted)
    {
        mute->setIcon(QIcon::fromTheme(muted ? QStringLiteral("seven-sndvol-off") : QStringLiteral("seven-sndvol-on")));
        mute->setToolTip(muted ? tr7("Unmute %1").arg(m_name) : tr7("Mute %1").arg(m_name));
        meter->muted = muted;
    }
    QString m_name;
    bool m_updating = false;
};

/* ---------------- the window ---------------- */

class Mixer : public QDialog
{
public:
    Mixer()
    {
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-sndvol")));
        setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint);
        setAttribute(Qt::WA_DeleteOnClose);
        auto *h = new QHBoxLayout(this);
        const QRect devGroup = dlu(6, 6, 64, 153);
        h->setContentsMargins(devGroup.x(), devGroup.y(), devGroup.x(), devGroup.y());
        h->setSpacing(dlu(0, 0, 1, 0).width() + 2);
        m_devBox = new QGroupBox(tr7("Device"));
        m_devBox->setFixedWidth(devGroup.width());
        auto *dv = new QVBoxLayout(m_devBox);
        dv->setContentsMargins(3, dlu(0, 0, 0, 10).height() - 6, 3, 3);
        m_device = new Column(QStringLiteral("seven-snd-speakers"), QString(), true);
        dv->addWidget(m_device, 0, Qt::AlignHCenter);
        dv->addStretch(1);
        h->addWidget(m_devBox);
        m_appBox = new QGroupBox(tr7("Programs"));
        auto *av = new QVBoxLayout(m_appBox);
        av->setContentsMargins(3, dlu(0, 0, 0, 10).height() - 6, 3, 3);
        m_scroll = new QScrollArea;
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setWidgetResizable(true);
        m_apps = new QWidget;
        m_appsLayout = new QHBoxLayout(m_apps);
        m_appsLayout->setContentsMargins(0, 0, 0, 0);
        m_appsLayout->setSpacing(0);
        m_appsLayout->addStretch(1);
        m_scroll->setWidget(m_apps);
        av->addWidget(m_scroll);
        h->addWidget(m_appBox, 1);
        m_device->volumeChanged = [this](int v) {
            sh(QStringLiteral("pactl"), {QStringLiteral("set-sink-volume"), m_sink, QStringLiteral("%1%").arg(v)});
        };
        m_device->muteToggled = [this](bool on) {
            sh(QStringLiteral("pactl"), {QStringLiteral("set-sink-mute"), m_sink, on ? QStringLiteral("1") : QStringLiteral("0")});
        };
        m_menu = new QMenu(this);
        m_device->iconButton->setMenu(m_menu);
        connect(m_menu, &QMenu::aboutToShow, this, [this] {
            fillDeviceMenu();
        });
        m_sink = sh(QStringLiteral("pactl"), {QStringLiteral("get-default-sink")}).trimmed();
        reload();
        m_poll.setInterval(1000);
        connect(&m_poll, &QTimer::timeout, this, [this] {
            reload();
        });
        m_poll.start();
        m_paint.setInterval(60);
        connect(&m_paint, &QTimer::timeout, this, [this] {
            if (m_devicePeak) {
                m_device->meter->level = m_devicePeak->level;
                m_device->meter->update();
            }
            for (auto it = m_columns.cbegin(); it != m_columns.cend(); ++it) {
                double l = 0;
                for (Peak *pk : m_peaks.value(it.key())) {
                    l = qMax(l, pk->level);
                }
                it.value()->meter->level = l;
                it.value()->meter->update();
            }
        });
        m_paint.start();
    }

private:
    void fillDeviceMenu()
    {
        m_menu->clear();
        for (const Win7Sound::Endpoint &e : Win7Sound::endpoints(true, false, false)) {
            if (e.node.startsWith(QLatin1String("win7fx."))) {
                continue;
            }
            QAction *a = m_menu->addAction(QIcon::fromTheme(e.shownIcon()), e.shownName() + QStringLiteral(" (") + e.deviceName + QLatin1Char(')'));
            a->setCheckable(true);
            a->setChecked(e.node == m_sink);
            const QString node = e.node;
            connect(a, &QAction::triggered, this, [this, node] {
                m_sink = node;
                m_lastSink.clear();
                reload();
            });
        }
    }

    void reload()
    {
        // the device
        const QList<Win7Sound::Endpoint> outs = Win7Sound::endpoints(true, false, false);
        int sinkIndex = -1;
        for (const Win7Sound::Endpoint &e : outs) {
            if (e.node != m_sink) {
                continue;
            }
            const QString title = e.shownName() + QStringLiteral(" (") + e.deviceName + QLatin1Char(')');
            setWindowTitle(tr7("Volume Mixer - %1").arg(title));
            m_device->setName(e.shownName());
            m_device->iconButton->setIcon(QIcon::fromTheme(e.shownIcon()));
            m_device->setState(e.volume(), e.mute);
            sinkIndex = sinkIndexOf(e.node);
        }
        if (m_lastSink != m_sink) {
            m_lastSink = m_sink;
            delete m_devicePeak;
            m_devicePeak = new Peak({QStringLiteral("--device=") + m_sink + QStringLiteral(".monitor")}, this);
        }
        // programs on this device
        QMap<QString, App> apps;
        App sys;
        sys.key = QStringLiteral("#system");
        sys.name = tr7("System Sounds");
        sys.icon = QStringLiteral("seven-sounds");
        sys.system = true;
        const auto ss = systemSounds();
        sys.volume = ss.first;
        sys.mute = ss.second;
        apps.insert(QStringLiteral(" "), sys); // always first, like Windows
        const QString text = sh(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("sink-inputs")});
        for (const QString &block : text.split(QRegularExpression(QStringLiteral("\\n(?=Sink Input #)")))) {
            if (!block.startsWith(QLatin1String("Sink Input #"))) {
                continue;
            }
            const int index = block.section(QLatin1Char('#'), 1).section(QLatin1Char('\n'), 0, 0).toInt();
            const int sink = Win7Sound::field(block, QStringLiteral("Sink")).toInt();
            const auto props = Win7Sound::properties(block);
            if (sink != sinkIndex || props.value(QStringLiteral("media.role")) == QLatin1String("event")
                || props.value(QStringLiteral("application.name")).startsWith(QLatin1String("seven-"))) {
                continue;
            }
            QString name = props.value(QStringLiteral("application.name"));
            if (name.isEmpty()) {
                name = props.value(QStringLiteral("media.name"), tr7("Unknown application"));
            }
            App &a = apps[name.toLower()];
            if (a.key.isEmpty()) {
                a.key = name;
                a.name = name;
                a.icon = iconFor(name, props.value(QStringLiteral("application.process.binary")), props.value(QStringLiteral("application.icon_name")));
                a.volume = 0;
                a.mute = true;
            }
            a.streams << index;
            const QList<int> vols = Win7Sound::parseVolumes(Win7Sound::field(block, QStringLiteral("Volume")));
            for (int v : vols) {
                a.volume = qMax(a.volume, v);
            }
            a.mute = a.mute && Win7Sound::field(block, QStringLiteral("Mute")) == QLatin1String("yes");
        }
        // columns: add new, update, remove gone
        QSet<QString> seen;
        int pos = 0;
        for (const App &a : std::as_const(apps)) {
            seen.insert(a.key);
            Column *c = m_columns.value(a.key);
            if (!c) {
                c = new Column(a.icon, a.name, false);
                const QString key = a.key;
                const bool system = a.system;
                c->volumeChanged = [this, key, system](int v) {
                    if (system) {
                        setSystemSounds(v, m_columns.value(key)->mute->isChecked());
                        return;
                    }
                    for (int s : m_streams.value(key)) {
                        sh(QStringLiteral("pactl"), {QStringLiteral("set-sink-input-volume"), QString::number(s), QStringLiteral("%1%").arg(v)});
                    }
                };
                c->muteToggled = [this, key, system](bool on) {
                    if (system) {
                        setSystemSounds(m_columns.value(key)->meter->slider->value(), on);
                        return;
                    }
                    for (int s : m_streams.value(key)) {
                        sh(QStringLiteral("pactl"), {QStringLiteral("set-sink-input-mute"), QString::number(s), on ? QStringLiteral("1") : QStringLiteral("0")});
                    }
                };
                m_columns.insert(a.key, c);
                m_appsLayout->insertWidget(pos, c);
            }
            c->setState(a.volume, a.mute);
            // one peak meter per stream
            if (m_streams.value(a.key) != a.streams && !a.system) {
                qDeleteAll(m_peaks.value(a.key));
                QList<Peak *> peaks;
                for (int s : a.streams) {
                    peaks << new Peak({QStringLiteral("--monitor-stream=") + QString::number(s), QStringLiteral("--device=") + m_sink + QStringLiteral(".monitor")}, this);
                }
                m_peaks.insert(a.key, peaks);
            }
            m_streams.insert(a.key, a.streams);
            ++pos;
        }
        for (auto it = m_columns.begin(); it != m_columns.end();) {
            if (!seen.contains(it.key())) {
                qDeleteAll(m_peaks.take(it.key()));
                m_streams.remove(it.key());
                delete it.value();
                it = m_columns.erase(it);
            } else {
                ++it;
            }
        }
        // the window grows with the programs, like SndVol
        const int colW = dlu(0, 0, 63, 0).width();
        const int appsW = qMax(dlu(0, 0, 193, 0).width(), int(m_columns.size()) * colW + 12);
        const int maxW = 1100;
        m_appBox->setMinimumWidth(qMin(appsW, maxW));
        m_scroll->setHorizontalScrollBarPolicy(appsW > maxW ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
        setFixedHeight(dlu(0, 0, 0, 166).height() + (appsW > maxW ? 16 : 0));
        if (!m_sized) {
            m_sized = true;
            adjustSize();
        }
    }

    static int sinkIndexOf(const QString &node)
    {
        for (const QString &line : sh(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("short"), QStringLiteral("sinks")}).split(QLatin1Char('\n'))) {
            if (line.section(QLatin1Char('\t'), 1, 1) == node) {
                return line.section(QLatin1Char('\t'), 0, 0).toInt();
            }
        }
        return -1;
    }

    QGroupBox *m_devBox, *m_appBox;
    Column *m_device;
    QScrollArea *m_scroll;
    QWidget *m_apps;
    QHBoxLayout *m_appsLayout;
    QMenu *m_menu;
    QString m_sink, m_lastSink;
    Peak *m_devicePeak = nullptr;
    QMap<QString, Column *> m_columns;
    QHash<QString, QList<int>> m_streams;
    QHash<QString, QList<Peak *>> m_peaks;
    QTimer m_poll, m_paint;
    bool m_sized = false;
};
}
