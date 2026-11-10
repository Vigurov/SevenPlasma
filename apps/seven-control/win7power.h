/*
 * "Power Options" pages of the Windows 7 style Control Panel.
 *
 * Plans map to power-profiles-daemon profiles; the display/sleep timeouts of a plan
 * are written into the PowerDevil config (plugged in and on battery) when the plan
 * is active. Power button action and "lock on resume" are PowerDevil/kscreenlocker settings.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7chrome.h"

#include <KConfig>
#include <KConfigGroup>

#include <QButtonGroup>
#include <QComboBox>
#include <QDBusInterface>
#include <QDir>
#include <QSlider>
#include <QGridLayout>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>

namespace Win7Power
{
enum {
    PlansView = 50,
    ButtonsView = 52,
    EditView = 60, // + index into plans()
};

struct Plan {
    QString id; // power-profiles-daemon profile
    QString name;
    QString description;
    int displayMin; // 0 = never
    int sleepMin;
};

inline const QList<Plan> &plans()
{
    static const QList<Plan> list{
        {QStringLiteral("balanced"), tr7("Balanced"),
         tr7("Automatically balances performance with energy consumption on capable hardware."), 10, 30},
        {QStringLiteral("performance"), tr7("High performance"),
         tr7("Favors performance, but may use more energy."), 15, 0},
        {QStringLiteral("power-saver"), tr7("Power saver"),
         tr7("Saves energy by reducing your computer's performance where possible."), 5, 15},
    };
    return list;
}

inline QSettings &store()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-powerrc"), QSettings::IniFormat);
    return s;
}

inline QString runQuiet(const QString &exe, const QStringList &args)
{
    QProcess p;
    p.start(exe, args);
    if (!p.waitForFinished(2000)) {
        p.kill();
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

/** Profiles power-profiles-daemon offers (all three if it isn't running) */
inline QStringList availablePlans()
{
    QStringList ids;
    const QStringList lines = runQuiet(QStringLiteral("powerprofilesctl"), {QStringLiteral("list")}).split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.startsWith(QLatin1Char('*'))) {
            line = line.mid(1).trimmed();
        }
        if (line.endsWith(QLatin1Char(':')) && !line.contains(QLatin1Char(' '))) {
            ids << line.chopped(1);
        }
    }
    if (ids.isEmpty()) {
        for (const Plan &p : plans()) {
            ids << p.id;
        }
    }
    return ids;
}

inline QString activePlan()
{
    const QString id = runQuiet(QStringLiteral("powerprofilesctl"), {QStringLiteral("get")}).trimmed();
    return id.isEmpty() ? store().value(QStringLiteral("General/ActivePlan"), QStringLiteral("balanced")).toString() : id;
}

inline int planIndex(const QString &id)
{
    for (int i = 0; i < plans().size(); ++i) {
        if (plans().at(i).id == id) {
            return i;
        }
    }
    return 0;
}

/** Saved timeouts of a plan, Windows 7 defaults until changed */
inline std::pair<int, int> planTimeouts(const Plan &p)
{
    const QString g = p.id + QLatin1Char('/');
    return {store().value(g + QStringLiteral("DisplayMin"), p.displayMin).toInt(), store().value(g + QStringLiteral("SleepMin"), p.sleepMin).toInt()};
}

