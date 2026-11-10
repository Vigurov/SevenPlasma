#pragma once
#include "win7i18n.h"
/*
 * The WMP 12 playback controls: the grey pill with shuffle, repeat, stop, the blue Play orb between
 * Previous/Next, mute and volume, plus the seek bar above it. Two looks: Library (light) and Now Playing (black).
 * Positions are measured on the real Windows 7 WMP (relative to the centre of the Play orb).
 */
#include "engine.h"
#include "res.h"

#include <QMouseEvent>
#include <QTimer>
#include <QToolTip>
#include <QWidget>

class Transport : public QWidget
{
    Q_OBJECT
public:
    enum Mode { Library, NowPlaying };
    enum Part { None, Seek, Shuffle, Repeat, Stop, Prev, Play, Next, Mute, Volume, Switch, FullScreen };

    Transport(Engine *engine, Mode mode, QWidget *parent)
        : QWidget(parent)
        , m_engine(engine)
        , m_mode(mode)
    {
        setMouseTracking(true);
        setFixedHeight(mode == Library ? 59 : 61);
        connect(engine, &Engine::changed, this, qOverload<>(&QWidget::update));
        connect(engine->player(), &QMediaPlayer::positionChanged, this, qOverload<>(&QWidget::update));
        connect(engine->player(), &QMediaPlayer::durationChanged, this, qOverload<>(&QWidget::update));
        m_hold.setInterval(250);
        connect(&m_hold, &QTimer::timeout, this, [this] {
            // press and hold Previous/Next: rewind / fast forward
            m_seeking = true;
            auto *pl = m_engine->player();
            pl->setPosition(qBound<qint64>(0, pl->position() + (m_pressed == Prev ? -5000 : 5000), pl->duration()));
        });
    }

    void setStatus(const QString &s)
    {
        m_status = s;
        update();
    }

Q_SIGNALS:
    void switchClicked();
    void fullScreenClicked();

protected:
    // ---- geometry ----
    QPoint centre() const
    {
        return QPoint(width() / 2, m_mode == Library ? 35 : 35);
    }
    QRect partRect(Part part) const
    {
        const QPoint c = centre();
        const bool lib = m_mode == Library;
        switch (part) {
        case Seek:
            return lib ? QRect(12, 3, width() - 24, 6) : QRect(9, 4, width() - 18, 6);
        case Shuffle:
            return QRect(c.x() + (lib ? -182 : -169), c.y() - 12, 25, 25);
        case Repeat:
            return QRect(c.x() + (lib ? -151 : -138), c.y() - 12, 25, 25);
        case Stop:
            return QRect(c.x() + (lib ? -106 : -98), c.y() - 12, 25, 25);
        case Prev:
            return QRect(c.x() + (lib ? -70 : -67), c.y() - 12, lib ? 46 : 43, 25);
        case Play:
            return QRect(c.x() - 23, c.y() - 23, 46, 46);
        case Next:
            return QRect(c.x() + (lib ? 24 : 24), c.y() - 12, lib ? 46 : 43, 25);
        case Mute:
            return QRect(c.x() + (lib ? 84 : 73), c.y() - 12, 25, 25);
        case Volume:
            return QRect(c.x() + (lib ? 120 : 108), c.y() - 4, 68, 9);
        case Switch:
            return lib ? QRect(width() - 41, c.y() - 12, 25, 25) : QRect();
        case FullScreen:
            return lib ? QRect() : QRect(width() - 40, c.y() - 11, 25, 25);
        default:
            return {};
        }
    }
    Part partAt(const QPoint &p) const
    {
        for (Part part : {Seek, Shuffle, Repeat, Stop, Prev, Play, Next, Mute, Volume, Switch, FullScreen}) {
            QRect r = partRect(part);
            if (part == Volume) {
                r.adjust(-4, -4, 4, 4);
            }
            if (part == Seek) {
                r.adjust(0, -2, 0, 2);
            }
            if (part == Play) {
                if (QLineF(p, r.center()).length() <= 23) {
                    return Play;
                }
                continue;
            }
            if (r.contains(p)) {
                return part;
            }
        }
        return None;
    }

    // ---- painting ----
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const bool lib = m_mode == Library;
        if (!lib) {
            p.fillRect(rect(), Qt::black);
        }
        auto *pl = m_engine->player();
        const bool hasMedia = m_engine->index() >= 0;
        const bool playing = pl->playbackState() == QMediaPlayer::PlayingState;
        const QPoint c = centre();

