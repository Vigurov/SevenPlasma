/*
 * "Sound" of Windows 7: Playback / Recording / Sounds tabs, device properties (General, Listen,
 * Levels, Advanced), Balance, the "Speaker Setup" wizard, "Speech Recognition" page and
 * "Text to Speech" properties.
 *
 *   devices, volume, default device  -> PipeWire through pactl
 *   enable / disable a device        -> card profile (like "off" in Plasma)
 *   default format                   -> a WirePlumber rule for that device
 *   listen to this device            -> module-loopback
 *   sound scheme / program events    -> the freedesktop sound theme (kdeglobals) and KNotification events
 *   text to speech                   -> speech-dispatcher
 * The "Communications" tab (lower other sounds during calls) has nothing to act on in PipeWire, so it is left out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7chrome.h"
#include "win7soundfx.h"

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QCollator>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QInputDialog>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QElapsedTimer>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtEndian>

#include <cmath>

namespace Win7Sound
{
inline QString sh(const QString &cmd, const QStringList &args, int timeout = 4000)
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C")); // stable keys; values stay UTF-8
    p.setProcessEnvironment(env);
    p.start(cmd, args);
    p.waitForFinished(timeout);
    return QString::fromUtf8(p.readAllStandardOutput());
}

inline void pactl(const QStringList &args)
{
    QProcess::execute(QStringLiteral("pactl"), args);
}

inline QSettings &store()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/seven-soundrc"), QSettings::IniFormat);
    return s;
}

inline QString key(const QString &node)
{
    return QString(node).replace(QLatin1Char('/'), QLatin1Char('_'));
}

/* ---------------- devices ---------------- */

struct Endpoint {
    bool output = true;
    QString node;         // sink / source name, empty for a disabled card
    QString card;         // device.name
    QString typeName;     // "Динаміки", "Мікрофон", ...
    QString deviceName;   // "Ryzen HD Audio Controller"
    QString vendor;
    QString icon;
    QString port;         // active port description
    QString state;        // RUNNING / SUSPENDED / IDLE
    QString sampleSpec;   // "s32le 2ch 48000Hz"
    QStringList channels; // front-left, ...
    QList<int> volumes;   // percent per channel
    bool mute = false;
    bool isDefault = false;
    bool disabled = false;      // card profile without this direction
    bool unplugged = false;     // jack with nothing in it
    QString enableProfile;      // profile that brings the device back
    QString disableProfile;     // profile without this direction
    QStringList surroundProfiles; // for the speaker wizard

    QString id() const
    {
        return node.isEmpty() ? card + (output ? QStringLiteral(":out") : QStringLiteral(":in")) : node;
    }
    int volume() const
    {
        int m = 0;
        for (int v : volumes) {
            m = qMax(m, v);
        }
        return m;
    }
    QString shownName() const
    {
        const QString custom = store().value(QStringLiteral("Names/") + key(id())).toString();
        return custom.isEmpty() ? typeName : custom;
    }
    QString shownIcon() const
    {
        const QString custom = store().value(QStringLiteral("Icons/") + key(id())).toString();
        return custom.isEmpty() ? icon : custom;
    }
    QString status() const
    {
        if (disabled) {
            return tr7("Off", "firewall state");
        }
        if (unplugged) {
            return tr7("Not connected");
        }
        if (isDefault) {
            return tr7("Default Device");
        }
        return tr7("Ready");
    }
};

}
Q_DECLARE_METATYPE(Win7Sound::Endpoint)
namespace Win7Sound
{

struct Card {
    QString name;
    QString description;
    QString vendor;
    QString formFactor;
    QString bus;
    QString active;
    struct Profile {
        QString name;
        QString description;
        int sinks = 0, sources = 0, priority = 0;
        bool available = true;
    };
    QList<Profile> profiles;
};

/** "key = "value"" property lines of a pactl block */
inline QHash<QString, QString> properties(const QString &block)
{
    QHash<QString, QString> props;
    static const QRegularExpression re(QStringLiteral("^\\s+([\\w.\\-]+) = \"(.*)\"$"), QRegularExpression::MultilineOption);
    for (auto it = re.globalMatch(block); it.hasNext();) {
        const auto m = it.next();
        props.insert(m.captured(1), m.captured(2));
    }
    return props;
}

inline QString field(const QString &block, const QString &name)
{
    const QRegularExpression re(QStringLiteral("^\\t%1: (.*)$").arg(QRegularExpression::escape(name)), QRegularExpression::MultilineOption);
    const auto m = re.match(block);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

inline QList<Card> cards()
{
    QList<Card> list;
    for (const QString &block : sh(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("cards")}).split(QStringLiteral("\nCard #"))) {
        Card c;
        c.name = field(block, QStringLiteral("Name"));
        if (c.name.isEmpty()) {
            continue;
        }
        const auto props = properties(block);
        c.description = props.value(QStringLiteral("device.description"), props.value(QStringLiteral("device.alias"), c.name));
        c.vendor = props.value(QStringLiteral("device.vendor.name"));
        c.formFactor = props.value(QStringLiteral("device.form_factor"));
        c.bus = props.value(QStringLiteral("device.bus"));
        c.active = field(block, QStringLiteral("Active Profile"));
        const int ps = block.indexOf(QStringLiteral("\tProfiles:\n"));
        const int pe = block.indexOf(QStringLiteral("\tActive Profile:"));
        if (ps >= 0 && pe > ps) {
            static const QRegularExpression pre(QStringLiteral("^\\t\\t([^:\\n]+(?::[^:\\s]+)?): (.*) \\(sinks: (\\d+), sources: (\\d+), priority: (\\d+), available: (\\w+)\\)$"),
                                                QRegularExpression::MultilineOption);
            for (auto it = pre.globalMatch(block.mid(ps, pe - ps)); it.hasNext();) {
                const auto m = it.next();
                c.profiles.append({m.captured(1), m.captured(2), m.captured(3).toInt(), m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6) == QLatin1String("yes")});
            }
        }
        list.append(c);
    }
    return list;
}

inline QString typeFor(bool output, const QString &port, const QString &formFactor, const QString &node)
{
    const QString p = port.toLower();
    if (node.endsWith(QLatin1String(".monitor"))) {
        return tr7("Stereo Mix");
    }
    if (output) {
        if (p.contains(QLatin1String("headphone")) || formFactor == QLatin1String("headphone")) {
            return tr7("Headphones");
        }
        if (p.contains(QLatin1String("headset")) || formFactor == QLatin1String("headset")) {
            return tr7("Headphones");
        }
        if (p.contains(QLatin1String("hdmi")) || p.contains(QLatin1String("displayport"))) {
            return tr7("Digital Output (HDMI)");
        }
        if (p.contains(QLatin1String("iec958")) || p.contains(QLatin1String("spdif"))) {
            return tr7("Digital Output");
        }
        if (p.contains(QLatin1String("lineout")) || p.contains(QLatin1String("line-out"))) {
            return tr7("Line Out");
        }
        return tr7("Speakers");
    }
    if (p.contains(QLatin1String("linein")) || p.contains(QLatin1String("line-in"))) {
        return tr7("Line In");
    }
    if (p.contains(QLatin1String("headset")) || formFactor == QLatin1String("headset")) {
        return tr7("Headset Microphone");
    }
    return tr7("Microphone");
}

inline QString iconFor(bool output, const QString &port, const QString &formFactor, const QString &node)
{
    const QString p = port.toLower();
    if (node.endsWith(QLatin1String(".monitor"))) {
        return QStringLiteral("seven-snd-card");
    }
    if (formFactor == QLatin1String("headset") || p.contains(QLatin1String("headset"))) {
        return QStringLiteral("seven-snd-headset");
    }
    if (formFactor == QLatin1String("webcam")) {
        return QStringLiteral("seven-snd-webcam");
    }
    if (output) {
        if (p.contains(QLatin1String("headphone")) || formFactor == QLatin1String("headphone")) {
            return QStringLiteral("seven-snd-headphones");
        }
        if (p.contains(QLatin1String("hdmi")) || p.contains(QLatin1String("displayport"))) {
            return QStringLiteral("seven-snd-hdmi");
        }
        if (p.contains(QLatin1String("iec958")) || p.contains(QLatin1String("spdif"))) {
            return QStringLiteral("seven-snd-digital");
        }
        return QStringLiteral("seven-snd-speakers");
    }
    if (p.contains(QLatin1String("linein")) || p.contains(QLatin1String("line-in"))) {
        return QStringLiteral("seven-snd-linein");
    }
    return QStringLiteral("seven-snd-mic");
}

inline QList<int> parseVolumes(const QString &line)
{
    QList<int> v;
    static const QRegularExpression re(QStringLiteral("(\\d+)%"));
    for (auto it = re.globalMatch(line); it.hasNext();) {
        v << it.next().captured(1).toInt();
    }
    return v;
}

