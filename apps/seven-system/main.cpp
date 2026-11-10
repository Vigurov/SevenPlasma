/*
 * "System" window in the style of Windows 7 (Control Panel > System),
 * showing the real information of this Linux machine.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "win7chrome.h"
#include <QTimer>

#include <AeroQt/insetwindow.h>
#include <AeroQt/navbuttons.h>
#include <AeroQt/sidebar.h>
#include <AeroQt/stylesheet.h>

#include <KConfig>
#include <KConfigGroup>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHostInfo>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStyle>
#include <QSysInfo>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <functional>

namespace
{
/* ---------------- System information ---------------- */

struct Info {
    QString edition, subtitle, kernel, logo;
    QString cpu, memory, systemType, touch;
    QString computer, fullName, description, workgroup;
};

Info collect()
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
    i.kernel = QStringLiteral("Ядро Linux ") + QSysInfo::kernelVersion();

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
        i.memory += QStringLiteral(" (%1 доступно)").arg(gb(totalGiB));
    }

    i.systemType = QSysInfo::WordSize == 64 ? QStringLiteral("64-розрядна операційна система") : QStringLiteral("32-розрядна операційна система");

    const QString inputs = readFile(QStringLiteral("/proc/bus/input/devices"));
    const bool touch = inputs.contains(QStringLiteral("Touchscreen"), Qt::CaseInsensitive) || inputs.contains(QStringLiteral(" Pen"), Qt::CaseInsensitive);
    i.touch = touch ? QStringLiteral("Доступна підтримка пера і сенсорного введення") : QStringLiteral("Перо і сенсорне введення недоступні для цього екрана");

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

/* ---------------- Window ---------------- */

QWidget *makeHeader(QWidget *parent, QToolButton **refreshOut)
{
    auto openControlPanel = [] {
        launchWin7Tool(QStringLiteral("seven-control"), QStringLiteral("systemsettings"));
    };
    auto openAllItems = [] {
        const QString exe = QStandardPaths::findExecutable(QStringLiteral("seven-control"), {QDir::homePath() + QStringLiteral("/.local/bin")});
        launch(exe.isEmpty() ? QStringLiteral("systemsettings") : exe, exe.isEmpty() ? QStringList() : QStringList{QStringLiteral("--all")});
    };
    Win7Header h = makeWin7Header(parent,
                                  {{QStringLiteral("Панель керування"), openControlPanel},
                                   {QStringLiteral("Усі елементи панелі керування"), openAllItems},
                                   {QStringLiteral("Система"), nullptr}},
                                  QStringLiteral("preferences-system"),
                                  QStringLiteral("Пошук у панелі керування"));
    QObject::connect(h.history, &QToolButton::clicked, h.history, [btn = h.history, openControlPanel] {
        QMenu menu(btn);
        menu.addAction(QIcon::fromTheme(QStringLiteral("preferences-system")), QStringLiteral("Панель керування"), openControlPanel);
        menu.exec(btn->mapToGlobal(QPoint(0, btn->height())));
    });
    QObject::connect(h.search, &QLineEdit::returnPressed, h.search, openControlPanel);
    *refreshOut = h.refresh;
    return h.widget;
}

Aero::Sidebar *makeSidebar()
{
    auto *home = new QAction(QStringLiteral("Панель керування — домашня сторінка"));
    QObject::connect(home, &QAction::triggered, [] {
        launchWin7Tool(QStringLiteral("seven-control"), QStringLiteral("systemsettings"));
    });
    auto *sidebar = new Aero::Sidebar(home, 200, nullptr, false);

    auto elevated = [sidebar](const QString &text, std::function<void()> fn) {
        auto *a = new QAction(text, sidebar);
        a->setProperty("elevatedPriv", true); // Windows 7 blue/yellow shield
        QObject::connect(a, &QAction::triggered, a, fn);
        sidebar->addAction(a);
    };
    elevated(QStringLiteral("Диспетчер пристроїв"), [] {
        const QString exe = QStandardPaths::findExecutable(QStringLiteral("seven-devmgr"), {QDir::homePath() + QStringLiteral("/.local/bin")});
        launch(exe.isEmpty() ? QStringLiteral("kinfocenter") : exe);
    });
    // System Properties of seven-control (the Remote tab has no counterpart here)
    auto sysprops = [](int tab) {
        launch(QDir::homePath() + QStringLiteral("/.local/bin/seven-control"), {QStringLiteral("--sysprops"), QString::number(tab)});
    };
    elevated(QStringLiteral("Захист системи"), [sysprops] {
        sysprops(3);
    });
    elevated(QStringLiteral("Додаткові параметри системи"), [sysprops] {
        sysprops(2);
    });

    auto seeAlso = [sidebar](const QString &text, std::function<void()> fn) {
        auto *a = new QAction(text, sidebar);
        QObject::connect(a, &QAction::triggered, a, fn);
        sidebar->addSeeAlso(a);
    };
    seeAlso(QStringLiteral("Центр підтримки"), [] {
        launch(QStringLiteral("khelpcenter"));
    });
    seeAlso(QStringLiteral("Центр оновлення"), [] {
        launch(QStringLiteral("systemsettings"), {QStringLiteral("kcm_updates")});
    });
    seeAlso(QStringLiteral("Диспетчер задач"), [] {
        launchWin7Tool(QStringLiteral("seven-taskmgr"), QStringLiteral("plasma-systemmonitor"));
    });

    // libAeroQt only ships the English caption
    for (QLabel *l : sidebar->findChildren<QLabel *>()) {
        if (l->text() == QLatin1String("See also")) {
            l->setText(QStringLiteral("Див. також"));
        }
    }
    return sidebar;
}

