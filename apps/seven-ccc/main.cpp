/*
 * seven-ccc: AMD Catalyst™ Control Center of a Windows 7 laptop with Radeon graphics (Catalyst 14.x look),
 * for the AMD GPU of this Linux system. Only pages that really do something are shown; what they change is
 * described in cccbackend.h.
 *
 * seven-ccc [--page ID] [--apply] [--screenshot FILE]
 */
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QRadioButton>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QFrame>
#include <QSvgRenderer>
#include <QTreeWidget>
#include <QUrl>

#include <KWindowEffects>
#include <QPainterPath>

#include "cccbackend.h"
#include "cccui.h"

namespace Ccc
{

/* ---------------------------------------------------------------- pages */

class Page : public QWidget
{
public:
    QString id, title, description;
    std::function<void()> changed; // the window refreshes Apply / Discard
    virtual void load() {}
    virtual bool dirty() const { return false; }
    virtual bool apply() { return true; }
    virtual bool hasApply() const { return true; }
    virtual void defaults() {}
    virtual bool hasDefaults() const { return false; }

    QVBoxLayout *body = nullptr;
    void build()
    {
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto *scroll = new QScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        auto *w = new QWidget;
        w->setAutoFillBackground(true);
        QPalette pal = w->palette();
        pal.setColor(QPalette::Window, Qt::white);
        w->setPalette(pal);
        body = new QVBoxLayout(w);
        body->setContentsMargins(0, 0, 0, 10);
        body->setSpacing(0);
        if (!description.isEmpty()) {
            auto *d = new QLabel(description, w);
            d->setWordWrap(true);
            d->setFont(uiFont(12));
            d->setContentsMargins(10, 10, 10, 12);
            body->addWidget(d);
        }
        scroll->setWidget(w);
        outer->addWidget(scroll);
    }
    QWidget *box(QLayout *l, int left = 22)
    {
        auto *w = new QWidget;
        l->setContentsMargins(left, 8, 14, 12);
        w->setLayout(l);
        body->addWidget(w);
        return w;
    }
    void section(const QString &t) { body->addWidget(new SectionTitle(t)); }
    void notify()
    {
        if (changed) {
            changed();
        }
    }
};

inline QLabel *label(const QString &t, bool bold = false)
{
    auto *l = new QLabel(t);
    l->setFont(uiFont(12, bold ? QFont::DemiBold : QFont::Normal));
    l->setWordWrap(true);
    return l;
}

inline QString hz(double r)
{
    return QStringLiteral("%1 Гц").arg(qRound(r));
}

inline QString rotationName(int r)
{
    return r == 2 ? QStringLiteral("Книжкова (90°)") : r == 4 ? QStringLiteral("Альбомна, перевернута (180°)") : r == 8 ? QStringLiteral("Книжкова, перевернута (270°)") : QStringLiteral("Стандартна альбомна (0°)");
}

inline const QList<int> &rotations()
{
    static const QList<int> r{1, 2, 4, 8};
    return r;
}

/* ask to keep a new display mode, revert after 15 s like Catalyst / Windows */
inline bool confirmKeep(QWidget *parent)
{
    QMessageBox box(QMessageBox::Question, QStringLiteral("AMD Catalyst Control Center"), QString(), QMessageBox::Yes | QMessageBox::No, parent);
    box.button(QMessageBox::Yes)->setText(QStringLiteral("Так"));
    box.button(QMessageBox::No)->setText(QStringLiteral("Ні"));
    int left = 15;
    auto text = [&] { return QStringLiteral("Нові параметри дисплея застосовано.\nЗберегти ці параметри?\n\nПопередні параметри буде відновлено через %1 с.").arg(left); };
    box.setText(text());
    QTimer t;
    QObject::connect(&t, &QTimer::timeout, &box, [&] {
        if (--left <= 0) {
            box.reject();
        }
        box.setText(text());
    });
    t.start(1000);
    return box.exec() == QMessageBox::Yes;
}

/* which output a display page works on: a small selector when there is more than one */
class OutputPicker : public QWidget
{
public:
    OutputPicker(bool builtInOnly, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_builtIn(builtInOnly)
    {
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(10, 0, 10, 6);
        h->addWidget(label(QStringLiteral("Дисплей:")));
        combo = new QComboBox(this);
        combo->setMinimumWidth(260);
        h->addWidget(combo);
        h->addStretch(1);
    }
    QComboBox *combo;
    QList<Output> list;
    void reload()
    {
        const QString cur = combo->currentData().toString();
        list.clear();
        for (const Output &o : outputs()) {
            if (o.enabled && (!m_builtIn || o.builtIn())) {
                list << o;
            }
        }
        QSignalBlocker b(combo);
        combo->clear();
        for (const Output &o : list) {
            QString n = edidName(QString(), o.name);
            combo->addItem(QStringLiteral("%1%2 (%3)").arg(o.builtIn() ? QStringLiteral("Вбудований дисплей") : QStringLiteral("Монітор"), n.isEmpty() ? QString() : QStringLiteral(" ") + n, o.name), o.name);
        }
        const int i = combo->findData(cur);
        combo->setCurrentIndex(i >= 0 ? i : 0);
        setVisible(list.size() > 1);
    }
    Output current() const
    {
        const QString n = combo->currentData().toString();
        for (const Output &o : list) {
            if (o.name == n) {
                return o;
            }
        }
        return list.value(0);
    }

private:
    bool m_builtIn;
};

/* -------- Керування робочим столом > Властивості робочого стола */

class DesktopPropsPage : public Page
{
public:
    DesktopPropsPage(const QString &pid, const QString &t, bool builtIn)
    {
        id = pid;
        title = t;
        description = builtIn ? QStringLiteral("Перегляд і змінення параметрів вбудованого дисплея: роздільної здатності, частоти оновлення та повороту.")
                              : QStringLiteral("Змінення роздільної здатності, частоти оновлення та інших параметрів робочого стола.");
        build();
        picker = new OutputPicker(builtIn);
        body->addWidget(picker);
        section(QStringLiteral("Параметри поточного режиму"));
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(30);
        g->setVerticalSpacing(5);
        curType = label(QString());
        curMode = label(QString());
        curRot = label(QString());
        g->addWidget(label(QStringLiteral("Тип робочого стола:")), 0, 0);
        g->addWidget(curType, 0, 1);
        g->addWidget(label(QStringLiteral("Режим:")), 1, 0);
        g->addWidget(curMode, 1, 1);
        g->addWidget(label(QStringLiteral("Поворот:")), 2, 0);
        g->addWidget(curRot, 2, 1);
        g->setColumnStretch(1, 1);
        box(g);
        section(QStringLiteral("Параметри"));
        auto *f = new QGridLayout;
        f->setHorizontalSpacing(24);
        f->setVerticalSpacing(6);
        sizes = new QListWidget;
        sizes->setFixedSize(170, 150);
        f->addWidget(label(QStringLiteral("Розмір робочого стола (у пікселях):")), 0, 0);
        f->addWidget(sizes, 1, 0, 6, 1);
        depth = new QComboBox;
        depth->addItem(QStringLiteral("32 біти"));
        rate = new QComboBox;
        rot = new QComboBox;
        for (int r : rotations()) {
            rot->addItem(rotationName(r), r);
        }
        f->addWidget(label(QStringLiteral("Якість кольору:")), 1, 1);
        f->addWidget(depth, 2, 1);
        f->addWidget(label(QStringLiteral("Частота оновлення:")), 3, 1);
        f->addWidget(rate, 4, 1);
        f->addWidget(label(QStringLiteral("Поворот:")), 5, 1);
        f->addWidget(rot, 6, 1);
        f->setColumnStretch(2, 1);
        f->setRowStretch(7, 1);
        box(f);
        body->addStretch(1);
        QObject::connect(picker->combo, &QComboBox::currentIndexChanged, this, [this] { fill(); });
        QObject::connect(sizes, &QListWidget::currentRowChanged, this, [this] { fillRates(); notify(); });
        QObject::connect(rate, &QComboBox::currentIndexChanged, this, [this] { notify(); });
        QObject::connect(rot, &QComboBox::currentIndexChanged, this, [this] { notify(); });
    }
    void load() override
    {
        picker->reload();
        fill();
    }
    bool dirty() const override { return selectedMode() != o.currentMode || rot->currentData().toInt() != o.rotation; }
    bool hasDefaults() const override { return true; }
    void defaults() override
    {
        const Mode pm = o.mode(o.preferredMode.isEmpty() ? o.currentMode : o.preferredMode);
        for (int i = 0; i < sizes->count(); ++i) {
            if (sizes->item(i)->data(Qt::UserRole).toSize() == QSize(pm.w, pm.h)) {
                sizes->setCurrentRow(i);
            }
        }
        rate->setCurrentIndex(qMax(0, rate->findData(o.preferredMode)));
        rot->setCurrentIndex(0);
        notify();
    }
    bool apply() override
    {
        const Output old = o;
        const QString m = selectedMode();
        const int r = rot->currentData().toInt();
        if (!kscreen({QStringLiteral("output.%1.mode.%2").arg(o.name, m), QStringLiteral("output.%1.rotation.%2").arg(o.name, rotationWord(r))})) {
            QMessageBox::warning(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("Не вдалося застосувати параметри дисплея."));
            return false;
        }
        if (!confirmKeep(this)) {
            kscreen({QStringLiteral("output.%1.mode.%2").arg(old.name, old.currentMode), QStringLiteral("output.%1.rotation.%2").arg(old.name, rotationWord(old.rotation))});
        }
        load();
        return true;
    }

private:
    void fill()
    {
        o = picker->current();
        curType->setText(picker->list.size() > 1 ? QStringLiteral("Розширений робочий стіл") : QStringLiteral("Один"));
        const Mode cm = o.mode(o.currentMode);
        curMode->setText(QStringLiteral("%1 × %2, %3").arg(cm.w).arg(cm.h).arg(hz(cm.hz)));
        curRot->setText(rotationName(o.rotation));
        QSignalBlocker b1(sizes), b2(rot);
        sizes->clear();
        QList<QSize> seen;
        for (const Mode &m : o.modes) {
            if (!seen.contains(QSize(m.w, m.h))) {
                seen << QSize(m.w, m.h);
            }
        }
        std::sort(seen.begin(), seen.end(), [](QSize a, QSize b) { return a.width() * a.height() > b.width() * b.height(); });
        for (QSize s : seen) {
            auto *it = new QListWidgetItem(QStringLiteral("%1 × %2").arg(s.width()).arg(s.height()));
            it->setData(Qt::UserRole, s);
            sizes->addItem(it);
            if (s == QSize(cm.w, cm.h)) {
                sizes->setCurrentItem(it);
            }
        }
        rot->setCurrentIndex(qMax(0, rot->findData(o.rotation)));
        fillRates();
        rate->setCurrentIndex(qMax(0, rate->findData(o.currentMode)));
        notify();
    }
    void fillRates()
    {
        QSignalBlocker b(rate);
        const QSize s = sizes->currentItem() ? sizes->currentItem()->data(Qt::UserRole).toSize() : QSize();
        const QString keep = rate->currentData().toString();
        rate->clear();
        for (const Mode &m : o.modes) {
            if (QSize(m.w, m.h) == s) {
                rate->addItem(hz(m.hz), m.id);
            }
        }
        const int i = rate->findData(keep);
        rate->setCurrentIndex(i >= 0 ? i : 0);
    }
    QString selectedMode() const { return rate->currentData().toString(); }
    OutputPicker *picker;
    Output o;
    QLabel *curType, *curMode, *curRot;
    QListWidget *sizes;
    QComboBox *depth, *rate, *rot;
};

/* -------- colour: gamma / brightness / contrast per channel, with the curve */

class CurveView : public QWidget
{
public:
    Color c;
    CurveView() { setFixedSize(150, 150); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setPen(QColor(60, 60, 60));
        for (int i = 1; i < 4; ++i) {
            p.drawLine(i * width() / 4, 0, i * width() / 4, height());
            p.drawLine(0, i * height() / 4, width(), i * height() / 4);
        }
        p.setRenderHint(QPainter::Antialiasing);
        const QColor cols[3] = {QColor(255, 60, 60), QColor(60, 220, 60), QColor(80, 120, 255)};
        for (int ch = 2; ch >= 0; --ch) {
            QPolygonF poly;
            for (int k = 0; k <= 64; ++k) {
                const double x = k / 64.0;
                poly << QPointF(x * (width() - 1), (1 - ramp(c, ch, x)) * (height() - 1));
            }
            p.setPen(QPen(cols[ch], 1.5));
            p.drawPolyline(poly);
        }
        p.setPen(QColor(0xA4, 0xA4, 0xA4));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
};

class SliderRow
{
public:
    QSlider *slider;
    QDoubleSpinBox *spin;
    double scale;
    SliderRow(QGridLayout *g, int row, const QString &name, double lo, double hi, double step, int decimals, const QString &left, const QString &right)
        : scale(1.0 / step)
    {
        g->addWidget(label(name, true), row, 0, 1, 3);
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(qRound(lo * scale), qRound(hi * scale));
        slider->setMinimumWidth(260);
        spin = new QDoubleSpinBox;
        spin->setRange(lo, hi);
        spin->setDecimals(decimals);
        spin->setSingleStep(step);
        spin->setFixedWidth(80);
        auto *l = label(left);
        auto *r = label(right);
        l->setStyleSheet(QStringLiteral("color:#555"));
        r->setStyleSheet(QStringLiteral("color:#555"));
        g->addWidget(l, row + 1, 0);
        g->addWidget(slider, row + 1, 1);
        g->addWidget(r, row + 1, 2);
        g->addWidget(spin, row + 1, 3);
        // the slider drives the spin box (whose valueChanged the page listens to); the way back is silent
        QObject::connect(slider, &QSlider::valueChanged, spin, [this](int v) { spin->setValue(v / scale); });
        QObject::connect(spin, &QDoubleSpinBox::valueChanged, slider, [this](double v) {
            QSignalBlocker b(slider);
            slider->setValue(qRound(v * scale));
        });
    }
    double value() const { return spin->value(); }
    void set(double v)
    {
        QSignalBlocker b1(slider), b2(spin);
        spin->setValue(v);
        slider->setValue(qRound(v * scale));
    }
};

class ColorPage : public Page
{
public:
    ColorPage(const QString &pid, const QString &t, bool builtIn)
    {
        id = pid;
        title = t;
        description = QStringLiteral("Регулювання колірних характеристик робочого стола: гамми, яскравості та контрасту для всіх кольорів разом або для кожного окремо. "
                                     "Зміни можна переглянути до застосування; вони зберігаються й відновлюються після входу в систему.");
        build();
        picker = new OutputPicker(builtIn);
        body->addWidget(picker);
        section(QStringLiteral("Колір"));
        auto *top = new QHBoxLayout;
        auto *radios = new QVBoxLayout;
        group = new QButtonGroup(this);
        const QStringList names{QStringLiteral("Усі кольори"), QStringLiteral("Червоний"), QStringLiteral("Зелений"), QStringLiteral("Синій")};
        for (int i = 0; i < 4; ++i) {
            auto *r = new QRadioButton(names.at(i));
            r->setFont(uiFont(12));
            group->addButton(r, i);
            radios->addWidget(r);
        }
        group->button(0)->setChecked(true);
        radios->addStretch(1);
        top->addLayout(radios);
        top->addSpacing(30);
        curve = new CurveView;
        top->addWidget(curve);
        top->addStretch(1);
        box(top);
        section(QStringLiteral("Регулювання"));
        auto *g = new QGridLayout;
        g->setVerticalSpacing(4);
        gamma = new SliderRow(g, 0, QStringLiteral("Гамма"), 0.3, 2.8, 0.01, 2, QStringLiteral("0,3"), QStringLiteral("2,8"));
        bright = new SliderRow(g, 2, QStringLiteral("Яскравість"), -100, 100, 1, 0, QStringLiteral("-100"), QStringLiteral("100"));
        contrast = new SliderRow(g, 4, QStringLiteral("Контраст"), 0, 200, 1, 0, QStringLiteral("0"), QStringLiteral("200"));
        g->setColumnStretch(1, 1);
        box(g);
        body->addStretch(1);
        QObject::connect(group, &QButtonGroup::idClicked, this, [this] { showChannel(); });
        for (SliderRow *s : {gamma, bright, contrast}) {
            QObject::connect(s->spin, &QDoubleSpinBox::valueChanged, this, [this] { edit(); });
        }
        QObject::connect(picker->combo, &QComboBox::currentIndexChanged, this, [this] { fill(); });
    }
    ~ColorPage() override
    {
        if (c != saved && !m_out.isEmpty()) {
            applyColor(m_out, saved); // leaving without Apply: back to the saved colours
        }
    }
    void load() override
    {
        picker->reload();
        fill();
    }
    bool dirty() const override { return !(c == saved); }
    bool apply() override
    {
        saveColor(m_out, c);
        applyColor(m_out, c);
        saved = c;
        bool any = false;
        for (const Output &o : outputs()) {
            any |= !loadColor(o.name).isDefault();
        }
        setLoginApply(any);
        return true;
    }
    bool hasDefaults() const override { return true; }
    void defaults() override
    {
        c = Color();
        showChannel();
        preview();
    }
    void discardPreview()
    {
        c = saved;
        applyColor(m_out, c);
        showChannel();
    }

private:
    void fill()
    {
        if (!m_out.isEmpty() && !(c == saved)) {
            applyColor(m_out, saved);
        }
        m_out = picker->current().name;
        saved = c = loadColor(m_out);
        showChannel();
    }
    int channel() const { return group->checkedId(); } // 0 all, 1..3 r g b
    void showChannel()
    {
        const int ch = qMax(0, channel() - 1);
        gamma->set(c.gamma[ch]);
        bright->set(c.brightness[ch]);
        contrast->set(c.contrast[ch]);
        curve->c = c;
        curve->update();
        notify();
    }
    void edit()
    {
        const int sel = channel();
        for (int ch = 0; ch < 3; ++ch) {
            if (sel == 0 || sel - 1 == ch) {
                c.gamma[ch] = gamma->value();
                c.brightness[ch] = qRound(bright->value());
                c.contrast[ch] = qRound(contrast->value());
            }
        }
        preview();
    }
    void preview()
    {
        curve->c = c;
        curve->update();
        applyColor(m_out, c); // live preview, like Catalyst
        notify();
    }
    OutputPicker *picker;
    QString m_out;
    Color c, saved;
    QButtonGroup *group;
    CurveView *curve;
    SliderRow *gamma, *bright, *contrast;
};

/* -------- Загальні завдання дисплея > Повернути робочий стіл */

class RotationTile : public QAbstractButton
{
public:
    int rotation;
    RotationTile(int r)
        : rotation(r)
    {
        setCheckable(true);
        setFixedSize(132, 128);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (isChecked() || underMouse()) {
            // DisplayPaletteSelectionHighlightBrush / HoverHighlight
            QLinearGradient g(0, 0, 0, height());
            const int a = isChecked() ? 0x80 : 0x40;
            g.setColorAt(0, QColor(255, 255, 255, a));
            g.setColorAt(0.2, QColor(0x00, 0x99, 0x66, a));
            g.setColorAt(0.8, QColor(0x00, 0x99, 0x66, a));
            g.setColorAt(1, QColor(255, 255, 255, a));
            p.setPen(isChecked() ? QPen(QColor(0x5D, 0xB4, 0x93), 1) : Qt::NoPen);
            p.setBrush(g);
            p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        }
        const QPixmap pm = Ccc::icon(QStringLiteral("IconLCD"), 64);
        const int angle = rotation == 2 ? -90 : rotation == 4 ? 180 : rotation == 8 ? 90 : 0;
        p.save();
        p.translate(width() / 2.0, 46);
        p.rotate(angle);
        p.drawPixmap(QRectF(-32, -32, 64, 64), pm, pm.rect());
        p.restore();
        p.setPen(QColor(0x1E, 0x1E, 0x1E));
        p.setFont(uiFont(11));
        p.drawText(QRect(4, 86, width() - 8, 40), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, rotationName(rotation));
    }
};

class RotatePage : public Page
{
public:
    RotatePage()
    {
        id = QStringLiteral("tasks.rotate");
        title = QStringLiteral("Повернути робочий стіл");
        description = QStringLiteral("Повернення робочого стола для дисплеїв, які можна обертати. Виберіть потрібне положення та натисніть «Застосувати».");
        build();
        picker = new OutputPicker(false);
        body->addWidget(picker);
        section(QStringLiteral("Поворот"));
        auto *h = new QHBoxLayout;
        h->setSpacing(10);
        group = new QButtonGroup(this);
        for (int r : rotations()) {
            auto *t = new RotationTile(r);
            group->addButton(t, r);
            h->addWidget(t);
        }
        h->addStretch(1);
        box(h);
        body->addStretch(1);
        QObject::connect(group, &QButtonGroup::idClicked, this, [this] { notify(); });
        QObject::connect(picker->combo, &QComboBox::currentIndexChanged, this, [this] { fill(); });
    }
    void load() override
    {
        picker->reload();
        fill();
    }
    bool dirty() const override { return group->checkedId() != o.rotation; }
    bool hasDefaults() const override { return true; }
    void defaults() override
    {
        group->button(1)->setChecked(true);
        notify();
    }
    bool apply() override
    {
        const int old = o.rotation;
        kscreen({QStringLiteral("output.%1.rotation.%2").arg(o.name, rotationWord(group->checkedId()))});
        if (!confirmKeep(this)) {
            kscreen({QStringLiteral("output.%1.rotation.%2").arg(o.name, rotationWord(old))});
        }
        load();
        return true;
    }

private:
    void fill()
    {
        o = picker->current();
        if (auto *b = group->button(o.rotation)) {
            b->setChecked(true);
        }
        notify();
    }
    OutputPicker *picker;
    Output o;
    QButtonGroup *group;
};

/* -------- Загальні завдання дисплея > Додати чи видалити дисплей / Виявити дисплеї */

class DisplaysPage : public Page
{
public:
    DisplaysPage()
    {
        id = QStringLiteral("tasks.addremove");
        title = QStringLiteral("Додати чи видалити дисплей");
        description = QStringLiteral("Увімкнення та вимкнення підключених дисплеїв. Позначте дисплеї, на яких треба показувати робочий стіл.");
        build();
        section(QStringLiteral("Підключені дисплеї"));
        auto *v = new QVBoxLayout;
        list = new QTreeWidget;
        list->setHeaderLabels({QStringLiteral("Дисплей"), QStringLiteral("Підключення"), QStringLiteral("Режим")});
        list->setRootIsDecorated(false);
        list->setMinimumHeight(140);
        list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        v->addWidget(list);
        box(v);
        body->addStretch(1);
        QObject::connect(list, &QTreeWidget::itemChanged, this, [this] { notify(); });
    }
    void load() override
    {
        QSignalBlocker b(list);
        list->clear();
        state.clear();
        for (const Output &o : outputs()) {
            const QString n = edidName(QString(), o.name);
            auto *it = new QTreeWidgetItem(list, {(o.builtIn() ? QStringLiteral("Вбудований дисплей") : QStringLiteral("Монітор")) + (n.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(n)), o.name,
                                                  o.enabled ? QStringLiteral("%1 × %2, %3").arg(o.mode(o.currentMode).w).arg(o.mode(o.currentMode).h).arg(hz(o.mode(o.currentMode).hz)) : QStringLiteral("Вимкнено")});
            it->setIcon(0, QIcon(icon(o.builtIn() ? QStringLiteral("IconLCD") : QStringLiteral("IconDFP"), 32)));
            it->setCheckState(0, o.enabled ? Qt::Checked : Qt::Unchecked);
            it->setData(0, Qt::UserRole, o.name);
            state.insert(o.name, o.enabled);
        }
    }
    bool dirty() const override
    {
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            auto *it = list->topLevelItem(i);
            if ((it->checkState(0) == Qt::Checked) != state.value(it->data(0, Qt::UserRole).toString())) {
                return true;
            }
        }
        return false;
    }
    bool apply() override
    {
        QStringList args;
        int on = 0;
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            auto *it = list->topLevelItem(i);
            const bool e = it->checkState(0) == Qt::Checked;
            on += e;
            args << QStringLiteral("output.%1.%2").arg(it->data(0, Qt::UserRole).toString(), e ? QStringLiteral("enable") : QStringLiteral("disable"));
        }
        if (!on) {
            QMessageBox::warning(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("Має бути ввімкнено принаймні один дисплей."));
            return false;
        }
        kscreen(args);
        if (!confirmKeep(this)) {
            QStringList back;
            for (auto it = state.begin(); it != state.end(); ++it) {
                back << QStringLiteral("output.%1.%2").arg(it.key(), it.value() ? QStringLiteral("enable") : QStringLiteral("disable"));
            }
            kscreen(back);
        }
        load();
        return true;
    }

private:
    QTreeWidget *list;
    QHash<QString, bool> state;
};

class DetectPage : public Page
{
public:
    DetectPage()
    {
        id = QStringLiteral("tasks.detect");
        title = QStringLiteral("Виявити дисплеї");
        description = QStringLiteral("Пошук дисплеїв, підключених до комп'ютера. Використовуйте цю дію, якщо новий дисплей не з'явився автоматично.");
        build();
        section(QStringLiteral("Виявлення дисплеїв"));
        auto *v = new QVBoxLayout;
        auto *b = new QPushButton(QStringLiteral("Виявити дисплеї"));
        b->setFixedWidth(160);
        result = label(QString());
        v->addWidget(b);
        v->addSpacing(8);
        v->addWidget(result);
        box(v);
        body->addStretch(1);
        QObject::connect(b, &QPushButton::clicked, this, [this] {
            const QList<Output> o = outputs();
            QStringList names;
            for (const Output &x : o) {
                names << QStringLiteral("• %1 (%2)").arg(x.builtIn() ? QStringLiteral("Вбудований дисплей") : QStringLiteral("Монітор"), x.name);
            }
            result->setText(QStringLiteral("Знайдено дисплеїв: %1\n%2").arg(o.size()).arg(names.join(QLatin1Char('\n'))));
        });
    }
    bool hasApply() const override { return false; }

private:
    QLabel *result;
};

/* -------- Мої вбудовані дисплеї > Властивості */

class LcdInfoPage : public Page
{
public:
    LcdInfoPage()
    {
        id = QStringLiteral("lcd.props");
        title = QStringLiteral("Властивості (Вбудований дисплей)");
        description = QStringLiteral("Відомості про вбудований дисплей ноутбука та його можливості.");
        build();
        section(QStringLiteral("Відомості про дисплей"));
        auto *v = new QVBoxLayout;
        table = new QTreeWidget;
        table->setHeaderLabels({QStringLiteral("Елемент"), QStringLiteral("Значення")});
        table->setRootIsDecorated(false);
        table->setMinimumHeight(220);
        table->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        v->addWidget(table);
        box(v);
        body->addStretch(1);
    }
    bool hasApply() const override { return false; }
    void load() override
    {
        table->clear();
        for (const Output &o : outputs()) {
            if (!o.builtIn()) {
                continue;
            }
            const Mode pm = o.mode(o.preferredMode.isEmpty() ? o.currentMode : o.preferredMode);
            QSet<int> rates;
            for (const Mode &m : o.modes) {
                rates.insert(qRound(m.hz));
            }
            QList<int> rl(rates.begin(), rates.end());
            std::sort(rl.begin(), rl.end());
            QStringList rs;
            for (int r : rl) {
                rs << QString::number(r);
            }
            const double diag = std::sqrt(double(o.mmW) * o.mmW + double(o.mmH) * o.mmH) / 25.4;
            const QString n = edidName(QString(), o.name);
            const QList<Row> rows{
                {QStringLiteral("Назва дисплея"), n.isEmpty() ? QStringLiteral("Вбудована цифрова плоска панель") : n},
                {QStringLiteral("Тип підключення"), o.name.startsWith(QLatin1String("eDP")) ? QStringLiteral("Embedded DisplayPort (eDP)") : o.name},
                {QStringLiteral("Рідна роздільна здатність"), QStringLiteral("%1 × %2").arg(pm.w).arg(pm.h)},
                {QStringLiteral("Максимальна частота оновлення"), rl.isEmpty() ? QString() : hz(rl.last())},
                {QStringLiteral("Підтримувані частоти, Гц"), rs.join(QStringLiteral(", "))},
                {QStringLiteral("Фізичний розмір"), o.mmW ? QStringLiteral("%1 × %2 мм (%3\")").arg(o.mmW).arg(o.mmH).arg(diag, 0, 'f', 1) : QString()},
                {QStringLiteral("Поточний стан"), o.enabled ? QStringLiteral("Увімкнено") : QStringLiteral("Вимкнено")},
            };
            for (const Row &r : rows) {
                new QTreeWidgetItem(table, {r.item, r.value});
            }
        }
    }

private:
    QTreeWidget *table;
};

/* -------- Ігри > Налаштування 3D-програм */

class Gaming3DPage : public Page
{
public:
    Gaming3DPage()
    {
        id = QStringLiteral("game.3d");
        title = QStringLiteral("Налаштування 3D-програм");
        description = QStringLiteral("Глобальні параметри для 3D-програм. Синхронізація застосовується до програм OpenGL під час наступного запуску; "
                                     "анізотропна фільтрація — до програм Vulkan (зокрема ігор через DXVK) після наступного входу в систему.");
        build();
        section(QStringLiteral("Системні параметри"));
        auto *f = new QFormLayout;
        f->setHorizontalSpacing(24);
        f->setVerticalSpacing(8);
        vsync = new QComboBox;
        vsync->addItems({QStringLiteral("Завжди вимкнено"), QStringLiteral("Вимкнено, якщо програма не вказує інше"), QStringLiteral("Увімкнено, якщо програма не вказує інше"), QStringLiteral("Завжди ввімкнено")});
        aniso = new QComboBox;
        aniso->addItem(QStringLiteral("Використовувати параметри програми"), 0);
        for (int x : {2, 4, 8, 16}) {
            aniso->addItem(QStringLiteral("%1x").arg(x), x);
        }
        f->addRow(label(QStringLiteral("Очікувати вертикальне оновлення:")), vsync);
        f->addRow(label(QStringLiteral("Рівень анізотропної фільтрації:")), aniso);
        box(f);
        body->addStretch(1);
        QObject::connect(vsync, &QComboBox::currentIndexChanged, this, [this] { notify(); });
        QObject::connect(aniso, &QComboBox::currentIndexChanged, this, [this] { notify(); });
    }
    static QString envPath() { return QDir::homePath() + QStringLiteral("/.config/environment.d/90-seven-ccc.conf"); }
    static int savedAniso()
    {
        const auto m = QRegularExpression(QStringLiteral("RADV_TEX_ANISO=(\\d+)")).match(readFile(envPath()));
        return m.hasMatch() ? m.captured(1).toInt() : 0;
    }
    void load() override
    {
        QSignalBlocker b1(vsync), b2(aniso);
        m_v = vblankMode();
        m_a = savedAniso();
        vsync->setCurrentIndex(m_v);
        aniso->setCurrentIndex(qMax(0, aniso->findData(m_a)));
    }
    bool dirty() const override { return vsync->currentIndex() != m_v || aniso->currentData().toInt() != m_a; }
    bool hasDefaults() const override { return true; }
    void defaults() override
    {
        vsync->setCurrentIndex(1);
        aniso->setCurrentIndex(0);
        notify();
    }
    bool apply() override
    {
        bool ok = setVblankMode(vsync->currentIndex());
        const int a = aniso->currentData().toInt();
        QDir().mkpath(QFileInfo(envPath()).path());
        if (a) {
            QFile f(envPath());
            ok &= f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write(QStringLiteral("# AMD Catalyst Control Center: anisotropic filtering for Vulkan (RADV)\nRADV_TEX_ANISO=%1\n").arg(a).toUtf8());
        } else {
            QFile::remove(envPath());
        }
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("Не вдалося зберегти параметри 3D."));
        }
        load();
        return ok;
    }

