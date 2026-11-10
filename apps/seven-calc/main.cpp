/*
 * Calculator in the style of Windows 7.
 *
 * Modes: Standard (immediate execution), Scientific and Programmer (operator precedence,
 * parentheses), Statistics. History, digit grouping, and the extended panels:
 * Unit conversion, Date calculation and the worksheets (Mortgage, Vehicle lease, Fuel economy).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../seven-common/win7wrapmenubar.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCollator>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <optional>

namespace
{
using Num = long double; // 64 bit mantissa: exact for the 64 bit integers of the Programmer mode

const QChar kComma(QLatin1Char(','));
const Num kPi = 3.14159265358979323846264338327950288L;

enum Mode {
    Standard,
    Scientific,
    Programmer,
    Statistics,
};

enum Panel {
    BasicPanel,
    UnitsPanel,
    DatesPanel,
    MortgagePanel,
    LeasePanel,
    FuelMpgPanel,
    FuelLitresPanel,
};

/* ---------------- number text ---------------- */

/** "1.5e+20" -> mantissa "1,5", exponent text "e+20" */
QString normalizeExponent(QString s)
{
    const int e = s.indexOf(QLatin1Char('e'));
    if (e < 0) {
        return s;
    }
    QString m = s.left(e);
    const int ex = s.mid(e + 1).toInt();
    if (m.contains(QLatin1Char('.'))) {
        while (m.endsWith(QLatin1Char('0'))) {
            m.chop(1);
        }
    } else {
        m += QLatin1Char('.'); // Windows writes "1,e+20"
    }
    return m + QStringLiteral("e") + (ex >= 0 ? QStringLiteral("+") : QStringLiteral("-")) + QString::number(std::abs(ex));
}

QString numToText(Num x, bool fe, int digits = 16)
{
    const double d = double(x);
    if (!std::isfinite(d) || d == 0) {
        return QStringLiteral("0");
    }
    QString s = fe ? normalizeExponent(QString::number(d, 'e', digits - 1)) : normalizeExponent(QString::number(d, 'g', digits));
    s.replace(QLatin1Char('.'), kComma);
    return s;
}

/** Digit grouping of a decimal number: "1234567,89" -> "1 234 567,89" */
QString groupDecimal(const QString &raw)
{
    int start = raw.startsWith(QLatin1Char('-')) ? 1 : 0;
    int end = start;
    while (end < raw.size() && raw.at(end).isDigit()) {
        ++end;
    }
    QString intPart = raw.mid(start, end - start);
    for (int i = intPart.size() - 3; i > 0; i -= 3) {
        intPart.insert(i, QLatin1Char(' '));
    }
    return raw.left(start) + intPart + raw.mid(end);
}

QString groupEvery(const QString &raw, int n)
{
    QString s = raw;
    const int start = s.startsWith(QLatin1Char('-')) ? 1 : 0;
    for (int i = s.size() - n; i > start; i -= n) {
        s.insert(i, QLatin1Char(' '));
    }
    return s;
}

std::optional<double> parseInput(QString text)
{
    text = text.trimmed().remove(QLatin1Char(' ')).remove(QChar(0x00a0));
    text.replace(kComma, QLatin1Char('.'));
    bool ok = false;
    const double v = text.toDouble(&ok);
    if (!ok || !std::isfinite(v)) {
        return std::nullopt;
    }
    return v;
}

QString resultText(double v)
{
    return numToText(v, false, 16);
}

/* ---------------- widgets ---------------- */

/** Expression line, memory "M", parentheses count and the big number */
class NumberView : public QWidget
{
public:
    NumberView()
    {
        setFixedHeight(48);
    }
    void set(const QString &number, const QString &expression, bool memory, int parens)
    {
        m_number = number;
        m_expression = expression;
        m_memory = memory;
        m_parens = parens;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QFont small(QStringLiteral("DM Mono"));
        small.setPixelSize(11);
        p.setFont(small);
        p.setPen(QColor(0x4a, 0x4a, 0x4a));
        const QFontMetrics sfm(small);
        p.drawText(QRect(6, 1, width() - 12, sfm.height()), Qt::AlignRight | Qt::AlignVCenter, sfm.elidedText(m_expression, Qt::ElideLeft, width() - 24));
        QString flags;
        if (m_memory) {
            flags += QStringLiteral("M ");
        }
        if (m_parens > 0) {
            flags += QStringLiteral("(=%1").arg(m_parens);
        }
        p.drawText(QRect(6, height() - sfm.height() - 4, 60, sfm.height()), Qt::AlignLeft | Qt::AlignVCenter, flags);

        // digits like Consolas: a wide mono font, and the zero drawn by hand with the slash inside the oval
        QFont big(QStringLiteral("DejaVu Sans Mono"));
        int px = 26;
        big.setPixelSize(px);
        while (px > 11 && QFontMetrics(big).horizontalAdvance(m_number) > width() - 40) {
            big.setPixelSize(--px);
        }
        p.setFont(big);
        const QColor ink(0x30, 0x30, 0x30);
        p.setPen(ink);
        const QFontMetricsF fm(big);
        const QRectF area(6, 14, width() - 12, height() - 16);
        const double total = fm.horizontalAdvance(m_number);
        double x = area.right() - total;
        const double baseline = area.center().y() + (fm.ascent() - fm.descent()) / 2;
        const double digitHeight = fm.tightBoundingRect(QStringLiteral("8")).height();
        for (const QChar ch : m_number) {
            const double cw = fm.horizontalAdvance(ch);
            if (ch == QLatin1Char('0')) {
                const double stroke = qMax(1.4, px * 0.11);
                const double ow = qMin(cw * 1.02, digitHeight * 0.74); // Consolas zero is a round oval
                const QRectF oval(x + (cw - ow) / 2 + stroke / 2, baseline - digitHeight + stroke / 2, ow - stroke, digitHeight - stroke);
                p.save();
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(QPen(ink, stroke));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(oval);
                p.setPen(QPen(ink, stroke * 0.9, Qt::SolidLine, Qt::FlatCap));
                p.drawLine(QPointF(oval.left() + oval.width() * 0.2, oval.bottom() - oval.height() * 0.3),
                           QPointF(oval.right() - oval.width() * 0.2, oval.top() + oval.height() * 0.3));
                p.restore();
            } else {
                p.drawText(QPointF(x, baseline), QString(ch));
            }
            x += cw;
        }
    }

private:
    QString m_number = QStringLiteral("0");
    QString m_expression;
    bool m_memory = false;
    int m_parens = 0;
};

/** List of the display that can tell whether a line is being edited */
class HistoryList : public QListWidget
{
public:
    bool isEditing() const
    {
        return state() == QAbstractItemView::EditingState;
    }
};

/** White display with the light gradient; holds the history / statistics list when shown */
class DisplayBox : public QFrame
{
public:
    DisplayBox()
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(4, 3, 4, 3);
        v->setSpacing(1);
        auto *top = new QHBoxLayout;
        top->setSpacing(2);
        top->addStretch(1);
        up = new QToolButton;
        down = new QToolButton;
        for (QToolButton *b : {up, down}) {
            b->setAutoRaise(true);
            b->setFixedSize(16, 14);
            b->setFocusPolicy(Qt::NoFocus);
            b->setStyleSheet(QStringLiteral("QToolButton { border: none; font-size: 8px; color: #1e1e1e; background: transparent; }"
                                            "QToolButton:hover { color: #3c7fb1; }"));
        }
        up->setText(QStringLiteral("▲"));
        down->setText(QStringLiteral("▼"));
        top->addWidget(up);
        top->addWidget(down);
        topBar = new QWidget;
        topBar->setLayout(top);
        v->addWidget(topBar);
        list = new HistoryList;
        list->setFrameShape(QFrame::NoFrame);
        list->setFocusPolicy(Qt::ClickFocus);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        QFont lf(QStringLiteral("DM Mono"));
        lf.setPixelSize(11);
        list->setFont(lf);
        list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; color: #6a6a6a; }"
                                           "QListWidget::item { padding: 0px 2px; }"
                                           "QListWidget::item:selected { background: #8ec0ec; color: #1e1e1e; }"));
        list->setFixedHeight(104);
        v->addWidget(list);
        line = new QFrame;
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background: #c9d3df;"));
        v->addWidget(line);
        count = new QLabel;
        QFont cf = count->font();
        cf.setPixelSize(11);
        count->setFont(cf);
        v->addWidget(count);
        number = new NumberView;
        v->addWidget(number);
        connect(up, &QToolButton::clicked, list, [this] {
            list->setCurrentRow(qMax(0, list->currentRow() - 1));
        });
        connect(down, &QToolButton::clicked, list, [this] {
            list->setCurrentRow(qMin(list->count() - 1, list->currentRow() + 1));
        });
        showList(false, false);
    }
    void showList(bool shown, bool withCount)
    {
        topBar->setVisible(shown);
        list->setVisible(shown);
        line->setVisible(shown);
        count->setVisible(withCount);
    }

    QToolButton *up;
    QToolButton *down;
    QWidget *topBar;
    HistoryList *list;
    QFrame *line;
    QLabel *count;
    NumberView *number;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor(0xe4, 0xe9, 0xef));
        g.setColorAt(0.35, QColor(0xf6, 0xf8, 0xfa));
        g.setColorAt(1, QColor(0xff, 0xff, 0xff));
        p.setBrush(g);
        p.setPen(QColor(0x8e, 0x9f, 0xb4));
        p.drawRoundedRect(r, 2, 2);
    }
};

/** 64 bits in two rows of nibbles; clicking a bit flips it */
class BitView : public QWidget
{
public:
    std::function<void(int)> toggled;

    BitView()
    {
        setFixedHeight(64);
        m_font = QFont(QStringLiteral("DM Mono"));
        m_font.setPixelSize(11);
    }
    void setValue(quint64 v, int bits)
    {
        m_value = v;
        m_bits = bits;
        update();
    }

protected:
    QRectF bitRect(int bit) const
    {
        const QFontMetricsF fm(m_font);
        const double cw = fm.horizontalAdvance(QLatin1Char('0'));
        const int row = bit >= 32 ? 0 : 1;
        const int inRow = (row == 0 ? 63 : 31) - bit; // 0..31 from the left
        const int group = inRow / 4;
        const double gw = (width() - 12) / 8.0;
        const double x = 6 + group * gw + (inRow % 4) * cw;
        const double y = 4 + row * 30;
        return QRectF(x, y, cw, fm.height());
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor(0xf0, 0xf4, 0xf9));
        g.setColorAt(1, QColor(0xe6, 0xec, 0xf3));
        p.setBrush(g);
        p.setPen(QColor(0x9a, 0xa8, 0xba));
        p.drawRoundedRect(r, 2, 2);
        p.setFont(m_font);
        for (int bit = 63; bit >= 0; --bit) {
            p.setPen(bit < m_bits ? QColor(0x1e, 0x1e, 0x1e) : QColor(0xa8, 0xb0, 0xbb));
            p.drawText(bitRect(bit), Qt::AlignCenter, ((m_value >> bit) & 1) ? QStringLiteral("1") : QStringLiteral("0"));
        }
        QFont small = m_font;
        small.setPixelSize(10);
        p.setFont(small);
        p.setPen(QColor(0x60, 0x68, 0x72));
        const QFontMetricsF fm(m_font);
        for (int row = 0; row < 2; ++row) {
            const int hi = row == 0 ? 63 : 31;
            const double y = 4 + row * 30 + fm.height();
            const QRectF first = bitRect(hi);
            const QRectF mid = bitRect(hi - 16);
            const QRectF last = bitRect(hi - 31);
            p.drawText(QRectF(first.left(), y, 30, 12), Qt::AlignLeft | Qt::AlignTop, QString::number(hi));
            p.drawText(QRectF(mid.left(), y, 30, 12), Qt::AlignLeft | Qt::AlignTop, QString::number(hi - 16));
            p.drawText(QRectF(last.right() - 30, y, 30, 12), Qt::AlignRight | Qt::AlignTop, QString::number(hi - 31));
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        for (int bit = 0; bit < m_bits; ++bit) {
            if (bitRect(bit).adjusted(-1, -2, 1, 2).contains(e->position()) && toggled) {
                toggled(bit);
                return;
            }
        }
    }

private:
    QFont m_font;
    quint64 m_value = 0;
    int m_bits = 64;
};

/** Light blue rounded calculator key */
class Key : public QPushButton
{
public:
    Key(const QString &text, QWidget *parent = nullptr)
        : QPushButton(text, parent)
    {
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
        QFont f = font();
        f.setPixelSize(text.size() > 3 ? 11 : (text.size() > 1 ? 12 : 13));
        if (text.contains(QLatin1Char('x')) && text != QLatin1String("Xor")) {
            f.setItalic(true);
        }
        setFont(f);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient g(0, 0, 0, height());
        const bool on = isEnabled();
        // Windows 7 calculator keys glow yellow-orange under the mouse
        if (on && (isDown() || isChecked())) {
            // pressed: the full yellow-orange
            g.setColorAt(0, QColor(0xfa, 0xe6, 0x9a));
            g.setColorAt(0.45, QColor(0xfd, 0xd0, 0x6c));
            g.setColorAt(0.55, QColor(0xfc, 0xcf, 0x68));
            g.setColorAt(1, QColor(0xff, 0xee, 0xa2));
            p.setPen(QColor(0xd8, 0xb2, 0x5c));
        } else if (on && underMouse()) {
            // hover: a pale yellow glow
            g.setColorAt(0, QColor(0xfe, 0xf8, 0xe2));
            g.setColorAt(0.45, QColor(0xfd, 0xee, 0xc4));
            g.setColorAt(0.55, QColor(0xfc, 0xea, 0xb8));
            g.setColorAt(1, QColor(0xfe, 0xf5, 0xd6));
            p.setPen(QColor(0xe2, 0xcd, 0x92));
        } else if (m_light) {
            // digits and the comma are whiter than the other keys in Windows 7
            g.setColorAt(0, QColor(0xff, 0xff, 0xff));
            g.setColorAt(0.5, QColor(0xf7, 0xf9, 0xfc));
            g.setColorAt(0.51, QColor(0xee, 0xf2, 0xf7));
            g.setColorAt(1, QColor(0xe7, 0xec, 0xf3));
            p.setPen(on ? QColor(0x9a, 0xa8, 0xba) : QColor(0xb8, 0xc3, 0xd0));
        } else if (m_memory) {
            // the memory row is a little bluer and darker (colours taken from Windows 7)
            g.setColorAt(0, QColor(0xec, 0xf1, 0xf5));
            g.setColorAt(0.5, QColor(0xe4, 0xea, 0xef));
            g.setColorAt(0.51, QColor(0xdd, 0xe4, 0xea));
            g.setColorAt(1, QColor(0xd4, 0xdc, 0xe4));
            p.setPen(on ? QColor(0x9a, 0xa8, 0xba) : QColor(0xb8, 0xc3, 0xd0));
        } else {
            g.setColorAt(0, QColor(0xf3, 0xf8, 0xfc));
            g.setColorAt(0.5, QColor(0xee, 0xf4, 0xf8));
            g.setColorAt(0.51, QColor(0xe8, 0xef, 0xf4));
            g.setColorAt(1, QColor(0xdd, 0xe6, 0xee));
            p.setPen(on ? QColor(0x9a, 0xa8, 0xba) : QColor(0xb8, 0xc3, 0xd0));
        }
        p.setBrush(g);
        p.drawRoundedRect(r, 3, 3);
        p.setPen(QColor(255, 255, 255, 170));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 2, 2);
        // Windows 7 draws the labels in soft grey, the digits even lighter
        const QColor ink = !on ? QColor(0xa4, 0xae, 0xbb) : m_light ? QColor(0x4a, 0x4d, 0x52) : QColor(0x36, 0x3a, 0x40);
        const QPointF c = QRectF(rect()).center();
        if (text() == QStringLiteral("-")) {
            // thick minus like Windows 7
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            p.drawRect(QRectF(c.x() - 2.5, c.y() - 0.6, 5, 1.4));
            return;
        }
        if (text() == QStringLiteral("←")) {
            // bold arrow: thick shaft and a solid head
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            p.drawRect(QRectF(c.x() - 5, c.y() - 0.8, 12, 1.7));
            p.drawPolygon(QPolygonF({QPointF(c.x() - 10, c.y()), QPointF(c.x() - 4.5, c.y() - 3.6), QPointF(c.x() - 4.5, c.y() + 3.6)}));
            return;
        }
        p.setPen(ink);
        if (text() == QLatin1String("1/x")) {
            // "1/" upright, "x" in italic serif like Windows 7
            QFont upright = font();
            upright.setItalic(false);
            QFont xf(QStringLiteral("DejaVu Serif"));
            xf.setPixelSize(13);
            xf.setItalic(true);
            const int w1 = QFontMetrics(upright).horizontalAdvance(QStringLiteral("1/"));
            const int w2 = QFontMetrics(xf).horizontalAdvance(QLatin1Char('x'));
            const int x0 = (width() - w1 - w2) / 2;
            p.setFont(upright);
            p.drawText(QRect(x0, 0, w1, height()), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("1/"));
            p.setFont(xf);
            p.drawText(QRect(x0 + w1, 0, w2 + 2, height()), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("x"));
            return;
        }
        QFont f = font();
        if (m_light && text() != QString(kComma)) {
            f.setPixelSize(16); // digits are bigger than the function labels in Windows 7
        }
        if (text() == QLatin1String("=")) {
            f.setPixelSize(20);
            f.setBold(true);
        }
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, text());
    }

