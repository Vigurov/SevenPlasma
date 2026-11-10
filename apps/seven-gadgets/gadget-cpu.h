/*
 * CPU Meter: the two dials of CPU.Gadget (cpu.js), fed from /proc/stat and /proc/meminfo.
 * Included inside main.cpp's namespace.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

class CpuGadget : public Gadget
{
public:
    explicit CpuGadget(const QString &gadgetId)
        : Gadget(gadgetId)
    {
        sample();
        connect(&m_timer, &QTimer::timeout, this, [this] {
            sample();
            update();
        });
        m_timer.start(kRefresh);
    }

protected:
    static constexpr int kRefresh = 2000; // cpu.js REFRESH_INTERVAL is 3000; a little livelier here

    QSize bodySize() const override
    {
        return isBig() ? QSize(198, 159) : QSize(130, 103);
    }
    bool glassFrame() const override
    {
        return false;
    }
    bool hasSettingsButton() const override
    {
        return false;
    }
    void configure() override
    {
    }

    void paintBody(QPainter &p, const QRectF &) override
    {
        const bool big = isBig();
        const QString sfx = big ? QStringLiteral("_lrg") : QString();
        p.drawPixmap(0, 0, gadgetPixmap(QStringLiteral("cpu/back%1.png").arg(sfx)));
        // dockedState() / undockedState(): needle images and where they sit
        paintDial(p, gadgetPixmap(big ? QStringLiteral("cpu/dial_lrg.png") : QStringLiteral("cpu/dial.png")), big ? QPointF(63, 34) : QPointF(38, 22), m_cpu);
        paintDial(p, gadgetPixmap(big ? QStringLiteral("cpu/dial_lrg_sml.png") : QStringLiteral("cpu/dial_sml.png")), big ? QPointF(137, 16) : QPointF(92, 8), m_mem);
        p.drawPixmap(0, 0, gadgetPixmap(QStringLiteral("cpu/dialdot%1.png").arg(sfx)));

        // the read-outs under the dials: Calibri bold, white
        p.setFont(gadgetFont(big ? 12 : 10, true));
        const QString cpu = percentText(m_cpu);
        const QString mem = percentText(m_mem);
        const QFontMetrics fm(p.font());
        const QPointF cpuAt = big ? QPointF(56, 108) : QPointF(33, 76);
        const QPointF memAt = big ? QPointF(133, 66) : QPointF(89, 43);
        // centerPercent(): "100%" moves left by a few pixels
        const qreal cpuShift = cpu.size() > 3 ? 3 : 0;
        const qreal memShift = mem.size() > 3 ? (big ? 5 : 3) : 0;
        p.setPen(Qt::white);
        p.drawText(QPointF(cpuAt.x() - cpuShift, cpuAt.y() + fm.ascent()), cpu);
        p.drawText(QPointF(memAt.x() - memShift, memAt.y() + fm.ascent()), mem);
        p.drawPixmap(0, 0, gadgetPixmap(QStringLiteral("cpu/glass%1.png").arg(sfx)));
    }

private:
    /** The needle turns about its own centre: -125° at 0 %, +125° at 100 % (Rotation = pct * 2.5 - 125) */
    void paintDial(QPainter &p, const QPixmap &needle, QPointF topLeft, qreal percent)
    {
        const QPointF c = topLeft + QPointF(needle.width() / 2.0, needle.height() / 2.0);
        const qreal angle = percent * 2.5 - 125;
        // addShadow("grey", 2, 40, 2, 2)
        QImage shadow(needle.size(), QImage::Format_ARGB32_Premultiplied);
        shadow.fill(Qt::transparent);
        {
            QPainter sp(&shadow);
            sp.drawPixmap(0, 0, needle);
            sp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            sp.fillRect(shadow.rect(), QColor(128, 128, 128, 102));
        }
        for (const auto &[img, offset] : {std::pair<QPixmap, QPointF>{QPixmap::fromImage(shadow), QPointF(2, 2)}, {needle, QPointF(0, 0)}}) {
            p.save();
            p.translate(c + offset);
            p.rotate(angle);
            p.drawPixmap(QPointF(-needle.width() / 2.0, -needle.height() / 2.0), img);
            p.restore();
        }
    }

    static QString percentText(qreal v)
    {
        // numberFormat(): "00%", "07%", "42%", "100%"
        const int n = qBound(0, int(std::lround(v)), 100);
        return (n < 10 ? QStringLiteral("0") : QString()) + QString::number(n) + QLatin1Char('%');
    }

    void sample()
    {
        QFile stat(QStringLiteral("/proc/stat"));
        if (stat.open(QIODevice::ReadOnly)) {
            const QList<QByteArray> f = stat.readLine().simplified().split(' ');
            quint64 total = 0;
            for (int i = 1; i < f.size(); ++i) {
                total += f.at(i).toULongLong();
            }
            // idle + iowait
            const quint64 idle = f.value(4).toULongLong() + f.value(5).toULongLong();
            if (m_total && total > m_total) {
                m_cpu = 100.0 * (1.0 - double(idle - m_idle) / double(total - m_total));
            }
            m_total = total;
            m_idle = idle;
        }
        QFile info(QStringLiteral("/proc/meminfo"));
        if (info.open(QIODevice::ReadOnly)) {
            quint64 memTotal = 0;
            quint64 memAvail = 0;
            for (const QByteArray &line : info.readAll().split('\n')) {
                if (line.startsWith("MemTotal:")) {
                    memTotal = line.mid(9).simplified().split(' ').value(0).toULongLong();
                } else if (line.startsWith("MemAvailable:")) {
                    memAvail = line.mid(13).simplified().split(' ').value(0).toULongLong();
                }
            }
            if (memTotal > memAvail) {
                m_mem = 100.0 - 100.0 * double(memAvail) / double(memTotal);
            }
        }
        setToolTip(QStringLiteral("Використання ЦП: %1\nОперативна пам'ять (ОЗП): %2").arg(percentText(m_cpu), percentText(m_mem)));
    }

    qreal m_cpu = 0;
    qreal m_mem = 0;
    quint64 m_total = 0;
    quint64 m_idle = 0;
    QTimer m_timer;
};