private:
    QComboBox *vsync, *aniso;
    int m_v = 1, m_a = 0;
};

/* -------- Живлення > PowerPlay */

class PowerPlayPage : public Page
{
public:
    PowerPlayPage()
    {
        id = QStringLiteral("power.pp");
        title = QStringLiteral("PowerPlay™");
        description = QStringLiteral("Технологія PowerPlay™ керує енергоспоживанням графічного процесора й системи. Виберіть режим, який відповідає "
                                     "вашим потребам: максимальну продуктивність, баланс або найдовшу роботу від батареї.");
        build();
        section(QStringLiteral("Параметри PowerPlay™"));
        auto *v = new QVBoxLayout;
        enable = new QCheckBox(QStringLiteral("Увімкнути PowerPlay™"));
        enable->setChecked(true);
        enable->setEnabled(false);
        v->addWidget(enable);
        v->addSpacing(8);
        group = new QButtonGroup(this);
        const QList<QPair<QString, QString>> modes{{QStringLiteral("performance"), QStringLiteral("Максимальна продуктивність")},
                                                    {QStringLiteral("balanced"), QStringLiteral("Збалансований режим")},
                                                    {QStringLiteral("power-saver"), QStringLiteral("Максимальний час роботи батареї")}};
        const QStringList have = powerProfiles();
        int idx = 0;
        for (const auto &m : modes) {
            auto *r = new QRadioButton(m.second);
            r->setFont(uiFont(12));
            r->setProperty("profile", m.first);
            r->setEnabled(have.contains(m.first));
            group->addButton(r, idx++);
            v->addWidget(r);
        }
        box(v);
        section(QStringLiteral("Поточний стан"));
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(30);
        source = label(QString());
        clock = label(QString());
        g->addWidget(label(QStringLiteral("Джерело живлення:")), 0, 0);
        g->addWidget(source, 0, 1);
        g->addWidget(label(QStringLiteral("Поточна частота графічного процесора:")), 1, 0);
        g->addWidget(clock, 1, 1);
        g->setColumnStretch(1, 1);
        box(g);
        body->addStretch(1);
        QObject::connect(group, &QButtonGroup::idClicked, this, [this] { notify(); });
        auto *t = new QTimer(this);
        QObject::connect(t, &QTimer::timeout, this, [this] { status(); });
        t->start(1000);
    }
    void load() override
    {
        m_p = powerProfile();
        for (QAbstractButton *b : group->buttons()) {
            if (b->property("profile").toString() == m_p) {
                b->setChecked(true);
            }
        }
        status();
    }
    bool dirty() const override { return group->checkedButton() && group->checkedButton()->property("profile").toString() != m_p; }
    bool hasDefaults() const override { return true; }
    void defaults() override
    {
        for (QAbstractButton *b : group->buttons()) {
            if (b->property("profile").toString() == QLatin1String("balanced")) {
                b->setChecked(true);
            }
        }
        notify();
    }
    bool apply() override
    {
        const bool ok = setPowerProfile(group->checkedButton()->property("profile").toString());
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("Не вдалося змінити режим PowerPlay™."));
        }
        load();
        return ok;
    }

