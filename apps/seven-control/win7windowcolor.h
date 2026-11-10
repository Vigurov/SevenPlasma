/*
 * Windows 7 "Window Color and Appearance" (Personalization > Window Color): the 16 Aero colours (the swatch
 * pictures of themecpl.dll), transparency, colour intensity and the colour mixer, shown on the windows at once
 * (aeroglassblur effect), kept by "Save changes"; plus the "Advanced appearance settings" dialog.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once
#include "win7i18n.h"

#include "win7chrome.h"
#include "win7states.h"

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDBusInterface>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <qdrawutil.h>

#include <functional>

namespace Win7WindowColor
{
enum { ColorView = 84 };

struct Preset {
    const char *name;
    QRgb argb;
    const char *picture; // themecpl.dll bitmap
};

/** Windows 7's 16 colours (ColorizationColor, alpha = intensity), in its order */
inline const QList<Preset> &presets()
{
    static const QList<Preset> list{{"Sky", 0x6B74B8FC, "2_1"},       {"Twilight", 0xA80046AD, "2_2"},  {"Sea", 0x8032CDCD, "2_3"},
                                    {"Leaf", 0x6614A600, "2_4"},      {"Lime", 0x6697D937, "2_5"},     {"Sun", 0x54FADC0E, "2_6"},
                                    {"Pumpkin", 0x80FF9C00, "2_7"},     {"Ruby", 0xA8CE0F0F, "2_8"},    {"Fuchsia", 0x66FF0099, "2_21"},
                                    {"Blush", 0x70FCC7F8, "2_22"}, {"Violet", 0x856E3BA1, "2_23"}, {"Lavender", 0x528D5A94, "2_24"},
                                    {"Taupe", 0x6698844C, "2_25"}, {"Chocolate", 0xA84F1B1B, "2_26"}, {"Slate", 0x80555555, "2_27"},
                                    {"Frost", 0x54FCFCFC, "2_28"}};
    return list;
}

/** the effect's settings */
struct Glass {
    int accent = 1; // 1..16 a preset, 0 own colour
    int hue = 0, saturation = 50, brightness = 50, intensity = 50;
    bool transparency = true;
    QString custom = QStringLiteral("#ffffffff");
};

inline Glass readGlass()
{
    const KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-aeroglassblur"));
    Glass s;
    s.accent = g.readEntry("AccentColorName", 1);
    s.hue = g.readEntry("AeroHue", 0);
    s.saturation = g.readEntry("AeroSaturation", 50);
    s.brightness = g.readEntry("AeroBrightness", 50);
    s.intensity = g.readEntry("AeroIntensity", 50);
    s.transparency = g.readEntry("EnableTransparency", true);
    s.custom = g.readEntry("CustomColor", QStringLiteral("#ffffffff"));
    return s;
}

inline void writeGlass(const Glass &s)
{
    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup g(cfg, QStringLiteral("Effect-aeroglassblur"));
    g.writeEntry("AccentColorGroup", 0);
    g.writeEntry("AccentColorName", s.accent);
    g.writeEntry("AeroHue", s.hue);
    g.writeEntry("AeroSaturation", s.saturation);
    g.writeEntry("AeroBrightness", s.brightness);
    g.writeEntry("AeroIntensity", s.intensity);
    g.writeEntry("EnableTransparency", s.transparency);
    g.writeEntry("CustomColor", s.custom);
    g.sync();
    QDBusInterface effects(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"));
    effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("aeroglassblur"));
}

inline Glass fromPreset(int index, bool transparency)
{
    const QColor c = QColor::fromRgba(presets().at(index - 1).argb);
    Glass s;
    s.accent = index;
    s.hue = c.hslHue() < 0 ? 0 : c.hslHue();
    s.saturation = int(c.hsvSaturationF() * 100.0);
    s.brightness = int(c.valueF() * 100.0);
    s.intensity = c.alpha();
    s.transparency = transparency;
    return s;
}

