/*
 * Windows 7 "Performance Options" (SystemPropertiesPerformance.exe, sysdm.cpl dialogs 112 / 103).
 *
 *   Animate controls and elements inside windows -> kdeglobals [KDE] AnimationDurationFactor
 *   Animate windows when minimizing and maximizing -> KWin squashaero + aeroglide
 *   Enable Aero Peek                             -> KWin smodpeekeffect
 *   Enable desktop composition                   -> KWin compositing (suspend/resume, re-applied at login)
 *   Enable transparent glass                     -> KWin aeroglassblur
 *   Fade or slide menus into view                -> KWin fadingpopupsaero
 *   Smooth edges of screen fonts                 -> kdeglobals XftAntialias + fontconfig + Xft.antialias
 * Effects with no counterpart here (cursor shadows, combo box sliding, folder backgrounds...) are left
 * out, as are Processor scheduling and Data Execution Prevention. Virtual memory shows the swap size.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7sysprops.h"

#include <KConfig>
#include <KConfigGroup>
#include <QDBusInterface>
#include <QDBusReply>
#include <QThread>

namespace Win7VisualFx
{
using Win7SysProps::dlu;
using Win7SysProps::place;

enum Effect { Controls, Windows, Peek, Composition, Glass, Menus, Fonts, EffectCount };

struct Item {
    Effect id;
    QString text;
    QStringList kwin; // KWin effects behind it
};

inline const QList<Item> &items()
{
    static const QList<Item> list{
        {Controls, QStringLiteral("Анімація елементів керування та елементів усередині вікна"), {}},
        {Windows, QStringLiteral("Анімація вікон під час згортання й розгортання"), {QStringLiteral("squashaero"), QStringLiteral("aeroglide")}},
        {Peek, QStringLiteral("Увімкнути Aero Peek"), {QStringLiteral("smodpeekeffect")}},
        {Composition, QStringLiteral("Увімкнути композицію робочого столу"), {}},
        {Glass, QStringLiteral("Увімкнути прозорість"), {QStringLiteral("aeroglassblur")}},
        {Menus, QStringLiteral("Згасання або ковзання меню й підказок під час появи"), {QStringLiteral("fadingpopupsaero")}},
        {Fonts, QStringLiteral("Згладжування нерівностей екранних шрифтів"), {}},
    };
    return list;
}

inline QSettings &store()
{
    static QSettings s(QDir::homePath() + QStringLiteral("/.config/seven-visualfxrc"), QSettings::IniFormat);
    return s;
}

inline QDBusInterface &compositor()
{
    static QDBusInterface i(QStringLiteral("org.kde.KWin"), QStringLiteral("/Compositor"), QStringLiteral("org.kde.kwin.Compositing"));
    return i;
}

inline QStringList loadedEffects()
{
    QDBusInterface fx(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"));
    return fx.property("loadedEffects").toStringList();
}

/** what is in effect now */
inline QList<bool> current()
{
    QList<bool> on(EffectCount, true);
    const QStringList loaded = loadedEffects();
    const bool composited = compositor().property("active").toBool();
    KConfigGroup kde(KSharedConfig::openConfig(QStringLiteral("kdeglobals")), QStringLiteral("KDE"));
    KConfigGroup general(KSharedConfig::openConfig(QStringLiteral("kdeglobals")), QStringLiteral("General"));
    KConfigGroup plugins(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Plugins"));
    for (const Item &it : items()) {
        if (!it.kwin.isEmpty()) {
            // with compositing suspended nothing is loaded, so fall back to kwinrc
            on[it.id] = composited ? loaded.contains(it.kwin.first()) : plugins.readEntry(it.kwin.first() + QStringLiteral("Enabled"), true);
        }
    }
    on[Controls] = kde.readEntry("AnimationDurationFactor", 1.0) > 0;
    on[Composition] = composited;
    on[Fonts] = general.readEntry("XftAntialias", true);
    return on;
}

inline void setFontSmoothing(bool on)
{
    KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("kdeglobals")), QStringLiteral("General"));
    g.writeEntry("XftAntialias", on, KConfig::Notify | KConfig::Normal);
    g.sync();
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
    const QString value = on ? QStringLiteral("true") : QStringLiteral("false");
    const QRegularExpression re(QStringLiteral("(<edit mode=\"assign\" name=\"antialias\">\\s*<bool>)[^<]*(</bool>)"));
    if (xml.contains(re)) {
        xml.replace(re, QStringLiteral("\\1%1\\2").arg(value));
    } else {
        xml.replace(QStringLiteral("</fontconfig>"),
                    QStringLiteral(" <match target=\"font\">\n  <edit mode=\"assign\" name=\"antialias\">\n   <bool>%1</bool>\n  </edit>\n </match>\n</fontconfig>").arg(value));
    }
    QDir().mkpath(QFileInfo(path).path());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(xml.toUtf8());
        f.close();
    }
    QProcess xrdb;
    xrdb.start(QStringLiteral("xrdb"), {QStringLiteral("-merge")});
    if (xrdb.waitForStarted(2000)) {
        xrdb.write(QStringLiteral("Xft.antialias: %1\n").arg(on ? 1 : 0).toUtf8());
        xrdb.closeWriteChannel();
        xrdb.waitForFinished(3000);
    }
}

