#pragma once
/*
 * MPRIS 2 for the player: Plasma's taskbar shows Previous / Play-Pause / Next in the window's tooltip (our
 * counterpart of WMP's taskbar thumbnail toolbar), and the media keys / lock screen controls work.
 */
#include "engine.h"
#include "library.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QWidget>

class MprisRoot : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ yes)
    Q_PROPERTY(bool CanRaise READ yes)
    Q_PROPERTY(bool HasTrackList READ no)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ schemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ mimes)
public:
    MprisRoot(QObject *parent, QWidget *window)
        : QDBusAbstractAdaptor(parent)
        , m_window(window)
    {
    }
    bool yes() const
    {
        return true;
    }
    bool no() const
    {
        return false;
    }
    QString identity() const
    {
        return QStringLiteral("Програвач Windows Media");
    }
    QString desktopEntry() const
    {
        return QStringLiteral("seven-wmp");
    }
    QStringList schemes() const
    {
        return {QStringLiteral("file"), QStringLiteral("http"), QStringLiteral("https")};
    }
    QStringList mimes() const
    {
        return {QStringLiteral("audio/mpeg"), QStringLiteral("video/mp4")};
    }
public Q_SLOTS:
    void Raise()
    {
        m_window->showNormal();
        m_window->raise();
        m_window->activateWindow();
    }
    void Quit()
    {
        m_window->close();
    }

private:
    QWidget *m_window;
};

class MprisPlayer : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minRate)
    Q_PROPERTY(double MaximumRate READ maxRate)
    Q_PROPERTY(bool CanGoNext READ canControl)
    Q_PROPERTY(bool CanGoPrevious READ canControl)
    Q_PROPERTY(bool CanPlay READ canControl)
    Q_PROPERTY(bool CanPause READ canControl)
    Q_PROPERTY(bool CanSeek READ canControl)
    Q_PROPERTY(bool CanControl READ yes)
public:
    MprisPlayer(QObject *parent, Engine *engine, Library *lib)
        : QDBusAbstractAdaptor(parent)
        , m_engine(engine)
        , m_lib(lib)
    {
        connect(engine, &Engine::changed, this, &MprisPlayer::notify);
        connect(engine->player(), &QMediaPlayer::seekableChanged, this, &MprisPlayer::notify);
        connect(engine->player(), &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
            // a jump (not normal progress) is announced as Seeked
            if (qAbs(pos - m_lastPos) > 1500) {
                Q_EMIT Seeked(pos * 1000);
            }
            m_lastPos = pos;
        });
    }

    QString playbackStatus() const
    {
        switch (m_engine->player()->playbackState()) {
        case QMediaPlayer::PlayingState:
            return QStringLiteral("Playing");
        case QMediaPlayer::PausedState:
            return QStringLiteral("Paused");
        default:
            return QStringLiteral("Stopped");
        }
    }
    QString loopStatus() const
    {
        return m_engine->repeat() ? QStringLiteral("Playlist") : QStringLiteral("None");
    }
    void setLoopStatus(const QString &s)
    {
        m_engine->setRepeat(s != QLatin1String("None"));
    }
    double rate() const
    {
        return m_engine->player()->playbackRate();
    }
    void setRate(double r)
    {
        m_engine->player()->setPlaybackRate(r);
    }
    bool shuffle() const
    {
        return m_engine->shuffle();
    }
    void setShuffle(bool on)
    {
        m_engine->setShuffle(on);
    }
    double volume() const
    {
        return m_engine->audio()->volume();
    }
    void setVolume(double v)
    {
        m_engine->audio()->setVolume(qBound(0.0, v, 1.0));
    }
    qlonglong position() const
    {
        return m_engine->player()->position() * 1000;
    }
    double minRate() const
    {
        return 0.5;
    }
    double maxRate() const
    {
        return 2.0;
    }
    bool canControl() const
    {
        return !m_engine->list().isEmpty();
    }
    bool yes() const
    {
        return true;
    }
    QVariantMap metadata() const
    {
        QVariantMap m;
        const QUrl u = m_engine->current();
        if (u.isEmpty()) {
            return m;
        }
        m[QStringLiteral("mpris:trackid")] = QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/sevenplasma/wmp/track/%1").arg(m_engine->index())));
        m[QStringLiteral("xesam:url")] = u.toString();
        m[QStringLiteral("xesam:title")] = m_engine->title();
        const qint64 dur = m_engine->player()->duration();
        if (dur > 0) {
            m[QStringLiteral("mpris:length")] = qlonglong(dur * 1000);
        }
        if (const MediaItem *item = u.isLocalFile() ? m_lib->find(u.toLocalFile()) : nullptr) {
            if (!item->artist.isEmpty()) {
                m[QStringLiteral("xesam:artist")] = QStringList{item->artist};
            }
            if (!item->album.isEmpty()) {
                m[QStringLiteral("xesam:album")] = item->album;
            }
            const QString dir = QFileInfo(item->path).absolutePath();
            for (const QString &n : {QStringLiteral("cover.jpg"), QStringLiteral("folder.jpg"), QStringLiteral("Folder.jpg"), QStringLiteral("cover.png")}) {
                if (QFile::exists(dir + QLatin1Char('/') + n)) {
                    m[QStringLiteral("mpris:artUrl")] = QUrl::fromLocalFile(dir + QLatin1Char('/') + n).toString();
                    break;
                }
            }
        }
        return m;
    }