inline bool hasLid()
{
    return !QDir(QStringLiteral("/proc/acpi/button/lid")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
}

inline bool hasBattery()
{
    return !QDir(QStringLiteral("/sys/class/power_supply")).entryList({QStringLiteral("BAT*")}, QDir::Dirs | QDir::System | QDir::NoDotAndDotDot).isEmpty();
}

inline void reloadPowerDevil()
{
    QProcess::startDetached(QStringLiteral("qdbus6"),
                            {QStringLiteral("org.kde.Solid.PowerManagement"), QStringLiteral("/org/kde/Solid/PowerManagement"),
                             QStringLiteral("org.kde.Solid.PowerManagement.reparseConfiguration")});
}

/** Make a plan active: power profile + its timeouts, plugged in and on battery */
inline void applyPlan(const Plan &p)
{
    const auto [displayMin, sleepMin] = planTimeouts(p);
    KConfig cfg(QStringLiteral("powerdevilrc"));
    for (const char *source : {"AC", "Battery"}) {
        KConfigGroup g = cfg.group(QLatin1String(source));
        g.group(QStringLiteral("Performance")).writeEntry("PowerProfile", p.id);
        KConfigGroup d = g.group(QStringLiteral("Display"));
        d.writeEntry("TurnOffDisplayWhenIdle", displayMin > 0);
        if (displayMin > 0) {
            d.writeEntry("TurnOffDisplayIdleTimeoutSec", displayMin * 60);
        }
        KConfigGroup s = g.group(QStringLiteral("SuspendAndShutdown"));
        s.writeEntry("AutoSuspendAction", sleepMin > 0 ? 1 : 0); // 1 = sleep
        if (sleepMin > 0) {
            s.writeEntry("AutoSuspendIdleTimeoutSec", sleepMin * 60);
        }
    }
    cfg.sync();
    store().setValue(QStringLiteral("General/ActivePlan"), p.id);
    store().sync();
    QProcess::startDetached(QStringLiteral("powerprofilesctl"), {QStringLiteral("set"), p.id});
    reloadPowerDevil();
}

/* ---------------- widgets ---------------- */

inline QLabel *pageTitle(const QString &text)
{
    auto *l = new QLabel(text);
    QFont f = l->font();
    f.setPixelSize(16);
    l->setFont(f);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: #1e3287;"));
    return l;
}

inline QLabel *bodyText(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    l->setStyleSheet(QStringLiteral("color: #000000;"));
    return l;
}

/** Round "^"/"v" button at the end of "Show/Hide additional plans" */
class ExpandButton : public QAbstractButton
{
public:
    explicit ExpandButton(QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setCheckable(true);
        setFixedSize(19, 19);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient g(0, 0, 0, height());
        const bool hot = underMouse();
        g.setColorAt(0, hot ? QColor(0xf2, 0xf8, 0xff) : QColor(0xff, 0xff, 0xff));
        g.setColorAt(1, hot ? QColor(0xcf, 0xe4, 0xfa) : QColor(0xe4, 0xe4, 0xe4));
        p.setBrush(g);
        p.setPen(hot ? QColor(0x7d, 0xa2, 0xce) : QColor(0x9c, 0x9c, 0x9c));
        p.drawEllipse(r);
        QPen pen(QColor(0x3a, 0x3a, 0x3a), 1.6);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        const qreal cx = width() / 2.0, cy = height() / 2.0;
        if (isChecked()) { // expanded: "^"
            p.drawPolyline(QPolygonF({QPointF(cx - 3.5, cy + 1.8), QPointF(cx, cy - 1.8), QPointF(cx + 3.5, cy + 1.8)}));
        } else {
            p.drawPolyline(QPolygonF({QPointF(cx - 3.5, cy - 1.8), QPointF(cx, cy + 1.8), QPointF(cx + 3.5, cy - 1.8)}));
        }
    }
};

/** Section title with the grey line; optional round expand button at the end */
inline QWidget *sectionLine(const QString &text, ExpandButton *button = nullptr)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(2);
    h->addWidget(new SectionHeader(text), 1);
    if (button) {
        h->addWidget(button, 0, Qt::AlignVCenter);
    }
    return w;
}

struct TimeoutChoice {
    int minutes;
    QString text;
};

inline QList<TimeoutChoice> timeoutChoices()
{
    QList<TimeoutChoice> list;
    for (int m : {1, 2, 3, 5, 10, 15, 20, 25, 30, 45}) {
        list.append({m, tr7("%1 minutes").arg(m)});
    }
    list.append({60, tr7("1 hour", "short")});
    for (int h : {2, 3, 4, 5}) {
        list.append({h * 60, tr7("%1 hours").arg(h)});
    }
    list.append({0, tr7("Never")});
    return list;
}

inline QComboBox *timeoutCombo(int minutes)
{
    auto *c = new QComboBox;
    c->setFixedWidth(150);
    c->setMaxVisibleItems(20);
    for (const TimeoutChoice &t : timeoutChoices()) {
        c->addItem(t.text, t.minutes);
    }
    int idx = c->findData(minutes);
    if (idx < 0) {
        c->insertItem(c->count() - 1, tr7("%1 minutes").arg(minutes), minutes);
        idx = c->findData(minutes);
    }
    c->setCurrentIndex(idx);
    return c;
}