inline QPixmap themecpl(const QString &name)
{
    return QPixmap(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-aero/themecpl/") + name + QStringLiteral(".png")));
}

/** One colour: the 64 x 64 swatch picture, framed while hot or chosen */
class Swatch : public QWidget
{
public:
    int index;
    bool chosen = false;
    std::function<void(int)> clicked;
    Swatch(int i, QWidget *parent = nullptr)
        : QWidget(parent)
        , index(i)
        , m_pic(themecpl(QLatin1String(presets().at(i - 1).picture)))
    {
        setFixedSize(68, 68);
        setAttribute(Qt::WA_Hover);
        setToolTip(tr7(presets().at(i - 1).name));
    }

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::HoverEnter || e->type() == QEvent::HoverLeave) {
            update();
        }
        return QWidget::event(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && clicked) {
            clicked(index);
        }
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QString state;
        if (chosen) {
            state = underMouse() ? QStringLiteral("item-hotselected") : QStringLiteral("item-selected");
        } else if (underMouse()) {
            state = QStringLiteral("item-hot");
        }
        if (!state.isEmpty()) {
            qDrawBorderPixmap(&p, rect(), QMargins(3, 3, 3, 3), QPixmap(Win7States::picture(state)));
        }
        p.drawPixmap(2, 2, m_pic);
    }

private:
    QPixmap m_pic;
};

/** A slider with a painted gradient track (hue / saturation / brightness of the colour mixer) */
class MixSlider : public QSlider
{
public:
    std::function<QLinearGradient(const QRect &)> gradient;
    MixSlider(int max)
        : QSlider(Qt::Horizontal)
    {
        setRange(0, max);
        setFixedWidth(270);
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        if (!gradient) {
            QSlider::paintEvent(e);
            return;
        }
        // the colour track, then only the thumb of the theme over it
        QPainter p(this);
        const QRect track(5, height() / 2 - 3, width() - 10, 7);
        p.setPen(QColor(0x9a, 0x9a, 0x9a));
        p.setBrush(gradient(track));
        p.drawRect(track.adjusted(0, 0, -1, -1));
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        opt.subControls = QStyle::SC_SliderHandle;
        style()->drawComplexControl(QStyle::CC_Slider, &opt, &p, this);
    }
};

/** The round "show / hide" button of Windows 7 (TaskDialog expando button of aero.msstyles) */
class ExpandoButton : public QWidget
{
public:
    bool expanded = false;
    std::function<void()> clicked;
    ExpandoButton()
    {
        setFixedSize(19, 21); // the picture is 19 x 20
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::HoverEnter || e->type() == QEvent::HoverLeave) {
            update();
        }
        return QWidget::event(e);
    }
    void mousePressEvent(QMouseEvent *) override
    {
        m_down = true;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        m_down = false;
        update();
        if (rect().contains(e->position().toPoint()) && clicked) {
            clicked();
        }
    }
    void paintEvent(QPaintEvent *) override
    {
        // the round button with the blue triangle of Windows 7's Basic look (as "Hide Folders"), cut from a real one
        QPainter p(this);
        if (underMouse() && !m_down) {
            p.setOpacity(0.85);
        }
        p.drawPixmap(0, m_down ? 1 : 0, themecpl(expanded ? QStringLiteral("mixer-up") : QStringLiteral("mixer-down")));
    }

private:
    bool m_down = false;
};

/* ---------------- Advanced appearance settings ---------------- */

/** Colour button with the Windows 7 drop-down of 20 colours and "Other..." */
class ColorButton : public QToolButton
{
public:
    QColor color = Qt::black;
    std::function<void()> changed;
    ColorButton()
    {
        setFixedSize(40, 21);
        setPopupMode(QToolButton::InstantPopup);
        setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0px; }"));
        auto *menu = new QMenu(this);
        auto *grid = new QWidget;
        auto *gl = new QGridLayout(grid);
        gl->setContentsMargins(4, 4, 4, 4);
        gl->setSpacing(3);
        static const char *cols[] = {"ffffff", "000000", "c0c0c0", "808080", "ff0000", "800000", "ffff00", "808000", "00ff00", "008000",
                                     "00ffff", "008080", "0000ff", "000080", "ff00ff", "800080", "c0dcc0", "a6caf0", "fffbf0", "a0a0a4"};
        int i = 0;
        for (const char *hex : cols) {
            auto *b = new QToolButton;
            b->setFixedSize(18, 18);
            const QColor c(QLatin1Char('#') + QLatin1String(hex));
            b->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 1px solid #808080; } QToolButton:hover { border: 1px solid #3399ff; }").arg(c.name()));
            connect(b, &QToolButton::clicked, this, [this, c, menu] {
                set(c);
                menu->close();
            });
            gl->addWidget(b, i / 4, i % 4);
            ++i;
        }
        auto *other = new QPushButton(tr7("More..."));
        connect(other, &QPushButton::clicked, this, [this, menu] {
            menu->close();
            const QColor c = QColorDialog::getColor(color, window(), tr7("Color"));
            if (c.isValid()) {
                set(c);
            }
        });
        gl->addWidget(other, 5, 0, 1, 4);
        auto *wa = new QWidgetAction(menu);
        wa->setDefaultWidget(grid);
        menu->addAction(wa);
        setMenu(menu);
    }
    void set(const QColor &c)
    {
        color = c;
        update();
        if (changed) {
            changed();
        }
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QToolButton::paintEvent(e);
        QPainter p(this);
        const QRect sw(5, 5, 20, height() - 10);
        if (isEnabled()) {
            p.fillRect(sw, color);
            p.setPen(Qt::black);
            p.drawRect(sw.adjusted(0, 0, -1, -1));
        }
        // the "▾" of the button
        const QColor a = isEnabled() ? QColor(0, 0, 0) : QColor(0xa0, 0xa0, 0xa0);
        const int ax = width() - 11, ay = height() / 2 - 1;
        for (int i = 0; i < 3; ++i) {
            p.fillRect(QRect(ax + i, ay + i, 5 - 2 * i, 1), a);
        }
    }
};