private:
    void status()
    {
        if (!isVisible()) {
            return;
        }
        source->setText(onBattery() ? QStringLiteral("Батарея") : QStringLiteral("Мережа змінного струму"));
        const int c = currentClock(amdGpu().card + QStringLiteral("/device/pp_dpm_sclk"));
        clock->setText(c ? QStringLiteral("%1 МГц").arg(c) : QStringLiteral("Невідомо"));
    }
    QCheckBox *enable;
    QButtonGroup *group;
    QLabel *source, *clock;
    QString m_p;
};

/* -------- Живлення > Перемикана графіка (AMD PowerXpress) */

inline const QStringList &pxStateNames()
{
    static const QStringList n{QStringLiteral("Не призначено"), QStringLiteral("Висока продуктивність"), QStringLiteral("Енергозбереження")};
    return n;
}
inline int pxStateIndex(const QString &s)
{
    return s == QLatin1String("perf") ? 1 : s == QLatin1String("save") ? 2 : 0;
}
inline QString pxStateKey(int i)
{
    return i == 1 ? QStringLiteral("perf") : i == 2 ? QStringLiteral("save") : QString();
}

/* desktop file id of a running executable, if exactly one menu entry starts it plainly (Exec with only %U-style
   arguments); else the program stands for itself. A script that only starts the real binary (google-chrome ->
   /opt/google/chrome/chrome) is matched by its folder. */
inline QString desktopIdForExe(const QString &exe)
{
    static QHash<QString, QStringList> byExe, byDir;
    static bool built = false;
    if (!built) {
        built = true;
        QSet<QString> ids;
        for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
            const QDir d(dir);
            for (const QString &f : d.entryList({QStringLiteral("*.desktop")}, QDir::Files)) {
                if (ids.contains(f)) {
                    continue; // a local copy hides the system one
                }
                ids.insert(f);
                const QString path = d.absoluteFilePath(f);
                if (desktopValue(path, QStringLiteral("NoDisplay")) == QLatin1String("true")) {
                    continue;
                }
                QStringList w = QProcess::splitCommand(desktopValue(path, QStringLiteral("Exec")));
                while (!w.isEmpty() && (w.first() == QLatin1String("env") || w.first().contains(QLatin1Char('=')))) {
                    w.removeFirst();
                }
                bool plain = true;
                for (int i = 1; i < w.size(); ++i) {
                    plain &= w.at(i).startsWith(QLatin1Char('%'));
                }
                const QString e = QFileInfo(desktopExec(path)).canonicalFilePath();
                if (e.isEmpty() || !plain) {
                    continue;
                }
                byExe[e] << f;
                if (!e.startsWith(QLatin1String("/usr/bin/")) && !e.startsWith(QLatin1String("/bin/"))) {
                    byDir[QFileInfo(e).path()] << f;
                }
            }
        }
    }
    const QString c = QFileInfo(exe).canonicalFilePath();
    QStringList l = byExe.value(c);
    if (l.isEmpty()) {
        l = byDir.value(QFileInfo(c).path());
    }
    return l.size() == 1 ? l.first() : QString();
}

/* what a process runs: for interpreters the script (python3 /path/seven-snip.py), else /proc/<pid>/exe */
inline QString processProgram(const GpuProcess &g)
{
    static const QRegularExpression interp(QStringLiteral("^(python[0-9.]*|perl|ruby|node|bash|sh|java|mono|wine64-preloader|wine-preloader)$"));
    if (!interp.match(QFileInfo(g.exe).fileName()).hasMatch()) {
        return g.exe;
    }
    QFile f(QStringLiteral("/proc/%1/cmdline").arg(g.pid));
    if (!f.open(QIODevice::ReadOnly)) {
        return g.exe;
    }
    const QList<QByteArray> argv = f.readAll().split('\0');
    for (int i = 1; i < argv.size(); ++i) {
        const QString a = QString::fromLocal8Bit(argv.at(i));
        if (!a.startsWith(QLatin1Char('-')) && !a.isEmpty()) {
            const QString full = QFileInfo(a).isAbsolute() ? a : QFileInfo(QFile::symLinkTarget(QStringLiteral("/proc/%1/cwd").arg(g.pid)) + QLatin1Char('/') + a).absoluteFilePath();
            return QFile::exists(full) ? full : g.exe;
        }
    }
    return g.exe;
}

/* parts of the desktop itself, never offered as programs */
inline bool pxShellProcess(const QString &exe)
{
    static const QStringList shell{QStringLiteral("Xorg"), QStringLiteral("Xwayland"), QStringLiteral("kwin_x11"), QStringLiteral("kwin_wayland"), QStringLiteral("plasmashell"),
                                   QStringLiteral("ksecretd"), QStringLiteral("kded6"), QStringLiteral("ksmserver"), QStringLiteral("krunner"), QStringLiteral("kaccess"),
                                   QStringLiteral("polkit-kde-authentication-agent-1"), QStringLiteral("uac-polkit-agent"), QStringLiteral("xdg-desktop-portal-kde"),
                                   QStringLiteral("org_kde_powerdevil"), QStringLiteral("seven-ccc"), QStringLiteral("kscreenlocker_greet"), QStringLiteral("baloorunner"),
                                   QStringLiteral("kwalletd6"), QStringLiteral("kwalletd5"), QStringLiteral("kactivitymanagerd"), QStringLiteral("xembedsniproxy"),
                                   QStringLiteral("gmenudbusmenuproxy"), QStringLiteral("kdeconnectd"), QStringLiteral("DiscoverNotifier"), QStringLiteral("drkonqi"),
                                   QStringLiteral("plasma-browser-integration-host"), QStringLiteral("spectacle"), QStringLiteral("kglobalacceld")};
    return shell.contains(QFileInfo(exe).fileName());
}

class PxAppsPage : public Page
{
public:
    PxAppsPage()
    {
        id = QStringLiteral("power.pxapps");
        title = QStringLiteral("Параметри програм перемиканої графіки");
        description = QStringLiteral("Використовуйте цю сторінку, щоб призначити програмам енергозберігальний або високопродуктивний графічний процесор залежно від глобальних параметрів. "
                                     "Високопродуктивний процесор краще підходить для програм, що інтенсивно використовують тривимірну графіку або відео.");
        build();
        section(QStringLiteral("Недавні програми"));
        auto *v1 = new QVBoxLayout;
        v1->addWidget(label(QStringLiteral("Програми, які зараз використовують графічний процесор. Щоб змінити графічний процесор, пов'язаний із програмою, натисніть кнопку поруч із нею.")));
        recent = makeTree();
        v1->addWidget(recent);
        box(v1, 10);
        section(QStringLiteral("Інші програми"));
        auto *v2 = new QVBoxLayout;
        v2->addWidget(label(QStringLiteral("Щоб налаштувати інші програми, спочатку виберіть їх на комп'ютері, а потім виберіть потрібний графічний процесор для кожної програми.")));
        other = makeTree();
        v2->addWidget(other);
        auto *h = new QHBoxLayout;
        h->addStretch(1);
        auto *browse = new QPushButton(QStringLiteral("Огляд..."));
        h->addWidget(browse);
        v2->addLayout(h);
        auto *note = label(QStringLiteral("Щоб вибрані параметри перемиканої графіки набули чинності, потрібно закрити всі тривимірні програми. "
                                          "Для програм Vulkan вибір діє, коли їх запущено з меню «Пуск»."));
        QPalette pal = note->palette();
        pal.setColor(QPalette::WindowText, QColor(0x55, 0x55, 0x55));
        note->setPalette(pal);
        v2->addSpacing(6);
        v2->addWidget(note);
        box(v2, 10);
        body->addStretch(1);
        QObject::connect(browse, &QPushButton::clicked, this, [this] { add(); });
    }
    void load() override
    {
        m_saved.clear();
        m_now.clear();
        for (const PxApp &a : pxAssigned()) {
            m_saved.insert(a.key, a.state);
        }
        m_now = m_saved;
        m_removed.clear();
        fill();
    }
    bool dirty() const override { return m_now != m_saved || !m_removed.isEmpty(); }
    bool hasDefaults() const override { return true; }
    void defaults() override
    {
        // "Відновити значення за замовчуванням": every program back to "not assigned"
        for (auto it = m_now.begin(); it != m_now.end(); ++it) {
            it.value().clear();
        }
        fill();
        notify();
    }
    bool apply() override
    {
        for (const QString &k : std::as_const(m_removed)) {
            removePx(k);
        }
        for (auto it = m_now.cbegin(); it != m_now.cend(); ++it) {
            setPxState(it.key(), it.value());
            if (m_seen.contains(it.key())) {
                setPxExe(it.key(), m_seen.value(it.key()));
            }
        }
        store().sync();
        const bool ok = applySwitchable();
        QProcess::startDetached(QStringLiteral("kbuildsycoca6"), {});
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("Не вдалося зберегти параметри перемиканої графіки (~/.drirc)."));
        }
        load();
        return ok;
    }

