#pragma once
#include "win7i18n.h"
/*
 * Graphic equalizer (Now Playing > Enhancements > Graphic equalizer).
 * Sound: a PipeWire filter-chain (10 biquads: low shelf 31 Hz, peaking 62 Hz..8 kHz, high shelf 16 kHz) run as our
 * own child `pipewire -c`; its sink "seven_wmp_eq" becomes the player's audio output while the EQ is on and the gains
 * change live with `pw-cli set-param`. Window: the black WMP 12 enhancement pane with the pictures of wmploc.dll
 * (2136 background, 1793-1799 slider, 2120-2122 radios, 506/509/512 link modes).
 */
#include "engine.h"
#include "res.h"

#include <QDir>
#include <QPainterPath>

#include <QAudioDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QMediaDevices>
#include <QProcessEnvironment>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>

class Equalizer : public QObject
{
    Q_OBJECT
public:
    static constexpr int Bands = 10;

    explicit Equalizer(Engine *engine, QObject *parent = nullptr)
        : QObject(parent)
        , m_engine(engine)
    {
        QSettings s;
        m_on = s.value(QStringLiteral("eq/on"), false).toBool();
        m_preset = s.value(QStringLiteral("eq/preset"), tr7("Default")).toString();
        const QStringList g = s.value(QStringLiteral("eq/gains")).toStringList();
        for (int i = 0; i < Bands; ++i) {
            m_gains[i] = i < g.size() ? g[i].toDouble() : 0.0;
        }
        m_push.setSingleShot(true);
        m_push.setInterval(60);
        connect(&m_push, &QTimer::timeout, this, &Equalizer::pushGains);
        connect(engine->player(), &QMediaPlayer::playbackStateChanged, this, &Equalizer::streamsChanged);
        connect(engine, &Engine::currentChanged, this, &Equalizer::streamsChanged);
        connect(m_devices = new QMediaDevices(this), &QMediaDevices::audioOutputsChanged, this, &Equalizer::route);
        connect(m_devices, &QMediaDevices::audioOutputsChanged, this, [this] {
            QTimer::singleShot(500, this, &Equalizer::route);
        });
        if (m_on) {
            start();
        }
    }
    ~Equalizer() override
    {
        // the engine may already be gone here: only end our filter process
        save();
        if (m_proc) {
            m_proc->terminate();
            m_proc->waitForFinished(1000);
        }
    }

