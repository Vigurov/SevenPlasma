/*
 * ClearType Text Tuner (cttune.exe): turn subpixel text on or off, check the native resolution, then pick the
 * best looking sample a few times. On Linux "ClearType" is FreeType's subpixel rendering: the samples differ in
 * subpixel order, hinting and LCD filter, and the choice goes to fontconfig, kdeglobals and the X resources.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7fontdata.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QToolButton>

namespace Win7Fonts
{

/** What the system uses now (kdeglobals, as the KDE font settings write it) */
inline RenderMode currentRenderMode()
{
    const KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("kdeglobals")), QStringLiteral("General"));
    RenderMode m;
    const QString sub = g.readEntry("XftSubPixel", QStringLiteral("rgb"));
    m.lcd = sub == QLatin1String("rgb") || sub == QLatin1String("bgr");
    m.bgr = sub == QLatin1String("bgr");
    const QString hint = g.readEntry("XftHintStyle", QStringLiteral("hintslight"));
    m.hinting = hint == QLatin1String("hintnone") ? 0 : (hint == QLatin1String("hintslight") ? 1 : 2);
    const QString filter = fontSettings().value(QStringLiteral("ClearType/Filter"), QStringLiteral("lcddefault")).toString();
    m.filter = filter == QLatin1String("lcdlight") ? FT_LCD_FILTER_LIGHT : (filter == QLatin1String("lcdlegacy") ? FT_LCD_FILTER_LEGACY : FT_LCD_FILTER_DEFAULT);
    return m;
}

inline QString uiFontFile()
{
    FcPattern *pat = FcNameParse(reinterpret_cast<const FcChar8 *>("Segoe UI"));
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    FcPattern *m = FcFontMatch(nullptr, pat, &res);
    QString file;
    FcChar8 *s = nullptr;
    if (m && FcPatternGetString(m, FC_FILE, 0, &s) == FcResultMatch) {
        file = QString::fromUtf8(reinterpret_cast<const char *>(s));
    }
    if (m) {
        FcPatternDestroy(m);
    }
    FcPatternDestroy(pat);
    return file;
}

/** Writes the choice where fontconfig, KDE and the X server look for it */
inline void applyRenderMode(const RenderMode &m)
{
    const QString rgba = m.lcd ? (m.bgr ? QStringLiteral("bgr") : QStringLiteral("rgb")) : QStringLiteral("none");
    const QString hint = m.hinting == 0 ? QStringLiteral("hintnone") : (m.hinting == 1 ? QStringLiteral("hintslight") : QStringLiteral("hintfull"));
    const QString filter = m.filter == FT_LCD_FILTER_LIGHT ? QStringLiteral("lcdlight") : (m.filter == FT_LCD_FILTER_LEGACY ? QStringLiteral("lcdlegacy") : QStringLiteral("lcddefault"));

    KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("kdeglobals")), QStringLiteral("General"));
    g.writeEntry("XftAntialias", true);
    g.writeEntry("XftSubPixel", rgba);
    g.writeEntry("XftHintStyle", hint);
    g.sync();
    fontSettings().setValue(QStringLiteral("ClearType/Filter"), filter);
    fontSettings().sync();

    // ~/.config/fontconfig/fonts.conf is the KDE font module's file: change its blocks, add the LCD filter
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/fontconfig/fonts.conf");
    QFile f(path);
    QString xml;
    if (f.open(QIODevice::ReadOnly)) {
        xml = QString::fromUtf8(f.readAll());
        f.close();
    }
    if (xml.isEmpty()) {
        xml = QStringLiteral("<?xml version='1.0'?>\n<!DOCTYPE fontconfig SYSTEM 'fonts.dtd'>\n<fontconfig>\n</fontconfig>\n");
    }
    auto setConst = [&xml](const QString &name, const QString &value) {
        const QRegularExpression re(QStringLiteral("(<edit mode=\"assign\" name=\"%1\">\\s*<const>)[^<]*(</const>)").arg(name));
        if (xml.contains(re)) {
            xml.replace(re, QStringLiteral("\\1%1\\2").arg(value));
        } else {
            xml.replace(QStringLiteral("</fontconfig>"), QStringLiteral(" <match target=\"font\">\n  <edit mode=\"assign\" name=\"%1\">\n   <const>%2</const>\n  </edit>\n </match>\n</fontconfig>")
                                                             .arg(name, value));
        }
    };
    setConst(QStringLiteral("rgba"), rgba);
    setConst(QStringLiteral("hintstyle"), hint);
    setConst(QStringLiteral("lcdfilter"), filter);
    QDir().mkpath(QFileInfo(path).path());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(xml.toUtf8());
    }

    // programs that read the X resources (and the ones started later from this session)
    QProcess xrdb;
    xrdb.start(QStringLiteral("xrdb"), {QStringLiteral("-merge")});
    if (xrdb.waitForStarted(2000)) {
        xrdb.write(QStringLiteral("Xft.antialias: 1\nXft.rgba: %1\nXft.hintstyle: %2\nXft.hinting: %3\nXft.lcdfilter: %4\n")
                       .arg(rgba, hint, m.hinting ? QStringLiteral("1") : QStringLiteral("0"), filter)
                       .toUtf8());
        xrdb.closeWriteChannel();
        xrdb.waitForFinished(3000);
    }
    FcInitReinitialize();
}