private:
    QTreeWidget *makeTree()
    {
        auto *t = new QTreeWidget;
        t->setHeaderLabels({QStringLiteral("Програма"), QStringLiteral("Графічний процесор")});
        t->setRootIsDecorated(false);
        t->setIconSize(QSize(24, 24));
        t->setMinimumHeight(150);
        t->header()->setStretchLastSection(false);
        t->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        t->header()->setSectionResizeMode(1, QHeaderView::Fixed);
        t->header()->resizeSection(1, 200);
        t->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(t, &QTreeWidget::customContextMenuRequested, this, [this, t](const QPoint &pos) {
            QTreeWidgetItem *it = t->itemAt(pos);
            if (!it) {
                return;
            }
            const QString key = it->data(0, Qt::UserRole).toString();
            QMenu m;
            auto *def = m.addAction(QStringLiteral("Відновити параметр за замовчуванням"));
            QAction *del = m_now.contains(key) ? m.addAction(QStringLiteral("Видалити зі списку")) : nullptr;
            QAction *a = m.exec(t->viewport()->mapToGlobal(pos));
            if (a == def) {
                if (m_now.contains(key)) {
                    m_now[key].clear();
                }
            } else if (a && a == del) {
                m_now.remove(key);
                m_removed.insert(key);
            } else {
                return;
            }
            fill();
            notify();
        });
        return t;
    }
    void addRow(QTreeWidget *t, const PxApp &a, const QString &state, const QString &extra = QString())
    {
        auto *it = new QTreeWidgetItem(t, {a.name});
        it->setData(0, Qt::UserRole, a.key);
        QIcon ic = QIcon::fromTheme(a.icon.isEmpty() ? QFileInfo(a.exec).fileName() : a.icon);
        if (ic.isNull()) {
            ic = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        }
        it->setIcon(0, ic);
        it->setToolTip(0, QStringLiteral("Шлях: %1%2").arg(a.exec.isEmpty() ? a.key : a.exec, extra));
        auto *c = new QComboBox;
        c->addItems(pxStateNames());
        c->setCurrentIndex(pxStateIndex(state));
        c->setToolTip(QStringList{QStringLiteral("Цю програму не пов'язано з певним процесором. Натисніть, щоб вибрати процесор."),
                                  QStringLiteral("Ця програма працює на високопродуктивному процесорі. Натисніть, щоб вибрати енергозберігальний процесор."),
                                  QStringLiteral("Ця програма працює на енергозберігальному процесорі. Натисніть, щоб вибрати високопродуктивний процесор.")}
                          .at(pxStateIndex(state)));
        const QString key = a.key;
        QObject::connect(c, &QComboBox::currentIndexChanged, this, [this, key](int i) {
            m_now[key] = pxStateKey(i);
            m_removed.remove(key);
            notify();
        });
        t->setItemWidget(it, 1, c);
    }
    void fill()
    {
        recent->clear();
        other->clear();
        const Switchable sw = switchable();
        QSet<QString> shown;
        for (const GpuProcess &g : gpuProcesses()) {
            const QString prog = processProgram(g);
            if (prog.isEmpty() || pxShellProcess(prog)) {
                continue;
            }
            const QString did = desktopIdForExe(prog);
            const QString key = did.isEmpty() ? prog : did;
            if (shown.contains(key)) {
                continue;
            }
            shown.insert(key);
            PxApp a = pxApp(key);
            if (key != prog) {
                m_seen.insert(key, prog);
            }
            if (a.exec.isEmpty()) {
                a.exec = prog;
            }
            const QString on = g.pci == sw.performance.pci ? QStringLiteral("високопродуктивному") : QStringLiteral("енергозберігальному");
            addRow(recent, a, m_now.value(key), QStringLiteral("\nЗараз працює на %1 графічному процесорі").arg(on));
        }
        for (auto it = m_now.cbegin(); it != m_now.cend(); ++it) {
            if (!shown.contains(it.key())) {
                addRow(other, pxApp(it.key()), it.value());
            }
        }
    }
    void add()
    {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Вибрати"), QStringLiteral("/usr/share/applications"),
                                                       QStringLiteral("Програми (*.desktop);;Усі файли (*)"));
        if (f.isEmpty()) {
            return;
        }
        QString key;
        if (f.endsWith(QLatin1String(".desktop"))) {
            key = QFileInfo(f).fileName();
            if (desktopPath(key).isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("Цей ярлик не встановлено в меню програм."));
                return;
            }
        } else if (QFileInfo(f).isExecutable()) {
            key = QFileInfo(f).canonicalFilePath();
        } else {
            QMessageBox::warning(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("%1 не є програмою. Перевірте шлях і повторіть спробу.").arg(f));
            return;
        }
        if (!m_now.contains(key)) {
            m_now.insert(key, QStringLiteral("perf"));
            m_removed.remove(key);
        }
        fill();
        notify();
    }
    QTreeWidget *recent, *other;
    QMap<QString, QString> m_saved, m_now;
    QHash<QString, QString> m_seen; // desktop id -> binary seen running
    QSet<QString> m_removed;
};

class PxGlobalPage : public Page
{
public:
    PxGlobalPage()
    {
        id = QStringLiteral("power.pxglobal");
        title = QStringLiteral("Глобальні параметри перемиканої графіки");
        description = QStringLiteral("Використовуйте цю сторінку, щоб змінити параметри перемиканих графічних адаптерів для всієї системи.");
        build();
        section(QStringLiteral("Глобальні параметри програм"));
        auto *v = new QVBoxLayout;
        auto *h = new QHBoxLayout;
        auto *cap = label(QStringLiteral("Глобальні параметри програм:"));
        cap->setWordWrap(false);
        h->addWidget(cap);
        mode = new QComboBox;
        mode->addItems({QStringLiteral("Оптимальне енергозбереження"), QStringLiteral("Оптимальна продуктивність"),
                        QStringLiteral("Примусово використовувати енергозберігальні графічні адаптери")});
        mode->setToolTip(QStringLiteral("Виберіть глобальний параметр програм."));
        h->addWidget(mode);
        h->addStretch(1);
        v->addLayout(h);
        explain = label(QString());
        v->addSpacing(6);
        v->addWidget(explain);
        box(v);
        section(QStringLiteral("Графічні процесори"));
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(30);
        g->setVerticalSpacing(6);
        const Switchable sw = switchable();
        auto name = [](const Gpu &x) {
            const QString n = x.name.section(QLatin1Char('['), 1).section(QLatin1Char(']'), 0, 0);
            return n.isEmpty() ? x.name : n;
        };
        auto *l1 = label(QStringLiteral("Енергозберігальний графічний процесор:"));
        auto *l2 = label(QStringLiteral("Високопродуктивний графічний процесор:"));
        l1->setWordWrap(false);
        l2->setWordWrap(false);
        g->addWidget(l1, 0, 0);
        g->addWidget(label(name(sw.powerSaving)), 0, 1);
        g->addWidget(l2, 1, 0);
        g->addWidget(label(name(sw.performance)), 1, 1);
        g->setColumnStretch(1, 1);
        box(g);
        body->addStretch(1);
        QObject::connect(mode, &QComboBox::currentIndexChanged, this, [this] {
            text();
            notify();
        });
    }
    void load() override
    {
        QSignalBlocker b(mode);
        m_m = pxGlobal();
        mode->setCurrentIndex(m_m);
        text();
    }
    bool dirty() const override { return mode->currentIndex() != m_m; }
    bool hasDefaults() const override { return true; }
    void defaults() override
    {
        mode->setCurrentIndex(0);
    }
    bool apply() override
    {
        store().setValue(QStringLiteral("SwitchableGlobal"), mode->currentIndex());
        store().sync();
        const bool ok = applySwitchable();
        QProcess::startDetached(QStringLiteral("kbuildsycoca6"), {});
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("Не вдалося зберегти параметри перемиканої графіки (~/.drirc)."));
        }
        load();
        return ok;
    }

private:
    void text()
    {
        static const QStringList t{
            QStringLiteral("Запускати програми на призначеному графічному процесорі. Непризначені програми запускаються на енергозберігальному графічному процесорі."),
            QStringLiteral("Запускати програми на призначеному графічному процесорі. Непризначені програми OpenGL запускаються на високопродуктивному графічному процесорі."),
            QStringLiteral("Запускати програми на енергозберігальному графічному процесорі незалежно від параметрів окремих програм.")};
        explain->setText(t.value(mode->currentIndex()));
    }
    QComboBox *mode;
    QLabel *explain;
    int m_m = 0;
};

class PxMonitorPage : public Page
{
public:
    PxMonitorPage()
    {
        id = QStringLiteral("power.pxmon");
        title = QStringLiteral("Монітор програм перемиканих графічних адаптерів");
        description = QStringLiteral("Стеження за тим, які програми працюють на високопродуктивному графічному процесорі. Програми, що виконуються деінде, у списку не показано. "
                                     "Після зупинки стеження список програм буде очищено.");
        build();
        auto *v = new QVBoxLayout;
        auto *h = new QHBoxLayout;
        toggle = new QPushButton(QStringLiteral("Запустити моніторинг"));
        h->addWidget(toggle);
        h->addStretch(1);
        v->addLayout(h);
        list = new QTreeWidget;
        list->setHeaderLabels({QStringLiteral("Програма"), QStringLiteral("Код процесу (PID)"), QStringLiteral("Шлях")});
        list->setRootIsDecorated(false);
        list->setIconSize(QSize(16, 16));
        list->setMinimumHeight(260);
        list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        v->addWidget(list);
        auto *note = label(QStringLiteral("Монітор показує, на якому графічному процесорі працює програма, але й сам споживає системні ресурси. Якщо моніторинг не потрібен, його можна зупинити."));
        v->addWidget(note);
        box(v, 10);
        body->addStretch(1);
        timer = new QTimer(this);
        QObject::connect(timer, &QTimer::timeout, this, [this] { refresh(); });
        QObject::connect(toggle, &QPushButton::clicked, this, [this] {
            if (timer->isActive()) {
                timer->stop();
                list->clear();
                toggle->setText(QStringLiteral("Запустити моніторинг"));
                toggle->setToolTip(QStringLiteral("Натисніть, щоб почати стежити за тим, який графічний процесор використовує та чи інша програма."));
            } else {
                refresh();
                timer->start(2000);
                toggle->setText(QStringLiteral("Зупинити моніторинг"));
                toggle->setToolTip(QStringLiteral("Натисніть, щоб зупинити стеження за програмами й очистити список."));
            }
        });
    }
    bool hasApply() const override { return false; }

private:
    void refresh()
    {
        const QString perf = switchable().performance.pci;
        list->clear();
        for (const GpuProcess &g : gpuProcesses()) {
            if (g.pci != perf) {
                continue;
            }
            const QString prog = processProgram(g);
            const QString did = desktopIdForExe(prog);
            const PxApp a = pxApp(did.isEmpty() ? prog : did);
            auto *it = new QTreeWidgetItem(list, {a.name.isEmpty() ? g.name : a.name, QString::number(g.pid), prog});
            QIcon ic = QIcon::fromTheme(a.icon.isEmpty() ? g.name : a.icon);
            it->setIcon(0, ic.isNull() ? QIcon::fromTheme(QStringLiteral("application-x-executable")) : ic);
        }
    }
    QPushButton *toggle;
    QTreeWidget *list;
    QTimer *timer;
};

/* -------- Продуктивність > AMD OverDrive (graphics cards) */

/* the row of OverDrive gauges: CCC 14.9 vector pictures (od/*.svg), needles and pie drawn here */
class OdGauges : public QWidget
{
public:
    OdInfo info;
    explicit OdGauges(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(150);
        setMinimumWidth(4 + 6 * 95);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        struct G {
            QString title, svg, value;
            int kind; // 0 picture, 1 needle, 2 pie
            double frac;
        };
        const OdInfo &o = info;
        auto pct = [](double a, double b) { return b > 0 ? qBound(0.0, a / b, 1.0) : 0.0; };
        const int sclkTop = o.sclkLevels.isEmpty() ? maxClock(o.dev + QStringLiteral("/pp_dpm_sclk")) : o.sclkMax;
        const int mclkTop = o.mclkLevels.isEmpty() ? maxClock(o.dev + QStringLiteral("/pp_dpm_mclk")) : o.mclkMax;
        QList<G> gs;
        gs << G{QStringLiteral("Активність"), QStringLiteral("GPUActivity_base"), o.activity >= 0 ? QStringLiteral("%1%").arg(o.activity) : QStringLiteral("—"), 2, o.activity / 100.0};
        gs << G{QStringLiteral("Частота GPU"), QStringLiteral("ActualGPUClock"), o.sclk ? QStringLiteral("%1 МГц").arg(o.sclk) : QStringLiteral("—"), 1, pct(o.sclk, sclkTop)};
        if (o.mclk) {
            gs << G{QStringLiteral("Частота пам'яті"), QStringLiteral("ActualMemoryClock"), QStringLiteral("%1 МГц").arg(o.mclk), 1, pct(o.mclk, mclkTop)};
        }
        if (o.powerW >= 0) {
            gs << G{QStringLiteral("Потужність"), QStringLiteral("RequestedPC"), QStringLiteral("%1 Вт").arg(qRound(o.powerW)), 0, 0};
        }
        if (o.temp > -1000) {
            gs << G{QStringLiteral("Температура"), QStringLiteral("GPUTemperature"), QStringLiteral("%1 °C").arg(o.temp), 0, 0};
        }
        if (o.fanPct >= 0 || o.fanRpm >= 0) {
            gs << G{QStringLiteral("Швидкість вентилятора"), QStringLiteral("ActualFanSpeed"),
                    o.fanPct >= 0 ? QStringLiteral("%1%").arg(o.fanPct) : QStringLiteral("%1 об/хв").arg(o.fanRpm), 0, 0};
        }
        const int colW = 95, x0 = 2;
        for (int i = 0; i < gs.size(); ++i) {
            const G &g = gs.at(i);
            const int cx = x0 + i * colW + colW / 2;
            p.setPen(QColor(0x1E, 0x1E, 0x1E));
            p.setFont(uiFont(12));
            p.drawText(QRect(cx - 46, 0, 92, 42), Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap, g.title);
            const QRectF pic(cx - 33.5, 46, 67, 67);
            QSvgRenderer r(assets() + QStringLiteral("od/") + g.svg + QStringLiteral(".svg"));
            QSizeF sz = r.viewBoxF().size().scaled(pic.size(), Qt::KeepAspectRatio);
            r.render(&p, QRectF(pic.center() - QPointF(sz.width() / 2, sz.height() / 2), sz));
            if (g.kind == 2) {
                // PieGraph: green radial wedge clockwise from 12 o'clock, 58/67 of the gauge, under the top ring
                const double rr = 29.0 * pic.width() / 67.0;
                QRadialGradient rg(pic.center(), rr);
                rg.setColorAt(0, QColor(0x00, 0xDC, 0x9C));
                rg.setColorAt(1, QColor(0x00, 0xA8, 0x6C));
                p.setPen(Qt::NoPen);
                p.setBrush(rg);
                const QRectF pr(pic.center() - QPointF(rr, rr), QSizeF(2 * rr, 2 * rr));
                if (g.frac >= 0.999) {
                    p.drawEllipse(pr);
                } else if (g.frac > 0) {
                    p.drawPie(pr, 90 * 16, -qRound(g.frac * 360 * 16));
                }
                QSvgRenderer top(assets() + QStringLiteral("od/GPUActivity_top.svg"));
                top.render(&p, pic);
            } else if (g.kind == 1) {
                // needle "M0,0 L-1.5,-1 L-3,-5 L-1,-20 L0,-22 L1,-20 L3,-5 L1.5,-1", pivot at the centre, -135°..135°
                p.save();
                p.translate(pic.center());
                const double k = pic.width() / 67.0;
                p.scale(k, k);
                p.rotate(-135 + 270 * g.frac);
                QPainterPath n;
                n.moveTo(0, 0);
                for (const QPointF &pt : {QPointF(-1.5, -1), QPointF(-3, -5), QPointF(-1, -20), QPointF(0, -22), QPointF(1, -20), QPointF(3, -5), QPointF(1.5, -1)}) {
                    n.lineTo(pt);
                }
                n.closeSubpath();
                QLinearGradient lg(QPointF(-3, 0), QPointF(3, -22));
                lg.setColorAt(0, QColor(0x00, 0xA8, 0x6C));
                lg.setColorAt(1, QColor(0x00, 0xCA, 0xB5));
                p.setPen(Qt::NoPen);
                p.setBrush(lg);
                p.drawPath(n);
                p.restore();
            }
            // value box: black 1px border, white, 0.85 opacity
            const QRect vb(cx - 42, 118, 84, 21);
            p.setOpacity(0.85);
            p.setPen(Qt::black);
            p.setBrush(Qt::white);
            p.drawRect(vb.adjusted(0, 0, -1, -1));
            p.setOpacity(1);
            p.setPen(QColor(0x1E, 0x1E, 0x1E));
            p.drawText(vb, Qt::AlignCenter, g.value);
        }
    }
};

