/*
 * "Ease of Access Center" of Windows 7: the center page, its "Explore all settings" pages, the
 * "Get recommendations" questions, the Magnifier and Narrator windows, the On-Screen Keyboard
 * and "Keyboard Properties".
 *
 *   Sticky / Filter / Toggle / Mouse Keys,  -> kaccessrc (kaccess applies them through XKB AccessX)
 *   keyboard shortcuts, sound sentry
 *   Narrator                                 -> Orca (kaccessrc [ScreenReader] starts it at log on)
 *   Magnifier                                -> the KWin zoom effect, driven by its global shortcuts
 *   On-Screen Keyboard                       -> our own keyboard window, keys sent with XTest
 *   mouse pointers                           -> seven-cursors-{white,black}-{1,2,3} (tools/make_cursor_schemes.py)
 *   hover activates windows / no snapping    -> kwinrc FocusPolicy / ElectricBorderMaximize, -Tiling
 *   animations, notification time            -> kdeglobals AnimationDurationFactor, plasmanotifyrc PopupTimeout
 *   keyboard speed, cursor blink rate        -> kcminputrc [Keyboard] + xset, kdeglobals CursorBlinkRate
 * Settings without a counterpart (Audio Description, text captions, thicker focus rectangle, inverting
 * pointers, ...) are shown like in Windows 7 but greyed out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7chrome.h"
#include "win7display.h"

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QGridLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>

#include <QMenuBar>
#include <QScrollArea>

#include <cmath>
#include <dlfcn.h>

namespace Win7Access
{
enum {
    CenterView = 3,
    SeeView,
    NoDisplayView,
    NoInputView,
    MouseView,
    MouseKeysView,
    KeyboardView,
    StickyView,
    FilterView,
    SoundsView,
    FocusView,
    QuestionsView,
    ResultsView,
};

inline bool isAccessView(int v)
{
    return v >= CenterView && v <= ResultsView;
}

struct Nav {
    std::function<void(int)> open;
    std::function<void()> home;
    std::function<void()> back;
    std::function<void(const QStringList &)> self; // runs seven-control with these arguments
};

/* ---------------- settings ---------------- */

struct Settings {
    // keyboard (kaccessrc [Keyboard])
    bool sticky = false, stickyLatch = true, stickyAutoOff = false, stickyBeep = false;
    bool toggleBeep = false;
    bool slow = false, bounce = false;
    int slowDelay = 500, bounceDelay = 500;
    bool slowBeep = true;
    bool gestures = true, gestureConfirm = true, accessXBeep = true;
    // mouse keys (kaccessrc [Mouse])
    bool mouseKeys = false;
    int mkMaxSpeed = 30, mkTimeToMax = 30;
    // bell
    bool visibleBell = false, visibleBellInvert = false;
    // screen reader, magnifier, on-screen keyboard at log on
    bool narrator = false, magnifier = false, osk = false;
    // windows
    bool hoverFocus = false, noSnap = false;
    bool noAnimations = false;
    int popupSeconds = 5;
    QString cursorTheme;
    bool filterRepeat = false; // "Turn on Repeat Keys and Slow Keys" chosen instead of Bounce Keys
};

inline QString autostartFile(const QString &what)
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart/seven-%1.desktop").arg(what);
}

inline Settings load()
{
    Settings s;
    KConfig acc(QStringLiteral("kaccessrc"));
    const KConfigGroup kb(&acc, QStringLiteral("Keyboard"));
    s.sticky = kb.readEntry("StickyKeys", false);
    s.stickyLatch = kb.readEntry("StickyKeysLatch", true);
    s.stickyAutoOff = kb.readEntry("StickyKeysAutoOff", false);
    s.stickyBeep = kb.readEntry("StickyKeysBeep", false);
    s.toggleBeep = kb.readEntry("ToggleKeysBeep", false);
    s.slow = kb.readEntry("SlowKeys", false);
    s.slowDelay = kb.readEntry("SlowKeysDelay", 500);
    s.slowBeep = kb.readEntry("SlowKeysAcceptBeep", true);
    s.bounce = kb.readEntry("BounceKeys", false);
    s.bounceDelay = kb.readEntry("BounceKeysDelay", 500);
    s.gestures = kb.readEntry("Gestures", true);
    s.gestureConfirm = kb.readEntry("GestureConfirmation", true);
    s.accessXBeep = kb.readEntry("AccessXBeep", true);
    s.filterRepeat = s.slow;
    const KConfigGroup mouse(&acc, QStringLiteral("Mouse"));
    s.mouseKeys = mouse.readEntry("MouseKeys", false);
    s.mkMaxSpeed = mouse.readEntry("MKMaxSpeed", 30);
    s.mkTimeToMax = mouse.readEntry("MKTimeToMax", 30);
    const KConfigGroup bell(&acc, QStringLiteral("Bell"));
    s.visibleBell = bell.readEntry("VisibleBell", false);
    s.visibleBellInvert = bell.readEntry("VisibleBellInvert", false);
    s.narrator = KConfigGroup(&acc, QStringLiteral("ScreenReader")).readEntry("Enabled", false);
    s.magnifier = QFile::exists(autostartFile(QStringLiteral("magnifier")));
    s.osk = QFile::exists(autostartFile(QStringLiteral("osk")));
    KConfig kwin(QStringLiteral("kwinrc"));
    const KConfigGroup win(&kwin, QStringLiteral("Windows"));
    s.hoverFocus = win.readEntry("FocusPolicy", QString()) == QLatin1String("FocusFollowsMouse");
    s.noSnap = !win.readEntry("ElectricBorderMaximize", true) && !win.readEntry("ElectricBorderTiling", true);
    KConfig globals(QStringLiteral("kdeglobals"));
    s.noAnimations = KConfigGroup(&globals, QStringLiteral("KDE")).readEntry("AnimationDurationFactor", 1.0) == 0.0;
    KConfig notify(QStringLiteral("plasmanotifyrc"));
    s.popupSeconds = KConfigGroup(&notify, QStringLiteral("Notifications")).readEntry("PopupTimeout", 5000) / 1000;
    KConfig input(QStringLiteral("kcminputrc"));
    s.cursorTheme = KConfigGroup(&input, QStringLiteral("Mouse")).readEntry("cursorTheme", QStringLiteral("breeze_cursors"));
    return s;
}

inline void writeAutostart(const QString &what, bool on, const QString &name, const QString &args)
{
    const QString f = autostartFile(what);
    if (!on) {
        QFile::remove(f);
        return;
    }
    QDir().mkpath(QFileInfo(f).path());
    QFile file(f);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nExec=%2 %3\nNoDisplay=true\nX-KDE-autostart-phase=2\n")
                       .arg(name, QCoreApplication::applicationFilePath(), args)
                       .toUtf8());
    }
}

inline void save(const Settings &s, const Settings &old)
{
    KConfig acc(QStringLiteral("kaccessrc"));
    KConfigGroup kb(&acc, QStringLiteral("Keyboard"));
    kb.writeEntry("StickyKeys", s.sticky);
    kb.writeEntry("StickyKeysLatch", s.stickyLatch);
    kb.writeEntry("StickyKeysAutoOff", s.stickyAutoOff);
    kb.writeEntry("StickyKeysBeep", s.stickyBeep);
    kb.writeEntry("ToggleKeysBeep", s.toggleBeep);
    kb.writeEntry("SlowKeys", s.slow);
    kb.writeEntry("SlowKeysDelay", s.slowDelay);
    kb.writeEntry("SlowKeysAcceptBeep", s.slowBeep);
    kb.writeEntry("SlowKeysPressBeep", false);
    kb.writeEntry("BounceKeys", s.bounce);
    kb.writeEntry("BounceKeysDelay", s.bounceDelay);
    kb.writeEntry("BounceKeysRejectBeep", s.slowBeep);
    kb.writeEntry("Gestures", s.gestures);
    kb.writeEntry("GestureConfirmation", s.gestureConfirm);
    kb.writeEntry("AccessXBeep", s.accessXBeep);
    KConfigGroup mouse(&acc, QStringLiteral("Mouse"));
    mouse.writeEntry("MouseKeys", s.mouseKeys);
    mouse.writeEntry("MKMaxSpeed", s.mkMaxSpeed);
    mouse.writeEntry("MKTimeToMax", s.mkTimeToMax);
    KConfigGroup bell(&acc, QStringLiteral("Bell"));
    bell.writeEntry("VisibleBell", s.visibleBell);
    bell.writeEntry("VisibleBellInvert", s.visibleBellInvert);
    KConfigGroup(&acc, QStringLiteral("ScreenReader")).writeEntry("Enabled", s.narrator);
    acc.sync();
    QProcess::startDetached(QStringLiteral("kaccess"), {}); // re-reads kaccessrc and applies it through AccessX

    if (s.narrator != old.narrator) {
        if (s.narrator) {
            QProcess::startDetached(QStringLiteral("orca"), {QStringLiteral("--replace")});
        } else {
            QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-x"), QStringLiteral("orca")});
        }
    }
    writeAutostart(QStringLiteral("magnifier"), s.magnifier, QStringLiteral("Magnifier"), QStringLiteral("--magnifier"));
    writeAutostart(QStringLiteral("osk"), s.osk, QStringLiteral("On-Screen Keyboard"), QStringLiteral("--osk"));
    if (s.magnifier && !old.magnifier) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--magnifier")});
    }
    if (s.osk && !old.osk) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--osk")});
    }

    if (s.hoverFocus != old.hoverFocus || s.noSnap != old.noSnap) {
        KConfig kwin(QStringLiteral("kwinrc"));
        KConfigGroup win(&kwin, QStringLiteral("Windows"));
        if (s.hoverFocus) {
            win.writeEntry("FocusPolicy", QStringLiteral("FocusFollowsMouse"));
            win.writeEntry("AutoRaise", true);
            win.writeEntry("AutoRaiseInterval", 750);
            win.writeEntry("DelayFocusInterval", 300);
        } else {
            win.writeEntry("FocusPolicy", QStringLiteral("ClickToFocus"));
            win.writeEntry("AutoRaise", false);
        }
        win.writeEntry("ElectricBorderMaximize", !s.noSnap);
        win.writeEntry("ElectricBorderTiling", !s.noSnap);
        kwin.sync();
        QProcess::startDetached(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("reconfigure")});
    }
    if (s.noAnimations != old.noAnimations) {
        KConfig globals(QStringLiteral("kdeglobals"));
        KConfigGroup kde(&globals, QStringLiteral("KDE"));
        static double before = 0.5;
        if (s.noAnimations) {
            before = kde.readEntry("AnimationDurationFactor", 0.5);
            kde.writeEntry("AnimationDurationFactor", 0.0, KConfig::Notify | KConfig::Normal);
        } else {
            kde.writeEntry("AnimationDurationFactor", before > 0 ? before : 0.5, KConfig::Notify | KConfig::Normal);
        }
        globals.sync();
    }
    if (s.popupSeconds != old.popupSeconds) {
        KConfig notify(QStringLiteral("plasmanotifyrc"));
        KConfigGroup(&notify, QStringLiteral("Notifications")).writeEntry("PopupTimeout", s.popupSeconds * 1000, KConfig::Notify | KConfig::Normal);
        notify.sync();
    }
    if (s.cursorTheme != old.cursorTheme && !s.cursorTheme.isEmpty()) {
        static const QRegularExpression level(QStringLiteral("^seven-cursors-\\w+-(\\d)$"));
        const auto m = level.match(s.cursorTheme);
        const int size = m.hasMatch() ? QList<int>{32, 48, 64}.value(m.captured(1).toInt() - 1, 32) : 24;
        QProcess::startDetached(QStringLiteral("plasma-apply-cursortheme"), {s.cursorTheme, QStringLiteral("--size"), QString::number(size)});
    }
}

/* ---------------- page pieces ---------------- */

inline QLabel *heading(const QString &text, int px = 16)
{
    auto *l = new QLabel(text);
    QFont f = l->font();
    f.setPixelSize(px);
    l->setFont(f);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: #1e3287;"));
    return l;
}

inline QLabel *plainText(const QString &t)
{
    auto *l = new QLabel(t);
    l->setWordWrap(true);
    return l;
}

/** text placed under a checkbox, at the checkbox text */
inline QLabel *under(const QString &t, int indent = 24)
{
    auto *l = plainText(t);
    l->setContentsMargins(indent, 0, 0, 2);
    return l;
}

inline QString unavailable()
{
    return QStringLiteral("У цій системі такого параметра немає.");
}