/** Bottom part of the settings pages: full width line, then [Save changes] [Cancel] */
inline QWidget *saveCancelBar(QPushButton **save, QPushButton **cancel, int contentWidth)
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: #e3e3e3;"));
    v->addWidget(line);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *buttons = new QWidget;
    buttons->setFixedWidth(contentWidth);
    auto *bh = new QHBoxLayout(buttons);
    bh->setContentsMargins(0, 0, 0, 0);
    bh->setSpacing(8);
    bh->addStretch(1);
    *save = new QPushButton(tr7("Save changes"));
    *cancel = new QPushButton(tr7("Cancel"));
    (*save)->setMinimumWidth(150);
    (*cancel)->setMinimumWidth(75);
    (*save)->setEnabled(false);
    bh->addWidget(*save);
    bh->addWidget(*cancel);
    row->addWidget(buttons);
    row->addStretch(1);
    v->addLayout(row);
    return w;
}

/** Centered fixed width column used by the "Edit plan" and "System settings" pages */
inline QWidget *centeredColumn(QWidget *content, int width)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->addStretch(1);
    content->setFixedWidth(width);
    h->addWidget(content);
    h->addStretch(1);
    return w;
}

inline QWidget *whitePage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("win7page"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    return page;
}

struct Nav {
    std::function<void(int)> open; // switch the Control Panel to a view
    std::function<void()> back;
    std::function<void()> home;
};

/* ---------------- "Edit Plan Settings" ---------------- */