/** The black preview of the dialog: inactive and active windows, a menu bar, a message box */
class AppearancePreview : public QWidget
{
public:
    AppearancePreview()
    {
        setFixedSize(380, 205);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x80, 0x80, 0x80));
        p.fillRect(rect().adjusted(2, 2, -2, -2), Qt::black);
        auto window = [&p, this](const QRect &r, const QString &title, bool active, bool buttons) {
            QLinearGradient g(r.topLeft(), r.topRight());
            g.setColorAt(0, active ? QColor(0x99, 0xb4, 0xd1) : QColor(0xbf, 0xcd, 0xdb));
            g.setColorAt(1, active ? QColor(0xb9, 0xd1, 0xea) : QColor(0xd7, 0xe4, 0xf2));
            p.fillRect(r, QColor(0xf0, 0xf0, 0xf0));
            p.setPen(QColor(0x60, 0x60, 0x60));
            p.drawRect(r.adjusted(0, 0, -1, -1));
            const QRect bar(r.left() + 3, r.top() + 3, r.width() - 6, 18);
            p.fillRect(bar, g);
            p.setPen(active ? Qt::black : QColor(0x43, 0x4e, 0x54));
            p.drawText(bar.adjusted(4, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, title);
            if (buttons) {
                for (int i = 0; i < 3; ++i) {
                    const QRect b(bar.right() - 16 - i * 17, bar.top() + 2, 15, 14);
                    p.fillRect(b, QColor(0xf0, 0xf0, 0xf0));
                    p.setPen(Qt::black);
                    p.drawRect(b.adjusted(0, 0, -1, -1));
                    p.drawText(b, Qt::AlignCenter, i == 0 ? QStringLiteral("×") : i == 1 ? QStringLiteral("□") : QStringLiteral("_"));
                }
            }
            return bar;
        };
        window(QRect(16, 14, 330, 120), tr7("Inactive Window"), false, true);
        const QRect bar = window(QRect(26, 38, 330, 150), tr7("Active Window"), true, true);
        int x = bar.left() + 6;
        const int y = bar.bottom() + 2;
        for (const auto &[text, col] : {std::pair{tr7("Regular"), QColor(Qt::black)}, std::pair{tr7("Disabled", "color item"), QColor(0x8d, 0x8d, 0x8d)}}) {
            p.setPen(col);
            p.drawText(QRect(x, y, 80, 17), Qt::AlignLeft | Qt::AlignVCenter, text);
            x += fontMetrics().horizontalAdvance(text) + 12;
        }
        p.setPen(Qt::black);
        p.drawText(QRect(x, y, 80, 17), Qt::AlignLeft | Qt::AlignVCenter, tr7("Selected"));
        const QRect text(bar.left(), y + 19, bar.width(), 105);
        p.fillRect(text, Qt::white);
        p.setPen(QColor(0x82, 0x87, 0x90));
        p.drawRect(text.adjusted(0, 0, -1, -1));
        p.setPen(Qt::black);
        p.drawText(text.adjusted(4, 2, 0, 0), Qt::AlignLeft | Qt::AlignTop, tr7("Window Text"));
        p.fillRect(QRect(text.right() - 15, text.top() + 1, 15, text.height() - 2), QColor(0xe8, 0xe8, 0xe8));
        const QRect box(42, 120, 228, 72);
        const QRect mbar = window(box, tr7("Message Box"), true, false);
        p.setPen(Qt::black);
        p.drawText(QRect(mbar.left() + 4, mbar.bottom() + 4, 200, 16), Qt::AlignLeft | Qt::AlignVCenter, tr7("Message Text"));
        const QRect ok(box.center().x() - 34, box.bottom() - 24, 68, 20);
        p.fillRect(ok, QColor(0xf0, 0xf0, 0xf0));
        p.setPen(QColor(0x70, 0x70, 0x70));
        p.drawRect(ok.adjusted(0, 0, -1, -1));
        p.setPen(Qt::black);
        p.drawText(ok, Qt::AlignCenter, QStringLiteral("OK"));
    }
};