/* "min [——slider——] max [ value unit ]" of the requested settings */
class OdRow : public QWidget
{
public:
    QLabel *title, *lo, *hi;
    QSlider *slider;
    QSpinBox *spin;
    std::function<void()> changed;
    OdRow(const QString &t, const QString &unit, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 6, 0, 4);
        title = label(t);
        v->addWidget(title);
        auto *h = new QHBoxLayout;
        lo = label(QString());
        hi = label(QString());
        lo->setWordWrap(false);
        hi->setWordWrap(false);
        lo->setMinimumWidth(40);
        hi->setMinimumWidth(40);
        slider = new QSlider(Qt::Horizontal);
        spin = new QSpinBox;
        spin->setSuffix(QLatin1Char(' ') + unit);
        spin->setMinimumWidth(100);
        h->addWidget(lo);
        h->addWidget(slider, 1);
        h->addWidget(hi);
        h->addSpacing(10);
        h->addWidget(spin);
        v->addLayout(h);
        QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        QObject::connect(spin, &QSpinBox::valueChanged, this, [this](int x) {
            QSignalBlocker b(slider);
            slider->setValue(x);
            if (changed) {
                changed();
            }
        });
    }
    void setRange(int a, int b)
    {
        QSignalBlocker b1(slider), b2(spin);
        slider->setRange(a, b);
        spin->setRange(a, b);
        lo->setText(QString::number(a));
        hi->setText(QString::number(b));
    }
    void setValue(int x)
    {
        QSignalBlocker b1(slider), b2(spin);
        slider->setValue(x);
        spin->setValue(x);
    }
    int value() const { return spin->value(); }
};

inline bool odWarningAccepted(QWidget *parent)
{
    if (store().value(QStringLiteral("OverDriveAccepted"), false).toBool()) {
        return true;
    }
    QDialog d(parent);
    d.setWindowTitle(QStringLiteral("Попередження AMD OverDrive"));
    d.resize(560, 460);
    auto *v = new QVBoxLayout(&d);
    auto *head = label(QStringLiteral("Прочитайте ліцензійну угоду й натисніть «Прийняти»."));
    head->setFont(uiFont(16));
    v->addWidget(head);
    auto *t = new QTextBrowser(&d);
    t->setHtml(QStringLiteral(
        "<p><b><u>ПОПЕРЕДЖЕННЯ.</u></b> Процесори AMD призначено для роботи лише в середовищах із відповідними технічними характеристиками та "
        "заводськими параметрами. Робота процесорів AMD з обладнанням, технічні характеристики якого не відповідають офіційно затвердженим "
        "компанією AMD, або з параметрами, що відрізняються від установлених виробником, зокрема, але не виключно, розгін процесора (у тому числі "
        "за допомогою цієї програми для розгону, навіть якщо її прямо чи опосередковано надала компанія AMD або її якось пов'язано з AMD), може "
        "пошкодити процесор і/або спричинити інші проблеми. До них, серед іншого, належать: пошкодження компонентів системи (зокрема материнської "
        "плати та встановлених на ній компонентів, наприклад модулів пам'яті), нестабільна робота системи (наприклад, утрата даних і спотворене "
        "зображення), зниження продуктивності системи, скорочення строку служби процесора, компонентів системи та/або самої системи, а в крайніх "
        "випадках — повна відмова системи.</p>"
        "<p>Компанія AMD не надає технічної підтримки чи обслуговування у зв'язку зі збоями або пошкодженнями, спричиненими роботою процесорів AMD "
        "з обладнанням, технічні характеристики якого не відповідають офіційно затвердженим компанією AMD, або з параметрами, що відрізняються від "
        "установлених виробником. Від технічної підтримки чи обслуговування можуть також відмовитися виробники материнської плати або системи.</p>"
        "<p>Перш ніж використовувати цю програму для розгону, обов'язково створіть резервну копію важливих даних.</p>"
        "<p>НА ПОШКОДЖЕННЯ, СПРИЧИНЕНІ РОБОТОЮ ПРОЦЕСОРІВ AMD АБО ІНШИХ КОМПОНЕНТІВ (НАПРИКЛАД, ПАМ'ЯТІ) З ОБЛАДНАННЯМ, ТЕХНІЧНІ ХАРАКТЕРИСТИКИ ЯКОГО "
        "НЕ ВІДПОВІДАЮТЬ ОФІЦІЙНО ЗАТВЕРДЖЕНИМ КОМПАНІЄЮ AMD, АБО З ПАРАМЕТРАМИ, ЩО ВІДРІЗНЯЮТЬСЯ ВІД УСТАНОВЛЕНИХ ВИРОБНИКОМ, НЕ ПОШИРЮЄТЬСЯ ЖОДНА "
        "З ГАРАНТІЙ НА ПРОДУКЦІЮ AMD, А ТАКОЖ МОЖУТЬ НЕ ПОШИРЮВАТИСЯ ГАРАНТІЇ ВИРОБНИКІВ МАТЕРИНСЬКОЇ ПЛАТИ АБО СИСТЕМИ.</p>"));
    v->addWidget(t, 1);
    auto *bb = new QDialogButtonBox(&d);
    auto *acc = bb->addButton(QStringLiteral("Прийняти"), QDialogButtonBox::AcceptRole);
    bb->addButton(QStringLiteral("Скасувати"), QDialogButtonBox::RejectRole);
    acc->setDefault(true);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(bb);
    if (d.exec() != QDialog::Accepted) {
        return false;
    }
    store().setValue(QStringLiteral("OverDriveAccepted"), true);
    return true;
}

class OverDrivePage : public Page
{
public:
    OverDrivePage()
    {
        id = QStringLiteral("perf.od");
        title = QStringLiteral("AMD OverDrive");
        description = QStringLiteral("Налаштуйте швидкість вентилятора, тактову частоту, енергоспоживання й цільову робочу температуру відеокарти.");
        build();
        m_gpus = overdriveGpus();
        if (m_gpus.size() > 1) {
            auto *h = new QHBoxLayout;
            auto *l = label(QStringLiteral("Виберіть GPU для налаштування:"));
            l->setWordWrap(false);
            h->addWidget(l);
            pick = new QComboBox;
            for (const Gpu &g : std::as_const(m_gpus)) {
                pick->addItem(cardName(g));
            }
            h->addWidget(pick);
            h->addStretch(1);
            box(h, 10);
            QObject::connect(pick, &QComboBox::currentIndexChanged, this, [this] {
                load();
                notify();
            });
        }
        section(QStringLiteral("Поточні значення"));
        gauges = new OdGauges;
        auto *gw = new QHBoxLayout;
        gw->addWidget(gauges);
        gw->addStretch(1);
        box(gw, 10);
        enable = new QCheckBox(QStringLiteral("Увімкнути Graphics OverDrive"));
        enable->setToolTip(QStringLiteral("Установіть цей прапорець, щоб використовувати запитані параметри OverDrive, або зніміть його, щоб використовувати параметри драйвера за замовчуванням."));
        enable->setFont(uiFont(12));
        auto *eh = new QHBoxLayout;
        eh->addWidget(enable);
        eh->addStretch(1);
        box(eh, 10);
        frame = new QFrame;
        frame->setFrameShape(QFrame::Box);
        frame->setStyleSheet(QStringLiteral("QFrame#odframe{border:1px solid #c8c8c8;}"));
        frame->setObjectName(QStringLiteral("odframe"));
        auto *fv = new QVBoxLayout(frame);
        fv->setContentsMargins(12, 8, 12, 10);
        auto *ih = new QHBoxLayout;
        auto *ii = new QLabel;
        ii->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-information")).pixmap(16, 16));
        ih->addWidget(ii, 0, Qt::AlignTop);
        info = label(QString());
        ih->addWidget(info, 1);
        fv->addLayout(ih);
        sclk = new OdRow(QStringLiteral("Параметри частоти високопродуктивного графічного процесора:"), QStringLiteral("МГц"));
        sclk->spin->setToolTip(QStringLiteral("Налаштуйте тактову частоту GPU для вибраного графічного процесора."));
        mclk = new OdRow(QStringLiteral("Параметри частоти високопродуктивної пам'яті:"), QStringLiteral("МГц"));
        mclk->spin->setToolTip(QStringLiteral("Налаштуйте тактову частоту пам'яті для вибраного графічного процесора."));
        fanOn = new QCheckBox(QStringLiteral("Увімкнути ручне керування вентилятором"));
        fanOn->setFont(uiFont(12));
        fanOn->setToolTip(QStringLiteral("Установіть прапорець для ручного керування швидкістю вентилятора або зніміть його для автоматичного керування."));
        fan = new OdRow(QString(), QStringLiteral("%"));
        fan->title->hide();
        temp = new OdRow(QStringLiteral("Цільова температура GPU:"), QStringLiteral("°C"));
        temp->spin->setToolTip(QStringLiteral("Налаштуйте цільову температуру для вибраного графічного процесора."));
        power = new OdRow(QStringLiteral("Обмеження енергоспоживання (%):"), QStringLiteral("%"));
        power->spin->setToolTip(QStringLiteral("Налаштуйте максимальні межі живлення для вибраного графічного процесора."));
        fv->addWidget(sclk);
        fv->addWidget(mclk);
        fv->addSpacing(4);
        fv->addWidget(fanOn);
        fv->addWidget(fan);
        fv->addWidget(temp);
        fv->addWidget(power);
        auto *fw = new QHBoxLayout;
        fw->addWidget(frame);
        box(fw, 10);
        body->addStretch(1);
        for (OdRow *r : {sclk, mclk, fan, temp, power}) {
            r->changed = [this] { notify(); };
        }
        QObject::connect(fanOn, &QCheckBox::toggled, this, [this](bool on) {
            fan->setEnabled(on && enable->isChecked());
            notify();
        });
        QObject::connect(enable, &QCheckBox::clicked, this, [this](bool on) {
            if (on && !odWarningAccepted(this)) {
                enable->setChecked(false);
                return;
            }
            controls();
            notify();
        });
        auto *t = new QTimer(this);
        QObject::connect(t, &QTimer::timeout, this, [this] {
            if (isVisible()) {
                gauges->info = odInfo(gpu());
                gauges->update();
            }
        });
        t->start(1000);
    }
    void load() override
    {
        const OdInfo o = odInfo(gpu());
        m_o = o;
        gauges->info = o;
        gauges->update();
        const QString k = key();
        m_saved = {store().value(k + QStringLiteral("enabled"), false).toBool(), o.sclkLevels.isEmpty() ? 0 : o.sclkLevels.last().mhz,
                   o.mclkLevels.isEmpty() ? 0 : o.mclkLevels.last().mhz, o.fanManual, o.fanPwmPct, o.tempTarget,
                   o.capDefUw ? qRound(o.capUw * 100.0 / o.capDefUw) - 100 : 0};
        QSignalBlocker b(enable), b2(fanOn);
        enable->setChecked(m_saved.enabled);
        if (o.odClocks) {
            sclk->setRange(o.sclkMin, o.sclkMax);
            sclk->setValue(m_saved.sclk);
            if (!o.mclkLevels.isEmpty() && o.mclkMax > 0) {
                mclk->setRange(o.mclkMin, o.mclkMax);
                mclk->setValue(m_saved.mclk);
            }
            info->setText(QStringLiteral("Параметри тактової частоти застосовуються лише тоді, коли GPU працює в режимі високої продуктивності."));
        } else {
            info->setText(QStringLiteral("Драйвер не дозволяє змінювати частоти цієї відеокарти. Щоб їх змінювати, додайте до параметрів ядра "
                                         "amdgpu.ppfeaturemask=0xffffffff і перезавантажте комп'ютер. Вентилятор і обмеження енергоспоживання можна налаштувати й так."));
        }
        sclk->setVisible(o.odClocks);
        mclk->setVisible(o.odClocks && !o.mclkLevels.isEmpty() && o.mclkMax > 0);
        fanOn->setVisible(o.fanControl);
        fan->setVisible(o.fanControl);
        fan->setRange(0, 100);
        fanOn->setChecked(m_saved.fanManual);
        fan->setValue(qMax(0, m_saved.fanPct));
        temp->setVisible(o.targetTemp);
        temp->setRange(o.tempMin, o.tempMax);
        temp->setValue(m_saved.temp);
        const bool cap = o.capDefUw > 0 && o.capMaxUw > 0;
        power->setVisible(cap);
        if (cap) {
            power->setRange(qRound(o.capMinUw * 100.0 / o.capDefUw) - 100, qRound(o.capMaxUw * 100.0 / o.capDefUw) - 100);
            power->setValue(m_saved.power);
        }
        controls();
    }
    bool dirty() const override { return now() != m_saved; }
    bool hasDefaults() const override { return true; }
    void defaults() override
    {
        enable->setChecked(false);
        controls();
        notify();
    }
    bool apply() override
    {
        const State n = now();
        const OdInfo &o = m_o;
        QStringList w;
        const QString od = o.dev + QStringLiteral("/pp_od_clk_voltage");
        if (n.enabled) {
            if (o.odClocks) {
                const OdLevel s = o.sclkLevels.last();
                w << od << (s.mv ? QStringLiteral("s %1 %2 %3").arg(s.index).arg(n.sclk).arg(s.mv) : QStringLiteral("s %1 %2").arg(s.index).arg(n.sclk));
                if (mclk->isVisible()) {
                    const OdLevel m = o.mclkLevels.last();
                    w << od << (m.mv ? QStringLiteral("m %1 %2 %3").arg(m.index).arg(n.mclk).arg(m.mv) : QStringLiteral("m %1 %2").arg(m.index).arg(n.mclk));
                }
                w << od << QStringLiteral("c");
            }
            if (o.fanControl) {
                w << o.hwmon + QStringLiteral("/pwm1_enable") << (n.fanManual ? QStringLiteral("1") : QStringLiteral("2"));
                if (n.fanManual) {
                    w << o.hwmon + QStringLiteral("/pwm1") << QString::number(qRound(n.fanPct * 255 / 100.0));
                }
            }
            if (o.targetTemp) {
                const QString f = o.dev + QStringLiteral("/gpu_od/fan_ctrl/fan_target_temperature");
                w << f << QString::number(n.temp) << f << QStringLiteral("c");
            }
            if (power->isVisible()) {
                w << o.hwmon + QStringLiteral("/power1_cap") << QString::number(qint64(o.capDefUw * (100 + n.power) / 100.0));
            }
        } else {
            // the driver's own settings
            if (o.odClocks) {
                w << od << QStringLiteral("r") << od << QStringLiteral("c");
            }
            if (o.fanControl) {
                w << o.hwmon + QStringLiteral("/pwm1_enable") << QStringLiteral("2");
            }
            if (power->isVisible()) {
                w << o.hwmon + QStringLiteral("/power1_cap") << QString::number(o.capDefUw);
            }
        }
        const bool ok = w.isEmpty() || odWrite(w, gpu().pci, n.enabled);
        if (ok) {
            store().setValue(key() + QStringLiteral("enabled"), n.enabled);
        } else {
            QMessageBox::warning(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("Не вдалося застосувати параметри AMD OverDrive."));
        }
        load();
        return ok;
    }

private:
    struct State {
        bool enabled = false;
        int sclk = 0, mclk = 0;
        bool fanManual = false;
        int fanPct = 0, temp = 0, power = 0;
        bool operator==(const State &) const = default;
    };
    State now() const
    {
        return {enable->isChecked(), sclk->value(), mclk->value(), fanOn->isChecked(), fan->value(), temp->value(), power->value()};
    }
    Gpu gpu() const { return m_gpus.value(pick ? pick->currentIndex() : 0); }
    QString key() const { return QStringLiteral("OverDrive/%1/").arg(QString(gpu().pci).replace(QLatin1Char(':'), QLatin1Char('_'))); }
    void controls()
    {
        const bool on = enable->isChecked();
        for (QWidget *w : {static_cast<QWidget *>(sclk), static_cast<QWidget *>(mclk), static_cast<QWidget *>(fanOn), static_cast<QWidget *>(temp), static_cast<QWidget *>(power)}) {
            w->setEnabled(on);
        }
        fan->setEnabled(on && fanOn->isChecked());
    }
    QList<Gpu> m_gpus;
    QComboBox *pick = nullptr;
    OdGauges *gauges;
    QCheckBox *enable, *fanOn;
    QFrame *frame;
    QLabel *info;
    OdRow *sclk, *mclk, *fan, *temp, *power;
    OdInfo m_o;
    State m_saved;
};