        paintSeek(p);

        // the pill
        const QPixmap &pill = Res::px(lib ? 2164 : 2200);
        p.drawPixmap(c.x() - pill.width() / 2, c.y() - (lib ? 34 : 34), pill);

        // Stop/Previous/Play/Next cluster: normal picture, the hovered or pressed segment from the state pictures
        const QPoint cl = c + (lib ? QPoint(-108, -25) : QPoint(-100, -24));
        const QPixmap &normal = Res::px(lib ? 4153 : 4200);
        p.drawPixmap(cl, normal);
        const Part hot = m_pressed != None ? m_pressed : m_hover;
        if (hot == Stop || hot == Prev || hot == Play || hot == Next) {
            const bool down = m_pressed == hot && m_pressedInside;
            const QPixmap &state = Res::px(lib ? (down ? 4154 : 4155) : (down ? 4201 : 4202));
            const int b0 = lib ? 36 : 33, b1 = lib ? 84 : 76, b2 = lib ? 132 : 124;
            QRect seg;
            switch (hot) {
            case Stop:
                seg = QRect(0, 0, b0, normal.height());
                break;
            case Prev:
                seg = QRect(b0, 0, b1 - b0, normal.height());
                break;
            case Play:
                seg = QRect(b1, 0, b2 - b1, normal.height());
                break;
            default:
                seg = QRect(b2, 0, normal.width() - b2, normal.height());
            }
            if (hot == Play || hasMedia || hot == Stop) {
                p.drawPixmap(cl + seg.topLeft(), state, seg);
            }
        }

        // glyphs (white set 3xxxx in Now Playing)
        const int base = lib ? 0 : 30000;
        auto glyph = [&](Part part, int id, bool enabled = true) {
            const QPixmap &g = Res::px(base + id + (enabled ? 0 : 1));
            const QRect r = partRect(part);
            p.drawPixmap(r.x() + (r.width() - g.width()) / 2, r.y() + (r.height() - g.height()) / 2, g);
        };
        // shuffle/repeat: blue when on (and while hovered)
        glyph(Shuffle, (m_engine->shuffle() || m_hover == Shuffle) ? 4174 : 4175);
        glyph(Repeat, (m_engine->repeat() || m_hover == Repeat) ? 4176 : 4177);
        if (!lib) {
            // Now Playing has no blue state pictures: a soft glow instead
        }
        glyph(Stop, 4179, hasMedia && pl->playbackState() != QMediaPlayer::StoppedState);
        {
            // Now Playing: ◀◀ ▶▶ for video, ⏮ ⏭ otherwise (like WMP 12)
            const bool video = !lib && pl->hasVideo();
            const QPixmap &g = Res::px(base + (video ? 4170 : 4168) + (hasMedia ? 0 : 1));
            p.drawPixmap(c.x() + (lib ? -70 : -67), c.y() - 12, g);
            const QPixmap &n = Res::px(base + (video ? 4172 : 4166) + (hasMedia ? 0 : 1));
            p.drawPixmap(c.x() + (lib ? 18 : 14), c.y() - 12, n);
        }
        {
            const QPixmap &g = Res::px(base + (playing ? 4164 : 4162));
            p.drawPixmap(c.x() - 21, c.y() - 21, g);
        }
        // mute / volume level
        {
            const auto *a = m_engine->audio();
            const double v = a->volume();
            const int id = a->isMuted() ? 5244 : 5246;
            Q_UNUSED(v)
            const QPixmap &g = Res::px(base + id);
            const QRect r = partRect(Mute);
            p.drawPixmap(r.topLeft(), g);
        }
        paintVolume(p);