/** "Window Color and Appearance" (advanced): the items of the classic appearance. With Aero most of them are
 *  not used, as Windows 7 itself says; the desktop colour and the title bar / menu fonts are applied. */
class AdvancedDialog : public QDialog
{
public:
    explicit AdvancedDialog(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr7("Window Color and Appearance"));
        setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-desk")));
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 7, 7, 7);
        auto *tabs = new QTabWidget;
        auto *page = new QWidget;
        tabs->addTab(page, tr7("Window Color and Appearance"));
        v->addWidget(tabs);
        auto *pv = new QVBoxLayout(page);
        pv->setContentsMargins(11, 11, 11, 11);
        pv->setSpacing(8);
        pv->addWidget(new AppearancePreview, 0, Qt::AlignHCenter);
        auto *info = new QLabel(tr7("To enable Windows Aero, select a Windows 7 theme. Colors, fonts, and sizes selected here apply only if you have selected the Windows 7 Basic theme or a High Contrast theme."));
        info->setWordWrap(true);
        pv->addWidget(info);

        auto *g = new QGridLayout;
        g->setHorizontalSpacing(8);
        g->setVerticalSpacing(3);
        g->addWidget(new QLabel(tr7("Item:")), 0, 0);
        m_item = new QComboBox;
        m_item->setMinimumWidth(210);
        static const char *items[] = {"3D Objects", "Active Title Bar", "Active Window Border", "Application Background", "Border Padding",
                                      "Caption Buttons", "Desktop", "Disabled Item", "Hyperlink", "Icon",
                                      "Icon Spacing (Horizontal)", "Icon Spacing (Vertical)", "Inactive Title Bar",
                                      "Inactive Window Border", "Menu", "Message Box", "Palette Title", "Scrollbar",
                                      "Selected Items", "ToolTip", "Window"};
        for (const char *it : items) {
            m_item->addItem(tr7(it));
        }
        g->addWidget(m_item, 1, 0);
        m_sizeLabel = new QLabel(tr7("Size:"));
        g->addWidget(m_sizeLabel, 0, 1);
        m_size = new QSpinBox;
        m_size->setRange(0, 200);
        m_size->setFixedWidth(52);
        g->addWidget(m_size, 1, 1);
        m_c1Label = new QLabel(tr7("Color 1:"));
        g->addWidget(m_c1Label, 0, 2);
        m_color1 = new ColorButton;
        g->addWidget(m_color1, 1, 2);
        m_c2Label = new QLabel(tr7("Color 2:"));
        g->addWidget(m_c2Label, 0, 3);
        m_color2 = new ColorButton;
        g->addWidget(m_color2, 1, 3);
        m_fontLabel = new QLabel(tr7("Font:"));
        g->addWidget(m_fontLabel, 2, 0);
        m_font = new QComboBox;
        m_font->addItems(QFontDatabase::families());
        g->addWidget(m_font, 3, 0);
        m_fontSizeLabel = new QLabel(tr7("Size:"));
        g->addWidget(m_fontSizeLabel, 2, 1);
        m_fontSize = new QComboBox;
        for (int s : {6, 7, 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24}) {
            m_fontSize->addItem(QString::number(s), s);
        }
        g->addWidget(m_fontSize, 3, 1);
        m_fontColorLabel = new QLabel(tr7("Color:"));
        g->addWidget(m_fontColorLabel, 2, 2);
        m_fontColor = new ColorButton;
        g->addWidget(m_fontColor, 3, 2);
        auto *bi = new QHBoxLayout;
        bi->setSpacing(3);
        m_bold = new QToolButton;
        m_bold->setText(tr7("B"));
        m_bold->setCheckable(true);
        QFont bf = m_bold->font();
        bf.setBold(true);
        m_bold->setFont(bf);
        m_italic = new QToolButton;
        m_italic->setText(tr7("I"));
        m_italic->setCheckable(true);
        QFont itf = m_italic->font();
        itf.setItalic(true);
        m_italic->setFont(itf);
        for (QToolButton *b : {m_bold, m_italic}) {
            b->setFixedSize(21, 21);
            bi->addWidget(b);
        }
        g->addLayout(bi, 3, 3);
        pv->addLayout(g);

        auto *buttons = new QDialogButtonBox;
        auto *ok = buttons->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
        buttons->addButton(tr7("Cancel"), QDialogButtonBox::RejectRole);
        m_apply = buttons->addButton(tr7("Apply"), QDialogButtonBox::ApplyRole);
        m_apply->setEnabled(false);
        for (QAbstractButton *b : buttons->buttons()) {
            b->setMinimumWidth(75);
        }
        v->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(ok, &QPushButton::clicked, this, [this] {
            apply();
            accept();
        });
        connect(m_apply, &QPushButton::clicked, this, [this] {
            apply();
        });

        load();
        connect(m_item, &QComboBox::currentIndexChanged, this, [this] {
            showItem();
        });
        auto dirty = [this] {
            if (!m_loading) {
                store();
                m_apply->setEnabled(true);
            }
        };
        m_color1->changed = dirty;
        m_color2->changed = dirty;
        m_fontColor->changed = dirty;
        connect(m_size, &QSpinBox::valueChanged, this, dirty);
        connect(m_font, &QComboBox::currentIndexChanged, this, dirty);
        connect(m_fontSize, &QComboBox::currentIndexChanged, this, dirty);
        connect(m_bold, &QToolButton::toggled, this, dirty);
        connect(m_italic, &QToolButton::toggled, this, dirty);
        m_item->setCurrentText(tr7("Desktop"));
        showItem();
        setFixedSize(sizeHint()); // not resizable, only the close button, as on Windows 7
    }

