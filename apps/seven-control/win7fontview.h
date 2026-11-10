/*
 * Windows Font Viewer (fontview.exe): name, version and outlines of one font file, its alphabet and the pangram
 * at 12–72 points, with Print and Install. Opened by double-clicking a font in the folder or a .ttf in Dolphin.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7fonts.h"

#include <QPrintDialog>
#include <QPrinter>

namespace Win7Fonts
{

struct ViewerFace {
    QString file;
    int index = 0;
    QString family;
    QString style;
    QString fullName;
    QString version;
    QStringList traits;
    bool cyrillic = true;
};

inline ViewerFace viewerFace(const QString &file, int index)
{
    ViewerFace v{file, index};
    FcFontSet *set = FcFontSetCreate();
    FcFreeTypeQueryAll(reinterpret_cast<const FcChar8 *>(QFile::encodeName(file).constData()), index, nullptr, nullptr, set);
    if (set->nfont > 0) {
        Face f;
        fillFace(f, set->fonts[0]);
        v.family = f.family;
        v.style = f.style;
        v.cyrillic = f.scripts.contains(QStringLiteral("Кирилиця"));
    }
    FcFontSetDestroy(set);
    SfntFile sf(file, index);
    v.fullName = sf.name(4);
    if (v.fullName.isEmpty()) {
        v.fullName = (v.family + QLatin1Char(' ') + v.style).trimmed();
    }
    v.version = sf.name(5);
    if (sf.has("GSUB") || sf.has("GPOS")) {
        v.traits << QStringLiteral("Макет OpenType");
    }
    if (sf.has("DSIG")) {
        v.traits << QStringLiteral("Цифровий підпис");
    }
    if (sf.has("CFF ") || sf.has("CFF2")) {
        v.traits << QStringLiteral("Контури PostScript");
    } else if (sf.has("glyf")) {
        v.traits << QStringLiteral("Контури TrueType");
    }
    return v;
}

/** The page of fontview.exe, painted the same on screen and on paper */
class FontSheet : public QWidget
{
public:
    explicit FontSheet(ViewerFace face)
        : m_face(std::move(face))
    {
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("background: #ffffff;"));
        setMinimumSize(400, sheetSize(1.0).height()); // long lines are cut off at the edge, as in fontview.exe
    }

    QSize sheetSize(qreal scale) const
    {
        return QSize(int(900 * scale), int(820 * scale));
    }

    /** scale: device pixels per screen pixel (1 on screen, ~6 on a 600 dpi printer) */
    void paintSheet(QPainter &p, qreal scale, int width)
    {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QFont ui = font();
        ui.setPixelSize(int(12 * scale));
        int y = int(14 * scale);
        const int x = int(16 * scale);
        auto line = [&](const QString &text, bool bold = false, int px = 12) {
            QFont f = ui;
            f.setPixelSize(int(px * scale));
            f.setBold(bold);
            p.setFont(f);
            p.setPen(Qt::black);
            const QFontMetrics fm(f);
            p.drawText(x, y + fm.ascent(), text);
            y += fm.height() + int(2 * scale);
        };
        line(QStringLiteral("Ім'я шрифту: %1").arg(m_face.fullName), false, 15);
        if (!m_face.version.isEmpty()) {
            line(QStringLiteral("Версія: %1").arg(m_face.version.section(QLatin1Char(';'), 0, 0).trimmed()));
        }
        if (!m_face.traits.isEmpty()) {
            line(m_face.traits.join(QStringLiteral(", ")));
        }
        y += int(8 * scale);
        p.fillRect(QRect(x, y, width - 2 * x, qMax(1, int(scale))), QColor(0xb0, 0xb0, 0xb0));
        y += int(12 * scale);
        const QString alphabet = m_face.cyrillic ? QStringLiteral("абвгґдеєжзиіїйклмнопрстуфхцчшщьюя АБВГҐДЕЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЬЮЯ")
                                                 : QStringLiteral("abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        for (const QString &s : {alphabet, QStringLiteral("1234567890.:,; ' \" (!?) +-*/=")}) {
            const QImage img = renderText(m_face.file, m_face.index, s, int(16 * scale));
            p.drawImage(x, y, img);
            y += img.height() + int(4 * scale);
        }
        y += int(8 * scale);
        p.fillRect(QRect(x, y, width - 2 * x, qMax(1, int(scale))), QColor(0xb0, 0xb0, 0xb0));
        y += int(12 * scale);
        const QString pangram = m_face.cyrillic ? QStringLiteral("Чуєш їх, доцю, га? Кумедна ж ти, прощайся без ґольфів! 1234567890")
                                                : QStringLiteral("The quick brown fox jumps over the lazy dog. 1234567890");
        for (int pt : {12, 18, 24, 36, 48, 60, 72}) {
            QFont f = ui;
            f.setPixelSize(int(11 * scale));
            p.setFont(f);
            const int px = int(std::lround(pt * 96.0 / 72.0 * scale));
            const QImage img = renderText(m_face.file, m_face.index, pangram, px);
            p.setPen(Qt::black);
            p.drawText(x, y + QFontMetrics(f).ascent() + int(2 * scale), QString::number(pt));
            p.save();
            p.setClipRect(QRect(x + int(30 * scale), y, width - x - int(30 * scale), img.height()));
            p.drawImage(x + int(30 * scale), y, img);
            p.restore();
            y += img.height() + int(6 * scale);
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        paintSheet(p, 1.0, width());
    }

private:
    ViewerFace m_face;
};