public Q_SLOTS:
    void Next()
    {
        m_engine->next();
    }
    void Previous()
    {
        m_engine->previous();
    }
    void Pause()
    {
        m_engine->player()->pause();
    }
    void PlayPause()
    {
        m_engine->playPause();
    }
    void Stop()
    {
        m_engine->stop();
    }
    void Play()
    {
        if (m_engine->player()->playbackState() != QMediaPlayer::PlayingState) {
            m_engine->playPause();
        }
    }
    void Seek(qlonglong offsetUs)
    {
        auto *p = m_engine->player();
        p->setPosition(qBound<qint64>(0, p->position() + offsetUs / 1000, p->duration()));
    }
    void SetPosition(const QDBusObjectPath &, qlonglong posUs)
    {
        m_engine->player()->setPosition(posUs / 1000);
    }
    void OpenUri(const QString &uri)
    {
        m_engine->playList({QUrl(uri)});
    }

Q_SIGNALS:
    void Seeked(qlonglong position);

private:
    void notify()
    {
        QVariantMap changed;
        changed[QStringLiteral("PlaybackStatus")] = playbackStatus();
        changed[QStringLiteral("LoopStatus")] = loopStatus();
        changed[QStringLiteral("Shuffle")] = shuffle();
        changed[QStringLiteral("Volume")] = volume();
        changed[QStringLiteral("Metadata")] = metadata();
        changed[QStringLiteral("CanGoNext")] = canControl();
        changed[QStringLiteral("CanGoPrevious")] = canControl();
        changed[QStringLiteral("CanPlay")] = canControl();
        changed[QStringLiteral("CanPause")] = canControl();
        changed[QStringLiteral("CanSeek")] = canControl();
        if (changed == m_last) {
            return;
        }
        m_last = changed;
        QDBusMessage msg = QDBusMessage::createSignal(QStringLiteral("/org/mpris/MediaPlayer2"), QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("PropertiesChanged"));
        msg << QStringLiteral("org.mpris.MediaPlayer2.Player") << changed << QStringList();
        QDBusConnection::sessionBus().send(msg);
    }

    Engine *m_engine;
    Library *m_lib;
    QVariantMap m_last;
    qint64 m_lastPos = 0;
};

inline void registerMpris(QObject *holder, QWidget *window, Engine *engine, Library *lib)
{
    new MprisRoot(holder, window);
    new MprisPlayer(holder, engine, lib);
    auto bus = QDBusConnection::sessionBus();
    bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), holder);
    bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.seven-wmp"));
}