inline QCheckBox *dead(const QString &t)
{
    auto *c = new QCheckBox(t);
    c->setEnabled(false);
    c->setToolTip(unavailable());
    return c;
}

inline Link *indentedLink(const QString &t, std::function<void()> open, int indent = 24)
{
    auto *l = new Link(t, std::move(open));
    l->setContentsMargins(indent, 0, 0, 0);
    l->setWordWrap(false);
    l->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    return l;
}

inline void learnOnline()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://userbase.kde.org/Accessibility")));
}

/**
 * Full-width settings page: centered column, scrollable, OK / Cancel / Apply at the bottom.
 * `body` gets the content, the Settings are edited in place and saved on OK / Apply.
 */
class SettingsPage : public QWidget
{
public:
    SettingsPage(const Nav &nav, const QString &title, const QString &intro = QStringLiteral("Якщо вибрати ці параметри, вони автоматично вмикатимуться під час кожного входу в систему."))
        : m_nav(nav)
        , m_old(load())
        , s(m_old)
    {
        setObjectName(QStringLiteral("win7page"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *inner = new QWidget;
        inner->setObjectName(QStringLiteral("win7eoabody"));
        inner->setAttribute(Qt::WA_StyledBackground, true);
        inner->setStyleSheet(QStringLiteral("#win7eoabody { background: #ffffff; }"));
        auto *row = new QHBoxLayout(inner);
        row->setContentsMargins(0, 18, 0, 20);
        row->addStretch(1);
        auto *col = new QWidget;
        col->setFixedWidth(820);
        body = new QVBoxLayout(col);
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(6);
        body->addWidget(heading(title));
        if (!intro.isEmpty()) {
            body->addWidget(plainText(intro));
        }
        body->addSpacing(6);
        row->addWidget(col, 0, Qt::AlignTop);
        row->addStretch(2);
        scroll->setWidget(inner);
        outer->addWidget(scroll, 1);
        auto *bar = new QWidget;
        bar->setObjectName(QStringLiteral("win7eoabar"));
        bar->setAttribute(Qt::WA_StyledBackground, true);
        bar->setStyleSheet(QStringLiteral("#win7eoabar { background: #f0f0f0; border-top: 1px solid #dfdfdf; }"));
        auto *bh = new QHBoxLayout(bar);
        bh->setContentsMargins(10, 9, 14, 9);
        bh->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        m_apply = new QPushButton(QStringLiteral("Застосувати"));
        for (QPushButton *b : {ok, cancel, m_apply}) {
            b->setMinimumWidth(85);
            bh->addWidget(b);
        }
        bh->addSpacing(qMax(0, 0));
        m_apply->setEnabled(false);
        outer->addWidget(bar);
        connect(ok, &QPushButton::clicked, this, [this] {
            apply();
            m_nav.back();
        });
        connect(cancel, &QPushButton::clicked, this, [this] { m_nav.back(); });
        connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
    }

    /** "Mouse pointers" and similar group captions with the grey line */
    void section(const QString &t)
    {
        body->addSpacing(8);
        body->addWidget(new SectionHeader(t));
    }

    QCheckBox *check(const QString &t, bool *value, int indent = 0)
    {
        auto *c = new QCheckBox(t);
        c->setChecked(*value);
        if (indent) {
            auto *w = new QWidget;
            auto *h = new QHBoxLayout(w);
            h->setContentsMargins(indent, 0, 0, 0);
            h->addWidget(c);
            h->addStretch(1);
            body->addWidget(w);
        } else {
            body->addWidget(c);
        }
        connect(c, &QCheckBox::toggled, this, [this, value](bool on) {
            *value = on;
            changed();
        });
        return c;
    }

    void deadCheck(const QString &t, int indent = 24)
    {
        auto *w = new QWidget;
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(indent, 0, 0, 0);
        h->addWidget(dead(t));
        h->addStretch(1);
        body->addWidget(w);
    }

    void text(const QString &t, int indent = 48)
    {
        body->addWidget(under(t, indent));
    }

    void link(const QString &t, std::function<void()> open, int indent = 24)
    {
        body->addWidget(indentedLink(t, std::move(open), indent));
    }

    void changed()
    {
        m_apply->setEnabled(true);
    }

    void apply()
    {
        save(s, m_old);
        m_old = s;
        m_apply->setEnabled(false);
    }

    /** the three "keyboard shortcut" checkboxes; all of them are the one AccessX gestures switch */
    void shortcutBlock(const QString &shortcutText)
    {
        section(QStringLiteral("Сполучення клавіш"));
        check(shortcutText, &s.gestures, 24);
        text(QStringLiteral("Якщо для ввімкнення параметрів спеціальних можливостей використовуються сполучення клавіш:"), 48);
        check(QStringLiteral("Відображати попередження під час увімкнення параметра"), &s.gestureConfirm, 48);
        check(QStringLiteral("Подавати звуковий сигнал під час увімкнення або вимкнення параметра"), &s.accessXBeep, 48);
    }

    void seeAlso(const QList<QPair<QString, std::function<void()>>> &links)
    {
        section(QStringLiteral("Див. також"));
        for (const auto &l : links) {
            link(l.first, l.second);
        }
        link(QStringLiteral("Додаткові відомості про допоміжні технології в Інтернеті"), learnOnline);
    }

    void finish()
    {
        body->addStretch(1);
    }

    QVBoxLayout *body;
    Nav m_nav;
    Settings m_old;
    Settings s;

private:
    QPushButton *m_apply;
};

/* ---------------- the pages ---------------- */

/** a big tool link of "Quick access to common tools" */
class ToolLink : public QWidget
{
public:
    ToolLink(const QString &icon, const QString &text, const QString &tip, std::function<void()> open)
        : m_open(std::move(open))
    {
        setCursor(Qt::PointingHandCursor);
        setToolTip(tip);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(4, 3, 8, 3);
        h->setSpacing(10);
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(icon).pixmap(32, 32));
        h->addWidget(ic);
        m_label = new QLabel(text);
        QFont f = m_label->font();
        f.setPixelSize(16);
        m_label->setFont(f);
        h->addWidget(m_label, 1);
        setHot(false);
    }
    void setHot(bool on)
    {
        m_hot = on;
        update();
    }
    void activate()
    {
        if (m_open) {
            m_open();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_hot) {
            return;
        }
        QPainter p(this);
        p.setPen(QPen(QColor(0x33, 0x66, 0xcc), 2));
        p.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
    }
    void enterEvent(QEnterEvent *) override
    {
        m_label->setStyleSheet(QStringLiteral("text-decoration: underline; color: #3399ff;"));
    }
    void leaveEvent(QEvent *) override
    {
        m_label->setStyleSheet(QString());
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            activate();
        }
    }

private:
    std::function<void()> m_open;
    QLabel *m_label;
    bool m_hot = false;
};

/** "Make your computer easier to use" */
class CenterPage : public QWidget
{
public:
    explicit CenterPage(const Nav &nav)
        : m_nav(nav)
    {
        setFocusPolicy(Qt::StrongFocus);
        auto *side = Win7Display::sidePane(nav.home, {}, {});
        auto *content = new QWidget;
        auto *v = new QVBoxLayout(content);
        v->setContentsMargins(26, 18, 30, 20);
        v->setSpacing(6);
        v->addWidget(heading(QStringLiteral("Полегшення роботи з комп'ютером")));
        v->addSpacing(4);
        auto *box = new QFrame;
        box->setObjectName(QStringLiteral("win7eoaquick"));
        box->setStyleSheet(QStringLiteral("#win7eoaquick { border: 1px solid #d5d9de; background: #ffffff; }"));
        box->setMaximumWidth(820);
        box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto *bv = new QVBoxLayout(box);
        bv->setContentsMargins(0, 0, 0, 0);
        bv->setSpacing(0);
        auto *top = new QWidget;
        top->setObjectName(QStringLiteral("win7eoaquicktop"));
        top->setAttribute(Qt::WA_StyledBackground, true);
        top->setStyleSheet(QStringLiteral("#win7eoaquicktop { background: #f1f2f4; border-bottom: 1px solid #d5d9de; }"));
        auto *tv = new QVBoxLayout(top);
        tv->setContentsMargins(26, 10, 18, 12);
        tv->setSpacing(4);
        tv->addWidget(heading(QStringLiteral("Швидкий доступ до звичайних засобів"), 17));
        tv->addWidget(plainText(QStringLiteral("Засоби в цьому розділі допоможуть розпочати роботу.")));
        auto *scanText = plainText(QStringLiteral("Windows може автоматично читати й переглядати цей список. Щоб вибрати виділений засіб, натисніть клавішу ПРОБІЛ."));
        scanText->setMinimumWidth(200);
        tv->addWidget(scanText);
        auto *row = new QHBoxLayout;
        KConfigGroup cfg(KSharedConfig::openConfig(QStringLiteral("seven-controlrc")), QStringLiteral("EaseOfAccess"));
        m_read = new QCheckBox(QStringLiteral("Завжди читати цей розділ уголос"));
        m_scan = new QCheckBox(QStringLiteral("Завжди переглядати цей розділ"));
        m_read->setChecked(cfg.readEntry("ReadAloud", false));
        m_scan->setChecked(cfg.readEntry("Scan", false));
        row->addWidget(m_read);
        row->addSpacing(60);
        row->addWidget(m_scan);
        row->addStretch(1);
        tv->addLayout(row);
        bv->addWidget(top);
        auto *grid = new QGridLayout;
        grid->setContentsMargins(26, 14, 18, 14);
        grid->setHorizontalSpacing(30);
        grid->setVerticalSpacing(4);
        m_tools = {new ToolLink(QStringLiteral("seven-eoa-magnifier"), QStringLiteral("Запустити екранну лупу"), QStringLiteral("Екранна лупа збільшує частину екрана, на якій розташовано покажчик миші."),
                                [nav] { nav.self({QStringLiteral("--magnifier")}); }),
                   new ToolLink(QStringLiteral("seven-eoa-narrator"), QStringLiteral("Запустити екранного диктора"), QStringLiteral("Екранний диктор читає вголос текст на екрані."),
                                [nav] { nav.self({QStringLiteral("--narrator")}); }),
                   new ToolLink(QStringLiteral("seven-eoa-osk"), QStringLiteral("Запустити екранну клавіатуру"),
                                QStringLiteral("Екранна клавіатура дає змогу вводити текст мишею або іншим вказівним пристроєм, клацаючи клавіші на зображенні клавіатури."),
                                [nav] { nav.self({QStringLiteral("--osk")}); }),
                   new ToolLink(QStringLiteral("seven-eoa-contrast"), QStringLiteral("Налаштувати високу контрастність"),
                                QStringLiteral("Висока контрастність робить текст і зображення чіткішими та зручнішими для читання."), [nav] { nav.open(Win7Display::PersonalizeView); })};
        grid->addWidget(m_tools[0], 0, 0);
        grid->addWidget(m_tools[1], 0, 1);
        grid->addWidget(m_tools[2], 1, 0);
        grid->addWidget(m_tools[3], 1, 1);
        grid->setColumnStretch(2, 1);
        bv->addLayout(grid);
        v->addWidget(box);
        // "Not sure where to start?"
        auto *tip = new QWidget;
        tip->setObjectName(QStringLiteral("win7eoatip"));
        tip->setAttribute(Qt::WA_StyledBackground, true);
        tip->setStyleSheet(QStringLiteral("#win7eoatip { background: #fdfbd0; }"));
        tip->setMaximumWidth(820);
        auto *th = new QHBoxLayout(tip);
        th->setContentsMargins(10, 5, 10, 5);
        auto *bulb = new QLabel;
        bulb->setPixmap(QIcon::fromTheme(QStringLiteral("seven-eoa-tip")).pixmap(16, 16));
        th->addWidget(bulb);
        th->addWidget(new QLabel(QStringLiteral("Не знаєте, з чого почати?")));
        auto *rec = new Link(QStringLiteral("Отримати рекомендації щодо полегшення роботи з комп'ютером"), [nav] { nav.open(QuestionsView); });
        rec->setWordWrap(false);
        th->addWidget(rec);
        th->addStretch(1);
        v->addSpacing(8);
        v->addWidget(tip);
        v->addSpacing(14);
        v->addWidget(heading(QStringLiteral("Перегляд усіх параметрів"), 17));
        v->addWidget(plainText(QStringLiteral("Якщо вибрати ці параметри, вони автоматично вмикатимуться під час кожного входу в систему.")));
        v->addSpacing(6);
        const QList<std::tuple<QString, QString, QString, int>> all{
            {QStringLiteral("seven-eoa-nodisplay"), QStringLiteral("Використання комп'ютера без дисплея"), QStringLiteral("Оптимізація для незрячих"), NoDisplayView},
            {QStringLiteral("seven-eoa-see"), QStringLiteral("Полегшення роботи з екраном"), QStringLiteral("Оптимізація зображення на екрані"), SeeView},
            {QStringLiteral("seven-eoa-noinput"), QStringLiteral("Використання комп'ютера без миші або клавіатури"), QStringLiteral("Налаштування альтернативних пристроїв введення"), NoInputView},
            {QStringLiteral("seven-eoa-mouse"), QStringLiteral("Полегшення роботи з мишею"), QStringLiteral("Налаштування параметрів миші та інших вказівних пристроїв"), MouseView},
            {QStringLiteral("seven-eoa-keyboard"), QStringLiteral("Полегшення роботи з клавіатурою"), QStringLiteral("Налаштування параметрів клавіатури"), KeyboardView},
            {QStringLiteral("seven-eoa-sounds"), QStringLiteral("Використання текстових або візуальних альтернатив для звуків"), QStringLiteral("Налаштування альтернатив для звуків"), SoundsView},
            {QStringLiteral("seven-eoa-focus"), QStringLiteral("Полегшення зосередження на завданнях"), QStringLiteral("Налаштування параметрів читання та введення"), FocusView},
        };
        for (const auto &[icon, title, desc, view] : all) {
            auto *r = new QHBoxLayout;
            r->setSpacing(12);
            auto *ic = new QLabel;
            ic->setPixmap(QIcon::fromTheme(icon).pixmap(48, 48));
            ic->setFixedSize(52, 50);
            r->addWidget(ic, 0, Qt::AlignTop);
            auto *col = new QVBoxLayout;
            col->setSpacing(2);
            const int target = view;
            auto *l = new Link(title, [nav, target] { nav.open(target); });
            l->setWordWrap(false);
            l->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
            col->addWidget(l);
            col->addWidget(new QLabel(desc));
            r->addLayout(col, 1);
            v->addLayout(r);
            v->addSpacing(4);
        }
        v->addStretch(1);
        auto *root = Win7Display::pageWithSide(side, content);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addWidget(root);
        connect(m_read, &QCheckBox::toggled, this, [this](bool on) {
            KConfigGroup c(KSharedConfig::openConfig(QStringLiteral("seven-controlrc")), QStringLiteral("EaseOfAccess"));
            c.writeEntry("ReadAloud", on);
            c.sync();
            if (on) {
                readAloud();
            }
        });
        connect(m_scan, &QCheckBox::toggled, this, [this](bool on) {
            KConfigGroup c(KSharedConfig::openConfig(QStringLiteral("seven-controlrc")), QStringLiteral("EaseOfAccess"));
            c.writeEntry("Scan", on);
            c.sync();
            on ? startScan() : stopScan();
        });
        if (m_read->isChecked()) {
            QTimer::singleShot(400, this, [this] { readAloud(); });
        }
        if (m_scan->isChecked()) {
            QTimer::singleShot(400, this, [this] { startScan(); });
        }
    }

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Space && m_timer && m_current >= 0) {
            ToolLink *t = m_tools.value(m_current);
            stopScan();
            m_scan->setChecked(true);
            t->activate();
            return;
        }
        QWidget::keyPressEvent(e);
    }

