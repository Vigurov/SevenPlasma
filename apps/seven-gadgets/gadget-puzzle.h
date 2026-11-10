#include "win7i18n.h"
/*
 * Picture Puzzle: PicturePuzzle.Gadget (picturePuzzle.js / picturePuzzle.css) — the 4x4 sliding puzzle
 * on the wooden board with the timer, "show picture" and shuffle / solve buttons. Included inside main.cpp's namespace.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

constexpr int kPuzzlePictures = 11;

class PuzzlePicturePreview : public QWidget
{
public:
    int image = 1;

    PuzzlePicturePreview()
    {
        setFixedSize(130, 130);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(QRect(11, 11, 108, 108), gadgetPixmap(QStringLiteral("puzzle/%1.png").arg(image)));
        p.setPen(QColor(0x46, 0x23, 0x00));
        p.drawRect(QRect(10, 10, 109, 109));
    }
};

class PuzzleSettings : public GadgetDialog
{
public:
    explicit PuzzleSettings(int image)
        : GadgetDialog(tr7("Picture Puzzle"))
    {
        body->addWidget(new QLabel(tr7("Choose a picture:")));
        m_preview = new PuzzlePicturePreview;
        m_preview->image = image;
        body->addWidget(m_preview, 0, Qt::AlignHCenter);
        auto *nav = new QHBoxLayout;
        nav->addStretch(1);
        auto *prev = new QToolButton;
        auto *next = new QToolButton;
        prev->setArrowType(Qt::LeftArrow);
        next->setArrowType(Qt::RightArrow);
        prev->setToolTip(tr7("Previous"));
        next->setToolTip(tr7("Next"));
        m_count = new QLabel;
        nav->addWidget(prev);
        nav->addSpacing(8);
        nav->addWidget(m_count);
        nav->addSpacing(8);
        nav->addWidget(next);
        nav->addStretch(1);
        body->addLayout(nav);
        body->addStretch(1);
        updateCount();
        connect(prev, &QToolButton::clicked, this, [this] {
            m_preview->image = m_preview->image > 1 ? m_preview->image - 1 : kPuzzlePictures;
            updateCount();
        });
        connect(next, &QToolButton::clicked, this, [this] {
            m_preview->image = m_preview->image < kPuzzlePictures ? m_preview->image + 1 : 1;
            updateCount();
        });
    }

    int image() const
    {
        return m_preview->image;
    }

private:
    void updateCount()
    {
        m_count->setText(tr7("%1 of %2").arg(m_preview->image).arg(kPuzzlePictures));
        m_preview->update();
    }

    PuzzlePicturePreview *m_preview;
    QLabel *m_count;
};

class PuzzleGadget : public Gadget
{
public:
    explicit PuzzleGadget(const QString &gadgetId)
        : Gadget(gadgetId)
    {
        m_image = qBound(1, store().value(id + QStringLiteral("/imageID"), 1).toInt(), kPuzzlePictures);
        m_seconds = store().value(id + QStringLiteral("/totalSeconds"), 0).toInt();
        const QStringList saved = store().value(id + QStringLiteral("/Tiles")).toStringList();
        m_tiles.resize(16);
        bool ok = saved.size() == 16;
        for (int i = 0; ok && i < 16; ++i) {
            m_tiles[i] = saved.at(i).toInt(&ok);
        }
        if (!ok || !isPermutation()) {
            std::iota(m_tiles.begin(), m_tiles.end(), 0);
            shuffle();
        }
        m_started = store().value(id + QStringLiteral("/Started"), false).toBool() && !solved();
        m_solving = !solved();

        m_tick.setInterval(1000);
        connect(&m_tick, &QTimer::timeout, this, [this] {
            ++m_seconds;
            store().setValue(id + QStringLiteral("/totalSeconds"), m_seconds);
            update();
        });
        // a game in progress waits paused until the next move
        m_slide.setDuration(110);
        m_slide.setStartValue(0.0);
        m_slide.setEndValue(1.0);
        connect(&m_slide, &QVariantAnimation::valueChanged, this, [this] {
            update();
        });
        connect(&m_slide, &QVariantAnimation::finished, this, [this] {
            m_moving = -1;
            update();
        });
        m_winFade.setDuration(700);
        m_winFade.setStartValue(0.0);
        m_winFade.setEndValue(1.0);
        connect(&m_winFade, &QVariantAnimation::valueChanged, this, [this] {
            update();
        });
    }

protected:
    static constexpr int kTile = 27; // tileSize
    static constexpr QPoint kPad = QPoint(11, 21);

    QSize bodySize() const override
    {
        return QSize(130, 138);
    }
    bool glassFrame() const override
    {
        return false;
    }
    bool hasSizeButton() const override
    {
        return false;
    }

    enum PuzzleButton { Timer, Hint, Shuffle, NoButton };

    QRect buttonRect(PuzzleButton b) const
    {
        switch (b) {
        case Timer:
            return QRect(9, 4, 14, 14);
        case Hint:
            return QRect(92, 4, 14, 14);
        case Shuffle:
            return QRect(107, 4, 14, 14);
        default:
            return {};
        }
    }
    PuzzleButton puzzleButtonAt(QPointF pos) const
    {
        for (PuzzleButton b : {Timer, Hint, Shuffle}) {
            if (buttonRect(b).contains(pos.toPoint())) {
                return b;
            }
        }
        return NoButton;
    }

    QPointF slotPos(int slot) const
    {
        return QPointF(kPad.x() + (slot % 4) * kTile, kPad.y() + (slot / 4) * kTile);
    }

    void paintBody(QPainter &p, const QRectF &) override
    {
        p.drawPixmap(0, 0, gadgetPixmap(QStringLiteral("puzzle/background.png")));
        const QString names[] = {QStringLiteral("timer"), QStringLiteral("hint"), QStringLiteral("shuffle")};
        for (PuzzleButton b : {Timer, Hint, Shuffle}) {
            const QString state = b != m_hotButton ? QStringLiteral("up") : (m_downButton == b ? QStringLiteral("down") : QStringLiteral("over"));
            p.drawPixmap(buttonRect(b).topLeft(), gadgetPixmap(QStringLiteral("puzzle/%1_%2.png").arg(names[b], state)));
        }
        p.setFont(gadgetFont(11, true));
        p.setPen(QColor(0x46, 0x23, 0x00));
        p.drawText(QRect(24, 3, 66, 16), Qt::AlignLeft | Qt::AlignVCenter, timeText());

        // the pad: 1px brown border around 4x4 tiles of 27px
        p.setPen(QColor(0x46, 0x23, 0x00));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(kPad.x() - 1, kPad.y() - 1, 4 * kTile + 1, 4 * kTile + 1));
        const QPixmap &picture = gadgetPixmap(QStringLiteral("puzzle/%1.png").arg(m_image));
        const QPixmap &bezel = gadgetPixmap(QStringLiteral("puzzle/tile_bezel.png"));
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (m_hint) {
            p.drawPixmap(QRect(kPad, QSize(4 * kTile, 4 * kTile)), picture);
        } else {
            const int hole = int(m_tiles.indexOf(15));
            p.drawPixmap(QRectF(slotPos(hole), QSizeF(kTile, kTile)), gadgetPixmap(QStringLiteral("puzzle/tile_drop_shadow.png")), QRectF(0, 0, 32, 32));
            for (int slot = 0; slot < 16; ++slot) {
                const int tile = m_tiles.at(slot);
                if (tile == 15) {
                    continue;
                }
                QPointF at = slotPos(slot);
                if (tile == m_moving && m_slide.state() == QAbstractAnimation::Running) {
                    const qreal t = m_slide.currentValue().toReal();
                    at = m_moveFrom + (at - m_moveFrom) * t;
                }
                const QRectF dst(at, QSizeF(kTile, kTile));
                p.drawPixmap(dst, picture, QRectF((tile % 4) * kTile, (tile / 4) * kTile, kTile, kTile));
                p.drawPixmap(dst, bezel, QRectF(bezel.rect()));
            }
        }
        p.drawPixmap(QRect(kPad, QSize(4 * kTile, 4 * kTile)), gadgetPixmap(QStringLiteral("puzzle/glow.png")));

        if (m_won) {
            const qreal a = m_winFade.state() == QAbstractAnimation::Running ? m_winFade.currentValue().toReal() : 1.0;
            const QRect box(10, 19, 110, 110);
            p.fillRect(box, QColor(0x33, 0x33, 0x33, int(178 * a)));
            p.setPen(QColor(0x46, 0x23, 0x00, int(255 * a)));
            p.drawRect(box.adjusted(0, 0, -1, -1));
            p.setFont(gadgetFont(12, true));
            p.setPen(QColor(255, 255, 255, int(255 * a)));
            p.drawText(box.adjusted(4, 30, -4, -4), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, tr7("Congratulations!\nYou won!"));
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        Gadget::mouseMoveEvent(e);
        const PuzzleButton hot = puzzleButtonAt(e->position());
        if (hot != m_hotButton) {
            m_hotButton = hot;
            setToolTip(hot == Timer ? tr7("Pause timer")
                                    : hot == Hint ? tr7("Show picture")
                                                  : hot == Shuffle ? (m_solving ? tr7("Solve") : tr7("Shuffle")) : QString());
            update();
        }
    }
    void leaveEvent(QEvent *e) override
    {
        Gadget::leaveEvent(e);
        m_hotButton = NoButton;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        m_downButton = puzzleButtonAt(e->position());
        if (m_downButton == Hint && e->button() == Qt::LeftButton) {
            // hold to peek at the picture
            m_hint = true;
            update();
            return;
        }
        // the tiles take clicks, the wooden frame drags the gadget
        if (e->button() == Qt::LeftButton && padSlotAt(e->position()) >= 0) {
            m_padPress = true;
            return;
        }
        Gadget::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (m_hint) {
            m_hint = false;
            m_downButton = NoButton;
            update();
            return;
        }
        if (m_padPress) {
            m_padPress = false;
            const int slot = padSlotAt(e->position());
            if (slot >= 0) {
                clickSlot(slot);
            }
            return;
        }
        Gadget::mouseReleaseEvent(e);
        m_downButton = NoButton;
        update();
    }
    void bodyClicked(QPointF pos) override
    {
        switch (puzzleButtonAt(pos)) {
        case Timer:
            if (m_tick.isActive()) {
                m_tick.stop();
            } else if (m_started && !m_won) {
                m_tick.start();
            }
            break;
        case Shuffle:
            // shuffleHandler(): "Solve" while a game runs, then "Shuffle"
            if (m_solving) {
                std::iota(m_tiles.begin(), m_tiles.end(), 0);
                m_tick.stop();
                m_started = false;
                m_solving = false;
            } else {
                shuffle();
                m_seconds = 0;
                m_started = false;
                m_solving = true;
            }
            m_won = false;
            save();
            update();
            break;
        default:
            break;
        }
    }

    void configure() override
    {
        PuzzleSettings dlg(m_image);
        if (dlg.exec() != QDialog::Accepted || dlg.image() == m_image) {
            return;
        }
        m_image = dlg.image();
        // a new picture is a new game
        shuffle();
        m_seconds = 0;
        m_started = false;
        m_solving = true;
        m_won = false;
        m_tick.stop();
        save();
        update();
    }

private:
    int padSlotAt(QPointF pos) const
    {
        const QPointF r = pos - QPointF(kPad);
        if (r.x() < 0 || r.y() < 0 || r.x() >= 4 * kTile || r.y() >= 4 * kTile || m_hint) {
            return -1;
        }
        return int(r.y() / kTile) * 4 + int(r.x() / kTile);
    }

    /** A tile next to the hole slides into it */
    void clickSlot(int slot)
    {
        if (m_won) {
            m_won = false;
            update();
            return;
        }
        const int hole = int(m_tiles.indexOf(15));
        const int dx = std::abs(slot % 4 - hole % 4);
        const int dy = std::abs(slot / 4 - hole / 4);
        if (dx + dy != 1 || !m_solving) {
            return;
        }
        m_moving = m_tiles.at(slot);
        m_moveFrom = slotPos(slot);
        std::swap(m_tiles[slot], m_tiles[hole]);
        m_slide.stop();
        m_slide.start();
        if (!m_started) {
            m_started = true;
        }
        if (!m_tick.isActive()) {
            m_tick.start();
        }
        if (solved()) {
            m_tick.stop();
            m_won = true;
            m_solving = false;
            m_started = false;
            m_winFade.start();
        }
        save();
        update();
    }

    /** onShuffle(): random moves of the hole, so the puzzle stays solvable */
    void shuffle()
    {
        std::iota(m_tiles.begin(), m_tiles.end(), 0);
        int hole = 15;
        int last = -1;
        auto *rng = QRandomGenerator::global();
        for (int i = 0; i < 1000 || solved(); ++i) {
            QList<int> next;
            if (hole % 4 > 0) {
                next << hole - 1;
            }
            if (hole % 4 < 3) {
                next << hole + 1;
            }
            if (hole / 4 > 0) {
                next << hole - 4;
            }
            if (hole / 4 < 3) {
                next << hole + 4;
            }
            next.removeAll(last);
            const int to = next.at(rng->bounded(int(next.size())));
            std::swap(m_tiles[hole], m_tiles[to]);
            last = hole;
            hole = to;
        }
    }

    bool solved() const
    {
        for (int i = 0; i < 16; ++i) {
            if (m_tiles.at(i) != i) {
                return false;
            }
        }
        return true;
    }
    bool isPermutation() const
    {
        QList<int> sorted = m_tiles;
        std::sort(sorted.begin(), sorted.end());
        for (int i = 0; i < 16; ++i) {
            if (sorted.at(i) != i) {
                return false;
            }
        }
        return true;
    }

    QString timeText() const
    {
        return QStringLiteral("%1:%2:%3")
            .arg(m_seconds / 3600, 2, 10, QLatin1Char('0'))
            .arg((m_seconds / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(m_seconds % 60, 2, 10, QLatin1Char('0'));
    }

    void save()
    {
        QStringList tiles;
        for (int t : std::as_const(m_tiles)) {
            tiles << QString::number(t);
        }
        store().setValue(id + QStringLiteral("/Tiles"), tiles);
        store().setValue(id + QStringLiteral("/imageID"), m_image);
        store().setValue(id + QStringLiteral("/totalSeconds"), m_seconds);
        store().setValue(id + QStringLiteral("/Started"), m_started);
        store().sync();
    }

    void hideEvent(QHideEvent *e) override
    {
        Gadget::hideEvent(e);
        save();
    }

    QList<int> m_tiles; // slot -> tile, 15 is the hole
    int m_image = 1;
    int m_seconds = 0;
    bool m_started = false;
    bool m_solving = true;
    bool m_won = false;
    bool m_hint = false;
    bool m_padPress = false;
    int m_moving = -1;
    QPointF m_moveFrom;
    QTimer m_tick;
    QVariantAnimation m_slide;
    QVariantAnimation m_winFade;
    PuzzleButton m_hotButton = NoButton;
    PuzzleButton m_downButton = NoButton;
};
