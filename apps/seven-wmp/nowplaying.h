#pragma once
#include "win7i18n.h"
/*
 * "Now Playing" mode: black window, the title in the top band, the video (or album art for music) and the
 * dark controls at the bottom. The bands keep their height; their contents disappear when the mouse leaves the
 * window (and after 3 s without movement in full screen), like WMP 12.
 */
#include "library.h"
#include "transport.h"

#include <QContextMenuEvent>
#include <QLabel>
#include <QMenu>
#include <QVBoxLayout>
#include <QSettings>
#include <QVideoWidget>
#include <QListWidget>
#include <QHBoxLayout>
#include <QPainterPath>
#include <cmath>

class NowPlayingHeader : public QWidget
{
    Q_OBJECT
public:
    NowPlayingHeader(Engine *engine, QWidget *parent)
        : QWidget(parent)
        , m_engine(engine)
    {
        setFixedHeight(55);
        setMouseTracking(true);
        connect(engine, &Engine::changed, this, qOverload<>(&QWidget::update));
    }
    void setShown(bool on)
    {
        m_shown = on;
        update();
    }
Q_SIGNALS:
    void switchClicked();

protected:
    QRect switchRect() const
    {
        return QRect(width() - 30, 3, 25, 25);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        if (!m_shown) {
            return;
        }
        p.setPen(QColor(0xf0, 0xf0, 0xf0));
        p.drawText(QRect(10, 6, width() - 60, 18), Qt::AlignLeft | Qt::AlignVCenter, m_engine->title());
        p.drawPixmap(switchRect().topLeft(), Res::px(4190));
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        setToolTip(switchRect().contains(e->pos()) ? tr7("Go to Library") : QString());
        QWidget::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (m_shown && switchRect().contains(e->pos())) {
            Q_EMIT switchClicked();
        }
    }

private:
    Engine *m_engine;
    bool m_shown = true;
};