private:
    void readAloud()
    {
        QStringList texts{QStringLiteral("Швидкий доступ до звичайних засобів."), QStringLiteral("Запустити екранну лупу."), QStringLiteral("Запустити екранного диктора."),
                          QStringLiteral("Запустити екранну клавіатуру."), QStringLiteral("Налаштувати високу контрастність.")};
        QProcess::startDetached(QStringLiteral("spd-say"), {QStringLiteral("-l"), QStringLiteral("uk"), texts.join(QLatin1Char(' '))});
    }

    void startScan()
    {
        setFocus();
        if (!m_timer) {
            m_timer = new QTimer(this);
            connect(m_timer, &QTimer::timeout, this, [this] {
                if (m_current >= 0) {
                    m_tools[m_current]->setHot(false);
                }
                m_current = (m_current + 1) % m_tools.size();
                m_tools[m_current]->setHot(true);
            });
        }
        m_timer->start(1800);
    }

    void stopScan()
    {
        if (m_timer) {
            m_timer->stop();
        }
        if (m_current >= 0) {
            m_tools[m_current]->setHot(false);
        }
        m_current = -1;
    }

    Nav m_nav;
    QCheckBox *m_read;
    QCheckBox *m_scan;
    QList<ToolLink *> m_tools;
    QTimer *m_timer = nullptr;
    int m_current = -1;
};

inline QStringList popupTimes()
{
    return {QStringLiteral("5 секунд"), QStringLiteral("7 секунд"), QStringLiteral("15 секунд"), QStringLiteral("30 секунд"), QStringLiteral("1 хвилина"), QStringLiteral("5 хвилин")};
}

inline QList<int> popupSeconds()
{
    return {5, 7, 15, 30, 60, 300};
}

inline void notificationTime(SettingsPage *p)
{
    p->text(QStringLiteral("Скільки часу мають відображатися діалогові вікна сповіщень Windows?"), 24);
    auto *combo = new QComboBox;
    combo->addItems(popupTimes());
    const int i = popupSeconds().indexOf(p->s.popupSeconds);
    combo->setCurrentIndex(i < 0 ? 0 : i);
    combo->setFixedWidth(170);
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(24, 0, 0, 0);
    h->addWidget(combo);
    h->addStretch(1);
    p->body->addWidget(w);
    QObject::connect(combo, &QComboBox::currentIndexChanged, p, [p](int idx) {
        p->s.popupSeconds = popupSeconds().value(idx, 5);
        p->changed();
    });
}

inline void narratorChecks(SettingsPage *p)
{
    p->check(QStringLiteral("Увімкнути екранного диктора"), &p->s.narrator, 24);
    p->text(QStringLiteral("Екранний диктор читає вголос будь-який текст на екрані. Потрібні динаміки."));
    p->deadCheck(QStringLiteral("Увімкнути звуковий опис"));
    p->text(QStringLiteral("Прослуховування опису подій у відео (якщо доступно)."));
}

inline QWidget *buildSeePage(const Nav &nav)
{
    auto *p = new SettingsPage(nav, QStringLiteral("Полегшення роботи з екраном"));
    p->section(QStringLiteral("Висока контрастність"));
    p->link(QStringLiteral("Вибір теми з високою контрастністю"), [nav] { nav.open(Win7Display::PersonalizeView); });
    p->deadCheck(QStringLiteral("Вмикати або вимикати високу контрастність натисканням лівих клавіш ALT + SHIFT + PRINT SCREEN"));
    p->section(QStringLiteral("Читання вголос тексту та описів"));
    narratorChecks(p);
    p->section(QStringLiteral("Збільшення елементів на екрані"));
    p->link(QStringLiteral("Змінення розміру тексту та значків"), [nav] { nav.open(Win7Display::DisplayView); });
    p->check(QStringLiteral("Увімкнути екранну лупу"), &p->s.magnifier, 24);
    p->text(QStringLiteral("Екранна лупа збільшує будь-яку частину екрана. Її можна переміщувати, закріплювати в одному місці або змінювати її розмір."));
    p->section(QStringLiteral("Полегшення сприйняття елементів на екрані"));
    p->link(QStringLiteral("Настроювання кольору й прозорості меж вікон"), [] { Win7Display::openWindowColor(); });
    p->link(QStringLiteral("Точне настроювання ефектів відображення"), [] { launch(QStringLiteral("systemsettings"), {QStringLiteral("kcm_colors")}); });
    p->deadCheck(QStringLiteral("Зробити прямокутник фокусу товщим"));
    auto *caret = new QWidget;
    auto *ch = new QHBoxLayout(caret);
    ch->setContentsMargins(24, 2, 0, 2);
    auto *cl = new QLabel(QStringLiteral("Товщина миготливого курсора:"));
    auto *cc = new QComboBox;
    cc->addItem(QStringLiteral("1"));
    auto *preview = new QLabel(QStringLiteral(" | "));
    preview->setStyleSheet(QStringLiteral("QLabel { border: 1px solid #a0a0a0; padding: 1px 6px; }"));
    for (QWidget *x : {static_cast<QWidget *>(cl), static_cast<QWidget *>(cc), static_cast<QWidget *>(preview)}) {
        x->setEnabled(false);
        x->setToolTip(unavailable());
    }
    ch->addWidget(cl);
    ch->addWidget(cc);
    ch->addSpacing(30);
    ch->addWidget(new QLabel(QStringLiteral("Зразок:")));
    ch->addWidget(preview);
    ch->addStretch(1);
    p->body->addWidget(caret);
    p->check(QStringLiteral("Вимкнути всі непотрібні анімації (якщо можливо)"), &p->s.noAnimations, 24);
    p->deadCheck(QStringLiteral("Видалити фонові зображення (якщо доступно)"));
    p->seeAlso({{QStringLiteral("Персоналізація вигляду та звукових ефектів"), [nav] { nav.open(Win7Display::PersonalizeView); }}});
    p->finish();
    return p;
}

inline QWidget *buildNoDisplayPage(const Nav &nav)
{
    auto *p = new SettingsPage(nav, QStringLiteral("Використання комп'ютера без дисплея"));
    p->section(QStringLiteral("Читання тексту вголос"));
    narratorChecks(p);
    p->link(QStringLiteral("Налаштування перетворення тексту на мовлення"), [nav] { nav.self({QStringLiteral("--tts")}); });
    p->section(QStringLiteral("Настроювання часових обмежень і миготливих елементів"));
    p->check(QStringLiteral("Вимкнути всі непотрібні анімації (якщо можливо)"), &p->s.noAnimations, 24);
    notificationTime(p);
    p->seeAlso({{QStringLiteral("Звукові пристрої та звукові теми"), [nav] { nav.self({QStringLiteral("--sound")}); }}});
    p->finish();
    return p;
}

inline QWidget *buildNoInputPage(const Nav &nav, int speechView)
{
    auto *p = new SettingsPage(nav, QStringLiteral("Використання комп'ютера без миші або клавіатури"));
    p->section(QStringLiteral("Введення за допомогою вказівного пристрою"));
    p->check(QStringLiteral("Використовувати екранну клавіатуру"), &p->s.osk, 24);
    p->text(QStringLiteral("Введення за допомогою миші або іншого вказівного пристрою (наприклад, джойстика) шляхом вибору клавіш на зображенні клавіатури."));
    p->section(QStringLiteral("Робота без миші та клавіатури"));
    p->text(QStringLiteral("Керування комп'ютером, відкриття програм і диктування тексту за допомогою голосу та мікрофона."), 24);
    p->link(QStringLiteral("Використання розпізнавання мовлення"), [nav, speechView] { nav.open(speechView); });
    p->seeAlso({});
    p->finish();
    return p;
}

/** radio button + cursor picture of the "Mouse pointers" grid */
inline QWidget *pointerChoice(QButtonGroup *group, const QString &label, const QString &theme, const QString &preview, bool enabled)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(4, 4, 4, 4);
    h->setSpacing(10);
    auto *r = new QRadioButton(label);
    r->setProperty("theme", theme);
    r->setFixedWidth(130);
    if (!enabled) {
        r->setProperty("unavailable", true);
    }
    group->addButton(r);
    h->addWidget(r, 0, Qt::AlignTop);
    auto *pic = new QLabel;
    pic->setFixedSize(46, 56);
    pic->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    pic->setContentsMargins(0, 4, 0, 0);
    pic->setStyleSheet(QStringLiteral("QLabel { background: #f2f2f2; }"));
    QPixmap pm(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/seven-control/cursor-previews/") + preview + QStringLiteral(".png"));
    pic->setPixmap(pm);
    h->addWidget(pic);
    h->addStretch(1);
    return w;
}

inline void mouseKeysBlock(SettingsPage *p, const Nav &nav)
{
    p->section(QStringLiteral("Керування мишею за допомогою клавіатури"));
    p->check(QStringLiteral("Увімкнути керування вказівником із клавіатури"), &p->s.mouseKeys, 24);
    p->text(QStringLiteral("Використання цифрової клавіатури для переміщення покажчика миші по екрану."));
    p->link(QStringLiteral("Налаштування керування вказівником із клавіатури"), [nav] { nav.open(MouseKeysView); }, 48);
}