/** A box of sample text; the chosen one gets the blue frame (BitmapSwitchClass) */
class SampleBox : public QWidget
{
public:
    std::function<void()> clicked;
    bool chosen = false;

    SampleBox(const RenderMode &mode, const QString &fontFile, int width, int lines)
        : m_mode(mode)
        , m_file(fontFile)
        , m_lines(lines)
    {
        setFixedSize(width, 18 * lines + 18);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        static const QStringList text{
            tr7("The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs!"),
            QStringLiteral("The Quick Brown Fox Jumps Over the Lazy Dog."),
            QStringLiteral("Lorem ipsum dolor sit amet, consectetuer adipiscing elit."),
            QStringLiteral("Mauris ornare odio vel risus. Maecenas elit metus,"),
            tr7("pellentesque quis, pretium. Sphinx of black quartz, judge my vow. How vexingly quick daft zebras jump."),
            tr7("ClearType improves the readability of text on LCD monitors."),
        };
        p.setClipRect(rect().adjusted(6, 6, -6, -6));
        for (int i = 0; i < m_lines; ++i) {
            const QImage img = renderText(m_file, 0, text.at(i % text.size()), 13, m_mode);
            p.drawImage(9, 9 + i * 18, img);
        }
        p.setClipping(false);
        if (chosen) {
            p.setPen(QPen(QColor(0x33, 0x99, 0xff), 3));
            p.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
        } else {
            p.setPen(QColor(0xc0, 0xc0, 0xc0));
            p.drawRect(rect().adjusted(0, 0, -1, -1));
        }
    }
    void mousePressEvent(QMouseEvent *) override
    {
        if (clicked) {
            clicked();
        }
    }

private:
    RenderMode m_mode;
    QString m_file;
    int m_lines;
};