public:
    bool m_light = false;
    bool m_memory = false;
};

/** Frame with radio buttons (angle units, number base, word size) */
QFrame *radioFrame()
{
    auto *f = new QFrame;
    f->setObjectName(QStringLiteral("win7calcradios"));
    f->setStyleSheet(QStringLiteral("#win7calcradios { border: 1px solid #9aa8ba; border-radius: 3px;"
                                    " background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f4f7fb, stop:1 #e3eaf2); }"));
    return f;
}

/* ---------------- unit conversion data ---------------- */

struct Unit {
    QString name;
    double factor; // to the base unit of the category
};

struct Category {
    QString name;
    QList<Unit> units;
    bool temperature = false;
};

QList<Category> categories()
{
    QList<Category> list{
        {QStringLiteral("Вага/Маса"),
         {{QStringLiteral("Гектограм"), 0.1}, {QStringLiteral("Грам"), 0.001}, {QStringLiteral("Декаграм"), 0.01}, {QStringLiteral("Дециграм"), 0.0001},
          {QStringLiteral("Довга тонна (1016 кг)"), 1016.0469088}, {QStringLiteral("Карат"), 0.0002}, {QStringLiteral("Кілограм"), 1},
          {QStringLiteral("Коротка тонна (~907 кг)"), 907.18474}, {QStringLiteral("Міліграм"), 1e-6}, {QStringLiteral("Сантиграм"), 1e-5},
          {QStringLiteral("Стоун (14 фунтів)"), 6.35029318}, {QStringLiteral("Тонна"), 1000}, {QStringLiteral("Унція"), 0.028349523125},
          {QStringLiteral("Фунт"), 0.45359237}}},
        {QStringLiteral("Час"),
         {{QStringLiteral("Мікросекунда"), 1e-6}, {QStringLiteral("Мілісекунда"), 1e-3}, {QStringLiteral("Секунда"), 1}, {QStringLiteral("Хвилина"), 60},
          {QStringLiteral("Година"), 3600}, {QStringLiteral("День"), 86400}, {QStringLiteral("Тиждень"), 604800}}},
        {QStringLiteral("Тиск"),
         {{QStringLiteral("Атмосфера"), 101325}, {QStringLiteral("Бар"), 1e5}, {QStringLiteral("Кілопаскаль"), 1000},
          {QStringLiteral("Міліметр ртутного стовпа"), 133.322387415}, {QStringLiteral("Паскаль"), 1},
          {QStringLiteral("Фунт на квадратний дюйм"), 6894.757293168}}},
        {QStringLiteral("Довжина"),
         {{QStringLiteral("Ангстрем"), 1e-10}, {QStringLiteral("Нанометр"), 1e-9}, {QStringLiteral("Мікрон"), 1e-6}, {QStringLiteral("Міліметр"), 1e-3},
          {QStringLiteral("Сантиметр"), 0.01}, {QStringLiteral("Метр"), 1}, {QStringLiteral("Кілометр"), 1000}, {QStringLiteral("Дюйм"), 0.0254},
          {QStringLiteral("Фут"), 0.3048}, {QStringLiteral("Ярд"), 0.9144}, {QStringLiteral("Миля"), 1609.344}, {QStringLiteral("Морська миля"), 1852},
          {QStringLiteral("Морська сажень"), 1.8288}, {QStringLiteral("Ланцюг"), 20.1168}, {QStringLiteral("Род"), 5.0292}}},
        {QStringLiteral("Потужність"),
         {{QStringLiteral("Ват"), 1}, {QStringLiteral("Кіловат"), 1000}, {QStringLiteral("Кінська сила"), 745.69987158227},
          {QStringLiteral("Метрична кінська сила"), 735.49875}, {QStringLiteral("Фут-фунт за хвилину"), 0.0225969658},
          {QStringLiteral("Британська теплова одиниця за хвилину"), 17.58426666}}},
        {QStringLiteral("Об'єм"),
         {{QStringLiteral("Кубічний сантиметр"), 1e-6}, {QStringLiteral("Кубічний метр"), 1}, {QStringLiteral("Літр"), 1e-3},
          {QStringLiteral("Мілілітр"), 1e-6}, {QStringLiteral("Кубічний дюйм"), 1.6387064e-5}, {QStringLiteral("Кубічний фут"), 0.028316846592},
          {QStringLiteral("Кубічний ярд"), 0.764554857984}, {QStringLiteral("Галон (США)"), 3.785411784e-3},
          {QStringLiteral("Галон (Великобританія)"), 4.54609e-3}, {QStringLiteral("Кварта (США)"), 9.46352946e-4},
          {QStringLiteral("Пінта (США)"), 4.73176473e-4}, {QStringLiteral("Чашка (США)"), 2.365882365e-4},
          {QStringLiteral("Рідка унція (США)"), 2.95735295625e-5}, {QStringLiteral("Столова ложка (США)"), 1.478676478125e-5},
          {QStringLiteral("Чайна ложка (США)"), 4.92892159375e-6}}},
        {QStringLiteral("Площа"),
         {{QStringLiteral("Квадратний міліметр"), 1e-6}, {QStringLiteral("Квадратний сантиметр"), 1e-4}, {QStringLiteral("Квадратний метр"), 1},
          {QStringLiteral("Гектар"), 1e4}, {QStringLiteral("Квадратний кілометр"), 1e6}, {QStringLiteral("Квадратний дюйм"), 6.4516e-4},
          {QStringLiteral("Квадратний фут"), 0.09290304}, {QStringLiteral("Квадратний ярд"), 0.83612736}, {QStringLiteral("Акр"), 4046.8564224},
          {QStringLiteral("Квадратна миля"), 2589988.110336}}},
        {QStringLiteral("Швидкість"),
         {{QStringLiteral("Сантиметр за секунду"), 0.01}, {QStringLiteral("Метр за секунду"), 1}, {QStringLiteral("Кілометр за годину"), 1 / 3.6},
          {QStringLiteral("Фут за секунду"), 0.3048}, {QStringLiteral("Миля за годину"), 0.44704}, {QStringLiteral("Вузол"), 1852.0 / 3600.0},
          {QStringLiteral("Мах"), 340.3}}},
        {QStringLiteral("Температура"),
         {{QStringLiteral("Градус Цельсія"), 0}, {QStringLiteral("Градус Фаренгейта"), 0}, {QStringLiteral("Кельвін"), 0}},
         true},
        {QStringLiteral("Кут"), {{QStringLiteral("Градус"), M_PI / 180}, {QStringLiteral("Радіан"), 1}, {QStringLiteral("Град"), M_PI / 200}}},
        {QStringLiteral("Енергія"),
         {{QStringLiteral("Джоуль"), 1}, {QStringLiteral("Кілоджоуль"), 1000}, {QStringLiteral("Калорія"), 4.1868},
          {QStringLiteral("Кілокалорія"), 4186.8}, {QStringLiteral("Британська теплова одиниця"), 1055.05585262},
          {QStringLiteral("Фут-фунт"), 1.3558179483314}, {QStringLiteral("Електрон-вольт"), 1.602176634e-19},
          {QStringLiteral("Кіловат-година"), 3.6e6}}},
    };
    QCollator collator(QLocale(QLocale::Ukrainian));
    for (Category &c : list) {
        if (!c.temperature) {
            std::sort(c.units.begin(), c.units.end(), [&collator](const Unit &a, const Unit &b) {
                return collator.compare(a.name, b.name) < 0;
            });
        }
    }
    return list;
}

double toKelvin(int unit, double v)
{
    return unit == 0 ? v + 273.15 : unit == 1 ? (v - 32) * 5 / 9 + 273.15 : v;
}

double fromKelvin(int unit, double v)
{
    return unit == 0 ? v - 273.15 : unit == 1 ? (v - 273.15) * 9 / 5 + 32 : v;
}

/* ---------------- worksheets ---------------- */

struct Sheet {
    QStringList fields;
    QList<int> targets;
    std::function<std::optional<double>(int target, const QMap<int, double> &in)> solve;
};

double annuity(double r, double n)
{
    return r == 0 ? n : (1 - std::pow(1 + r, -n)) / r;
}

Sheet mortgageSheet()
{
    // 0 price, 1 down payment, 2 years, 3 rate, 4 monthly payment
    return {{QStringLiteral("Ціна покупки"), QStringLiteral("Початковий внесок"), QStringLiteral("Термін (років)"), QStringLiteral("Процентна ставка (%)"),
             QStringLiteral("Щомісячний платіж")},
            {4, 1, 2, 0},
            [](int t, const QMap<int, double> &in) -> std::optional<double> {
                const double r = in.value(3) / 1200;
                if (t != 2 && in.value(2) <= 0) {
                    return std::nullopt;
                }
                const double n = in.value(2) * 12;
                switch (t) {
                case 4:
                    return (in.value(0) - in.value(1)) / annuity(r, n);
                case 1:
                    return in.value(0) - in.value(4) * annuity(r, n);
                case 0:
                    return in.value(4) * annuity(r, n) + in.value(1);
                case 2: {
                    const double principal = in.value(0) - in.value(1);
                    if (in.value(4) <= 0) {
                        return std::nullopt;
                    }
                    if (r == 0) {
                        return principal / in.value(4) / 12;
                    }
                    const double arg = 1 - principal * r / in.value(4);
                    if (arg <= 0) {
                        return std::nullopt;
                    }
                    return -std::log(arg) / std::log(1 + r) / 12;
                }
                }
                return std::nullopt;
            }};
}

Sheet leaseSheet()
{
    // 0 lease amount, 1 residual value, 2 years, 3 rate, 4 monthly payment
    return {{QStringLiteral("Сума лізингу"), QStringLiteral("Залишкова вартість"), QStringLiteral("Термін лізингу (років)"), QStringLiteral("Процентна ставка (%)"),
             QStringLiteral("Щомісячний платіж")},
            {4, 0, 2, 1},
            [](int t, const QMap<int, double> &in) -> std::optional<double> {
                const double r = in.value(3) / 1200;
                auto disc = [r](double n) {
                    return r == 0 ? 1.0 : std::pow(1 + r, -n);
                };
                const double n = in.value(2) * 12;
                if (t != 2 && n <= 0) {
                    return std::nullopt;
                }
                switch (t) {
                case 4:
                    return (in.value(0) - in.value(1) * disc(n)) / annuity(r, n);
                case 0:
                    return in.value(4) * annuity(r, n) + in.value(1) * disc(n);
                case 1:
                    return (in.value(0) - in.value(4) * annuity(r, n)) / disc(n);
                case 2: {
                    // solve payment * A(n) + residual * D(n) = lease amount by bisection
                    auto f = [&](double months) {
                        return in.value(4) * annuity(r, months) + in.value(1) * disc(months) - in.value(0);
                    };
                    double lo = 0.01, hi = 1200;
                    if (f(lo) * f(hi) > 0) {
                        return std::nullopt;
                    }
                    for (int i = 0; i < 200; ++i) {
                        const double mid = (lo + hi) / 2;
                        (f(lo) * f(mid) <= 0 ? hi : lo) = mid;
                    }
                    return (lo + hi) / 2 / 12;
                }
                }
                return std::nullopt;
            }};
}

Sheet fuelSheet(bool litres)
{
    // 0 distance, 1 fuel used, 2 economy
    QStringList fields = litres ? QStringList{QStringLiteral("Відстань (км)"), QStringLiteral("Використано пального (л)"), QStringLiteral("Витрата пального (л/100 км)")}
                                : QStringList{QStringLiteral("Відстань (милі)"), QStringLiteral("Використано пального (галони)"),
                                              QStringLiteral("Економія пального (миль на галон)")};
    return {fields, {2, 0, 1}, [litres](int t, const QMap<int, double> &in) -> std::optional<double> {
                const double d = in.value(0), f = in.value(1), e = in.value(2);
                if (litres) {
                    if (t == 2) {
                        return d == 0 ? std::nullopt : std::optional<double>(f / d * 100);
                    }
                    if (t == 0) {
                        return e == 0 ? std::nullopt : std::optional<double>(f / e * 100);
                    }
                    return e * d / 100;
                }
                if (t == 2) {
                    return f == 0 ? std::nullopt : std::optional<double>(d / f);
                }
                if (t == 0) {
                    return e * f;
                }
                return e == 0 ? std::nullopt : std::optional<double>(d / e);
            }};
}

QFrame *whitePanel()
{
    auto *f = new QFrame;
    f->setObjectName(QStringLiteral("win7calcpanel"));
    QFont pf = f->font();
    pf.setPixelSize(12); // Segoe UI 9 like the rest of the window
    f->setFont(pf);
    f->setStyleSheet(QStringLiteral("#win7calcpanel { background: #ffffff; border: 1px solid #8e9fb4; border-radius: 2px; }"
                                    "QComboBox, QLineEdit, QSpinBox, QDateEdit, QPushButton, QRadioButton, QLabel { font-size: 12px; }"
                                    "QLineEdit[readOnly=\"true\"] { background: #eef1f4; border: 1px solid #d5dbe2; }"));
    f->setFixedWidth(470);
    return f;
}

