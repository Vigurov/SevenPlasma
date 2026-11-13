/*
 * Control Panel\System and Security\System: a page of the Control Panel's window, as in Windows 7 (its address
 * bar, Back and the pages of its side pane stay in this window). seven-system opens it (Win+Pause, the Start
 * menu's Computer > Properties).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7chrome.h"
#include "win7perf.h"

#include <AeroQt/sidebar.h>

#include <KConfig>
#include <KConfigGroup>

#include <QHostInfo>
#include <QPainterPath>
#include <QSettings>
#include <QStyle>
#include <QSysInfo>

#include <cmath>

namespace Win7System
{
enum { SystemView = 98 };

struct Nav {
    std::function<void(int)> open;
    std::function<void()> home;
    int actionCenter = 0;
    int windowsUpdate = 0;
};

struct Info {
    QString edition, subtitle, kernel, logo;
    QString cpu, memory, systemType, touch;
    QString computer, fullName, description, workgroup;
};

inline Info collect()
{
    Info i;

    // Branding: the same file KInfoCenter uses ("AeroThemePlasma", "Windows 7", logo)
    KConfig branding(QStringLiteral("kcm-about-distrorc"));
    const KConfigGroup g = branding.group(QStringLiteral("General"));
    const QString osRelease = readFile(QStringLiteral("/etc/os-release"));
    QString name = g.readEntry("Name", keyValue(osRelease, QStringLiteral("NAME"), QLatin1Char('=')));
    QString version = g.readEntry("Version", QString());
    if (version.isEmpty() || g.readEntry("UseOSReleaseVersion", true)) {
        if (version.isEmpty()) {
            version = keyValue(osRelease, QStringLiteral("VERSION_ID"), QLatin1Char('='));
        }
    }
    const QString variant = g.readEntry("Variant", QString());
    i.edition = QStringLiteral("%1 %2").arg(name, version).trimmed();
    if (!variant.isEmpty()) {
        i.edition += QStringLiteral(" (%1)").arg(variant);
    }
    i.logo = g.readEntry("LogoPath", QString());

    QString plasma;
    {
        QProcess proc;
        proc.start(QStringLiteral("plasmashell"), {QStringLiteral("--version")});
        if (proc.waitForFinished(2000)) {
            plasma = QString::fromUtf8(proc.readAllStandardOutput()).section(QLatin1Char(' '), 1).trimmed();
        }
    }
    i.subtitle = keyValue(osRelease, QStringLiteral("PRETTY_NAME"), QLatin1Char('='));
    if (!plasma.isEmpty()) {
        i.subtitle += QStringLiteral(", KDE Plasma ") + plasma;
    }
    i.kernel = tr7("Linux kernel ") + QSysInfo::kernelVersion();

    // Processor "AMD Ryzen 7 5800H with Radeon Graphics   3.20 GHz"
    const QString cpuinfo = readFile(QStringLiteral("/proc/cpuinfo"));
    i.cpu = keyValue(cpuinfo, QStringLiteral("model name"), QLatin1Char(':'));
    const double maxKHz = readFile(QStringLiteral("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq")).trimmed().toDouble();
    if (maxKHz > 0) {
        i.cpu += QStringLiteral("   ") + QLocale(QLocale::C).toString(maxKHz / 1e6, 'f', 2) + QStringLiteral(" GHz");
    }

    // Installed memory (RAM): "32,0 ГБ (31,2 ГБ доступно)"
    const double totalGiB = keyValue(readFile(QStringLiteral("/proc/meminfo")), QStringLiteral("MemTotal"), QLatin1Char(':')).section(QLatin1Char(' '), 0, 0).toDouble()
        / (1024.0 * 1024.0);
    const double installed = std::ceil(totalGiB / 2.0) * 2.0; // firmware reserves a bit; modules come in pairs of GiB
    i.memory = gb(installed);
    if (installed - totalGiB > 0.05) {
        i.memory += tr7(" (%1 usable)").arg(gb(totalGiB));
    }

    i.systemType = QSysInfo::WordSize == 64 ? tr7("64-bit Operating System") : tr7("32-bit Operating System");

    const QString inputs = readFile(QStringLiteral("/proc/bus/input/devices"));
    const bool touch = inputs.contains(QStringLiteral("Touchscreen"), Qt::CaseInsensitive) || inputs.contains(QStringLiteral(" Pen"), Qt::CaseInsensitive);
    i.touch = touch ? tr7("Pen and Touch Input Available") : tr7("No Pen or Touch Input is available for this Display");

    i.computer = QSysInfo::machineHostName();
    const QString domain = QHostInfo::localDomainName();
    i.fullName = domain.isEmpty() ? i.computer : i.computer + QLatin1Char('.') + domain;
    i.description = keyValue(readFile(QStringLiteral("/etc/machine-info")), QStringLiteral("PRETTY_HOSTNAME"), QLatin1Char('='));
    i.workgroup = keyValue(readFile(QStringLiteral("/etc/samba/smb.conf")), QStringLiteral("workgroup"), QLatin1Char('='));
    if (i.workgroup.isEmpty()) {
        i.workgroup = QStringLiteral("WORKGROUP");
    }
    return i;
}


/** the side pane of Windows 7's System page */
inline QWidget *makeSidebar(const Nav &nav)
{
    auto *home = new QAction(tr7("Control Panel Home"));
    QObject::connect(home, &QAction::triggered, home, [nav] {
        nav.home();
    });
    auto *sidebar = new Aero::Sidebar(home, 200, nullptr, true);

    auto elevated = [sidebar](const QString &text, std::function<void()> fn) {
        auto *a = new QAction(text, sidebar);
        // the Windows 7 blue/yellow shield: our own picture (the style's SP_VistaShield is empty with Kvantum on X11)
        a->setIcon(QIcon::fromTheme(QStringLiteral("seven-cp-shield"), qApp->style()->standardIcon(QStyle::SP_VistaShield)));
        QObject::connect(a, &QAction::triggered, a, fn);
        sidebar->addAction(a);
    };
    // Device Manager is an MMC window of its own, System Properties a dialog (as in Windows 7)
    elevated(tr7("Device Manager"), [] {
        launchWin7Tool(QStringLiteral("seven-devmgr"), QStringLiteral("kinfocenter"));
    });
    auto sysprops = [](int tab) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--sysprops"), QString::number(tab)});
    };
    elevated(tr7("System protection"), [sysprops] {
        sysprops(3);
    });
    elevated(tr7("Advanced system settings"), [sysprops] {
        sysprops(2);
    });

    auto seeAlso = [sidebar](const QString &text, std::function<void()> fn) {
        auto *a = new QAction(text, sidebar);
        QObject::connect(a, &QAction::triggered, a, fn);
        sidebar->addSeeAlso(a);
    };
    seeAlso(tr7("Action Center"), [nav] {
        nav.open(nav.actionCenter);
    });
    seeAlso(tr7("Windows Update"), [nav] {
        nav.open(nav.windowsUpdate);
    });
    seeAlso(tr7("Performance Information and Tools"), [nav] {
        nav.open(Win7Perf::PerfView);
    });
    // libAeroQt only ships the English caption
    for (QLabel *l : sidebar->findChildren<QLabel *>()) {
        if (l->text() == QLatin1String("See also")) {
            l->setText(tr7("See also"));
        }
    }
    return sidebar;
}