private:
    struct Item {
        bool hasSize = false, hasColor1 = false, hasColor2 = false, hasFont = false;
        int size = 0;
        QColor c1, c2, fontColor = Qt::black;
        QFont font;
    };

    void load()
    {
        const KSharedConfig::Ptr g = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
        const QFont general = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        QFont title = general;
        title.fromString(g->group(QStringLiteral("WM")).readEntry("activeFont", general.toString()));
        QFont menu = general;
        menu.fromString(g->group(QStringLiteral("General")).readEntry("menuFont", general.toString()));
        auto c = [](const char *hex) {
            return QColor(QLatin1Char('#') + QLatin1String(hex));
        };
        // Windows 7 Basic's values of the classic items
        m_items = {
            {false, true, false, false, 0, c("f0f0f0"), {}, Qt::black, general},       // 3D objects
            {true, true, true, true, 21, c("99b4d1"), c("b9d1ea"), Qt::black, title},  // active title bar
            {true, true, false, false, 1, c("b4b4b4"), {}, Qt::black, general},        // active window border
            {false, true, false, false, 0, c("ababab"), {}, Qt::black, general},       // application background
            {true, false, false, false, 4, {}, {}, Qt::black, general},                // border padding
            {true, false, false, false, 21, {}, {}, Qt::black, general},               // caption buttons
            {false, true, false, false, 0, desktopColor(), {}, Qt::black, general},    // desktop
            {false, false, false, false, 0, {}, {}, c("6d6d6d"), general},             // disabled item
            {false, false, false, false, 0, {}, {}, c("0066cc"), general},             // hyperlink
            {true, false, false, true, 32, {}, {}, Qt::black, general},                // icon
            {true, false, false, false, 43, {}, {}, Qt::black, general},               // icon spacing (h)
            {true, false, false, false, 43, {}, {}, Qt::black, general},               // icon spacing (v)
            {true, true, true, true, 21, c("bfcddb"), c("d7e4f2"), c("434e54"), title}, // inactive title bar
            {true, true, false, false, 1, c("f4f7fc"), {}, Qt::black, general},        // inactive window border
            {true, true, false, true, 19, c("f0f0f0"), {}, Qt::black, menu},           // menu
            {false, false, false, true, 0, {}, {}, Qt::black, general},                // message box
            {true, false, false, true, 17, {}, {}, Qt::black, general},                // palette title
            {true, false, false, false, 17, {}, {}, Qt::black, general},               // scrollbar
            {true, true, false, true, 19, c("3399ff"), {}, Qt::white, general},        // selected items
            {false, true, false, true, 0, c("ffffe1"), {}, Qt::black, general},        // tooltip
            {false, true, false, false, 0, c("ffffff"), {}, Qt::black, general},       // window
        };
        m_original = m_items;
    }

    QColor desktopColor() const
    {
        const QStringList out = Win7Background::shellScript(QStringLiteral("var d = desktops()[0]; d.currentConfigGroup = ['Wallpaper', d.wallpaperPlugin, 'General'];"
                                                                           " print(d.readConfig('Color'));"))
                                    .trimmed()
                                    .split(QLatin1Char(','));
        if (out.size() >= 3) {
            return QColor(out.at(0).toInt(), out.at(1).toInt(), out.at(2).toInt());
        }
        return QColor::isValidColorName(out.value(0)) ? QColor::fromString(out.value(0)) : QColor(Qt::black);
    }

    void showItem()
    {
        m_loading = true;
        const Item &it = m_items.at(m_item->currentIndex());
        m_size->setEnabled(it.hasSize);
        m_size->setValue(it.hasSize ? it.size : 0);
        if (!it.hasSize) {
            m_size->clear();
        }
        m_color1->setEnabled(it.hasColor1);
        m_color1->color = it.c1;
        m_color2->setEnabled(it.hasColor2);
        m_color2->color = it.c2;
        for (QWidget *w : std::initializer_list<QWidget *>{m_font, m_fontSize, m_fontColor, m_bold, m_italic}) {
            w->setEnabled(it.hasFont);
        }
        m_font->setCurrentText(it.hasFont ? it.font.family() : QString());
        if (!it.hasFont) {
            m_font->setCurrentIndex(-1);
            m_fontSize->setCurrentIndex(-1);
        } else {
            m_fontSize->setCurrentIndex(qMax(0, m_fontSize->findData(it.font.pointSize())));
        }
        m_fontColor->color = it.fontColor;
        m_sizeLabel->setEnabled(it.hasSize);
        m_c1Label->setEnabled(it.hasColor1);
        m_c2Label->setEnabled(it.hasColor2);
        for (QLabel *l : {m_fontLabel, m_fontSizeLabel, m_fontColorLabel}) {
            l->setEnabled(it.hasFont);
        }
        m_bold->setChecked(it.hasFont && it.font.bold());
        m_italic->setChecked(it.hasFont && it.font.italic());
        m_color1->update();
        m_color2->update();
        m_fontColor->update();
        m_loading = false;
    }

    void store()
    {
        Item &it = m_items[m_item->currentIndex()];
        it.size = m_size->value();
        it.c1 = m_color1->color;
        it.c2 = m_color2->color;
        it.fontColor = m_fontColor->color;
        if (it.hasFont) {
            it.font.setFamily(m_font->currentText());
            it.font.setPointSize(m_fontSize->currentData().toInt());
            it.font.setBold(m_bold->isChecked());
            it.font.setItalic(m_italic->isChecked());
        }
    }

    void apply()
    {
        // desktop colour: what shows around a centred or fitted picture, or alone
        if (m_items.at(6).c1 != m_original.at(6).c1) {
            Win7Background::shellScript(QStringLiteral("var all = desktops(); for (var i = 0; i < all.length; ++i) { var d = all[i];"
                                                       " d.currentConfigGroup = ['Wallpaper', d.wallpaperPlugin, 'General']; d.writeConfig('Color', '%1'); }")
                                            .arg(m_items.at(6).c1.name()));
        }
        KSharedConfig::Ptr g = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
        if (m_items.at(1).font != m_original.at(1).font) {
            g->group(QStringLiteral("WM")).writeEntry("activeFont", m_items.at(1).font.toString(), KConfig::Notify);
        }
        if (m_items.at(14).font != m_original.at(14).font) {
            g->group(QStringLiteral("General")).writeEntry("menuFont", m_items.at(14).font.toString(), KConfig::Notify);
        }
        g->sync();
        m_original = m_items;
        m_apply->setEnabled(false);
    }

    QComboBox *m_item = nullptr;
    QSpinBox *m_size = nullptr;
    ColorButton *m_color1 = nullptr;
    ColorButton *m_color2 = nullptr;
    QComboBox *m_font = nullptr;
    QComboBox *m_fontSize = nullptr;
    ColorButton *m_fontColor = nullptr;
    QToolButton *m_bold = nullptr;
    QToolButton *m_italic = nullptr;
    QPushButton *m_apply = nullptr;
    QLabel *m_sizeLabel = nullptr, *m_c1Label = nullptr, *m_c2Label = nullptr, *m_fontLabel = nullptr, *m_fontSizeLabel = nullptr,
           *m_fontColorLabel = nullptr;
    QList<Item> m_items, m_original;
    bool m_loading = false;
};