QLabel *panelLabel(const QString &text)
{
    auto *l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("color: #333333;"));
    return l;
}

QLineEdit *valueEdit()
{
    auto *e = new QLineEdit;
    e->setPlaceholderText(QStringLiteral("Введіть значення"));
    return e;
}

QLineEdit *resultEdit()
{
    auto *e = new QLineEdit;
    e->setReadOnly(true);
    e->setFocusPolicy(Qt::ClickFocus);
    return e;
}


/* ---------------- Windows 7 date picker ---------------- */

/** Month calendar popup like the Windows 7 date picker */
class Win7Calendar : public QWidget
{
public:
    std::function<void(QDate)> picked;

    explicit Win7Calendar(QDate selected)
        : QWidget(nullptr, Qt::Popup)
        , m_selected(selected)
        , m_month(QDate(selected.year(), selected.month(), 1))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setMouseTracking(true);
        QFont f = font();
        f.setPixelSize(12);
        setFont(f);
        setFixedSize(7 * kCell + 16, 46 + 6 * kRow + 30);
    }

protected:
    static constexpr int kCell = 26;
    static constexpr int kRow = 17;

    QDate firstShown() const
    {
        return m_month.addDays(-(m_month.dayOfWeek() - 1 == 0 ? 7 : m_month.dayOfWeek() - 1));
    }
    QRect cellRect(int i) const
    {
        return QRect(8 + (i % 7) * kCell, 46 + (i / 7) * kRow, kCell, kRow);
    }
    QRect footerRect() const
    {
        return QRect(8, 46 + 6 * kRow + 4, width() - 16, 20);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setPen(QColor(0x97, 0x97, 0x97));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        // header: ◂ Month Year ▸
        p.setPen(Qt::black);
        const QString title = QLocale(QLocale::Ukrainian).standaloneMonthName(m_month.month()) + QStringLiteral(" ") + QString::number(m_month.year());
        QString cap = title;
        cap[0] = cap.at(0).toUpper();
        p.drawText(QRect(0, 4, width(), 18), Qt::AlignCenter, cap);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::black);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(12, 13), QPointF(16, 9.5), QPointF(16, 16.5)}));
        p.drawPolygon(QPolygonF({QPointF(width() - 12, 13), QPointF(width() - 16, 9.5), QPointF(width() - 16, 16.5)}));
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setBrush(Qt::NoBrush);
        // weekday names and the line under them
        const QStringList days{QStringLiteral("Пн"), QStringLiteral("Вт"), QStringLiteral("Ср"), QStringLiteral("Чт"), QStringLiteral("Пт"), QStringLiteral("Сб"), QStringLiteral("Нд")};
        p.setPen(Qt::black);
        for (int i = 0; i < 7; ++i) {
            p.drawText(QRect(8 + i * kCell, 24, kCell, 16), Qt::AlignCenter, days.at(i));
        }
        // days
        const QDate start = firstShown();
        const QDate today = QDate::currentDate();
        for (int i = 0; i < 42; ++i) {
            const QDate d = start.addDays(i);
            const QRect r = cellRect(i).adjusted(2, 0, -2, 0);
            if (d == m_selected) {
                p.fillRect(r, QColor(0xdc, 0xeb, 0xfc));
            } else if (d == m_hover) {
                p.fillRect(r, QColor(0xee, 0xf5, 0xfd));
            }
            if (d == today) {
                p.setPen(QColor(0x2f, 0x6c, 0xb9));
                p.drawRect(r.adjusted(0, 0, -1, -1));
            }
            p.setPen(d.month() == m_month.month() ? QColor(Qt::black) : QColor(0x8d, 0x8d, 0x8d));
            p.drawText(cellRect(i), Qt::AlignCenter, QString::number(d.day()));
        }
        // "Today: dd.MM.yyyy"
        // centred like Windows 7
        const QRect fr = footerRect();
        const QString todayText = QStringLiteral("Сьогодні: ") + today.toString(QStringLiteral("dd.MM.yyyy"));
        const int tw = fontMetrics().horizontalAdvance(todayText);
        const int x0 = fr.left() + (fr.width() - tw - 24) / 2;
        p.setPen(QColor(0x80, 0x80, 0x80));
        p.drawRect(QRect(x0, fr.center().y() - 5, 16, 10));
        p.setPen(m_hoverFooter ? QColor(0x00, 0x66, 0xcc) : QColor(Qt::black));
        p.drawText(QRect(x0 + 24, fr.top(), tw + 2, fr.height()), Qt::AlignLeft | Qt::AlignVCenter, todayText);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        QDate h;
        for (int i = 0; i < 42; ++i) {
            if (cellRect(i).contains(e->position().toPoint())) {
                h = firstShown().addDays(i);
            }
        }
        const bool footer = footerRect().contains(e->position().toPoint());
        if (h != m_hover || footer != m_hoverFooter) {
            m_hover = h;
            m_hoverFooter = footer;
            update();
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        const QPoint pos = e->position().toPoint();
        if (!rect().contains(pos)) {
            close();
            return;
        }
        if (pos.y() < 24) {
            if (pos.x() < 30) {
                m_month = m_month.addMonths(-1);
            } else if (pos.x() > width() - 30) {
                m_month = m_month.addMonths(1);
            }
            update();
            return;
        }
        if (footerRect().contains(pos)) {
            choose(QDate::currentDate());
            return;
        }
        for (int i = 0; i < 42; ++i) {
            if (cellRect(i).contains(pos)) {
                choose(firstShown().addDays(i));
                return;
            }
        }
    }

    void wheelEvent(QWheelEvent *e) override
    {
        m_month = m_month.addMonths(e->angleDelta().y() > 0 ? -1 : 1);
        update();
    }

private:
    void choose(QDate d)
    {
        if (picked) {
            picked(d);
        }
        close();
    }

    QDate m_selected;
    QDate m_month;
    QDate m_hover;
    bool m_hoverFooter = false;
};

/** Date field: "dd.MM.yyyy" with the small calendar ▾ button of Windows 7 */
class DateField : public QFrame
{
public:
    DateField()
    {
        setObjectName(QStringLiteral("win7datefield"));
        setStyleSheet(QStringLiteral("#win7datefield { background: #ffffff; border: 1px solid #abadb3; }"
                                     "#win7datefield:hover { border-color: #7eb4ea; }"
                                     "QLineEdit { border: none; background: transparent; font-size: 12px; }"));
        setFixedSize(112, 22);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(3, 0, 1, 0);
        h->setSpacing(0);
        m_edit = new QLineEdit;
        m_edit->setInputMask(QStringLiteral("99.99.9999"));
        h->addWidget(m_edit, 1);
        m_button = new QToolButton;
        m_button->setAutoRaise(true);
        m_button->setFocusPolicy(Qt::NoFocus);
        m_button->setFixedSize(26, 20);
        m_button->setIcon(QIcon(buttonPixmap()));
        m_button->setIconSize(QSize(22, 14));
        m_button->setStyleSheet(QStringLiteral("QToolButton { border: none; background: transparent; }"
                                               "QToolButton:hover { background: #e5f1fb; }"));
        h->addWidget(m_button);
        setDate(QDate::currentDate());
        connect(m_button, &QToolButton::clicked, this, [this] {
            auto *cal = new Win7Calendar(date());
            cal->picked = [this](QDate d) {
                setDate(d);
            };
            cal->move(mapToGlobal(QPoint(0, height())));
            cal->show();
        });
    }
    QDate date() const
    {
        const QDate d = QDate::fromString(m_edit->text(), QStringLiteral("dd.MM.yyyy"));
        return d.isValid() ? d : QDate::currentDate();
    }
    void setDate(QDate d)
    {
        m_edit->setText(d.toString(QStringLiteral("dd.MM.yyyy")));
    }

private:
    static QPixmap buttonPixmap()
    {
        // tiny calendar page and a small ▾
        QPixmap pm(22, 14);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setPen(QColor(0x80, 0x80, 0x80));
        p.setBrush(QColor(0xf4, 0xf4, 0xf4));
        p.drawRect(1, 1, 11, 11);
        p.fillRect(2, 2, 10, 3, QColor(0xa0, 0xa8, 0xb4));
        p.setPen(QColor(0xb0, 0xb0, 0xb0));
        for (int y = 7; y <= 10; y += 3) {
            p.drawLine(3, y, 10, y);
        }
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x30, 0x30, 0x30));
        p.drawPolygon(QPolygonF({QPointF(15, 6), QPointF(20, 6), QPointF(17.5, 9)}));
        return pm;
    }

    QLineEdit *m_edit;
    QToolButton *m_button;
};

/* ---------------- the calculator ---------------- */

struct Op {
    QChar code; // '(' marks a parenthesis
};

class Calculator : public QMainWindow
{
public:
    Calculator()
        : m_settings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-calcrc"), QSettings::IniFormat)
    {
        setWindowTitle(QStringLiteral("Калькулятор"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-calc")));
        m_mode = Mode(m_settings.value(QStringLiteral("Mode"), Standard).toInt());
        m_historyOn = m_settings.value(QStringLiteral("History"), false).toBool();
        m_grouping = m_settings.value(QStringLiteral("Grouping"), false).toBool();
        m_panel = BasicPanel; // every start shows the plain calculator, the side panel isn't remembered
        m_radix = m_settings.value(QStringLiteral("Radix"), 10).toInt();
        m_wordBits = m_settings.value(QStringLiteral("WordBits"), 64).toInt();
        m_angle = m_settings.value(QStringLiteral("Angle"), 0).toInt();

        buildMenus();

        auto *central = new QWidget;
        central->setObjectName(QStringLiteral("win7calc"));
        central->setAttribute(Qt::WA_StyledBackground, true);
        central->setStyleSheet(QStringLiteral("#win7calc { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #eef1f5, stop:1 #e0e5eb); }"));
        central->setFocusPolicy(Qt::StrongFocus);
        auto *h = new QHBoxLayout(central);
        h->setContentsMargins(11, 11, 11, 11);
        h->setSpacing(10);
        auto *left = new QWidget;
        m_leftLayout = new QVBoxLayout(left);
        m_leftLayout->setContentsMargins(0, 0, 0, 0);
        m_leftLayout->setSpacing(8);
        m_display = new DisplayBox;
        m_leftLayout->addWidget(m_display);
        m_bitView = new BitView;
        m_bitView->toggled = [this](int bit) {
            if (m_error) {
                clearAll();
            }
            const quint64 u = quint64(currentInt()) ^ (quint64(1) << bit);
            setEntryNum(Num(normU(u)));
            m_fresh = false;
            refresh();
        };
        m_leftLayout->addWidget(m_bitView);
        h->addWidget(left, 0, Qt::AlignTop);
        m_panels = new QStackedWidget;
        h->addWidget(m_panels);
        setCentralWidget(central);
        buildPanels();

        connect(m_display->list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            if (m_mode == Statistics) {
                return;
            }
            recallHistory(item);
        });
        connect(m_display->list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
            if (item && m_mode != Statistics && !m_display->list->isEditing()) {
                recallHistory(item);
            }
        });
        connect(m_display->list->itemDelegate(), &QAbstractItemDelegate::commitData, this, [this] {
            QTimer::singleShot(0, this, [this] {
                historyEdited(m_display->list->currentItem());
            });
        });

        setMode(m_mode, false);
        setPanel(m_panel);
        layout()->setSizeConstraint(QLayout::SetFixedSize); // calc.exe can't be resized
        setWindowFlag(Qt::WindowMaximizeButtonHint, false);
        central->setFocus();
    }

    QString shown() const
    {
        return m_entry;
    }

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        const QString t = e->text();
        const auto mods = e->modifiers();
        const bool ctrl = mods & Qt::ControlModifier;
        if (ctrl) {
            switch (e->key()) {
            case Qt::Key_L: memClear(); return;
            case Qt::Key_R: memRecall(); return;
            case Qt::Key_M: memStore(); return;
            case Qt::Key_P: memAdd(1); return;
            case Qt::Key_Q: memAdd(-1); return;
            default: break;
            }
        }
        if (m_mode == Programmer) {
            switch (e->key()) {
            case Qt::Key_F5: setRadix(16); return;
            case Qt::Key_F6: setRadix(10); return;
            case Qt::Key_F7: setRadix(8); return;
            case Qt::Key_F8: setRadix(2); return;
            case Qt::Key_F12: setWordBits(64); return;
            case Qt::Key_F2: setWordBits(32); return;
            case Qt::Key_F3: setWordBits(16); return;
            case Qt::Key_F4: setWordBits(8); return;
            default: break;
            }
            if (t.size() == 1 && m_radix == 16 && QStringLiteral("abcdefABCDEF").contains(t)) {
                digit(t.toUpper());
                return;
            }
            if (t == QLatin1String("&")) { binaryOp(QLatin1Char('&')); return; }
            if (t == QLatin1String("|")) { binaryOp(QLatin1Char('|')); return; }
            if (t == QLatin1String("^")) { binaryOp(QLatin1Char('x')); return; }
            if (t == QLatin1String("<")) { binaryOp(QLatin1Char('<')); return; }
            if (t == QLatin1String(">")) { binaryOp(QLatin1Char('>')); return; }
            if (t == QLatin1String("~")) { progUnary(QStringLiteral("Not")); return; }
            if (t == QLatin1String("%")) { binaryOp(QLatin1Char('m')); return; }
        }
        if (m_mode == Scientific) {
            static const QMap<QString, QString> sci{{QStringLiteral("s"), QStringLiteral("sin")}, {QStringLiteral("o"), QStringLiteral("cos")},
                                                    {QStringLiteral("t"), QStringLiteral("tan")}, {QStringLiteral("n"), QStringLiteral("ln")},
                                                    {QStringLiteral("l"), QStringLiteral("log")}, {QStringLiteral("!"), QStringLiteral("n!")},
                                                    {QStringLiteral("q"), QStringLiteral("x²")}, {QStringLiteral("#"), QStringLiteral("x³")},
                                                    {QStringLiteral("p"), QStringLiteral("π")}, {QStringLiteral("v"), QStringLiteral("F-E")},
                                                    {QStringLiteral("x"), QStringLiteral("Exp")}, {QStringLiteral("i"), QStringLiteral("Inv")}};
            if (sci.contains(t)) {
                sciKey(sci.value(t));
                return;
            }
            if (t == QLatin1String("y") || t == QLatin1String("^")) { binaryOp(QLatin1Char('^')); return; }
        }
        if ((m_mode == Scientific || m_mode == Programmer) && t == QLatin1String("(")) {
            openParen();
            return;
        }
        if ((m_mode == Scientific || m_mode == Programmer) && t == QLatin1String(")")) {
            closeParen();
            return;
        }
        if (m_mode == Statistics && e->key() == Qt::Key_Insert) {
            statAdd();
            return;
        }
        if (t.size() == 1 && t.at(0).isDigit()) {
            digit(t);
        } else if (t == QLatin1String(".") || t == QLatin1String(",")) {
            point();
        } else if (m_mode != Statistics && (t == QLatin1String("+") || t == QLatin1String("-") || t == QLatin1String("*") || t == QLatin1String("/"))) {
            binaryOp(t.at(0));
        } else if (t == QLatin1String("%") && (m_mode == Standard || m_mode == Scientific)) {
            percent();
        } else if (t == QLatin1String("@") && m_mode != Statistics && m_mode != Programmer) {
            stdUnary(QStringLiteral("sqrt"));
        } else if ((t == QLatin1String("r") || t == QLatin1String("R")) && (m_mode == Standard || m_mode == Scientific)) {
            stdUnary(QStringLiteral("reciproc"));
        } else if (m_mode != Statistics && (t == QLatin1String("=") || e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
            equals();
        } else if (e->key() == Qt::Key_Backspace) {
            backspace();
        } else if (e->key() == Qt::Key_Delete) {
            m_mode == Statistics ? clearEntry() : clearEntry();
        } else if (e->key() == Qt::Key_Escape) {
            clearAll();
        } else if (e->key() == Qt::Key_F9) {
            negate();
        } else {
            QMainWindow::keyPressEvent(e);
        }
    }

private:
    /* ----- integers of the Programmer mode ----- */