inline QWidget *buildMousePage(const Nav &nav)
{
    auto *p = new SettingsPage(nav, QStringLiteral("Полегшення роботи з мишею"));
    p->section(QStringLiteral("Покажчики миші"));
    p->text(QStringLiteral("Змінення кольору й розміру покажчиків миші."), 24);
    auto *box = new QFrame;
    box->setObjectName(QStringLiteral("win7ptrbox"));
    box->setStyleSheet(QStringLiteral("#win7ptrbox { border: 1px solid #000000; background: #ffffff; }"));
    box->setFixedWidth(720);
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(8, 8, 8, 8);
    auto *group = new QButtonGroup(box);
    const QStringList sizes{QStringLiteral("Звичайний"), QStringLiteral("Великий"), QStringLiteral("Дуже великий")};
    const QStringList colors{QStringLiteral("white"), QStringLiteral("black"), QStringLiteral("invert")};
    const QStringList names{QStringLiteral("білий"), QStringLiteral("чорний"), QStringLiteral("інвертований")};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const QString theme = c < 2 ? QStringLiteral("seven-cursors-%1-%2").arg(colors[c]).arg(r + 1) : QString();
            grid->addWidget(pointerChoice(group, sizes[r] + QLatin1Char('\n') + names[c], theme, QStringLiteral("%1-%2").arg(colors[c]).arg(r + 1), c < 2), r, c);
        }
    }
    for (QAbstractButton *b : group->buttons()) {
        if (b->property("theme").toString() == p->s.cursorTheme) {
            b->setChecked(true);
        }
    }
    QObject::connect(group, &QButtonGroup::buttonClicked, p, [p, group](QAbstractButton *b) {
        if (b->property("unavailable").toBool()) {
            // X11 cursors can't invert the screen under them
            QMessageBox::information(p->window(), QStringLiteral("Покажчики миші"),
                                     QStringLiteral("Інвертовані покажчики в цій системі недоступні. Виберіть білу або чорну схему."));
            group->setExclusive(false);
            b->setChecked(false);
            group->setExclusive(true);
            for (QAbstractButton *x : group->buttons()) {
                if (x->property("theme").toString() == p->s.cursorTheme) {
                    x->setChecked(true);
                }
            }
            return;
        }
        p->s.cursorTheme = b->property("theme").toString();
        p->changed();
    });
    auto *wrap = new QWidget;
    auto *wh = new QHBoxLayout(wrap);
    wh->setContentsMargins(24, 2, 0, 6);
    wh->addWidget(box);
    wh->addStretch(1);
    p->body->addWidget(wrap);
    mouseKeysBlock(p, nav);
    p->section(QStringLiteral("Полегшення керування вікнами"));
    p->check(QStringLiteral("Активувати вікно наведенням на нього покажчика миші"), &p->s.hoverFocus, 24);
    p->body->addSpacing(4);
    p->check(QStringLiteral("Не впорядковувати вікна автоматично під час переміщення до краю екрана"), &p->s.noSnap, 24);
    p->seeAlso({{QStringLiteral("Параметри миші"), [nav] { nav.self({QStringLiteral("--mouse")}); }}});
    p->finish();
    return p;
}

inline QSlider *wideSlider(int min, int max, int value)
{
    auto *s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    s->setValue(value);
    s->setFixedWidth(340);
    return s;
}

inline QWidget *sliderRow(const QString &left, QSlider *slider, const QString &right)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(96, 0, 0, 0);
    auto *l = new QLabel(left);
    l->setFixedWidth(50);
    h->addWidget(l);
    h->addWidget(slider);
    h->addWidget(new QLabel(right));
    h->addStretch(1);
    return w;
}

inline QWidget *buildMouseKeysPage(const Nav &nav)
{
    auto *p = new SettingsPage(nav, QStringLiteral("Налаштування керування вказівником із клавіатури"), QString());
    p->check(QStringLiteral("Увімкнути керування вказівником із клавіатури"), &p->s.mouseKeys);
    p->text(QStringLiteral("Використання цифрової клавіатури для переміщення покажчика миші по екрану."), 24);
    p->shortcutBlock(QStringLiteral("Вмикати керування вказівником із клавіатури лівими клавішами ALT + SHIFT + NUM LOCK"));
    p->section(QStringLiteral("Швидкість покажчика"));
    p->text(QStringLiteral("Максимальна швидкість:"), 24);
    // MKMaxSpeed: pixels per step at full speed; MKTimeToMax: steps until full speed
    auto *top = wideSlider(5, 100, qBound(5, p->s.mkMaxSpeed, 100));
    p->body->addWidget(sliderRow(QStringLiteral("Низька"), top, QStringLiteral("Висока")));
    p->text(QStringLiteral("Прискорення:"), 24);
    auto *acc = wideSlider(0, 95, 100 - qBound(5, p->s.mkTimeToMax, 100));
    p->body->addWidget(sliderRow(QStringLiteral("Повільно"), acc, QStringLiteral("Швидко")));
    QObject::connect(top, &QSlider::valueChanged, p, [p](int v) {
        p->s.mkMaxSpeed = v;
        p->changed();
    });
    QObject::connect(acc, &QSlider::valueChanged, p, [p](int v) {
        p->s.mkTimeToMax = 100 - v;
        p->changed();
    });
    p->deadCheck(QStringLiteral("Утримуйте клавішу CTRL для прискорення та клавішу SHIFT для сповільнення"));
    p->section(QStringLiteral("Інші параметри"));
    p->text(QStringLiteral("Використовувати керування вказівником із клавіатури, коли клавіша NUM LOCK:"), 24);
    auto *on = new QRadioButton(QStringLiteral("Увімк."));
    auto *off = new QRadioButton(QStringLiteral("Вимк."));
    on->setChecked(true);
    for (QRadioButton *r : {on, off}) {
        r->setEnabled(false);
        r->setToolTip(unavailable());
        auto *w = new QWidget;
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(48, 0, 0, 0);
        h->addWidget(r);
        h->addStretch(1);
        p->body->addWidget(w);
    }
    p->deadCheck(QStringLiteral("Відображати значок керування вказівником із клавіатури на панелі завдань"));
    p->finish();
    return p;
}

inline void stickyBlock(SettingsPage *p, const Nav &nav)
{
    p->check(QStringLiteral("Увімкнути залипання клавіш"), &p->s.sticky, 24);
    p->text(QStringLiteral("Натискання сполучень клавіш (наприклад, CTRL+ALT+DEL) по одній клавіші."));
    p->link(QStringLiteral("Налаштування залипання клавіш"), [nav] { nav.open(StickyView); }, 48);
}

inline void filterBlock(SettingsPage *p, const Nav &nav)
{
    auto *box = p->check(QStringLiteral("Увімкнути фільтрацію введення"), &p->s.slow, 24);
    // "Filter Keys" is Slow Keys or Bounce Keys, whichever was chosen on its page
    p->s.slow = p->s.slow || p->s.bounce;
    box->setChecked(p->s.slow);
    QObject::disconnect(box, nullptr, p, nullptr);
    QObject::connect(box, &QCheckBox::toggled, p, [p](bool on) {
        if (p->s.filterRepeat) {
            p->s.slow = on;
            p->s.bounce = false;
        } else {
            p->s.bounce = on;
            p->s.slow = false;
        }
        p->changed();
    });
    p->text(QStringLiteral("Пропуск або сповільнення коротких чи повторних натискань клавіш і настроювання частоти повторення."));
    p->link(QStringLiteral("Налаштування фільтрації введення"), [nav] { nav.open(FilterView); }, 48);
}

inline QWidget *buildKeyboardPage(const Nav &nav)
{
    auto *p = new SettingsPage(nav, QStringLiteral("Полегшення роботи з клавіатурою"));
    mouseKeysBlock(p, nav);
    p->section(QStringLiteral("Полегшення введення тексту"));
    stickyBlock(p, nav);
    p->body->addSpacing(6);
    p->check(QStringLiteral("Увімкнути озвучування перемикання"), &p->s.toggleBeep, 24);
    p->text(QStringLiteral("Відтворення звукового сигналу під час натискання клавіш CAPS LOCK, NUM LOCK або SCROLL LOCK."));
    p->deadCheck(QStringLiteral("Вмикати озвучування перемикання, утримуючи клавішу NUM LOCK протягом 5 секунд"), 48);
    p->body->addSpacing(6);
    filterBlock(p, nav);
    p->section(QStringLiteral("Полегшення використання сполучень клавіш"));
    p->deadCheck(QStringLiteral("Підкреслювати сполучення клавіш і клавіші доступу"));
    p->section(QStringLiteral("Полегшення керування вікнами"));
    p->check(QStringLiteral("Не впорядковувати вікна автоматично під час переміщення до краю екрана"), &p->s.noSnap, 24);
    p->seeAlso({{QStringLiteral("Додавання розкладки Дворака та змінення інших параметрів введення з клавіатури"), [nav] { nav.self({QStringLiteral("--text-services")}); }},
                {QStringLiteral("Параметри клавіатури"), [nav] { nav.self({QStringLiteral("--keyboard")}); }}});
    p->finish();
    return p;
}

inline QWidget *buildStickyPage(const Nav &nav)
{
    auto *p = new SettingsPage(nav, QStringLiteral("Налаштування залипання клавіш"), QString());
    p->check(QStringLiteral("Увімкнути залипання клавіш"), &p->s.sticky);
    p->text(QStringLiteral("Натискання сполучень клавіш (наприклад, CTRL+ALT+DEL) по одній клавіші."), 24);
    p->shortcutBlock(QStringLiteral("Вмикати залипання клавіш п'ятикратним натисканням клавіші SHIFT"));
    p->section(QStringLiteral("Параметри"));
    p->check(QStringLiteral("Фіксувати клавіші-модифікатори подвійним натисканням"), &p->s.stickyLatch, 24);
    p->check(QStringLiteral("Вимикати залипання клавіш одночасним натисканням двох клавіш"), &p->s.stickyAutoOff, 24);
    p->section(QStringLiteral("Зворотний зв'язок"));
    p->check(QStringLiteral("Подавати звуковий сигнал під час натискання клавіш-модифікаторів"), &p->s.stickyBeep, 24);
    p->deadCheck(QStringLiteral("Відображати значок залипання клавіш на панелі завдань"));
    p->finish();
    return p;
}

