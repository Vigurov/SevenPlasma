/*
 * Character Map (charmap.exe): every character of a font in the 20-column grid, the enlarged cell,
 * "Characters to copy", and the advanced view (Unicode subranges, search by name, go to U+).
 * Character names come from ICU (libicuuc, looked up at run time).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7fontdata.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFontComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QWidget>

#include <dlfcn.h>

namespace Win7Fonts
{

/** "LATIN CAPITAL LETTER A" -> "Latin Capital Letter A", as charmap's status bar writes it */
inline QString unicodeName(char32_t c)
{
    using CharName = int32_t (*)(int32_t, int, char *, int32_t, int *);
    static CharName fn = [] () -> CharName {
        for (const char *lib : {"libicuuc.so", "libicuuc.so.78", "libicuuc.so.77", "libicuuc.so.76", "libicuuc.so.75", "libicuuc.so.74", "libicuuc.so.72"}) {
            void *h = dlopen(lib, RTLD_LAZY);
            if (!h) {
                continue;
            }
            if (auto *f = reinterpret_cast<CharName>(dlsym(h, "u_charName"))) {
                return f;
            }
            for (int v = 90; v >= 50; --v) {
                if (auto *f = reinterpret_cast<CharName>(dlsym(h, QStringLiteral("u_charName_%1").arg(v).toLatin1().constData()))) {
                    return f;
                }
            }
        }
        return nullptr;
    }();
    if (!fn) {
        return {};
    }
    char buf[256];
    int err = 0;
    const int len = fn(int32_t(c), 0 /* U_UNICODE_CHAR_NAME */, buf, sizeof buf, &err);
    if (err > 0 || len <= 0) {
        return {};
    }
    QString s = QString::fromLatin1(buf, len).toLower();
    bool up = true;
    for (QChar &ch : s) {
        if (up && ch.isLetter()) {
            ch = ch.toUpper();
        }
        up = ch == QLatin1Char(' ') || ch == QLatin1Char('-');
    }
    return s;
}

/** charmap.exe's "Unicode Subrange" list */
inline const QList<std::tuple<QString, char32_t, char32_t>> &subranges()
{
    static const QList<std::tuple<QString, char32_t, char32_t>> r{
        {tr7("Latin"), 0x0020, 0x024f},
        {tr7("Spacing Modifier Letters"), 0x02b0, 0x02ff},
        {tr7("Combining Diacritical Marks"), 0x0300, 0x036f},
        {tr7("Greek"), 0x0370, 0x03ff},
        {tr7("Cyrillic"), 0x0400, 0x052f},
        {tr7("Armenian"), 0x0530, 0x058f},
        {tr7("Hebrew"), 0x0590, 0x05ff},
        {tr7("Arabic"), 0x0600, 0x06ff},
        {tr7("Devanagari"), 0x0900, 0x097f},
        {tr7("Bengali"), 0x0980, 0x09ff},
        {tr7("Gurmukhi"), 0x0a00, 0x0a7f},
        {tr7("Gujarati"), 0x0a80, 0x0aff},
        {tr7("Oriya"), 0x0b00, 0x0b7f},
        {tr7("Tamil"), 0x0b80, 0x0bff},
        {tr7("Telugu"), 0x0c00, 0x0c7f},
        {tr7("Kannada"), 0x0c80, 0x0cff},
        {tr7("Malayalam"), 0x0d00, 0x0d7f},
        {tr7("Thai"), 0x0e00, 0x0e7f},
        {tr7("Lao"), 0x0e80, 0x0eff},
        {tr7("Tibetan"), 0x0f00, 0x0fff},
        {tr7("Georgian"), 0x10a0, 0x10ff},
        {tr7("Hangul"), 0x1100, 0x11ff},
        {tr7("General Punctuation"), 0x2000, 0x206f},
        {tr7("Superscripts and Subscripts"), 0x2070, 0x209f},
        {tr7("Currency Symbols"), 0x20a0, 0x20cf},
        {tr7("Letterlike Symbols"), 0x2100, 0x214f},
        {tr7("Number Forms"), 0x2150, 0x218f},
        {tr7("Arrows"), 0x2190, 0x21ff},
        {tr7("Mathematical Operators"), 0x2200, 0x22ff},
        {tr7("Miscellaneous Technical"), 0x2300, 0x23ff},
        {tr7("Control Pictures and OCR"), 0x2400, 0x245f},
        {tr7("Enclosed Alphanumerics"), 0x2460, 0x24ff},
        {tr7("Box Drawing"), 0x2500, 0x257f},
        {tr7("Block Elements and Geometric Shapes"), 0x2580, 0x25ff},
        {tr7("Symbols and Dingbats"), 0x2600, 0x27bf},
        {tr7("CJK Symbols and Punctuation"), 0x3000, 0x303f},
        {tr7("Hiragana and Katakana"), 0x3040, 0x30ff},
        {tr7("Bopomofo"), 0x3100, 0x312f},
        {tr7("CJK Unified Ideographs"), 0x4e00, 0x9fff},
        {tr7("Hangul Syllables"), 0xac00, 0xd7af},
        {tr7("Private Use Characters"), 0xe000, 0xf8ff},
        {tr7("CJK Compatibility Forms"), 0xfe30, 0xfe4f},
        {tr7("Small Form Variants"), 0xfe50, 0xfe6f},
        {tr7("Halfwidth and Fullwidth Forms"), 0xff00, 0xffef},
    };
    return r;
}