    quint64 mask() const
    {
        return m_wordBits >= 64 ? ~quint64(0) : ((quint64(1) << m_wordBits) - 1);
    }

    qint64 normU(quint64 u) const
    {
        u &= mask();
        if (m_wordBits < 64 && ((u >> (m_wordBits - 1)) & 1)) {
            u |= ~mask();
        }
        return qint64(u);
    }

    qint64 norm(Num x) const
    {
        if (!std::isfinite(double(x))) {
            return 0;
        }
        x = std::trunc(x);
        const Num two64 = 18446744073709551616.0L;
        if (x >= two64 || x <= -two64) {
            x = std::fmod(x, two64);
        }
        quint64 u;
        if (x < 0) {
            u = quint64(-(qint64)0) - quint64((unsigned long long)(-x));
        } else {
            u = (unsigned long long)x;
        }
        return normU(u);
    }

    QString progText(qint64 v) const
    {
        if (m_radix == 10) {
            return QString::number(v);
        }
        return QString::number(quint64(v) & mask(), m_radix).toUpper();
    }

    qint64 currentInt() const
    {
        if (m_error) {
            return 0;
        }
        bool ok = false;
        if (m_radix == 10) {
            const qint64 v = m_entry.toLongLong(&ok);
            return ok ? normU(quint64(v)) : 0;
        }
        const quint64 u = m_entry.toULongLong(&ok, m_radix);
        return ok ? normU(u) : 0;
    }

    /* ----- entry ----- */

    Num value() const
    {
        if (m_error) {
            return 0;
        }
        if (m_mode == Programmer) {
            return Num(currentInt());
        }
        QString s = m_entry;
        s.replace(kComma, QLatin1Char('.'));
        if (s.endsWith(QLatin1Char('+')) || s.endsWith(QLatin1Char('-'))) {
            s.chop(1);
        }
        if (s.endsWith(QLatin1Char('e'))) {
            s.chop(1);
        }
        return Num(s.toDouble());
    }

    int maxDigits() const
    {
        return m_mode == Standard ? 16 : 32;
    }

    void setEntryNum(Num x)
    {
        m_entry = m_mode == Programmer ? progText(norm(x)) : numToText(x, m_fe, 16);
        m_expEntry = false;
    }

    QString entryForExpression() const
    {
        return m_mode == Programmer ? m_entry : (m_grouping ? groupDecimal(m_entry) : m_entry);
    }

    void showError(const QString &message)
    {
        m_error = true;
        m_entry = message;
        m_expr.clear();
        m_vals.clear();
        m_ops.clear();
        m_depth = 0;
        refresh();
    }

    void refresh(bool mirror = true)
    {
        QString text;
        if (m_error) {
            text = m_entry;
        } else if (m_mode == Programmer) {
            text = m_grouping ? groupEvery(m_entry, (m_radix == 16 || m_radix == 2) ? 4 : 3) : m_entry;
        } else {
            text = m_grouping ? groupDecimal(m_entry) : m_entry;
        }
        m_display->number->set(text, m_mode == Statistics ? QString() : m_expr, m_hasMemory, m_depth);
        if (m_mode == Programmer) {
            m_bitView->setValue(quint64(currentInt()) & mask(), m_wordBits);
        }
        if (m_mode == Statistics) {
            m_display->count->setText(QStringLiteral("Кількість = %1").arg(m_stats.size()));
        }
        if (mirror && m_panel == UnitsPanel && m_unitFrom && !m_unitFrom->hasFocus()) {
            m_unitFrom->setText(m_error ? QString() : m_entry);
        }
    }

    void resetCalc()
    {
        m_vals.clear();
        m_ops.clear();
        m_expr.clear();
        m_depth = 0;
        m_lastWasOp = false;
        m_operandShown = false;
        m_operandText.clear();
    }

    void clearAll()
    {
        resetCalc();
        m_entry = QStringLiteral("0");
        m_fresh = false;
        m_error = false;
        m_afterEquals = false;
        m_expEntry = false;
        m_hasLastOp = false;
        refresh();
    }

    void clearEntry()
    {
        if (m_error) {
            clearAll();
            return;
        }
        if (m_operandShown) {
            removeOperandText();
        }
        m_entry = QStringLiteral("0");
        m_fresh = false;
        m_expEntry = false;
        refresh();
    }

    void removeOperandText()
    {
        const QString tail = m_operandText + QLatin1Char(' ');
        if (!m_operandText.isEmpty() && m_expr.endsWith(tail)) {
            m_expr.chop(tail.size());
        }
        m_operandShown = false;
        m_operandText.clear();
    }

    void startTyping()
    {
        if (m_error) {
            clearAll();
        }
        if (m_afterEquals) {
            resetCalc();
            m_afterEquals = false;
        }
        if (m_operandShown) {
            removeOperandText();
            m_fresh = true;
        }
        if (m_fresh) {
            m_entry = QStringLiteral("0");
            m_fresh = false;
            m_expEntry = false;
        }
        m_lastWasOp = false;
    }

    void digit(const QString &d)
    {
        if (m_mode == Programmer && d.at(0).isLetterOrNumber()) {
            bool ok = false;
            const int dv = d.toInt(&ok, 16);
            if (!ok || dv >= m_radix) {
                return;
            }
        }
        startTyping();
        if (m_expEntry) {
            const int e = m_entry.indexOf(QLatin1Char('e'));
            QString exp = m_entry.mid(e + 2);
            if (exp == QLatin1String("0")) {
                exp.clear();
            }
            if (exp.size() >= 4) {
                return;
            }
            m_entry = m_entry.left(e + 2) + exp + d;
            refresh();
            return;
        }
        if (m_mode == Programmer) {
            const QString candidate = (m_entry == QLatin1String("0")) ? d : m_entry + d;
            bool ok = false;
            if (m_radix == 10) {
                const qint64 v = candidate.toLongLong(&ok);
                const qint64 lim = m_wordBits >= 64 ? std::numeric_limits<qint64>::max() : (qint64(1) << (m_wordBits - 1)) - 1;
                if (!ok || v > lim + (candidate.startsWith(QLatin1Char('-')) ? 1 : 0)) {
                    return;
                }
            } else {
                const quint64 u = candidate.toULongLong(&ok, m_radix);
                if (!ok || (u & ~mask())) {
                    return;
                }
            }
            m_entry = candidate;
            refresh();
            return;
        }
        const int count = QString(m_entry).remove(kComma).remove(QLatin1Char('-')).size();
        if (count >= maxDigits() && m_entry != QLatin1String("0")) {
            return;
        }
        m_entry = (m_entry == QLatin1String("0")) ? d : m_entry + d;
        refresh();
    }

    void point()
    {
        if (m_mode == Programmer) {
            return;
        }
        startTyping();
        if (!m_expEntry && !m_entry.contains(kComma)) {
            m_entry += kComma;
        }
        refresh();
    }

    void exponentKey()
    {
        if (m_mode == Programmer || m_error) {
            return;
        }
        startTyping();
        if (!m_entry.contains(QLatin1Char('e'))) {
            m_entry += QStringLiteral("e+0");
            m_expEntry = true;
        }
        refresh();
    }

    void backspace()
    {
        if (m_error) {
            clearAll();
            return;
        }
        if (m_fresh || m_operandShown) {
            return;
        }
        if (m_expEntry) {
            const int e = m_entry.indexOf(QLatin1Char('e'));
            QString exp = m_entry.mid(e + 2);
            if (exp == QLatin1String("0")) {
                m_entry = m_entry.left(e);
                m_expEntry = false;
            } else {
                exp.chop(1);
                m_entry = m_entry.left(e + 2) + (exp.isEmpty() ? QStringLiteral("0") : exp);
            }
            refresh();
            return;
        }
        m_entry.chop(1);
        if (m_entry.isEmpty() || m_entry == QLatin1String("-")) {
            m_entry = QStringLiteral("0");
        }
        refresh();
    }

    void negate()
    {
        if (m_error) {
            return;
        }
        if (m_expEntry) {
            const int e = m_entry.indexOf(QLatin1Char('e'));
            m_entry[e + 1] = m_entry.at(e + 1) == QLatin1Char('+') ? QLatin1Char('-') : QLatin1Char('+');
            refresh();
            return;
        }
        if (m_mode == Programmer) {
            const qint64 v = currentInt();
            const QString old = m_entry;
            setEntryNum(Num(normU(quint64(0) - quint64(v))));
            if (m_fresh || m_operandShown) {
                applyOperandText(QStringLiteral("negate(%1)").arg(m_operandShown ? m_operandText : old));
            }
            refresh();
            return;
        }
        if (m_fresh || m_operandShown) {
            // negating a result: Windows writes negate(...) into the expression
            if (m_afterEquals) {
                resetCalc();
                m_afterEquals = false;
            }
            const QString inner = m_operandShown ? m_operandText : entryForExpression();
            setEntryNum(-value());
            applyOperandText(QStringLiteral("negate(%1)").arg(inner));
            refresh();
            return;
        }
        if (m_entry == QLatin1String("0")) {
            return;
        }
        m_entry = m_entry.startsWith(QLatin1Char('-')) ? m_entry.mid(1) : QStringLiteral("-") + m_entry;
        refresh();
    }

    /** Writes the text of the current operand (e.g. "sqrt(9)") into the expression */
    void applyOperandText(const QString &text)
    {
        if (m_operandShown) {
            removeOperandText();
        }
        m_operandText = text;
        m_expr += text + QLatin1Char(' ');
        m_operandShown = true;
        m_fresh = true;
        m_lastWasOp = false;
    }

    /* ----- operators ----- */

    int precedence(QChar op) const
    {
        if (m_mode == Standard) {
            return 1;
        }
        if (m_mode == Programmer) {
            switch (op.unicode()) {
            case '|': return 1;
            case 'x': return 2;
            case '&': return 3;
            case '<':
            case '>': return 4;
            case '+':
            case '-': return 5;
            default: return 6; // * / Mod
            }
        }
        switch (op.unicode()) {
        case '+':
        case '-': return 1;
        case '^':
        case 'r': return 3;
        default: return 2; // * / Mod
        }
    }

    static QString opSymbol(QChar op)
    {
        switch (op.unicode()) {
        case 'm': return QStringLiteral("Mod");
        case '^': return QStringLiteral("^");
        case 'r': return QStringLiteral("yroot");
        case '&': return QStringLiteral("And");
        case '|': return QStringLiteral("Or");
        case 'x': return QStringLiteral("Xor");
        case '<': return QStringLiteral("Lsh");
        case '>': return QStringLiteral("Rsh");
        default: return QString(op);
        }
    }

    std::optional<Num> applyOp(QChar op, Num a, Num b)
    {
        if (m_mode == Programmer) {
            const qint64 sa = norm(a), sb = norm(b);
            const quint64 ua = quint64(sa), ub = quint64(sb);
            switch (op.unicode()) {
            case '+': return Num(normU(ua + ub));
            case '-': return Num(normU(ua - ub));
            case '*': return Num(normU(ua * ub));
            case '/':
            case 'm':
                if (sb == 0) {
                    showError(QStringLiteral("Ділення на нуль неможливе"));
                    return std::nullopt;
                }
                if (sb == -1) {
                    return Num(op == QLatin1Char('/') ? normU(quint64(0) - ua) : 0);
                }
                return Num(normU(quint64(op == QLatin1Char('/') ? sa / sb : sa % sb)));
            case '&': return Num(normU(ua & ub));
            case '|': return Num(normU(ua | ub));
            case 'x': return Num(normU(ua ^ ub));
            case '<': return Num(normU((ub & 63) ? ua << (ub & 63) : ua));
            case '>': return Num(normU(quint64(sa >> (ub & 63))));
            }
            return std::nullopt;
        }
        Num r = 0;
        switch (op.unicode()) {
        case '+': r = a + b; break;
        case '-': r = a - b; break;
        case '*': r = a * b; break;
        case '/':
            if (b == 0) {
                showError(a == 0 ? QStringLiteral("Результат не визначено") : QStringLiteral("Ділення на нуль неможливе"));
                return std::nullopt;
            }
            r = a / b;
            break;
        case 'm':
            if (b == 0) {
                showError(QStringLiteral("Ділення на нуль неможливе"));
                return std::nullopt;
            }
            r = std::fmod(a, b);
            break;
        case '^': r = std::pow(a, b); break;
        case 'r':
            if (b == 0) {
                showError(QStringLiteral("Неприпустиме введення"));
                return std::nullopt;
            }
            if (a < 0 && std::fmod(b, 2.0L) != 0 && std::fmod(b, 1.0L) == 0) {
                r = -std::pow(-a, 1 / b);
            } else {
                r = std::pow(a, 1 / b);
            }
            break;
        }
        return checked(r);
    }

    std::optional<Num> checked(Num r)
    {
        if (std::isnan(double(r))) {
            showError(QStringLiteral("Неприпустиме введення"));
            return std::nullopt;
        }
        if (std::isinf(double(r))) {
            showError(QStringLiteral("Переповнення"));
            return std::nullopt;
        }
        return r;
    }

    bool reduceTop()
    {
        const QChar op = m_ops.takeLast().code;
        if (m_vals.size() < 2) {
            return true;
        }
        const Num b = m_vals.takeLast();
        const Num a = m_vals.takeLast();
        const auto r = applyOp(op, a, b);
        if (!r) {
            return false;
        }
        m_vals.append(*r);
        return true;
    }