inline QWidget *buildFilterPage(const Nav &nav)
{
    auto *p = new SettingsPage(nav, QStringLiteral("Налаштування фільтрації введення"), QString());
    auto *on = new QCheckBox(QStringLiteral("Увімкнути фільтрацію введення"));
    on->setChecked(p->s.slow || p->s.bounce);
    p->body->addWidget(on);
    p->text(QStringLiteral("Пропуск або сповільнення коротких чи повторних натискань клавіш і настроювання частоти повторення."), 24);
    p->shortcutBlock(QStringLiteral("Вмикати фільтрацію введення, утримуючи праву клавішу SHIFT протягом 8 секунд"));
    p->section(QStringLiteral("Параметри фільтрації"));
    auto *bounce = new QRadioButton(QStringLiteral("Увімкнути пропуск повторних натискань"));
    auto *repeat = new QRadioButton(QStringLiteral("Увімкнути повторне і повільне введення"));
    (p->s.filterRepeat ? repeat : bounce)->setChecked(true);
    auto addRadio = [p](QRadioButton *r) {
        auto *w = new QWidget;
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(24, 0, 0, 0);
        h->addWidget(r);
        h->addStretch(1);
        p->body->addWidget(w);
    };
    addRadio(bounce);
    p->text(QStringLiteral("Якщо пальці випадково кілька разів поспіль торкаються клавіші, повторні натискання протягом указаного часу пропускатимуться."));
    const QList<int> delays{500, 700, 1000, 1500, 2000};
    auto delayCombo = [&delays](int current) {
        auto *c = new QComboBox;
        for (int d : delays) {
            c->addItem(QStringLiteral("%1 с").arg(QLocale(QLocale::Ukrainian).toString(d / 1000.0, 'f', 1)), d);
        }
        int best = 0;
        for (int i = 0; i < delays.size(); ++i) {
            if (qAbs(delays[i] - current) < qAbs(delays[best] - current)) {
                best = i;
            }
        }
        c->setCurrentIndex(best);
        c->setFixedWidth(200);
        return c;
    };
    auto *bounceDelay = delayCombo(p->s.bounceDelay);
    auto *br = new QWidget;
    auto *brh = new QHBoxLayout(br);
    brh->setContentsMargins(48, 0, 0, 0);
    brh->addWidget(new QLabel(QStringLiteral("Скільки часу комп'ютер має чекати, перш ніж прийняти натискання?")));
    brh->addWidget(bounceDelay);
    brh->addStretch(1);
    p->body->addWidget(br);
    addRadio(repeat);
    p->text(QStringLiteral("Комп'ютер пропускатиме короткі натискання клавіш: щоб натискання було прийнято, клавішу слід утримувати вказаний час."));
    auto *slowDelay = delayCombo(p->s.slowDelay);
    auto *sr = new QWidget;
    auto *srh = new QHBoxLayout(sr);
    srh->setContentsMargins(48, 0, 0, 0);
    srh->addWidget(new QLabel(QStringLiteral("Скільки часу слід утримувати клавішу, щоб натискання було прийнято?")));
    srh->addWidget(slowDelay);
    srh->addStretch(1);
    p->body->addWidget(sr);
    p->link(QStringLiteral("Налаштування частоти повторення клавіш"), [nav] { nav.self({QStringLiteral("--keyboard")}); }, 48);
    auto *test = new QLineEdit;
    test->setPlaceholderText(QStringLiteral("Введіть тут текст, щоб перевірити параметри"));
    test->setFixedWidth(420);
    auto *tw = new QWidget;
    auto *th = new QHBoxLayout(tw);
    th->setContentsMargins(24, 6, 0, 0);
    th->addWidget(test);
    th->addStretch(1);
    p->body->addWidget(tw);
    p->section(QStringLiteral("Інші параметри"));
    p->check(QStringLiteral("Подавати звуковий сигнал під час натискання або прийняття клавіш"), &p->s.slowBeep, 24);
    p->deadCheck(QStringLiteral("Відображати значок фільтрації введення на панелі завдань"));
    auto sync = [p, on, repeat] {
        p->s.filterRepeat = repeat->isChecked();
        p->s.slow = on->isChecked() && p->s.filterRepeat;
        p->s.bounce = on->isChecked() && !p->s.filterRepeat;
        p->changed();
    };
    QObject::connect(on, &QCheckBox::toggled, p, sync);
    QObject::connect(repeat, &QRadioButton::toggled, p, sync);
    QObject::connect(bounceDelay, &QComboBox::currentIndexChanged, p, [p, bounceDelay] {
        p->s.bounceDelay = bounceDelay->currentData().toInt();
        p->changed();
    });
    QObject::connect(slowDelay, &QComboBox::currentIndexChanged, p, [p, slowDelay] {
        p->s.slowDelay = slowDelay->currentData().toInt();
        p->changed();
    });
    p->finish();
    return p;
}

inline QWidget *buildSoundsPage(const Nav &nav)
{
    auto *p = new SettingsPage(nav, QStringLiteral("Використання текстових або візуальних альтернатив для звуків"));
    p->section(QStringLiteral("Використання візуальних підказок замість звуків"));
    p->check(QStringLiteral("Увімкнути візуальне сповіщення про звуки (Sound Sentry)"), &p->s.visibleBell, 24);
    p->text(QStringLiteral("Виберіть візуальне попередження"), 48);
    auto *group = new QButtonGroup(p);
    const QStringList options{QStringLiteral("Немає"), QStringLiteral("Блимання активного заголовка"), QStringLiteral("Блимання активного вікна"), QStringLiteral("Блимання робочого стола")};
    for (int i = 0; i < options.size(); ++i) {
        auto *r = new QRadioButton(options[i]);
        group->addButton(r, i);
        auto *w = new QWidget;
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(64, 0, 0, 0);
        h->addWidget(r);
        h->addStretch(1);
        p->body->addWidget(w);
    }
    // KDE flashes the screen: in colour (caption / window) or inverted (whole desktop)
    group->button(!p->s.visibleBell ? 0 : p->s.visibleBellInvert ? 3 : 2)->setChecked(true);
    QObject::connect(group, &QButtonGroup::idClicked, p, [p](int id) {
        p->s.visibleBell = id != 0;
        p->s.visibleBellInvert = id == 3;
        p->changed();
    });
    p->section(QStringLiteral("Увімкнення текстових субтитрів для мовлення"));
    p->deadCheck(QStringLiteral("Увімкнути текстові субтитри для мовлення (якщо доступно)"));
    p->text(QStringLiteral("Відображення текстових субтитрів замість звуків для зазначення дії, яка відбувається на комп'ютері (наприклад, початок або завершення друку документа)."));
    p->seeAlso({{QStringLiteral("Звукові пристрої та звукові теми"), [nav] { nav.self({QStringLiteral("--sound")}); }}});
    p->finish();
    return p;
}

inline QWidget *buildFocusPage(const Nav &nav)
{
    auto *p = new SettingsPage(nav, QStringLiteral("Полегшення зосередження на завданнях"));
    p->section(QStringLiteral("Читання вголос тексту та описів"));
    narratorChecks(p);
    p->section(QStringLiteral("Видалення відволікаючих анімацій"));
    p->check(QStringLiteral("Вимкнути всі непотрібні анімації (якщо можливо)"), &p->s.noAnimations, 24);
    p->deadCheck(QStringLiteral("Видалити фонові зображення (якщо доступно)"));
    p->section(QStringLiteral("Полегшення введення тексту"));
    stickyBlock(p, nav);
    p->body->addSpacing(6);
    p->check(QStringLiteral("Увімкнути озвучування перемикання"), &p->s.toggleBeep, 24);
    p->text(QStringLiteral("Відтворення звукового сигналу під час натискання клавіш CAPS LOCK, NUM LOCK або SCROLL LOCK."));
    p->body->addSpacing(6);
    filterBlock(p, nav);
    p->section(QStringLiteral("Настроювання часових обмежень і миготливих елементів"));
    notificationTime(p);
    p->seeAlso({});
    p->finish();
    return p;
}

/* ---------------- "Get recommendations to make your computer easier to use" ---------------- */

inline QStringList &answers()
{
    static QStringList a;
    return a;
}

inline QWidget *buildQuestionsPage(const Nav &nav)
{
    auto *p = new SettingsPage(nav, QStringLiteral("Отримання рекомендацій щодо полегшення роботи з комп'ютером"),
                               QStringLiteral("Позначте всі твердження, які стосуються вас. Windows порекомендує параметри, що можуть полегшити роботу з комп'ютером."));
    struct Q {
        QString group;
        QStringList items;
    };
    const QList<Q> qs{
        {QStringLiteral("Зір"),
         {QStringLiteral("eyes|Мені важко розпізнавати обличчя або текст на телевізорі"), QStringLiteral("light|Мені важко розрізняти зображення через освітлення"),
          QStringLiteral("blind|Я незрячий"), QStringLiteral("other-eyes|Я маю інші проблеми із зором")}},
        {QStringLiteral("Рухливість"),
         {QStringLiteral("pen|Мені важко користуватися ручкою або олівцем"), QStringLiteral("nokeyboard|Я не використовую клавіатуру"),
          QStringLiteral("nomouse|Я не використовую мишу"), QStringLiteral("tremor|Я маю інші проблеми з рухливістю")}},
        {QStringLiteral("Слух"), {QStringLiteral("hear|Мені важко чути розмову, навіть коли поруч тихо"), QStringLiteral("deaf|Я глухий")}},
        {QStringLiteral("Мовлення"), {QStringLiteral("speech|Я маю порушення мовлення")}},
        {QStringLiteral("Мислення та сприйняття"),
         {QStringLiteral("focus|Мені важко зосередитися та запам'ятовувати"), QStringLiteral("read|Мені важко читати текст")}},
    };
    QList<QCheckBox *> boxes;
    for (const Q &q : qs) {
        p->section(q.group);
        for (const QString &it : q.items) {
            auto *c = new QCheckBox(it.section(QLatin1Char('|'), 1));
            c->setProperty("key", it.section(QLatin1Char('|'), 0, 0));
            c->setChecked(answers().contains(c->property("key").toString()));
            auto *w = new QWidget;
            auto *h = new QHBoxLayout(w);
            h->setContentsMargins(24, 0, 0, 0);
            h->addWidget(c);
            h->addStretch(1);
            p->body->addWidget(w);
            boxes.append(c);
        }
    }
    p->body->addSpacing(14);
    auto *done = new QPushButton(QStringLiteral("Готово"));
    done->setFixedWidth(100);
    p->body->addWidget(done);
    QObject::connect(done, &QPushButton::clicked, p, [nav, boxes] {
        answers().clear();
        for (QCheckBox *c : boxes) {
            if (c->isChecked()) {
                answers().append(c->property("key").toString());
            }
        }
        nav.open(ResultsView);
    });
    p->finish();
    return p;
}

inline QWidget *buildResultsPage(const Nav &nav)
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("win7page"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(150, 20, 60, 20);
    v->setSpacing(8);
    v->addWidget(heading(QStringLiteral("Рекомендовані параметри")));
    v->addWidget(plainText(QStringLiteral("Виходячи з ваших відповідей, рекомендуємо переглянути ці параметри.")));
    v->addSpacing(8);
    const QStringList a = answers();
    auto has = [&a](std::initializer_list<const char *> keys) {
        for (const char *k : keys) {
            if (a.contains(QLatin1String(k))) {
                return true;
            }
        }
        return false;
    };
    QList<std::tuple<QString, QString, int>> rec;
    if (has({"blind"})) {
        rec.append({QStringLiteral("seven-eoa-nodisplay"), QStringLiteral("Використання комп'ютера без дисплея"), NoDisplayView});
    }
    if (has({"eyes", "light", "other-eyes", "read"})) {
        rec.append({QStringLiteral("seven-eoa-see"), QStringLiteral("Полегшення роботи з екраном"), SeeView});
    }
    if (has({"nokeyboard", "nomouse", "pen"})) {
        rec.append({QStringLiteral("seven-eoa-noinput"), QStringLiteral("Використання комп'ютера без миші або клавіатури"), NoInputView});
    }
    if (has({"pen", "tremor", "nomouse"})) {
        rec.append({QStringLiteral("seven-eoa-mouse"), QStringLiteral("Полегшення роботи з мишею"), MouseView});
    }
    if (has({"tremor", "pen", "nokeyboard"})) {
        rec.append({QStringLiteral("seven-eoa-keyboard"), QStringLiteral("Полегшення роботи з клавіатурою"), KeyboardView});
    }
    if (has({"hear", "deaf"})) {
        rec.append({QStringLiteral("seven-eoa-sounds"), QStringLiteral("Використання текстових або візуальних альтернатив для звуків"), SoundsView});
    }
    if (has({"focus", "read"})) {
        rec.append({QStringLiteral("seven-eoa-focus"), QStringLiteral("Полегшення зосередження на завданнях"), FocusView});
    }
    if (rec.isEmpty()) {
        v->addWidget(plainText(QStringLiteral("Рекомендацій немає. Усі параметри можна переглянути в Центрі спеціальних можливостей.")));
    }
    for (const auto &[icon, title, view] : rec) {
        auto *r = new QHBoxLayout;
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(icon).pixmap(32, 32));
        r->addWidget(ic);
        const int target = view;
        auto *l = new Link(title, [nav, target] { nav.open(target); });
        l->setWordWrap(false);
        r->addWidget(l);
        r->addStretch(1);
        v->addLayout(r);
    }
    v->addSpacing(14);
    v->addWidget(new Link(QStringLiteral("Повернутися до Центру спеціальних можливостей"), [nav] { nav.open(CenterView); }));
    v->addStretch(1);
    return page;
}

inline QWidget *build(int view, const Nav &nav, int speechView)
{
    switch (view) {
    case CenterView:
        return new CenterPage(nav);
    case SeeView:
        return buildSeePage(nav);
    case NoDisplayView:
        return buildNoDisplayPage(nav);
    case NoInputView:
        return buildNoInputPage(nav, speechView);
    case MouseView:
        return buildMousePage(nav);
    case MouseKeysView:
        return buildMouseKeysPage(nav);
    case KeyboardView:
        return buildKeyboardPage(nav);
    case StickyView:
        return buildStickyPage(nav);
    case FilterView:
        return buildFilterPage(nav);
    case SoundsView:
        return buildSoundsPage(nav);
    case FocusView:
        return buildFocusPage(nav);
    case QuestionsView:
        return buildQuestionsPage(nav);
    case ResultsView:
        return buildResultsPage(nav);
    }
    return new QWidget;
}