        if (lib) {
            // status text right of the pill, Switch to Now Playing at the right end
            if (!m_status.isEmpty()) {
                p.setPen(QColor(0x1e, 0x39, 0x5b));
                QFont f = font();
                p.setFont(f);
                const int x = c.x() + 228;
                p.drawText(QRect(x, c.y() - 10, width() - x - 60, 20), Qt::AlignVCenter | Qt::AlignLeft,
                           fontMetrics().elidedText(m_status, Qt::ElideRight, width() - x - 60));
            }
            p.drawPixmap(partRect(Switch).topLeft(), Res::px(4191));
        } else {
            // elapsed time left of the pill
            p.setPen(QColor(0xe6, 0xe6, 0xe6));
            const qint64 pos = (m_seekDrag ? m_seekValue : pl->position()) / 1000;
            const QString t = QStringLiteral("%1:%2").arg(pos / 60, 2, 10, QLatin1Char('0')).arg(pos % 60, 2, 10, QLatin1Char('0'));
            p.drawText(QRect(0, c.y() - 10, c.x() - 203, 20), Qt::AlignVCenter | Qt::AlignRight, t);
            p.drawPixmap(partRect(FullScreen).topLeft(), Res::px(4601));
        }
    }

    void paintSeek(QPainter &p)
    {
        // a 6 px groove (rows measured on WMP 12), the played part blue, the thumb only under the mouse
        const QRect r = partRect(Seek);
        const bool lib = m_mode == Library;
        static const QColor libGroove[6] = {QColor(231, 235, 244), QColor(157, 161, 166), QColor(228, 234, 242), QColor(0, 0, 0, 0), QColor(190, 195, 203), QColor(234, 239, 246)};
        static const QColor npGroove[6] = {QColor(64, 63, 69), QColor(25, 25, 27), QColor(1, 2, 4), QColor(1, 2, 4), QColor(17, 17, 20), QColor(77, 77, 80)};
        static const QColor libFill[3] = {QColor(168, 214, 246), QColor(70, 116, 180), QColor(151, 192, 244)};
        static const QColor npFill[3] = {QColor(127, 201, 250), QColor(1, 76, 182), QColor(97, 159, 242)};
        const QColor *groove = lib ? libGroove : npGroove;
        for (int i = 0; i < 6; ++i) {
            if (groove[i].alpha()) {
                p.fillRect(QRect(r.x() + 1, r.y() + i, r.width() - 2, 1), groove[i]);
            }
        }
        auto *pl = m_engine->player();
        const qint64 dur = pl->duration();
        const qint64 pos = m_seekDrag ? m_seekValue : pl->position();
        if (dur > 0) {
            const int w = int(double(r.width()) * pos / dur);
            const QColor *fill = lib ? libFill : npFill;
            for (int i = 0; i < 3; ++i) {
                p.fillRect(QRect(r.x() + 1, r.y() + (lib ? 1 : 2) + i, qMax(0, w - 1), 1), fill[i]);
            }
            if (m_hover == Seek || m_seekDrag) {
                const QPixmap &th = Res::px(m_seekDrag ? 450 : 449);
                p.drawPixmap(r.x() + w - th.width() / 2, r.y() + 3 - th.height() / 2, th);
            }
        }
    }

    void paintVolume(QPainter &p)
    {
        // a 5 px groove measured on WMP 12: light/dark/bg/grey/light rows; the played part blue
        const QRect r = partRect(Volume);
        const bool lib = m_mode == Library;
        const int y = r.center().y() - 2;
        static const QColor libGroove[5] = {QColor(224, 231, 241), QColor(162, 169, 176), QColor(0, 0, 0, 0), QColor(191, 197, 207), QColor(228, 234, 244)};
        static const QColor npGroove[5] = {QColor(91, 93, 96), QColor(46, 48, 67), QColor(59, 62, 64), QColor(68, 70, 77), QColor(102, 104, 106)};
        static const QColor libFill[3] = {QColor(168, 214, 246), QColor(70, 116, 180), QColor(151, 192, 244)};
        static const QColor npFill[3] = {QColor(101, 151, 190), QColor(20, 71, 144), QColor(87, 129, 188)};
        const QColor *groove = lib ? libGroove : npGroove;
        const QColor *fill = lib ? libFill : npFill;
        for (int i = 0; i < 5; ++i) {
            if (groove[i].alpha()) {
                p.fillRect(QRect(r.x() + (i == 0 || i == 4 ? 1 : 0), y + i, r.width() - (i == 0 || i == 4 ? 2 : 0), 1), groove[i]);
            }
        }
        const double v = m_engine->audio()->isMuted() ? 0.0 : m_engine->audio()->volume();
        const int x = r.x() + int(r.width() * v);
        for (int i = 0; i < 3; ++i) {
            p.fillRect(QRect(r.x(), y + i, x - r.x(), 1), fill[i]);
        }
        const QPixmap &th = Res::px(m_pressed == Volume ? 2114 : m_hover == Volume ? 2113 : 2112);
        p.drawPixmap(x - th.width() / 2, r.center().y() - th.height() / 2, th);
    }

    // ---- mouse ----
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const Part part = partAt(e->pos());
        if (m_pressed == Volume) {
            setVolumeAt(e->pos().x());
        } else if (m_seekDrag) {
            m_seekValue = seekValueAt(e->pos().x());
            update();
        }
        m_pressedInside = (part == m_pressed);
        if (part != m_hover) {
            m_hover = part;
            QToolTip::hideText();
            setToolTip(tip(part));
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = None;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        m_pressed = partAt(e->pos());
        m_pressedInside = true;
        m_seeking = false;
        if (m_pressed == Volume) {
            m_engine->audio()->setMuted(false);
            setVolumeAt(e->pos().x());
        } else if (m_pressed == Seek && m_engine->player()->duration() > 0) {
            m_seekDrag = true;
            m_seekValue = seekValueAt(e->pos().x());
        } else if (m_pressed == Prev || m_pressed == Next) {
            m_hold.start();
        }
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        m_hold.stop();
        const Part part = partAt(e->pos());
        const Part pressed = m_pressed;
        m_pressed = None;
        if (m_seekDrag) {
            m_seekDrag = false;
            m_engine->player()->setPosition(m_seekValue);
        } else if (part == pressed && !m_seeking) {
            click(part);
        }
        m_seeking = false;
        update();
    }
    void wheelEvent(QWheelEvent *e) override
    {
        // the wheel over the controls changes the volume
        auto *a = m_engine->audio();
        a->setVolume(qBound(0.0, a->volume() + (e->angleDelta().y() > 0 ? 0.05 : -0.05), 1.0));
    }

    void click(Part part)
    {
        switch (part) {
        case Shuffle:
            m_engine->setShuffle(!m_engine->shuffle());
            break;
        case Repeat:
            m_engine->setRepeat(!m_engine->repeat());
            break;
        case Stop:
            m_engine->stop();
            break;
        case Prev:
            m_engine->previous();
            break;
        case Play:
            m_engine->playPause();
            break;
        case Next:
            m_engine->next();
            break;
        case Mute:
            m_engine->audio()->setMuted(!m_engine->audio()->isMuted());
            break;
        case Switch:
            Q_EMIT switchClicked();
            break;
        case FullScreen:
            Q_EMIT fullScreenClicked();
            break;
        default:
            break;
        }
        setToolTip(tip(m_hover));
    }

    QString tip(Part part) const
    {
        const bool playing = m_engine->player()->playbackState() == QMediaPlayer::PlayingState;
        switch (part) {
        case Seek:
            return tr7("Search");
        case Shuffle:
            return m_engine->shuffle() ? tr7("Turn shuffle off (Ctrl+H)") : tr7("Turn shuffle on (Ctrl+H)");
        case Repeat:
            return m_engine->repeat() ? tr7("Turn repeat off (Ctrl+T)") : tr7("Turn repeat on (Ctrl+T)");
        case Stop:
            return tr7("Stop (Ctrl+S)");
        case Prev:
            return m_mode == Library ? tr7("Previous (Ctrl+B)") : tr7("Previous (press and hold to rewind)");
        case Play:
            return playing ? tr7("Pause (Ctrl+P)") : tr7("Play (Ctrl+P)");
        case Next:
            return m_mode == Library ? tr7("Next (Ctrl+F)") : tr7("Next (press and hold to fast-forward)");
        case Mute:
            return m_engine->audio()->isMuted() ? tr7("Unmute (F7)") : tr7("Mute (F7)");
        case Volume:
            return tr7("Volume");
        case Switch:
            return tr7("Switch to Now Playing");
        case FullScreen:
            return tr7("View full screen");
        default:
            return {};
        }
    }

private:
    void setVolumeAt(int x)
    {
        const QRect r = partRect(Volume);
        m_engine->audio()->setVolume(qBound(0.0, double(x - r.x()) / r.width(), 1.0));
    }
    qint64 seekValueAt(int x) const
    {
        const QRect r = partRect(Seek);
        return qint64(qBound(0.0, double(x - r.x()) / r.width(), 1.0) * m_engine->player()->duration());
    }

    Engine *m_engine;
    Mode m_mode;
    Part m_hover = None;
    Part m_pressed = None;
    bool m_pressedInside = false;
    bool m_seekDrag = false;
    bool m_seeking = false;
    qint64 m_seekValue = 0;
    QTimer m_hold;
    QString m_status;
};