    void binaryOp(QChar op)
    {
        if (m_error) {
            return;
        }
        if (m_afterEquals) {
            const Num v = value();
            resetCalc();
            m_afterEquals = false;
            m_vals.append(v);
            m_ops.append({op});
            m_expr = entryForExpression() + QLatin1Char(' ') + opSymbol(op) + QLatin1Char(' ');
            m_fresh = true;
            m_lastWasOp = true;
            refresh();
            return;
        }
        if (m_lastWasOp && m_fresh && !m_ops.isEmpty() && m_ops.last().code != QLatin1Char('(')) {
            // another operator right after one replaces it
            const QString old = opSymbol(m_ops.last().code) + QLatin1Char(' ');
            if (m_expr.endsWith(old)) {
                m_expr.chop(old.size());
            }
            m_ops.last().code = op;
            m_expr += opSymbol(op) + QLatin1Char(' ');
            refresh();
            return;
        }
        const Num v = value();
        if (!m_operandShown) {
            m_expr += entryForExpression() + QLatin1Char(' ');
        }
        m_vals.append(v);
        while (!m_ops.isEmpty() && m_ops.last().code != QLatin1Char('(') && precedence(m_ops.last().code) >= precedence(op)) {
            if (!reduceTop()) {
                return;
            }
        }
        m_ops.append({op});
        m_expr += opSymbol(op) + QLatin1Char(' ');
        setEntryNum(m_vals.last());
        m_fresh = true;
        m_operandShown = false;
        m_operandText.clear();
        m_lastWasOp = true;
        refresh();
    }

    void openParen()
    {
        if (m_error) {
            return;
        }
        if (m_afterEquals) {
            resetCalc();
            m_afterEquals = false;
        }
        if (m_operandShown) {
            removeOperandText();
        }
        m_ops.append({QLatin1Char('(')});
        m_expr += QStringLiteral("( ");
        ++m_depth;
        m_fresh = true;
        m_lastWasOp = false;
        refresh();
    }

    void closeParen()
    {
        if (m_error || m_depth == 0) {
            return;
        }
        const Num v = value();
        if (!m_operandShown) {
            m_expr += entryForExpression() + QLatin1Char(' ');
        }
        m_vals.append(v);
        while (!m_ops.isEmpty() && m_ops.last().code != QLatin1Char('(')) {
            if (!reduceTop()) {
                return;
            }
        }
        if (!m_ops.isEmpty()) {
            m_ops.removeLast();
        }
        --m_depth;
        m_expr += QStringLiteral(") ");
        const Num r = m_vals.isEmpty() ? v : m_vals.takeLast();
        setEntryNum(r);
        m_operandShown = true;
        m_operandText.clear(); // the group stays in the expression
        m_fresh = true;
        m_lastWasOp = false;
        refresh();
    }

    void equals()
    {
        if (m_error) {
            clearAll();
            return;
        }
        if (m_afterEquals || (m_ops.isEmpty() && m_vals.isEmpty())) {
            if (m_hasLastOp) {
                // "=" again repeats the last operation
                const Num a = value();
                const QString text = entryForExpression() + QLatin1Char(' ') + opSymbol(m_lastOp) + QLatin1Char(' ')
                    + (m_mode == Programmer ? progText(norm(m_lastOperand)) : numToText(m_lastOperand, false));
                const auto r = applyOp(m_lastOp, a, m_lastOperand);
                if (!r) {
                    return;
                }
                resetCalc();
                setEntryNum(*r);
                addHistory(text, *r);
            } else {
                resetCalc();
            }
            m_fresh = true;
            m_afterEquals = true;
            refresh();
            return;
        }
        const Num v = value();
        const QString finalExpr = (m_expr + (m_operandShown ? QString() : entryForExpression())).trimmed();
        m_vals.append(v);
        for (int i = m_ops.size() - 1; i >= 0; --i) {
            if (m_ops.at(i).code != QLatin1Char('(')) {
                m_lastOp = m_ops.at(i).code;
                m_lastOperand = v;
                m_hasLastOp = true;
                break;
            }
        }
        while (!m_ops.isEmpty()) {
            if (m_ops.last().code == QLatin1Char('(')) {
                m_ops.removeLast();
                continue;
            }
            if (!reduceTop()) {
                return;
            }
        }
        const Num r = m_vals.isEmpty() ? v : m_vals.last();
        resetCalc();
        setEntryNum(r);
        addHistory(finalExpr, r);
        m_fresh = true;
        m_afterEquals = true;
        refresh();
    }

    /** x% = accumulator * x / 100, like calc.exe */
    void percent()
    {
        if (m_error) {
            return;
        }
        const Num x = m_vals.isEmpty() ? 0 : m_vals.last() * value() / 100;
        if (m_afterEquals) {
            resetCalc();
            m_afterEquals = false;
        }
        setEntryNum(x);
        applyOperandText(entryForExpression());
        refresh();
    }

    /** Functions of the current number; `name` is what Windows writes in the expression */
    void unary(const QString &name, const std::function<Num(Num)> &fn)
    {
        if (m_error) {
            return;
        }
        const Num x = value();
        const auto r = checked(fn(x));
        if (!r) {
            return;
        }
        if (m_afterEquals) {
            resetCalc();
            m_afterEquals = false;
        }
        const QString inner = m_operandShown ? m_operandText : entryForExpression();
        setEntryNum(*r);
        applyOperandText(QStringLiteral("%1(%2)").arg(name, inner));
        refresh();
    }

    void stdUnary(const QString &which)
    {
        if (which == QLatin1String("sqrt")) {
            if (value() < 0) {
                showError(QStringLiteral("Неприпустиме введення"));
                return;
            }
            unary(QStringLiteral("sqrt"), [](Num x) { return std::sqrt(x); });
        } else if (which == QLatin1String("reciproc")) {
            if (value() == 0) {
                showError(QStringLiteral("Ділення на нуль неможливе"));
                return;
            }
            unary(QStringLiteral("reciproc"), [](Num x) { return 1 / x; });
        }
    }

    void progUnary(const QString &which)
    {
        if (m_error) {
            return;
        }
        const quint64 u = quint64(currentInt()) & mask();
        const int b = m_wordBits;
        quint64 r = u;
        QString name;
        if (which == QLatin1String("Not")) {
            r = ~u;
            name = QStringLiteral("NOT");
        } else if (which == QLatin1String("RoL")) {
            r = ((u << 1) | (u >> (b - 1))) & mask();
            name = QStringLiteral("RoL");
        } else if (which == QLatin1String("RoR")) {
            r = ((u >> 1) | (u << (b - 1))) & mask();
            name = QStringLiteral("RoR");
        }
        if (m_afterEquals) {
            resetCalc();
            m_afterEquals = false;
        }
        const QString inner = m_operandShown ? m_operandText : m_entry;
        setEntryNum(Num(normU(r)));
        applyOperandText(QStringLiteral("%1(%2)").arg(name, inner));
        refresh();
    }

    Num toRadians(Num x) const
    {
        return m_angle == 0 ? x * kPi / 180 : m_angle == 2 ? x * kPi / 200 : x;
    }
    Num fromRadians(Num x) const
    {
        return m_angle == 0 ? x * 180 / kPi : m_angle == 2 ? x * 200 / kPi : x;
    }
    QString angleSuffix() const
    {
        return m_angle == 0 ? QStringLiteral("d") : m_angle == 2 ? QStringLiteral("g") : QStringLiteral("r");
    }

    /** Scientific mode keys (Inv changes some of them) */
    void sciKey(const QString &k)
    {
        const bool inv = m_inv;
        auto done = [this] {
            if (m_inv) {
                m_inv = false;
                m_invKey->setChecked(false);
                updateInvLabels();
            }
        };
        if (k == QLatin1String("Inv")) {
            m_inv = !m_inv;
            m_invKey->setChecked(m_inv);
            updateInvLabels();
            return;
        }
        if (k == QLatin1String("F-E")) {
            m_fe = !m_fe;
            m_feKey->setChecked(m_fe);
            if (!m_error && (m_fresh || m_afterEquals || m_operandShown)) {
                setEntryNum(value());
            }
            refresh();
            return;
        }
        if (k == QLatin1String("Exp")) {
            exponentKey();
            return;
        }
        if (k == QLatin1String("π")) {
            startTyping();
            setEntryNum(inv ? 2 * kPi : kPi);
            m_fresh = true;
            done();
            refresh();
            return;
        }
        const QString s = angleSuffix();
        if (k == QLatin1String("sin") || k == QLatin1String("cos") || k == QLatin1String("tan")) {
            if (!inv) {
                unary(k + s, [this, k](Num x) {
                    const Num r = toRadians(x);
                    const Num v = k == QLatin1String("sin") ? std::sin(r) : k == QLatin1String("cos") ? std::cos(r) : std::tan(r);
                    return std::fabs(v) < 1e-15L ? Num(0) : v;
                });
            } else {
                const Num x = value();
                if (k != QLatin1String("tan") && (x < -1 || x > 1)) {
                    showError(QStringLiteral("Неприпустиме введення"));
                    return;
                }
                unary(QStringLiteral("a") + k + s, [this, k](Num x) {
                    return fromRadians(k == QLatin1String("sin") ? std::asin(x) : k == QLatin1String("cos") ? std::acos(x) : std::atan(x));
                });
            }
        } else if (k == QLatin1String("sinh") || k == QLatin1String("cosh") || k == QLatin1String("tanh")) {
            if (!inv) {
                unary(k, [k](Num x) { return k == QLatin1String("sinh") ? std::sinh(x) : k == QLatin1String("cosh") ? std::cosh(x) : std::tanh(x); });
            } else {
                unary(QStringLiteral("a") + k, [k](Num x) { return k == QLatin1String("sinh") ? std::asinh(x) : k == QLatin1String("cosh") ? std::acosh(x) : std::atanh(x); });
            }
        } else if (k == QLatin1String("ln")) {
            if (inv) {
                unary(QStringLiteral("powe"), [](Num x) { return std::exp(x); });
            } else if (value() <= 0) {
                showError(QStringLiteral("Неприпустиме введення"));
                return;
            } else {
                unary(QStringLiteral("ln"), [](Num x) { return std::log(x); });
            }
        } else if (k == QLatin1String("log")) {
            if (value() <= 0) {
                showError(QStringLiteral("Неприпустиме введення"));
                return;
            }
            unary(QStringLiteral("log"), [](Num x) { return std::log10(x); });
        } else if (k == QLatin1String("10ˣ")) {
            unary(QStringLiteral("powten"), [](Num x) { return std::pow(Num(10), x); });
        } else if (k == QLatin1String("x²")) {
            unary(QStringLiteral("sqr"), [](Num x) { return x * x; });
        } else if (k == QLatin1String("x³")) {
            unary(QStringLiteral("cube"), [](Num x) { return x * x * x; });
        } else if (k == QLatin1String("∛x")) {
            unary(QStringLiteral("cuberoot"), [](Num x) { return std::cbrt(x); });
        } else if (k == QLatin1String("n!")) {
            const Num x = value();
            if (x < 0 && std::fmod(x, 1.0L) == 0) {
                showError(QStringLiteral("Неприпустиме введення"));
                return;
            }
            unary(QStringLiteral("fact"), [](Num x) { return std::tgamma(x + 1); });
        } else if (k == QLatin1String("Int")) {
            if (inv) {
                unary(QStringLiteral("frac"), [](Num x) { return x - std::trunc(x); });
            } else {
                unary(QStringLiteral("int"), [](Num x) { return std::trunc(x); });
            }
        } else if (k == QLatin1String("dms")) {
            if (inv) {
                // 12,3045 (12°30'45") -> 12,5125
                unary(QStringLiteral("degrees"), [](Num x) {
                    const Num sign = x < 0 ? -1 : 1;
                    x = std::fabs(x);
                    const Num deg = std::trunc(x);
                    const Num minutes = std::trunc((x - deg) * 100 + 1e-9L);
                    const Num seconds = ((x - deg) * 100 - minutes) * 100;
                    return sign * (deg + minutes / 60 + seconds / 3600);
                });
            } else {
                unary(QStringLiteral("dms"), [](Num x) {
                    const Num sign = x < 0 ? -1 : 1;
                    x = std::fabs(x);
                    const Num deg = std::trunc(x);
                    const Num m = (x - deg) * 60;
                    const Num minutes = std::trunc(m + 1e-9L);
                    const Num seconds = (m - minutes) * 60;
                    return sign * (deg + minutes / 100 + seconds / 10000);
                });
            }
        } else if (k == QLatin1String("xʸ")) {
            binaryOp(inv ? QLatin1Char('r') : QLatin1Char('^'));
        } else if (k == QLatin1String("ʸ√x")) {
            binaryOp(QLatin1Char('r'));
        } else if (k == QLatin1String("Mod")) {
            binaryOp(QLatin1Char('m'));
        }
        done();
    }

    void updateInvLabels()
    {
        for (auto it = m_invKeys.cbegin(); it != m_invKeys.cend(); ++it) {
            Key *key = it.value();
            const QString base = it.key();
            QString label = base;
            if (m_inv) {
                if (base == QLatin1String("sin") || base == QLatin1String("cos") || base == QLatin1String("tan") || base == QLatin1String("sinh")
                    || base == QLatin1String("cosh") || base == QLatin1String("tanh")) {
                    label = base + QStringLiteral("⁻¹");
                } else if (base == QLatin1String("ln")) {
                    label = QStringLiteral("eˣ");
                } else if (base == QLatin1String("Int")) {
                    label = QStringLiteral("Frac");
                } else if (base == QLatin1String("dms")) {
                    label = QStringLiteral("deg");
                } else if (base == QLatin1String("π")) {
                    label = QStringLiteral("2π");
                }
            }
            key->setText(label);
        }
    }

    /* ----- memory ----- */

    void memClear()
    {
        m_memory = 0;
        m_hasMemory = false;
        refresh();
    }
    void memRecall()
    {
        if (!m_hasMemory) {
            return;
        }
        startTyping();
        setEntryNum(m_memory);
        m_fresh = true;
        refresh();
    }
    void memStore()
    {
        if (m_error) {
            return;
        }
        m_memory = value();
        m_hasMemory = true;
        m_fresh = true;
        refresh();
    }
    void memAdd(int sign)
    {
        if (m_error) {
            return;
        }
        m_memory = m_mode == Programmer ? Num(norm(m_memory + sign * value())) : m_memory + sign * value();
        m_hasMemory = true;
        m_fresh = true;
        refresh();
    }

    /* ----- statistics ----- */

    void statAdd()
    {
        if (m_error) {
            clearAll();
            return;
        }
        const Num v = value();
        m_stats.append(v);
        auto *item = new QListWidgetItem(numToText(v, false), m_display->list);
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_display->list->scrollToBottom();
        m_fresh = true;
        refresh();
    }

    void statClearAll()
    {
        m_stats.clear();
        m_display->list->clear();
        refresh();
    }

    void statFunction(const QString &which)
    {
        const int n = m_stats.size();
        Num sum = 0, sumSq = 0;
        for (Num v : std::as_const(m_stats)) {
            sum += v;
            sumSq += v * v;
        }
        Num r = 0;
        if (which == QLatin1String("Σx")) {
            r = sum;
        } else if (which == QLatin1String("Σx²")) {
            r = sumSq;
        } else if (n == 0) {
            showError(QStringLiteral("Неприпустиме введення"));
            return;
        } else if (which == QLatin1String("x̄")) {
            r = sum / n;
        } else if (which == QLatin1String("x̄²")) {
            r = sumSq / n;
        } else {
            const Num mean = sum / n;
            Num dev = 0;
            for (Num v : std::as_const(m_stats)) {
                dev += (v - mean) * (v - mean);
            }
            if (which == QLatin1String("σₙ")) {
                r = std::sqrt(dev / n);
            } else {
                if (n < 2) {
                    showError(QStringLiteral("Неприпустиме введення"));
                    return;
                }
                r = std::sqrt(dev / (n - 1));
            }
        }
        startTyping();
        setEntryNum(r);
        m_fresh = true;
        refresh();
    }

