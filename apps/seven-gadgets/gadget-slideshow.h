#include "win7i18n.h"
/*
 * Slide Show: SlideShow.Gadget (slideShow.js / slideShow.css) — pictures from a folder in the glass frame,
 * with the prev / pause / next / view bar on hover. Included inside main.cpp's namespace.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

QString samplePicturesDir()
{
    return gadgetData(QStringLiteral("slideshow/Sample Pictures"));
}

QString picturesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
}

/** slideTimeArray */
const QList<QPair<QString, int>> &slideTimes()
{
    static const QList<QPair<QString, int>> times{
        {tr7("5 seconds"), 5},   {tr7("10 seconds"), 10}, {tr7("15 seconds"), 15}, {tr7("30 seconds"), 30},
        {tr7("1 minute"), 60}, {tr7("2 minutes"), 120}, {tr7("5 minutes"), 300},
    };
    return times;
}

enum class SlideEffect { None, Fade, Slide, Wipe, Iris, Count };

QString slideEffectName(int e)
{
    switch (SlideEffect(e)) {
    case SlideEffect::None:
        return tr7("None");
    case SlideEffect::Fade:
        return tr7("Fade");
    case SlideEffect::Slide:
        return tr7("Slide");
    case SlideEffect::Wipe:
        return tr7("Wipe");
    case SlideEffect::Iris:
        return tr7("Iris");
    default:
        return {};
    }
}

class SlideShowSettings : public GadgetDialog
{
public:
    SlideShowSettings(const QString &folder, bool subdirs, int seconds, int effect, bool shuffle)
        : GadgetDialog(tr7("Slide Show"), 340)
    {
        body->addWidget(new QLabel(tr7("Folder:")));
        auto *row = new QHBoxLayout;
        m_folder = new QComboBox;
        m_folder->addItem(tr7("Sample Pictures"), samplePicturesDir());
        m_folder->addItem(tr7("Pictures"), picturesDir());
        if (m_folder->findData(folder) < 0) {
            m_folder->addItem(QDir(folder).dirName(), folder);
        }
        m_folder->setCurrentIndex(qMax(0, m_folder->findData(folder)));
        auto *browse = new QPushButton(QStringLiteral("..."));
        browse->setFixedWidth(32);
        row->addWidget(m_folder, 1);
        row->addWidget(browse);
        body->addLayout(row);
        m_subdirs = new QCheckBox(tr7("Include subfolders"));
        m_subdirs->setChecked(subdirs);
        body->addWidget(m_subdirs);
        body->addSpacing(8);
        body->addWidget(new QLabel(tr7("Show each picture:")));
        m_time = new QComboBox;
        for (const auto &t : slideTimes()) {
            m_time->addItem(t.first, t.second);
        }
        m_time->setCurrentIndex(qMax(0, m_time->findData(seconds)));
        body->addWidget(m_time);
        body->addSpacing(8);
        body->addWidget(new QLabel(tr7("Transition between pictures:")));
        m_effect = new QComboBox;
        for (int e = 0; e < int(SlideEffect::Count); ++e) {
            m_effect->addItem(slideEffectName(e), e);
        }
        m_effect->setCurrentIndex(qBound(0, effect, int(SlideEffect::Count) - 1));
        body->addWidget(m_effect);
        body->addSpacing(8);
        m_shuffle = new QCheckBox(tr7("Shuffle pictures"));
        m_shuffle->setChecked(shuffle);
        body->addWidget(m_shuffle);
        body->addStretch(1);

        connect(browse, &QPushButton::clicked, this, [this] {
            const QString dir = QFileDialog::getExistingDirectory(this, tr7("Browse For Folder"), m_folder->currentData().toString());
            if (dir.isEmpty()) {
                return;
            }
            int i = m_folder->findData(dir);
            if (i < 0) {
                m_folder->addItem(QDir(dir).dirName(), dir);
                i = m_folder->count() - 1;
            }
            m_folder->setCurrentIndex(i);
        });
    }