    static const QList<QPair<QString, QList<double>>> &presets()
    {
        // WMP's presets (dB, -12..+12)
        static const QList<QPair<QString, QList<double>>> p{
            {tr7("Default"), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
            {tr7("Rock"), {5, 3, 1, -1, -2, -1, 1, 3, 4, 5}},
            {tr7("Rap"), {6, 5, 2, 0, -1, -1, 1, 2, 3, 4}},
            {tr7("Grunge"), {4, 3, 0, -2, -1, 1, 3, 4, 3, 2}},
            {tr7("Metal"), {5, 4, 2, 0, -2, 0, 2, 4, 5, 5}},
            {tr7("Dance"), {7, 5, 2, 0, 0, -2, -1, 1, 3, 4}},
            {tr7("Country"), {2, 1, 0, 1, 2, 2, 2, 3, 3, 2}},
            {tr7("Jazz"), {3, 2, 1, 2, -1, -1, 0, 1, 2, 3}},
            {tr7("Speech"), {-4, -3, -1, 2, 4, 4, 3, 1, -1, -3}},
            {tr7("Classical"), {3, 2, 1, 0, 0, 0, -1, -1, 1, 3}},
            {tr7("Blues"), {3, 2, 1, 0, -1, 0, 1, 2, 2, 1}},
            {tr7("Oldies"), {-1, 1, 3, 4, 3, 2, 1, 0, -2, -4}},
            {tr7("Reggae"), {5, 4, 1, -1, -2, 0, 1, 3, 3, 2}},
            {tr7("Opera"), {-2, -1, 0, 2, 4, 4, 3, 2, 0, -1}},
            {tr7("Swing"), {2, 1, 0, 1, 2, 3, 2, 1, 1, 2}},
            {tr7("Acoustic"), {3, 2, 1, 1, 2, 2, 3, 3, 2, 1}},
            {tr7("New Age"), {3, 2, 0, -1, 0, 1, 2, 3, 3, 3}},
        };
        return p;
    }

    bool isOn() const
    {
        return m_on;
    }
    double gain(int i) const
    {
        return m_gains[i];
    }
    QString preset() const
    {
        return m_preset;
    }
    void setOn(bool on)
    {
        if (on == m_on) {
            return;
        }
        m_on = on;
        on ? start() : stop();
        save();
        Q_EMIT changed();
    }
    void setGain(int i, double g)
    {
        m_gains[i] = qBound(-12.0, g, 12.0);
        m_preset = tr7("Custom");
        m_push.start();
        Q_EMIT changed();
    }
    void applyPreset(const QString &name)
    {
        for (const auto &p : presets()) {
            if (p.first == name) {
                for (int i = 0; i < Bands; ++i) {
                    m_gains[i] = p.second[i];
                }
            }
        }
        m_preset = name;
        m_push.start();
        save();
        Q_EMIT changed();
    }
    void save() const
    {
        QSettings s;
        s.setValue(QStringLiteral("eq/on"), m_on);
        s.setValue(QStringLiteral("eq/preset"), m_preset);
        QStringList g;
        for (double v : m_gains) {
            g << QString::number(v);
        }
        s.setValue(QStringLiteral("eq/gains"), g);
    }

Q_SIGNALS:
    void changed();

private:
    QString confPath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/seven-wmp-eq.conf");
    }
    void start()
    {
        static const int freqs[Bands] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
        QString nodes, links;
        for (int i = 0; i < Bands; ++i) {
            const char *label = i == 0 ? "bq_lowshelf" : i == Bands - 1 ? "bq_highshelf" : "bq_peaking";
            nodes += QStringLiteral("{ type = builtin name = eq%1 label = %2 control = { \"Freq\" = %3 \"Q\" = 1.0 \"Gain\" = %4 } } ")
                         .arg(i + 1)
                         .arg(QLatin1String(label))
                         .arg(freqs[i])
                         .arg(m_gains[i], 0, 'f', 2);
            if (i > 0) {
                links += QStringLiteral("{ output = \"eq%1:Out\" input = \"eq%2:In\" } ").arg(i).arg(i + 1);
            }
        }
        QFile f(confPath());
        if (f.open(QIODevice::WriteOnly)) {
            f.write(tr7("context.properties = { log.level = 0 }\ncontext.spa-libs = { audio.convert.* = audioconvert/libspa-audioconvert support.* = support/libspa-support }\ncontext.modules = [\n  { name = libpipewire-module-rt flags = [ ifexists nofail ] }\n  { name = libpipewire-module-protocol-native }\n  { name = libpipewire-module-client-node }\n  { name = libpipewire-module-adapter }\n  { name = libpipewire-module-filter-chain args = {\n      node.description = \"Windows Media Player - equalizer\"\n      media.name = \"Windows Media Player - equalizer\"\n      filter.graph = { nodes = [ %1] links = [ %2] }\n      audio.channels = 2\n      audio.position = [ FL FR ]\n      capture.props = { node.name = \"seven_wmp_eq\" media.class = Audio/Sink }\n      playback.props = { node.name = \"seven_wmp_eq_out\" node.passive = true }\n  } }\n]\n")
                        .arg(nodes, links)
                        .toUtf8());
        }
        m_proc = new QProcess(this);
        m_proc->start(QStringLiteral("pipewire"), {QStringLiteral("-c"), confPath()});
        // the sink shows up in a moment; route() then moves the player to it
        QTimer::singleShot(800, this, &Equalizer::route);
        QTimer::singleShot(2000, this, &Equalizer::route);
    }
    void stop()
    {
        m_on = false;
        route();
        if (m_proc) {
            m_proc->terminate();
            if (!m_proc->waitForFinished(1000)) {
                m_proc->kill();
            }
            m_proc->deleteLater();
            m_proc = nullptr;
        }
        m_nodeId = -1;
    }
    /** moves our playback streams into the EQ sink (Qt only offers the hardware devices) */
    void route()
    {
        // the EQ sink while it is on, otherwise the system's default output (also after headphones connect)
        QAudioDevice want = QMediaDevices::defaultAudioOutput();
        if (m_on) {
            for (const QAudioDevice &d : QMediaDevices::audioOutputs()) {
                if (d.id() == "seven_wmp_eq") {
                    want = d;
                }
            }
        }
        if (!want.isNull() && m_engine->audio()->device().id() != want.id()) {
            m_engine->audio()->setDevice(want);
        }
    }
    void moveStreams(const QString &sink)
    {
        auto *p = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        p->setProcessEnvironment(env);
        connect(p, &QProcess::finished, this, [this, p, sink] {
            const auto arr = QJsonDocument::fromJson(p->readAllStandardOutput()).array();
            // Qt's PipeWire streams carry no process id; they are named after the application
            const QString name = QCoreApplication::applicationName();
            for (const auto &v : arr) {
                const auto o = v.toObject();
                const auto props = o[u"properties"].toObject();
                if (props[u"node.name"].toString() == name || props[u"application.process.id"].toString() == QString::number(QCoreApplication::applicationPid())) {
                    if (qEnvironmentVariableIsSet("WMP_DEBUG")) {
                        qWarning("move %d -> %s", o[u"index"].toInt(), qPrintable(sink));
                    }
                    QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("move-sink-input"), QString::number(o[u"index"].toInt()), sink});
                }
            }
            p->deleteLater();
        });
        p->start(QStringLiteral("pactl"), {QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"), QStringLiteral("sink-inputs")});
    }

public:
    /** call when playback (re)starts: new streams are created per item */
    void streamsChanged()
    {
        // the stream appears a little after playback starts
        for (int ms : {300, 1200, 3000}) {
            QTimer::singleShot(ms, this, &Equalizer::route);
        }
    }

private:
    int nodeId()
    {
        if (m_nodeId >= 0) {
            return m_nodeId;
        }
        QProcess p;
        p.start(QStringLiteral("pw-dump"), {});
        p.waitForFinished(3000);
        for (const auto &v : QJsonDocument::fromJson(p.readAllStandardOutput()).array()) {
            const auto o = v.toObject();
            if (o[u"info"].toObject()[u"props"].toObject()[u"node.name"].toString() == QLatin1String("seven_wmp_eq")) {
                m_nodeId = o[u"id"].toInt();
            }
        }
        return m_nodeId;
    }
    void pushGains()
    {
        if (!m_on || !m_proc) {
            return;
        }
        const int id = nodeId();
        if (id < 0) {
            return;
        }
        QString params;
        for (int i = 0; i < Bands; ++i) {
            params += QStringLiteral("\"eq%1:Gain\" %2 ").arg(i + 1).arg(m_gains[i], 0, 'f', 2);
        }
        QProcess::startDetached(QStringLiteral("pw-cli"), {QStringLiteral("set-param"), QString::number(id), QStringLiteral("Props"),
                                                           QStringLiteral("{ params = [ %1] }").arg(params)});
    }

    Engine *m_engine;
    QMediaDevices *m_devices = nullptr;
    QProcess *m_proc = nullptr;
    int m_nodeId = -1;
    bool m_on = false;
    double m_gains[Bands] = {};
    QString m_preset;
    QTimer m_push;
};

