#pragma once
/*
 * Skin mode (View > Skin, Ctrl+2) with the "Revert" skin of Windows 7 (Revert.wmz, converted to
 * ~/.local/share/seven-wmp/skins/revert/*.png). The layout follows its netgen.wms: one 256x130 view, a button
 * group mapped by colours in player_map, a Pause group over Play, the seek slider, three metadata lines, the
 * video / visualization pane with the album art sliding in, a pop-up volume slider; the window fades to 40/255
 * while the mouse is away.
 */
#include "engine.h"
#include "library.h"
#include "res.h"

#include <QBitmap>
#include <QCloseEvent>
#include <QDir>
#include <QPainterPath>
#include <QMenu>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVideoWidget>
#include <QWidget>

class SkinWindow : public QWidget
{
    Q_OBJECT
public:
    SkinWindow(Engine *engine, Library *lib)
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::Window)
        , m_engine(engine)
        , m_lib(lib)
    {
        setWindowTitle(QStringLiteral("Програвач Windows Media"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-wmp")));
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        m_up = pic(QStringLiteral("player_up"));
        m_hover = pic(QStringLiteral("player_hover"));
        m_down = pic(QStringLiteral("player_down"));
        m_disabled = pic(QStringLiteral("player_disable"));
        m_map = pic(QStringLiteral("player_map")).toImage();
        m_pauseUp = pic(QStringLiteral("pause_up"));
        m_pauseHover = pic(QStringLiteral("pause_hover"));
        m_pauseDown = pic(QStringLiteral("pause_down"));
        m_pauseMap = pic(QStringLiteral("pause_colormap")).toImage();
        setFixedSize(m_up.size());
        // the clipping colour #FF00FF cuts the window shape
        setMask(QBitmap::fromImage(m_up.toImage().createMaskFromColor(qRgb(255, 0, 255), Qt::MaskInColor)));
        m_video = new QVideoWidget(this);
        m_video->setGeometry(3 + 158, 17, 92, 67);
        m_video->setAspectRatioMode(Qt::KeepAspectRatio);
        m_video->hide();
        connect(engine, &Engine::changed, this, [this] {
            const bool video = m_engine->player()->hasVideo();
            m_video->setVisible(video && isVisible());
            update();
        });
        connect(engine, &Engine::currentChanged, this, [this] {
            // the album art slides in for four seconds when the item changes
            m_artShown = true;
            m_artTimer.start(4000);
            update();
        });
        connect(engine->player(), &QMediaPlayer::positionChanged, this, qOverload<>(&QWidget::update));
        connect(engine->buffers(), &QAudioBufferOutput::audioBufferReceived, this, [this](const QAudioBuffer &buf) {
            if (!isVisible()) {
                return;
            }
            const float *d = buf.constData<float>();
            const int n = qMin<int>(int(buf.frameCount()), 256);
            m_wave.resize(n);
            for (int i = 0; i < n; ++i) {
                m_wave[i] = d[i];
            }
        });
        m_artTimer.setSingleShot(true);
        connect(&m_artTimer, &QTimer::timeout, this, [this] {
            m_artShown = false;
            update();
        });
        m_volTimer.setSingleShot(true);
        connect(&m_volTimer, &QTimer::timeout, this, [this] {
            if (m_pressed != Volume) {
                m_volumeShown = false;
                update();
            }
        });
        m_fade = new QPropertyAnimation(this, "windowOpacity", this);
        m_fade->setDuration(300);
        setWindowOpacity(1.0);
    }

    QVideoWidget *videoWidget() const
    {
        return m_video;
    }

Q_SIGNALS:
    void returnToLibrary();
    void menuWanted(const QPoint &global);
    void closeWanted();
    void eqWanted();

protected:
    enum Element { None, Quick, Return, Minimize, Close, Play, Stop, Prev, Next, Mute, VolumeBtn, Shuffle, Repeat, Eq, Playlist, Pause, Seek, Volume };

    static QPixmap pic(const QString &name)
    {
        return QPixmap(QDir::homePath() + QStringLiteral("/.local/share/seven-wmp/skins/revert/") + name + QStringLiteral(".png"));
    }
    Element elementAt(const QPoint &p) const
    {
        if (seekRect().adjusted(0, -2, 0, 2).contains(p) && m_engine->player()->duration() > 0) {
            return Seek;
        }
        if (m_volumeShown && QRect(35, 103, 84, 18).contains(p)) {
            return Volume;
        }
        if (playing() && QRect(14, 97, 32, 32).contains(p) && qRed(m_pauseMap.pixel(p - QPoint(14, 97))) == 255
            && qGreen(m_pauseMap.pixel(p - QPoint(14, 97))) == 255 && qBlue(m_pauseMap.pixel(p - QPoint(14, 97))) == 0) {
            return Pause;
        }
        if (!rect().contains(p)) {
            return None;
        }
        switch (m_map.pixel(p) & 0xffffff) {
        case 0x7F9DB9:
            return Quick;
        case 0x00CCFF:
            return Return;
        case 0x13920D:
            return Minimize;
        case 0xDE5C2F:
            return Close;
        case 0x00FF00:
            return Play;
        case 0xFF0000:
            return Stop;
        case 0x00FFFF:
            return Prev;
        case 0x0000FF:
            return Next;
        case 0xFFFF00:
            return Mute;
        case 0xFF9933:
            return VolumeBtn;
        case 0x39B54A:
            return Shuffle;
        case 0xF26522:
            return Repeat;
        case 0xA36209:
            return Eq;
        case 0xABA000:
            return Playlist;
        default:
            return None;
        }
    }
    QRgb colorOf(Element e) const
    {
        switch (e) {
        case Quick:
            return 0x7F9DB9;
        case Return:
            return 0x00CCFF;
        case Minimize:
            return 0x13920D;
        case Close:
            return 0xDE5C2F;
        case Play:
            return 0x00FF00;
        case Stop:
            return 0xFF0000;
        case Prev:
            return 0x00FFFF;
        case Next:
            return 0x0000FF;
        case Mute:
            return 0xFFFF00;
        case VolumeBtn:
            return 0xFF9933;
        case Shuffle:
            return 0x39B54A;
        case Repeat:
            return 0xF26522;
        case Eq:
            return 0xA36209;
        case Playlist:
            return 0xABA000;
        default:
            return 0;
        }
    }
    bool playing() const
    {
        return m_engine->player()->playbackState() == QMediaPlayer::PlayingState;
    }
    QRect seekRect() const
    {
        return QRect(7, 86, width() - 14, 13);
    }
    /** paints the pixels of one mapped element from a state picture */
    void paintElement(QPainter &p, Element e, const QPixmap &state)
    {
        const QRgb c = colorOf(e);
        auto it = m_masks.find(e);
        if (it == m_masks.end()) {
            QImage m = m_map.convertToFormat(QImage::Format_RGB32);
            QBitmap bm = QBitmap::fromImage(m.createMaskFromColor(qRgb(qRed(c), qGreen(c), qBlue(c)), Qt::MaskOutColor));
            it = m_masks.insert(e, bm);
        }
        p.save();
        p.setClipRegion(QRegion(*it));
        p.drawPixmap(0, 0, state);
        p.restore();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const bool hasMedia = m_engine->index() >= 0;
        p.drawPixmap(0, 0, m_up);
        // disabled transport when nothing is loaded
        if (!hasMedia) {
            for (Element e : {Play, Stop, Prev, Next}) {
                paintElement(p, e, m_disabled);
            }
        }
        // sticky buttons drawn down while on
        if (m_engine->shuffle()) {
            paintElement(p, Shuffle, m_down);
        }
        if (m_engine->repeat()) {
            paintElement(p, Repeat, m_down);
        }
        if (m_engine->audio()->isMuted()) {
            paintElement(p, Mute, m_down);
        }
        const Element hot = m_pressed != None ? m_pressed : m_hot;
        if (hot != None && hot != Seek && hot != Volume && hot != Pause && (hasMedia || (hot != Play && hot != Stop && hot != Prev && hot != Next))) {
            paintElement(p, hot, m_pressed == hot ? m_down : m_hover);
        }
        // Pause over Play while playing
        if (playing()) {
            QImage pi = (hot == Pause ? (m_pressed == Pause ? m_pauseDown : m_pauseHover) : m_pauseUp).toImage().convertToFormat(QImage::Format_ARGB32);
            const QImage pm = pi.createMaskFromColor(qRgb(255, 0, 255), Qt::MaskOutColor);
            pi.setAlphaChannel(pm.convertToFormat(QImage::Format_Grayscale8));
            p.drawImage(14, 97, pi);
        }
        // title
        QFont f = font();
        f.setPixelSize(10);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0x60, 0x64, 0x7F));
        p.drawText(QRect(19, 2, 150, 13), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Програвач Windows Media"));
        // metadata lines (artist / title / album)
        if (hasMedia) {
            QString artist, album;
            const QUrl u = m_engine->current();
            if (const MediaItem *m = u.isLocalFile() ? m_lib->find(u.toLocalFile()) : nullptr) {
                artist = m->artist;
                album = m->album;
            }
            p.setPen(QColor(0x89, 0xA4, 0xFF));
            QFont small = font();
            small.setPixelSize(10);
            QFont big = font();
            big.setPixelSize(13);
            p.setFont(small);
            p.drawText(QRect(6, 20, 153, 14), Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(artist, Qt::ElideRight, 153));
            p.setFont(big);
            p.drawText(QRect(6, 32, 153, 18), Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(m_engine->title(), Qt::ElideRight, 153));
            p.setFont(small);
            p.drawText(QRect(6, 50, 153, 14), Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(album, Qt::ElideRight, 153));
        }
        // visualization pane (music): a small scope; album art for 4 s after a change
        if (hasMedia && !m_engine->player()->hasVideo()) {
            const QRect vis(3 + 161, 17, 89, 67);
            p.save();
            p.setClipRect(vis);
            if (!m_wave.isEmpty() && playing()) {
                p.setRenderHint(QPainter::Antialiasing);
                QPainterPath path;
                for (int i = 0; i < m_wave.size(); ++i) {
                    const double x = vis.x() + double(i) / (m_wave.size() - 1) * vis.width();
                    const double y = vis.center().y() - m_wave[i] * vis.height() * 0.45;
                    i ? path.lineTo(x, y) : path.moveTo(x, y);
                }
                p.setPen(QPen(QColor(0x5f, 0xd0, 0x5f), 1));
                p.drawPath(path);
            }
            if (m_artShown && m_engine->current().isLocalFile()) {
                const QImage art = Library::albumArt(m_engine->current().toLocalFile());
                if (!art.isNull()) {
                    p.setOpacity(200 / 255.0);
                    p.drawImage(QRect(3 + 175, 17 + 5, 57, 57), art);
                }
            }
            p.restore();
        }
        // seek slider: the fill tile up to the position, the thumb
        auto *pl = m_engine->player();
        if (pl->duration() > 0) {
            const QRect r = seekRect();
            const qint64 pos = m_seekDrag ? m_seekValue : pl->position();
            const int w = int(double(r.width() - 17) * pos / pl->duration());
            const QPixmap fill = pic(QStringLiteral("seek_fill_tile"));
            p.drawPixmap(QRect(r.x(), r.y() + 2, w + 8, fill.height()), fill, QRect(0, 0, qMin(w + 8, fill.width()), fill.height()));
            const QPixmap th = pic(m_seekDrag ? QStringLiteral("eq_hslider_thumb_down") : m_hot == Seek ? QStringLiteral("eq_hslider_thumb_hover") : QStringLiteral("eq_hslider_thumb_up"));
            QImage ti = th.toImage().convertToFormat(QImage::Format_ARGB32);
            const QImage tm = ti.createMaskFromColor(qRgb(255, 0, 255), Qt::MaskOutColor);
            ti.setAlphaChannel(tm.convertToFormat(QImage::Format_Grayscale8));
            p.drawImage(r.x() + w, r.y() + 2, ti);
        }
        // pop-up volume slider
        if (m_volumeShown) {
            auto keyed = [](const QPixmap &pm) {
                QImage i = pm.toImage().convertToFormat(QImage::Format_ARGB32);
                QImage m = i.createMaskFromColor(qRgb(255, 0, 255), Qt::MaskOutColor);
                i.setAlphaChannel(m.convertToFormat(QImage::Format_Grayscale8));
                return i;
            };
            const QRect r(35, 103, 84, 18);
            p.drawImage(r.topLeft(), keyed(pic(QStringLiteral("volume_slider_off"))));
            const double v = m_engine->audio()->volume();
            const int x = 10 + int((r.width() - 20) * v);
            p.drawImage(r.topLeft(), keyed(pic(QStringLiteral("volume_slider_on"))), QRect(0, 0, x, 18));
            p.drawImage(r.x() + x - 4, r.y(), keyed(pic(m_pressed == Volume ? QStringLiteral("volume_thumb_down") : QStringLiteral("volume_thumb"))));
        }
    }

    void enterEvent(QEnterEvent *) override
    {
        fadeTo(1.0);
    }
    void leaveEvent(QEvent *) override
    {
        m_hot = None;
        update();
        if (!m_dragging) {
            fadeTo(40 / 255.0);
        }
    }
    void showEvent(QShowEvent *) override
    {
        m_video->setVisible(m_engine->player()->hasVideo());
        m_engine->player()->setVideoOutput(m_video);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPoint p = e->position().toPoint();
        if (m_dragging) {
            move(e->globalPosition().toPoint() - m_dragOffset);
            return;
        }
        if (m_pressed == Volume) {
            setVolumeAt(p.x());
            return;
        }
        if (m_seekDrag) {
            const QRect r = seekRect();
            m_seekValue = qint64(qBound(0.0, double(p.x() - r.x()) / r.width(), 1.0) * m_engine->player()->duration());
            update();
            return;
        }
        const Element e2 = elementAt(p);
        if (e2 != m_hot) {
            m_hot = e2;
            setToolTip(tip(e2));
            update();
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const QPoint p = e->position().toPoint();
        if (e->button() == Qt::RightButton) {
            Q_EMIT menuWanted(e->globalPosition().toPoint());
            return;
        }
        m_pressed = elementAt(p);
        if (m_pressed == None) {
            // drag the skin by its body
            m_dragging = true;
            m_dragOffset = e->globalPosition().toPoint() - pos();
            return;
        }
        if (m_pressed == Volume) {
            setVolumeAt(p.x());
        } else if (m_pressed == Seek) {
            m_seekDrag = true;
            const QRect r = seekRect();
            m_seekValue = qint64(qBound(0.0, double(p.x() - r.x()) / r.width(), 1.0) * m_engine->player()->duration());
        }
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (m_dragging) {
            m_dragging = false;
            return;
        }
        const Element released = elementAt(e->position().toPoint());
        const Element pressed = m_pressed;
        m_pressed = None;
        if (m_seekDrag) {
            m_seekDrag = false;
            m_engine->player()->setPosition(m_seekValue);
        } else if (pressed == Volume) {
            m_volTimer.start(2500);
        } else if (released == pressed) {
            activate(pressed, e->globalPosition().toPoint());
        }
        update();
    }
    void wheelEvent(QWheelEvent *e) override
    {
        auto *a = m_engine->audio();
        a->setVolume(qBound(0.0, a->volume() + (e->angleDelta().y() > 0 ? 0.05 : -0.05), 1.0));
    }

private:
    void activate(Element e, const QPoint &global)
    {
        switch (e) {
        case Quick:
            Q_EMIT menuWanted(global);
            break;
        case Return:
            Q_EMIT returnToLibrary();
            break;
        case Minimize:
            showMinimized();
            break;
        case Close:
            Q_EMIT closeWanted();
            break;
        case Play:
        case Pause:
            m_engine->playPause();
            break;
        case Stop:
            m_engine->stop();
            break;
        case Prev:
            m_engine->previous();
            break;
        case Next:
            m_engine->next();
            break;
        case Mute:
            m_engine->audio()->setMuted(!m_engine->audio()->isMuted());
            break;
        case VolumeBtn:
            m_volumeShown = !m_volumeShown;
            if (m_volumeShown) {
                m_volTimer.start(2500);
            }
            break;
        case Shuffle:
            m_engine->setShuffle(!m_engine->shuffle());
            break;
        case Repeat:
            m_engine->setRepeat(!m_engine->repeat());
            break;
        case Eq:
            Q_EMIT eqWanted();
            break;
        case Playlist:
            Q_EMIT returnToLibrary();
            break;
        default:
            break;
        }
        update();
    }
    void setVolumeAt(int x)
    {
        m_engine->audio()->setMuted(false);
        m_engine->audio()->setVolume(qBound(0.0, double(x - 35 - 10) / (84 - 20), 1.0));
        m_volTimer.start(2500);
        update();
    }
    void fadeTo(double o)
    {
        m_fade->stop();
        m_fade->setStartValue(windowOpacity());
        m_fade->setEndValue(o);
        m_fade->start();
    }
    QString tip(Element e) const
    {
        switch (e) {
        case Quick:
            return QStringLiteral("Швидкий доступ");
        case Return:
            return QStringLiteral("Перейти до повного режиму");
        case Minimize:
            return QStringLiteral("Згорнути");
        case Close:
            return QStringLiteral("Закрити");
        case Play:
            return QStringLiteral("Відтворити");
        case Pause:
            return QStringLiteral("Призупинити");
        case Stop:
            return QStringLiteral("Зупинити");
        case Prev:
            return QStringLiteral("Попередня");
        case Next:
            return QStringLiteral("Наступна");
        case Mute:
            return m_engine->audio()->isMuted() ? QStringLiteral("Увімкнути звук") : QStringLiteral("Вимкнути звук");
        case VolumeBtn:
            return QStringLiteral("Гучність");
        case Shuffle:
            return m_engine->shuffle() ? QStringLiteral("Вимкнути перемішування") : QStringLiteral("Увімкнути перемішування");
        case Repeat:
            return m_engine->repeat() ? QStringLiteral("Вимкнути повторення") : QStringLiteral("Увімкнути повторення");
        case Eq:
            return QStringLiteral("Графічний еквалайзер");
        case Playlist:
            return QStringLiteral("Показати список відтворення");
        case Seek:
            return QStringLiteral("Пошук");
        default:
            return {};
        }
    }

    Engine *m_engine;
    Library *m_lib;
    QPixmap m_up, m_hover, m_down, m_disabled, m_pauseUp, m_pauseHover, m_pauseDown;
    QImage m_map, m_pauseMap;
    QHash<int, QBitmap> m_masks;
    QVideoWidget *m_video;
    Element m_hot = None, m_pressed = None;
    bool m_dragging = false;
    QPoint m_dragOffset;
    bool m_seekDrag = false;
    qint64 m_seekValue = 0;
    bool m_volumeShown = false;
    bool m_artShown = false;
    QTimer m_artTimer, m_volTimer;
    QList<float> m_wave;
    QPropertyAnimation *m_fade;
};

/*
 * The "Corporate" skin of WMP 12 (built into wmploc.dll: bitmaps 1680-1682 and 1800-1845, layout read from the
 * running skin in Windows 7): a normal framed, resizable window with the menu bar, a grey/blue top strip with the
 * Captions / Full screen / Switch to Library buttons, the black screen and the blue control strip.
 */
#include <QMenuBar>
#include <QVBoxLayout>

class CorporateCanvas : public QWidget
{
    Q_OBJECT
public:
    CorporateCanvas(Engine *engine, QWidget *parent)
        : QWidget(parent)
        , m_engine(engine)
    {
        setMouseTracking(true);
        setMinimumSize(300, 140);
        m_video = new QVideoWidget(this);
        m_video->setAspectRatioMode(Qt::KeepAspectRatio);
        m_video->hide();
        connect(engine, &Engine::changed, this, [this] {
            m_video->setVisible(m_engine->player()->hasVideo());
            update();
        });
        connect(engine->player(), &QMediaPlayer::positionChanged, this, qOverload<>(&QWidget::update));
    }
    QVideoWidget *videoWidget() const
    {
        return m_video;
    }

Q_SIGNALS:
    void returnToLibrary();
    void fullScreenWanted();

protected:
    enum Part { None, Captions, Full, Library, Play, Stop, Prev, Next, Rew, Ffw, Mute, Seek, Volume };
    static QPixmap pic(int id)
    {
        static QHash<int, QPixmap> cache;
        auto it = cache.find(id);
        if (it == cache.end()) {
            it = cache.insert(id, QPixmap(QDir::homePath() + QStringLiteral("/.local/share/seven-wmp/skins/corporate/%1.png").arg(id)));
        }
        return *it;
    }
    QRect rectOf(Part p) const
    {
        const int w = width(), h = height();
        switch (p) {
        case Captions:
            return QRect(w - 77, 0, 16, 13);
        case Full:
            return QRect(w - 52, 0, 16, 13);
        case Library:
            return QRect(w - 27, 0, 16, 13);
        case Play:
            return QRect(2, h - 37, 30, 34);
        case Stop:
            return QRect(33, h - 31, 19, 28);
        case Prev:
            return QRect(55, h - 19, 21, 16);
        case Next:
            return QRect(76, h - 19, 22, 16);
        case Rew:
            return QRect(46, h - 37, 16, 13);
        case Ffw:
            return QRect(w - 21, h - 37, 16, 13);
        case Mute:
            return QRect(111, h - 19, 21, 16);
        case Seek:
            return QRect(62, h - 37, w - 21 - 62, 15);
        case Volume:
            return QRect(139, h - 19, 41, 15);
        default:
            return {};
        }
    }
    Part partAt(const QPoint &pt) const
    {
        for (Part p : {Captions, Full, Library, Play, Stop, Prev, Next, Rew, Ffw, Mute, Seek, Volume}) {
            if (rectOf(p).contains(pt)) {
                return p;
            }
        }
        return None;
    }
    QRect screenRect() const
    {
        return QRect(0, 13, width(), height() - 13 - 59 + 17);
    }
    void resizeEvent(QResizeEvent *) override
    {
        m_video->setGeometry(QRect(0, 13, width(), height() - 13 - 60));
    }
    /** picks the state picture: base = normal, +1 hover, +2 down, +3 disabled (when the set has it) */
    QPixmap state(int base, Part p, bool enabled, int states = 4) const
    {
        if (!enabled && states >= 4) {
            return pic(base + 3);
        }
        if (m_pressed == p && m_hot == p && states >= 3) {
            return pic(base + 2);
        }
        if (m_hot == p) {
            return pic(base + 1);
        }
        return pic(base);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int w = width(), h = height();
        auto *pl = m_engine->player();
        const bool has = m_engine->index() >= 0;
        const bool playing = pl->playbackState() == QMediaPlayer::PlayingState;
        // top strip: grey stretched + the blue part with the buttons at the right
        p.drawPixmap(QRect(0, 0, w - 100, 13), pic(1801));
        p.drawPixmap(w - 102, 0, pic(1803));
        p.drawPixmap(rectOf(Captions).topLeft(), state(1804, Captions, has));
        p.drawPixmap(rectOf(Full).topLeft(), state(1808, Full, has && pl->hasVideo()));
        p.drawPixmap(rectOf(Library).topLeft(), state(1812, Library, true, 3));
        // screen and the control strip (its top 17 rows are the black bottom of the screen)
        p.fillRect(screenRect(), Qt::black);
        p.drawPixmap(QRect(0, h - 59, w, 59), pic(1802));
        // seek: ◀◀ track ▶▶, thumb
        p.drawPixmap(rectOf(Rew).topLeft(), state(1832, Rew, has && playing));
        p.drawPixmap(rectOf(Ffw).topLeft(), state(1836, Ffw, has && playing));
        const QRect sr = rectOf(Seek);
        Res::hslice(p, sr, pic(1830), 8, 8);
        if (pl->duration() > 0) {
            const qint64 pos = m_drag == Seek ? m_dragValue : pl->position();
            const int x = sr.x() + int(double(sr.width() - 16) * pos / pl->duration());
            if (x > sr.x() + 2) {
                p.save();
                p.setClipRect(QRect(sr.x(), sr.y(), x - sr.x() + 8, sr.height()));
                Res::hslice(p, sr, pic(1831), 8, 8);
                p.restore();
            }
            p.drawPixmap(x, sr.y() - 1, pic(m_drag == Seek ? 1682 : m_hot == Seek ? 1681 : 1680));
        }
        // transport
        p.drawPixmap(rectOf(Play).topLeft(), playing ? state(1818, Play, true, 3) : state(1815, Play, has, 3));
        p.drawPixmap(rectOf(Stop).topLeft(), state(1821, Stop, has && pl->playbackState() != QMediaPlayer::StoppedState));
        {
            // previous / next share one picture mapped by 1826 (red = previous, yellow = next)
            const QRect pr(55, h - 19, 43, 16);
            p.drawPixmap(pr.topLeft(), has ? pic(1825) : pic(1829));
            if (has && (m_hot == Prev || m_hot == Next)) {
                const QRect half = m_hot == Prev ? QRect(0, 0, 21, 16) : QRect(21, 0, 22, 16);
                p.drawPixmap(pr.topLeft() + half.topLeft(), pic(m_pressed == m_hot ? 1828 : 1827), half);
            }
        }
        p.drawPixmap(rectOf(Mute).topLeft(), m_engine->audio()->isMuted() ? pic(1842) : state(1840, Mute, true, 2));
        // volume: blue groove, green up to the level, thumb
        const QRect vr = rectOf(Volume);
        p.drawPixmap(vr.topLeft(), pic(1843));
        const double v = m_engine->audio()->isMuted() ? 0 : m_engine->audio()->volume();
        const int vx = vr.x() + int((vr.width() - 16) * v);
        p.drawPixmap(vr.topLeft(), pic(1844), QRect(0, 0, vx - vr.x() + 8, 15));
        p.drawPixmap(vx, vr.y() - 2, pic(m_drag == Volume ? 1682 : m_hot == Volume ? 1681 : 1680));
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPoint pt = e->position().toPoint();
        if (m_drag == Seek) {
            const QRect sr = rectOf(Seek);
            m_dragValue = qint64(qBound(0.0, double(pt.x() - sr.x() - 8) / (sr.width() - 16), 1.0) * m_engine->player()->duration());
            update();
            return;
        }
        if (m_drag == Volume) {
            setVolumeAt(pt.x());
            return;
        }
        const Part pa = partAt(pt);
        if (pa != m_hot) {
            m_hot = pa;
            setToolTip(tip(pa));
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hot = None;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) {
            return;
        }
        m_pressed = partAt(e->position().toPoint());
        if (m_pressed == Seek && m_engine->player()->duration() > 0) {
            m_drag = Seek;
            mouseMoveEvent(e);
        } else if (m_pressed == Volume) {
            m_drag = Volume;
            m_engine->audio()->setMuted(false);
            setVolumeAt(e->position().toPoint().x());
        }
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        const Part released = partAt(e->position().toPoint());
        if (m_drag == Seek) {
            m_engine->player()->setPosition(m_dragValue);
        } else if (m_drag == None && released == m_pressed) {
            activate(released);
        }
        m_drag = None;
        m_pressed = None;
        update();
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (screenRect().contains(e->position().toPoint())) {
            Q_EMIT fullScreenWanted();
        }
    }
    void wheelEvent(QWheelEvent *e) override
    {
        auto *a = m_engine->audio();
        a->setVolume(qBound(0.0, a->volume() + (e->angleDelta().y() > 0 ? 0.05 : -0.05), 1.0));
    }

private:
    void activate(Part p)
    {
        auto *pl = m_engine->player();
        switch (p) {
        case Captions:
            pl->setActiveSubtitleTrack(pl->activeSubtitleTrack() >= 0 || pl->subtitleTracks().isEmpty() ? -1 : 0);
            break;
        case Full:
            Q_EMIT fullScreenWanted();
            break;
        case Library:
            Q_EMIT returnToLibrary();
            break;
        case Play:
            m_engine->playPause();
            break;
        case Stop:
            m_engine->stop();
            break;
        case Prev:
            m_engine->previous();
            break;
        case Next:
            m_engine->next();
            break;
        case Rew:
            pl->setPosition(qMax<qint64>(0, pl->position() - 10000));
            break;
        case Ffw:
            pl->setPosition(qMin(pl->duration(), pl->position() + 10000));
            break;
        case Mute:
            m_engine->audio()->setMuted(!m_engine->audio()->isMuted());
            break;
        default:
            break;
        }
    }
    void setVolumeAt(int x)
    {
        const QRect vr = rectOf(Volume);
        m_engine->audio()->setVolume(qBound(0.0, double(x - vr.x() - 8) / (vr.width() - 16), 1.0));
        update();
    }
    QString tip(Part p) const
    {
        switch (p) {
        case Captions:
            return QStringLiteral("Підписи");
        case Full:
            return QStringLiteral("Перегляд у повноекранному режимі");
        case Library:
            return QStringLiteral("Перейти до бібліотеки");
        case Play:
            return m_engine->player()->playbackState() == QMediaPlayer::PlayingState ? QStringLiteral("Призупинити") : QStringLiteral("Відтворити");
        case Stop:
            return QStringLiteral("Зупинити");
        case Prev:
            return QStringLiteral("Попередня");
        case Next:
            return QStringLiteral("Наступна");
        case Rew:
            return QStringLiteral("Перемотати назад");
        case Ffw:
            return QStringLiteral("Перемотати вперед");
        case Mute:
            return QStringLiteral("Вимкнути звук");
        case Seek:
            return QStringLiteral("Пошук");
        case Volume:
            return QStringLiteral("Гучність");
        default:
            return {};
        }
    }

    Engine *m_engine;
    QVideoWidget *m_video;
    Part m_hot = None, m_pressed = None, m_drag = None;
    qint64 m_dragValue = 0;
};

class CorporateWindow : public QWidget
{
    Q_OBJECT
public:
    CorporateWindow(Engine *engine, const QList<QMenu *> &menus)
        : QWidget(nullptr, Qt::Window)
    {
        setWindowTitle(QStringLiteral("Програвач Windows Media"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-wmp")));
        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        auto *mb = new QMenuBar(this);
        for (QMenu *m : menus) {
            mb->addMenu(m);
        }
        l->setMenuBar(mb);
        m_canvas = new CorporateCanvas(engine, this);
        l->addWidget(m_canvas, 1);
        connect(m_canvas, &CorporateCanvas::returnToLibrary, this, &CorporateWindow::returnToLibrary);
        connect(m_canvas, &CorporateCanvas::fullScreenWanted, this, [this] {
            isFullScreen() ? showNormal() : showFullScreen();
        });
        resize(300, 276);
    }
    QVideoWidget *videoWidget() const
    {
        return m_canvas->videoWidget();
    }
Q_SIGNALS:
    void returnToLibrary();
    void closing();

protected:
    void closeEvent(QCloseEvent *e) override
    {
        Q_EMIT closing();
        e->accept();
    }

private:
    CorporateCanvas *m_canvas;
};