/* -------- Інформація */

class InfoTablePage : public Page
{
public:
    std::function<QList<Row>()> rows;
    QString caption;
    bool systemButton = false;
    InfoTablePage(const QString &pid, const QString &t, const QString &desc, const QString &cap, std::function<QList<Row>()> r, bool sysBtn)
    {
        id = pid;
        title = t;
        description = desc;
        rows = std::move(r);
        caption = cap;
        build();
        auto *v = new QVBoxLayout;
        v->addWidget(label(cap));
        table = new QTreeWidget;
        table->setHeaderLabels({QStringLiteral("Елемент"), QStringLiteral("Значення")});
        table->setRootIsDecorated(false);
        table->setMinimumHeight(300);
        table->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        v->addWidget(table);
        if (sysBtn) {
            auto *h = new QHBoxLayout;
            h->addStretch(1);
            auto *b = new QPushButton(QStringLiteral("Відомості про систему..."));
            h->addWidget(b);
            v->addLayout(h);
            QObject::connect(b, &QPushButton::clicked, this, [this] { systemInfo(); });
        }
        box(v, 10);
        body->addStretch(1);
    }
    bool hasApply() const override { return false; }
    void load() override
    {
        table->clear();
        for (const Row &r : rows()) {
            if (!r.value.isEmpty()) {
                new QTreeWidgetItem(table, {r.item, r.value});
            }
        }
        if (table->topLevelItemCount()) {
            table->setCurrentItem(table->topLevelItem(0));
        }
    }
    void systemInfo();

private:
    QTreeWidget *table;
};

inline QString adapterTitle(int i)
{
    return i == 0 ? QStringLiteral("Основний адаптер") : QStringLiteral("Адаптер %1 увімкнено").arg(i);
}

inline void systemInfoDialog(QWidget *parent);

inline void InfoTablePage::systemInfo()
{
    systemInfoDialog(this);
}

inline void systemInfoDialog(QWidget *parent)
{
    QDialog d(parent);
    d.setWindowTitle(QStringLiteral("Відомості про систему"));
    d.resize(560, 520);
    auto *v = new QVBoxLayout(&d);
    auto *t = new QTreeWidget(&d);
    t->setHeaderLabels({QStringLiteral("Елемент"), QStringLiteral("Значення")});
    t->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    auto add = [&](const QString &group, const QList<Row> &rows) {
        auto *g = new QTreeWidgetItem(t, {group});
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        for (const Row &r : rows) {
            if (!r.value.isEmpty()) {
                new QTreeWidgetItem(g, {r.item, r.value});
            }
        }
        g->setExpanded(true);
    };
    QString cpu;
    for (const QString &l : readFile(QStringLiteral("/proc/cpuinfo")).split(QLatin1Char('\n'))) {
        if (l.startsWith(QLatin1String("model name"))) {
            cpu = l.section(QLatin1Char(':'), 1).trimmed();
            break;
        }
    }
    const auto mem = QRegularExpression(QStringLiteral("MemTotal:\\s+(\\d+)")).match(readFile(QStringLiteral("/proc/meminfo")));
    add(QStringLiteral("Комп'ютер"), {{QStringLiteral("Операційна система"), QSysInfo::prettyProductName()},
                                      {QStringLiteral("Ядро"), QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion()},
                                      {QStringLiteral("Процесор"), cpu},
                                      {QStringLiteral("Системна пам'ять"), mem.hasMatch() ? QStringLiteral("%1 МБ").arg(mem.captured(1).toLongLong() / 1024) : QString()},
                                      {QStringLiteral("Мова системи"), QLocale().nativeLanguageName()}});
    add(QStringLiteral("Програмне забезпечення"), softwareRows());
    const QList<Gpu> adapters = amdGpus();
    for (int i = 0; i < adapters.size(); ++i) {
        add(adapters.size() > 1 ? QStringLiteral("Апаратне забезпечення — %1").arg(adapterTitle(i)) : QStringLiteral("Апаратне забезпечення"), hardwareRows(adapters.at(i)));
    }
    v->addWidget(t);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, &d);
    bb->button(QDialogButtonBox::Close)->setText(QStringLiteral("Закрити"));
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(bb);
    d.exec();
}

/* -------- Інформація > Апаратне забезпечення: one expander per AMD adapter (Catalyst AdapterInfoCollection) */

class AdapterToggle : public QAbstractButton
{
public:
    explicit AdapterToggle(const QString &t, QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setText(t);
        setCheckable(true);
        setFixedHeight(30);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (isEnabled()) {
            // the 4x7 chevron of ExpanderToggleButtonTemplate, turned 90° when open
            p.save();
            p.translate(12, height() / 2.0);
            if (isChecked()) {
                p.rotate(90);
            }
            p.setPen(QPen(QColor(0x33, 0x33, 0x33), 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(QPolygonF({QPointF(-2, -3.5), QPointF(2, 0), QPointF(-2, 3.5)}));
            p.restore();
        }
        p.setPen(QColor(0x1E, 0x1E, 0x1E));
        p.setFont(uiFont(12, QFont::DemiBold));
        p.drawText(rect().adjusted(22, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, text());
    }
};

class HardwarePage : public Page
{
public:
    HardwarePage()
    {
        id = QStringLiteral("info.hw");
        title = QStringLiteral("Апаратне забезпечення");
        build();
        auto *top = new QHBoxLayout;
        auto *d = label(QStringLiteral("Перегляд властивостей графічного обладнання."));
        top->addWidget(d, 1);
        auto *b = new QPushButton(QStringLiteral("Відомості про систему..."));
        top->addWidget(b);
        box(top, 10);
        QObject::connect(b, &QPushButton::clicked, this, [this] { systemInfoDialog(this); });
        const QList<Gpu> adapters = amdGpus();
        for (int i = 0; i < adapters.size(); ++i) {
            auto *t = new AdapterToggle(adapterTitle(i));
            auto *table = new QTreeWidget;
            table->setHeaderLabels({QStringLiteral("Елемент"), QStringLiteral("Значення")});
            table->setRootIsDecorated(false);
            table->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            auto *wrap = new QWidget;
            auto *wl = new QVBoxLayout(wrap);
            wl->setContentsMargins(40, 0, 40, 15);
            wl->addWidget(table);
            body->addWidget(t);
            body->addWidget(wrap);
            // a single adapter: always open, no chevron (DisableToggleButtonConverter)
            t->setEnabled(adapters.size() > 1);
            t->setChecked(i == 0 || adapters.size() == 1);
            wrap->setVisible(t->isChecked());
            QObject::connect(t, &QAbstractButton::toggled, wrap, &QWidget::setVisible);
            m_tables << qMakePair(adapters.at(i), table);
        }
        body->addStretch(1);
    }
    bool hasApply() const override { return false; }
    void load() override
    {
        for (const auto &x : std::as_const(m_tables)) {
            QTreeWidget *table = x.second;
            table->clear();
            for (const Row &r : hardwareRows(x.first)) {
                if (!r.value.isEmpty()) {
                    new QTreeWidgetItem(table, {r.item, r.value});
                }
            }
            // as tall as its rows, the page scrolls instead
            int h = table->header()->sizeHint().height() + 2 * table->frameWidth();
            for (int k = 0; k < table->topLevelItemCount(); ++k) {
                h += table->sizeHintForRow(k);
            }
            table->setFixedHeight(h + 2);
        }
    }

private:
    QList<QPair<Gpu, QTreeWidget *>> m_tables;
};

class LinksPage : public Page
{
public:
    LinksPage(const QString &pid, const QString &t, const QString &desc, const QList<QPair<QString, std::function<void()>>> &links)
    {
        id = pid;
        title = t;
        description = desc;
        build();
        auto *v = new QVBoxLayout;
        for (const auto &l : links) {
            auto *b = new QPushButton(l.first);
            b->setFlat(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(QStringLiteral("QPushButton{color:#990000;text-align:left;border:none;padding:2px}QPushButton:hover{text-decoration:underline}"));
            b->setFont(uiFont(12));
            QObject::connect(b, &QPushButton::clicked, this, l.second);
            v->addWidget(b);
        }
        box(v, 10);
        result = label(QString());
        if (pid == QLatin1String("info.home")) {
            // in place of the web content of AMD Home: the Radeon badge with this system's GPU and driver
            auto *card = new QWidget;
            auto *cv = new QVBoxLayout(card);
            cv->setAlignment(Qt::AlignCenter);
            auto *logo = new QLabel;
            logo->setPixmap(QPixmap(assets() + QStringLiteral("amd_radeon.png")).scaled(280, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            logo->setAlignment(Qt::AlignCenter);
            logo->setStyleSheet(QStringLiteral("background:#080808;padding:18px 40px;border-radius:6px"));
            cv->addWidget(logo, 0, Qt::AlignHCenter);
            const QString gpu = glInfo().value(0).section(QStringLiteral(" ("), 0, 0);
            auto *name = label(gpu.isEmpty() ? QStringLiteral("AMD Radeon Graphics") : gpu, true);
            name->setAlignment(Qt::AlignCenter);
            name->setFont(uiFont(18, QFont::DemiBold));
            cv->addSpacing(10);
            cv->addWidget(name);
            auto *drv = label(QStringLiteral("Драйвер: Mesa %1 (amdgpu)").arg(mesaVersion()));
            drv->setAlignment(Qt::AlignCenter);
            cv->addWidget(drv);
            body->addSpacing(20);
            body->addWidget(card);
        }
        result->setContentsMargins(10, 0, 10, 0);
        body->addWidget(result);
        body->addStretch(1);
    }
    bool hasApply() const override { return false; }
    QLabel *result;
};

/* -------- Установки */

struct Preset {
    QString name;
    QVariantMap values;
};

inline QList<Preset> presets()
{
    QList<Preset> out;
    QSettings &s = store();
    const int n = s.beginReadArray(QStringLiteral("Presets"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        out << Preset{s.value(QStringLiteral("name")).toString(), s.value(QStringLiteral("values")).toMap()};
    }
    s.endArray();
    return out;
}

inline void savePresets(const QList<Preset> &l)
{
    QSettings &s = store();
    s.beginWriteArray(QStringLiteral("Presets"), l.size());
    for (int i = 0; i < l.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("name"), l.at(i).name);
        s.setValue(QStringLiteral("values"), l.at(i).values);
    }
    s.endArray();
    s.sync();
}

/* snapshot of what a preset can hold */
inline QVariantMap snapshot(bool display, bool color, bool power, bool d3)
{
    QVariantMap v;
    for (const Output &o : outputs()) {
        if (!o.enabled) {
            continue;
        }
        if (display) {
            v.insert(QStringLiteral("mode/") + o.name, o.currentMode);
            v.insert(QStringLiteral("rotation/") + o.name, o.rotation);
        }
        if (color) {
            const Color c = loadColor(o.name);
            QVariantList l;
            for (int i = 0; i < 3; ++i) {
                l << c.gamma[i] << c.brightness[i] << c.contrast[i];
            }
            v.insert(QStringLiteral("color/") + o.name, l);
        }
    }
    if (power) {
        v.insert(QStringLiteral("power"), powerProfile());
    }
    if (d3) {
        v.insert(QStringLiteral("vsync"), vblankMode());
    }
    return v;
}

inline void applyPreset(const Preset &p)
{
    QStringList args;
    for (auto it = p.values.begin(); it != p.values.end(); ++it) {
        const QString k = it.key();
        if (k.startsWith(QLatin1String("mode/"))) {
            args << QStringLiteral("output.%1.mode.%2").arg(k.mid(5), it.value().toString());
        } else if (k.startsWith(QLatin1String("rotation/"))) {
            args << QStringLiteral("output.%1.rotation.%2").arg(k.mid(9), rotationWord(it.value().toInt()));
        } else if (k.startsWith(QLatin1String("color/"))) {
            const QVariantList l = it.value().toList();
            Color c;
            for (int i = 0; i < 3 && l.size() >= 9; ++i) {
                c.gamma[i] = l.at(i * 3).toDouble();
                c.brightness[i] = l.at(i * 3 + 1).toInt();
                c.contrast[i] = l.at(i * 3 + 2).toInt();
            }
            saveColor(k.mid(6), c);
            applyColor(k.mid(6), c);
            setLoginApply(true);
        } else if (k == QLatin1String("power")) {
            setPowerProfile(it.value().toString());
        } else if (k == QLatin1String("vsync")) {
            setVblankMode(it.value().toInt());
        }
    }
    if (!args.isEmpty()) {
        kscreen(args);
    }
}

inline QString presetContents(const Preset &p)
{
    QStringList parts;
    bool d = false, c = false;
    for (const QString &k : p.values.keys()) {
        d |= k.startsWith(QLatin1String("mode/"));
        c |= k.startsWith(QLatin1String("color/"));
    }
    if (d) {
        parts << QStringLiteral("Параметри дисплея");
    }
    if (c) {
        parts << QStringLiteral("Колір");
    }
    if (p.values.contains(QStringLiteral("power"))) {
        parts << QStringLiteral("PowerPlay™");
    }
    if (p.values.contains(QStringLiteral("vsync"))) {
        parts << QStringLiteral("3D");
    }
    return parts.join(QStringLiteral(", "));
}

class AddPresetPage : public Page
{
public:
    std::function<void()> presetsChanged;
    AddPresetPage()
    {
        id = QStringLiteral("presets.add");
        title = QStringLiteral("Додати установку");
        description = QStringLiteral("Установка зберігає поточні параметри, щоб пізніше застосувати їх одним клацанням у розділі «Установки». "
                                     "Введіть назву та виберіть, які параметри зберегти.");
        build();
        section(QStringLiteral("Нова установка"));
        auto *v = new QVBoxLayout;
        v->addWidget(label(QStringLiteral("Назва установки:")));
        name = new QLineEdit;
        name->setMaximumWidth(300);
        v->addWidget(name);
        v->addSpacing(10);
        v->addWidget(label(QStringLiteral("Зберегти в установці:")));
        display = new QCheckBox(QStringLiteral("Параметри дисплея (роздільна здатність, частота, поворот)"));
        color = new QCheckBox(QStringLiteral("Колір робочого стола"));
        power = new QCheckBox(QStringLiteral("Режим PowerPlay™"));
        d3 = new QCheckBox(QStringLiteral("Налаштування 3D-програм"));
        for (QCheckBox *c : {display, color, power, d3}) {
            c->setChecked(c != d3);
            v->addWidget(c);
        }
        box(v);
        body->addStretch(1);
        QObject::connect(name, &QLineEdit::textChanged, this, [this] { notify(); });
    }
    void load() override { name->clear(); }
    bool dirty() const override { return !name->text().trimmed().isEmpty(); }
    bool apply() override
    {
        QList<Preset> l = presets();
        const QString n = name->text().trimmed();
        for (const Preset &p : l) {
            if (p.name == n) {
                QMessageBox::warning(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("Установка з такою назвою вже існує."));
                return false;
            }
        }
        l << Preset{n, snapshot(display->isChecked(), color->isChecked(), power->isChecked(), d3->isChecked())};
        savePresets(l);
        name->clear();
        if (presetsChanged) {
            presetsChanged();
        }
        return true;
    }

private:
    QLineEdit *name;
    QCheckBox *display, *color, *power, *d3;
};

class ManagePresetsPage : public Page
{
public:
    std::function<void()> presetsChanged;
    ManagePresetsPage()
    {
        id = QStringLiteral("presets.manage");
        title = QStringLiteral("Керування установками");
        description = QStringLiteral("Перегляд, застосування, перейменування та видалення установок.");
        build();
        auto *v = new QVBoxLayout;
        list = new QTreeWidget;
        list->setHeaderLabels({QStringLiteral("Назва"), QStringLiteral("Вміст")});
        list->setRootIsDecorated(false);
        list->setMinimumHeight(220);
        list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        v->addWidget(list);
        auto *h = new QHBoxLayout;
        auto *apply = new QPushButton(QStringLiteral("Застосувати"));
        auto *ren = new QPushButton(QStringLiteral("Перейменувати"));
        auto *del = new QPushButton(QStringLiteral("Видалити"));
        h->addWidget(apply);
        h->addWidget(ren);
        h->addWidget(del);
        h->addStretch(1);
        v->addLayout(h);
        box(v, 10);
        body->addStretch(1);
        QObject::connect(apply, &QPushButton::clicked, this, [this] {
            const int i = list->indexOfTopLevelItem(list->currentItem());
            if (i >= 0) {
                applyPreset(presets().at(i));
            }
        });
        QObject::connect(list, &QTreeWidget::itemDoubleClicked, apply, &QPushButton::click);
        QObject::connect(ren, &QPushButton::clicked, this, [this] {
            const int i = list->indexOfTopLevelItem(list->currentItem());
            if (i < 0) {
                return;
            }
            QList<Preset> l = presets();
            bool ok = false;
            const QString n = QInputDialog::getText(this, QStringLiteral("Перейменувати установку"), QStringLiteral("Нова назва:"), QLineEdit::Normal, l.at(i).name, &ok).trimmed();
            if (ok && !n.isEmpty()) {
                l[i].name = n;
                savePresets(l);
                changedList();
            }
        });
        QObject::connect(del, &QPushButton::clicked, this, [this] {
            const int i = list->indexOfTopLevelItem(list->currentItem());
            if (i < 0) {
                return;
            }
            QList<Preset> l = presets();
            if (QMessageBox::question(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("Видалити установку «%1»?").arg(l.at(i).name)) == QMessageBox::Yes) {
                l.removeAt(i);
                savePresets(l);
                changedList();
            }
        });
    }
    bool hasApply() const override { return false; }
    void load() override
    {
        list->clear();
        for (const Preset &p : presets()) {
            auto *it = new QTreeWidgetItem(list, {p.name, presetContents(p)});
            it->setIcon(0, QIcon(icon(QStringLiteral("IconPresets"), 16)));
        }
    }

private:
    void changedList()
    {
        load();
        if (presetsChanged) {
            presetsChanged();
        }
    }
    QTreeWidget *list;
};

/* -------- Закріплені */

class PinnedPage : public Page
{
public:
    std::function<void(const QString &)> open;
    std::function<QString(const QString &)> titleOf;
    PinnedPage()
    {
        id = QStringLiteral("pinned");
        title = QStringLiteral("Закріплені");
        description = QStringLiteral("Сторінки, які ви закріпили, щоб швидко їх відкривати. Щоб закріпити сторінку, натисніть значок кнопки в її заголовку.");
        build();
        auto *v = new QVBoxLayout;
        list = new QListWidget;
        list->setMinimumHeight(200);
        list->setFrameShape(QFrame::NoFrame);
        v->addWidget(list);
        box(v, 10);
        body->addStretch(1);
        QObject::connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
            if (open) {
                open(it->data(Qt::UserRole).toString());
            }
        });
        QObject::connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
            if (open) {
                open(it->data(Qt::UserRole).toString());
            }
        });
    }
    bool hasApply() const override { return false; }
    void load() override
    {
        list->clear();
        for (const QString &id : store().value(QStringLiteral("Pinned")).toStringList()) {
            auto *it = new QListWidgetItem(QIcon(icon(QStringLiteral("IconFavourites"), 20)), titleOf ? titleOf(id) : id);
            it->setData(Qt::UserRole, id);
            it->setForeground(QColor(0x99, 0, 0));
            list->addItem(it);
        }
        if (!list->count()) {
            auto *it = new QListWidgetItem(QStringLiteral("Немає закріплених сторінок."));
            it->setFlags(Qt::NoItemFlags);
            list->addItem(it);
        }
    }

private:
    QListWidget *list;
};

/* ---------------------------------------------------------------- about */

inline void about(QWidget *parent)
{
    QDialog d(parent);
    d.setWindowTitle(QStringLiteral("Про AMD Catalyst™ Control Center"));
    auto *v = new QVBoxLayout(&d);
    auto *h = new QHBoxLayout;
    auto *logo = new QLabel;
    logo->setPixmap(QPixmap(assets() + QStringLiteral("amd_radeon.png")));
    logo->setStyleSheet(QStringLiteral("background:#080808;padding:6px"));
    h->addWidget(logo, 0, Qt::AlignTop);
    h->addSpacing(14);
    auto *t = label(QStringLiteral("AMD Catalyst Control Center\nВерсія 2014.0915.1813.32737\n© Advanced Micro Devices, Inc., 2002–2014\nУсі права захищено."));
    h->addWidget(t, 1, Qt::AlignTop);
    v->addLayout(h);
    v->addSpacing(14);
    v->addWidget(label(QStringLiteral("Це відтворення AMD Catalyst Control Center для Linux (seven-ccc). Параметри застосовуються через драйвер amdgpu, Mesa та KDE Plasma.")));
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok, &d);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    v->addWidget(bb);
    d.setFixedWidth(430);
    d.exec();
}

/* ---------------------------------------------------------------- the window */

class Opaque : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override { QPainter(this).fillRect(rect(), Qt::white); }
};