/** KWin on X11 always starts composited: a login autostart suspends it again when it is turned off */
inline void setComposition(bool on)
{
    const QString autostart = QDir::homePath() + QStringLiteral("/.config/autostart/seven-visualfx.desktop");
    if (on) {
        QFile::remove(autostart);
    } else {
        QDir().mkpath(QFileInfo(autostart).path());
        QFile f(autostart);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Win7 visual effects\nExec=%1 --visualfx-apply\nNoDisplay=true\nX-KDE-autostart-phase=2\n")
                        .arg(QCoreApplication::applicationFilePath())
                        .toUtf8());
        }
    }
    if (compositor().property("active").toBool() != on) {
        QDBusInterface accel(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/component/kwin"), QStringLiteral("org.kde.kglobalaccel.Component"));
        accel.call(QStringLiteral("invokeShortcut"), QStringLiteral("Suspend Compositing"));
    }
}

/** --visualfx-apply: run at login when desktop composition is turned off */
inline int applyAtLogin()
{
    for (int i = 0; i < 20 && !compositor().isValid(); ++i) {
        QThread::sleep(1);
    }
    if (!store().value(QStringLiteral("Composition"), true).toBool()) {
        setComposition(false);
    }
    return 0;
}

inline void apply(const QList<bool> &want, const QList<bool> &was)
{
    KSharedConfigPtr kwinrc = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup plugins(kwinrc, QStringLiteral("Plugins"));
    QDBusInterface fx(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"));
    for (const Item &it : items()) {
        if (want[it.id] == was[it.id]) {
            continue;
        }
        for (const QString &e : it.kwin) {
            plugins.writeEntry(e + QStringLiteral("Enabled"), want[it.id]);
            fx.call(want[it.id] ? QStringLiteral("loadEffect") : QStringLiteral("unloadEffect"), e);
        }
    }
    kwinrc->sync();
    if (want[Controls] != was[Controls]) {
        KConfig globals(QStringLiteral("kdeglobals"));
        KConfigGroup kde(&globals, QStringLiteral("KDE"));
        if (!want[Controls]) {
            store().setValue(QStringLiteral("AnimationDurationFactor"), kde.readEntry("AnimationDurationFactor", 0.5));
            kde.writeEntry("AnimationDurationFactor", 0.0, KConfig::Notify | KConfig::Normal);
        } else {
            const double before = store().value(QStringLiteral("AnimationDurationFactor"), 0.5).toDouble();
            kde.writeEntry("AnimationDurationFactor", before > 0 ? before : 0.5, KConfig::Notify | KConfig::Normal);
        }
        globals.sync();
    }
    if (want[Fonts] != was[Fonts]) {
        setFontSmoothing(want[Fonts]);
    }
    if (want[Composition] != was[Composition]) {
        store().setValue(QStringLiteral("Composition"), want[Composition]);
        setComposition(want[Composition]);
    }
    store().sync();
}

inline qint64 swapMegabytes()
{
    QFile f(QStringLiteral("/proc/swaps"));
    qint64 kb = 0;
    if (f.open(QIODevice::ReadOnly)) {
        f.readLine();
        while (!f.atEnd()) {
            const QStringList c = QString::fromUtf8(f.readLine()).simplified().split(QLatin1Char(' '));
            if (c.size() >= 3) {
                kb += c[2].toLongLong();
            }
        }
    }
    return kb / 1024;
}

class PerformanceOptions : public QDialog
{
public:
    explicit PerformanceOptions(QWidget *parent = nullptr)
        : QDialog(parent)
        , m_was(current())
    {
        setWindowTitle(QStringLiteral("Параметри швидкодії"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cps-system")));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_DeleteOnClose);
        const QSize page = dlu(0, 0, 227, 267).size();
        setFixedSize(page.width() + 26, page.height() + 76);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(8, 8, 8, 8);
        auto *tabs = new QTabWidget;
        tabs->addTab(effectsPage(), QStringLiteral("Візуальні ефекти"));
        tabs->addTab(advancedPage(), QStringLiteral("Додатково"));
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
            commit();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] {
            commit();
        });
    }

private:
    QWidget *effectsPage()
    {
        auto *p = new QWidget;
        Win7SysProps::text(p, QStringLiteral("Виберіть параметри, які потрібно використовувати для оформлення та швидкодії Windows на цьому комп'ютері."), dlu(7, 6, 208, 18));
        const QStringList radios{QStringLiteral("Дозволити Windows вибирати найкращі параметри"), QStringLiteral("Забезпечити найкращий вигляд"),
                                 QStringLiteral("Забезпечити найкращу швидкодію"), QStringLiteral("Особливі ефекти:")};
        for (int i = 0; i < radios.size(); ++i) {
            m_radio[i] = place(p, new QRadioButton(radios[i]), dlu(7, 30 + 12 * i, 200, 12));
            connect(m_radio[i], &QRadioButton::clicked, this, [this, i] {
                preset(i);
            });
        }
        m_tree = place(p, new QTreeWidget, dlu(7, 80, 213, 180));
        m_tree->setHeaderHidden(true);
        m_tree->setRootIsDecorated(false);
        m_tree->setIndentation(0);
        m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
        for (const Item &it : items()) {
            auto *ti = new QTreeWidgetItem(m_tree, {it.text});
            ti->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            ti->setCheckState(0, m_was[it.id] ? Qt::Checked : Qt::Unchecked);
        }
        connect(m_tree, &QTreeWidget::itemChanged, this, [this] {
            if (!m_presetting) {
                m_radio[3]->setChecked(true);
                m_apply->setEnabled(true);
            }
        });
        // the remembered choice, unless the effects were changed somewhere else since
        int choice = store().value(QStringLiteral("Setting"), -1).toInt();
        const bool allOn = !m_was.contains(false), allOff = !m_was.contains(true);
        if ((choice == 0 || choice == 1) && !allOn) {
            choice = 3;
        } else if (choice == 2 && !allOff) {
            choice = 3;
        } else if (choice < 0) {
            choice = allOn ? 0 : (allOff ? 2 : 3);
        }
        m_radio[qBound(0, choice, 3)]->setChecked(true);
        return p;
    }

    QWidget *advancedPage()
    {
        auto *p = new QWidget;
        auto *box = place(p, new QGroupBox(QStringLiteral("Віртуальна пам'ять")), dlu(7, 8, 213, 52));
        Q_UNUSED(box)
        Win7SysProps::text(p, QStringLiteral("Файл підкачки – це область на жорсткому диску, яку Windows використовує як оперативну пам'ять."), dlu(14, 20, 199, 16));
        Win7SysProps::text(p, QStringLiteral("Загальний розмір файлу підкачки для всіх дисків:"), dlu(14, 40, 172, 8));
        Win7SysProps::text(p, QStringLiteral("%1 МБ").arg(QLocale(QLocale::Ukrainian).toString(swapMegabytes())), dlu(187, 40, 32, 8));
        return p;
    }

    void preset(int i)
    {
        if (i == 3) {
            return;
        }
        m_presetting = true;
        for (int r = 0; r < m_tree->topLevelItemCount(); ++r) {
            m_tree->topLevelItem(r)->setCheckState(0, i == 2 ? Qt::Unchecked : Qt::Checked);
        }
        m_presetting = false;
        m_apply->setEnabled(true);
    }

    void commit()
    {
        QList<bool> want(EffectCount, true);
        for (int r = 0; r < m_tree->topLevelItemCount(); ++r) {
            want[items()[r].id] = m_tree->topLevelItem(r)->checkState(0) == Qt::Checked;
        }
        for (int i = 0; i < 4; ++i) {
            if (m_radio[i]->isChecked()) {
                store().setValue(QStringLiteral("Setting"), i);
            }
        }
        apply(want, m_was);
        m_was = want;
        m_apply->setEnabled(false);
    }

    QList<bool> m_was;
    QRadioButton *m_radio[4] = {};
    QTreeWidget *m_tree = nullptr;
    QPushButton *m_apply = nullptr;
    bool m_presetting = false;
};

inline void open()
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--visualfx")});
}
}