class ClearTypeTuner : public QDialog
{
public:
    ClearTypeTuner()
        : m_mode(currentRenderMode())
        , m_font(uiFontFile())
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(tr7("ClearType Text Tuner"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cttune")));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        m_start = m_mode;
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto *white = new QWidget;
        white->setObjectName(QStringLiteral("win7wizwhite"));
        white->setAttribute(Qt::WA_StyledBackground, true);
        white->setStyleSheet(QStringLiteral("#win7wizwhite { background: #ffffff; }"));
        auto *wv = new QVBoxLayout(white);
        wv->setContentsMargins(10, 8, 30, 14);
        auto *head = new QHBoxLayout;
        m_back = new QToolButton;
        m_back->setIcon(QIcon::fromTheme(QStringLiteral("go-previous")));
        m_back->setAutoRaise(true);
        head->addWidget(m_back);
        auto *hicon = new QLabel;
        hicon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cttune")).pixmap(16, 16));
        head->addWidget(hicon);
        head->addWidget(new QLabel(tr7("ClearType Text Tuner")), 1);
        wv->addLayout(head);
        m_title = new QLabel;
        QFont tf = m_title->font();
        tf.setPixelSize(16);
        m_title->setFont(tf);
        m_title->setStyleSheet(QStringLiteral("color: #1e3287;"));
        m_title->setWordWrap(true);
        m_title->setContentsMargins(30, 14, 0, 6);
        wv->addWidget(m_title);
        m_body = new QWidget;
        m_bodyLayout = new QVBoxLayout(m_body);
        m_bodyLayout->setContentsMargins(30, 6, 0, 0);
        wv->addWidget(m_body, 1);
        outer->addWidget(white, 1);
        auto *line = new QFrame;
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
        outer->addWidget(line);
        auto *btnRow = new QHBoxLayout;
        btnRow->setContentsMargins(10, 10, 10, 10);
        btnRow->addStretch(1);
        m_next = new QPushButton(tr7("Next"));
        m_cancel = new QPushButton(tr7("Cancel"));
        for (QPushButton *b : {m_next, m_cancel}) {
            b->setMinimumWidth(75);
            btnRow->addWidget(b);
        }
        m_next->setDefault(true);
        outer->addLayout(btnRow);
        connect(m_back, &QToolButton::clicked, this, [this] { go(m_page - 1); });
        connect(m_next, &QPushButton::clicked, this, [this] {
            if (m_page == lastPage()) {
                applyRenderMode(m_mode);
                accept();
            } else {
                go(m_page + 1);
            }
        });
        connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
        resize(640, 560);
        go(0);
    }

private:
    // 0 turn on, 1 native resolution, then the sample steps, then done
    QList<int> steps() const
    {
        return m_mode.lcd ? QList<int>{0, 1, 2} : QList<int>{1};
    }
    int lastPage() const
    {
        return 2 + int(steps().size());
    }

    void clearBody()
    {
        while (QLayoutItem *it = m_bodyLayout->takeAt(0)) {
            if (it->widget()) {
                it->widget()->deleteLater();
            }
            if (it->layout()) {
                QLayoutItem *sub;
                while ((sub = it->layout()->takeAt(0))) {
                    if (sub->widget()) {
                        sub->widget()->deleteLater();
                    }
                    delete sub;
                }
            }
            delete it;
        }
    }

    void go(int page)
    {
        m_page = qBound(0, page, lastPage());
        m_back->setEnabled(m_page > 0);
        m_next->setText(m_page == lastPage() ? tr7("Done") : tr7("Next"));
        clearBody();
        if (m_page == 0) {
            m_title->setText(tr7("Make the text on your screen easier to read"));
            auto *on = new QCheckBox(tr7("Turn on ClearType"));
            on->setChecked(m_mode.lcd);
            m_bodyLayout->addWidget(on);
            auto *sample = new SampleBox(m_mode, m_font, 540, 4);
            m_bodyLayout->addWidget(sample);
            connect(on, &QCheckBox::toggled, this, [this](bool v) {
                m_mode.lcd = v;
                go(0);
            });
            auto *about = new QLabel(tr7("ClearType is a technology that improves the readability of text on LCD monitors, such as laptop screens and flat panel monitors. With ClearType, the words on your screen look almost as sharp and clear as those printed on paper."));
            about->setWordWrap(true);
            m_bodyLayout->addWidget(about);
        } else if (m_page == 1) {
            m_title->setText(tr7("Windows is making sure that your monitor is set to its native resolution..."));
            m_bodyLayout->addWidget(new QLabel(nativeResolutionText()));
        } else if (m_page < lastPage()) {
            const int step = m_page - 2;
            const int kind = steps().at(step);
            m_title->setText(tr7("Click the text sample that looks best to you (%1 of %2)").arg(step + 1).arg(steps().size()));
            QList<RenderMode> choices;
            int current = 0;
            for (int i = 0; i < 3; ++i) {
                RenderMode m = m_mode;
                if (kind == 0) {
                    if (i == 2) {
                        break;
                    }
                    m.bgr = i == 1;
                    if (m.bgr == m_mode.bgr) {
                        current = i;
                    }
                } else if (kind == 1) {
                    m.hinting = i;
                    if (i == m_mode.hinting) {
                        current = i;
                    }
                } else {
                    static const FT_LcdFilter filters[3] = {FT_LCD_FILTER_DEFAULT, FT_LCD_FILTER_LIGHT, FT_LCD_FILTER_LEGACY};
                    m.filter = filters[i];
                    if (m.filter == m_mode.filter) {
                        current = i;
                    }
                }
                choices << m;
            }
            auto *row = new QHBoxLayout;
            QList<SampleBox *> boxes;
            const int w = choices.size() == 2 ? 265 : 175;
            for (int i = 0; i < choices.size(); ++i) {
                auto *box = new SampleBox(choices.at(i), m_font, w, choices.size() == 2 ? 6 : 8);
                box->chosen = i == current;
                boxes << box;
                row->addWidget(box);
            }
            row->addStretch(1);
            m_bodyLayout->addLayout(row);
            for (int i = 0; i < boxes.size(); ++i) {
                boxes.at(i)->clicked = [this, boxes, choices, i] {
                    m_mode = choices.at(i);
                    for (SampleBox *b : boxes) {
                        b->chosen = b == boxes.at(i);
                        b->update();
                    }
                };
            }
        } else {
            m_title->setText(tr7("You have finished tuning the text on your monitor"));
            auto *note = new QLabel(tr7("The new text settings apply to programs opened after you click Finish."));
            note->setWordWrap(true);
            m_bodyLayout->addWidget(note);
        }
        m_bodyLayout->addStretch(1);
    }

    static QString nativeResolutionText()
    {
        QProcess xr;
        xr.start(QStringLiteral("xrandr"), {QStringLiteral("--current")});
        xr.waitForFinished(3000);
        QString monitor, current, preferred;
        for (const QString &line : QString::fromLocal8Bit(xr.readAllStandardOutput()).split(QLatin1Char('\n'))) {
            if (line.contains(QLatin1String(" connected"))) {
                if (!current.isEmpty()) {
                    break; // the first connected monitor is enough here
                }
                monitor = line.section(QLatin1Char(' '), 0, 0);
            } else if (!monitor.isEmpty() && line.startsWith(QLatin1String("   "))) {
                const QString mode = line.simplified().section(QLatin1Char(' '), 0, 0);
                if (line.contains(QLatin1Char('*'))) {
                    current = mode;
                }
                if (line.contains(QLatin1Char('+')) && preferred.isEmpty()) {
                    preferred = mode;
                }
            }
        }
        if (current.isEmpty()) {
            return tr7("The resolution of the monitor could not be determined.");
        }
        if (preferred.isEmpty() || preferred == current) {
            return tr7("Your monitor %1 is set to its native resolution (%2).").arg(monitor, current);
        }
        return tr7("Your monitor %1 is not set to its native resolution.\nCurrent: %2, native: %3. You can change it on the Screen Resolution page.")
            .arg(monitor, current, preferred);
    }

    RenderMode m_mode;
    RenderMode m_start;
    QString m_font;
    int m_page = 0;
    QToolButton *m_back;
    QLabel *m_title;
    QWidget *m_body;
    QVBoxLayout *m_bodyLayout;
    QPushButton *m_next;
    QPushButton *m_cancel;
};

} // namespace Win7Fonts