QWidget *makePage(const Info &info)
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
    auto *title = new QLabel(QStringLiteral("Перегляд основних відомостей про ваш комп'ютер"));
    QFont tf = title->font();
    tf.setPixelSize(16);
    title->setFont(tf);
    title->setStyleSheet(QStringLiteral("color: #003399;"));
    top->addWidget(title, 1, Qt::AlignTop);
    auto *help = new QToolButton;
    help->setIcon(QIcon::fromTheme(QStringLiteral("browser-help"), QIcon::fromTheme(QStringLiteral("help-contents"))));
    help->setAutoRaise(true);
    help->setToolTip(QStringLiteral("Отримати довідку"));
    QObject::connect(help, &QToolButton::clicked, [] {
        launch(QStringLiteral("khelpcenter"));
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
    editionCol->addWidget(new SectionHeader(QStringLiteral("Випуск Windows")));
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
    v->addWidget(new SectionHeader(QStringLiteral("Система")));
    QGridLayout *sys = grid(v);
    const QList<QPair<QString, QString>> sysRows{{QStringLiteral("Процесор:"), info.cpu},
                                                 {QStringLiteral("Встановлена пам'ять (ОЗП):"), info.memory},
                                                 {QStringLiteral("Тип системи:"), info.systemType},
                                                 {QStringLiteral("Перо і сенсорне введення:"), info.touch}};
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
        auto open = [] {
            launch(QDir::homePath() + QStringLiteral("/.local/bin/seven-control"), {QStringLiteral("--perf")});
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
                const QString text = QLocale(QLocale::Ukrainian).toString(base, 'f', 1);
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
            auto *link = new Link(QStringLiteral("Індекс продуктивності Windows"), open);
            link->setWordWrap(false);
            row->addWidget(link);
        } else {
            auto *link = new Link(QStringLiteral("Індекс продуктивності Windows для системи ще не визначено"), open);
            link->setWordWrap(false);
            row->addWidget(link);
        }
        row->addStretch(1);
        sys->addWidget(grey(QStringLiteral("Оцінка:")), 0, 0, Qt::AlignVCenter);
        sys->addLayout(row, 0, 1);
    }
    for (int r = 0; r < sysRows.size(); ++r) {
        sys->addWidget(grey(sysRows[r].first), r + 1, 0, Qt::AlignTop);
        sys->addWidget(value(sysRows[r].second), r + 1, 1, Qt::AlignTop);
    }

    // --- Computer name, domain and workgroup settings ---
    v->addWidget(new SectionHeader(QStringLiteral("Ім'я комп'ютера, домену та параметри робочої групи")));
    QGridLayout *name = grid(v);
    const QList<QPair<QString, QString>> nameRows{{QStringLiteral("Комп'ютер:"), info.computer},
                                                  {QStringLiteral("Повне ім'я:"), info.fullName},
                                                  {QStringLiteral("Опис:"), info.description},
                                                  {QStringLiteral("Робоча група:"), info.workgroup}};
    for (int r = 0; r < nameRows.size(); ++r) {
        name->addWidget(grey(nameRows[r].first), r, 0, Qt::AlignTop);
        name->addWidget(value(nameRows[r].second), r, 1, Qt::AlignTop);
    }
    auto *change = new QWidget;
    auto *cl = new QHBoxLayout(change);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(4);
    auto *shield = new QLabel;
    shield->setPixmap(qApp->style()->standardIcon(QStyle::SP_VistaShield).pixmap(16, 16));
    cl->addWidget(shield, 0, Qt::AlignTop);
    cl->addWidget(new Link(QStringLiteral("Змінити\nпараметри"), [] {
        launch(QDir::homePath() + QStringLiteral("/.local/bin/seven-control"), {QStringLiteral("--sysprops"), QStringLiteral("0")});
    }));
    name->addWidget(change, 0, 2, 2, 1, Qt::AlignTop | Qt::AlignRight);

    v->addStretch(1);
    return page;
}

class SystemWindow : public QMainWindow
{
public:
    SystemWindow()
    {
        setWindowTitle(QStringLiteral("Система"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("computer")));

        auto *central = new QWidget(this);
        auto *h = new QHBoxLayout(central);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);
        h->addWidget(makeSidebar());

        m_scroll = new QScrollArea(central);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidgetResizable(true);
        m_scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: #ffffff; }"));
        m_scroll->setWidget(makePage(collect()));
        h->addWidget(m_scroll, 1);
        setCentralWidget(central);

        QToolButton *refresh = nullptr;
        QWidget *header = makeHeader(this, &refresh);
        connect(refresh, &QToolButton::clicked, this, [this] {
            m_scroll->setWidget(makePage(collect()));
        });
        Aero::makeInsetWindow(this, nullptr, header, nullptr);
        resize(860, 560);
    }

private:
    QScrollArea *m_scroll;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("seven-system"));
    app.setApplicationDisplayName(QStringLiteral("Система"));
    app.setDesktopFileName(QStringLiteral("seven-system"));
    Aero::registerStylesheet(&app);

    SystemWindow w;
    w.show();
    // testing: WIN7_GRAB=<file.png> saves the window and quits
    if (!qEnvironmentVariableIsEmpty("WIN7_GRAB")) {
        QTimer::singleShot(2000, &app, [] {
            for (QWidget *t : QApplication::topLevelWidgets()) {
                if (t->isVisible() && t->width() > 300) {
                    t->grab().save(qEnvironmentVariable("WIN7_GRAB"));
                }
            }
            QApplication::quit();
        });
    }
    return app.exec();
}