/** shown instead of the video for music: the album art (or a visualization) on WMP's dark blue glow */
class ArtView : public QWidget
{
public:
    enum Viz { Art, Bars, Scope };
    ArtView(Engine *engine, QWidget *parent)
        : QWidget(parent)
        , m_engine(engine)
    {
        m_viz = Viz(QSettings().value(QStringLiteral("visualization"), int(Art)).toInt());
        connect(engine, &Engine::currentChanged, this, qOverload<>(&QWidget::update));
        connect(engine, &Engine::changed, this, qOverload<>(&QWidget::update));
        connect(engine->buffers(), &QAudioBufferOutput::audioBufferReceived, this, [this](const QAudioBuffer &buf) {
            if (m_viz == Art || !isVisible()) {
                return;
            }
            const float *d = buf.constData<float>();
            const int n = int(buf.frameCount());
            m_samples.resize(512);
            for (int i = 0; i < 512; ++i) {
                m_samples[i] = i < n ? d[i] : 0.f;
            }
            // 40 bands (log spaced) via a Goertzel pass each
            m_bands.resize(40);
            for (int b = 0; b < 40; ++b) {
                const double f = 40.0 * std::pow(10000.0 / 40.0, b / 39.0) / 22050.0;
                const double w = 2 * M_PI * f, c = 2 * std::cos(w);
                double s1 = 0, s2 = 0;
                for (int i = 0; i < 512; ++i) {
                    const double win = 0.5 - 0.5 * std::cos(2 * M_PI * i / 511);
                    const double s0 = m_samples[i] * win + c * s1 - s2;
                    s2 = s1;
                    s1 = s0;
                }
                const double mag = std::sqrt(qMax(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / 64.0;
                const double db = qBound(0.0, (20 * std::log10(mag + 1e-6) + 50) / 50.0, 1.0);
                // fall slowly like WMP's bars, peaks hang a little
                m_bands[b] = qMax(db, m_bands.value(b) - 0.04);
                if (m_peaks.size() < 40) {
                    m_peaks.resize(40);
                }
                m_peaks[b] = qMax(m_bands[b], m_peaks[b] - 0.012);
            }
            update();
        });
    }
    Viz viz() const
    {
        return m_viz;
    }
    void setViz(Viz v)
    {
        m_viz = v;
        QSettings().setValue(QStringLiteral("visualization"), int(v));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // WMP's dark blue glow (measured: ~rgb(21,61,97) in the middle band, black at the top and bottom)
        QLinearGradient v(0, 0, 0, height());
        v.setColorAt(0.0, QColor(0, 0, 0));
        v.setColorAt(0.2, QColor(8, 22, 33));
        v.setColorAt(0.48, QColor(19, 58, 92));
        v.setColorAt(0.75, QColor(11, 27, 47));
        v.setColorAt(1.0, QColor(1, 2, 4));
        p.fillRect(rect(), v);
        QLinearGradient h(0, 0, width(), 0);
        h.setColorAt(0.0, QColor(0, 0, 0, 120));
        h.setColorAt(0.6, QColor(0, 0, 0, 0));
        h.setColorAt(1.0, QColor(0, 0, 0, 60));
        p.fillRect(rect(), h);
        const QUrl u = m_engine->current();
        if (!u.isLocalFile() || m_engine->player()->playbackState() == QMediaPlayer::StoppedState) {
            return;
        }
        if (m_viz == Bars) {
            paintBars(p);
            return;
        }
        if (m_viz == Scope) {
            paintScope(p);
            return;
        }
        QImage art = Library::albumArt(u.toLocalFile());
        QPixmap pm = art.isNull() ? Res::px(QStringLiteral("album_0")) : QPixmap::fromImage(art);
        const int s = qMin(200, qMin(width(), height()) - 40);
        if (s <= 0) {
            return;
        }
        const QRect r(width() / 2 - s / 2, height() / 2 - s / 2, s, s);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(r, pm);
        // the reflection under the cover
        QImage refl = pm.toImage().scaled(s, s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).mirrored(false, true);
        refl = refl.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        {
            QPainter rp(&refl);
            rp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            QLinearGradient g(0, 0, 0, s / 3);
            g.setColorAt(0, QColor(0, 0, 0, 90));
            g.setColorAt(1, QColor(0, 0, 0, 0));
            rp.fillRect(refl.rect(), g);
        }
        p.drawImage(r.left(), r.bottom() + 2, refl, 0, 0, s, s / 3);
    }
    void paintBars(QPainter &p)
    {
        const int n = int(m_bands.size());
        if (!n) {
            return;
        }
        const int base = height() * 3 / 4;
        const double bw = double(width()) * 0.8 / n;
        const double x0 = width() * 0.1;
        for (int i = 0; i < n; ++i) {
            const int bh = int(m_bands[i] * height() * 0.6);
            const QRectF r(x0 + i * bw + 1, base - bh, bw - 2, bh);
            QLinearGradient g(0, base - height() * 0.6, 0, base);
            g.setColorAt(0, QColor(0x9c, 0xe4, 0xff));
            g.setColorAt(0.5, QColor(0x2f, 0x8f, 0xe8));
            g.setColorAt(1, QColor(0x0b, 0x3a, 0x8c));
            p.fillRect(r, g);
            const int py = base - int(m_peaks.value(i) * height() * 0.6) - 3;
            p.fillRect(QRectF(x0 + i * bw + 1, py, bw - 2, 2), QColor(0xd8, 0xf2, 0xff));
            // reflection
            p.fillRect(QRectF(r.x(), base + 2, r.width(), bh / 4.0), QColor(0x2f, 0x8f, 0xe8, 50));
        }
    }
    void paintScope(QPainter &p)
    {
        if (m_samples.isEmpty()) {
            return;
        }
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        const int mid = height() / 2;
        for (int i = 0; i < m_samples.size(); ++i) {
            const double x = double(i) / (m_samples.size() - 1) * width();
            const double y = mid - m_samples[i] * height() * 0.4;
            if (i == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }
        p.setPen(QPen(QColor(0x7f, 0xd4, 0xff), 2));
        p.drawPath(path);
    }

private:
    Engine *m_engine;
    Viz m_viz = Art;
    QList<float> m_samples;
    QList<double> m_bands, m_peaks;
};

class NowPlaying : public QWidget
{
    Q_OBJECT
public:
    NowPlaying(Engine *engine, QWidget *parent)
        : QWidget(parent)
        , m_engine(engine)
    {
        setMouseTracking(true);
        setAutoFillBackground(true);
        QPalette pal = palette();
        pal.setColor(QPalette::Window, Qt::black);
        setPalette(pal);
        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(0);
        m_header = new NowPlayingHeader(engine, this);
        l->addWidget(m_header);
        m_video = new QVideoWidget(this);
        m_video->setAspectRatioMode(Qt::KeepAspectRatio);
        m_video->setMouseTracking(true);
        m_art = new ArtView(engine, this);
        m_art->setMouseTracking(true);
        auto *mid = new QHBoxLayout;
        mid->setContentsMargins(0, 0, 0, 0);
        mid->setSpacing(0);
        mid->addWidget(m_video, 1);
        mid->addWidget(m_art, 1);
        // "Show list": the Now Playing list on the right, white text on the dark background
        m_list = new QListWidget(this);
        m_list->setFixedWidth(240);
        m_list->setFrameShape(QFrame::NoFrame);
        m_list->setStyleSheet(QStringLiteral("QListWidget { background: #0b1520; color: #e6e6e6; border-left: 1px solid #2a3a4a; }"
                                             "QListWidget::item { padding: 3px 8px; } QListWidget::item:selected { background: #1f4a78; }"));
        m_list->setVisible(QSettings().value(QStringLiteral("npList"), false).toBool());
        mid->addWidget(m_list);
        l->addLayout(mid, 1);
        connect(engine, &Engine::listChanged, this, &NowPlaying::refreshList);
        connect(engine, &Engine::currentChanged, this, &NowPlaying::refreshList);
        connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
            m_engine->go(m_list->row(it));
        });
        m_transport = new Transport(engine, Transport::NowPlaying, this);
        l->addWidget(m_transport);
        engine->player()->setVideoOutput(m_video);
        connect(engine, &Engine::changed, this, &NowPlaying::updateVideo);
        connect(m_header, &NowPlayingHeader::switchClicked, this, &NowPlaying::switchClicked);
        connect(m_transport, &Transport::fullScreenClicked, this, &NowPlaying::fullScreenClicked);
        m_idle.setSingleShot(true);
        m_idle.setInterval(3000);
        connect(&m_idle, &QTimer::timeout, this, [this] {
            if (window()->isFullScreen() && !m_menuOpen && QSettings().value(QStringLiteral("options/autoHide"), true).toBool()) {
                setControlsShown(false);
            }
        });
        for (QWidget *w : {static_cast<QWidget *>(m_video), static_cast<QWidget *>(m_art)}) {
            w->installEventFilter(this);
        }
        updateVideo();
    }

    Transport *transport() const
    {
        return m_transport;
    }
    QVideoWidget *videoWidget() const
    {
        return m_video;
    }
    ArtView *art() const
    {
        return m_art;
    }
    bool listShown() const
    {
        return m_list->isVisible();
    }
    void setListShown(bool on)
    {
        m_list->setVisible(on);
        QSettings().setValue(QStringLiteral("npList"), on);
        refreshList();
    }
    void activity()
    {
        setControlsShown(true);
        if (window()->isFullScreen()) {
            m_idle.start();
        }
    }
    void setControlsShown(bool on)
    {
        if (m_shown == on) {
            return;
        }
        m_shown = on;
        m_header->setShown(on);
        m_transport->setVisible(on || !window()->isFullScreen());
        m_header->setVisible(on || !window()->isFullScreen());
        if (!on) {
            m_transport->update();
        }
        if (window()->isFullScreen()) {
            setCursor(on ? Qt::ArrowCursor : Qt::BlankCursor);
        }
    }

Q_SIGNALS:
    void switchClicked();
    void fullScreenClicked();
    void contextMenuWanted(const QPoint &global);

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::MouseMove) {
            activity();
        } else if (e->type() == QEvent::MouseButtonDblClick) {
            Q_EMIT fullScreenClicked();
            return true;
        } else if (e->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent *>(e)->button() == Qt::LeftButton) {
        } else if (e->type() == QEvent::ContextMenu) {
            m_menuOpen = true;
            Q_EMIT contextMenuWanted(static_cast<QContextMenuEvent *>(e)->globalPos());
            m_menuOpen = false;
            return true;
        }
        return QWidget::eventFilter(o, e);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        activity();
        QWidget::mouseMoveEvent(e);
    }
    void enterEvent(QEnterEvent *) override
    {
        activity();
    }
    void leaveEvent(QEvent *) override
    {
        if (!window()->isFullScreen() && !m_menuOpen) {
            // the bands stay, only their contents fade out
            m_header->setShown(false);
            m_shown = false;
        }
    }

private:
    void refreshList()
    {
        if (!m_list->isVisible()) {
            return;
        }
        m_list->clear();
        for (int i = 0; i < m_engine->list().size(); ++i) {
            auto *it = new QListWidgetItem(m_engine->title(i == m_engine->index() ? -1 : i), m_list);
            if (i == m_engine->index()) {
                QFont f = it->font();
                f.setBold(true);
                it->setFont(f);
                it->setForeground(QColor(0x7f, 0xc4, 0xff));
            }
        }
    }
    void updateVideo()
    {
        const bool video = m_engine->player()->hasVideo();
        m_video->setVisible(video);
        m_art->setVisible(!video);
    }

    Engine *m_engine;
    NowPlayingHeader *m_header;
    QVideoWidget *m_video;
    ArtView *m_art;
    QListWidget *m_list;
    Transport *m_transport;
    QTimer m_idle;
    bool m_shown = true;
    bool m_menuOpen = false;
};