inline QList<Endpoint> endpoints(bool output, bool showDisabled = true, bool showUnplugged = true)
{
    QList<Endpoint> list;
    const QList<Card> allCards = cards();
    const QString def = sh(QStringLiteral("pactl"), {output ? QStringLiteral("get-default-sink") : QStringLiteral("get-default-source")}).trimmed();
    const QString what = output ? QStringLiteral("sinks") : QStringLiteral("sources");
    QSet<QString> cardsWithNode;
    for (const QString &block : sh(QStringLiteral("pactl"), {QStringLiteral("list"), what}).split(QRegularExpression(QStringLiteral("\\n(?=(Sink|Source) #)")))) {
        Endpoint e;
        e.output = output;
        e.node = field(block, QStringLiteral("Name"));
        if (e.node.isEmpty()) {
            continue;
        }
        const auto props = properties(block);
        if (!output && e.node.endsWith(QLatin1String(".monitor")) && !props.value(QStringLiteral("device.class")).isEmpty()) {
            // monitors of sinks: Windows lists one "Stereo Mix" per card
        }
        e.card = props.value(QStringLiteral("device.name"));
        e.deviceName = props.value(QStringLiteral("device.description"), props.value(QStringLiteral("device.alias"), field(block, QStringLiteral("Description"))));
        e.vendor = props.value(QStringLiteral("device.vendor.name"));
        e.state = field(block, QStringLiteral("State"));
        e.sampleSpec = field(block, QStringLiteral("Sample Specification"));
        e.channels = field(block, QStringLiteral("Channel Map")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        e.volumes = parseVolumes(field(block, QStringLiteral("Volume")));
        e.mute = field(block, QStringLiteral("Mute")) == QLatin1String("yes");
        e.isDefault = e.node == def;
        const QString activePort = field(block, QStringLiteral("Active Port"));
        const QString formFactor = props.value(QStringLiteral("device.form_factor"));
        // port descriptions
        QHash<QString, QPair<QString, QString>> ports; // name -> (description, availability)
        const int ps = block.indexOf(QStringLiteral("\tPorts:\n"));
        if (ps >= 0) {
            static const QRegularExpression pre(QStringLiteral("^\\t\\t([^:\\n]+): (.*?) \\(type: [^,]*, priority: \\d+(?:, availability group: [^,]*)?, (.*)\\)$"), QRegularExpression::MultilineOption);
            for (auto it = pre.globalMatch(block.mid(ps)); it.hasNext();) {
                const auto m = it.next();
                ports.insert(m.captured(1), {m.captured(2), m.captured(3)});
            }
        }
        e.port = ports.value(activePort).first;
        e.typeName = typeFor(output, activePort, formFactor, e.node);
        e.icon = iconFor(output, activePort, formFactor, e.node);
        if (e.node.endsWith(QLatin1String(".monitor"))) {
            e.deviceName = e.deviceName.isEmpty() ? props.value(QStringLiteral("device.description")) : e.deviceName;
            if (!showDisabled) { // Windows hides "Stereo Mix" unless disabled devices are shown
                continue;
            }
        }
        for (const Card &c : allCards) {
            if (c.name != e.card) {
                continue;
            }
            cardsWithNode.insert(c.name + (output ? QStringLiteral(":out") : QStringLiteral(":in")));
            if (e.vendor.isEmpty()) {
                e.vendor = c.vendor;
            }
            // the profile that keeps the other direction only, for "Disable"
            int best = -1;
            for (const Card::Profile &p : c.profiles) {
                if (p.name == QLatin1String("pro-audio")) {
                    continue;
                }
                const bool other = output ? (p.sinks == 0 && p.sources > 0) : (p.sources == 0 && p.sinks > 0);
                const bool keepsOther = output ? c.profiles.size() && c.active.contains(QLatin1String("input")) : c.active.contains(QLatin1String("output"));
                if ((keepsOther ? other : p.name == QLatin1String("off")) && p.priority > best) {
                    best = p.priority;
                    e.disableProfile = p.name;
                }
            }
            if (e.disableProfile.isEmpty()) {
                e.disableProfile = QStringLiteral("off");
            }
            for (const Card::Profile &p : c.profiles) {
                if (output && p.sinks > 0 && p.available && p.name != QLatin1String("pro-audio")) {
                    e.surroundProfiles << p.name;
                }
            }
        }
        list.append(e);
        // jacks with nothing plugged in, shown greyed like Windows' "Not plugged in"
        if (showUnplugged && !e.node.endsWith(QLatin1String(".monitor"))) {
            for (auto it = ports.cbegin(); it != ports.cend(); ++it) {
                if (it.key() != activePort && it.value().second.contains(QLatin1String("not available"))) {
                    Endpoint u = e;
                    u.node = e.node + QLatin1Char('#') + it.key();
                    u.unplugged = true;
                    u.isDefault = false;
                    u.typeName = typeFor(output, it.key(), formFactor, e.node);
                    u.icon = iconFor(output, it.key(), formFactor, e.node);
                    u.port = it.value().first;
                    list.append(u);
                }
            }
        }
    }
    // cards switched off for this direction ("Disabled") or with nothing connected (HDMI without a screen)
    for (const Card &c : allCards) {
        if (cardsWithNode.contains(c.name + (output ? QStringLiteral(":out") : QStringLiteral(":in")))) {
            continue;
        }
        Card::Profile best;
        best.priority = -1;
        bool anyAvailable = false;
        for (const Card::Profile &p : c.profiles) {
            const bool has = output ? p.sinks > 0 : p.sources > 0;
            if (!has || p.name == QLatin1String("pro-audio")) {
                continue;
            }
            const int prio = p.priority + (p.available ? 100000 : 0);
            anyAvailable = anyAvailable || p.available;
            if (prio > best.priority) {
                best = p;
                best.priority = prio;
            }
        }
        if (best.name.isEmpty()) {
            continue;
        }
        Endpoint e;
        e.output = output;
        e.card = c.name;
        e.deviceName = c.description;
        e.vendor = c.vendor;
        e.enableProfile = best.name;
        const QString port = best.name;
        e.typeName = typeFor(output, port, c.formFactor, QString());
        e.icon = iconFor(output, port, c.formFactor, QString());
        if (anyAvailable) {
            e.disabled = true;
            if (!showDisabled) {
                continue;
            }
        } else {
            e.unplugged = true;
            if (!showUnplugged) {
                continue;
            }
        }
        list.append(e);
    }
    std::stable_sort(list.begin(), list.end(), [](const Endpoint &a, const Endpoint &b) {
        auto rank = [](const Endpoint &e) {
            return e.disabled ? 3 : e.unplugged ? 2 : e.node.endsWith(QLatin1String(".monitor")) ? 1 : 0;
        };
        return rank(a) < rank(b);
    });
    return list;
}

inline void setVolume(const Endpoint &e, const QList<int> &perChannel)
{
    QStringList args{e.output ? QStringLiteral("set-sink-volume") : QStringLiteral("set-source-volume"), e.node};
    for (int v : perChannel) {
        args << QStringLiteral("%1%").arg(v);
    }
    pactl(args);
}

inline void setMute(const Endpoint &e, bool mute)
{
    pactl({e.output ? QStringLiteral("set-sink-mute") : QStringLiteral("set-source-mute"), e.node, mute ? QStringLiteral("1") : QStringLiteral("0")});
}

inline void setDefault(const Endpoint &e)
{
    pactl({e.output ? QStringLiteral("set-default-sink") : QStringLiteral("set-default-source"), e.node});
}

inline void setDeviceEnabled(const Endpoint &e, bool on)
{
    if (e.card.isEmpty()) {
        return;
    }
    pactl({QStringLiteral("set-card-profile"), e.card, on ? e.enableProfile : e.disableProfile});
}

/* ---------------- test sound ---------------- */

inline const QStringList &channelLabels()
{
    static const QStringList l{tr7("L"), tr7("R"), tr7("C"), tr7("LFE"), tr7("RL"), tr7("RR"), tr7("SL"), tr7("SR")};
    return l;
}

inline QString channelShort(const QString &ch)
{
    static const QHash<QString, QString> m{{QStringLiteral("front-left"), tr7("L")},  {QStringLiteral("front-right"), tr7("R")},
                                           {QStringLiteral("front-center"), tr7("C")}, {QStringLiteral("lfe"), tr7("LFE")},
                                           {QStringLiteral("rear-left"), tr7("RL")},  {QStringLiteral("rear-right"), tr7("RR")},
                                           {QStringLiteral("side-left"), tr7("SL")},  {QStringLiteral("side-right"), tr7("SR")},
                                           {QStringLiteral("mono"), tr7("M")}};
    return m.value(ch, ch);
}

/** A short chime in the chosen channels (all if `only` < 0), written as WAV and played on the device */
inline QProcess *playTest(const QString &sink, const QStringList &channels, int only = -1, QObject *parent = nullptr)
{
    const int rate = 48000, n = qMax(1, int(channels.size()));
    const int frames = rate * 6 / 10;
    QByteArray pcm(frames * n * 2, '\0');
    auto *out = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < frames; ++i) {
        const double t = double(i) / rate;
        const double env = std::exp(-t * 6) * std::min(1.0, t * 200);
        const double s = env * (0.6 * std::sin(2 * M_PI * 880 * t) + 0.3 * std::sin(2 * M_PI * 1320 * t) + 0.1 * std::sin(2 * M_PI * 1760 * t));
        for (int c = 0; c < n; ++c) {
            out[i * n + c] = (only < 0 || c == only) ? qint16(s * 20000) : 0;
        }
    }
    auto *file = new QTemporaryFile(QDir::tempPath() + QStringLiteral("/seven-test-XXXXXX.wav"));
    file->open();
    auto le32 = [](quint32 v) {
        QByteArray b(4, '\0');
        qToLittleEndian(v, b.data());
        return b;
    };
    auto le16 = [](quint16 v) {
        QByteArray b(2, '\0');
        qToLittleEndian(v, b.data());
        return b;
    };
    file->write("RIFF" + le32(36 + pcm.size()) + "WAVEfmt " + le32(16) + le16(1) + le16(n) + le32(rate) + le32(rate * n * 2) + le16(n * 2) + le16(16) + "data" + le32(pcm.size()));
    file->write(pcm);
    file->flush();
    auto *p = new QProcess(parent);
    QStringList args{QStringLiteral("--device=") + sink};
    if (!channels.isEmpty()) {
        args << QStringLiteral("--channel-map=") + channels.join(QLatin1Char(','));
    }
    args << file->fileName();
    QObject::connect(p, &QProcess::finished, p, [p, file] {
        delete file;
        p->deleteLater();
    });
    p->start(QStringLiteral("paplay"), args);
    return p;
}

/* ---------------- level meters ---------------- */

/** Peak level of a device, read from a tiny parec stream while the window is open */
class Meter : public QObject
{
public:
    double level = 0;

    Meter(const QString &source, QObject *parent)
        : QObject(parent)
    {
        m_proc.start(QStringLiteral("parec"),
                     {QStringLiteral("--device=") + source, QStringLiteral("--raw"), QStringLiteral("--format=s16le"), QStringLiteral("--rate=8000"),
                      QStringLiteral("--channels=1"), QStringLiteral("--latency-msec=40"), QStringLiteral("--client-name=seven-control"),
                      QStringLiteral("--stream-name=peak")});
        connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this] {
            const QByteArray d = m_proc.readAllStandardOutput();
            const auto *s = reinterpret_cast<const qint16 *>(d.constData());
            int peak = 0;
            for (int i = 0; i < d.size() / 2; ++i) {
                peak = qMax(peak, qAbs(int(s[i])));
            }
            level = qMax(level * 0.8, peak / 32768.0);
        });
    }
    ~Meter() override
    {
        m_proc.kill();
        m_proc.waitForFinished(300);
    }

private:
    QProcess m_proc;
};

/* ---------------- device list ---------------- */

enum Roles {
    EndpointRole = Qt::UserRole + 1,
    LevelRole,
};

/** Icon with the green tick / grey or red arrow of the Windows 7 device list */
inline QPixmap endpointIcon(const Endpoint &e, int size = 48)
{
    QPixmap pm = QIcon::fromTheme(e.shownIcon(), QIcon::fromTheme(QStringLiteral("audio-card"))).pixmap(size, size);
    if (e.disabled || e.unplugged) {
        QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < img.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const int g = qGray(line[x]);
                line[x] = qRgba((g + 255) / 2, (g + 255) / 2, (g + 255) / 2, qAlpha(line[x]) * 3 / 4);
            }
        }
        pm = QPixmap::fromImage(img);
    }
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal r = size * 0.19;
    const QPointF c(size - r - 1, size - r - 1);
    if (e.isDefault) {
        QRadialGradient g(c - QPointF(r / 3, r / 3), r * 1.3);
        g.setColorAt(0, QColor(0x8e, 0xe0, 0x63));
        g.setColorAt(1, QColor(0x1e, 0x8f, 0x1e));
        p.setBrush(g);
        p.setPen(QPen(Qt::white, 1.2));
        p.drawEllipse(c, r, r);
        p.setPen(QPen(Qt::white, r * 0.32, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(QPolygonF({c + QPointF(-r * 0.5, 0), c + QPointF(-r * 0.12, r * 0.42), c + QPointF(r * 0.55, -r * 0.45)}));
    } else if (e.disabled || e.unplugged) {
        const QColor col = e.unplugged ? QColor(0xc8, 0x20, 0x20) : QColor(0x40, 0x40, 0x40);
        p.setBrush(Qt::white);
        p.setPen(QPen(col, 1));
        p.drawEllipse(c, r, r);
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawPolygon(QPolygonF({c + QPointF(-r * 0.55, -r * 0.1), c + QPointF(r * 0.55, -r * 0.1), c + QPointF(0, r * 0.6)}));
        p.drawRect(QRectF(c.x() - r * 0.2, c.y() - r * 0.6, r * 0.4, r * 0.55));
    }
    return pm;
}

class EndpointDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(200, 58);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        const Endpoint e = idx.data(EndpointRole).value<Endpoint>();
        p->save();
        const QRect r = opt.rect;
        if (opt.state & QStyle::State_Selected) { // the light blue Windows 7 selection
            QLinearGradient g(r.topLeft(), r.bottomLeft());
            g.setColorAt(0, QColor(0xf1, 0xf7, 0xfe));
            g.setColorAt(1, QColor(0xcf, 0xe4, 0xfa));
            p->setPen(QColor(0x84, 0xac, 0xdd));
            p->setBrush(g);
            p->drawRoundedRect(QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5), 3, 3);
        } else if (opt.state & QStyle::State_MouseOver) {
            QLinearGradient g(r.topLeft(), r.bottomLeft());
            g.setColorAt(0, QColor(0xfa, 0xfb, 0xfd));
            g.setColorAt(1, QColor(0xeb, 0xf3, 0xfd));
            p->setPen(QColor(0xb8, 0xd6, 0xfb));
            p->setBrush(g);
            p->drawRoundedRect(QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5), 3, 3);
        }
        p->drawPixmap(r.left() + 6, r.top() + 5, endpointIcon(e));
        QFont f = opt.font;
        p->setFont(f);
        p->setPen(e.disabled || e.unplugged ? QColor(0x6d, 0x6d, 0x6d) : QColor(Qt::black));
        const int tx = r.left() + 62;
        const int tw = r.width() - 62 - 30;
        const QFontMetrics fm(f);
        p->drawText(QRect(tx, r.top() + 6, tw, 16), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(e.shownName(), Qt::ElideRight, tw));
        p->setPen(QColor(0x6d, 0x6d, 0x6d));
        p->drawText(QRect(tx, r.top() + 22, tw, 16), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(e.deviceName, Qt::ElideRight, tw));
        p->drawText(QRect(tx, r.top() + 38, tw, 16), Qt::AlignLeft | Qt::AlignVCenter, e.status());
        if (!e.disabled && !e.unplugged) { // the green peak meter on the right
            const double level = idx.data(LevelRole).toDouble();
            const int bars = 10, lit = int(std::round(std::sqrt(level) * bars));
            for (int i = 0; i < bars; ++i) {
                const QRect bar(r.right() - 22, r.bottom() - 9 - i * 5, 12, 3);
                p->fillRect(bar, i < lit ? QColor(0x22, 0xb1, 0x4c) : QColor(0xd8, 0xe4, 0xe8));
            }
        }
        p->restore();
    }
};

/** A theme icon cut to its visible part, so small glyphs fill small buttons */
inline QIcon trimmedIcon(const QString &name, int size = 32)
{
    const QImage img = QIcon::fromTheme(name).pixmap(size, size).toImage();
    QRect box;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(img.pixel(x, y)) > 8) {
                box |= QRect(x, y, 1, 1);
            }
        }
    }
    return box.isEmpty() ? QIcon::fromTheme(name) : QIcon(QPixmap::fromImage(img.copy(box)));
}

/* ---------------- Balance ---------------- */