/** the black "Graphic equalizer" pane */
class EqWindow : public QWidget
{
    Q_OBJECT
public:
    EqWindow(Equalizer *eq, QWidget *parent)
        : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
        , m_eq(eq)
    {
        setFixedSize(448, 168);
        setMouseTracking(true);
        setWindowTitle(tr7("Graphic equalizer"));
        m_mode = QSettings().value(QStringLiteral("eq/link"), 1).toInt();
        connect(eq, &Equalizer::changed, this, qOverload<>(&QWidget::update));
    }

protected:
    static QPixmap pic(int id)
    {
        return QPixmap(QDir::homePath() + QStringLiteral("/.local/share/seven-wmp/eq/%1.png").arg(id));
    }
    static int sliderX(int i)
    {
        return 84 + 22 * i; // centres, measured on WMP 12
    }
    int yOf(double gain) const
    {
        // track from y 50 (+12 dB) to y 106 (-12 dB)
        return 78 - int(gain / 12.0 * 28);
    }
    QRect linkRect(int i) const
    {
        static const int xs[3] = {17, 81, 190};
        return QRect(xs[i], 28, i == 2 ? 120 : 56, 14);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setPen(QColor(0xb4, 0xb4, 0xb4));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        // title
        QFont b = font();
        b.setBold(true);
        p.setFont(b);
        p.setPen(Qt::white);
        p.drawText(QRect(50, 7, 300, 18), Qt::AlignLeft | Qt::AlignVCenter, tr7("Graphic equalizer"));
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(QRect(8, 7, 38, 19), Res::px(5350));
        // close button
        p.fillRect(QRect(width() - 22, 9, 13, 13), m_hot == 99 ? QColor(0xe0, 0x50, 0x40) : QColor(0xb0, 0x40, 0x30));
        p.setPen(Qt::white);
        p.drawLine(width() - 19, 12, width() - 13, 18);
        p.drawLine(width() - 13, 12, width() - 19, 18);
        // links
        QFont u = font();
        u.setUnderline(true);
        p.setFont(u);
        const QStringList links{m_eq->isOn() ? tr7("Turn off") : tr7("Turn on"), tr7("Reset"), m_eq->preset()};
        for (int i = 0; i < 3; ++i) {
            const QRect r = linkRect(i);
            p.setPen(m_hot == 50 + i ? QColor(0xd8, 0xec, 0xff) : QColor(0x8c, 0xc4, 0xff));
            if (i == 2) {
                QPainterPath a;
                a.moveTo(r.x(), r.center().y() - 1);
                a.lineTo(r.x() + 7, r.center().y() - 1);
                a.lineTo(r.x() + 3.5, r.center().y() + 3);
                a.closeSubpath();
                p.fillPath(a, QColor(0x8c, 0xc4, 0xff));
                p.drawText(r.adjusted(11, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, links[i]);
            } else {
                p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, links[i]);
            }
        }
        p.setFont(font());
        // link modes: radios + icons
        for (int i = 0; i < 3; ++i) {
            const int y = 52 + i * 20;
            p.drawPixmap(16, y, pic(m_mode == i ? 2121 : m_hot == 60 + i ? 2122 : 2120));
            p.drawPixmap(36, y - 2, pic(i == 0 ? 512 : i == 1 ? 509 : 506));
        }
        // background with the dashes, tracks, thumbs, labels
        p.drawPixmap(48, 48, pic(2136));
        const bool on = m_eq->isOn();
        static const QStringList labels{QStringLiteral("31"), QStringLiteral("62"), QStringLiteral("125"), QStringLiteral("250"), QStringLiteral("500"),
                                        tr7("1K"), tr7("2K"), tr7("4K"), tr7("8K"), tr7("16K")};
        QFont small = font();
        small.setPixelSize(10);
        for (int i = 0; i < Equalizer::Bands; ++i) {
            const int x = sliderX(i);
            p.fillRect(QRect(x - 1, 48, 3, 60), on ? QColor(0xe8, 0xe8, 0xe8) : QColor(0x70, 0x70, 0x70));
            const QPixmap thumb = pic(!on ? 1799 : m_drag == i ? 1798 : m_hot == i ? 1797 : 1799);
            p.setOpacity(on ? 1.0 : 0.5);
            p.drawPixmap(x - 8, yOf(m_eq->gain(i)) - 4, thumb);
            p.setOpacity(1.0);
            p.setFont(small);
            p.setPen(on ? QColor(0xdc, 0xe8, 0xf8) : QColor(0x80, 0x80, 0x80));
            p.drawText(QRect(x - 11, 110, 22, 14), Qt::AlignCenter, labels[i]);
        }
    }
    int hitTest(const QPoint &pt) const
    {
        if (QRect(width() - 22, 9, 13, 13).contains(pt)) {
            return 99;
        }
        for (int i = 0; i < 3; ++i) {
            if (linkRect(i).contains(pt)) {
                return 50 + i;
            }
            if (QRect(14, 50 + i * 20, 40, 18).contains(pt)) {
                return 60 + i;
            }
        }
        for (int i = 0; i < Equalizer::Bands; ++i) {
            if (QRect(sliderX(i) - 9, 44, 18, 68).contains(pt)) {
                return i;
            }
        }
        return -1;
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPoint pt = e->position().toPoint();
        if (m_drag >= 0) {
            setBand(m_drag, (78 - pt.y()) / 28.0 * 12.0);
            return;
        }
        if (m_moving) {
            move(e->globalPosition().toPoint() - m_moveOffset);
            return;
        }
        const int h = hitTest(pt);
        if (h != m_hot) {
            m_hot = h;
            setCursor(h >= 50 && h < 60 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const int h = hitTest(e->position().toPoint());
        if (h >= 0 && h < Equalizer::Bands && m_eq->isOn()) {
            m_drag = h;
            m_dragStart = m_eq->gain(h);
            mouseMoveEvent(e);
        } else if (h < 0) {
            m_moving = true;
            m_moveOffset = e->globalPosition().toPoint() - pos();
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (m_drag >= 0) {
            m_drag = -1;
            m_eq->save();
            update();
            return;
        }
        m_moving = false;
        const int h = hitTest(e->position().toPoint());
        if (h == 99) {
            hide();
        } else if (h == 50) {
            m_eq->setOn(!m_eq->isOn());
        } else if (h == 51) {
            m_eq->applyPreset(tr7("Default"));
        } else if (h == 52) {
            QMenu m(this);
            for (const auto &pr : Equalizer::presets()) {
                QAction *a = m.addAction(pr.first);
                a->setCheckable(true);
                a->setChecked(pr.first == m_eq->preset());
            }
            if (QAction *a = m.exec(mapToGlobal(linkRect(2).bottomLeft()))) {
                m_eq->setOn(true);
                m_eq->applyPreset(a->text());
            }
        } else if (h >= 60 && h < 63) {
            m_mode = h - 60;
            QSettings().setValue(QStringLiteral("eq/link"), m_mode);
            update();
        }
    }

private:
    void setBand(int i, double g)
    {
        g = qBound(-12.0, g, 12.0);
        const double delta = g - m_eq->gain(i);
        m_eq->setGain(i, g);
        // loosely / tightly linked: the neighbours follow
        if (m_mode != 0) {
            const double k1 = m_mode == 1 ? 0.5 : 0.75, k2 = m_mode == 1 ? 0.2 : 0.5;
            for (int d : {-2, -1, 1, 2}) {
                const int j = i + d;
                if (j >= 0 && j < Equalizer::Bands) {
                    m_eq->setGain(j, m_eq->gain(j) + delta * (qAbs(d) == 1 ? k1 : k2));
                }
            }
        }
    }

    Equalizer *m_eq;
    int m_mode = 1;
    int m_hot = -1;
    int m_drag = -1;
    double m_dragStart = 0;
    bool m_moving = false;
    QPoint m_moveOffset;
};