/** Characters fontconfig says the family has */
inline QList<char32_t> fontCharacters(const QString &family)
{
    QList<char32_t> out;
    FcPattern *pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, reinterpret_cast<const FcChar8 *>(family.toUtf8().constData()));
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    FcPattern *match = FcFontMatch(nullptr, pat, &res);
    FcCharSet *cs = nullptr;
    if (match && FcPatternGetCharSet(match, FC_CHARSET, 0, &cs) == FcResultMatch) {
        FcChar32 map[FC_CHARSET_MAP_SIZE];
        FcChar32 next;
        for (FcChar32 base = FcCharSetFirstPage(cs, map, &next); base != FC_CHARSET_DONE; base = FcCharSetNextPage(cs, map, &next)) {
            for (int i = 0; i < FC_CHARSET_MAP_SIZE; ++i) {
                for (int b = 0; b < 32; ++b) {
                    if (map[i] & (1u << b)) {
                        const char32_t c = base + i * 32 + b;
                        if (c >= 0x20 && !(c >= 0x7f && c < 0xa0)) {
                            out << c;
                        }
                    }
                }
            }
        }
    }
    if (match) {
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pat);
    return out;
}

/** The grid: 20 columns of cells with grey lines, the chosen one enlarged while the button is down */
class CharGrid : public QWidget
{
public:
    static constexpr int kCols = 20;
    static constexpr int kRows = 10;
    static constexpr int kCell = 20;

    std::function<void(char32_t)> current; // status bar
    std::function<void(char32_t)> picked;  // double click: into "Characters to copy"
    QScrollBar *bar = nullptr;