class BalanceDialog : public QDialog
{
public:
    BalanceDialog(const Endpoint &e, QWidget *parent)
        : QDialog(parent)
        , m_e(e)
    {
        setWindowTitle(tr7("Balance"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-sound")));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        auto *v = new QVBoxLayout(this);
        auto *grid = new QGridLayout;
        for (int i = 0; i < e.channels.size(); ++i) {
            auto *label = new QLabel(channelShort(e.channels.at(i)));
            auto *slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, 100);
            slider->setValue(e.volumes.value(i));
            slider->setFixedWidth(130);
            auto *box = new QSpinBox;
            box->setRange(0, 100);
            box->setValue(e.volumes.value(i));
            box->setButtonSymbols(QAbstractSpinBox::NoButtons);
            box->setFixedWidth(52);
            box->setAlignment(Qt::AlignRight);
            grid->addWidget(label, i, 0);
            grid->addWidget(slider, i, 1);
            grid->addWidget(box, i, 2);
            connect(slider, &QSlider::valueChanged, box, &QSpinBox::setValue);
            connect(box, &QSpinBox::valueChanged, slider, &QSlider::setValue);
            connect(slider, &QSlider::valueChanged, this, [this] {
                apply();
            });
            m_sliders << slider;
        }
        v->addLayout(grid);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        ok->setMinimumWidth(75);
        ok->setDefault(true);
        v->addWidget(ok, 0, Qt::AlignHCenter);
        setFixedSize(sizeHint());
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    }

    QList<int> values() const
    {
        QList<int> l;
        for (QSlider *s : m_sliders) {
            l << s->value();
        }
        return l;
    }

private:
    void apply()
    {
        setVolume(m_e, values());
    }

    Endpoint m_e;
    QList<QSlider *> m_sliders;
};

/* ---------------- device properties ---------------- */

inline const QList<QPair<QString, QString>> &iconChoices()
{
    static const QList<QPair<QString, QString>> l{
        {QStringLiteral("seven-snd-speakers"), tr7("Speakers")},     {QStringLiteral("seven-snd-headphones"), tr7("Headphones")},
        {QStringLiteral("seven-snd-headset"), tr7("Headset")},     {QStringLiteral("seven-snd-hdmi"), tr7("Monitor")},
        {QStringLiteral("seven-snd-digital"), tr7("Digital")},      {QStringLiteral("seven-snd-linein"), tr7("Line")},
        {QStringLiteral("seven-snd-mic"), tr7("Microphone")},          {QStringLiteral("seven-snd-webcam"), tr7("Webcam")},
        {QStringLiteral("seven-snd-phone"), tr7("Phone")},         {QStringLiteral("seven-snd-card"), tr7("Sound card")},
    };
    return l;
}

inline QString pickIcon(QWidget *parent, const QString &current)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Change Icon"));
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(tr7("Select an icon from the list below:")));
    auto *list = new QListWidget;
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(48, 48));
    list->setGridSize(QSize(84, 80));
    list->setMovement(QListView::Static);
    list->setFixedSize(370, 190);
    for (const auto &c : iconChoices()) {
        auto *it = new QListWidgetItem(QIcon::fromTheme(c.first), c.second, list);
        it->setData(Qt::UserRole, c.first);
        if (c.first == current) {
            list->setCurrentItem(it);
        }
    }
    v->addWidget(list);
    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    ok->setDefault(true);
    btns->addWidget(ok);
    btns->addWidget(cancel);
    v->addLayout(btns);
    dlg.setFixedSize(dlg.sizeHint());
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    if (dlg.exec() != QDialog::Accepted || !list->currentItem()) {
        return current;
    }
    return list->currentItem()->data(Qt::UserRole).toString();
}

inline QString wireplumberRules()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/wireplumber/wireplumber.conf.d/51-seven-sound.conf");
}

/** device node -> "S16LE 48000" chosen in Advanced, kept as WirePlumber rules */
inline QMap<QString, QString> formatRules()
{
    QMap<QString, QString> m;
    const QStringList list = store().value(QStringLiteral("Formats/Rules")).toStringList();
    for (const QString &r : list) {
        m.insert(r.section(QLatin1Char('|'), 0, 0), r.section(QLatin1Char('|'), 1));
    }
    return m;
}

inline void writeFormatRules(const QMap<QString, QString> &m)
{
    QStringList list;
    QString conf = QStringLiteral("# Default formats chosen in Windows 7 Sound > Properties > Advanced\nmonitor.alsa.rules = [\n");
    for (auto it = m.cbegin(); it != m.cend(); ++it) {
        list << it.key() + QLatin1Char('|') + it.value();
        conf += QStringLiteral("  {\n    matches = [ { node.name = \"%1\" } ]\n    actions = { update-props = { audio.format = \"%2\", audio.rate = %3 } }\n  }\n")
                    .arg(it.key(), it.value().section(QLatin1Char(' '), 0, 0), it.value().section(QLatin1Char(' '), 1, 1));
    }
    conf += QStringLiteral("]\n");
    store().setValue(QStringLiteral("Formats/Rules"), list);
    store().sync();
    QDir().mkpath(QFileInfo(wireplumberRules()).absolutePath());
    if (m.isEmpty()) {
        QFile::remove(wireplumberRules());
    } else {
        QFile f(wireplumberRules());
        if (f.open(QIODevice::WriteOnly)) {
            f.write(conf.toUtf8());
        }
    }
    QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("restart"), QStringLiteral("wireplumber")});
}

inline QString formatLabel(const QString &format, int rate)
{
    const int bits = format.contains(QLatin1String("32")) ? 32 : format.contains(QLatin1String("24")) ? 24 : 16;
    QString quality;
    if (bits == 16 && rate == 44100) {
        quality = tr7(" (CD Quality)");
    } else if (bits == 16 && rate == 48000) {
        quality = tr7(" (DVD Quality)");
    } else if (bits >= 24) {
        quality = tr7(" (Studio Quality)");
    }
    return tr7("%1 bit, %2 Hz%3").arg(bits).arg(rate).arg(quality);
}

class PropertiesDialog : public QDialog
{
public:
    std::function<void()> changedOutside;

    PropertiesDialog(const Endpoint &e, QWidget *parent)
        : QDialog(parent)
        , m_e(e)
    {
        setWindowTitle(tr7("%1 Properties").arg(e.shownName()));
        setWindowIcon(QIcon::fromTheme(e.shownIcon()));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setFixedWidth(420);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 9, 7, 9);
        auto *tabs = new QTabWidget;
        tabs->addTab(generalTab(), tr7("General"));
        if (!e.output && !e.node.endsWith(QLatin1String(".monitor")) && !e.disabled) {
            tabs->addTab(listenTab(), tr7("Listen"));
        }
        if (!e.disabled && !e.unplugged) {
            tabs->addTab(levelsTab(), tr7("Levels"));
            if (e.output && !e.node.isEmpty()) {
                const bool phones = e.node.startsWith(QLatin1String("bluez_output")) || e.typeName.contains(tr7("Headphone"))
                    || e.port.contains(QLatin1String("Headphone"), Qt::CaseInsensitive) || e.port.contains(tr7("Headphone"));
                m_fx = new Win7SoundFx::EnhancementsTab(e.node, e.shownName(), phones);
                m_fx->changed = [this] {
                    changed();
                };
                tabs->addTab(m_fx, tr7("Enhancements"));
            }
            tabs->addTab(advancedTab(), tr7("Advanced"));
        }
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
        setFixedHeight(480);
        connect(ok, &QPushButton::clicked, this, [this] {
            apply();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, [this] {
            if (m_volumeTouched) { // Windows: levels are undone by Cancel
                setVolume(m_e, m_e.volumes);
                setMute(m_e, m_e.mute);
            }
            reject();
        });
        connect(m_apply, &QPushButton::clicked, this, [this] {
            apply();
        });
    }

private:
    void changed()
    {
        m_apply->setEnabled(true);
    }

    static QGroupBox *group(const QString &title)
    {
        return new QGroupBox(title);
    }