    QString folder() const
    {
        return m_folder->currentData().toString();
    }
    bool subdirs() const
    {
        return m_subdirs->isChecked();
    }
    int seconds() const
    {
        return m_time->currentData().toInt();
    }
    int effect() const
    {
        return m_effect->currentData().toInt();
    }
    bool shuffle() const
    {
        return m_shuffle->isChecked();
    }

private:
    QComboBox *m_folder;
    QCheckBox *m_subdirs;
    QComboBox *m_time;
    QComboBox *m_effect;
    QCheckBox *m_shuffle;
};

class SlideShowGadget : public Gadget
{
public:
    explicit SlideShowGadget(const QString &gadgetId)
        : Gadget(gadgetId)
    {
        loadSettings();
        m_anim.setDuration(500);
        m_anim.setStartValue(0.0);
        m_anim.setEndValue(1.0);
        connect(&m_anim, &QVariantAnimation::valueChanged, this, [this] {
            update();
        });
        connect(&m_anim, &QVariantAnimation::finished, this, [this] {
            m_previous = QPixmap();
            update();
        });
        connect(&m_timer, &QTimer::timeout, this, [this] {
            showPicture(m_index + 1, true);
        });
        rescan();
    }

protected:
    QSize bodySize() const override
    {
        return isBig() ? QSize(360, 280) : QSize(130, 100);
    }
    bool glassFrame() const override
    {
        return false;
    }
    QRect pictureRect() const
    {
        // dockedState() / undockedState(): pictureFrame
        return isBig() ? QRect(17, 16, 320, 240) : QRect(4, 5, 120, 90);
    }
    QPoint barPos() const
    {
        return isBig() ? QPoint(119, 220) : QPoint(7, 68);
    }

    enum BarButton { Prev, PlayPause, Next, Reveal, NoButton };

    QRect buttonRect(BarButton b) const
    {
        // topBar at 4,3 inside the bar; every image has hspace=3
        const QPoint o = barPos() + QPoint(4, 3);
        switch (b) {
        case Prev:
            return QRect(o + QPoint(3, 0), QSize(20, 17));
        case PlayPause:
            return QRect(o + QPoint(29, 0), QSize(17, 17));
        case Next:
            return QRect(o + QPoint(52, 0), QSize(17, 17));
        case Reveal:
            return QRect(o + QPoint(81, 0), QSize(23, 17));
        default:
            return {};
        }
    }
    BarButton barButtonAt(QPointF pos) const
    {
        if (!underMouse()) {
            return NoButton;
        }
        for (BarButton b : {Prev, PlayPause, Next, Reveal}) {
            if (buttonRect(b).contains(pos.toPoint())) {
                return b;
            }
        }
        return NoButton;
    }

    void paintBody(QPainter &p, const QRectF &) override
    {
        const bool big = isBig();
        p.drawPixmap(0, 0, gadgetPixmap(big ? QStringLiteral("slideshow/on_desktop/slideshow_glass_frame.png") : QStringLiteral("slideshow/in_sidebar/slideshow_glass_frame.png")));
        const QRect pr = pictureRect();
        p.save();
        p.setClipRect(pr);
        const qreal t = m_anim.state() == QAbstractAnimation::Running ? m_anim.currentValue().toReal() : 1.0;
        paintTransition(p, pr, t);
        p.restore();
        if (m_pictures.isEmpty()) {
            p.setFont(gadgetFont(big ? 13 : 11));
            p.setPen(Qt::white);
            p.drawText(pr.adjusted(6, 6, -6, -6), Qt::AlignCenter | Qt::TextWordWrap, tr7("There are no pictures in this folder."));
        }
        if (underMouse()) {
            p.drawPixmap(barPos(), gadgetPixmap(QStringLiteral("slideshow/in_sidebar/bg_sidebar.png")));
            const QString names[] = {QStringLiteral("prev"), m_playing ? QStringLiteral("pause") : QStringLiteral("play"), QStringLiteral("next"), QStringLiteral("reveal")};
            for (BarButton b : {Prev, PlayPause, Next, Reveal}) {
                const QString state = b != m_hotButton ? QStringLiteral("rest") : (m_downButton == b ? QStringLiteral("down") : QStringLiteral("hov"));
                p.drawPixmap(buttonRect(b).topLeft(), gadgetPixmap(QStringLiteral("slideshow/%1_%2.png").arg(names[b], state)));
            }
        }
    }