    /* ----- history ----- */

    void addHistory(const QString &expression, Num result)
    {
        if (m_mode != Standard && m_mode != Scientific) {
            return;
        }
        auto *item = new QListWidgetItem(expression);
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        item->setData(Qt::UserRole, double(result));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        m_history.append({expression, result});
        if (m_historyOn) {
            m_display->list->blockSignals(true);
            m_display->list->addItem(item);
            m_display->list->setCurrentItem(nullptr);
            m_display->list->scrollToBottom();
            m_display->list->blockSignals(false);
        } else {
            delete item;
        }
    }

    void fillHistoryList()
    {
        m_display->list->blockSignals(true);
        m_display->list->clear();
        for (const auto &[expression, result] : std::as_const(m_history)) {
            auto *item = new QListWidgetItem(expression, m_display->list);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            item->setData(Qt::UserRole, double(result));
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
        m_display->list->scrollToBottom();
        m_display->list->blockSignals(false);
    }

    void recallHistory(QListWidgetItem *item)
    {
        if (!item) {
            return;
        }
        resetCalc();
        m_error = false;
        setEntryNum(Num(item->data(Qt::UserRole).toDouble()));
        m_fresh = true;
        m_afterEquals = true;
        refresh();
    }

    /** Evaluates an edited history line: numbers, + - * / Mod ^ and parentheses */
    std::optional<Num> evaluate(const QString &text) const
    {
        struct Tok {
            int kind; // 0 number, 1 operator, 2 '(', 3 ')'
            Num v;
            QChar op;
        };
        QList<Tok> toks;
        QString s = text;
        s.remove(QLatin1Char(' ')).remove(QChar(0x00a0));
        s.replace(QStringLiteral("Mod"), QStringLiteral("m"));
        bool expectNumber = true;
        for (int i = 0; i < s.size();) {
            const QChar c = s.at(i);
            if (c.isDigit() || c == kComma || c == QLatin1Char('.') || (expectNumber && c == QLatin1Char('-'))) {
                int j = i + 1;
                while (j < s.size() && (s.at(j).isDigit() || s.at(j) == kComma || s.at(j) == QLatin1Char('.')
                                        || s.at(j) == QLatin1Char('e')
                                        || ((s.at(j) == QLatin1Char('+') || s.at(j) == QLatin1Char('-')) && s.at(j - 1) == QLatin1Char('e')))) {
                    ++j;
                }
                QString num = s.mid(i, j - i);
                num.replace(kComma, QLatin1Char('.'));
                bool ok = false;
                const double v = num.toDouble(&ok);
                if (!ok) {
                    return std::nullopt;
                }
                toks.append({0, Num(v), QChar()});
                i = j;
                expectNumber = false;
            } else if (QStringLiteral("+-*/m^").contains(c)) {
                toks.append({1, 0, c});
                ++i;
                expectNumber = true;
            } else if (c == QLatin1Char('(')) {
                toks.append({2, 0, c});
                ++i;
                expectNumber = true;
            } else if (c == QLatin1Char(')')) {
                toks.append({3, 0, c});
                ++i;
                expectNumber = false;
            } else {
                return std::nullopt;
            }
        }
        QList<Num> vals;
        QList<QChar> ops;
        auto prec = [this](QChar o) {
            return precedence(o);
        };
        auto reduce = [&]() -> bool {
            if (vals.size() < 2 || ops.isEmpty()) {
                return false;
            }
            const QChar o = ops.takeLast();
            const Num b = vals.takeLast(), a = vals.takeLast();
            Num r = 0;
            switch (o.unicode()) {
            case '+': r = a + b; break;
            case '-': r = a - b; break;
            case '*': r = a * b; break;
            case '/': if (b == 0) return false; r = a / b; break;
            case 'm': if (b == 0) return false; r = std::fmod(a, b); break;
            case '^': r = std::pow(a, b); break;
            default: return false;
            }
            vals.append(r);
            return true;
        };
        for (const Tok &t : std::as_const(toks)) {
            if (t.kind == 0) {
                vals.append(t.v);
            } else if (t.kind == 2) {
                ops.append(QLatin1Char('('));
            } else if (t.kind == 3) {
                while (!ops.isEmpty() && ops.last() != QLatin1Char('(')) {
                    if (!reduce()) {
                        return std::nullopt;
                    }
                }
                if (ops.isEmpty()) {
                    return std::nullopt;
                }
                ops.removeLast();
            } else {
                while (!ops.isEmpty() && ops.last() != QLatin1Char('(') && prec(ops.last()) >= prec(t.op)) {
                    if (!reduce()) {
                        return std::nullopt;
                    }
                }
                ops.append(t.op);
            }
        }
        while (!ops.isEmpty()) {
            if (ops.last() == QLatin1Char('(')) {
                ops.removeLast();
                continue;
            }
            if (!reduce()) {
                return std::nullopt;
            }
        }
        if (vals.size() != 1 || !std::isfinite(double(vals.first()))) {
            return std::nullopt;
        }
        return vals.first();
    }

    void historyEdited(QListWidgetItem *item)
    {
        if (!item) {
            return;
        }
        const int row = m_display->list->row(item);
        const auto r = evaluate(item->text());
        if (!r) {
            showError(QStringLiteral("Неприпустиме введення"));
            return;
        }
        item->setData(Qt::UserRole, double(*r));
        if (row >= 0 && row < m_history.size()) {
            m_history[row] = {item->text(), *r};
        }
        recallHistory(item);
    }

    /* ----- modes ----- */

    void setRadix(int radix)
    {
        if (m_mode != Programmer) {
            return;
        }
        const qint64 v = currentInt();
        m_radix = radix;
        m_settings.setValue(QStringLiteral("Radix"), radix);
        if (!m_error) {
            setEntryNum(Num(v));
        }
        for (auto it = m_radixButtons.cbegin(); it != m_radixButtons.cend(); ++it) {
            it.value()->setChecked(it.key() == radix);
        }
        updateDigitKeys();
        refresh();
    }

    void setWordBits(int bits)
    {
        if (m_mode != Programmer) {
            return;
        }
        const qint64 v = currentInt();
        m_wordBits = bits;
        m_settings.setValue(QStringLiteral("WordBits"), bits);
        if (!m_error) {
            setEntryNum(Num(normU(quint64(v))));
        }
        for (auto it = m_wordButtons.cbegin(); it != m_wordButtons.cend(); ++it) {
            it.value()->setChecked(it.key() == bits);
        }
        refresh();
    }

    void updateDigitKeys()
    {
        for (int i = 0; i < 16; ++i) {
            if (m_digitKeys[i]) {
                m_digitKeys[i]->setEnabled(m_mode != Programmer || i < m_radix);
            }
        }
    }

    Key *makeKey(QGridLayout *grid, const QString &text, int row, int col, std::function<void()> fn, int rowSpan = 1, int colSpan = 1)
    {
        const QSize keySize(34, 27);
        auto *k = new Key(text);
        k->m_memory = text == QLatin1String("MC") || text == QLatin1String("MR") || text == QLatin1String("MS") || text == QLatin1String("M+")
            || text == QLatin1String("M-");
        k->setFixedSize(keySize.width() * colSpan + 5 * (colSpan - 1), keySize.height() * rowSpan + 5 * (rowSpan - 1));
        if (fn) {
            connect(k, &QPushButton::clicked, this, std::move(fn));
        } else {
            k->setEnabled(false);
        }
        grid->addWidget(k, row, col, rowSpan, colSpan);
        return k;
    }

    /** MC..M-, ←/CE/C/±/√, digits, operators: the right part shared by Standard, Scientific and Programmer */
    void addStandardKeys(QGridLayout *g, int c)
    {
        const bool prog = m_mode == Programmer;
        makeKey(g, QStringLiteral("MC"), 0, c, [this] { memClear(); });
        makeKey(g, QStringLiteral("MR"), 0, c + 1, [this] { memRecall(); });
        makeKey(g, QStringLiteral("MS"), 0, c + 2, [this] { memStore(); });
        makeKey(g, QStringLiteral("M+"), 0, c + 3, [this] { memAdd(1); });
        makeKey(g, QStringLiteral("M-"), 0, c + 4, [this] { memAdd(-1); });
        makeKey(g, QStringLiteral("←"), 1, c, [this] { backspace(); });
        makeKey(g, QStringLiteral("CE"), 1, c + 1, [this] { clearEntry(); });
        makeKey(g, QStringLiteral("C"), 1, c + 2, [this] { clearAll(); });
        makeKey(g, QStringLiteral("±"), 1, c + 3, [this] { negate(); });
        makeKey(g, QStringLiteral("√"), 1, c + 4, prog ? std::function<void()>() : [this] { stdUnary(QStringLiteral("sqrt")); });
        const int digits[3][3] = {{7, 8, 9}, {4, 5, 6}, {1, 2, 3}};
        for (int r = 0; r < 3; ++r) {
            for (int i = 0; i < 3; ++i) {
                const int d = digits[r][i];
                m_digitKeys[d] = makeKey(g, QString::number(d), 2 + r, c + i, [this, d] { digit(QString::number(d)); });
                m_digitKeys[d]->m_light = true;
            }
        }
        makeKey(g, QStringLiteral("/"), 2, c + 3, [this] { binaryOp(QLatin1Char('/')); });
        makeKey(g, QStringLiteral("%"), 2, c + 4, prog ? std::function<void()>() : [this] { percent(); });
        makeKey(g, QStringLiteral("*"), 3, c + 3, [this] { binaryOp(QLatin1Char('*')); });
        makeKey(g, QStringLiteral("1/x"), 3, c + 4, prog ? std::function<void()>() : [this] { stdUnary(QStringLiteral("reciproc")); });
        makeKey(g, QStringLiteral("-"), 4, c + 3, [this] { binaryOp(QLatin1Char('-')); });
        makeKey(g, QStringLiteral("="), 4, c + 4, [this] { equals(); }, 2, 1);
        m_digitKeys[0] = makeKey(g, QStringLiteral("0"), 5, c, [this] { digit(QStringLiteral("0")); }, 1, 2);
        m_digitKeys[0]->m_light = true;
        makeKey(g, QString(kComma), 5, c + 2, prog ? std::function<void()>() : [this] { point(); })->m_light = true;
        makeKey(g, QStringLiteral("+"), 5, c + 3, [this] { binaryOp(QLatin1Char('+')); });
    }

    QWidget *buildKeypad()
    {
        auto *w = new QWidget;
        auto *g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(5);
        for (Key *&k : m_digitKeys) {
            k = nullptr;
        }
        m_invKeys.clear();
        m_radixButtons.clear();
        m_wordButtons.clear();

        if (m_mode == Standard) {
            addStandardKeys(g, 0);
        } else if (m_mode == Scientific) {
            auto *angles = radioFrame();
            auto *ah = new QHBoxLayout(angles);
            ah->setContentsMargins(6, 0, 6, 0);
            auto *angleGroup = new QButtonGroup(angles);
            const QStringList names{QStringLiteral("Градуси"), QStringLiteral("Радіани"), QStringLiteral("Гради")};
            for (int i = 0; i < 3; ++i) {
                auto *r = new QRadioButton(names.at(i));
                r->setFocusPolicy(Qt::NoFocus);
                QFont f = r->font();
                f.setPixelSize(11);
                r->setFont(f);
                r->setChecked(i == m_angle);
                angleGroup->addButton(r, i);
                ah->addWidget(r);
            }
            connect(angleGroup, &QButtonGroup::idClicked, this, [this](int id) {
                m_angle = id;
                m_settings.setValue(QStringLiteral("Angle"), id);
            });
            angles->setFixedHeight(27);
            g->addWidget(angles, 0, 0, 1, 5);
            makeKey(g, QString(), 1, 0, nullptr);
            m_invKey = makeKey(g, QStringLiteral("Inv"), 1, 1, [this] { sciKey(QStringLiteral("Inv")); });
            m_invKey->setCheckable(true);
            auto sci = [this, g](const QString &label, int r, int c) {
                Key *k = makeKey(g, label, r, c, [this, label] { sciKey(label); });
                m_invKeys.insert(label, k);
                return k;
            };
            sci(QStringLiteral("ln"), 1, 2);
            makeKey(g, QStringLiteral("("), 1, 3, [this] { openParen(); });
            makeKey(g, QStringLiteral(")"), 1, 4, [this] { closeParen(); });
            sci(QStringLiteral("Int"), 2, 0);
            sci(QStringLiteral("sinh"), 2, 1);
            sci(QStringLiteral("sin"), 2, 2);
            sci(QStringLiteral("x²"), 2, 3);
            sci(QStringLiteral("n!"), 2, 4);
            sci(QStringLiteral("dms"), 3, 0);
            sci(QStringLiteral("cosh"), 3, 1);
            sci(QStringLiteral("cos"), 3, 2);
            sci(QStringLiteral("xʸ"), 3, 3);
            sci(QStringLiteral("ʸ√x"), 3, 4);
            sci(QStringLiteral("π"), 4, 0);
            sci(QStringLiteral("tanh"), 4, 1);
            sci(QStringLiteral("tan"), 4, 2);
            sci(QStringLiteral("x³"), 4, 3);
            sci(QStringLiteral("∛x"), 4, 4);
            m_feKey = makeKey(g, QStringLiteral("F-E"), 5, 0, [this] { sciKey(QStringLiteral("F-E")); });
            m_feKey->setCheckable(true);
            m_feKey->setChecked(m_fe);
            makeKey(g, QStringLiteral("Exp"), 5, 1, [this] { sciKey(QStringLiteral("Exp")); });
            makeKey(g, QStringLiteral("Mod"), 5, 2, [this] { sciKey(QStringLiteral("Mod")); });
            sci(QStringLiteral("log"), 5, 3);
            sci(QStringLiteral("10ˣ"), 5, 4);
            addStandardKeys(g, 5);
            m_inv = false;
        } else if (m_mode == Programmer) {
            auto radios = [this](const QList<std::pair<int, QString>> &items, QMap<int, QRadioButton *> &store, int current, const std::function<void(int)> &set) {
                auto *f = radioFrame();
                auto *v = new QVBoxLayout(f);
                v->setContentsMargins(8, 4, 8, 4);
                v->setSpacing(2);
                auto *group = new QButtonGroup(f);
                for (const auto &[id, text] : items) {
                    auto *r = new QRadioButton(text);
                    r->setFocusPolicy(Qt::NoFocus);
                    QFont rf = r->font();
                    rf.setPixelSize(11);
                    r->setFont(rf);
                    r->setChecked(id == current);
                    group->addButton(r, id);
                    store.insert(id, r);
                    v->addWidget(r);
                }
                connect(group, &QButtonGroup::idClicked, this, set);
                f->setFixedWidth(88);
                return f;
            };
            g->addWidget(radios({{16, QStringLiteral("Hex")}, {10, QStringLiteral("Dec")}, {8, QStringLiteral("Oct")}, {2, QStringLiteral("Bin")}}, m_radixButtons, m_radix,
                                [this](int id) { setRadix(id); }),
                         0, 0, 3, 1);
            g->addWidget(radios({{64, QStringLiteral("8 байтів")}, {32, QStringLiteral("4 байти")}, {16, QStringLiteral("2 байти")}, {8, QStringLiteral("1 байт")}},
                                m_wordButtons, m_wordBits, [this](int id) { setWordBits(id); }),
                         3, 0, 3, 1);
            makeKey(g, QString(), 0, 1, nullptr);
            makeKey(g, QStringLiteral("Mod"), 0, 2, [this] { binaryOp(QLatin1Char('m')); });
            makeKey(g, QStringLiteral("("), 1, 1, [this] { openParen(); });
            makeKey(g, QStringLiteral(")"), 1, 2, [this] { closeParen(); });
            makeKey(g, QStringLiteral("RoL"), 2, 1, [this] { progUnary(QStringLiteral("RoL")); });
            makeKey(g, QStringLiteral("RoR"), 2, 2, [this] { progUnary(QStringLiteral("RoR")); });
            makeKey(g, QStringLiteral("Or"), 3, 1, [this] { binaryOp(QLatin1Char('|')); });
            makeKey(g, QStringLiteral("Xor"), 3, 2, [this] { binaryOp(QLatin1Char('x')); });
            makeKey(g, QStringLiteral("Lsh"), 4, 1, [this] { binaryOp(QLatin1Char('<')); });
            makeKey(g, QStringLiteral("Rsh"), 4, 2, [this] { binaryOp(QLatin1Char('>')); });
            makeKey(g, QStringLiteral("Not"), 5, 1, [this] { progUnary(QStringLiteral("Not")); });
            makeKey(g, QStringLiteral("And"), 5, 2, [this] { binaryOp(QLatin1Char('&')); });
            const QString hex = QStringLiteral("ABCDEF");
            for (int i = 0; i < 6; ++i) {
                const QString d = hex.mid(i, 1);
                m_digitKeys[10 + i] = makeKey(g, d, i, 3, [this, d] { digit(d); });
                m_digitKeys[10 + i]->m_light = true;
            }
            addStandardKeys(g, 4);
            updateDigitKeys();
        } else { // Statistics
            makeKey(g, QStringLiteral("MC"), 0, 0, [this] { memClear(); });
            makeKey(g, QStringLiteral("MR"), 0, 1, [this] { memRecall(); });
            makeKey(g, QStringLiteral("MS"), 0, 2, [this] { memStore(); });
            makeKey(g, QStringLiteral("M+"), 0, 3, [this] { memAdd(1); });
            makeKey(g, QStringLiteral("M-"), 0, 4, [this] { memAdd(-1); });
            makeKey(g, QStringLiteral("←"), 1, 0, [this] { backspace(); });
            makeKey(g, QStringLiteral("CAD"), 1, 1, [this] { statClearAll(); });
            makeKey(g, QStringLiteral("C"), 1, 2, [this] { clearAll(); });
            m_feKey = makeKey(g, QStringLiteral("F-E"), 1, 3, [this] {
                m_fe = !m_fe;
                m_feKey->setChecked(m_fe);
                if (m_fresh) {
                    setEntryNum(value());
                }
                refresh();
            });
            m_feKey->setCheckable(true);
            m_feKey->setChecked(m_fe);
            makeKey(g, QStringLiteral("Exp"), 1, 4, [this] { exponentKey(); });
            const int digits[3][3] = {{7, 8, 9}, {4, 5, 6}, {1, 2, 3}};
            for (int r = 0; r < 3; ++r) {
                for (int i = 0; i < 3; ++i) {
                    const int d = digits[r][i];
                    makeKey(g, QString::number(d), 2 + r, i, [this, d] { digit(QString::number(d)); })->m_light = true;
                }
            }
            const QList<std::tuple<QString, int, int>> fns{{QStringLiteral("x̄"), 2, 3}, {QStringLiteral("x̄²"), 2, 4}, {QStringLiteral("Σx"), 3, 3},
                                                           {QStringLiteral("Σx²"), 3, 4}, {QStringLiteral("σₙ"), 4, 3}, {QStringLiteral("σₙ₋₁"), 4, 4}};
            for (const auto &[label, r, c] : fns) {
                makeKey(g, label, r, c, [this, label] { statFunction(label); });
            }
            makeKey(g, QStringLiteral("0"), 5, 0, [this] { digit(QStringLiteral("0")); }, 1, 2)->m_light = true;
            makeKey(g, QString(kComma), 5, 2, [this] { point(); })->m_light = true;
            makeKey(g, QStringLiteral("±"), 5, 3, [this] { negate(); });
            makeKey(g, QStringLiteral("Add"), 5, 4, [this] { statAdd(); });
        }
        return w;
    }

    void setMode(Mode mode, bool keepValue = true)
    {
        const Num v = (keepValue && !m_error) ? value() : 0;
        m_mode = mode;
        m_settings.setValue(QStringLiteral("Mode"), int(mode));
        if (m_keypad) {
            m_leftLayout->removeWidget(m_keypad);
            m_keypad->deleteLater();
        }
        m_keypad = buildKeypad();
        m_leftLayout->addWidget(m_keypad);
        m_bitView->setVisible(mode == Programmer);
        resetCalc();
        m_error = false;
        m_afterEquals = false;
        m_hasLastOp = false;
        setEntryNum(mode == Programmer ? Num(norm(v)) : v);
        m_fresh = true;
        // history list in Standard/Scientific, data list in Statistics
        if (mode == Statistics) {
            m_display->showList(true, true);
            m_display->list->clear();
            for (Num s : std::as_const(m_stats)) {
                auto *item = new QListWidgetItem(numToText(s, false), m_display->list);
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            }
        } else {
            const bool history = m_historyOn && (mode == Standard || mode == Scientific);
            m_display->showList(history, false);
            if (history) {
                fillHistoryList();
            }
        }
        if (m_modeActions.contains(mode)) {
            m_modeActions.value(mode)->setChecked(true);
        }
        m_historyAction->setEnabled(mode == Standard || mode == Scientific);
        updateHistoryEditActions();
        refresh(false);
        adjustSize();
    }

    void updateHistoryEditActions()
    {
        const bool on = m_historyOn && (m_mode == Standard || m_mode == Scientific);
        for (QAction *a : {m_copyHistory, m_editHistory, m_clearHistory}) {
            a->setEnabled(on);
        }
    }

    /* ----- menus ----- */

    void buildMenus()
    {
        // Windows 7 calculator menu bar: pale blue-white, light blue box on the hovered/open item
        menuBar()->setStyleSheet(QStringLiteral(
            "QMenuBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fbfbfd, stop:0.5 #eef0f6, stop:1 #e2e5ef);"
            " border: none; border-bottom: 1px solid #d3d8e2; }"
            "QMenuBar::item { background: transparent; padding: 3px 7px; margin: 1px 0px; color: #3c3c3c; border: 1px solid transparent; }"
            "QMenuBar::item:selected { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fdfeff, stop:1 #e4effa);"
            " border: 1px solid #a8c8ea; border-radius: 2px; }"
            "QMenuBar::item:pressed { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f6f9fd, stop:1 #e2eaf4);"
            " border: 1px solid #8e9fb4; border-radius: 2px; }"));
        QMenu *view = menuBar()->addMenu(QStringLiteral("&Вигляд"));
        auto *modes = new QActionGroup(this);
        const QList<std::tuple<Mode, QString, QString>> list{{Standard, QStringLiteral("&Звичайний"), QStringLiteral("Alt+1")},
                                                             {Scientific, QStringLiteral("&Інженерний"), QStringLiteral("Alt+2")},
                                                             {Programmer, QStringLiteral("&Програміст"), QStringLiteral("Alt+3")},
                                                             {Statistics, QStringLiteral("&Статистика"), QStringLiteral("Alt+4")}};
        for (const auto &[mode, text, key] : list) {
            QAction *a = view->addAction(text);
            a->setCheckable(true);
            a->setShortcut(QKeySequence(key));
            modes->addAction(a);
            m_modeActions.insert(mode, a);
            const Mode md = mode;
            connect(a, &QAction::triggered, this, [this, md] {
                setMode(md);
            });
        }
        view->addSeparator();
        m_historyAction = view->addAction(QStringLiteral("&Журнал"));
        m_historyAction->setCheckable(true);
        m_historyAction->setChecked(m_historyOn);
        m_historyAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
        connect(m_historyAction, &QAction::toggled, this, [this](bool on) {
            m_historyOn = on;
            m_settings.setValue(QStringLiteral("History"), on);
            setMode(m_mode);
        });
        QAction *grouping = view->addAction(QStringLiteral("&Групування цифр за розрядами"));
        grouping->setCheckable(true);
        grouping->setChecked(m_grouping);
        connect(grouping, &QAction::toggled, this, [this](bool on) {
            m_grouping = on;
            m_settings.setValue(QStringLiteral("Grouping"), on);
            refresh(false);
        });
        view->addSeparator();
        auto *panels = new QActionGroup(this);
        auto addPanel = [this, panels](QMenu *menu, Panel p, const QString &text, const QString &key) {
            QAction *a = menu->addAction(text);
            a->setCheckable(true);
            if (!key.isEmpty()) {
                a->setShortcut(QKeySequence(key));
            }
            panels->addAction(a);
            m_panelActions.insert(p, a);
            connect(a, &QAction::triggered, this, [this, p] {
                // clicking the selected panel again closes it
                setPanel(p != BasicPanel && p == m_panel ? BasicPanel : p);
            });
        };
        addPanel(view, BasicPanel, QStringLiteral("&Основний"), QStringLiteral("Ctrl+F4"));
        addPanel(view, UnitsPanel, QStringLiteral("&Перетворення одиниць"), QStringLiteral("Ctrl+U"));
        addPanel(view, DatesPanel, QStringLiteral("&Обчислення дати"), QStringLiteral("Ctrl+E"));
        QMenu *sheets = view->addMenu(QStringLiteral("&Листи"));
        addPanel(sheets, MortgagePanel, QStringLiteral("&Іпотека"), {});
        addPanel(sheets, LeasePanel, QStringLiteral("&Автолізинг"), {});
        addPanel(sheets, FuelMpgPanel, QStringLiteral("Економія пального (&миль на галон)"), {});
        addPanel(sheets, FuelLitresPanel, QStringLiteral("Економія пального (&л/100 км)"), {});

        QMenu *edit = menuBar()->addMenu(QStringLiteral("&Правка"));
        edit->setProperty("win7plain", true);
        edit->addAction(QStringLiteral("&Копіювати"), QKeySequence::Copy, this, [this] {
            QGuiApplication::clipboard()->setText(m_entry);
        });
        QAction *paste = edit->addAction(QStringLiteral("&Вставити"), QKeySequence::Paste, this, [this] {
            pasteNumber(QGuiApplication::clipboard()->text());
        });
        connect(edit, &QMenu::aboutToShow, paste, [paste] {
            paste->setEnabled(!QGuiApplication::clipboard()->text().trimmed().isEmpty());
        });
        edit->addSeparator();
        QMenu *history = edit->addMenu(QStringLiteral("&Журнал"));
        history->setProperty("win7plain", true);
        m_copyHistory = history->addAction(QStringLiteral("&Копіювати журнал"), this, [this] {
            QStringList lines;
            for (const auto &[expression, result] : std::as_const(m_history)) {
                lines << expression + QStringLiteral(" = ") + numToText(result, false);
            }
            QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
        });
        m_editHistory = history->addAction(QStringLiteral("&Змінити"), QKeySequence(Qt::Key_F2), this, [this] {
            QListWidget *l = m_display->list;
            if (!l->currentItem() && l->count() > 0) {
                l->setCurrentRow(l->count() - 1);
            }
            if (l->currentItem()) {
                l->editItem(l->currentItem());
            }
        });
        QAction *cancel = history->addAction(QStringLiteral("&Скасувати редагування\tEsc"), this, [this] {
            if (QWidget *editor = QApplication::focusWidget()) {
                QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QApplication::sendEvent(editor, &esc);
            }
        });
        connect(history, &QMenu::aboutToShow, cancel, [this, cancel] {
            cancel->setEnabled(m_display->list->isEditing());
        });
        m_clearHistory = history->addAction(QStringLiteral("&Очистити"), QKeySequence(QStringLiteral("Ctrl+Shift+D")), this, [this] {
            m_history.clear();
            if (m_mode != Statistics) {
                m_display->list->clear();
            }
        });

        QMenu *help = menuBar()->addMenu(QStringLiteral("&Довідка"));
        help->setProperty("win7plain", true);
        help->addAction(QStringLiteral("&Переглянути довідку"), QKeySequence(Qt::Key_F1), this, [this] {
            showHelp();
        });
        help->addSeparator();
        help->addAction(QStringLiteral("&Про програму"), this, [this] {
            QMessageBox about(this);
            about.setWindowTitle(QStringLiteral("Про програму «Калькулятор»"));
            about.setIconPixmap(QIcon::fromTheme(QStringLiteral("seven-calc")).pixmap(48, 48));
            about.setText(QStringLiteral("<b>Калькулятор</b>"));
            about.setInformativeText(QStringLiteral("Калькулятор у стилі Windows 7: звичайний, інженерний, програмістський і статистичний режими, "
                                                    "журнал, перетворення одиниць, обчислення дати та листи."));
            about.exec();
        });
        win7WrapMenuBar(this); // Windows 7: the menu bar wraps instead of showing a "»" arrow
    }

    void showHelp()
    {
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Довідка: Калькулятор"));
        auto *v = new QVBoxLayout(&dlg);
        auto *text = new QLabel(QStringLiteral(
            "<b>Клавіатура</b><br>"
            "Цифри, + − * /, Enter або = — обчислення<br>"
            "Esc — C (очистити все), Delete — CE, Backspace — ←, F9 — ±<br>"
            "@ — квадратний корінь, R — 1/x, % — відсоток<br>"
            "Ctrl+M / Ctrl+R / Ctrl+P / Ctrl+Q / Ctrl+L — MS, MR, M+, M−, MC<br><br>"
            "<b>Інженерний</b>: S sin, O cos, T tan, N ln, L log, Q x², # x³, Y xʸ, ! n!, P π, I Inv, V F-E, X Exp, ( )<br>"
            "<b>Програміст</b>: F5 Hex, F6 Dec, F7 Oct, F8 Bin, F12/F2/F3/F4 — 8/4/2/1 байти, &amp; And, | Or, ^ Xor, &lt; Lsh, &gt; Rsh, ~ Not<br>"
            "<b>Статистика</b>: Insert — Add<br><br>"
            "<b>Вигляд</b>: Alt+1…Alt+4 — режими, Ctrl+H — журнал, Ctrl+U — одиниці, Ctrl+E — дати, Ctrl+F4 — основний"));
        text->setWordWrap(true);
        text->setMinimumWidth(460);
        v->addWidget(text);
        auto *bb = new QDialogButtonBox;
        bb->addButton(QStringLiteral("Закрити"), QDialogButtonBox::AcceptRole);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        v->addWidget(bb);
        dlg.exec();
    }

    void pasteNumber(const QString &text)
    {
        QString t = text.trimmed();
        if (m_mode == Programmer) {
            t.remove(QLatin1Char(' '));
            bool ok = false;
            const quint64 u = t.toULongLong(&ok, m_radix);
            if (ok) {
                startTyping();
                setEntryNum(Num(normU(u)));
                refresh();
            }
            return;
        }
        if (const auto v = parseInput(t)) {
            startTyping();
            setEntryNum(Num(*v));
            refresh();
        }
    }

    /* ----- right panels ----- */

    void buildPanels()
    {
        m_panels->addWidget(new QWidget); // Basic: nothing
        m_panels->addWidget(buildUnitsPanel());
        m_panels->addWidget(buildDatesPanel());
        m_panels->addWidget(buildSheetPanel(mortgageSheet()));
        m_panels->addWidget(buildSheetPanel(leaseSheet()));
        m_panels->addWidget(buildSheetPanel(fuelSheet(false)));
        m_panels->addWidget(buildSheetPanel(fuelSheet(true)));
    }

    void setPanel(Panel p)
    {
        m_panel = p;
        m_panels->setCurrentIndex(int(p));
        m_panels->setVisible(p != BasicPanel);
        if (m_panelActions.contains(p)) {
            m_panelActions.value(p)->setChecked(true);
        }
        adjustSize();
    }

    QWidget *buildUnitsPanel()
    {
        auto *panel = whitePanel();
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 12, 12, 12);
        v->setSpacing(6);
        const QList<Category> cats = categories();
        v->addWidget(panelLabel(QStringLiteral("Виберіть тип перетворюваної одиниці")));
        auto *category = new QComboBox;
        for (const Category &c : cats) {
            category->addItem(c.name);
        }
        v->addWidget(category);
        v->addSpacing(6);
        v->addWidget(panelLabel(QStringLiteral("З")));
        m_unitFrom = valueEdit();
        v->addWidget(m_unitFrom);
        auto *fromUnit = new QComboBox;
        v->addWidget(fromUnit);
        v->addSpacing(6);
        v->addWidget(panelLabel(QStringLiteral("в")));
        auto *to = resultEdit();
        v->addWidget(to);
        auto *toUnit = new QComboBox;
        v->addWidget(toUnit);
        v->addStretch(1);

        auto convert = [=] {
            const Category &c = cats.at(qMax(0, category->currentIndex()));
            const auto input = parseInput(m_unitFrom->text());
            if (!input || fromUnit->currentIndex() < 0 || toUnit->currentIndex() < 0) {
                to->clear();
                return;
            }
            double result;
            if (c.temperature) {
                result = fromKelvin(toUnit->currentIndex(), toKelvin(fromUnit->currentIndex(), *input));
            } else {
                result = *input * c.units.at(fromUnit->currentIndex()).factor / c.units.at(toUnit->currentIndex()).factor;
            }
            to->setText(resultText(result));
        };
        auto fillUnits = [=] {
            const Category &c = cats.at(qMax(0, category->currentIndex()));
            for (QComboBox *box : {fromUnit, toUnit}) {
                box->blockSignals(true);
                box->clear();
                for (const Unit &u : c.units) {
                    box->addItem(u.name);
                }
                box->setCurrentIndex(0);
                box->blockSignals(false);
            }
            convert();
        };
        connect(category, &QComboBox::currentIndexChanged, panel, fillUnits);
        connect(fromUnit, &QComboBox::currentIndexChanged, panel, convert);
        connect(toUnit, &QComboBox::currentIndexChanged, panel, convert);
        connect(m_unitFrom, &QLineEdit::textChanged, panel, convert);
        fillUnits();
        return panel;
    }