class Window : public QWidget
{
public:
    Window()
    {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground); // the Aero border is glass (WindowStyle None + ResizeMode CanResize)
        setWindowTitle(QStringLiteral("AMD Catalyst Control Center"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-ccc")));
        setMinimumSize(440, 440);
        resize(810, 700);
        setMouseTracking(true);
        auto *frameLayout = new QVBoxLayout(this);
        frameLayout->setContentsMargins(Frame, Frame, Frame, Frame);
        auto *content = new Opaque(this); // only the border is glass: the window itself is white
        frameLayout->addWidget(content);
        auto *v = new QVBoxLayout(content);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        banner = new Banner(QStringLiteral("AMD Catalyst Control Center"), this);
        v->addWidget(banner);
        auto *split = new QHBoxLayout;
        split->setSpacing(0);
        nav = new Navigation(this);
        split->addWidget(nav);
        auto *divider = new QFrame(this);
        divider->setFixedWidth(1);
        divider->setStyleSheet(QStringLiteral("background:#a4a4a4"));
        split->addWidget(divider);
        auto *right = new QVBoxLayout;
        right->setSpacing(0);
        header = new PageHeader(this);
        right->addWidget(header);
        stack = new QStackedWidget(this);
        right->addWidget(stack, 1);
        bar = new ApplyBar(this);
        right->addWidget(bar);
        split->addLayout(right, 1);
        v->addLayout(split, 1);

        QObject::connect(banner->close, &QAbstractButton::clicked, this, &QWidget::close);
        QObject::connect(banner->minimize, &QAbstractButton::clicked, this, &QWidget::showMinimized);
        QObject::connect(banner->preferences, &QAbstractButton::clicked, this, [this] { preferences(); });
        banner->logoClicked = [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.amd.com"))); };
        QObject::connect(bar->apply, &QPushButton::clicked, this, [this] {
            if (Page *p = page()) {
                p->apply();
                refreshButtons();
            }
        });
        QObject::connect(bar->discard, &QPushButton::clicked, this, [this] {
            if (Page *p = page()) {
                if (auto *c = dynamic_cast<ColorPage *>(p)) {
                    c->discardPreview();
                }
                p->load();
                refreshButtons();
            }
        });
        QObject::connect(bar->defaults, &QPushButton::clicked, this, [this] {
            if (Page *p = page()) {
                p->defaults();
                refreshButtons();
            }
        });
        QObject::connect(header->pin, &QToolButton::clicked, this, [this] { togglePin(); });
        QObject::connect(header->help, &QToolButton::clicked, this, [] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.amd.com/en/support")));
        });
        nav->activated = [this](const QString &id) { open(id); };

        makePages();
        winId();
        windowHandle()->installEventFilter(this);
        setTopmost(store().value(QStringLiteral("AlwaysOnTop"), false).toBool());
        open(store().value(QStringLiteral("HomeOnStartup"), false).toBool() ? QStringLiteral("info.home") : store().value(QStringLiteral("LastPage"), QStringLiteral("info.sw")).toString());
    }

    void open(const QString &id)
    {
        if (id.startsWith(QLatin1String("preset:"))) {
            const int i = id.mid(7).toInt();
            const QList<Preset> l = presets();
            if (i >= 0 && i < l.size()) {
                applyPreset(l.at(i));
            }
            return;
        }
        Page *p = m_pages.value(id);
        if (!p) {
            p = m_pages.value(QStringLiteral("info.sw"));
        }
        if (Page *old = page(); old && old != p && old->dirty()) {
            const auto r = QMessageBox::question(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("На цій сторінці є незастосовані зміни. Застосувати їх?"),
                                                 QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (r == QMessageBox::Cancel) {
                return;
            }
            if (r == QMessageBox::Yes) {
                old->apply();
            } else if (auto *c = dynamic_cast<ColorPage *>(old)) {
                c->discardPreview();
            }
        }
        stack->setCurrentWidget(p);
        header->title->setText(p->title);
        header->setPinned(pinned().contains(p->id));
        header->pin->setVisible(p->id != QLatin1String("pinned"));
        nav->setCurrent(p->id);
        p->load();
        refreshButtons();
        store().setValue(QStringLiteral("LastPage"), p->id);
    }

protected:
    static constexpr int Frame = 8;
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath outer;
        outer.addRoundedRect(r, 7, 7);
        QPainterPath inner;
        inner.addRect(QRectF(rect()).adjusted(Frame, Frame, -Frame, -Frame));
        // Windows 7 glass: tinted, translucent (KWin blurs what is behind), a sheen in the upper part
        p.fillPath(outer.subtracted(inner), QColor(0x4A, 0x8C, 0xD0, 120));
        QLinearGradient sheen(0, 0, 0, height());
        sheen.setColorAt(0, QColor(255, 255, 255, 70));
        sheen.setColorAt(0.25, QColor(255, 255, 255, 25));
        sheen.setColorAt(1, QColor(255, 255, 255, 10));
        p.fillPath(outer.subtracted(inner), sheen);
        p.setPen(QPen(QColor(0, 0, 0, 120), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(outer);
        p.setPen(QPen(QColor(255, 255, 255, 120), 1));
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 6, 6);
        p.setPen(QPen(QColor(0x30, 0x40, 0x50, 160), 1));
        p.drawRect(QRectF(rect()).adjusted(Frame - 0.5, Frame - 0.5, -Frame + 0.5, -Frame + 0.5));
    }
    QRegion shape() const
    {
        if (isMaximized()) {
            return QRegion(rect());
        }
        QPainterPath p;
        p.addRoundedRect(QRectF(rect()), 7, 7);
        return QRegion(p.toFillPolygon().toPolygon());
    }
    void updateShape()
    {
        setMask(shape());
        if (windowHandle()) {
            KWindowEffects::enableBlurBehind(windowHandle(), true, shape().subtracted(QRegion(rect().adjusted(Frame, Frame, -Frame, -Frame))));
        }
    }
    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        updateShape();
    }
    void showEvent(QShowEvent *e) override
    {
        QWidget::showEvent(e);
        updateShape();
    }
    /* frameless window: resize from the edges */
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == windowHandle()) {
            if (e->type() == QEvent::MouseButtonPress) {
                auto *me = static_cast<QMouseEvent *>(e);
                const Qt::Edges ed = edges(me->position().toPoint());
                if (ed && me->button() == Qt::LeftButton && !isMaximized()) {
                    windowHandle()->startSystemResize(ed);
                    return true;
                }
            } else if (e->type() == QEvent::MouseMove) {
                const Qt::Edges ed = isMaximized() ? Qt::Edges() : edges(static_cast<QMouseEvent *>(e)->position().toPoint());
                if (ed) {
                    setCursor((ed == (Qt::LeftEdge | Qt::TopEdge) || ed == (Qt::RightEdge | Qt::BottomEdge)) ? Qt::SizeFDiagCursor
                              : (ed == (Qt::RightEdge | Qt::TopEdge) || ed == (Qt::LeftEdge | Qt::BottomEdge)) ? Qt::SizeBDiagCursor
                              : (ed & (Qt::LeftEdge | Qt::RightEdge))                                        ? Qt::SizeHorCursor
                                                                                                             : Qt::SizeVerCursor);
                    m_edgeCursor = true;
                } else if (m_edgeCursor) {
                    unsetCursor();
                    m_edgeCursor = false;
                }
            } else if (e->type() == QEvent::Leave && m_edgeCursor) {
                unsetCursor();
                m_edgeCursor = false;
            }
        }
        return QWidget::eventFilter(o, e);
    }
    void closeEvent(QCloseEvent *e) override
    {
        if (Page *p = page(); p && p->dirty()) {
            const auto r = QMessageBox::question(this, QStringLiteral("AMD Catalyst Control Center"), QStringLiteral("На цій сторінці є незастосовані зміни. Застосувати їх перед закриттям?"),
                                                 QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (r == QMessageBox::Cancel) {
                e->ignore();
                return;
            }
            if (r == QMessageBox::Yes) {
                p->apply();
            } else if (auto *c = dynamic_cast<ColorPage *>(p)) {
                c->discardPreview();
            }
        }
        e->accept();
    }

private:
    Qt::Edges edges(QPoint p) const
    {
        const int m = Frame;
        Qt::Edges e;
        if (p.x() < m) e |= Qt::LeftEdge;
        if (p.x() > width() - m) e |= Qt::RightEdge;
        if (p.y() < m) e |= Qt::TopEdge;
        if (p.y() > height() - m) e |= Qt::BottomEdge;
        return e;
    }
    Page *page() const { return static_cast<Page *>(stack->currentWidget()); }
    void add(Page *p)
    {
        p->changed = [this] { refreshButtons(); };
        m_pages.insert(p->id, p);
        stack->addWidget(p);
    }
    void refreshButtons()
    {
        Page *p = page();
        const bool has = p && p->hasApply();
        bar->defaults->setEnabled(p && p->hasDefaults());
        bar->apply->setEnabled(has && p->dirty());
        bar->discard->setEnabled(has && p->dirty());
    }
    QStringList pinned() const { return store().value(QStringLiteral("Pinned")).toStringList(); }
    void togglePin()
    {
        QStringList l = pinned();
        const QString id = page()->id;
        l.contains(id) ? (void)l.removeAll(id) : l.append(id);
        store().setValue(QStringLiteral("Pinned"), l);
        header->setPinned(l.contains(id));
    }
    void setTopmost(bool on)
    {
        const bool vis = isVisible();
        setWindowFlag(Qt::WindowStaysOnTopHint, on);
        if (vis) {
            show();
        }
        store().setValue(QStringLiteral("AlwaysOnTop"), on);
    }

    void makePages()
    {
        auto *pin = new PinnedPage;
        pin->open = [this](const QString &id) { open(id); };
        pin->titleOf = [this](const QString &id) { return m_pages.value(id) ? m_pages.value(id)->title : id; };
        add(pin);
        auto *addp = new AddPresetPage;
        addp->presetsChanged = [this] { sections(); };
        add(addp);
        auto *man = new ManagePresetsPage;
        man->presetsChanged = [this] { sections(); };
        add(man);
        add(new DesktopPropsPage(QStringLiteral("desk.props"), QStringLiteral("Властивості робочого стола"), false));
        add(new ColorPage(QStringLiteral("desk.color"), QStringLiteral("Колір робочого стола"), false));
        add(new RotatePage);
        add(new DisplaysPage);
        add(new DetectPage);
        add(new LcdInfoPage);
        add(new ColorPage(QStringLiteral("lcd.color"), QStringLiteral("Колір (Вбудований дисплей)"), true));
        add(new Gaming3DPage);
        add(new PowerPlayPage);
        if (!overdriveGpus().isEmpty()) {
            add(new OverDrivePage);
        }
        auto *home = new LinksPage(QStringLiteral("info.home"), QStringLiteral("Домашня сторінка AMD"),
                                   QStringLiteral("Покращте свої враження від графіки з AMD."),
                                   {{QStringLiteral("Відвідати www.amd.com"), [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.amd.com"))); }},
                                    {QStringLiteral("Драйвери та підтримка"), [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.amd.com/en/support"))); }},
                                    {QStringLiteral("Ігри та технології Radeon"), [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.amd.com/en/products/graphics"))); }}});
        {
            auto *h = new QHBoxLayout;
            auto *web = new QCheckBox(QStringLiteral("Дозволити веб-вміст"));
            auto *start = new QCheckBox(QStringLiteral("Показувати цю сторінку під час запуску"));
            web->setChecked(store().value(QStringLiteral("AllowWebContent"), true).toBool());
            start->setChecked(store().value(QStringLiteral("HomeOnStartup"), false).toBool());
            QObject::connect(web, &QCheckBox::toggled, home, [](bool on) { store().setValue(QStringLiteral("AllowWebContent"), on); });
            QObject::connect(start, &QCheckBox::toggled, home, [](bool on) { store().setValue(QStringLiteral("HomeOnStartup"), on); });
            h->addWidget(web);
            h->addSpacing(12);
            h->addWidget(start);
            h->addStretch(1);
            home->box(h, 10);
        }
        add(home);
        add(new InfoTablePage(QStringLiteral("info.sw"), QStringLiteral("Програмне забезпечення"), QStringLiteral("Перегляд властивостей програмного забезпечення."),
                              QStringLiteral("Графічне програмне забезпечення:"), [] { return softwareRows(); }, true));
        add(new HardwarePage);
        if (switchable().valid()) {
            add(new PxAppsPage);
            add(new PxGlobalPage);
            add(new PxMonitorPage);
        }
        auto *upd = new LinksPage(QStringLiteral("info.update"), QStringLiteral("Оновлення програмного забезпечення"),
                            QStringLiteral("Перевірка наявності нових версій графічного драйвера (Mesa, ядро Linux, мікропрограми)."),
                            {{QStringLiteral("Перевірити наявність оновлень"), [this] {
                                  const QString o = run(QStringLiteral("apt"), {QStringLiteral("list"), QStringLiteral("--upgradable")}, 20000);
                                  QStringList hits;
                                  for (const QString &l : o.split(QLatin1Char('\n'))) {
                                      if (l.contains(QRegularExpression(QStringLiteral("^(mesa|libgl1-mesa|libegl-mesa|libglx-mesa|linux-image|linux-firmware|xserver-xorg-video-amdgpu|libdrm)")))) {
                                          hits << QStringLiteral("• ") + l.section(QLatin1Char('/'), 0, 0) + QStringLiteral(" → ") + l.section(QLatin1Char(' '), 1, 1);
                                      }
                                  }
                                  if (m_update) {
                                      m_update->result->setText(hits.isEmpty() ? QStringLiteral("Нових версій графічного драйвера не знайдено.")
                                                                         : QStringLiteral("Доступні оновлення графічного драйвера:\n") + hits.join(QLatin1Char('\n')));
                                  }
                              }},
                             {QStringLiteral("Відкрити Центр оновлення Windows"), [] { QProcess::startDetached(QStringLiteral("seven-control"), {QStringLiteral("--windows-update")}); }}});
        m_update = upd;
        add(upd);
        sections();
    }

    void sections()
    {
        const bool advanced = store().value(QStringLiteral("AdvancedView"), true).toBool();
        QList<NavSection> s;
        s << NavSection{QStringLiteral("pinned"), QStringLiteral("Закріплені"), QStringLiteral("IconFavourites"), {}};
        NavSection pre{QStringLiteral("presets"), QStringLiteral("Установки"), QStringLiteral("IconPresets"),
                       {{QStringLiteral("presets.add"), QStringLiteral("Додати установку")}, {QStringLiteral("presets.manage"), QStringLiteral("Керування установками")}}};
        const QList<Preset> l = presets();
        for (int i = 0; i < l.size(); ++i) {
            pre.items << NavItem{QStringLiteral("preset:%1").arg(i), l.at(i).name};
        }
        s << pre;
        s << NavSection{QStringLiteral("desk"), QStringLiteral("Керування робочим столом"), QStringLiteral("IconDesktopDisplays"),
                        {{QStringLiteral("desk.props"), QStringLiteral("Властивості робочого стола")}, {QStringLiteral("desk.color"), QStringLiteral("Колір робочого стола")}}};
        s << NavSection{QStringLiteral("tasks"), QStringLiteral("Загальні завдання дисплея"), QStringLiteral("IconDisplayConfig"),
                        {{QStringLiteral("tasks.rotate"), QStringLiteral("Повернути робочий стіл")},
                         {QStringLiteral("tasks.addremove"), QStringLiteral("Додати чи видалити дисплей")},
                         {QStringLiteral("tasks.detect"), QStringLiteral("Виявити дисплеї")}}};
        bool lcd = false;
        for (const Output &o : outputs()) {
            lcd |= o.builtIn();
        }
        if (lcd) {
            s << NavSection{QStringLiteral("lcd"), QStringLiteral("Мої вбудовані дисплеї"), QStringLiteral("IconLCD"),
                            {{QStringLiteral("lcd.props"), QStringLiteral("Властивості (Вбудований дисплей)")}, {QStringLiteral("lcd.color"), QStringLiteral("Колір (Вбудований дисплей)")}}};
        }
        if (advanced) {
            s << NavSection{QStringLiteral("game"), QStringLiteral("Ігри"), QStringLiteral("IconGaming"), {{QStringLiteral("game.3d"), QStringLiteral("Налаштування 3D-програм")}}};
            if (!overdriveGpus().isEmpty()) {
                s << NavSection{QStringLiteral("perf"), QStringLiteral("Продуктивність"), QStringLiteral("IconPerformance"), {{QStringLiteral("perf.od"), QStringLiteral("AMD OverDrive")}}};
            }
            NavSection power{QStringLiteral("power"), QStringLiteral("Живлення"), QStringLiteral("IconPower"), {{QStringLiteral("power.pp"), QStringLiteral("PowerPlay™")}}};
            if (switchable().valid()) {
                power.items << NavItem{QStringLiteral("power.pxapps"), QStringLiteral("Параметри програм перемиканої графіки")}
                            << NavItem{QStringLiteral("power.pxglobal"), QStringLiteral("Глобальні параметри перемиканої графіки")}
                            << NavItem{QStringLiteral("power.pxmon"), QStringLiteral("Монітор програм перемиканих графічних адаптерів")};
            }
            s << power;
        }
        s << NavSection{QStringLiteral("info"), QStringLiteral("Інформація"), QStringLiteral("IconInfo"),
                        {{QStringLiteral("info.home"), QStringLiteral("Домашня сторінка AMD")},
                         {QStringLiteral("info.sw"), QStringLiteral("Програмне забезпечення")},
                         {QStringLiteral("info.hw"), QStringLiteral("Апаратне забезпечення")},
                         {QStringLiteral("info.update"), QStringLiteral("Оновлення програмного забезпечення")}}};
        nav->setSections(s);
        if (page()) {
            nav->setCurrent(page()->id);
        }
    }

    void preferences()
    {
        QMenu m(this);
        auto *top = m.addAction(QStringLiteral("Завжди поверх інших вікон"));
        top->setCheckable(true);
        top->setChecked(windowFlags() & Qt::WindowStaysOnTopHint);
        m.addSeparator();
        auto *factory = m.addAction(QStringLiteral("Відновити заводські параметри"));
        m.addSeparator();
        const bool adv = store().value(QStringLiteral("AdvancedView"), true).toBool();
        auto *advA = m.addAction(QStringLiteral("Розширене подання"));
        auto *stdA = m.addAction(QStringLiteral("Стандартне подання"));
        advA->setCheckable(true);
        stdA->setCheckable(true);
        advA->setChecked(adv);
        stdA->setChecked(!adv);
        m.addSeparator();
        auto *ab = m.addAction(QStringLiteral("Про AMD Catalyst Control Center..."));
        auto *web = m.addAction(QStringLiteral("Відвідати www.amd.com..."));
        QAction *a = m.exec(banner->preferences->mapToGlobal(QPoint(0, banner->preferences->height() + 2)));
        if (a == top) {
            setTopmost(top->isChecked());
        } else if (a == factory) {
            if (QMessageBox::question(this, QStringLiteral("AMD Catalyst Control Center"),
                                      QStringLiteral("Відновити заводські параметри? Буде скинуто колір робочого стола, налаштування 3D-програм, перемиканої графіки і закріплені сторінки. "
                                                     "Роздільна здатність і поворот дисплеїв не зміняться.")) == QMessageBox::Yes) {
                for (const Output &o : outputs()) {
                    saveColor(o.name, Color());
                    applyColor(o.name, Color());
                }
                setLoginApply(false);
                setVblankMode(1);
                QFile::remove(Gaming3DPage::envPath());
                store().remove(QStringLiteral("Pinned"));
                for (const PxApp &a : pxAssigned()) {
                    removePx(a.key);
                }
                store().remove(QStringLiteral("SwitchableGlobal"));
                applySwitchable();
                open(page()->id);
            }
        } else if (a == advA || a == stdA) {
            store().setValue(QStringLiteral("AdvancedView"), a == advA);
            sections();
        } else if (a == ab) {
            about(this);
        } else if (a == web) {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.amd.com")));
        }
    }

    Banner *banner;
    Navigation *nav;
    PageHeader *header;
    QStackedWidget *stack;
    ApplyBar *bar;
    QHash<QString, Page *> m_pages;
    LinksPage *m_update = nullptr;
    bool m_edgeCursor = false;
};

} // namespace Ccc

int main(int argc, char **argv)
{
    // root helpers (pkexec / boot service): no display there
    if (argc > 1 && (qstrcmp(argv[1], "--od-write") == 0 || qstrcmp(argv[1], "--od-restore") == 0)) {
        QCoreApplication core(argc, argv);
        const QStringList a = core.arguments();
        return a.at(1) == QLatin1String("--od-write") ? Ccc::odWriteMain(a.mid(2)) : Ccc::odRestoreMain();
    }
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("seven-ccc"));
    app.setDesktopFileName(QStringLiteral("seven-ccc"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-ccc")));
    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--apply"))) {
        Ccc::applySavedColors();
        return 0;
    }
    if (Ccc::amdGpu().card.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("AMD Catalyst Control Center"),
                              QStringLiteral("Не вдалося запустити AMD Catalyst Control Center.\nЗараз немає параметрів, які можна налаштувати за допомогою AMD Catalyst Control Center."));
        return 1;
    }
    Ccc::Window w;
    const int pi = args.indexOf(QStringLiteral("--page"));
    if (pi > 0 && pi + 1 < args.size()) {
        w.open(args.at(pi + 1));
    }
    const int si = args.indexOf(QStringLiteral("--screenshot"));
    if (si > 0 && si + 1 < args.size()) {
        w.show();
        QTimer::singleShot(1500, &w, [&w, path = args.at(si + 1)] {
            w.grab().save(path);
            qApp->quit();
        });
        return app.exec();
    }
    w.show();
    return app.exec();
}