    void paintTransition(QPainter &p, const QRect &pr, qreal t)
    {
        auto placed = [&pr](const QPixmap &pm) {
            const QSizeF s = pm.deviceIndependentSize();
            return QRectF(pr.left() + (pr.width() - s.width()) / 2.0, pr.top() + (pr.height() - s.height()) / 2.0, s.width(), s.height());
        };
        if (m_previous.isNull() || t >= 1.0) {
            if (!m_current.isNull()) {
                p.drawPixmap(placed(m_current), m_current, QRectF(m_current.rect()));
            }
            return;
        }
        const QRectF from = placed(m_previous);
        const QRectF to = placed(m_current);
        switch (SlideEffect(m_effect)) {
        case SlideEffect::Slide:
            p.drawPixmap(from.translated(-pr.width() * t, 0), m_previous, QRectF(m_previous.rect()));
            p.drawPixmap(to.translated(pr.width() * (1 - t), 0), m_current, QRectF(m_current.rect()));
            break;
        case SlideEffect::Wipe:
            p.drawPixmap(from, m_previous, QRectF(m_previous.rect()));
            p.setClipRect(QRectF(pr.left(), pr.top(), pr.width() * t, pr.height()), Qt::IntersectClip);
            p.drawPixmap(to, m_current, QRectF(m_current.rect()));
            break;
        case SlideEffect::Iris: {
            p.drawPixmap(from, m_previous, QRectF(m_previous.rect()));
            QPainterPath circle;
            const qreal r = std::hypot(pr.width(), pr.height()) / 2.0 * t;
            circle.addEllipse(QRectF(pr).center(), r, r);
            p.setClipPath(circle, Qt::IntersectClip);
            p.drawPixmap(to, m_current, QRectF(m_current.rect()));
            break;
        }
        default: // fade
            p.drawPixmap(from, m_previous, QRectF(m_previous.rect()));
            p.setOpacity(t);
            p.drawPixmap(to, m_current, QRectF(m_current.rect()));
            p.setOpacity(1);
            break;
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        Gadget::mouseMoveEvent(e);
        const BarButton hot = barButtonAt(e->position());
        if (hot != m_hotButton) {
            m_hotButton = hot;
            update();
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        m_downButton = barButtonAt(e->position());
        Gadget::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        Gadget::mouseReleaseEvent(e);
        m_downButton = NoButton;
        update();
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (pictureRect().contains(e->position().toPoint()) && barButtonAt(e->position()) == NoButton) {
            reveal();
        }
    }
    void bodyClicked(QPointF pos) override
    {
        switch (barButtonAt(pos)) {
        case Prev:
            m_playing = true;
            showPicture(m_index - 1, true);
            break;
        case PlayPause:
            m_playing = !m_playing;
            if (m_playing) {
                m_timer.start();
            } else {
                m_timer.stop();
            }
            update();
            break;
        case Next:
            m_playing = true;
            showPicture(m_index + 1, true);
            break;
        case Reveal:
            reveal();
            break;
        default:
            break;
        }
    }

    void configure() override
    {
        SlideShowSettings dlg(m_folder, m_subdirs, m_seconds, m_effect, m_shuffle);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        store().setValue(id + QStringLiteral("/Folder"), dlg.folder());
        store().setValue(id + QStringLiteral("/SubDirectories"), dlg.subdirs());
        store().setValue(id + QStringLiteral("/HowOften"), dlg.seconds());
        store().setValue(id + QStringLiteral("/FadeEffect"), dlg.effect());
        store().setValue(id + QStringLiteral("/Shuffle"), dlg.shuffle());
        store().sync();
        loadSettings();
        rescan();
    }

    void resizeEvent(QResizeEvent *e) override
    {
        Gadget::resizeEvent(e);
        // the size button: reload the picture for the new frame
        if (!m_pictures.isEmpty() && m_loadedFor != pictureRect().size()) {
            m_previous = QPixmap();
            m_current = loadPicture(m_pictures.value(m_index));
        }
    }

private:
    void loadSettings()
    {
        m_folder = store().value(id + QStringLiteral("/Folder"), samplePicturesDir()).toString();
        m_subdirs = store().value(id + QStringLiteral("/SubDirectories"), false).toBool();
        m_seconds = store().value(id + QStringLiteral("/HowOften"), 15).toInt();
        m_effect = store().value(id + QStringLiteral("/FadeEffect"), int(SlideEffect::Fade)).toInt();
        m_shuffle = store().value(id + QStringLiteral("/Shuffle"), false).toBool();
        m_timer.setInterval(m_seconds * 1000);
    }

    /** makePicList(): at most 2000 pictures */
    void rescan()
    {
        m_pictures.clear();
        static const QStringList filters{QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"), QStringLiteral("*.bmp"),
                                         QStringLiteral("*.gif"), QStringLiteral("*.webp"), QStringLiteral("*.tif"), QStringLiteral("*.tiff")};
        QDirIterator it(m_folder, filters, QDir::Files | QDir::Readable, m_subdirs ? QDirIterator::Subdirectories | QDirIterator::FollowSymlinks : QDirIterator::NoIteratorFlags);
        while (it.hasNext() && m_pictures.size() < 2000) {
            m_pictures << it.next();
        }
        if (m_shuffle) {
            std::shuffle(m_pictures.begin(), m_pictures.end(), *QRandomGenerator::global());
        } else {
            QCollator c;
            c.setNumericMode(true);
            std::sort(m_pictures.begin(), m_pictures.end(), [&c](const QString &a, const QString &b) {
                return c.compare(a, b) < 0;
            });
        }
        m_index = 0;
        m_previous = QPixmap();
        m_current = m_pictures.isEmpty() ? QPixmap() : loadPicture(m_pictures.first());
        m_playing = true;
        m_timer.start();
        updateToolTip();
        update();
    }

    /** Scaled to fit the frame, keeping the shape, like checkHeightWidth() */
    QPixmap loadPicture(const QString &file)
    {
        m_loadedFor = pictureRect().size();
        QImageReader reader(file);
        reader.setAutoTransform(true);
        const QSize full = reader.size();
        const qreal dpr = devicePixelRatioF();
        if (full.isValid()) {
            reader.setScaledSize(full.scaled(m_loadedFor * dpr, Qt::KeepAspectRatio));
        }
        QImage img = reader.read();
        if (img.isNull()) {
            return {};
        }
        if (!full.isValid()) {
            img = img.scaled(m_loadedFor * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QPixmap pm = QPixmap::fromImage(img);
        pm.setDevicePixelRatio(dpr);
        return pm;
    }

    void showPicture(int index, bool animate)
    {
        if (m_pictures.isEmpty()) {
            return;
        }
        const int n = int(m_pictures.size());
        m_index = ((index % n) + n) % n;
        const QPixmap next = loadPicture(m_pictures.at(m_index));
        if (animate && m_effect != int(SlideEffect::None) && !m_current.isNull()) {
            m_previous = m_current;
            m_current = next;
            m_anim.stop();
            m_anim.start();
        } else {
            m_previous = QPixmap();
            m_current = next;
        }
        if (m_playing) {
            m_timer.start();
        }
        updateToolTip();
        update();
    }

    void updateToolTip()
    {
        setToolTip(m_pictures.isEmpty() ? QString() : QFileInfo(m_pictures.value(m_index)).fileName());
    }

    void reveal()
    {
        if (!m_pictures.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_pictures.value(m_index)));
        }
    }

    QString m_folder;
    bool m_subdirs = false;
    int m_seconds = 15;
    int m_effect = int(SlideEffect::Fade);
    bool m_shuffle = false;
    QStringList m_pictures;
    int m_index = 0;
    bool m_playing = true;
    QPixmap m_current;
    QPixmap m_previous;
    QSize m_loadedFor;
    QVariantAnimation m_anim;
    QTimer m_timer;
    BarButton m_hotButton = NoButton;
    BarButton m_downButton = NoButton;
};