    static DateField *dateEdit()
    {
        return new DateField;
    }

    static QString dateDifference(QDate a, QDate b)
    {
        if (a > b) {
            std::swap(a, b);
        }
        if (a == b) {
            return QStringLiteral("Однакові дати");
        }
        int months = (b.year() - a.year()) * 12 + (b.month() - a.month());
        if (a.addMonths(months) > b) {
            --months;
        }
        const QDate t = a.addMonths(months);
        const int days = t.daysTo(b);
        QStringList parts;
        if (months / 12) {
            parts << QStringLiteral("%1 р.").arg(months / 12);
        }
        if (months % 12) {
            parts << QStringLiteral("%1 міс.").arg(months % 12);
        }
        if (days / 7) {
            parts << QStringLiteral("%1 тиж.").arg(days / 7);
        }
        if (days % 7) {
            parts << QStringLiteral("%1 дн.").arg(days % 7);
        }
        return parts.join(QStringLiteral("; "));
    }

    QWidget *buildDatesPanel()
    {
        auto *panel = whitePanel();
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 12, 12, 12);
        v->setSpacing(6);
        v->addWidget(panelLabel(QStringLiteral("Виберіть потрібне обчислення дати")));
        auto *kind = new QComboBox;
        kind->addItem(QStringLiteral("Обчислення інтервалу між двома датами"));
        kind->addItem(QStringLiteral("Додавання або віднімання днів для вказаної дати"));
        v->addWidget(kind);
        auto *stack = new QStackedWidget;
        v->addWidget(stack);