inline QString viewTitle(int view)
{
    switch (view) {
    case CenterView:
        return QStringLiteral("Центр спеціальних можливостей");
    case SeeView:
        return QStringLiteral("Полегшення роботи з екраном");
    case NoDisplayView:
        return QStringLiteral("Використання комп'ютера без дисплея");
    case NoInputView:
        return QStringLiteral("Використання комп'ютера без миші або клавіатури");
    case MouseView:
        return QStringLiteral("Полегшення роботи з мишею");
    case MouseKeysView:
        return QStringLiteral("Налаштування керування вказівником із клавіатури");
    case KeyboardView:
        return QStringLiteral("Полегшення роботи з клавіатурою");
    case StickyView:
        return QStringLiteral("Налаштування залипання клавіш");
    case FilterView:
        return QStringLiteral("Налаштування фільтрації введення");
    case SoundsView:
        return QStringLiteral("Використання текстових або візуальних альтернатив для звуків");
    case FocusView:
        return QStringLiteral("Полегшення зосередження на завданнях");
    case QuestionsView:
        return QStringLiteral("Отримання рекомендацій");
    case ResultsView:
        return QStringLiteral("Рекомендовані параметри");
    }
    return QString();
}

/* ---------------- Magnifier ---------------- */

inline void kwinShortcut(const QString &name)
{
    QProcess::execute(QStringLiteral("qdbus6"),
                      {QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/component/kwin"), QStringLiteral("org.kde.kglobalaccel.Component.invokeShortcut"), name});
}

/** the small "Magnifier" window: − / + / 100% / Views / options / help */
class MagnifierWindow : public QWidget
{
public:
    MagnifierWindow()
    {
        setWindowTitle(QStringLiteral("Екранна лупа"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-magnify")));
        setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_DeleteOnClose);
        // KWin zooms by this factor per step; Windows 7 steps by 100 %
        KConfig kwin(QStringLiteral("kwinrc"));
        KConfigGroup z(&kwin, QStringLiteral("Effect-zoom"));
        m_factor = z.readEntry("ZoomFactor", 1.2);
        if (qAbs(m_factor - 2.0) > 0.01) {
            z.writeEntry("ZoomFactor", 2.0);
            KConfigGroup(&kwin, QStringLiteral("Plugins")).writeEntry("zoomEnabled", true);
            kwin.sync();
            QProcess::execute(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QStringLiteral("reconfigureEffect"), QStringLiteral("zoom")});
            m_factor = 2.0;
        }
        QProcess::execute(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QStringLiteral("loadEffect"), QStringLiteral("zoom")});
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(6, 4, 6, 4);
        h->setSpacing(2);
        auto button = [this, h](const QString &icon, const QString &tip) {
            auto *b = new QToolButton;
            b->setIcon(QIcon::fromTheme(icon));
            b->setIconSize(QSize(24, 24));
            b->setAutoRaise(true);
            b->setToolTip(tip);
            h->addWidget(b);
            return b;
        };
        auto *out = button(QStringLiteral("seven-magnify-out"), QStringLiteral("Зменшити (Windows+мінус)"));
        auto *in = button(QStringLiteral("seven-magnify-in"), QStringLiteral("Збільшити (Windows+плюс)"));
        m_percent = new QLabel;
        m_percent->setFixedWidth(44);
        m_percent->setAlignment(Qt::AlignCenter);
        h->addWidget(m_percent);
        auto *views = new QToolButton;
        views->setText(QStringLiteral("Подання  ▾"));
        views->setAutoRaise(true);
        views->setPopupMode(QToolButton::InstantPopup);
        views->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0px; }"));
        auto *menu = new QMenu(views);
        auto *full = menu->addAction(QStringLiteral("Повноекранний режим"));
        full->setCheckable(true);
        full->setChecked(true);
        for (const QString &t : {QStringLiteral("Збільшувальне скло"), QStringLiteral("Закріплений")}) {
            menu->addAction(t)->setEnabled(false);
        }
        views->setMenu(menu);
        h->addWidget(views);
        auto *opts = button(QStringLiteral("seven-magnify-options"), QStringLiteral("Параметри екранної лупи"));
        auto *help = button(QStringLiteral("seven-magnify-help"), QStringLiteral("Довідка"));
        opts->setIconSize(QSize(16, 16));
        help->setIconSize(QSize(16, 16));
        connect(in, &QToolButton::clicked, this, [this] { zoom(+1); });
        connect(out, &QToolButton::clicked, this, [this] { zoom(-1); });
        connect(opts, &QToolButton::clicked, this, [] { launch(QStringLiteral("systemsettings"), {QStringLiteral("kcm_access")}); });
        connect(help, &QToolButton::clicked, this, [this] {
            QMessageBox::information(this, windowTitle(),
                                     QStringLiteral("Екранна лупа збільшує частину екрана навколо покажчика миші.\n\nЗбільшити: Windows + «+» або кнопка «+».\n"
                                                    "Зменшити: Windows + «−» або кнопка «−».\nПід час закриття лупи масштаб повертається до 100 %."));
        });
        showPercent();
        QTimer::singleShot(0, this, [this] { zoom(+1); }); // Windows 7 starts at 200 %
    }

protected:
    void closeEvent(QCloseEvent *e) override
    {
        while (m_steps > 0) {
            zoom(-1);
        }
        QWidget::closeEvent(e);
    }

private:
    void zoom(int dir)
    {
        if (dir < 0 && m_steps == 0) {
            return;
        }
        kwinShortcut(dir > 0 ? QStringLiteral("view_zoom_in") : QStringLiteral("view_zoom_out"));
        m_steps += dir;
        showPercent();
    }
    void showPercent()
    {
        m_percent->setText(QStringLiteral("%1%").arg(qRound(100 * std::pow(m_factor, m_steps))));
    }
    double m_factor = 2.0;
    int m_steps = 0;
    QLabel *m_percent;
};

/* ---------------- Narrator ---------------- */