    QWidget *generalTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(12, 12, 12, 12);
        auto *top = new QHBoxLayout;
        m_iconLabel = new QLabel;
        m_icon = m_e.shownIcon();
        m_iconLabel->setPixmap(QIcon::fromTheme(m_icon).pixmap(48, 48));
        top->addWidget(m_iconLabel, 0, Qt::AlignTop);
        top->addSpacing(40);
        auto *nameCol = new QVBoxLayout;
        m_name = new QLineEdit(m_e.shownName());
        nameCol->addWidget(m_name);
        auto *changeIcon = new QPushButton(tr7("Change Icon", "sound"));
        nameCol->addWidget(changeIcon, 0, Qt::AlignLeft);
        top->addLayout(nameCol, 1);
        v->addLayout(top);
        v->addSpacing(8);
        auto *ctl = group(tr7("Controller Information"));
        auto *cg = new QGridLayout(ctl);
        cg->addWidget(new QLabel(m_e.deviceName), 0, 0);
        auto *props = new QPushButton(tr7("Properties"));
        cg->addWidget(props, 0, 1, Qt::AlignRight);
        cg->addWidget(new QLabel(m_e.vendor.isEmpty() ? tr7("Unknown manufacturer") : m_e.vendor.section(QLatin1Char('['), 0, 0).trimmed()), 1, 0);
        cg->setColumnStretch(0, 1);
        v->addWidget(ctl);
        auto *jack = group(tr7("Jack Information"));
        auto *jv = new QVBoxLayout(jack);
        jv->addWidget(new QLabel(m_e.port.isEmpty() || m_e.port == QLatin1String("(null)") ? tr7("No jack information available") : m_e.port));
        v->addWidget(jack);
        v->addStretch(1);
        v->addWidget(new QLabel(tr7("Device usage:")));
        m_use = new QComboBox;
        m_use->addItems({tr7("Use this device (enable)"), tr7("Don't use this device (disable)")});
        m_use->setCurrentIndex(m_e.disabled ? 1 : 0);
        m_use->setEnabled(!m_e.card.isEmpty() && !m_e.unplugged && !m_e.node.endsWith(QLatin1String(".monitor")));
        v->addWidget(m_use);
        connect(changeIcon, &QPushButton::clicked, this, [this] {
            const QString icon = pickIcon(this, m_icon);
            if (icon != m_icon) {
                m_icon = icon;
                m_iconLabel->setPixmap(QIcon::fromTheme(m_icon).pixmap(48, 48));
                changed();
            }
        });
        connect(props, &QPushButton::clicked, this, [this] {
            QProcess::startDetached(QDir::homePath() + QStringLiteral("/.local/bin/seven-devmgr"), {QStringLiteral("--properties"), m_e.deviceName});
        });
        connect(m_name, &QLineEdit::textEdited, this, [this] {
            changed();
        });
        connect(m_use, &QComboBox::currentIndexChanged, this, [this] {
            changed();
        });
        return page;
    }

    QWidget *listenTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(12, 12, 12, 12);
        auto *intro = new QLabel(tr7("You can listen to a portable music player or other device through this device. Connecting a microphone may cause feedback."));
        intro->setWordWrap(true);
        v->addWidget(intro);
        auto *pic = new QHBoxLayout;
        pic->addStretch(1);
        for (const QString &icon : {m_e.shownIcon(), QStringLiteral("seven-snd-arrow"), QStringLiteral("seven-snd-speakers")}) {
            auto *l = new QLabel;
            l->setPixmap(QIcon::fromTheme(icon).pixmap(icon == QLatin1String("seven-snd-arrow") ? 32 : 40, icon == QLatin1String("seven-snd-arrow") ? 32 : 40));
            pic->addWidget(l);
            pic->addSpacing(10);
        }
        pic->addStretch(1);
        v->addLayout(pic);
        m_listen = new QCheckBox(tr7("Listen to this device"));
        m_listen->setChecked(store().contains(QStringLiteral("Loopback/") + key(m_e.node)));
        v->addWidget(m_listen);
        v->addWidget(new QLabel(tr7("Playback through this device:")));
        m_listenTo = new QComboBox;
        m_listenTo->addItem(tr7("Default Playback Device"), QString());
        for (const Endpoint &o : endpoints(true, false, false)) {
            m_listenTo->addItem(o.shownName() + QStringLiteral(" (") + o.deviceName + QLatin1Char(')'), o.node);
        }
        const QString saved = store().value(QStringLiteral("Loopback/") + key(m_e.node)).toString().section(QLatin1Char('|'), 1);
        m_listenTo->setCurrentIndex(qMax(0, m_listenTo->findData(saved)));
        v->addWidget(m_listenTo);
        v->addStretch(1);
        connect(m_listen, &QCheckBox::toggled, this, [this] {
            changed();
        });
        connect(m_listenTo, &QComboBox::currentIndexChanged, this, [this] {
            changed();
        });
        return page;
    }

    QWidget *levelsTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(12, 12, 12, 12);
        auto *box = group(m_e.output ? m_e.shownName() : m_e.shownName());
        auto *h = new QHBoxLayout(box);
        m_slider = new QSlider(Qt::Horizontal);
        m_slider->setRange(0, 100);
        m_slider->setValue(m_e.volume());
        m_slider->setTickPosition(QSlider::TicksBelow);
        m_slider->setTickInterval(10);
        auto *spin = new QSpinBox;
        spin->setRange(0, 100);
        spin->setValue(m_e.volume());
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setAlignment(Qt::AlignRight);
        spin->setFixedWidth(52);
        m_muteBtn = new QToolButton;
        m_muteBtn->setCheckable(true);
        m_muteBtn->setChecked(m_e.mute);
        m_muteBtn->setIconSize(QSize(18, 16));
        m_muteBtn->setFixedSize(26, 24);
        auto updateMute = [this] {
            m_muteBtn->setIcon(trimmedIcon(m_muteBtn->isChecked() ? QStringLiteral("seven-snd-muted") : QStringLiteral("seven-snd-unmuted")));
            m_muteBtn->setToolTip(m_muteBtn->isChecked() ? tr7("Unmute") : tr7("Mute"));
        };
        updateMute();
        h->addWidget(m_slider, 1);
        h->addWidget(spin);
        h->addWidget(m_muteBtn);
        if (m_e.channels.size() > 1) {
            auto *balance = new QPushButton(tr7("Balance"));
            h->addWidget(balance);
            connect(balance, &QPushButton::clicked, this, [this, spin] {
                Endpoint cur = m_e;
                cur.volumes.clear();
                // keep the current proportions when the master slider moved
                for (int i = 0; i < m_e.channels.size(); ++i) {
                    cur.volumes << (m_balance.size() > i ? m_balance.at(i) : m_slider->value());
                }
                BalanceDialog dlg(cur, this);
                dlg.exec();
                m_balance = dlg.values();
                int mx = 0;
                for (int x : m_balance) {
                    mx = qMax(mx, x);
                }
                m_slider->blockSignals(true);
                spin->blockSignals(true);
                m_slider->setValue(mx);
                spin->setValue(mx);
                m_slider->blockSignals(false);
                spin->blockSignals(false);
                m_volumeTouched = true;
            });
        }
        v->addWidget(box);
        v->addStretch(1);
        connect(m_slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        connect(spin, &QSpinBox::valueChanged, m_slider, &QSlider::setValue);
        connect(m_slider, &QSlider::valueChanged, this, [this](int value) {
            // move all channels, keeping the balance
            QList<int> vols;
            int mx = 0;
            for (int x : (m_balance.isEmpty() ? m_e.volumes : m_balance)) {
                mx = qMax(mx, x);
            }
            for (int i = 0; i < qMax(1, int(m_e.channels.size())); ++i) {
                const int base = (m_balance.isEmpty() ? m_e.volumes : m_balance).value(i, mx);
                vols << (mx > 0 ? base * value / mx : value);
            }
            setVolume(m_e, vols);
            m_volumeTouched = true;
        });
        connect(m_muteBtn, &QToolButton::toggled, this, [this, updateMute](bool on) {
            updateMute();
            setMute(m_e, on);
            m_volumeTouched = true;
        });
        return page;
    }

    QWidget *advancedTab()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(12, 12, 12, 12);
        auto *box = group(tr7("Default Format"));
        auto *bv = new QVBoxLayout(box);
        auto *t = new QLabel(m_e.output ? tr7("Select the sample rate and bit depth to be used when running in shared mode.")
                                        : tr7("Select the sample rate and bit depth to be used when running in shared mode."));
        t->setWordWrap(true);
        bv->addWidget(t);
        auto *row = new QHBoxLayout;
        m_format = new QComboBox;
        const QString spec = m_e.sampleSpec; // "s32le 2ch 48000Hz"
        const QString curFormat = spec.section(QLatin1Char(' '), 0, 0).toUpper();
        const int curRate = spec.section(QLatin1Char(' '), 2, 2).remove(QStringLiteral("Hz")).toInt();
        const QString saved = formatRules().value(m_e.node);
        const QList<QPair<QString, int>> choices{{QStringLiteral("S16LE"), 44100}, {QStringLiteral("S16LE"), 48000}, {QStringLiteral("S24LE"), 44100},
                                                 {QStringLiteral("S24LE"), 48000}, {QStringLiteral("S24LE"), 96000}, {QStringLiteral("S24LE"), 192000},
                                                 {QStringLiteral("S32LE"), 48000}, {QStringLiteral("S32LE"), 96000}};
        for (const auto &c : choices) {
            m_format->addItem(formatLabel(c.first, c.second), c.first + QLatin1Char(' ') + QString::number(c.second));
        }
        QString current = saved.isEmpty() ? curFormat + QLatin1Char(' ') + QString::number(curRate) : saved;
        if (current.startsWith(QLatin1String("FLOAT32"))) {
            current = QStringLiteral("S32LE ") + current.section(QLatin1Char(' '), 1);
        }
        int i = m_format->findData(current);
        if (i < 0) {
            m_format->insertItem(0, formatLabel(current.section(QLatin1Char(' '), 0, 0), current.section(QLatin1Char(' '), 1).toInt()), current);
            i = 0;
        }
        m_format->setCurrentIndex(i);
        m_formatStart = m_format->currentData().toString();
        row->addWidget(m_format, 1);
        if (m_e.output) {
            auto *test = new QPushButton(QIcon::fromTheme(QStringLiteral("seven-snd-play")), tr7("Test"));
            row->addWidget(test);
            connect(test, &QPushButton::clicked, this, [this] {
                playTest(m_e.node, m_e.channels, -1, this);
            });
        }
        bv->addLayout(row);
        v->addWidget(box);
        v->addStretch(1);
        auto *defaults = new QPushButton(tr7("Default"));
        v->addWidget(defaults, 0, Qt::AlignLeft);
        connect(m_format, &QComboBox::currentIndexChanged, this, [this] {
            changed();
        });
        connect(defaults, &QPushButton::clicked, this, [this] {
            m_formatDefault = true;
            changed();
        });
        return page;
    }

    void apply()
    {
        if (!m_apply->isEnabled()) {
            return;
        }
        const QString name = m_name->text().trimmed();
        if (name.isEmpty() || name == m_e.typeName) {
            store().remove(QStringLiteral("Names/") + key(m_e.id()));
        } else {
            store().setValue(QStringLiteral("Names/") + key(m_e.id()), name);
        }
        if (m_icon == m_e.icon) {
            store().remove(QStringLiteral("Icons/") + key(m_e.id()));
        } else {
            store().setValue(QStringLiteral("Icons/") + key(m_e.id()), m_icon);
        }
        store().sync();
        setWindowTitle(tr7("%1 Properties").arg(name.isEmpty() ? m_e.typeName : name));
        if (m_listen) {
            const QString k = QStringLiteral("Loopback/") + key(m_e.node);
            const QString old = store().value(k).toString();
            const QString target = m_listenTo->currentData().toString();
            if (!old.isEmpty() && (!m_listen->isChecked() || old.section(QLatin1Char('|'), 1) != target)) {
                pactl({QStringLiteral("unload-module"), old.section(QLatin1Char('|'), 0, 0)});
                store().remove(k);
            }
            if (m_listen->isChecked() && (old.isEmpty() || old.section(QLatin1Char('|'), 1) != target)) {
                QStringList args{QStringLiteral("load-module"), QStringLiteral("module-loopback"), QStringLiteral("source=") + m_e.node, QStringLiteral("latency_msec=40")};
                if (!target.isEmpty()) {
                    args << QStringLiteral("sink=") + target;
                }
                const QString id = sh(QStringLiteral("pactl"), args).trimmed();
                if (!id.isEmpty()) {
                    store().setValue(k, id + QLatin1Char('|') + target);
                }
            }
            store().sync();
        }
        if (m_format) {
            QMap<QString, QString> rules = formatRules();
            const QString chosen = m_format->currentData().toString();
            if (m_formatDefault && rules.contains(m_e.node)) {
                rules.remove(m_e.node);
                writeFormatRules(rules);
            } else if (!m_formatDefault && chosen != m_formatStart) {
                rules.insert(m_e.node, chosen);
                writeFormatRules(rules);
            }
            m_formatStart = chosen;
            m_formatDefault = false;
        }
        if (m_fx) {
            m_fx->apply();
        }
        const bool wantOn = m_use->currentIndex() == 0;
        if (m_use->isEnabled() && wantOn == m_e.disabled) {
            setDeviceEnabled(m_e, wantOn);
        }
        if (changedOutside) {
            changedOutside();
        }
        m_apply->setEnabled(false);
    }

    Endpoint m_e;
    QPushButton *m_apply;
    QLabel *m_iconLabel = nullptr;
    QString m_icon;
    QLineEdit *m_name = nullptr;
    QComboBox *m_use = nullptr;
    QCheckBox *m_listen = nullptr;
    QComboBox *m_listenTo = nullptr;
    QSlider *m_slider = nullptr;
    QToolButton *m_muteBtn = nullptr;
    QList<int> m_balance;
    bool m_volumeTouched = false;
    QComboBox *m_format = nullptr;
    Win7SoundFx::EnhancementsTab *m_fx = nullptr;
    QString m_formatStart;
    bool m_formatDefault = false;
};

/* ---------------- Speaker Setup ---------------- */

struct SpeakerConfig {
    QString profile;
    QString title;
    QStringList channels;
};

/** The speaker room picture: listener in the middle, speakers around, the tested one sounding */
class SpeakerRoom : public QWidget
{
public:
    QStringList channels;  // present speakers
    QString sounding;      // channel being tested
    std::function<void(const QString &)> clicked;

    SpeakerRoom()
    {
        setFixedSize(190, 190);
        setCursor(Qt::PointingHandCursor);
    }