        // interval between two dates
        auto *p1 = new QWidget;
        auto *v1 = new QVBoxLayout(p1);
        v1->setContentsMargins(0, 4, 0, 0);
        auto *r1 = new QHBoxLayout;
        r1->addWidget(panelLabel(QStringLiteral("З")));
        auto *from = dateEdit();
        r1->addWidget(from);
        r1->addStretch(1);
        r1->addWidget(panelLabel(QStringLiteral("По")));
        auto *till = dateEdit();
        r1->addWidget(till);
        v1->addLayout(r1);
        v1->addWidget(panelLabel(QStringLiteral("Різниця (роки, місяці, тижні, дні)")));
        auto *diff = resultEdit();
        v1->addWidget(diff);
        v1->addWidget(panelLabel(QStringLiteral("Різниця (дні)")));
        auto *diffDays = resultEdit();
        v1->addWidget(diffDays);
        v1->addStretch(1);
        stack->addWidget(p1);

        // add or subtract
        auto *p2 = new QWidget;
        auto *v2 = new QVBoxLayout(p2);
        v2->setContentsMargins(0, 4, 0, 0);
        auto *r2 = new QHBoxLayout;
        r2->addWidget(panelLabel(QStringLiteral("З")));
        auto *base = dateEdit();
        r2->addWidget(base);
        auto *add = new QRadioButton(QStringLiteral("Додати"));
        auto *sub = new QRadioButton(QStringLiteral("Відняти"));
        add->setChecked(true);
        r2->addWidget(add);
        r2->addWidget(sub);
        r2->addStretch(1);
        v2->addLayout(r2);
        auto *r3 = new QHBoxLayout;
        auto spin = [&r3](const QString &label) {
            r3->addWidget(panelLabel(label));
            auto *s = new QSpinBox;
            s->setRange(0, 9999);
            s->setFixedWidth(64);
            r3->addWidget(s);
            r3->addStretch(1);
            return s;
        };
        auto *years = spin(QStringLiteral("Роки"));
        auto *months = spin(QStringLiteral("Місяці"));
        auto *days = spin(QStringLiteral("Дні"));
        v2->addLayout(r3);
        v2->addWidget(panelLabel(QStringLiteral("Дата")));
        auto *resultDate = resultEdit();
        v2->addWidget(resultDate);
        v2->addStretch(1);
        stack->addWidget(p2);

        auto *btnRow = new QHBoxLayout;
        btnRow->addStretch(1);
        auto *calc = new QPushButton(QStringLiteral("Обчислити"));
        calc->setMinimumWidth(100);
        calc->setAutoDefault(false);
        btnRow->addWidget(calc);
        v->addLayout(btnRow);
        v->addStretch(1);

        connect(kind, &QComboBox::currentIndexChanged, stack, &QStackedWidget::setCurrentIndex);
        connect(calc, &QPushButton::clicked, panel, [=] {
            if (kind->currentIndex() == 0) {
                diff->setText(dateDifference(from->date(), till->date()));
                diffDays->setText(from->date() == till->date() ? QStringLiteral("Однакові дати")
                                                               : QStringLiteral("%1 дн.").arg(std::abs(from->date().daysTo(till->date()))));
            } else {
                const int s = add->isChecked() ? 1 : -1;
                const QDate d = base->date().addYears(s * years->value()).addMonths(s * months->value()).addDays(s * days->value());
                resultDate->setText(d.isValid() ? QLocale(QLocale::Ukrainian).toString(d, QStringLiteral("d MMMM yyyy 'р.'")) : QString());
            }
        });
        return panel;
    }

    QWidget *buildSheetPanel(const Sheet &sheet)
    {
        auto *panel = whitePanel();
        auto *v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 12, 12, 12);
        v->setSpacing(6);
        v->addWidget(panelLabel(QStringLiteral("Виберіть значення для обчислення")));
        auto *target = new QComboBox;
        for (int t : sheet.targets) {
            target->addItem(sheet.fields.at(t), t);
        }
        v->addWidget(target);
        auto *formHost = new QWidget;
        auto *form = new QGridLayout(formHost);
        form->setContentsMargins(0, 4, 0, 0);
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(6);
        form->setColumnStretch(0, 1);
        form->setColumnStretch(1, 1);
        v->addWidget(formHost);
        auto *row = new QHBoxLayout;
        auto *calc = new QPushButton(QStringLiteral("Обчислити"));
        calc->setMinimumWidth(100);
        calc->setAutoDefault(false);
        row->addWidget(calc);
        row->addStretch(1);
        auto *result = resultEdit();
        result->setFixedWidth(210);
        row->addWidget(result);
        v->addSpacing(10);
        v->addLayout(row);
        v->addStretch(1);

        auto values = std::make_shared<QMap<int, QString>>();
        auto edits = std::make_shared<QMap<int, QLineEdit *>>();
        auto rebuild = [=] {
            for (auto it = edits->cbegin(); it != edits->cend(); ++it) {
                values->insert(it.key(), it.value()->text());
            }
            while (QLayoutItem *item = form->takeAt(0)) {
                delete item->widget();
                delete item;
            }
            edits->clear();
            const int t = target->currentData().toInt();
            int r = 0;
            for (int i = 0; i < sheet.fields.size(); ++i) {
                if (i == t) {
                    continue;
                }
                form->addWidget(panelLabel(sheet.fields.at(i)), r, 0);
                auto *e = valueEdit();
                e->setText(values->value(i));
                form->addWidget(e, r, 1);
                edits->insert(i, e);
                ++r;
            }
            result->clear();
        };
        connect(target, &QComboBox::currentIndexChanged, panel, rebuild);
        connect(calc, &QPushButton::clicked, panel, [=] {
            QMap<int, double> in;
            for (auto it = edits->cbegin(); it != edits->cend(); ++it) {
                const auto v = parseInput(it.value()->text());
                if (!v) {
                    result->setText(QStringLiteral("Неприпустиме введення"));
                    it.value()->setFocus();
                    return;
                }
                in.insert(it.key(), *v);
            }
            const auto r = sheet.solve(target->currentData().toInt(), in);
            result->setText(r && std::isfinite(*r) ? resultText(*r) : QStringLiteral("Неприпустиме введення"));
        });
        rebuild();
        return panel;
    }

    /* ----- state ----- */

    QSettings m_settings;
    Mode m_mode = Standard;
    Panel m_panel = BasicPanel;
    bool m_historyOn = false;
    bool m_grouping = false;
    int m_radix = 10;
    int m_wordBits = 64;
    int m_angle = 0;
    bool m_inv = false;
    bool m_fe = false;

    QVBoxLayout *m_leftLayout = nullptr;
    DisplayBox *m_display = nullptr;
    BitView *m_bitView = nullptr;
    QWidget *m_keypad = nullptr;
    QStackedWidget *m_panels = nullptr;
    QLineEdit *m_unitFrom = nullptr;
    Key *m_digitKeys[16] = {};
    Key *m_invKey = nullptr;
    Key *m_feKey = nullptr;
    QMap<QString, Key *> m_invKeys;
    QMap<int, QRadioButton *> m_radixButtons;
    QMap<int, QRadioButton *> m_wordButtons;
    QMap<Mode, QAction *> m_modeActions;
    QMap<Panel, QAction *> m_panelActions;
    QAction *m_historyAction = nullptr;
    QAction *m_copyHistory = nullptr;
    QAction *m_editHistory = nullptr;
    QAction *m_clearHistory = nullptr;

    QString m_entry = QStringLiteral("0");
    QString m_expr;
    QList<Num> m_vals;
    QList<Op> m_ops;
    int m_depth = 0;
    bool m_fresh = false;
    bool m_error = false;
    bool m_afterEquals = false;
    bool m_lastWasOp = false;
    bool m_operandShown = false; // the current operand is already written in the expression (after sqrt, %, ...)
    QString m_operandText;
    bool m_expEntry = false;
    QChar m_lastOp;
    Num m_lastOperand = 0;
    bool m_hasLastOp = false;
    Num m_memory = 0;
    bool m_hasMemory = false;
    QList<Num> m_stats;
    QList<std::pair<QString, Num>> m_history;
};
}

void applyWin7Menus(QApplication &app)
{
    // Windows 7 menus: grey gutter on the left, dot/tick on a light blue square, soft blue hover
    const QString res = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/seven-calc/");
    app.setStyleSheet(QStringLiteral(
                          "QMenu { background-color: #f0f0f0; background-image: url(%1menu-gutter.png); background-repeat: repeat-y;"
                          " background-position: left; border: 1px solid #979797; padding: 2px; }"
                          "QMenu::item { padding: 4px 26px 4px 14px; border: 1px solid transparent; color: #000000; }"
                          "QMenu::item:selected { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f6f9fe, stop:1 #e5effc);"
                          " border: 1px solid #aecff7; border-radius: 3px; }"
                          "QMenu::item:disabled { color: #8d8d8d; }"
                          "QMenu::indicator { width: 26px; height: 24px; left: 6px; }"
                          "QMenu[win7plain=\"true\"] { min-width: 190px; }"
                          "QMenu[win7plain=\"true\"]::item { padding-left: 46px; padding-right: 30px; }"
                          "QMenu::indicator:exclusive:checked { image: url(%1menu-dot.png); }"
                          "QMenu::indicator:non-exclusive:checked { image: url(%1menu-tick.png); }"
                          "QMenu::separator { height: 1px; background: #d7d7d7; margin: 3px 2px 3px 42px; }"
                          "QMenu::right-arrow { width: 8px; height: 8px; right: 8px; }")
                          .arg(res));
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("seven-calc"));
    app.setApplicationDisplayName(QStringLiteral("Калькулятор"));
    app.setDesktopFileName(QStringLiteral("seven-calc"));
    applyWin7Menus(app);
    Calculator w;
    w.show();
    return app.exec();
}