/* ---------------- the page ---------------- */

class ColorPage : public QWidget
{
public:
    std::function<void()> back;

    ColorPage()
    {
        setObjectName(QStringLiteral("win7page"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
        m_saved = readGlass();
        m_now = m_saved;

        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto *body = new QWidget;
        auto *v = new QVBoxLayout(body);
        v->setContentsMargins(101, 14, 101, 0);
        v->setSpacing(0);
        auto *title = new QLabel(tr7("Change the color of your window borders, Start menu, and taskbar"));
        QFont tf = title->font();
        tf.setPixelSize(16);
        title->setFont(tf);
        title->setStyleSheet(QStringLiteral("color: #1e3287;"));
        v->addWidget(title);
        v->addSpacing(18);

        auto *grid = new QGridLayout;
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        for (int i = 1; i <= 16; ++i) {
            auto *s = new Swatch(i);
            s->clicked = [this](int index) {
                m_now = fromPreset(index, m_transparency->isChecked());
                syncControls();
                writeGlass(m_now);
            };
            m_swatches << s;
            grid->addWidget(s, (i - 1) / 8, (i - 1) % 8);
        }
        grid->setColumnStretch(8, 1);
        v->addLayout(grid);
        v->addSpacing(8);
        m_current = new QLabel;
        v->addWidget(m_current);
        v->addSpacing(14);

        m_transparency = new QCheckBox(tr7("Enable transparent glass"));
        v->addWidget(m_transparency);
        v->addSpacing(10);
        auto *inten = new QHBoxLayout;
        inten->setSpacing(12);
        auto *il = new QLabel(tr7("Color intensity:"));
        il->setFixedWidth(150);
        inten->addWidget(il);
        m_intensity = new MixSlider(217);
        m_intensity->setMinimum(26);
        // (a plain track bar on Windows 7; only the mixer's sliders are coloured)
        inten->addWidget(m_intensity);
        inten->addStretch(1);
        v->addLayout(inten);
        v->addSpacing(12);

        // "Show colour mixer" with the round expander buttons of themecpl.dll
        auto *mixHead = new QHBoxLayout;
        mixHead->setSpacing(10); // themecpl: 10 px after the expando button
        m_expander = new ExpandoButton;
        m_expander->clicked = [this] {
            m_mixer->setVisible(!m_mixer->isVisible());
            syncExpander();
        };
        mixHead->addWidget(m_expander);
        m_mixLink = new Link(QString(), [this] {
            m_mixer->setVisible(!m_mixer->isVisible());
            syncExpander();
        });
        m_mixLink->setWordWrap(false);
        mixHead->addWidget(m_mixLink);
        mixHead->addStretch(1);
        v->addLayout(mixHead);

        m_mixer = new QWidget;
        auto *mg = new QGridLayout(m_mixer);
        mg->setContentsMargins(24, 6, 0, 0);
        mg->setHorizontalSpacing(12);
        mg->setVerticalSpacing(6);
        m_hue = new MixSlider(359);
        m_hue->gradient = [](const QRect &r) {
            QLinearGradient g(r.topLeft(), r.topRight());
            for (int i = 0; i <= 6; ++i) {
                g.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 255, 255));
            }
            return g;
        };
        m_sat = new MixSlider(100);
        m_sat->gradient = [this](const QRect &r) {
            QLinearGradient g(r.topLeft(), r.topRight());
            g.setColorAt(0, QColor::fromHsv(m_now.hue, 0, int(m_now.brightness * 2.55)));
            g.setColorAt(1, QColor::fromHsv(m_now.hue, 255, int(m_now.brightness * 2.55)));
            return g;
        };
        m_bright = new MixSlider(100);
        m_bright->gradient = [this](const QRect &r) {
            QLinearGradient g(r.topLeft(), r.topRight());
            g.setColorAt(0, Qt::black);
            g.setColorAt(1, QColor::fromHsv(m_now.hue, int(m_now.saturation * 2.55), 255));
            return g;
        };
        const QList<QPair<QString, MixSlider *>> rows{{tr7("Hue:"), m_hue}, {tr7("Saturation:"), m_sat}, {tr7("Brightness:"), m_bright}};
        for (int i = 0; i < rows.size(); ++i) {
            auto *l = new QLabel(rows.at(i).first);
            l->setFixedWidth(126);
            mg->addWidget(l, i, 0);
            mg->addWidget(rows.at(i).second, i, 1);
        }
        mg->setColumnStretch(2, 1);
        m_mixer->setVisible(false);
        v->addWidget(m_mixer);
        v->addSpacing(18);
        v->addWidget(new Link(tr7("Advanced appearance settings..."), [] {
            // its own window and taskbar button with the desk.cpl icon, like Windows 7
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--appearance-advanced")});
        }));
        v->addStretch(1);
        outer->addWidget(body, 1);

        auto *footer = new QWidget;
        footer->setObjectName(QStringLiteral("win7footer"));
        footer->setAttribute(Qt::WA_StyledBackground, true);
        footer->setStyleSheet(QStringLiteral("#win7footer { background: #f0f0f0; border-top: 1px solid #cccccc; }"));
        footer->setFixedHeight(51);
        auto *fh = new QHBoxLayout(footer);
        fh->setContentsMargins(101, 14, 101, 14);
        fh->setSpacing(12);
        fh->addStretch(1);
        auto *save = new QPushButton(tr7("Save changes"));
        save->setMinimumWidth(101);
        auto *cancel = new QPushButton(tr7("Cancel"));
        cancel->setMinimumWidth(74);
        fh->addWidget(save);
        fh->addWidget(cancel);
        outer->addWidget(footer);

        connect(save, &QPushButton::clicked, this, [this] {
            writeGlass(m_now);
            m_saved = m_now;
            if (back) {
                back();
            }
        });
        connect(cancel, &QPushButton::clicked, this, [this] {
            writeGlass(m_saved);
            if (back) {
                back();
            }
        });
        connect(m_transparency, &QCheckBox::toggled, this, [this](bool on) {
            if (m_syncing) {
                return;
            }
            m_now.transparency = on;
            writeGlass(m_now);
        });
        auto mixed = [this] {
            if (m_syncing) {
                return;
            }
            m_now.accent = 0; // own colour
            m_now.hue = m_hue->value();
            m_now.saturation = m_sat->value();
            m_now.brightness = m_bright->value();
            m_now.intensity = m_intensity->value();
            m_now.custom = QColor::fromHsv(m_now.hue, int(m_now.saturation * 2.55), int(m_now.brightness * 2.55), m_now.intensity).name(QColor::HexArgb);
            syncControls();
            writeGlass(m_now);
        };
        for (QSlider *s : {static_cast<QSlider *>(m_hue), static_cast<QSlider *>(m_sat), static_cast<QSlider *>(m_bright)}) {
            connect(s, &QSlider::valueChanged, this, mixed);
        }
        connect(m_intensity, &QSlider::valueChanged, this, [this] {
            if (m_syncing) {
                return;
            }
            m_now.intensity = m_intensity->value();
            writeGlass(m_now);
        });
        syncControls();
        syncExpander();
    }

private:
    void syncControls()
    {
        m_syncing = true;
        for (Swatch *s : std::as_const(m_swatches)) {
            s->chosen = s->index == m_now.accent;
            s->update();
        }
        m_current->setText(tr7("Current color: %1")
                               .arg(m_now.accent >= 1 && m_now.accent <= 16 ? tr7(presets().at(m_now.accent - 1).name) : tr7("Custom")));
        m_transparency->setChecked(m_now.transparency);
        m_intensity->setValue(m_now.intensity);
        m_hue->setValue(m_now.hue);
        m_sat->setValue(m_now.saturation);
        m_bright->setValue(m_now.brightness);
        for (QWidget *w : {static_cast<QWidget *>(m_intensity), static_cast<QWidget *>(m_sat), static_cast<QWidget *>(m_bright)}) {
            w->update();
        }
        m_syncing = false;
    }

    void syncExpander()
    {
        const bool open = m_mixer->isVisible();
        m_expander->expanded = open;
        m_expander->update();
        m_mixLink->setText(open ? tr7("Hide color mixer") : tr7("Show color mixer"));
    }

    Glass m_saved, m_now;
    QList<Swatch *> m_swatches;
    QLabel *m_current = nullptr;
    QCheckBox *m_transparency = nullptr;
    MixSlider *m_intensity = nullptr;
    MixSlider *m_hue = nullptr;
    MixSlider *m_sat = nullptr;
    MixSlider *m_bright = nullptr;
    ExpandoButton *m_expander = nullptr;
    Link *m_mixLink = nullptr;
    QWidget *m_mixer = nullptr;
    bool m_syncing = false;
};
}