    QPointF position(const QString &ch) const
    {
        static const QHash<QString, QPointF> pos{{QStringLiteral("front-left"), {38, 22}},   {QStringLiteral("front-center"), {95, 22}},
                                                 {QStringLiteral("front-right"), {152, 22}}, {QStringLiteral("lfe"), {175, 22}},
                                                 {QStringLiteral("side-left"), {12, 105}},   {QStringLiteral("side-right"), {178, 105}},
                                                 {QStringLiteral("rear-left"), {38, 168}},   {QStringLiteral("rear-right"), {152, 168}},
                                                 {QStringLiteral("mono"), {95, 22}}};
        return pos.value(ch, QPointF(95, 22));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF c(95, 98);
        // the listening arcs
        for (int i = 4; i >= 1; --i) {
            p.setPen(QPen(QColor(0xc4, 0xcc, 0xd4), 1));
            QRadialGradient g(c, i * 18);
            g.setColorAt(0, i == 1 ? QColor(0xc9, 0xe6, 0xfa) : QColor(0xf7, 0xf9, 0xfb));
            g.setColorAt(1, i == 1 ? QColor(0xe4, 0xf2, 0xfc) : QColor(0xe9, 0xed, 0xf1));
            p.setBrush(QBrush(g));
            p.drawPie(QRectF(c.x() - i * 18, c.y() - i * 18, i * 36, i * 36), 20 * 16, 140 * 16);
        }
        // listener
        p.setPen(QPen(QColor(0x55, 0x55, 0x55), 1));
        p.setBrush(QColor(0xee, 0xee, 0xee));
        p.drawEllipse(c, 9, 7);
        p.drawEllipse(c + QPointF(0, -3), 4, 4);
        for (const QString &ch : channels) {
            const QPointF at = position(ch);
            const bool on = ch == sounding;
            if (on) { // sound waves
                p.setBrush(Qt::NoBrush);
                for (int i = 1; i <= 3; ++i) {
                    p.setPen(QPen(QColor(0x50, 0x50, 0x50, 200 - i * 50), 1.4));
                    p.drawArc(QRectF(at.x() - 8 - i * 6, at.y() - 8 - i * 6, 16 + i * 12, 16 + i * 12), (at.y() < c.y() ? 200 : 20) * 16, 140 * 16);
                }
            }
            QLinearGradient g(at - QPointF(0, 9), at + QPointF(0, 9));
            g.setColorAt(0, on ? QColor(0xc8, 0xe8, 0xff) : QColor(0xf2, 0xf2, 0xf2));
            g.setColorAt(1, on ? QColor(0x60, 0x9e, 0xd8) : QColor(0x8c, 0x8c, 0x8c));
            p.setPen(QPen(QColor(0x50, 0x50, 0x50), 1));
            p.setBrush(g);
            if (ch == QLatin1String("lfe")) {
                p.drawRoundedRect(QRectF(at.x() - 9, at.y() - 9, 18, 18), 2, 2);
            } else {
                QPolygonF cone({at + QPointF(-4, -8), at + QPointF(4, -8), at + QPointF(9, 6), at + QPointF(-9, 6)});
                if (at.y() > c.y()) {
                    cone = QPolygonF({at + QPointF(-4, 8), at + QPointF(4, 8), at + QPointF(9, -6), at + QPointF(-9, -6)});
                }
                p.drawPolygon(cone);
            }
            QFont f = font();
            f.setPixelSize(9);
            p.setFont(f);
            p.setPen(QColor(0x40, 0x40, 0x40));
            const QRectF label(at.x() - 10, at.y() + (at.y() > c.y() ? -22 : -21), 20, 11);
            p.setBrush(Qt::white);
            p.drawRect(label.translated(0, at.y() > c.y() ? 0 : -0));
            p.drawText(label, Qt::AlignCenter, channelShort(ch));
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        for (const QString &ch : channels) {
            if (QLineF(position(ch), e->position()).length() < 14 && clicked) {
                clicked(ch);
            }
        }
    }
};

class SpeakerWizard : public QDialog
{
public:
    SpeakerWizard(const Endpoint &e, QWidget *parent)
        : QDialog(parent)
        , m_e(e)
    {
        setWindowTitle(tr7("Speaker Setup"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-sound")));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setFixedSize(560, 430);
        for (const QString &prof : e.surroundProfiles) {
            SpeakerConfig c{prof, {}, {}};
            if (prof.contains(QLatin1String("surround71")) || prof.contains(QLatin1String("surround-71"))) {
                c = {prof, tr7("7.1 Surround"), {QStringLiteral("front-left"), QStringLiteral("front-right"), QStringLiteral("front-center"), QStringLiteral("lfe"), QStringLiteral("rear-left"), QStringLiteral("rear-right"), QStringLiteral("side-left"), QStringLiteral("side-right")}};
            } else if (prof.contains(QLatin1String("surround-51")) || prof.contains(QLatin1String("surround")) && !prof.contains(QLatin1String("40"))) {
                c = {prof, tr7("5.1 Surround"), {QStringLiteral("front-left"), QStringLiteral("front-right"), QStringLiteral("front-center"), QStringLiteral("lfe"), QStringLiteral("rear-left"), QStringLiteral("rear-right")}};
            } else if (prof.contains(QLatin1String("surround-40")) || prof.contains(QLatin1String("quad"))) {
                c = {prof, tr7("Quadraphonic"), {QStringLiteral("front-left"), QStringLiteral("front-right"), QStringLiteral("rear-left"), QStringLiteral("rear-right")}};
            } else if (prof.contains(QLatin1String("mono"))) {
                c = {prof, tr7("Mono"), {QStringLiteral("mono")}};
            } else if (prof.contains(QLatin1String("stereo")) || prof.startsWith(QLatin1String("a2dp")) || prof.contains(QLatin1String("headset"))) {
                c = {prof, tr7("Stereo"), {QStringLiteral("front-left"), QStringLiteral("front-right")}};
            }
            if (c.title.isEmpty()) {
                continue;
            }
            bool dup = false;
            for (const SpeakerConfig &o : std::as_const(m_configs)) {
                dup = dup || o.title == c.title;
            }
            if (!dup) {
                m_configs.append(c);
            }
        }
        if (m_configs.isEmpty()) {
            m_configs.append({QString(), tr7("Stereo"), e.channels.isEmpty() ? QStringList{QStringLiteral("front-left"), QStringLiteral("front-right")} : e.channels});
        }

        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        auto *body = new QWidget;
        body->setObjectName(QStringLiteral("win7wiz"));
        body->setAttribute(Qt::WA_StyledBackground, true);
        body->setStyleSheet(QStringLiteral("#win7wiz { background: white; }"));
        auto *bh = new QHBoxLayout(body);
        bh->setContentsMargins(40, 24, 24, 12);
        auto *left = new QVBoxLayout;
        m_title = new QLabel;
        QFont tf = m_title->font();
        tf.setPixelSize(16);
        m_title->setFont(tf);
        m_title->setStyleSheet(QStringLiteral("color: #1e3287;"));
        left->addWidget(m_title);
        left->addSpacing(14);
        m_text = new QLabel;
        m_text->setWordWrap(true);
        m_text->setFixedWidth(270);
        left->addWidget(m_text);
        left->addSpacing(14);
        m_listLabel = new QLabel;
        left->addWidget(m_listLabel);
        m_list = new QListWidget;
        m_list->setFixedSize(165, 100);
        for (const SpeakerConfig &c : std::as_const(m_configs)) {
            m_list->addItem(c.title);
        }
        int cur = 0;
        for (int i = 0; i < m_configs.size(); ++i) {
            if (int(m_configs.at(i).channels.size()) == int(e.channels.size())) {
                cur = i;
            }
        }
        m_list->setCurrentRow(cur);
        left->addWidget(m_list);
        m_checks = new QWidget;
        m_checksLayout = new QVBoxLayout(m_checks);
        m_checksLayout->setContentsMargins(0, 0, 0, 0);
        left->addWidget(m_checks);
        left->addSpacing(16);
        m_test = new QPushButton(QIcon::fromTheme(QStringLiteral("seven-snd-play")), tr7("Test"));
        m_test->setFixedWidth(100);
        left->addWidget(m_test, 0, Qt::AlignLeft);
        left->addStretch(1);
        bh->addLayout(left);
        bh->addStretch(1);
        auto *right = new QVBoxLayout;
        right->addSpacing(30);
        m_room = new SpeakerRoom;
        right->addWidget(m_room, 0, Qt::AlignHCenter);
        right->addSpacing(14);
        m_hint = new QLabel(tr7("Click on a speaker above to test it."));
        right->addWidget(m_hint, 0, Qt::AlignHCenter);
        right->addStretch(1);
        bh->addLayout(right);
        v->addWidget(body, 1);
        auto *band = new QWidget;
        band->setObjectName(QStringLiteral("win7band"));
        band->setAttribute(Qt::WA_StyledBackground, true);
        band->setStyleSheet(QStringLiteral("#win7band { background: #f0f0f0; border-top: 1px solid #dfdfdf; }"));
        auto *bl = new QHBoxLayout(band);
        bl->setContentsMargins(12, 10, 12, 10);
        m_back = new QPushButton(tr7("< Back"));
        bl->addWidget(m_back);
        bl->addStretch(1);
        m_next = new QPushButton(tr7("Next"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        m_next->setMinimumWidth(75);
        cancel->setMinimumWidth(75);
        m_next->setDefault(true);
        bl->addWidget(m_next);
        bl->addWidget(cancel);
        v->addWidget(band);

        m_cycle.setInterval(900);
        connect(&m_cycle, &QTimer::timeout, this, [this] {
            nextChannel();
        });
        connect(m_test, &QPushButton::clicked, this, [this] {
            if (m_cycle.isActive()) {
                stopTest();
            } else {
                m_cycleIndex = -1;
                m_cycle.start();
                m_test->setText(tr7("Stop", "test"));
                m_test->setIcon(QIcon::fromTheme(QStringLiteral("seven-snd-stop")));
                nextChannel();
            }
        });
        m_room->clicked = [this](const QString &ch) {
            stopTest();
            sound(ch);
        };
        connect(m_list, &QListWidget::currentRowChanged, this, [this] {
            m_room->channels = config().channels;
            m_room->update();
        });
        connect(m_back, &QPushButton::clicked, this, [this] {
            showPage(m_page - 1 == 1 && !hasOptional() ? 0 : m_page - 1);
        });
        connect(m_next, &QPushButton::clicked, this, [this] {
            if (m_page == 3) {
                finish();
                return;
            }
            showPage(m_page == 0 && !hasOptional() ? 2 : m_page + 1);
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        showPage(0);
    }

private:
    SpeakerConfig config() const
    {
        return m_configs.value(qMax(0, m_list->currentRow()));
    }

    bool hasOptional() const
    {
        return config().channels.size() > 2;
    }

    QStringList present() const
    {
        QStringList chs = config().channels;
        for (auto it = m_missing.cbegin(); it != m_missing.cend(); ++it) {
            if (it.value()) {
                for (const QString &c : it.key().split(QLatin1Char(','))) {
                    chs.removeAll(c);
                }
            }
        }
        return chs;
    }

    void sound(const QString &ch)
    {
        m_room->sounding = ch;
        m_room->update();
        playTest(m_e.node, m_e.channels, int(m_e.channels.indexOf(ch)), this);
        QTimer::singleShot(700, this, [this, ch] {
            if (m_room->sounding == ch) {
                m_room->sounding.clear();
                m_room->update();
            }
        });
    }

    void nextChannel()
    {
        const QStringList chs = present();
        if (chs.isEmpty()) {
            return;
        }
        m_cycleIndex = (m_cycleIndex + 1) % chs.size();
        sound(chs.at(m_cycleIndex));
    }

    void stopTest()
    {
        m_cycle.stop();
        m_test->setText(tr7("Test"));
        m_test->setIcon(QIcon::fromTheme(QStringLiteral("seven-snd-play")));
    }

    void clearChecks()
    {
        while (QLayoutItem *it = m_checksLayout->takeAt(0)) {
            delete it->widget();
            delete it;
        }
    }

    void showPage(int page)
    {
        stopTest();
        m_page = page;
        clearChecks();
        m_back->setVisible(page > 0);
        m_list->setVisible(page == 0);
        m_listLabel->setVisible(page < 3);
        m_test->setVisible(page == 0);
        m_room->setVisible(page < 3);
        m_hint->setVisible(page < 3);
        m_next->setText(page == 3 ? tr7("Done") : tr7("Next"));
        m_room->channels = page == 0 ? config().channels : present();
        m_room->update();
        if (page == 0) {
            m_title->setText(tr7("Choose your configuration"));
            m_text->setText(tr7("Select the speaker setup below that is most like the configuration on your computer."));
            m_listLabel->setText(tr7("Audio channels:"));
        } else if (page == 1) {
            m_title->setText(tr7("Customize your configuration"));
            m_text->setText(tr7("Select which speakers are present in your configuration."));
            m_listLabel->setText(tr7("Optional speakers:"));
            const QStringList chs = config().channels;
            const QList<QPair<QString, QString>> optional{{QStringLiteral("front-center"), tr7("Center")},
                                                          {QStringLiteral("lfe"), tr7("Subwoofer")},
                                                          {QStringLiteral("side-left,side-right"), tr7("Side pair")},
                                                          {QStringLiteral("rear-left,rear-right"), tr7("Rear pair")}};
            for (const auto &o : optional) {
                auto *cb = new QCheckBox(o.second);
                const bool exists = chs.contains(o.first.section(QLatin1Char(','), 0, 0));
                cb->setEnabled(exists);
                cb->setChecked(exists && !m_missing.value(o.first));
                connect(cb, &QCheckBox::toggled, this, [this, o](bool on) {
                    m_missing[o.first] = !on;
                    m_room->channels = present();
                    m_room->update();
                });
                m_checksLayout->addWidget(cb);
            }
        } else if (page == 2) {
            m_title->setText(tr7("Select full-range speakers"));
            m_text->setText(tr7("Satellite speakers often have a limited range and need a subwoofer. If your configuration has full-range speakers, select them below."));
            m_listLabel->setText(tr7("Full-range speakers:"));
            auto *front = new QCheckBox(tr7("Front left and right"));
            front->setChecked(true);
            m_checksLayout->addWidget(front);
            if (present().size() > 2) {
                auto *surround = new QCheckBox(tr7("Surround speakers"));
                surround->setChecked(true);
                m_checksLayout->addWidget(surround);
            }
        } else {
            m_title->setText(tr7("Configuration complete"));
            m_text->setText(tr7("After you click Finish, the new settings are applied to your audio device. To make other changes, you can use the Properties dialog or run this wizard again."));
        }
    }

    void finish()
    {
        const SpeakerConfig c = config();
        if (!c.profile.isEmpty() && !m_e.card.isEmpty()) {
            pactl({QStringLiteral("set-card-profile"), m_e.card, c.profile.contains(QLatin1Char('+')) || c.profile.startsWith(QLatin1String("a2dp")) || c.profile.startsWith(QLatin1String("headset"))
                                                                   ? c.profile
                                                                   : c.profile});
        }
        accept();
    }

    Endpoint m_e;
    QList<SpeakerConfig> m_configs;
    QHash<QString, bool> m_missing;
    int m_page = 0;
    QLabel *m_title, *m_text, *m_listLabel, *m_hint;
    QListWidget *m_list;
    QWidget *m_checks;
    QVBoxLayout *m_checksLayout;
    QPushButton *m_test, *m_back, *m_next;
    SpeakerRoom *m_room;
    QTimer m_cycle;
    int m_cycleIndex = -1;
};

/* ---------------- sound schemes and program events ---------------- */

struct SoundEvent {
    QString group;   // "Windows" / "Провідник"
    QString title;   // Windows 7 name
    QString app;     // notifyrc name
    QString event;   // Event/<event>
};

inline const QList<SoundEvent> &soundEvents()
{
    static const QList<SoundEvent> l{
        {QStringLiteral("Windows"), tr7("Exit Windows"), QStringLiteral("plasma_workspace"), QStringLiteral("exitkde")},
        {QStringLiteral("Windows"), tr7("Windows Logon"), QStringLiteral("plasma_workspace"), QStringLiteral("startkde")},
        {QStringLiteral("Windows"), tr7("Question"), QStringLiteral("plasma_workspace"), QStringLiteral("messageQuestion")},
        {QStringLiteral("Windows"), tr7("Default Beep"), QStringLiteral("plasma_workspace"), QStringLiteral("beep")},
        {QStringLiteral("Windows"), tr7("Exclamation"), QStringLiteral("plasma_workspace"), QStringLiteral("messageWarning")},
        {QStringLiteral("Windows"), tr7("Critical Stop"), QStringLiteral("plasma_workspace"), QStringLiteral("messageCritical")},
        {QStringLiteral("Windows"), tr7("Critical Error"), QStringLiteral("plasma_workspace"), QStringLiteral("fatalerror")},
        {QStringLiteral("Windows"), tr7("Critical Battery Alarm"), QStringLiteral("powerdevil"), QStringLiteral("criticalbattery")},
        {QStringLiteral("Windows"), tr7("Low Battery Alarm"), QStringLiteral("powerdevil"), QStringLiteral("lowbattery")},
        {QStringLiteral("Windows"), tr7("Device Disconnect"), QStringLiteral("plasma_workspace"), QStringLiteral("deviceRemoved")},
        {QStringLiteral("Windows"), tr7("Device Connect"), QStringLiteral("plasma_workspace"), QStringLiteral("deviceAdded")},
        {QStringLiteral("Windows"), tr7("Print Error"), QStringLiteral("plasma_workspace"), QStringLiteral("printerror")},
        {QStringLiteral("Windows"), tr7("System Notification"), QStringLiteral("plasma_workspace"), QStringLiteral("messageInformation")},
        {QStringLiteral("Windows"), tr7("Power Connected"), QStringLiteral("powerdevil"), QStringLiteral("pluggedin")},
        {QStringLiteral("Windows"), tr7("Power Disconnected"), QStringLiteral("powerdevil"), QStringLiteral("unplugged")},
        {tr7("Explore"), tr7("Empty Recycle Bin"), QStringLiteral("plasma_workspace"), QStringLiteral("Trash: emptied")},
    };
    return l;
}

/** freedesktop sound name -> what Windows 7 calls that sound file */
inline QString soundLabel(const QString &name)
{
    static const QHash<QString, QString> m{
        {QStringLiteral("desktop-login"), tr7("Windows Logon")},        {QStringLiteral("desktop-logout"), tr7("Exit Windows")},
        {QStringLiteral("dialog-question"), tr7("Question")},              {QStringLiteral("bell-window-system"), tr7("Ring")},
        {QStringLiteral("bell"), tr7("Ring")},                      {QStringLiteral("dialog-warning"), tr7("Exclamation")},
        {QStringLiteral("dialog-error-critical"), tr7("Critical Stop")}, {QStringLiteral("dialog-error-serious"), tr7("Critical Error")},
        {QStringLiteral("dialog-error"), tr7("Error")},              {QStringLiteral("battery-low"), tr7("Critical Battery Alarm")},
        {QStringLiteral("battery-caution"), tr7("Low Battery Alarm")}, {QStringLiteral("battery-full"), tr7("Battery Charged")},
        {QStringLiteral("device-added"), tr7("Device Connect")},  {QStringLiteral("device-removed"), tr7("Device Disconnect")},
        {QStringLiteral("device-error"), tr7("Device Failed to Connect")}, {QStringLiteral("dialog-information"), tr7("Notification")},
        {QStringLiteral("trash-empty"), tr7("Empty Recycle Bin")},        {QStringLiteral("system-startup"), tr7("Windows Startup")},
        {QStringLiteral("system-shutdown"), tr7("Windows Shutdown")}, {QStringLiteral("power-plug"), tr7("Power Connected")},
        {QStringLiteral("power-unplug"), tr7("Power Disconnected")},  {QStringLiteral("message-new-email"), tr7("New Mail Notification")},
        {QStringLiteral("message-new-instant"), tr7("New Text Message Notification")}, {QStringLiteral("audio-volume-change"), tr7("Volume Change")},
        {QStringLiteral("desktop-unlock"), tr7("Windows Unlock")},       {QStringLiteral("window-inactive-click"), tr7("Click")},
        {QStringLiteral("dialog-calendar"), tr7("Reminder")},       {QStringLiteral("phone-incoming-call"), tr7("Incoming Call")},
    };
    return m.value(name, name);
}

struct Theme {
    QString dir;  // folder name in .../sounds
    QString name; // shown name
    bool user = false;
};

inline QString themeTitle(const QString &dir, const QString &name)
{
    static const QHash<QString, QString> m{{QStringLiteral("Windows 7"), tr7("Default")},
                                           {QStringLiteral("Windows 7 Afternoon"), tr7("Afternoon")},
                                           {QStringLiteral("Windows 7 Calligraphy"), tr7("Calligraphy")},
                                           {QStringLiteral("Windows 7 Characters"), tr7("Characters", "sound scheme")},
                                           {QStringLiteral("Windows 7 Cityscape"), tr7("Cityscape")},
                                           {QStringLiteral("Windows 7 Delta"), tr7("Delta")},
                                           {QStringLiteral("Windows 7 Festival"), tr7("Festival")},
                                           {QStringLiteral("Windows 7 Garden"), tr7("Garden")},
                                           {QStringLiteral("Windows 7 Heritage"), tr7("Heritage")},
                                           {QStringLiteral("Windows 7 Landscape"), tr7("Landscape", "sound scheme")},
                                           {QStringLiteral("Windows 7 Quirky"), tr7("Quirky")},
                                           {QStringLiteral("Windows 7 Raga"), tr7("Raga")},
                                           {QStringLiteral("Windows 7 Savanna"), tr7("Savanna")},
                                           {QStringLiteral("Windows 7 Sonata"), tr7("Sonata")}};
    return m.value(dir, name);
}

inline QList<Theme> themes()
{
    QList<Theme> list;
    QSet<QString> seen;
    const QString userDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/sounds");
    for (const QString &base : QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("sounds"), QStandardPaths::LocateDirectory)) {
        for (const QString &d : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString index = base + QLatin1Char('/') + d + QStringLiteral("/index.theme");
            if (seen.contains(d) || !QFile::exists(index) || d == QLatin1String("speech-dispatcher")) {
                continue;
            }
            QSettings ini(index, QSettings::IniFormat);
            const QString name = ini.value(QStringLiteral("Sound Theme/Name"), d).toString();
            if (ini.value(QStringLiteral("Sound Theme/Hidden")).toBool()) {
                continue;
            }
            seen.insert(d);
            list.append({d, themeTitle(d, name), QDir(base).canonicalPath() == QDir(userDir).canonicalPath()});
        }
    }
    QCollator c(QLocale{});
    std::sort(list.begin(), list.end(), [&c](const Theme &a, const Theme &b) {
        return c.compare(a.name, b.name) < 0;
    });
    return list;
}

/** The file of a sound name in a theme, following Inherits and falling back to freedesktop */
inline QString soundFile(const QString &name, const QString &theme, int depth = 0)
{
    if (name.startsWith(QLatin1Char('/'))) {
        return name;
    }
    if (depth > 4) {
        return {};
    }
    const QString dir = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("sounds/") + theme, QStandardPaths::LocateDirectory);
    if (!dir.isEmpty()) {
        for (const QString &sub : {QStringLiteral("stereo"), QStringLiteral("og"), QString()}) {
            for (const QString &ext : {QStringLiteral(".wav"), QStringLiteral(".oga"), QStringLiteral(".ogg")}) {
                const QString f = dir + QLatin1Char('/') + sub + QLatin1Char('/') + name + ext;
                if (QFile::exists(f)) {
                    return QFileInfo(f).absoluteFilePath();
                }
            }
        }
        QSettings ini(dir + QStringLiteral("/index.theme"), QSettings::IniFormat);
        for (const QString &parent : ini.value(QStringLiteral("Sound Theme/Inherits")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString f = soundFile(name, parent.trimmed(), depth + 1);
            if (!f.isEmpty()) {
                return f;
            }
        }
    }
    return theme == QLatin1String("freedesktop") ? QString() : soundFile(name, QStringLiteral("freedesktop"), depth + 1);
}

/** Sound names the theme has files for */
inline QStringList themeSounds(const QString &theme)
{
    QStringList names;
    const QString dir = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("sounds/") + theme, QStandardPaths::LocateDirectory);
    for (const QString &sub : {QStringLiteral("stereo"), QStringLiteral("og")}) {
        for (const QFileInfo &fi : QDir(dir + QLatin1Char('/') + sub).entryInfoList({QStringLiteral("*.wav"), QStringLiteral("*.oga"), QStringLiteral("*.ogg")}, QDir::Files)) {
            if (!names.contains(fi.completeBaseName()) && fi.completeBaseName() != QLatin1String("theme-demo")) {
                names << fi.completeBaseName();
            }
        }
    }
    return names;
}

inline QString currentTheme()
{
    return KConfigGroup(KSharedConfig::openConfig(QStringLiteral("kdeglobals")), QStringLiteral("Sounds")).readEntry("Theme", QStringLiteral("freedesktop"));
}

struct EventState {
    QString sound; // name or path, empty = none
    QString action;
};

inline EventState eventState(const SoundEvent &e)
{
    KConfig defaults(QStringLiteral("knotifications6/") + e.app + QStringLiteral(".notifyrc"), KConfig::NoGlobals, QStandardPaths::GenericDataLocation);
    KConfig user(e.app + QStringLiteral(".notifyrc"), KConfig::NoGlobals);
    const KConfigGroup d(&defaults, QStringLiteral("Event/") + e.event);
    const KConfigGroup u(&user, QStringLiteral("Event/") + e.event);
    EventState s;
    s.action = u.readEntry("Action", d.readEntry("Action", QString()));
    const QString sound = u.readEntry("Sound", d.readEntry("Sound", QString()));
    s.sound = s.action.split(QLatin1Char('|')).contains(QStringLiteral("Sound")) ? sound : QString();
    if (s.sound.isEmpty()) {
        s.sound.clear();
    }
    return s;
}

inline void writeEvent(const SoundEvent &e, const QString &sound)
{
    KConfig defaults(QStringLiteral("knotifications6/") + e.app + QStringLiteral(".notifyrc"), KConfig::NoGlobals, QStandardPaths::GenericDataLocation);
    KConfig user(e.app + QStringLiteral(".notifyrc"), KConfig::NoGlobals);
    KConfigGroup u(&user, QStringLiteral("Event/") + e.event);
    const KConfigGroup d(&defaults, QStringLiteral("Event/") + e.event);
    QStringList actions = u.readEntry("Action", d.readEntry("Action", QString())).split(QLatin1Char('|'), Qt::SkipEmptyParts);
    actions.removeAll(QStringLiteral("Sound"));
    if (!sound.isEmpty()) {
        actions << QStringLiteral("Sound");
        u.writeEntry("Sound", sound);
    }
    u.writeEntry("Action", actions.join(QLatin1Char('|')));
    user.sync();
}

inline QString startupAutostart()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart/seven-startup-sound.desktop");
}

/** Plays a theme sound, like canberra would (used by the Windows startup melody at login) */
inline void playSound(const QString &name)
{
    const QString file = soundFile(name, currentTheme());
    if (!file.isEmpty()) {
        QProcess::startDetached(QStringLiteral("paplay"), {file});
    }
}

/** Explorer's "Start Navigation" click (the Control Panel is an Explorer window in Windows 7);
 *  ~/.config/seven-soundsrc [Explorer] Navigation= overrides it, an empty value turns it off */
inline void playNavigationSound()
{
    static QElapsedTimer last;
    if (last.isValid() && last.elapsed() < 250) {
        return;
    }
    last.start();
    QSettings own(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-soundsrc"), QSettings::IniFormat);
    const QString file = own.contains(QStringLiteral("Explorer/Navigation")) ? own.value(QStringLiteral("Explorer/Navigation")).toString()
                                                                            : soundFile(QStringLiteral("Windows Navigation Start"), currentTheme());
    if (!file.isEmpty() && QFile::exists(file)) {
        QProcess::startDetached(QStringLiteral("paplay"), {QStringLiteral("--property=media.role=event"), file});
    }
}

class SoundsTab : public QWidget
{
public:
    std::function<void()> changed;

    SoundsTab()
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(10, 10, 10, 10);
        auto *intro = new QLabel(tr7("A sound theme is a set of sounds applied to events in Windows and programs. You can select an existing scheme or save one you have modified."));
        intro->setWordWrap(true);
        v->addWidget(intro);
        v->addWidget(new QLabel(tr7("Sound Scheme:")));
        auto *row = new QHBoxLayout;
        m_scheme = new QComboBox;
        row->addWidget(m_scheme, 1);
        auto *saveAs = new QPushButton(tr7("Save As..."));
        m_delete = new QPushButton(tr7("Delete"));
        row->addWidget(saveAs);
        row->addWidget(m_delete);
        v->addLayout(row);
        auto *hint = new QLabel(tr7("To change sounds, click a program event in the following list and then select a sound to apply. You can save the changes as a new sound scheme."));
        hint->setWordWrap(true);
        v->addWidget(hint);
        v->addWidget(new QLabel(tr7("Program Events:")));
        m_tree = new QTreeWidget;
        m_tree->setHeaderHidden(true);
        m_tree->setIndentation(14);
        m_tree->setMinimumHeight(120);
        v->addWidget(m_tree, 1);
        m_startup = new QCheckBox(tr7("Play Windows Startup sound"));
        m_startup->setChecked(QFile::exists(startupAutostart()));
        v->addWidget(m_startup);
        auto *srow = new QHBoxLayout;
        srow->addWidget(new QLabel(tr7("Sounds:")));
        m_sound = new QComboBox;
        srow->addWidget(m_sound, 1);
        v->addLayout(srow);
        auto *brow = new QHBoxLayout;
        brow->addStretch(1);
        m_test = new QPushButton(QIcon::fromTheme(QStringLiteral("seven-snd-play")), tr7("Test", "sound"));
        auto *browse = new QPushButton(tr7("Browse..."));
        brow->addWidget(m_test);
        brow->addWidget(browse);
        v->addLayout(brow);

        m_theme = currentTheme();
        fillSchemes();
        loadEvents();
        connect(m_scheme, &QComboBox::currentIndexChanged, this, [this] {
            const QString t = m_scheme->currentData().toString();
            if (t.isEmpty()) { // "No sounds"
                for (auto it = m_sounds.begin(); it != m_sounds.end(); ++it) {
                    it.value().clear();
                }
            } else {
                m_theme = t;
                for (const SoundEvent &e : soundEvents()) { // back to what the scheme's events use
                    KConfig defaults(QStringLiteral("knotifications6/") + e.app + QStringLiteral(".notifyrc"), KConfig::NoGlobals, QStandardPaths::GenericDataLocation);
                    const KConfigGroup d(&defaults, QStringLiteral("Event/") + e.event);
                    m_sounds[e.title] = d.readEntry("Sound", QString());
                    if (e.event == QLatin1String("startkde") || e.event == QLatin1String("exitkde")) {
                        m_sounds[e.title] = d.readEntry("Sound", QString());
                    }
                }
            }
            m_schemeChanged = true;
            refreshTree();
            selectEvent();
            m_delete->setEnabled(isUserTheme(t));
            notify();
        });
        connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] {
            selectEvent();
        });
        connect(m_sound, &QComboBox::activated, this, [this] {
            QTreeWidgetItem *it = m_tree->currentItem();
            if (!it || it->data(0, Qt::UserRole).isNull()) {
                return;
            }
            m_sounds[it->text(0)] = m_sound->currentData().toString();
            refreshTree();
            notify();
        });
        connect(m_test, &QPushButton::clicked, this, [this] {
            const QString f = soundFile(m_sound->currentData().toString(), m_theme);
            if (!f.isEmpty()) {
                QProcess::startDetached(QStringLiteral("paplay"), {f});
            }
        });
        connect(browse, &QPushButton::clicked, this, [this] {
            QTreeWidgetItem *it = m_tree->currentItem();
            if (!it || it->data(0, Qt::UserRole).isNull()) {
                return;
            }
            const QString f = QFileDialog::getOpenFileName(this, tr7("Browse for new %1 sound").arg(it->text(0)),
                                                           QFileInfo(soundFile(m_sounds.value(it->text(0)), m_theme)).absolutePath(),
                                                           tr7("Sound files (*.wav *.ogg *.oga *.flac *.mp3)"));
            if (!f.isEmpty()) {
                m_sounds[it->text(0)] = f;
                selectEvent();
                refreshTree();
                notify();
            }
        });
        connect(m_startup, &QCheckBox::toggled, this, [this] {
            notify();
        });
        connect(saveAs, &QPushButton::clicked, this, [this] {
            saveScheme();
        });
        connect(m_delete, &QPushButton::clicked, this, [this] {
            deleteScheme();
        });
    }

    void apply()
    {
        if (!m_dirty) {
            return;
        }
        const QString t = m_scheme->currentData().toString();
        if (!t.isEmpty()) {
            KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("kdeglobals")), QStringLiteral("Sounds"));
            g.writeEntry("Theme", t, KConfig::Notify);
            g.sync();
        }
        for (const SoundEvent &e : soundEvents()) {
            writeEvent(e, m_sounds.value(e.title));
        }
        if (m_startup->isChecked() != QFile::exists(startupAutostart())) {
            if (m_startup->isChecked()) {
                QDir().mkpath(QFileInfo(startupAutostart()).absolutePath());
                QFile f(startupAutostart());
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Windows startup sound\nName[uk]=Мелодія запуску Windows\nExec=%1 --play-sound system-startup\nNoDisplay=true\nX-KDE-autostart-phase=2\n")
                                .arg(QCoreApplication::applicationFilePath())
                                .toUtf8());
                }
            } else {
                QFile::remove(startupAutostart());
            }
        }
        m_dirty = false;
        m_schemeChanged = false;
    }