inline QWidget *buildEditPage(int planIdx, const Nav &nav)
{
    const Plan &plan = plans().at(planIdx);
    const auto [displayMin, sleepMin] = planTimeouts(plan);
    const int width = 580;

    auto *page = whitePage();
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 18, 0, 20);
    v->setSpacing(0);

    auto *col = new QWidget;
    auto *cv = new QVBoxLayout(col);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(pageTitle(tr7("Change settings for the plan: \"%1\"").arg(plan.name)));
    cv->addSpacing(6);
    cv->addWidget(bodyText(tr7("Choose the sleep and display settings that you want your computer to use.")));
    cv->addSpacing(28);

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(20);
    auto *display = timeoutCombo(displayMin);
    auto *sleep = timeoutCombo(sleepMin);
    const QList<std::tuple<QString, QString, QComboBox *>> rows{
        {QStringLiteral("seven-power-display"), tr7("Turn off the display:"), display},
        {QStringLiteral("seven-power-moon"), tr7("Put the computer to sleep:"), sleep},
    };
    int r = 0;
    for (const auto &[icon, text, combo] : rows) {
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(icon).pixmap(16, 16));
        grid->addWidget(ic, r, 0);
        grid->addWidget(new QLabel(text), r, 1);
        grid->addWidget(combo, r, 2);
        ++r;
    }
    grid->setColumnMinimumWidth(1, 240);
    grid->setColumnStretch(3, 1);
    cv->addLayout(grid);
    cv->addSpacing(32);

    cv->addWidget(new Link(tr7("Change advanced power settings"), [] {
        launch(QStringLiteral("systemsettings"), {QStringLiteral("kcm_powerdevilprofilesconfig")});
    }));
    cv->addSpacing(12);
    auto *restore = new Link(tr7("Restore default settings for this plan"), nullptr);
    cv->addWidget(restore);
    v->addWidget(centeredColumn(col, width));
    v->addSpacing(18);

    QPushButton *save = nullptr;
    QPushButton *cancel = nullptr;
    v->addWidget(saveCancelBar(&save, &cancel, width));
    v->addStretch(1);

    auto changed = [=] {
        save->setEnabled(display->currentData().toInt() != displayMin || sleep->currentData().toInt() != sleepMin);
    };
    QObject::connect(display, &QComboBox::currentIndexChanged, page, changed);
    QObject::connect(sleep, &QComboBox::currentIndexChanged, page, changed);
    QObject::connect(cancel, &QPushButton::clicked, page, [nav] {
        nav.back();
    });
    QObject::connect(save, &QPushButton::clicked, page, [=] {
        store().setValue(plan.id + QStringLiteral("/DisplayMin"), display->currentData().toInt());
        store().setValue(plan.id + QStringLiteral("/SleepMin"), sleep->currentData().toInt());
        store().sync();
        if (activePlan() == plan.id) {
            applyPlan(plan);
        }
        nav.open(PlansView);
    });
    // Link has no setter for the action; handle the click on the label itself
    class RestoreFilter : public QObject
    {
    public:
        std::function<void()> fn;
        bool eventFilter(QObject *, QEvent *e) override
        {
            if (e->type() == QEvent::MouseButtonRelease) {
                fn();
                return true;
            }
            return false;
        }
    };
    auto *filter = new RestoreFilter;
    filter->setParent(restore);
    filter->fn = [=] {
        if (QMessageBox::question(page->window(), tr7("Power Options"),
                                  tr7("Restore this plan's default settings?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
        store().remove(plan.id);
        store().sync();
        if (activePlan() == plan.id) {
            applyPlan(plan);
        }
        display->setCurrentIndex(display->findData(plan.displayMin));
        sleep->setCurrentIndex(sleep->findData(plan.sleepMin));
        nav.open(EditView + planIdx);
    };
    restore->installEventFilter(filter);
    return page;
}

/* ---------------- "System Settings": power button + password on wakeup ---------------- */

inline bool canHibernate()
{
    return runQuiet(QStringLiteral("busctl"),
                    {QStringLiteral("call"), QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
                     QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("CanHibernate")})
        .contains(QLatin1String("\"yes\""));
}

inline QWidget *buildButtonsPage(const Nav &nav)
{
    const int width = 580;
    KConfig power(QStringLiteral("powerdevilrc"));
    const int buttonAction = power.group(QStringLiteral("AC")).group(QStringLiteral("SuspendAndShutdown")).readEntry("PowerButtonAction", 16);
    const int lidAction = power.group(QStringLiteral("AC")).group(QStringLiteral("SuspendAndShutdown")).readEntry("LidAction", 1);
    const bool lid = hasLid();
    KConfig locker(QStringLiteral("kscreenlockerrc"));
    const bool lockOnResume = locker.group(QStringLiteral("Daemon")).readEntry("LockOnResume", true);

    auto *page = whitePage();
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 18, 0, 20);
    v->setSpacing(0);

    auto *col = new QWidget;
    auto *cv = new QVBoxLayout(col);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(pageTitle(tr7("Define power buttons and turn on password protection")));
    cv->addSpacing(6);
    cv->addWidget(bodyText(tr7("Choose the power settings that you want for your computer. The changes you make to the settings on this page apply to all of your power plans.")));
    cv->addSpacing(14);
    cv->addWidget(sectionLine(lid ? tr7("Power and lid button settings") : tr7("Power button settings")));
    cv->addSpacing(10);

    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(16, 0, 0, 0);
    btnRow->setSpacing(8);
    auto *ic = new QLabel;
    ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-power-button")).pixmap(32, 32));
    btnRow->addWidget(ic);
    auto *btnLabel = new QLabel(tr7("When I press the power button:"));
    btnRow->addWidget(btnLabel);
    auto *combo = new QComboBox;
    combo->setFixedWidth(170);
    combo->addItem(tr7("Do nothing"), 0);
    combo->addItem(tr7("Sleep"), 1);
    if (canHibernate()) {
        combo->addItem(tr7("Hibernate"), 2);
    }
    combo->addItem(tr7("Shut down"), 8);
    if (combo->findData(buttonAction) < 0) {
        // KDE's own choices (e.g. the "leave" dialog) stay selectable so nothing changes by accident
        combo->addItem(buttonAction == 16 ? tr7("Ask me what to do")
                                          : buttonAction == 32 ? tr7("Lock") : tr7("Other action"),
                       buttonAction);
    }
    combo->setCurrentIndex(combo->findData(buttonAction));
    btnRow->addWidget(combo);
    btnRow->addStretch(1);
    cv->addLayout(btnRow);

    // laptops: what closing the lid does
    auto *lidCombo = new QComboBox;
    if (lid) {
        auto *lidRow = new QHBoxLayout;
        lidRow->setContentsMargins(16, 6, 0, 0);
        lidRow->setSpacing(8);
        auto *lidIcon = new QLabel;
        lidIcon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-power-lid")).pixmap(32, 32));
        lidRow->addWidget(lidIcon);
        auto *lidLabel = new QLabel(tr7("When I close the lid:"));
        lidLabel->setMinimumWidth(btnLabel->sizeHint().width());
        lidRow->addWidget(lidLabel);
        lidCombo->setFixedWidth(170);
        lidCombo->addItem(tr7("Do nothing"), 0);
        lidCombo->addItem(tr7("Sleep"), 1);
        if (canHibernate()) {
            lidCombo->addItem(tr7("Hibernate"), 2);
        }
        lidCombo->addItem(tr7("Shut down"), 8);
        if (lidCombo->findData(lidAction) < 0) {
            lidCombo->addItem(lidAction == 32 ? tr7("Lock") : lidAction == 64 ? tr7("Turn off the display") : tr7("Other action"), lidAction);
        }
        lidCombo->setCurrentIndex(lidCombo->findData(lidAction));
        lidRow->addWidget(lidCombo);
        lidRow->addStretch(1);
        cv->addLayout(lidRow);
    }
    cv->addSpacing(16);

    cv->addWidget(sectionLine(tr7("Password protection on wakeup")));
    cv->addSpacing(8);
    auto *ask = new QRadioButton(tr7("Require a password (recommended)"));
    auto *dontAsk = new QRadioButton(tr7("Don't require a password"));
    auto *group = new QButtonGroup(page);
    group->addButton(ask);
    group->addButton(dontAsk);
    (lockOnResume ? ask : dontAsk)->setChecked(true);

    auto addChoice = [cv](QRadioButton *radio, QWidget *desc) {
        auto *wrap = new QVBoxLayout;
        wrap->setContentsMargins(16, 0, 0, 0);
        wrap->setSpacing(2);
        wrap->addWidget(radio);
        auto *d = new QHBoxLayout;
        d->setContentsMargins(19, 0, 0, 0);
        d->addWidget(desc);
        wrap->addLayout(d);
        cv->addLayout(wrap);
    };
    auto *askDesc = bodyText(tr7("When your computer wakes from sleep, no one can access your data without entering the correct password to unlock the computer. <a href=\"users\" style=\"color:#0066cc;\">Create or change your user account password</a>"));
    askDesc->setOpenExternalLinks(false);
    QObject::connect(askDesc, &QLabel::linkActivated, askDesc, [nav] {
        nav.open(20); // Win7Users::HomeView
    });
    addChoice(ask, askDesc);
    cv->addSpacing(14);
    addChoice(dontAsk,
              bodyText(tr7("When your computer wakes from sleep, anyone can access your data because the computer isn't locked.")));
    v->addWidget(centeredColumn(col, width));
    v->addSpacing(22);

    QPushButton *save = nullptr;
    QPushButton *cancel = nullptr;
    v->addWidget(saveCancelBar(&save, &cancel, width));
    v->addStretch(1);

    auto changed = [=] {
        save->setEnabled(combo->currentData().toInt() != buttonAction || ask->isChecked() != lockOnResume
                         || (lid && lidCombo->currentData().toInt() != lidAction));
    };
    QObject::connect(combo, &QComboBox::currentIndexChanged, page, changed);
    QObject::connect(lidCombo, &QComboBox::currentIndexChanged, page, changed);
    if (!lid) {
        lidCombo->setParent(page);
        lidCombo->hide();
    }
    QObject::connect(ask, &QRadioButton::toggled, page, changed);
    QObject::connect(cancel, &QPushButton::clicked, page, [nav] {
        nav.back();
    });
    QObject::connect(save, &QPushButton::clicked, page, [=] {
        KConfig cfg(QStringLiteral("powerdevilrc"));
        for (const char *source : {"AC", "Battery", "LowBattery"}) {
            cfg.group(QLatin1String(source)).group(QStringLiteral("SuspendAndShutdown")).writeEntry("PowerButtonAction", combo->currentData().toInt());
            if (lid) {
                cfg.group(QLatin1String(source)).group(QStringLiteral("SuspendAndShutdown")).writeEntry("LidAction", lidCombo->currentData().toInt());
            }
        }
        cfg.sync();
        reloadPowerDevil();
        KConfig lock(QStringLiteral("kscreenlockerrc"));
        lock.group(QStringLiteral("Daemon")).writeEntry("LockOnResume", ask->isChecked());
        lock.sync();
        QProcess::startDetached(QStringLiteral("qdbus6"),
                                {QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"), QStringLiteral("org.kde.screensaver.configure")});
        nav.open(PlansView);
    });
    return page;
}

/* ---------------- "Select a power plan" ---------------- */

/** One plan: radio + name, "Change plan settings" on the right, description below */
inline QWidget *planRow(int idx, bool checked, QButtonGroup *group, const Nav &nav)
{
    const Plan &p = plans().at(idx);
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(16, 0, 0, 0);
    v->setSpacing(2);
    auto *top = new QHBoxLayout;
    top->setSpacing(4);
    auto *radio = new QRadioButton(idx == 0 ? p.name + tr7(" (Recommended)") : p.name);
    if (idx == 0) {
        QFont f = radio->font();
        f.setBold(true);
        radio->setFont(f);
    }
    radio->setChecked(checked);
    group->addButton(radio, idx);
    top->addWidget(radio);
    top->addStretch(1);
    auto *change = new Link(tr7("Edit Plan Settings"), [nav, idx] {
        nav.open(EditView + idx);
    });
    change->setWordWrap(false);
    top->addWidget(change);
    v->addLayout(top);
    auto *desc = bodyText(p.description);
    desc->setContentsMargins(19, 0, 150, 0);
    v->addWidget(desc);
    return w;
}

inline QWidget *buildPlansPage(const Nav &nav)
{
    const QStringList available = availablePlans();
    const QString active = activePlan();

    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("win7page"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    root->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"
                                       "#win7side { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #e3ecf8, stop:0.5 #eef3fb, stop:1 #f3f7fc);"
                                       " border-right: 1px solid #e0e8f3; }"));
    auto *h = new QHBoxLayout(root);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    // left pane
    auto *side = new QWidget;
    side->setObjectName(QStringLiteral("win7side"));
    side->setAttribute(Qt::WA_StyledBackground, true);
    side->setFixedWidth(200);
    auto *sv = new QVBoxLayout(side);
    sv->setContentsMargins(4, 16, 10, 14);
    sv->setSpacing(7);
    const QString sideColor = QStringLiteral("#1e3a5f");
    auto addRow = [sv](const QString &icon, QWidget *text) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        auto *ic = new QLabel;
        ic->setFixedWidth(18);
        if (!icon.isEmpty()) {
            ic->setPixmap(QIcon::fromTheme(icon).pixmap(16, 16));
        }
        ic->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        row->addWidget(ic, 0, Qt::AlignTop);
        row->addWidget(text, 1);
        sv->addLayout(row);
    };
    const int activeIdx = planIndex(active);
    addRow({}, new Link(tr7("Control Panel Home"), nav.home, sideColor));
    sv->addSpacing(9);
    addRow({}, new Link(tr7("Require a password on wakeup"), [nav] {
        nav.open(ButtonsView);
    }, sideColor));
    addRow({}, new Link(tr7("Choose what the power buttons do"), [nav] {
        nav.open(ButtonsView);
    }, sideColor));
    if (hasLid()) {
        addRow({}, new Link(tr7("Choose what closing the lid does"), [nav] {
            nav.open(ButtonsView);
        }, sideColor));
    }
    addRow(QStringLiteral("seven-power-display"), new Link(tr7("Choose when to turn off the display"), [nav, activeIdx] {
        nav.open(EditView + activeIdx);
    }, sideColor));
    addRow(QStringLiteral("seven-power-moon"), new Link(tr7("Change when the computer sleeps", "task"), [nav, activeIdx] {
        nav.open(EditView + activeIdx);
    }, sideColor));
    sv->addStretch(1);
    auto *seeAlso = grey(tr7("See also"));
    auto *seeRow = new QHBoxLayout;
    seeRow->setContentsMargins(22, 0, 0, 0);
    seeRow->addWidget(seeAlso);
    sv->addLayout(seeRow);
    addRow({}, new Link(tr7("Personalization"), [nav] {
        nav.open(80); // Win7Display::PersonalizeView
    }, sideColor));
    addRow({}, new Link(tr7("User Accounts"), [nav] {
        nav.open(20); // Win7Users::HomeView
    }, sideColor));
    h->addWidget(side);

    // main part
    auto *main = new QWidget;
    auto *mv = new QVBoxLayout(main);
    mv->setContentsMargins(24, 18, 24, 20);
    mv->setSpacing(0);
    auto *content = new QWidget;
    content->setMaximumWidth(580);
    auto *cv = new QVBoxLayout(content);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(pageTitle(tr7("Select a power plan")));
    cv->addSpacing(8);
    cv->addWidget(bodyText(tr7("Power plans can help you maximize your computer's performance or conserve energy. Make a plan active by selecting it, or choose a plan and customize it by changing its power settings.")));
    cv->addSpacing(16);

    // Balanced and High performance are the preferred plans, Power saver is under "additional plans"
    QList<int> preferred{0};
    const int second = planIndex(QStringLiteral("performance"));
    QList<int> extra;
    for (int i = 1; i < plans().size(); ++i) {
        if (!available.contains(plans().at(i).id)) {
            continue;
        }
        (i == second ? preferred : extra).append(i);
    }

    auto *group = new QButtonGroup(root);
    cv->addWidget(sectionLine(hasBattery() ? tr7("Plans shown on the battery meter") : tr7("Preferred plans")));
    cv->addSpacing(8);
    for (int i : preferred) {
        cv->addWidget(planRow(i, i == activeIdx, group, nav));
        cv->addSpacing(14);
    }
    if (!extra.isEmpty()) {
        const bool shown = store().value(QStringLiteral("General/ShowExtraPlans"), false).toBool();
        auto *expand = new ExpandButton;
        expand->setChecked(shown);
        auto *header = new SectionHeader(shown ? tr7("Hide additional plans") : tr7("Show additional plans"));
        header->setCursor(Qt::PointingHandCursor);
        auto *hw = new QWidget;
        auto *hh = new QHBoxLayout(hw);
        hh->setContentsMargins(0, 0, 0, 0);
        hh->setSpacing(2);
        hh->addWidget(header, 1);
        hh->addWidget(expand, 0, Qt::AlignVCenter);
        cv->addWidget(hw);
        cv->addSpacing(8);
        auto *extraBox = new QWidget;
        auto *ev = new QVBoxLayout(extraBox);
        ev->setContentsMargins(0, 0, 0, 0);
        ev->setSpacing(14);
        for (int i : extra) {
            ev->addWidget(planRow(i, i == activeIdx, group, nav));
        }
        extraBox->setVisible(shown);
        cv->addWidget(extraBox);
        QObject::connect(expand, &QAbstractButton::toggled, root, [nav](bool on) {
            store().setValue(QStringLiteral("General/ShowExtraPlans"), on);
            store().sync();
            nav.open(PlansView); // rebuild with the other header text
        });
        class Click : public QObject
        {
        public:
            ExpandButton *b;
            bool eventFilter(QObject *, QEvent *e) override
            {
                if (e->type() == QEvent::MouseButtonRelease) {
                    b->toggle();
                    return true;
                }
                return false;
            }
        };
        auto *click = new Click;
        click->b = expand;
        click->setParent(header);
        header->installEventFilter(click);
    }
    QObject::connect(group, &QButtonGroup::idClicked, root, [](int idx) {
        applyPlan(plans().at(idx));
    });

    mv->addWidget(content);
    mv->addStretch(1);

    // "Screen brightness" slider at the bottom, like on Windows 7 laptops
    auto *brightness = new QDBusInterface(QStringLiteral("org.kde.ScreenBrightness"), QStringLiteral("/org/kde/ScreenBrightness/display0"),
                                          QStringLiteral("org.kde.ScreenBrightness.Display"), QDBusConnection::sessionBus(), main);
    const int maxBrightness = brightness->isValid() ? brightness->property("MaxBrightness").toInt() : 0;
    if (maxBrightness > 0) {
        auto *bar = new QWidget;
        bar->setObjectName(QStringLiteral("win7brightbar"));
        bar->setAttribute(Qt::WA_StyledBackground, true);
        bar->setStyleSheet(QStringLiteral("#win7brightbar { border-top: 1px solid #e3e8ef; background: #ffffff; }"));
        auto *bl = new QHBoxLayout(bar);
        bl->setContentsMargins(0, 8, 0, 4);
        bl->addStretch(1);
        bl->addWidget(new QLabel(tr7("Screen brightness:")));
        auto *dim = new QLabel;
        dim->setPixmap(QIcon::fromTheme(QStringLiteral("seven-sun-dim")).pixmap(16, 16));
        bl->addWidget(dim);
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, maxBrightness);
        slider->setValue(brightness->property("Brightness").toInt());
        slider->setFixedWidth(150);
        slider->setTickPosition(QSlider::TicksBelow);
        slider->setTickInterval(qMax(1, maxBrightness / 4));
        bl->addWidget(slider);
        auto *bright = new QLabel;
        bright->setPixmap(QIcon::fromTheme(QStringLiteral("seven-sun-bright")).pixmap(16, 16));
        bl->addWidget(bright);
        bl->addStretch(1);
        QObject::connect(slider, &QSlider::valueChanged, bar, [brightness](int v) {
            brightness->asyncCall(QStringLiteral("SetBrightness"), v, 0u);
        });
        mv->addWidget(bar);
    }
    h->addWidget(main, 1);
    return root;
}
}