class FontViewer : public QWidget
{
public:
    FontViewer(const QString &file, int index)
        : m_face(viewerFace(file, index))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-fontview")));
        setWindowTitle(QStringLiteral("%1 (%2)").arg(m_face.fullName, fontType(file, index)));
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        auto *top = new QWidget;
        top->setObjectName(QStringLiteral("win7fvbar"));
        top->setAttribute(Qt::WA_StyledBackground, true);
        top->setStyleSheet(QStringLiteral("#win7fvbar { background: #f0f0f0; border-bottom: 1px solid #d5d5d5; }"));
        auto *h = new QHBoxLayout(top);
        h->setContentsMargins(8, 6, 8, 6);
        auto *print = new QPushButton(QStringLiteral("Друк"));
        m_install = new QPushButton(QStringLiteral("Установити"));
        for (QPushButton *b : {print, m_install}) {
            b->setMinimumWidth(80);
            h->addWidget(b);
        }
        h->addStretch(1);
        v->addWidget(top);
        auto *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_sheet = new FontSheet(m_face);
        scroll->setWidget(m_sheet);
        v->addWidget(scroll, 1);
        updateInstall();
        connect(print, &QPushButton::clicked, this, [this] { printSheet(); });
        connect(m_install, &QPushButton::clicked, this, [this] {
            if (installFonts(this, {m_face.file})) {
                updateInstall();
            }
        });
        resize(760, 640);
    }

    void printSheet()
    {
        QPrinter printer(QPrinter::HighResolution);
        printer.setDocName(windowTitle());
        QPrintDialog dlg(&printer, this);
        dlg.setWindowTitle(QStringLiteral("Друк"));
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        QPainter p(&printer);
        const QRect page = printer.pageLayout().paintRectPixels(printer.resolution());
        const qreal scale = printer.resolution() / 96.0;
        m_sheet->paintSheet(p, scale, page.width());
    }

private:
    void updateInstall()
    {
        // already installed: the button is greyed, like fontview.exe
        bool installed = isUserFont(m_face.file) && m_face.file.startsWith(userFontDir());
        if (!installed) {
            for (const Face &f : installedFaces()) {
                if (QFileInfo(f.file).canonicalFilePath() == QFileInfo(m_face.file).canonicalFilePath()) {
                    installed = true;
                    break;
                }
            }
        }
        if (!installed && QFileInfo::exists(userFontDir() + QLatin1Char('/') + QFileInfo(m_face.file).fileName())) {
            installed = true;
        }
        m_install->setEnabled(!installed);
    }

    ViewerFace m_face;
    FontSheet *m_sheet;
    QPushButton *m_install;
};

} // namespace Win7Fonts