private:
    void notify()
    {
        m_dirty = true;
        if (changed) {
            changed();
        }
    }

    bool isUserTheme(const QString &dir) const
    {
        for (const Theme &t : themes()) {
            if (t.dir == dir) {
                return t.user;
            }
        }
        return false;
    }

    void fillSchemes()
    {
        m_scheme->blockSignals(true);
        m_scheme->clear();
        m_scheme->addItem(tr7("(None)", "sound"), QString());
        for (const Theme &t : themes()) {
            m_scheme->addItem(t.name, t.dir);
        }
        m_scheme->setCurrentIndex(qMax(0, m_scheme->findData(m_theme)));
        m_scheme->setMaxVisibleItems(20);
        m_scheme->blockSignals(false);
        m_delete->setEnabled(isUserTheme(m_theme));
    }

    void loadEvents()
    {
        for (const SoundEvent &e : soundEvents()) {
            m_sounds[e.title] = eventState(e).sound;
        }
        refreshTree();
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
    }

    void refreshTree()
    {
        const QString current = m_tree->currentItem() ? m_tree->currentItem()->text(0) : QString();
        m_tree->clear();
        QHash<QString, QTreeWidgetItem *> groups;
        QTreeWidgetItem *select = nullptr;
        for (const SoundEvent &e : soundEvents()) {
            QTreeWidgetItem *g = groups.value(e.group);
            if (!g) {
                g = new QTreeWidgetItem(m_tree, {e.group});
                g->setIcon(0, QIcon::fromTheme(e.group == QLatin1String("Windows") ? QStringLiteral("seven-sound") : QStringLiteral("system-file-manager")));
                groups.insert(e.group, g);
            }
            auto *it = new QTreeWidgetItem(g, {e.title});
            it->setData(0, Qt::UserRole, e.event);
            if (!m_sounds.value(e.title).isEmpty()) {
                it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-tray-volume")));
            } else {
                QPixmap blank(16, 16);
                blank.fill(Qt::transparent);
                it->setIcon(0, QIcon(blank));
            }
            if (e.title == current) {
                select = it;
            }
        }
        m_tree->expandAll();
        if (select) {
            m_tree->setCurrentItem(select);
        }
    }

    void selectEvent()
    {
        QTreeWidgetItem *it = m_tree->currentItem();
        const bool isEvent = it && !it->data(0, Qt::UserRole).isNull();
        m_sound->setEnabled(isEvent);
        m_test->setEnabled(isEvent);
        m_sound->clear();
        m_sound->addItem(tr7("(None)"), QString());
        QList<QPair<QString, QString>> items;
        for (const QString &n : themeSounds(m_theme.isEmpty() ? QStringLiteral("freedesktop") : m_theme)) {
            items.append({soundLabel(n), n});
        }
        std::sort(items.begin(), items.end());
        for (const auto &i : items) {
            m_sound->addItem(i.first, i.second);
        }
        if (!isEvent) {
            return;
        }
        const QString cur = m_sounds.value(it->text(0));
        int idx = m_sound->findData(cur);
        if (idx < 0 && !cur.isEmpty()) {
            m_sound->addItem(cur.startsWith(QLatin1Char('/')) ? QFileInfo(cur).fileName() : soundLabel(cur), cur);
            idx = m_sound->count() - 1;
        }
        m_sound->setCurrentIndex(qMax(0, idx));
        m_test->setEnabled(!cur.isEmpty());
    }

    void saveScheme()
    {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr7("Save Scheme"), tr7("Save this sound scheme as:"), QLineEdit::Normal,
                                                   tr7("%1 (modified)").arg(m_scheme->currentText()), &ok)
                                 .trimmed();
        if (!ok || name.isEmpty()) {
            return;
        }
        const QString dirName = QString(name).replace(QLatin1Char('/'), QLatin1Char('-'));
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/sounds/") + dirName;
        QDir().mkpath(dir + QStringLiteral("/stereo"));
        QFile index(dir + QStringLiteral("/index.theme"));
        if (index.open(QIODevice::WriteOnly)) {
            index.write(QStringLiteral("[Sound Theme]\nName=%1\nInherits=%2\nDirectories=stereo\n\n[stereo]\nOutputProfile=stereo\n").arg(name, m_theme).toUtf8());
        }
        // the event sounds of this scheme, as files of the new theme
        for (const SoundEvent &e : soundEvents()) {
            KConfig defaults(QStringLiteral("knotifications6/") + e.app + QStringLiteral(".notifyrc"), KConfig::NoGlobals, QStandardPaths::GenericDataLocation);
            const QString defName = KConfigGroup(&defaults, QStringLiteral("Event/") + e.event).readEntry("Sound", QString());
            const QString chosen = m_sounds.value(e.title);
            if (defName.isEmpty() || chosen.isEmpty()) {
                continue;
            }
            const QString src = soundFile(chosen, m_theme);
            if (!src.isEmpty()) {
                const QString dst = dir + QStringLiteral("/stereo/") + defName + QLatin1Char('.') + QFileInfo(src).suffix();
                QFile::remove(dst);
                QFile::copy(src, dst);
                m_sounds[e.title] = defName;
            }
        }
        m_theme = dirName;
        fillSchemes();
        notify();
    }

    void deleteScheme()
    {
        const QString t = m_scheme->currentData().toString();
        if (!isUserTheme(t)) {
            return;
        }
        if (QMessageBox::question(this, tr7("Delete Scheme"), tr7("Delete the sound scheme \"%1\"?").arg(m_scheme->currentText())) != QMessageBox::Yes) {
            return;
        }
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/sounds/") + t).removeRecursively();
        m_theme = QStringLiteral("Windows 7");
        fillSchemes();
        notify();
    }

    QComboBox *m_scheme;
    QPushButton *m_delete;
    QTreeWidget *m_tree;
    QCheckBox *m_startup;
    QComboBox *m_sound;
    QPushButton *m_test;
    QString m_theme;
    QHash<QString, QString> m_sounds; // event title -> sound name / path
    bool m_dirty = false;
    bool m_schemeChanged = false;
};