    CharGrid()
    {
        setFixedSize(kCols * kCell + 1, kRows * kCell + 1);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setChars(const QList<char32_t> &chars, const QFont &font)
    {
        m_chars = chars;
        m_font = font;
        m_font.setStyleStrategy(QFont::NoFontMerging);
        m_font.setPixelSize(14);
        m_sel = chars.isEmpty() ? -1 : 0;
        const int rows = int((chars.size() + kCols - 1) / kCols);
        bar->setRange(0, qMax(0, rows - kRows));
        bar->setPageStep(kRows);
        bar->setValue(0);
        notify();
        update();
    }

    char32_t selected() const
    {
        return m_sel >= 0 && m_sel < m_chars.size() ? m_chars.at(m_sel) : 0;
    }

    void select(char32_t c)
    {
        const int i = int(m_chars.indexOf(c));
        if (i >= 0) {
            m_sel = i;
            ensureVisible();
            notify();
            update();
        }
    }

protected:
    int first() const
    {
        return bar->value() * kCols;
    }
    QRect cellRect(int i) const
    {
        const int k = i - first();
        return QRect((k % kCols) * kCell, (k / kCols) * kCell, kCell, kCell);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setPen(QColor(0xa0, 0xa0, 0xa0));
        for (int c = 0; c <= kCols; ++c) {
            p.drawLine(c * kCell, 0, c * kCell, kRows * kCell);
        }
        for (int r = 0; r <= kRows; ++r) {
            p.drawLine(0, r * kCell, kCols * kCell, r * kCell);
        }
        p.setFont(m_font);
        for (int i = first(); i < qMin(int(m_chars.size()), first() + kCols * kRows); ++i) {
            const QRect r = cellRect(i).adjusted(1, 1, 0, 0);
            const bool sel = i == m_sel;
            if (sel) {
                p.fillRect(r, hasFocus() ? QColor(0x33, 0x99, 0xff) : QColor(0xd9, 0xd9, 0xd9));
            }
            p.setPen(sel && hasFocus() ? Qt::white : Qt::black);
            p.drawText(r, Qt::AlignCenter, QString::fromUcs4(&m_chars.at(i), 1));
        }
        // the enlarged cell, like charmap while the mouse button is held
        if (m_zoom && m_sel >= first() && m_sel < first() + kCols * kRows) {
            const QRect c = cellRect(m_sel);
            QRect z(0, 0, kCell * 2 + 10, kCell * 2 + 10);
            z.moveCenter(c.center());
            z.moveLeft(qBound(0, z.left(), width() - z.width() - 1));
            z.moveTop(qBound(0, z.top(), height() - z.height() - 1));
            p.fillRect(z, Qt::white);
            p.setPen(Qt::black);
            p.drawRect(z);
            p.drawRect(z.adjusted(1, 1, -1, -1));
            QFont big = m_font;
            big.setPixelSize(32);
            p.setFont(big);
            p.drawText(z, Qt::AlignCenter, QString::fromUcs4(&m_chars.at(m_sel), 1));
        }
    }

    int indexAt(QPoint pos) const
    {
        if (pos.x() < 0 || pos.y() < 0 || pos.x() >= kCols * kCell || pos.y() >= kRows * kCell) {
            return -1;
        }
        const int i = first() + (pos.y() / kCell) * kCols + pos.x() / kCell;
        return i < m_chars.size() ? i : -1;
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const int i = indexAt(e->position().toPoint());
        if (i >= 0) {
            m_sel = i;
            m_zoom = true;
            notify();
            update();
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!m_zoom) {
            return;
        }
        const int i = indexAt(e->position().toPoint());
        if (i >= 0 && i != m_sel) {
            m_sel = i;
            notify();
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent *) override
    {
        m_zoom = false;
        update();
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        const int i = indexAt(e->position().toPoint());
        if (i >= 0 && picked) {
            picked(m_chars.at(i));
        }
    }
    void wheelEvent(QWheelEvent *e) override
    {
        bar->setValue(bar->value() + (e->angleDelta().y() < 0 ? 3 : -3));
        update();
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        int d = 0;
        switch (e->key()) {
        case Qt::Key_Left:
            d = -1;
            break;
        case Qt::Key_Right:
            d = 1;
            break;
        case Qt::Key_Up:
            d = -kCols;
            break;
        case Qt::Key_Down:
            d = kCols;
            break;
        case Qt::Key_PageUp:
            d = -kCols * kRows;
            break;
        case Qt::Key_PageDown:
            d = kCols * kRows;
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            if (picked && selected()) {
                picked(selected());
            }
            return;
        default:
            QWidget::keyPressEvent(e);
            return;
        }
        m_sel = qBound(0, m_sel + d, int(m_chars.size()) - 1);
        ensureVisible();
        notify();
        update();
    }
    void focusInEvent(QFocusEvent *) override
    {
        update();
    }
    void focusOutEvent(QFocusEvent *) override
    {
        update();
    }

private:
    void ensureVisible()
    {
        const int row = m_sel / kCols;
        if (row < bar->value()) {
            bar->setValue(row);
        } else if (row >= bar->value() + kRows) {
            bar->setValue(row - kRows + 1);
        }
    }
    void notify()
    {
        if (current) {
            current(selected());
        }
    }

    QList<char32_t> m_chars;
    QFont m_font;
    int m_sel = -1;
    bool m_zoom = false;
};

class CharMap : public QWidget
{
public:
    CharMap()
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(tr7("Character Map"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-charmap")));
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(8, 8, 8, 0);
        v->setSpacing(6);
        auto *top = new QHBoxLayout;
        top->addWidget(new QLabel(tr7("Font:")));
        m_font = new QFontComboBox;
        m_font->setCurrentFont(QFont(QStringLiteral("Arial")));
        top->addWidget(m_font, 1);
        v->addLayout(top);

        auto *gridRow = new QHBoxLayout;
        gridRow->setSpacing(0);
        m_grid = new CharGrid;
        m_bar = new QScrollBar(Qt::Vertical);
        m_grid->bar = m_bar;
        gridRow->addWidget(m_grid);
        gridRow->addWidget(m_bar);
        gridRow->addStretch(1);
        v->addLayout(gridRow);

        auto *copyRow = new QHBoxLayout;
        copyRow->addWidget(new QLabel(tr7("Characters to copy:")));
        m_copy = new QLineEdit;
        QFont cf = m_copy->font();
        cf.setPixelSize(15);
        m_copy->setFont(cf);
        copyRow->addWidget(m_copy, 1);
        auto *select = new QPushButton(tr7("Select"));
        auto *copy = new QPushButton(tr7("Copy"));
        copyRow->addWidget(select);
        copyRow->addWidget(copy);
        v->addLayout(copyRow);

        m_advanced = new QCheckBox(tr7("Advanced view"));
        v->addWidget(m_advanced);
        m_adv = new QWidget;
        auto *ag = new QGridLayout(m_adv);
        ag->setContentsMargins(0, 0, 0, 0);
        ag->addWidget(new QLabel(tr7("Character set:")), 0, 0);
        auto *charset = new QComboBox;
        charset->addItem(tr7("Unicode"));
        ag->addWidget(charset, 0, 1);
        ag->addWidget(new QLabel(tr7("Go to Unicode:")), 0, 2);
        m_goto = new QLineEdit;
        m_goto->setPlaceholderText(QStringLiteral("U+"));
        m_goto->setMaximumWidth(80);
        ag->addWidget(m_goto, 0, 3);
        ag->addWidget(new QLabel(tr7("Group by:")), 1, 0);
        m_group = new QComboBox;
        m_group->addItems({tr7("All"), tr7("Unicode Subrange")});
        ag->addWidget(m_group, 1, 1);
        ag->addWidget(new QLabel(tr7("Search for:", "character map")), 2, 0);
        m_search = new QLineEdit;
        ag->addWidget(m_search, 2, 1);
        m_searchBtn = new QPushButton(tr7("Search"));
        ag->addWidget(m_searchBtn, 2, 2);
        ag->setColumnStretch(1, 1);
        m_adv->hide();
        v->addWidget(m_adv);

        m_status = new QLabel;
        m_status->setContentsMargins(4, 3, 4, 3);
        m_status->setStyleSheet(QStringLiteral("QLabel { border-top: 1px solid #d5d5d5; }"));
        v->addWidget(m_status);

        m_ranges = new QListWidget;
        m_ranges->setWindowFlags(Qt::Tool);
        m_ranges->setWindowTitle(tr7("Unicode Subrange"));
        m_ranges->addItem(tr7("All"));
        for (const auto &r : subranges()) {
            m_ranges->addItem(std::get<0>(r));
        }
        m_ranges->resize(200, 320);

        connect(m_font, &QFontComboBox::currentFontChanged, this, [this] { refill(); });
        connect(m_bar, &QScrollBar::valueChanged, m_grid, qOverload<>(&QWidget::update));
        m_grid->current = [this](char32_t c) {
            if (!c) {
                m_status->clear();
                return;
            }
            const QString name = unicodeName(c);
            m_status->setText(QStringLiteral("U+%1%2").arg(QString::number(uint(c), 16).toUpper().rightJustified(4, QLatin1Char('0')),
                                                           name.isEmpty() ? QString() : QStringLiteral(": ") + name));
        };
        m_grid->picked = [this](char32_t c) {
            m_copy->insert(QString::fromUcs4(&c, 1));
        };
        connect(select, &QPushButton::clicked, this, [this] {
            if (const char32_t c = m_grid->selected()) {
                m_copy->insert(QString::fromUcs4(&c, 1));
            }
        });
        connect(copy, &QPushButton::clicked, this, [this] {
            QApplication::clipboard()->setText(m_copy->text().isEmpty() && m_grid->selected() ? QString::fromUcs4(std::array{m_grid->selected()}.data(), 1) : m_copy->text());
        });
        connect(m_advanced, &QCheckBox::toggled, this, [this](bool on) {
            m_adv->setVisible(on);
            if (!on) {
                m_ranges->hide();
                m_group->setCurrentIndex(0);
                m_searched.clear();
                m_searchBtn->setText(tr7("Search"));
                refill();
            }
            QTimer::singleShot(0, this, [this] { adjustSize(); });
        });
        connect(m_group, &QComboBox::currentIndexChanged, this, [this](int i) {
            if (i == 1) {
                m_ranges->move(frameGeometry().topRight() + QPoint(8, 0));
                m_ranges->show();
            } else {
                m_ranges->hide();
                m_range = -1;
                refill();
            }
        });
        connect(m_ranges, &QListWidget::currentRowChanged, this, [this](int row) {
            m_range = row - 1;
            refill();
        });
        connect(m_searchBtn, &QPushButton::clicked, this, [this] {
            if (!m_searched.isEmpty()) {
                m_searched.clear();
                m_search->clear();
                m_searchBtn->setText(tr7("Search"));
            } else if (!m_search->text().trimmed().isEmpty()) {
                m_searched = m_search->text().trimmed();
                m_searchBtn->setText(tr7("Reset"));
            }
            refill();
        });
        connect(m_search, &QLineEdit::returnPressed, m_searchBtn, &QPushButton::click);
        connect(m_goto, &QLineEdit::returnPressed, this, [this] {
            bool ok = false;
            const uint c = QString(m_goto->text()).remove(QStringLiteral("U+"), Qt::CaseInsensitive).trimmed().toUInt(&ok, 16);
            if (ok) {
                m_grid->select(char32_t(c));
            }
        });
        refill();
        adjustSize();
        setFixedWidth(sizeHint().width());
    }

protected:
    void closeEvent(QCloseEvent *e) override
    {
        m_ranges->close();
        delete m_ranges;
        m_ranges = nullptr;
        QWidget::closeEvent(e);
    }

private:
    void refill()
    {
        QList<char32_t> chars = fontCharacters(m_font->currentFont().family());
        if (m_range >= 0 && m_range < subranges().size()) {
            const auto &[name, lo, hi] = subranges().at(m_range);
            QList<char32_t> in;
            for (char32_t c : std::as_const(chars)) {
                if (c >= lo && c <= hi) {
                    in << c;
                }
            }
            chars = in;
        }
        if (!m_searched.isEmpty()) {
            QList<char32_t> found;
            for (char32_t c : std::as_const(chars)) {
                if (unicodeName(c).contains(m_searched, Qt::CaseInsensitive)) {
                    found << c;
                }
            }
            chars = found;
        }
        m_grid->setChars(chars, m_font->currentFont());
    }

    QFontComboBox *m_font;
    CharGrid *m_grid;
    QScrollBar *m_bar;
    QLineEdit *m_copy;
    QCheckBox *m_advanced;
    QWidget *m_adv;
    QLineEdit *m_goto;
    QComboBox *m_group;
    QLineEdit *m_search;
    QPushButton *m_searchBtn;
    QLabel *m_status;
    QListWidget *m_ranges;
    int m_range = -1;
    QString m_searched;
};

} // namespace Win7Fonts