inline QWidget *makeInfo(const Info &info, const Nav &nav)
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("win7page"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));

    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(20, 12, 16, 16);
    v->setSpacing(4);

    // Title + "?"
    auto *top = new QHBoxLayout;
    auto *title = new QLabel(tr7("View basic information about your computer"));
    QFont tf = title->font();
    tf.setPixelSize(16);
    title->setFont(tf);
    title->setStyleSheet(QStringLiteral("color: #003399;"));
    top->addWidget(title, 1, Qt::AlignTop);
    auto *help = new QToolButton;
    help->setIcon(QIcon::fromTheme(QStringLiteral("seven-mmc-help"), QIcon::fromTheme(QStringLiteral("browser-help"))));
    help->setAutoRaise(true);
    help->setToolTip(tr7("Get help"));
    QObject::connect(help, &QToolButton::clicked, [] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--help-support")});
    });
    top->addWidget(help, 0, Qt::AlignTop);
    v->addLayout(top);
    v->addSpacing(8);

    auto grid = [](QVBoxLayout *into) {
        auto *g = new QGridLayout;
        g->setContentsMargins(14, 6, 0, 10);
        g->setHorizontalSpacing(18);
        g->setVerticalSpacing(8);
        g->setColumnMinimumWidth(0, 170);
        g->setColumnStretch(1, 1);
        into->addLayout(g);
        return g;
    };

    // --- Edition, with the big Windows logo on the right ---
    auto *editionRow = new QHBoxLayout;
    auto *editionCol = new QVBoxLayout;
    editionCol->addWidget(new SectionHeader(tr7("Windows edition")));
    auto *ed = new QVBoxLayout;
    ed->setContentsMargins(14, 6, 0, 10);
    ed->setSpacing(8);
    ed->addWidget(value(info.edition));
    ed->addWidget(value(info.subtitle));
    ed->addWidget(value(info.kernel));
    editionCol->addLayout(ed);
    editionCol->addStretch(1);
    editionRow->addLayout(editionCol, 1);
    auto *logo = new QLabel;
    QPixmap pm(info.logo);
    if (pm.isNull()) {
        pm = QIcon::fromTheme(QStringLiteral("start-here")).pixmap(128, 128);
    }
    logo->setPixmap(pm.scaled(118, 118, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    logo->setContentsMargins(10, 14, 16, 0);
    editionRow->addWidget(logo);
    v->addLayout(editionRow);

    // --- System ---
    v->addWidget(new SectionHeader(tr7("System")));
    QGridLayout *sys = grid(v);
    const QList<QPair<QString, QString>> sysRows{{tr7("Processor:"), info.cpu},
                                                 {tr7("Installed memory (RAM):"), info.memory},
                                                 {tr7("System type:"), info.systemType},
                                                 {tr7("Pen and Touch:"), info.touch}};
    // "Rating: [7,1] Windows Experience Index" (scores of seven-control's Performance Information and Tools)
    {
        QSettings wei(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/seven-weirc"), QSettings::IniFormat);
        double base = 0;
        if (wei.contains(QStringLiteral("LastUpdate"))) {
            base = 7.9;
            for (const char *k : {"Processor", "Memory", "Graphics", "Gaming", "Disk"}) {
                base = qMin(base, wei.value(QStringLiteral("Scores/") + QLatin1String(k)).toDouble());
            }
        }
        auto *row = new QHBoxLayout;
        row->setSpacing(6);
        auto open = [nav] {
            nav.open(Win7Perf::PerfView);
        };
        if (base > 0) {
            // the original WinSATAPI badge (bitmap 221) with the score drawn on top
            QPixmap pm(QDir::homePath() + QStringLiteral("/.local/share/seven-control/wei/badge-small.png"));
            if (pm.isNull()) {
                pm = QPixmap(32, 24);
                pm.fill(QColor(0x1c, 0x7c, 0xd5));
            }
            {
                QPainter p(&pm);
                p.setRenderHint(QPainter::Antialiasing);
                QFont f = QApplication::font();
                f.setBold(true);
                f.setPixelSize(14);
                const QString text = QLocale{}.toString(base, 'f', 1);
                const QFontMetricsF fm(f);
                QPainterPath path;
                path.addText(QPointF((pm.width() - fm.horizontalAdvance(text)) / 2.0, (pm.height() + fm.ascent() - fm.descent()) / 2.0), f, text);
                p.setPen(QPen(QColor(0x0a, 0x3a, 0x6e, 150), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawPath(path);
                p.setPen(Qt::NoPen);
                p.setBrush(Qt::white);
                p.drawPath(path);
            }
            auto *badge = new QLabel;
            badge->setPixmap(pm);
            badge->setFixedSize(pm.size());
            row->addWidget(badge);
            auto *link = new Link(tr7("Windows Experience Index"), open);
            link->setWordWrap(false);
            row->addWidget(link);
        } else {
            auto *link = new Link(tr7("System rating is not available"), open);
            link->setWordWrap(false);
            row->addWidget(link);
        }
        row->addStretch(1);
        sys->addWidget(grey(tr7("Rating:")), 0, 0, Qt::AlignVCenter);
        sys->addLayout(row, 0, 1);
    }
    for (int r = 0; r < sysRows.size(); ++r) {
        sys->addWidget(grey(sysRows[r].first), r + 1, 0, Qt::AlignTop);
        sys->addWidget(value(sysRows[r].second), r + 1, 1, Qt::AlignTop);
    }

    // --- Computer name, domain and workgroup settings ---
    v->addWidget(new SectionHeader(tr7("Computer name, domain, and workgroup settings")));
    QGridLayout *name = grid(v);
    const QList<QPair<QString, QString>> nameRows{{tr7("Computer name:"), info.computer},
                                                  {tr7("Full computer name:"), info.fullName},
                                                  {tr7("Computer description:"), info.description},
                                                  {tr7("Workgroup:"), info.workgroup}};
    for (int r = 0; r < nameRows.size(); ++r) {
        name->addWidget(grey(nameRows[r].first), r, 0, Qt::AlignTop);
        name->addWidget(value(nameRows[r].second), r, 1, Qt::AlignTop);
    }
    auto *change = new QWidget;
    auto *cl = new QHBoxLayout(change);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(4);
    auto *shield = new QLabel;
    shield->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cp-shield"), qApp->style()->standardIcon(QStyle::SP_VistaShield)).pixmap(16, 16));
    cl->addWidget(shield, 0, Qt::AlignTop);
    cl->addWidget(new Link(tr7("Change\nsettings"), [] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--sysprops"), QStringLiteral("0")});
    }));
    name->addWidget(change, 0, 2, 2, 1, Qt::AlignTop | Qt::AlignRight);

    v->addStretch(1);
    return page;
}

/** the whole page: side pane and the information */
inline QWidget *buildPage(const Nav &nav)
{
    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("win7page"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    root->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *h = new QHBoxLayout(root);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(makeSidebar(nav));
    h->addWidget(makeInfo(collect(), nav), 1);
    return root;
}
}