/* ---------------- Text to Speech ---------------- */

inline QString speechdConf()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/speech-dispatcher/speechd.conf");
}

inline QString speechdValue(const QString &key)
{
    QFile f(speechdConf());
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    for (const QByteArray &line : f.readAll().split('\n')) {
        const QString l = QString::fromUtf8(line).trimmed();
        if (l.startsWith(key + QLatin1Char(' '))) {
            return l.mid(key.size()).trimmed().remove(QLatin1Char('"'));
        }
    }
    return {};
}

inline void setSpeechdValues(const QHash<QString, QString> &values)
{
    QStringList lines;
    QFile f(speechdConf());
    if (f.open(QIODevice::ReadOnly)) {
        lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
        f.close();
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        bool found = false;
        for (QString &l : lines) {
            if (l.trimmed().startsWith(it.key() + QLatin1Char(' '))) {
                l = it.key() + QLatin1Char(' ') + it.value();
                found = true;
            }
        }
        if (!found) {
            lines << it.key() + QLatin1Char(' ') + it.value();
        }
    }
    QDir().mkpath(QFileInfo(speechdConf()).absolutePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(lines.join(QLatin1Char('\n')).trimmed().toUtf8() + '\n');
    }
}

class SpeechProperties : public QDialog
{
public:
    explicit SpeechProperties(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr7("Speech Properties"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("text-speak"), QIcon::fromTheme(QStringLiteral("seven-sound"))));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setFixedWidth(470);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 9, 7, 9);
        auto *tabs = new QTabWidget;
        auto *page = new QWidget;
        auto *pv = new QVBoxLayout(page);
        pv->setContentsMargins(10, 10, 10, 10);
        auto *intro = new QLabel(tr7("You can control the voice properties, speed, and other options for text-to-speech translation."));
        intro->setWordWrap(true);
        pv->addWidget(intro);
        auto *voiceBox = new QGroupBox(tr7("Voice selection"));
        auto *vb = new QVBoxLayout(voiceBox);
        m_voice = new QComboBox;
        const QString module = speechdValue(QStringLiteral("DefaultModule"));
        const QString lang = speechdValue(QStringLiteral("DefaultLanguage"));
        const QString variant = speechdValue(QStringLiteral("DefaultSynthesisVoice"));
        for (const QString &line : sh(QStringLiteral("spd-say"), {QStringLiteral("-L")}, 6000).split(QLatin1Char('\n')).mid(1)) {
            const QStringList cols = line.simplified().split(QLatin1Char(' '));
            if (cols.size() < 3 || cols.at(2) != QLatin1String("none")) {
                continue; // the base voices only, as Windows lists them
            }
            m_voice->addItem(QStringLiteral("eSpeak %1 - %2").arg(cols.at(0), cols.at(1)), cols.at(1));
        }
        int i = m_voice->findData(lang.isEmpty() ? QStringLiteral("uk") : lang);
        m_voice->setCurrentIndex(qMax(0, i));
        m_voice->setMaxVisibleItems(20);
        vb->addWidget(m_voice);
        Q_UNUSED(module)
        Q_UNUSED(variant)
        vb->addWidget(new QLabel(tr7("Use the following text to preview the voice:")));
        m_sample = new QLineEdit;
        vb->addWidget(m_sample);
        auto *preview = new QPushButton(tr7("Preview Voice"));
        vb->addWidget(preview, 0, Qt::AlignRight);
        pv->addWidget(voiceBox);
        auto *rateBox = new QGroupBox(tr7("Voice speed"));
        auto *rb = new QGridLayout(rateBox);
        m_rate = new QSlider(Qt::Horizontal);
        m_rate->setRange(-100, 100);
        m_rate->setTickPosition(QSlider::TicksBelow);
        m_rate->setTickInterval(10);
        m_rate->setValue(speechdValue(QStringLiteral("DefaultRate")).toInt());
        rb->addWidget(m_rate, 0, 0, 1, 3);
        rb->addWidget(new QLabel(tr7("Slow", "voice")), 1, 0, Qt::AlignLeft);
        rb->addWidget(new QLabel(tr7("Medium", "slide show")), 1, 1, Qt::AlignHCenter);
        rb->addWidget(new QLabel(tr7("Fast", "voice")), 1, 2, Qt::AlignRight);
        pv->addWidget(rateBox);
        pv->addStretch(1);
        tabs->addTab(page, tr7("Text to Speech"));
        v->addWidget(tabs, 1);
        auto *btns = new QHBoxLayout;
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
        updateSample();
        setFixedHeight(sizeHint().height());
        connect(m_voice, &QComboBox::currentIndexChanged, this, [this] {
            updateSample();
            m_apply->setEnabled(true);
        });
        connect(m_rate, &QSlider::valueChanged, this, [this] {
            m_apply->setEnabled(true);
        });
        connect(preview, &QPushButton::clicked, this, [this] {
            QProcess::startDetached(QStringLiteral("spd-say"), {QStringLiteral("-l"), m_voice->currentData().toString(), QStringLiteral("-r"), QString::number(m_rate->value()), m_sample->text()});
        });
        connect(ok, &QPushButton::clicked, this, [this] {
            apply();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] {
            apply();
        });
    }