class NarratorWindow : public QDialog
{
public:
    NarratorWindow()
    {
        setWindowTitle(QStringLiteral("Екранний диктор"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-narrator")));
        setAttribute(Qt::WA_DeleteOnClose);
        setFixedSize(470, 410);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 10, 12, 12);
        auto *bar = new QMenuBar;
        auto *file = bar->addMenu(QStringLiteral("Файл"));
        file->addAction(QStringLiteral("Вихід"), this, [this] { exitNarrator(); });
        auto *prefs = bar->addMenu(QStringLiteral("Параметри"));
        prefs->addAction(QStringLiteral("Параметри голосу..."), this, [] { QProcess::startDetached(QStringLiteral("orca"), {QStringLiteral("-s")}); });
        auto *help = bar->addMenu(QStringLiteral("Довідка"));
        help->addAction(QStringLiteral("Коротка довідка"), this, [this] { quickHelp(); });
        v->setMenuBar(bar);
        v->addWidget(plainText(QStringLiteral("Екранний диктор читатиме вголос текст на екрані, коли ви переходите між елементами за допомогою клавіатури.")));
        auto *box = new QGroupBox(QStringLiteral("Основні параметри екранного диктора"));
        auto *bv = new QVBoxLayout(box);
        bv->setSpacing(10);
        KConfigGroup cfg(KSharedConfig::openConfig(QStringLiteral("seven-controlrc")), QStringLiteral("Narrator"));
        auto *echo = new QCheckBox(QStringLiteral("Озвучувати натискання клавіш користувачем"));
        auto *sys = new QCheckBox(QStringLiteral("Повідомляти про системні події"));
        auto *scroll = new QCheckBox(QStringLiteral("Повідомляти про прокручування"));
        auto *minimized = new QCheckBox(QStringLiteral("Запускати диктора згорнутим"));
        for (QCheckBox *c : {echo, sys}) {
            c->setChecked(true);
            c->setEnabled(false);
            c->setToolTip(QStringLiteral("Налаштовується в параметрах голосу (Orca)."));
        }
        scroll->setEnabled(false);
        scroll->setToolTip(unavailable());
        minimized->setChecked(cfg.readEntry("StartMinimized", false));
        for (QCheckBox *c : {echo, sys, scroll, minimized}) {
            bv->addWidget(c);
        }
        v->addWidget(box);
        auto *logon = new Link(QStringLiteral("Керування запуском диктора під час входу в систему"), [] {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--ease"), QStringLiteral("nodisplay")});
        });
        v->addWidget(logon);
        v->addStretch(1);
        auto *row = new QHBoxLayout;
        auto *qh = new QPushButton(QStringLiteral("Коротка довідка"));
        auto *voice = new QPushButton(QStringLiteral("Параметри голосу"));
        auto *exit = new QPushButton(QStringLiteral("Вихід"));
        for (QPushButton *b : {qh, voice, exit}) {
            b->setMinimumHeight(30);
            row->addWidget(b);
        }
        v->addLayout(row);
        connect(minimized, &QCheckBox::toggled, this, [](bool on) {
            KConfigGroup c(KSharedConfig::openConfig(QStringLiteral("seven-controlrc")), QStringLiteral("Narrator"));
            c.writeEntry("StartMinimized", on);
            c.sync();
        });
        connect(qh, &QPushButton::clicked, this, [this] { quickHelp(); });
        connect(voice, &QPushButton::clicked, this, [] { QProcess::startDetached(QStringLiteral("orca"), {QStringLiteral("-s")}); });
        connect(exit, &QPushButton::clicked, this, [this] { exitNarrator(); });
        if (QProcess::execute(QStringLiteral("pgrep"), {QStringLiteral("-x"), QStringLiteral("orca")}) != 0) {
            QProcess::startDetached(QStringLiteral("orca"), {QStringLiteral("--replace")});
        }
        if (minimized->isChecked()) {
            QTimer::singleShot(0, this, &QWidget::showMinimized);
        }
    }

private:
    void quickHelp()
    {
        QMessageBox::information(this, windowTitle(),
                                 QStringLiteral("Екранний диктор (Orca) читає вголос елементи, на які переходить фокус.\n\n"
                                                "Insert + пробіл — параметри диктора.\nInsert + Q — вихід із диктора.\nInsert + H — режим навчання клавіш."));
    }
    void exitNarrator()
    {
        QProcess::execute(QStringLiteral("pkill"), {QStringLiteral("-x"), QStringLiteral("orca")});
        close();
    }
};

/* ---------------- On-Screen Keyboard ---------------- */

/** XTestFakeKeyEvent from libXtst, loaded at run time (Xlib's macros clash with Qt, so no X11 headers) */
class XTest
{
public:
    XTest()
    {
        void *x11 = dlopen("libX11.so.6", RTLD_LAZY);
        void *xtst = dlopen("libXtst.so.6", RTLD_LAZY);
        if (!x11 || !xtst) {
            return;
        }
        auto open = reinterpret_cast<void *(*)(const char *)>(dlsym(x11, "XOpenDisplay"));
        m_keycode = reinterpret_cast<unsigned char (*)(void *, unsigned long)>(dlsym(x11, "XKeysymToKeycode"));
        m_flush = reinterpret_cast<int (*)(void *)>(dlsym(x11, "XFlush"));
        m_fake = reinterpret_cast<int (*)(void *, unsigned, int, unsigned long)>(dlsym(xtst, "XTestFakeKeyEvent"));
        if (open) {
            m_dpy = open(nullptr);
        }
    }
    bool ok() const
    {
        return m_dpy && m_fake && m_keycode && m_flush;
    }
    void key(unsigned long keysym, bool press)
    {
        if (!ok()) {
            return;
        }
        const unsigned char code = m_keycode(m_dpy, keysym);
        if (code) {
            m_fake(m_dpy, code, press, 0);
            m_flush(m_dpy);
        }
    }

private:
    void *m_dpy = nullptr;
    unsigned char (*m_keycode)(void *, unsigned long) = nullptr;
    int (*m_flush)(void *) = nullptr;
    int (*m_fake)(void *, unsigned, int, unsigned long) = nullptr;
};

struct OskKey {
    QString label;      // "Esc", "q" (US layout name, used for the fixed keys)
    QString shifted;    // "!" for "1"
    unsigned long sym;  // X keysym (US layout); letters follow the current layout via XKB
    double width = 1;   // in key units
    bool modifier = false;
    bool gap = false;   // unused (layout is fixed below)
};

/** current XKB layout labels: the keysym a US key gives in the active group (Cyrillic for UA / RU) */
class XkbLabels
{
public:
    XkbLabels()
    {
        void *x11 = dlopen("libX11.so.6", RTLD_LAZY);
        void *xkbc = dlopen("libxkbcommon.so.0", RTLD_LAZY);
        if (!x11) {
            return;
        }
        auto open = reinterpret_cast<void *(*)(const char *)>(dlsym(x11, "XOpenDisplay"));
        m_keycode = reinterpret_cast<unsigned char (*)(void *, unsigned long)>(dlsym(x11, "XKeysymToKeycode"));
        m_kc2ks = reinterpret_cast<unsigned long (*)(void *, unsigned char, int, int)>(dlsym(x11, "XkbKeycodeToKeysym"));
        m_state = reinterpret_cast<int (*)(void *, unsigned, void *)>(dlsym(x11, "XkbGetState"));
        if (xkbc) {
            m_utf32 = reinterpret_cast<unsigned (*)(unsigned)>(dlsym(xkbc, "xkb_keysym_to_utf32"));
        }
        if (open) {
            m_dpy = open(nullptr);
        }
    }
    int group() const
    {
        if (!m_dpy || !m_state) {
            return 0;
        }
        unsigned char st[64] = {};
        m_state(m_dpy, 0x100 /* XkbUseCoreKbd */, st);
        return st[0];
    }
    /** text the key gives in group g (level 0 or 1), empty if unknown */
    QString text(unsigned long usSym, int g, int level) const
    {
        if (!m_dpy || !m_keycode || !m_kc2ks || !m_utf32) {
            return {};
        }
        const unsigned char kc = m_keycode(m_dpy, usSym);
        if (!kc) {
            return {};
        }
        const unsigned long ks = m_kc2ks(m_dpy, kc, g, level);
        const unsigned u = ks ? m_utf32(unsigned(ks)) : 0;
        return u ? QString::fromUcs4(reinterpret_cast<const char32_t *>(&u), 1) : QString();
    }

private:
    void *m_dpy = nullptr;
    unsigned char (*m_keycode)(void *, unsigned long) = nullptr;
    unsigned long (*m_kc2ks)(void *, unsigned char, int, int) = nullptr;
    int (*m_state)(void *, unsigned, void *) = nullptr;
    unsigned (*m_utf32)(unsigned) = nullptr;
};

/** Windows 7 osk.exe, measured on the real one (1920x1080, 96 dpi): keys 38 px wide on a 42 px grid, rows 40 px
 *  apart, genuine key images (normal / hover / pressed) in ~/.local/share/seven-control/osk, labels in Segoe UI.
 *  The window scales its contents when resized, like osk.exe. */
class KeyboardWindow : public QWidget
{
public:
    static constexpr double DW = 813, DH = 199; // design size of the client area
    KeyboardWindow()
    {
        setWindowTitle(QStringLiteral("Екранна клавіатура"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-osk")));
        setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_DeleteOnClose);
        setMouseTracking(true);
        const QString dir = QDir::homePath() + QStringLiteral("/.local/share/seven-control/osk/");
        for (const char *n : {"normal", "hover", "pressed"}) {
            m_tex.insert(QString::fromLatin1(n), QPixmap(dir + QStringLiteral("key-%1.png").arg(QString::fromLatin1(n))));
        }
        for (const char *n : {"flag", "menu", "enter", "up", "down", "left", "right"}) {
            m_glyph.insert(QString::fromLatin1(n), QImage(dir + QStringLiteral("glyph-%1.png").arg(QString::fromLatin1(n))));
        }
        auto k = [](const char *l, unsigned long sym, double w = 1, const char *sh = "", bool mod = false) {
            return OskKey{QString::fromUtf8(l), QString::fromUtf8(sh), sym, w, mod, false};
        };
        // main block: 16 units per row (like osk.exe)
        m_rows = {
            {k("Esc", 0xff1b, 1.5), k("`", 0x60, 1, "~"), k("1", 0x31, 1, "!"), k("2", 0x32, 1, "@"), k("3", 0x33, 1, "#"), k("4", 0x34, 1, "$"), k("5", 0x35, 1, "%"),
             k("6", 0x36, 1, "^"), k("7", 0x37, 1, "&"), k("8", 0x38, 1, "*"), k("9", 0x39, 1, "("), k("0", 0x30, 1, ")"), k("-", 0x2d, 1, "_"), k("=", 0x3d, 1, "+"),
             k("Bksp", 0xff08, 1.5)},
            {k("Tab", 0xff09, 2), k("q", 0x71), k("w", 0x77), k("e", 0x65), k("r", 0x72), k("t", 0x74), k("y", 0x79), k("u", 0x75), k("i", 0x69), k("o", 0x6f),
             k("p", 0x70), k("[", 0x5b, 1, "{"), k("]", 0x5d, 1, "}"), k("\\", 0x5c, 1, "|"), k("Del", 0xffff)},
            {k("Caps", 0xffe5, 2.5, "", true), k("a", 0x61), k("s", 0x73), k("d", 0x64), k("f", 0x66), k("g", 0x67), k("h", 0x68), k("j", 0x6a), k("k", 0x6b),
             k("l", 0x6c), k(";", 0x3b, 1, ":"), k("'", 0x27, 1, "\""), k("", 0xff0d, 2.5)},
            {k("Shift", 0xffe1, 3, "", true), k("z", 0x7a), k("x", 0x78), k("c", 0x63), k("v", 0x76), k("b", 0x62), k("n", 0x6e), k("m", 0x6d),
             k(",", 0x2c, 1, "<"), k(".", 0x2e, 1, ">"), k("/", 0x2f, 1, "?"), k("", 0xff52), k("Shift", 0xffe2, 2, "", true)},
            {k("Ctrl", 0xffe3, 1.5, "", true), k("", 0xffeb, 1, "", true), k("Alt", 0xffe9, 1, "", true), k("", 0x20, 5), k("Alt", 0xffea, 1, "", true),
             k("", 0xff67), k("Ctrl", 0xffe4, 1.5, "", true), k("", 0xff51), k("", 0xff54), k("", 0xff53), k("Fn", 0, 1)},
        };
        // the block on the right: two 64 px keys per row, Options across both
        m_side = {{k("Home", 0xff50), k("PgUp", 0xff55)}, {k("End", 0xff57), k("PgDn", 0xff56)}, {k("Insert", 0xff63), k("Pause", 0xff13)},
                  {k("PrtScn", 0xff61), k("ScrLk", 0xff14)}, {k("Options", 0), k("Help", 0)}};   // the logon-screen osk has one wide Options instead
        layoutKeys();
        resize(int(DW), int(DH));
        m_labelTimer.setInterval(400);
        connect(&m_labelTimer, &QTimer::timeout, this, [this] {
            const int g = m_xkb.group();
            if (g != m_group) {
                m_group = g;
                update();
            }
        });
        m_group = m_xkb.group();
        m_labelTimer.start();
        if (!m_x.ok()) {
            QTimer::singleShot(0, this, [this] {
                QMessageBox::warning(this, windowTitle(), QStringLiteral("Не вдалося підключитися до X11 (XTest). Екранна клавіатура не зможе вводити текст."));
            });
        }
    }

protected:
    struct Placed {
        QRectF r;
        OskKey key;
    };
    void layoutKeys()
    {
        static const double top[5] = {0, 40, 80, 120, 159}, high[5] = {36, 36, 36, 36, 35};
        const double x0 = 2, y0 = 3;
        m_keys.clear();
        for (int r = 0; r < m_rows.size(); ++r) {
            double u = 0;
            for (const OskKey &key : m_rows[r]) {
                m_keys.append({QRectF(x0 + u * 42, y0 + top[r], key.width * 42 - 4, high[r]), key});
                u += key.width;
            }
            const double sx = x0 + 676;
            if (m_side[r].size() == 2) {
                m_keys.append({QRectF(sx, y0 + top[r], 64, high[r]), m_side[r][0]});
                m_keys.append({QRectF(sx + 68, y0 + top[r], 64, high[r]), m_side[r][1]});
            } else {
                m_keys.append({QRectF(sx, y0 + top[r], 132, high[r]), m_side[r][0]});
            }
        }
    }
    QTransform toDesign() const
    {
        const double s = qMin(width() / DW, height() / DH);
        QTransform t;
        t.translate((width() - DW * s) / 2, (height() - DH * s) / 2);
        t.scale(s, s);
        return t;
    }
    void drawKey(QPainter &p, const QRectF &r, const QPixmap &tex)
    {
        // 9-slice of the genuine 38x35 key image: 6 px corners
        const int m = 6, tw = tex.width(), th = tex.height();
        const double x[4] = {r.left(), r.left() + m, r.right() - m, r.right()};
        const double y[4] = {r.top(), r.top() + m, r.bottom() - m, r.bottom()};
        const int sx[4] = {0, m, tw - m, tw}, sy[4] = {0, m, th - m, th};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                p.drawPixmap(QRectF(x[i], y[j], x[i + 1] - x[i], y[j + 1] - y[j]), tex, QRectF(sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]));
            }
        }
    }
    void drawGlyph(QPainter &p, const QString &name, const QPointF &at, const QColor &c)
    {
        QImage g = m_glyph.value(name);
        if (g.isNull()) {
            return;
        }
        QImage tinted(g.size(), QImage::Format_ARGB32_Premultiplied);
        tinted.fill(c);
        QPainter tp(&tinted);
        tp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        tp.drawImage(0, 0, g);
        tp.end();
        p.drawImage(at, tinted);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(185, 209, 234)); // osk.exe client = the window frame colour
        p.setTransform(toDesign());
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::TextAntialiasing);
        const bool shift = m_latched.contains(0xffe1) || m_latched.contains(0xffe2);
        QFont name(QStringLiteral("Segoe UI"));
        name.setPixelSize(18);
        name.setHintingPreference(QFont::PreferFullHinting); // osk.exe text is hinted (ClearType), crisper and fuller
        QFont big(name), small(name);
        big.setPixelSize(22);
        small.setPixelSize(16);
        for (int i = 0; i < m_keys.size(); ++i) {
            const auto &[r, key] = m_keys[i];
            const bool down = m_pressed == i || (key.modifier && (m_latched.contains(key.sym) || (key.sym == 0xffe5 && m_caps)));
            drawKey(p, r, m_tex.value(down ? QStringLiteral("pressed") : (m_hover == i ? QStringLiteral("hover") : QStringLiteral("normal"))));
            const QColor ink = down ? QColor(0, 0, 0) : QColor(255, 255, 255);
            p.setPen(ink);
            switch (key.sym) {
            case 0xffeb: drawGlyph(p, QStringLiteral("flag"), r.topLeft() + QPointF(3, 2), ink); continue;
            case 0xff67: drawGlyph(p, QStringLiteral("menu"), r.topLeft() + QPointF(3, 2), ink); continue;
            case 0xff0d: drawGlyph(p, QStringLiteral("enter"), r.topLeft() + QPointF(3, 1), ink); continue;
            case 0xff52: drawGlyph(p, QStringLiteral("up"), r.topLeft() + QPointF(3, 1), ink); continue;
            case 0xff51: drawGlyph(p, QStringLiteral("left"), r.topLeft() + QPointF(3, 2), ink); continue;
            case 0xff54: drawGlyph(p, QStringLiteral("down"), r.topLeft() + QPointF(3, 2), ink); continue;
            case 0xff53: drawGlyph(p, QStringLiteral("right"), r.topLeft() + QPointF(3, 2), ink); continue;
            case 0x20: continue;
            default: break;
            }
            // characters of the active layout (Cyrillic on UA / RU), US names for the fixed keys
            const bool printable = key.label.size() == 1;
            QString base = key.label, top = key.shifted;
            if (printable) {
                const QString l0 = m_xkb.text(key.sym, m_group, 0), l1 = m_xkb.text(key.sym, m_group, 1);
                if (!l0.isEmpty()) {
                    base = l0;
                    top = (l1.isEmpty() || l1 == l0.toUpper()) ? QString() : l1;
                }
            }
            const bool letter = printable && base.at(0).isLetter() && top.isEmpty();
            if (letter) {
                p.setFont(big);
                const bool upper = shift != m_caps;
                p.drawText(QPointF(r.left() + 5, r.top() + 20), upper ? base.toUpper() : base);
            } else if (printable) {
                // osk.exe: shifted character small at the top left, the plain one larger at the bottom right
                p.setFont(small);
                p.drawText(QPointF(r.left() + 5, r.top() + 15), top);
                p.setFont(big);
                p.drawText(QPointF(r.left() + 19, r.top() + 31), base);
            } else {
                QFont f = name;
                while (f.pixelSize() > 10 && QFontMetricsF(f).horizontalAdvance(key.label) > r.width() - 8) {
                    f.setPixelSize(f.pixelSize() - 1);
                }
                p.setFont(f);
                p.drawText(QPointF(r.left() + 4, r.top() + 19), key.label);
            }
        }
    }
    int keyAt(const QPointF &pos) const
    {
        const QPointF d = toDesign().inverted().map(pos);
        for (int i = 0; i < m_keys.size(); ++i) {
            if (m_keys[i].r.contains(d)) {
                return i;
            }
        }
        return -1;
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = keyAt(e->position());
        if (h != m_hover) {
            m_hover = h;
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        m_pressed = keyAt(e->position());
        if (m_pressed < 0) {
            return;
        }
        const OskKey &key = m_keys[m_pressed].key;
        if (key.sym == 0) {
            if (key.label == QLatin1String("Options") || key.label == QLatin1String("Help")) {
                QMessageBox::information(this, windowTitle(),
                                         QStringLiteral("Клацайте клавіші мишею, щоб вводити текст у вікно, яке зараз активне.\n\n"
                                                        "Shift, Ctrl, Alt і клавіша Windows залишаються натиснутими до наступної клавіші."));
            }
            update();
            return;
        }
        if (key.sym == 0xffe5) { // Caps Lock toggles
            m_caps = !m_caps;
            m_x.key(key.sym, true);
            m_x.key(key.sym, false);
        } else if (key.modifier) {
            if (m_latched.contains(key.sym)) {
                m_latched.removeAll(key.sym);
            } else {
                m_latched.append(key.sym);
            }
        } else {
            for (unsigned long m : std::as_const(m_latched)) {
                m_x.key(m, true);
            }
            m_x.key(key.sym, true);
        }
        update();
    }
    void mouseReleaseEvent(QMouseEvent *) override
    {
        if (m_pressed >= 0) {
            const OskKey &key = m_keys[m_pressed].key;
            if (key.sym && !key.modifier) {
                m_x.key(key.sym, false);
                for (unsigned long m : std::as_const(m_latched)) {
                    m_x.key(m, false);
                }
                m_latched.clear(); // like Windows: a modifier works for the next key only
            }
        }
        m_pressed = -1;
        update();
    }

private:
    QList<QList<OskKey>> m_rows, m_side;
    QList<Placed> m_keys;
    QHash<QString, QPixmap> m_tex;
    QHash<QString, QImage> m_glyph;
    QList<unsigned long> m_latched;
    bool m_caps = false;
    int m_hover = -1;
    int m_pressed = -1;
    int m_group = 0;
    QTimer m_labelTimer;
    XkbLabels m_xkb;
    XTest m_x;
};

