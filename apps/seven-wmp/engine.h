#pragma once
/*
 * Playback: QMediaPlayer + the "Now Playing" list (WMP's current playlist) with shuffle and repeat.
 */
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QFileInfo>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QRandomGenerator>
#include <QSettings>
#include <QUrl>

class Engine : public QObject
{
    Q_OBJECT
public:
    explicit Engine(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_player = new QMediaPlayer(this);
        m_audio = new QAudioOutput(this);
        m_player->setAudioOutput(m_audio);
        // the samples for the visualizations
        QAudioFormat fmt;
        fmt.setSampleFormat(QAudioFormat::Float);
        fmt.setChannelCount(1);
        fmt.setSampleRate(22050);
        m_buffers = new QAudioBufferOutput(fmt, this);
        m_player->setAudioBufferOutput(m_buffers);
        QSettings s;
        m_audio->setVolume(s.value(QStringLiteral("volume"), 0.5).toDouble());
        m_audio->setMuted(s.value(QStringLiteral("muted"), false).toBool());
        m_shuffle = s.value(QStringLiteral("shuffle"), false).toBool();
        m_repeat = s.value(QStringLiteral("repeat"), false).toBool();
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
            if (st == QMediaPlayer::EndOfMedia) {
                if (!advance(+1, false)) {
                    // end of the list: WMP stops and goes back to the first item
                    m_player->stop();
                    Q_EMIT changed();
                }
            }
            Q_EMIT changed();
        });
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, &Engine::changed);
        connect(m_player, &QMediaPlayer::metaDataChanged, this, &Engine::changed);
        connect(m_player, &QMediaPlayer::hasVideoChanged, this, &Engine::changed);
        connect(m_audio, &QAudioOutput::volumeChanged, this, &Engine::changed);
        connect(m_audio, &QAudioOutput::mutedChanged, this, &Engine::changed);
    }
    ~Engine() override
    {
        QSettings s;
        s.setValue(QStringLiteral("volume"), m_audio->volume());
        s.setValue(QStringLiteral("muted"), m_audio->isMuted());
        s.setValue(QStringLiteral("shuffle"), m_shuffle);
        s.setValue(QStringLiteral("repeat"), m_repeat);
    }

    QMediaPlayer *player() const
    {
        return m_player;
    }
    QAudioOutput *audio() const
    {
        return m_audio;
    }
    QAudioBufferOutput *buffers() const
    {
        return m_buffers;
    }

    const QList<QUrl> &list() const
    {
        return m_list;
    }
    int index() const
    {
        return m_index;
    }
    QUrl current() const
    {
        return m_index >= 0 && m_index < m_list.size() ? m_list[m_index] : QUrl();
    }

    /** replaces the Now Playing list and starts `start` */
    void playList(const QList<QUrl> &urls, int start = 0)
    {
        m_list = urls;
        m_played.clear();
        if (urls.isEmpty()) {
            m_index = -1;
            m_player->stop();
            m_player->setSource(QUrl());
            Q_EMIT listChanged();
            Q_EMIT changed();
            return;
        }
        Q_EMIT listChanged();
        go(qBound(0, start, int(urls.size()) - 1));
    }
    void enqueue(const QList<QUrl> &urls)
    {
        m_list += urls;
        Q_EMIT listChanged();
        if (m_index < 0) {
            go(0);
        }
    }
    void removeAt(int i)
    {
        if (i < 0 || i >= m_list.size()) {
            return;
        }
        m_list.removeAt(i);
        if (i < m_index) {
            --m_index;
        } else if (i == m_index) {
            if (m_list.isEmpty()) {
                m_index = -1;
                m_player->stop();
                m_player->setSource(QUrl());
            } else {
                go(qMin(i, int(m_list.size()) - 1), m_player->playbackState() == QMediaPlayer::PlayingState);
            }
        }
        Q_EMIT listChanged();
        Q_EMIT changed();
    }
    void clear()
    {
        playList({});
    }

    void go(int i, bool play = true)
    {
        if (i < 0 || i >= m_list.size()) {
            return;
        }
        m_index = i;
        m_played.insert(i);
        m_player->setSource(m_list[i]);
        if (play) {
            m_player->play();
        }
        Q_EMIT currentChanged();
        Q_EMIT changed();
    }

    void playPause()
    {
        if (m_index < 0) {
            if (!m_list.isEmpty()) {
                go(0);
            }
            return;
        }
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_player->pause();
        } else {
            m_player->play();
        }
    }
    void stop()
    {
        m_player->stop();
    }
    /** WMP: "Previous" restarts the item when more than a few seconds are played */
    void previous()
    {
        if (m_player->position() > 3000 || m_list.size() <= 1) {
            m_player->setPosition(0);
            return;
        }
        advance(-1, true);
    }
    void next()
    {
        advance(+1, true);
    }

    bool shuffle() const
    {
        return m_shuffle;
    }
    bool repeat() const
    {
        return m_repeat;
    }
    void setShuffle(bool on)
    {
        m_shuffle = on;
        m_played.clear();
        if (m_index >= 0) {
            m_played.insert(m_index);
        }
        Q_EMIT changed();
    }
    void setRepeat(bool on)
    {
        m_repeat = on;
        Q_EMIT changed();
    }

    /** title shown in Now Playing and the taskbar: the title tag, otherwise the file name without extension */
    QString title(int i = -1) const
    {
        if (i < 0) {
            i = m_index;
            const QString t = m_player->metaData().stringValue(QMediaMetaData::Title);
            if (!t.isEmpty()) {
                return t;
            }
        }
        if (i < 0 || i >= m_list.size()) {
            return {};
        }
        const QUrl u = m_list[i];
        return u.isLocalFile() ? QFileInfo(u.toLocalFile()).completeBaseName() : u.fileName();
    }

Q_SIGNALS:
    void changed();
    void listChanged();
    void currentChanged();

private:
    bool advance(int dir, bool user)
    {
        if (m_list.isEmpty()) {
            return false;
        }
        const bool playing = user ? m_player->playbackState() == QMediaPlayer::PlayingState : true;
        int n = -1;
        if (m_shuffle && m_list.size() > 1) {
            if (m_played.size() >= m_list.size()) {
                if (!m_repeat && !user) {
                    return false;
                }
                m_played.clear();
            }
            QList<int> left;
            for (int i = 0; i < m_list.size(); ++i) {
                if (!m_played.contains(i)) {
                    left << i;
                }
            }
            n = left.isEmpty() ? (m_index + 1) % m_list.size() : left[QRandomGenerator::global()->bounded(int(left.size()))];
        } else {
            n = m_index + dir;
            if (n >= m_list.size()) {
                if (!m_repeat && !user) {
                    m_index = 0;
                    m_player->setSource(m_list[0]);
                    Q_EMIT currentChanged();
                    return false;
                }
                n = 0;
            } else if (n < 0) {
                n = m_repeat ? int(m_list.size()) - 1 : 0;
            }
        }
        go(n, playing || !user);
        return true;
    }

    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QAudioBufferOutput *m_buffers;
    QList<QUrl> m_list;
    QSet<int> m_played;
    int m_index = -1;
    bool m_shuffle = false;
    bool m_repeat = false;
};