private:
    void updateSample()
    {
        const QString lang = m_voice->currentData().toString();
        m_sample->setText(lang == QLatin1String("uk") ? tr7("You have selected %1 as the computer's default voice.").arg(m_voice->currentText())
                                                       : QStringLiteral("You have selected %1 as the computer's default voice.").arg(m_voice->currentText()));
    }

    void apply()
    {
        setSpeechdValues({{QStringLiteral("DefaultModule"), QStringLiteral("espeak-ng")},
                          {QStringLiteral("DefaultLanguage"), QStringLiteral("\"%1\"").arg(m_voice->currentData().toString())},
                          {QStringLiteral("DefaultRate"), QString::number(m_rate->value())}});
        QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-x"), QStringLiteral("speech-dispatcher")}); // it starts again with the new defaults
        m_apply->setEnabled(false);
    }

    QComboBox *m_voice;
    QLineEdit *m_sample;
    QSlider *m_rate;
    QPushButton *m_apply;
};

/* ---------------- Sound ---------------- */

class SoundDialog : public QDialog
{
public:
    std::function<void()> openSpeech; // Recording > Configure

    explicit SoundDialog(int tab, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr7("Sound"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-sound")));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_DeleteOnClose);
        setFixedSize(420, 520);
        m_showDisabled = store().value(QStringLiteral("View/ShowDisabled"), true).toBool();
        m_showUnplugged = store().value(QStringLiteral("View/ShowUnplugged"), true).toBool();
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 9, 7, 9);
        m_tabs = new QTabWidget;
        m_tabs->addTab(deviceTab(true), tr7("Playback"));
        m_tabs->addTab(deviceTab(false), tr7("Write"));
        m_sounds = new SoundsTab;
        m_tabs->addTab(m_sounds, tr7("Sounds"));
        m_tabs->setCurrentIndex(tab);
        v->addWidget(m_tabs, 1);
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
        m_sounds->changed = [this] {
            m_apply->setEnabled(true);
        };
        connect(ok, &QPushButton::clicked, this, [this] {
            m_sounds->apply();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] {
            m_sounds->apply();
            m_apply->setEnabled(false);
        });
        connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
            startMeters();
        });
        // devices come and go (Bluetooth, USB): refresh like Windows does
        m_refresh.setInterval(2500);
        connect(&m_refresh, &QTimer::timeout, this, [this] {
            reload(true);
            reload(false);
        });
        m_refresh.start();
        m_meterTick.setInterval(60);
        connect(&m_meterTick, &QTimer::timeout, this, [this] {
            QListWidget *list = currentList();
            if (!list) {
                return;
            }
            for (int i = 0; i < list->count(); ++i) {
                QListWidgetItem *it = list->item(i);
                Meter *m = m_meters.value(it->data(EndpointRole).value<Endpoint>().id());
                const double level = m ? m->level : 0;
                if (m) {
                    m->level *= 0.85;
                }
                it->setData(LevelRole, level);
            }
        });
        m_meterTick.start();
        startMeters();
    }

private:
    struct DeviceTab {
        QListWidget *list = nullptr;
        QPushButton *configure = nullptr;
        QToolButton *setDefault = nullptr;
        QPushButton *properties = nullptr;
        QString signature;
    };

    QListWidget *currentList() const
    {
        const int t = m_tabs ? m_tabs->currentIndex() : 0;
        return t < 2 ? m_dev[t].list : nullptr;
    }

    void startMeters()
    {
        qDeleteAll(m_meters);
        m_meters.clear();
        QListWidget *list = currentList();
        if (!list) {
            return;
        }
        for (int i = 0; i < list->count(); ++i) {
            const Endpoint e = list->item(i)->data(EndpointRole).value<Endpoint>();
            if (e.disabled || e.unplugged || e.node.isEmpty()) {
                continue;
            }
            m_meters.insert(e.id(), new Meter(e.output ? e.node + QStringLiteral(".monitor") : e.node, this));
        }
    }

    Endpoint selected(bool output) const
    {
        QListWidgetItem *it = m_dev[output ? 0 : 1].list->currentItem();
        return it ? it->data(EndpointRole).value<Endpoint>() : Endpoint();
    }

    void updateButtons(bool output)
    {
        DeviceTab &t = m_dev[output ? 0 : 1];
        const QListWidgetItem *it = t.list->currentItem();
        const Endpoint e = it ? it->data(EndpointRole).value<Endpoint>() : Endpoint();
        const bool usable = it && !e.disabled && !e.unplugged && !e.node.isEmpty();
        t.configure->setEnabled(output ? usable : true);
        t.setDefault->setEnabled(usable && !e.isDefault);
        t.properties->setEnabled(it != nullptr);
    }

    void reload(bool output, bool force = false)
    {
        DeviceTab &t = m_dev[output ? 0 : 1];
        const QList<Endpoint> list = endpoints(output, m_showDisabled, m_showUnplugged);
        QString sig;
        for (const Endpoint &e : list) {
            sig += e.id() + e.status() + e.shownName() + e.shownIcon() + QLatin1Char(';');
        }
        if (sig == t.signature && !force) {
            return;
        }
        t.signature = sig;
        const QString current = t.list->currentItem() ? t.list->currentItem()->data(EndpointRole).value<Endpoint>().id() : QString();
        t.list->clear();
        for (const Endpoint &e : list) {
            auto *it = new QListWidgetItem(t.list);
            it->setData(EndpointRole, QVariant::fromValue(e));
            if (e.id() == current) {
                t.list->setCurrentItem(it);
            }
        }
        if (list.isEmpty()) {
            auto *it = new QListWidgetItem(output ? tr7("No audio devices are installed") : tr7("No audio devices are installed"), t.list);
            it->setFlags(Qt::NoItemFlags);
        }
        updateButtons(output);
        if ((m_tabs ? m_tabs->currentIndex() : 0) == (output ? 0 : 1)) {
            startMeters();
        }
    }

    void openProperties(bool output)
    {
        const Endpoint e = selected(output);
        if (e.id().isEmpty()) {
            return;
        }
        PropertiesDialog dlg(e, this);
        dlg.changedOutside = [this, output] {
            reload(output, true);
        };
        dlg.exec();
        reload(output, true);
    }

    QWidget *deviceTab(bool output)
    {
        DeviceTab &t = m_dev[output ? 0 : 1];
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(10, 10, 10, 10);
        auto *intro = new QLabel(output ? tr7("Select a playback device below to modify its settings:")
                                        : tr7("Select a recording device below to modify its settings:"));
        intro->setWordWrap(true);
        v->addWidget(intro);
        t.list = new QListWidget;
        t.list->setItemDelegate(new EndpointDelegate(t.list));
        t.list->setMouseTracking(true);
        t.list->setContextMenuPolicy(Qt::CustomContextMenu);
        t.list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        t.list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        v->addWidget(t.list, 1);
        auto *row = new QHBoxLayout;
        t.configure = new QPushButton(tr7("Configure", "sound"));
        row->addWidget(t.configure);
        row->addStretch(1);
        t.setDefault = new QToolButton;
        t.setDefault->setText(tr7("Default"));
        t.setDefault->setPopupMode(QToolButton::MenuButtonPopup);
        t.setDefault->setMinimumWidth(130);
        auto *defMenu = new QMenu(t.setDefault);
        defMenu->addAction(tr7("Default Device"), this, [this, output] {
            setDefault(selected(output));
            reload(output, true);
        });
        defMenu->addAction(tr7("Default Communication Device"), this, [this, output] {
            setDefault(selected(output));
            reload(output, true);
        });
        t.setDefault->setMenu(defMenu);
        row->addWidget(t.setDefault);
        t.properties = new QPushButton(tr7("Properties"));
        row->addWidget(t.properties);
        v->addLayout(row);
        reload(output, true);
        connect(t.list, &QListWidget::currentItemChanged, this, [this, output] {
            updateButtons(output);
        });
        connect(t.list, &QListWidget::itemDoubleClicked, this, [this, output] {
            openProperties(output);
        });
        connect(t.setDefault, &QToolButton::clicked, this, [this, output] {
            setDefault(selected(output));
            reload(output, true);
        });
        connect(t.properties, &QPushButton::clicked, this, [this, output] {
            openProperties(output);
        });
        connect(t.configure, &QPushButton::clicked, this, [this, output] {
            if (!output) {
                if (openSpeech) {
                    openSpeech();
                }
                return;
            }
            SpeakerWizard wiz(selected(true), this);
            wiz.exec();
            reload(true, true);
        });
        connect(t.list, &QListWidget::customContextMenuRequested, this, [this, output](const QPoint &pos) {
            DeviceTab &tab = m_dev[output ? 0 : 1];
            QListWidgetItem *it = tab.list->itemAt(pos);
            if (it) {
                tab.list->setCurrentItem(it);
            }
            const Endpoint e = it ? it->data(EndpointRole).value<Endpoint>() : Endpoint();
            const bool usable = it && !e.disabled && !e.unplugged && !e.node.isEmpty();
            QMenu menu(this);
            if (output) {
                QAction *test = menu.addAction(tr7("Test"), this, [this, e] {
                    playTest(e.node, e.channels, -1, this);
                });
                test->setEnabled(usable);
            } else if (usable) {
                // Windows offers "Listen" through Properties; keep the same items as Playback
            }
            if (it && !e.node.endsWith(QLatin1String(".monitor"))) {
                if (e.disabled) {
                    menu.addAction(tr7("Turn on"), this, [this, e, output] {
                        setDeviceEnabled(e, true);
                        reload(output, true);
                    });
                } else if (!e.unplugged) {
                    menu.addAction(tr7("Disable"), this, [this, e, output] {
                        setDeviceEnabled(e, false);
                        reload(output, true);
                    });
                }
            }
            QAction *def = menu.addAction(tr7("Set as the default printer"), this, [this, e, output] {
                setDefault(e);
                reload(output, true);
            });
            def->setEnabled(usable && !e.isDefault);
            QAction *comm = menu.addAction(tr7("Use Default Communication Device"), this, [this, e, output] {
                setDefault(e);
                reload(output, true);
            });
            comm->setEnabled(usable && !e.isDefault);
            menu.addSeparator();
            QAction *sd = menu.addAction(tr7("Show Disabled Devices"));
            sd->setCheckable(true);
            sd->setChecked(m_showDisabled);
            connect(sd, &QAction::toggled, this, [this](bool on) {
                m_showDisabled = on;
                store().setValue(QStringLiteral("View/ShowDisabled"), on);
                reload(true, true);
                reload(false, true);
            });
            QAction *su = menu.addAction(tr7("Show Disconnected Devices"));
            su->setCheckable(true);
            su->setChecked(m_showUnplugged);
            connect(su, &QAction::toggled, this, [this](bool on) {
                m_showUnplugged = on;
                store().setValue(QStringLiteral("View/ShowUnplugged"), on);
                reload(true, true);
                reload(false, true);
            });
            if (it) {
                menu.addSeparator();
                QAction *props = menu.addAction(tr7("Properties"), this, [this, output] {
                    openProperties(output);
                });
                QFont f = props->font();
                f.setBold(true);
                props->setFont(f);
            }
            menu.exec(tab.list->viewport()->mapToGlobal(pos));
        });
        return page;
    }

    QTabWidget *m_tabs = nullptr;
    DeviceTab m_dev[2];
    SoundsTab *m_sounds;
    QPushButton *m_apply;
    bool m_showDisabled = true;
    bool m_showUnplugged = true;
    QTimer m_refresh;
    QTimer m_meterTick;
    QHash<QString, Meter *> m_meters;
};

/* ---------------- Speech Recognition (Control Panel page) ---------------- */

inline QWidget *buildSpeechPage(std::function<void()> openTts)
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("win7page"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *h = new QHBoxLayout(page);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    auto *side = new QWidget;
    side->setObjectName(QStringLiteral("win7side"));
    side->setAttribute(Qt::WA_StyledBackground, true);
    side->setStyleSheet(QStringLiteral("#win7side { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #f0f6fc, stop:1 #d9e7f5); }"));
    side->setFixedWidth(200);
    auto *sv = new QVBoxLayout(side);
    sv->setContentsMargins(18, 16, 12, 16);
    sv->setSpacing(10);
    auto *home = new Link(tr7("Control Panel Home"), [] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    });
    sv->addWidget(home);
    sv->addWidget(new Link(tr7("Text to Speech"), std::move(openTts)));
    sv->addStretch(1);
    auto *see = new QLabel(tr7("See also"));
    see->setStyleSheet(QStringLiteral("color: #6d6d6d;"));
    sv->addWidget(see);
    sv->addWidget(new Link(tr7("Ease of Access"), [] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--ease")});
    }));
    sv->addWidget(new Link(tr7("Audio Devices"), [] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--sound")});
    }));
    h->addWidget(side);
    auto *body = new QWidget;
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(24, 16, 24, 16);
    auto *title = new QLabel(tr7("Speech Recognition Options"));
    QFont tf = title->font();
    tf.setPixelSize(16);
    title->setFont(tf);
    title->setStyleSheet(QStringLiteral("color: #1e3287;"));
    bv->addWidget(title);
    bv->addSpacing(18);
    bv->addWidget(new QLabel(tr7("Windows Speech Recognition is not supported for this language")));
    bv->addStretch(1);
    h->addWidget(body, 1);
    return page;
}
}