/* ---------------- Keyboard Properties ---------------- */

class KeyboardDialog : public QDialog
{
public:
    explicit KeyboardDialog(int tab = 0)
    {
        setWindowTitle(QStringLiteral("Властивості: Клавіатура"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cpi-keyboard")));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_DeleteOnClose);
        setFixedSize(420, 470);
        KConfig input(QStringLiteral("kcminputrc"));
        const KConfigGroup kb(&input, QStringLiteral("Keyboard"));
        m_delay = kb.readEntry("RepeatDelay", 600);
        m_rate = kb.readEntry("RepeatRate", 25.0);
        KConfig globals(QStringLiteral("kdeglobals"));
        m_blink = KConfigGroup(&globals, QStringLiteral("KDE")).readEntry("CursorBlinkRate", 1000);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 9, 7, 9);
        auto *tabs = new QTabWidget;
        tabs->addTab(speedTab(), QStringLiteral("Швидкість"));
        tabs->addTab(hardwareTab(), QStringLiteral("Устаткування"));
        tabs->setCurrentIndex(tab);
        v->addWidget(tabs, 1);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        m_apply = new QPushButton(QStringLiteral("Застосувати"));
        for (QPushButton *b : {ok, cancel, m_apply}) {
            b->setMinimumWidth(75);
            row->addWidget(b);
        }
        ok->setDefault(true);
        m_apply->setEnabled(false);
        v->addLayout(row);
        connect(ok, &QPushButton::clicked, this, [this] {
            apply();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
    }

private:
    static QSlider *ticks(int max)
    {
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(0, max);
        s->setPageStep(1);
        s->setTickPosition(QSlider::TicksBelow);
        s->setTickInterval(1);
        s->setFixedWidth(200);
        return s;
    }

    static QLabel *icon(const QString &name)
    {
        auto *l = new QLabel;
        l->setPixmap(QIcon::fromTheme(name).pixmap(32, 32));
        l->setFixedSize(40, 34);
        l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        return l;
    }

    QWidget *speedTab()
    {
        auto *w = new QWidget;
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(10, 12, 10, 10);
        v->setSpacing(10);
        auto *rep = new QGroupBox(QStringLiteral("Повторення символів"));
        auto *g = new QGridLayout(rep);
        g->setVerticalSpacing(4);
        g->addWidget(icon(QStringLiteral("seven-kbd-delay")), 0, 0, 2, 1);
        g->addWidget(new QLabel(QStringLiteral("Затримка перед початком повторення:")), 0, 1, 1, 3);
        g->addWidget(new QLabel(QStringLiteral("Довше")), 1, 1);
        // Windows: 4 steps, 1 s ... 250 ms
        auto *delay = ticks(3);
        delay->setValue(qBound(0, 3 - qRound((m_delay - 250) / 250.0), 3));
        g->addWidget(delay, 1, 2);
        g->addWidget(new QLabel(QStringLiteral("Коротше")), 1, 3);
        g->addWidget(icon(QStringLiteral("seven-kbd-rate")), 2, 0, 2, 1);
        g->addWidget(new QLabel(QStringLiteral("Швидкість повторення:")), 2, 1, 1, 3);
        g->addWidget(new QLabel(QStringLiteral("Нижче")), 3, 1);
        // Windows: 32 steps, about 2.5 ... 30 characters per second
        auto *rate = ticks(31);
        rate->setTickInterval(4);
        rate->setValue(qBound(0, qRound((m_rate - 2.5) / 27.5 * 31), 31));
        g->addWidget(rate, 3, 2);
        g->addWidget(new QLabel(QStringLiteral("Вище")), 3, 3);
        g->addWidget(new QLabel(QStringLiteral("Клацніть тут і утримуйте клавішу для перевірки:")), 4, 1, 1, 3);
        auto *test = new QLineEdit;
        g->addWidget(test, 5, 1, 1, 3);
        g->setColumnStretch(3, 1);
        v->addWidget(rep);
        auto *blink = new QGroupBox(QStringLiteral("Частота мерехтіння курсора"));
        auto *bg = new QGridLayout(blink);
        m_caret = new QLabel(QStringLiteral("|"));
        m_caret->setFixedWidth(40);
        bg->addWidget(m_caret, 0, 0);
        bg->addWidget(new QLabel(QStringLiteral("Немає")), 0, 1);
        auto *speed = ticks(10);
        // 0 = no blinking, 10 = 200 ms
        speed->setValue(m_blink <= 0 ? 0 : qBound(1, 10 - qRound((m_blink - 200) / 120.0), 10));
        bg->addWidget(speed, 0, 2);
        bg->addWidget(new QLabel(QStringLiteral("Висока")), 0, 3);
        bg->setColumnStretch(3, 1);
        v->addWidget(blink);
        v->addStretch(1);
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this] { m_caret->setText(m_caret->text().isEmpty() ? QStringLiteral("|") : QString()); });
        auto restart = [this, timer] {
            if (m_blink > 0) {
                timer->start(m_blink / 2);
            } else {
                timer->stop();
                m_caret->setText(QStringLiteral("|"));
            }
        };
        restart();
        connect(delay, &QSlider::valueChanged, this, [this](int val) {
            m_delay = 1000 - val * 250;
            m_apply->setEnabled(true);
        });
        connect(rate, &QSlider::valueChanged, this, [this](int val) {
            m_rate = 2.5 + val * 27.5 / 31;
            m_apply->setEnabled(true);
        });
        connect(speed, &QSlider::valueChanged, this, [this, restart](int val) {
            m_blink = val == 0 ? 0 : 200 + (10 - val) * 120;
            restart();
            m_apply->setEnabled(true);
        });
        return w;
    }

    QWidget *hardwareTab()
    {
        auto *w = new QWidget;
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(10, 12, 10, 10);
        v->addWidget(new QLabel(QStringLiteral("Пристрої:")));
        auto *tree = new QTreeWidget;
        tree->setRootIsDecorated(false);
        tree->setHeaderLabels({QStringLiteral("Ім'я"), QStringLiteral("Тип")});
        tree->setColumnWidth(0, 220);
        const QString devices = readFile(QStringLiteral("/proc/bus/input/devices"));
        static const QRegularExpression nameRe(QStringLiteral("N: Name=\"([^\"]+)\""));
        QStringList seen;
        for (const QString &block : devices.split(QStringLiteral("\n\n"))) {
            if (!block.contains(QLatin1String("EV=120013"))) { // real keyboards: keys + LEDs + repeat
                continue;
            }
            const QString n = nameRe.match(block).captured(1);
            if (n.isEmpty() || seen.contains(n)) {
                continue;
            }
            seen.append(n);
            auto *it = new QTreeWidgetItem(tree, {n, QStringLiteral("Клавіатури")});
            it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-devmgr-keyboard")));
            it->setData(0, Qt::UserRole, block);
        }
        v->addWidget(tree, 1);
        auto *props = new QGroupBox(QStringLiteral("Властивості пристрою"));
        auto *pg = new QGridLayout(props);
        auto *maker = new QLabel;
        auto *where = new QLabel;
        pg->addWidget(new QLabel(QStringLiteral("Виробник:")), 0, 0);
        pg->addWidget(maker, 0, 1);
        pg->addWidget(new QLabel(QStringLiteral("Розташування:")), 1, 0);
        pg->addWidget(where, 1, 1);
        pg->addWidget(new QLabel(QStringLiteral("Стан пристрою:")), 2, 0);
        pg->addWidget(new QLabel(QStringLiteral("Пристрій працює нормально.")), 2, 1);
        pg->setColumnStretch(1, 1);
        auto *btn = new QPushButton(QStringLiteral("Властивості"));
        pg->addWidget(btn, 3, 1, Qt::AlignRight);
        v->addWidget(props);
        connect(tree, &QTreeWidget::currentItemChanged, this, [maker, where](QTreeWidgetItem *it) {
            if (!it) {
                return;
            }
            const QString block = it->data(0, Qt::UserRole).toString();
            static const QRegularExpression bus(QStringLiteral("Bus=(\\w+)"));
            const QString b = bus.match(block).captured(1);
            where->setText(b == QLatin1String("0005") ? QStringLiteral("Bluetooth") : b == QLatin1String("0003") ? QStringLiteral("USB")
                               : b == QLatin1String("0011")                                    ? QStringLiteral("Порт PS/2")
                                                                                               : QStringLiteral("Невідомо"));
            maker->setText(it->text(0).section(QLatin1Char(' '), 0, 0));
        });
        if (tree->topLevelItemCount()) {
            tree->setCurrentItem(tree->topLevelItem(0));
        }
        connect(btn, &QPushButton::clicked, this, [] { QProcess::startDetached(QDir::homePath() + QStringLiteral("/.local/bin/seven-devmgr"), {}); });
        return w;
    }

    void apply()
    {
        KConfig input(QStringLiteral("kcminputrc"));
        KConfigGroup kb(&input, QStringLiteral("Keyboard"));
        kb.writeEntry("KeyRepeat", QStringLiteral("repeat"));
        kb.writeEntry("RepeatDelay", m_delay);
        kb.writeEntry("RepeatRate", m_rate);
        input.sync();
        QProcess::startDetached(QStringLiteral("xset"), {QStringLiteral("r"), QStringLiteral("rate"), QString::number(m_delay), QString::number(qRound(m_rate))});
        KConfig globals(QStringLiteral("kdeglobals"));
        KConfigGroup(&globals, QStringLiteral("KDE")).writeEntry("CursorBlinkRate", m_blink, KConfig::Notify | KConfig::Normal);
        globals.sync();
        QApplication::setCursorFlashTime(m_blink);
        m_apply->setEnabled(false);
    }

    int m_delay;
    double m_rate;
    int m_blink;
    QLabel *m_caret;
    QPushButton *m_apply;
};
} // namespace Win7Access
