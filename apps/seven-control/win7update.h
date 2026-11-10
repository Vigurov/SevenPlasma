/*
 * Control Panel\System and Security\Windows Update (wucltux.dll), working through PackageKit like Discover:
 * check for updates, choose and install them, hide them, the update history (apt's history.log) and the
 * automatic update settings (APT::Periodic / unattended-upgrades, written with pkexec).
 * Colours of the status box come from wucltux.dll's layout (UIFILE 123).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7chrome.h"
#include "win7display.h"
#include "win7programs.h"

#include <PackageKit/Daemon>
#include <PackageKit/Details>
#include <PackageKit/Transaction>

#include <QCheckBox>
#include <QDBusPendingCallWatcher>
#include <QMenu>
#include <QRegularExpression>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTreeWidget>

namespace Win7Update
{

enum {
    UpdateView = 72,
    SelectView = 73,
    SettingsView = 74,
    HistoryView = 75,
    HiddenView = 76,
};

struct Nav {
    std::function<void(int)> open;
    std::function<void()> home;
    std::function<void()> back;
};

/* ---------------- state shared by the pages ---------------- */

struct Update {
    QString id; // PackageKit package id
    QString name;
    QString version;
    QString summary;
    bool important = true;
    bool selected = true;
    qulonglong size = 0;
};

inline QSettings &store()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-updaterc"), QSettings::IniFormat);
    return s;
}

inline QStringList hiddenNames()
{
    return store().value(QStringLiteral("Hidden/Packages")).toStringList();
}

/** Lives as long as the Control Panel: the update list, the running transaction and its progress */
class Engine : public QObject
{
public:
    enum State { Idle, Checking, Installing, Failed, Done };

    static Engine &self()
    {
        static Engine e;
        return e;
    }

    QList<Update> updates; // without the hidden ones
    QList<Update> hidden;
    State state = Idle;
    QString error;
    QString progressText;
    int percent = -1;
    bool restartNeeded = false;
    bool loaded = false;
    int installedNow = 0;
    QList<std::function<void()>> listeners;

    void notify()
    {
        for (const auto &f : std::as_const(listeners)) {
            f();
        }
    }

    /** the cached list, no network: what Windows shows when the page opens */
    void load()
    {
        if (state == Checking || state == Installing) {
            return;
        }
        auto *list = new QList<Update>;
        PackageKit::Transaction *t = PackageKit::Daemon::getUpdates();
        connect(t, &PackageKit::Transaction::package, this, [list](PackageKit::Transaction::Info info, const QString &id, const QString &summary) {
            Update u;
            u.id = id;
            u.name = PackageKit::Transaction::packageName(id);
            u.version = PackageKit::Transaction::packageVersion(id);
            u.summary = summary;
            // Windows' important vs. optional: security fixes and ordinary fixes are important
            u.important = info != PackageKit::Transaction::InfoEnhancement && info != PackageKit::Transaction::InfoLow;
            u.selected = u.important;
            list->append(u);
        });
        connect(t, &PackageKit::Transaction::finished, this, [this, list](PackageKit::Transaction::Exit exit) {
            if (exit == PackageKit::Transaction::ExitSuccess) {
                setList(*list);
            }
            delete list;
            loaded = true;
            notify();
            fetchSizes();
        });
    }

    void check()
    {
        if (state == Checking || state == Installing) {
            return;
        }
        state = Checking;
        error.clear();
        percent = -1;
        progressText = tr7("Checking for updates...");
        notify();
        PackageKit::Transaction *t = PackageKit::Daemon::refreshCache(false);
        watchProgress(t);
        connect(t, &PackageKit::Transaction::errorCode, this, [this](PackageKit::Transaction::Error, const QString &details) {
            error = details;
        });
        connect(t, &PackageKit::Transaction::finished, this, [this](PackageKit::Transaction::Exit exit) {
            state = exit == PackageKit::Transaction::ExitSuccess ? Idle : Failed;
            if (state == Failed && error.isEmpty()) {
                error = tr7("An error occurred while checking for updates.");
            }
            store().setValue(QStringLiteral("State/LastCheck"), QDateTime::currentDateTime());
            notify();
            if (state == Idle) {
                load();
            }
        });
    }

    void install(const QStringList &ids)
    {
        if (ids.isEmpty() || state == Installing || state == Checking) {
            return;
        }
        state = Installing;
        error.clear();
        percent = 0;
        installedNow = 0;
        progressText = tr7("Preparing to install...");
        notify();
        PackageKit::Transaction *t = PackageKit::Daemon::updatePackages(ids);
        watchProgress(t);
        const int total = int(ids.size());
        connect(t, &PackageKit::Transaction::package, this, [this, total](PackageKit::Transaction::Info info, const QString &id, const QString &) {
            if (info == PackageKit::Transaction::InfoUpdating || info == PackageKit::Transaction::InfoInstalling) {
                ++installedNow;
                progressText = tr7("Installing update %1 of %2... (%3)").arg(qMin(installedNow, total)).arg(total).arg(PackageKit::Transaction::packageName(id));
                notify();
            } else if (info == PackageKit::Transaction::InfoDownloading) {
                progressText = tr7("Downloading updates... (%1)").arg(PackageKit::Transaction::packageName(id));
                notify();
            }
        });
        connect(t, &PackageKit::Transaction::requireRestart, this, [this](PackageKit::Transaction::Restart r) {
            if (r == PackageKit::Transaction::RestartSystem || r == PackageKit::Transaction::RestartSecuritySystem) {
                restartNeeded = true;
            }
        });
        connect(t, &PackageKit::Transaction::errorCode, this, [this](PackageKit::Transaction::Error, const QString &details) {
            error = details;
        });
        connect(t, &PackageKit::Transaction::finished, this, [this](PackageKit::Transaction::Exit exit) {
            state = exit == PackageKit::Transaction::ExitSuccess ? Done : (exit == PackageKit::Transaction::ExitCancelled ? Idle : Failed);
            if (state == Failed && error.isEmpty()) {
                error = tr7("The updates could not be installed.");
            }
            if (QFile::exists(QStringLiteral("/var/run/reboot-required"))) {
                restartNeeded = true;
            }
            m_running = nullptr;
            notify();
            load();
        });
        m_running = t;
    }

    void stop()
    {
        if (m_running) {
            m_running->cancel();
        }
    }

    void setHidden(const QStringList &names)
    {
        store().setValue(QStringLiteral("Hidden/Packages"), names);
        store().sync();
        // unattended-upgrades must leave them alone too, or "hidden" would mean nothing
        QString conf = QStringLiteral("// written by seven-control: updates hidden in Windows Update\n");
        if (!names.isEmpty()) {
            conf += QStringLiteral("Unattended-Upgrade::Package-Blacklist {\n");
            for (const QString &n : names) {
                conf += QStringLiteral("    \"%1$\";\n").arg(QRegularExpression::escape(n));
            }
            conf += QStringLiteral("};\n");
        }
        rootWrite(QStringLiteral("/etc/apt/apt.conf.d/53seven-hidden-updates"), names.isEmpty() ? QString() : conf);
        QList<Update> all = updates + hidden;
        setList(all);
        notify();
    }

    /** writes (or with empty text removes) a file as root; true when done */
    static bool rootWrite(const QString &path, const QString &text)
    {
        QProcess p;
        if (text.isEmpty()) {
            p.start(QStringLiteral("pkexec"), {QStringLiteral("rm"), QStringLiteral("-f"), path});
        } else {
            p.start(QStringLiteral("pkexec"), {QStringLiteral("tee"), path});
            p.waitForStarted();
            p.write(text.toUtf8());
            p.closeWriteChannel();
        }
        p.waitForFinished(120000);
        return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    }

    QDateTime lastCheck() const
    {
        return m_lastCheck;
    }

private:
    Engine()
    {
        // when apt (or Discover) last refreshed the lists
        auto reply = PackageKit::Daemon::getTimeSinceAction(PackageKit::Transaction::RoleRefreshCache);
        auto *w = new QDBusPendingCallWatcher(reply, this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
            QDBusPendingReply<uint> r = *call;
            const QDateTime stored = store().value(QStringLiteral("State/LastCheck")).toDateTime();
            if (!r.isError() && r.value() > 0 && r.value() < 10u * 365 * 24 * 3600) {
                m_lastCheck = QDateTime::currentDateTime().addSecs(-qint64(r.value()));
            }
            if (stored.isValid() && (!m_lastCheck.isValid() || stored > m_lastCheck)) {
                m_lastCheck = stored;
            }
            call->deleteLater();
            notify();
        });
    }

    void watchProgress(PackageKit::Transaction *t)
    {
        connect(t, &PackageKit::Transaction::percentageChanged, this, [this, t] {
            const uint p = t->percentage();
            percent = p <= 100 ? int(p) : -1;
            notify();
        });
    }

    void setList(const QList<Update> &all)
    {
        const QStringList hide = hiddenNames();
        updates.clear();
        hidden.clear();
        for (const Update &u : all) {
            (hide.contains(u.name) ? hidden : updates).append(u);
        }
    }

    void fetchSizes()
    {
        QStringList ids;
        for (const Update &u : std::as_const(updates)) {
            ids << u.id;
        }
        if (ids.isEmpty()) {
            return;
        }
        PackageKit::Transaction *t = PackageKit::Daemon::getDetails(ids);
        connect(t, &PackageKit::Transaction::details, this, [this](const PackageKit::Details &d) {
            for (Update &u : updates) {
                if (u.id == d.packageId()) {
                    u.size = d.size();
                    if (!d.description().isEmpty()) {
                        u.summary = d.description();
                    }
                }
            }
        });
        connect(t, &PackageKit::Transaction::finished, this, [this] { notify(); });
    }

    QPointer<PackageKit::Transaction> m_running;
    QDateTime m_lastCheck;
};

inline QString sizeText(qulonglong bytes)
{
    if (bytes >= 1024 * 1024) {
        return tr7("%1 MB").arg(QLocale{}.toString(bytes / 1048576.0, 'f', 1));
    }
    return tr7("%1 KB").arg(qMax<qulonglong>(1, bytes / 1024));
}

inline QString whenText(const QDateTime &t)
{
    if (!t.isValid()) {
        return tr7("Never");
    }
    const QString time = t.toString(QStringLiteral("HH:mm"));
    const int days = t.date().daysTo(QDate::currentDate());
    if (days == 0) {
        return tr7("Today at %1").arg(time);
    }
    if (days == 1) {
        return tr7("Yesterday at %1").arg(time);
    }
    return tr7("%1 at %2").arg(t.toString(QStringLiteral("dd.MM.yyyy")), time);
}

inline QString countText(int n, bool important)
{
    const QString kind = important ? tr7("important") : tr7("optional");
    const int m10 = n % 10, m100 = n % 100;
    if (m10 == 1 && m100 != 11) {
        return tr7("%1 %2 update is available").arg(n).arg(kind);
    }
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) {
        return tr7("%1 %2 updates are available").arg(n).arg(kind);
    }
    return tr7("%1 %2 updates are available", "many").arg(n).arg(kind);
}

/* ---------------- update history (apt) ---------------- */

struct HistoryRow {
    QString name;
    QString status;
    QDateTime date;
};

inline QList<HistoryRow> history()
{
    QList<HistoryRow> rows;
    QStringList texts;
    QFile f(QStringLiteral("/var/log/apt/history.log"));
    if (f.open(QIODevice::ReadOnly)) {
        texts << QString::fromUtf8(f.readAll());
    }
    for (int i = 1; i <= 4; ++i) {
        QProcess z;
        z.start(QStringLiteral("zcat"), {QStringLiteral("/var/log/apt/history.log.%1.gz").arg(i)});
        z.waitForFinished(5000);
        texts << QString::fromUtf8(z.readAllStandardOutput());
    }
    for (const QString &text : std::as_const(texts)) {
        QDateTime when;
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("Start-Date: "))) {
                when = QDateTime::fromString(line.mid(12).simplified(), QStringLiteral("yyyy-MM-dd  HH:mm:ss"));
                if (!when.isValid()) {
                    when = QDateTime::fromString(line.mid(12).simplified(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                }
            } else if (line.startsWith(QLatin1String("Upgrade: "))) {
                // "pkg:arch (old, new), pkg2:arch (old, new)"
                static const QRegularExpression re(QStringLiteral("([^ ,(]+) \\(([^,)]+), ([^)]+)\\)"));
                auto it = re.globalMatch(line.mid(9));
                while (it.hasNext()) {
                    const auto m = it.next();
                    rows.append({tr7("Update for %1 (%2)").arg(m.captured(1).section(QLatin1Char(':'), 0, 0), m.captured(3)), tr7("Succeeded"), when});
                }
            }
        }
    }
    std::sort(rows.begin(), rows.end(), [](const HistoryRow &a, const HistoryRow &b) {
        return a.date > b.date;
    });
    return rows;
}

/* ---------------- automatic updates (apt) ---------------- */

struct AutoSettings {
    int mode = 0; // 0 install automatically, 1 download only, 2 check only, 3 never
    QTime time = QTime(3, 0);
    int day = 0; // 0 every day, 1..7 Sunday..Saturday
    bool recommended = true;
};

inline QString aptValue(const QString &text, const QString &key)
{
    const QRegularExpression re(QStringLiteral("%1\\s+\"([^\"]*)\"").arg(QRegularExpression::escape(key)));
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

inline AutoSettings readAuto()
{
    AutoSettings s;
    QString text;
    for (const QString &f : {QStringLiteral("/etc/apt/apt.conf.d/10periodic"), QStringLiteral("/etc/apt/apt.conf.d/20auto-upgrades")}) {
        QFile file(f);
        if (file.open(QIODevice::ReadOnly)) {
            text += QString::fromUtf8(file.readAll());
        }
    }
    const bool lists = aptValue(text, QStringLiteral("APT::Periodic::Update-Package-Lists")).toInt() > 0;
    const bool download = aptValue(text, QStringLiteral("APT::Periodic::Download-Upgradeable-Packages")).toInt() > 0;
    const bool upgrade = aptValue(text, QStringLiteral("APT::Periodic::Unattended-Upgrade")).toInt() > 0;
    s.mode = !lists ? 3 : (upgrade ? 0 : (download ? 1 : 2));
    QFile timer(QStringLiteral("/etc/systemd/system/apt-daily-upgrade.timer.d/win7.conf"));
    if (timer.open(QIODevice::ReadOnly)) {
        const QString t = QString::fromUtf8(timer.readAll());
        const QRegularExpression re(QStringLiteral("OnCalendar=(?:(Sun|Mon|Tue|Wed|Thu|Fri|Sat) )?\\*-\\*-\\* (\\d\\d):(\\d\\d)"));
        const auto m = re.match(t);
        if (m.hasMatch()) {
            static const QStringList days{QStringLiteral("Sun"), QStringLiteral("Mon"), QStringLiteral("Tue"), QStringLiteral("Wed"), QStringLiteral("Thu"), QStringLiteral("Fri"), QStringLiteral("Sat")};
            s.day = m.captured(1).isEmpty() ? 0 : int(days.indexOf(m.captured(1))) + 1;
            s.time = QTime(m.captured(2).toInt(), m.captured(3).toInt());
        }
    }
    s.recommended = !QFile::exists(QStringLiteral("/etc/apt/apt.conf.d/52seven-no-recommended"));
    return s;
}

/** One pkexec call writes all three places */
inline bool writeAuto(const AutoSettings &s)
{
    const QString lists = s.mode == 3 ? QStringLiteral("0") : QStringLiteral("1");
    const QString download = (s.mode == 0 || s.mode == 1) ? QStringLiteral("1") : QStringLiteral("0");
    const QString upgrade = s.mode == 0 ? QStringLiteral("1") : QStringLiteral("0");
    static const QStringList days{QStringLiteral("Sun"), QStringLiteral("Mon"), QStringLiteral("Tue"), QStringLiteral("Wed"), QStringLiteral("Thu"), QStringLiteral("Fri"), QStringLiteral("Sat")};
    const QString when = (s.day > 0 ? days.at(s.day - 1) + QLatin1Char(' ') : QString()) + QStringLiteral("*-*-* ") + s.time.toString(QStringLiteral("HH:mm"));
    QString script = QStringLiteral(
                         "set -e\n"
                         "cat > /etc/apt/apt.conf.d/20auto-upgrades <<'EOF'\n"
                         "APT::Periodic::Update-Package-Lists \"%1\";\n"
                         "APT::Periodic::Download-Upgradeable-Packages \"%2\";\n"
                         "APT::Periodic::Unattended-Upgrade \"%3\";\n"
                         "EOF\n"
                         "mkdir -p /etc/systemd/system/apt-daily-upgrade.timer.d\n"
                         "cat > /etc/systemd/system/apt-daily-upgrade.timer.d/win7.conf <<'EOF'\n"
                         "# Windows Update settings of seven-control\n"
                         "[Timer]\nOnCalendar=\nOnCalendar=%4\nRandomizedDelaySec=0\n"
                         "EOF\n")
                         .arg(lists, download, upgrade, when);
    // "Recommended updates": the -updates pocket next to -security in unattended-upgrades
    if (s.recommended) {
        script += QStringLiteral("rm -f /etc/apt/apt.conf.d/52seven-no-recommended\n"
                                 "cat > /etc/apt/apt.conf.d/52seven-recommended <<'EOF'\n"
                                 "Unattended-Upgrade::Allowed-Origins { \"${distro_id}:${distro_codename}-updates\"; };\n"
                                 "EOF\n");
    } else {
        script += QStringLiteral("rm -f /etc/apt/apt.conf.d/52seven-recommended\n"
                                 "echo '// recommended updates off (seven-control)' > /etc/apt/apt.conf.d/52seven-no-recommended\n");
    }
    script += QStringLiteral("systemctl daemon-reload\n");
    QProcess p;
    p.start(QStringLiteral("pkexec"), {QStringLiteral("sh"), QStringLiteral("-c"), script});
    p.waitForFinished(120000);
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

/* ---------------- widgets ---------------- */

/** The status box of wucltux: coloured strip on the left, the colour fading into white, a thin border */
class StatusBox : public QWidget
{
public:
    enum Tone { Red, Yellow, Green, Blue, Grey };
    Tone tone = Blue;

    StatusBox()
    {
        setAttribute(Qt::WA_StyledBackground, false);
        setMinimumHeight(110);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        static const QColor fade[] = {QColor(251, 130, 129), QColor(252, 236, 100), QColor(175, 233, 176), QColor(145, 203, 211), QColor(200, 205, 212)};
        static const QPair<QColor, QColor> strip[] = {{QColor(172, 1, 0), QColor(222, 1, 0)},     {QColor(242, 177, 0), QColor(255, 206, 73)}, {QColor(22, 118, 20), QColor(66, 179, 62)},
                                                      {QColor(7, 63, 110), QColor(20, 120, 153)}, {QColor(133, 143, 157), QColor(166, 172, 179)}};
        QPainter p(this);
        const QRect r = rect().adjusted(0, 0, -1, -1);
        QLinearGradient bg(r.topLeft(), QPointF(r.left() + r.width() * 0.55, r.top()));
        bg.setColorAt(0, fade[tone].lighter(118));
        bg.setColorAt(1, Qt::white);
        p.fillRect(r, bg);
        QLinearGradient band(r.topLeft(), r.bottomLeft());
        band.setColorAt(0, strip[tone].first);
        band.setColorAt(1, strip[tone].second);
        p.fillRect(QRect(r.left(), r.top(), 11, r.height()), band);
        p.setPen(QColor(0xb9, 0xc3, 0xcd));
        p.drawRect(r);
    }
};

inline QLabel *grey(const QString &t)
{
    auto *l = new QLabel(t);
    l->setStyleSheet(QStringLiteral("color: #5a6779;"));
    return l;
}

inline QWidget *page(const QList<Win7Display::SideItem> &links, const QList<Win7Display::SideItem> &seeAlso, const Nav &nav, QWidget *content)
{
    return Win7Display::pageWithSide(Win7Display::sidePane(nav.home, links, seeAlso), content);
}

inline QList<Win7Display::SideItem> sideLinks(const Nav &nav)
{
    return {{tr7("Check for updates", "button"), [] { Engine::self().check(); }},
            {tr7("Change settings"), [nav] { nav.open(SettingsView); }},
            {tr7("View update history"), [nav] { nav.open(HistoryView); }},
            {tr7("Restore hidden updates"), [nav] { nav.open(HiddenView); }}};
}

inline QList<Win7Display::SideItem> seeAlso(const Nav &nav)
{
    return {{tr7("Installed Updates"), [nav] { nav.open(Win7Programs::UpdatesView); }}};
}

/** "Windows Update": the status box and the three lines under it */
class MainPage : public QWidget
{
public:
    explicit MainPage(Nav nav)
        : m_nav(std::move(nav))
    {
        auto *content = new QWidget;
        auto *v = new QVBoxLayout(content);
        v->setContentsMargins(32, 18, 30, 18);
        v->setSpacing(14);
        v->addWidget(Win7Programs::pageTitle(tr7("Windows Update")));
        m_box = new StatusBox;
        m_box->setMaximumWidth(640);
        auto *bh = new QHBoxLayout(m_box);
        bh->setContentsMargins(24, 14, 14, 14);
        bh->setSpacing(14);
        m_icon = new QLabel;
        m_icon->setFixedSize(48, 48);
        bh->addWidget(m_icon, 0, Qt::AlignTop);
        auto *left = new QVBoxLayout;
        m_title = new QLabel;
        QFont tf = m_title->font();
        tf.setPixelSize(15);
        m_title->setFont(tf);
        m_title->setWordWrap(true);
        left->addWidget(m_title);
        m_text = new QLabel;
        m_text->setWordWrap(true);
        left->addWidget(m_text);
        m_progress = new QProgressBar;
        m_progress->setTextVisible(false);
        m_progress->setFixedHeight(15);
        left->addWidget(m_progress);
        left->addStretch(1);
        bh->addLayout(left, 1);
        // the right column: "N important updates are available", the optional ones, the button
        auto *right = new QVBoxLayout;
        right->setSpacing(4);
        m_importantLink = new Link(QString(), [this] {
            store().setValue(QStringLiteral("State/Tab"), 0);
            m_nav.open(SelectView);
        });
        m_optionalLink = new Link(QString(), [this] {
            store().setValue(QStringLiteral("State/Tab"), 1);
            m_nav.open(SelectView);
        });
        QFont lf = m_importantLink->font();
        lf.setBold(true);
        m_importantLink->setFont(lf);
        right->addWidget(m_importantLink);
        right->addWidget(m_optionalLink);
        m_selected = grey(QString());
        right->addWidget(m_selected);
        right->addStretch(1);
        m_button = new QPushButton;
        m_button->setMinimumWidth(130);
        right->addWidget(m_button, 0, Qt::AlignRight);
        bh->addLayout(right);
        v->addWidget(m_box);

        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(18);
        grid->setVerticalSpacing(6);
        m_lastCheck = new QLabel;
        m_lastInstall = new QLabel;
        grid->addWidget(grey(tr7("Most recent check for updates:")), 0, 0);
        grid->addWidget(m_lastCheck, 0, 1);
        grid->addWidget(grey(tr7("Updates were installed:")), 1, 0);
        auto *installedRow = new QHBoxLayout;
        installedRow->addWidget(m_lastInstall);
        installedRow->addWidget(new Link(tr7("View update history"), [this] { m_nav.open(HistoryView); }));
        installedRow->addStretch(1);
        grid->addLayout(installedRow, 1, 1);
        grid->addWidget(grey(tr7("You receive updates:")), 2, 0);
        grid->addWidget(new QLabel(tr7("For the system and programs from the Ubuntu repositories.")), 2, 1);
        grid->setColumnStretch(2, 1);
        v->addLayout(grid);
        v->addStretch(1);

        auto *root = page(sideLinks(m_nav), seeAlso(m_nav), m_nav, content);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addWidget(root);

        connect(m_button, &QPushButton::clicked, this, [this] {
            Engine &e = Engine::self();
            if (e.state == Engine::Installing) {
                e.stop();
            } else if (e.restartNeeded && e.state == Engine::Done) {
                QProcess::startDetached(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"),
                                                                   QStringLiteral("org.kde.LogoutPrompt.promptReboot")});
            } else if (e.state == Engine::Failed) {
                e.check();
            } else {
                QStringList ids;
                for (const Update &u : std::as_const(e.updates)) {
                    if (u.selected) {
                        ids << u.id;
                    }
                }
                if (ids.isEmpty()) {
                    e.check();
                } else {
                    e.install(ids);
                }
            }
        });
        QPointer<MainPage> self(this);
        Engine::self().listeners << [self] {
            if (self) {
                self->refresh();
            }
        };
        m_lastInstallDate = history().isEmpty() ? QDateTime() : history().first().date;
        if (!Engine::self().loaded) {
            Engine::self().load();
        }
        refresh();
    }

private:
    void refresh()
    {
        Engine &e = Engine::self();
        int important = 0, optional = 0, selected = 0;
        qulonglong size = 0;
        for (const Update &u : std::as_const(e.updates)) {
            (u.important ? important : optional)++;
            if (u.selected) {
                ++selected;
                size += u.size;
            }
        }
        m_progress->setVisible(e.state == Engine::Checking || e.state == Engine::Installing);
        m_progress->setRange(0, e.percent < 0 ? 0 : 100);
        m_progress->setValue(qMax(0, e.percent));
        m_importantLink->setVisible(false);
        m_optionalLink->setVisible(false);
        m_selected->setVisible(false);
        m_button->setVisible(true);
        m_button->setIcon(QIcon());
        QString icon = QStringLiteral("seven-wu-app");
        if (e.state == Engine::Checking) {
            m_box->tone = StatusBox::Blue;
            m_title->setText(tr7("Checking for updates..."));
            m_text->setText(e.progressText == tr7("Checking for updates...") ? QString() : e.progressText);
            m_button->setVisible(false);
        } else if (e.state == Engine::Installing) {
            m_box->tone = StatusBox::Blue;
            m_title->setText(tr7("Installing updates..."));
            m_text->setText(e.progressText);
            m_button->setText(tr7("Stop installation"));
        } else if (e.state == Engine::Failed) {
            m_box->tone = StatusBox::Red;
            icon = QStringLiteral("seven-wu-shield");
            m_title->setText(tr7("Windows could not search for or install updates"));
            m_text->setText(e.error);
            m_button->setText(tr7("Try again"));
        } else if (e.restartNeeded) {
            m_box->tone = StatusBox::Yellow;
            icon = QStringLiteral("seven-wu-shield");
            m_title->setText(tr7("Restart your computer to finish installing updates"));
            m_text->setText(tr7("Save your files and close all programs before restarting."));
            m_button->setText(tr7("Restart now"));
        } else if (important > 0) {
            m_box->tone = StatusBox::Yellow;
            icon = QStringLiteral("seven-wu-shield");
            m_title->setText(tr7("Download and install updates for your computer"));
            m_text->setText(tr7("Without the latest updates the computer is more vulnerable to attacks and performance problems."));
            m_importantLink->setText(countText(important, true));
            m_importantLink->setVisible(true);
            if (optional > 0) {
                m_optionalLink->setText(countText(optional, false));
                m_optionalLink->setVisible(true);
            }
            m_selected->setText(selected ? tr7("%1 updates selected%2").arg(selected).arg(size ? QStringLiteral(", ") + sizeText(size) : QString())
                                         : tr7("No updates are selected."));
            m_selected->setVisible(true);
            m_button->setText(tr7("Install updates"));
            m_button->setIcon(QIcon::fromTheme(QStringLiteral("seven-cp-shield")));
        } else {
            m_box->tone = StatusBox::Green;
            icon = QStringLiteral("seven-wu-ok");
            m_title->setText(e.loaded ? tr7("Windows is up to date") : tr7("Checking the status of updates..."));
            m_text->setText(e.state == Engine::Done ? tr7("The updates were installed successfully.") : tr7("There are no updates available for your computer."));
            if (optional > 0) {
                m_optionalLink->setText(countText(optional, false));
                m_optionalLink->setVisible(true);
            }
            m_button->setText(tr7("Check for updates", "button"));
        }
        m_icon->setPixmap(QIcon::fromTheme(icon).pixmap(48, 48));
        m_box->update();
        m_lastCheck->setText(whenText(e.lastCheck()));
        m_lastInstall->setText(whenText(m_lastInstallDate) + QStringLiteral(". "));
    }

    Nav m_nav;
    StatusBox *m_box;
    QLabel *m_icon;
    QLabel *m_title;
    QLabel *m_text;
    QProgressBar *m_progress;
    Link *m_importantLink;
    Link *m_optionalLink;
    QLabel *m_selected;
    QPushButton *m_button;
    QLabel *m_lastCheck;
    QLabel *m_lastInstall;
    QDateTime m_lastInstallDate;
};

/** "Select updates to install": important / optional on the left, the list, the details on the right */
class SelectPage : public QWidget
{
public:
    explicit SelectPage(Nav nav)
        : m_nav(std::move(nav))
    {
        setObjectName(QStringLiteral("win7page"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(24, 18, 24, 14);
        v->setSpacing(10);
        v->addWidget(Win7Programs::pageTitle(tr7("Select the updates you want to install")));
        auto *h = new QHBoxLayout;
        h->setSpacing(0);
        // left column: the two kinds
        auto *kinds = new QWidget;
        kinds->setFixedWidth(150);
        kinds->setObjectName(QStringLiteral("win7wukinds"));
        kinds->setAttribute(Qt::WA_StyledBackground, true);
        kinds->setStyleSheet(QStringLiteral("#win7wukinds { background: #f1f5fb; border: 1px solid #d5dfe9; border-right: none; }"));
        auto *kv = new QVBoxLayout(kinds);
        kv->setContentsMargins(8, 10, 8, 10);
        m_importantBtn = new Link(QString(), [this] { setTab(0); });
        m_optionalBtn = new Link(QString(), [this] { setTab(1); });
        kv->addWidget(m_importantBtn);
        kv->addSpacing(6);
        kv->addWidget(m_optionalBtn);
        kv->addStretch(1);
        h->addWidget(kinds);
        m_list = new QTreeWidget;
        Win7HeaderView::install(m_list);
        m_list->setRootIsDecorated(false);
        m_list->setHeaderLabels({tr7("Name"), tr7("Size")});
        m_list->setColumnWidth(0, 360);
        m_list->setContextMenuPolicy(Qt::CustomContextMenu);
        h->addWidget(m_list, 1);
        m_details = new QLabel;
        m_details->setWordWrap(true);
        m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_details->setFixedWidth(230);
        m_details->setContentsMargins(12, 10, 10, 10);
        m_details->setTextFormat(Qt::RichText);
        m_details->setStyleSheet(QStringLiteral("QLabel { background: white; border: 1px solid #d5dfe9; border-left: none; }"));
        h->addWidget(m_details);
        v->addLayout(h, 1);
        auto *bottom = new QHBoxLayout;
        m_total = new QLabel;
        bottom->addWidget(m_total, 1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        for (QPushButton *b : {ok, cancel}) {
            b->setMinimumWidth(86);
            bottom->addWidget(b);
        }
        v->addLayout(bottom);
        m_work = Engine::self().updates;
        connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it) {
            const int i = it->data(0, Qt::UserRole).toInt();
            if (i >= 0 && i < m_work.size()) {
                m_work[i].selected = it->checkState(0) == Qt::Checked;
                updateTotal();
            }
        });
        connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) { showDetails(it); });
        connect(m_list, &QWidget::customContextMenuRequested, this, [this](QPoint pos) {
            QTreeWidgetItem *it = m_list->itemAt(pos);
            if (!it) {
                return;
            }
            QMenu menu;
            menu.addAction(tr7("Hide update"), this, [this, it] {
                QStringList names = hiddenNames();
                names << m_work.at(it->data(0, Qt::UserRole).toInt()).name;
                names.removeDuplicates();
                Engine::self().setHidden(names);
                m_work = Engine::self().updates;
                fill();
            });
            menu.exec(m_list->viewport()->mapToGlobal(pos));
        });
        connect(ok, &QPushButton::clicked, this, [this] {
            Engine::self().updates = m_work;
            Engine::self().notify();
            m_nav.back();
        });
        connect(cancel, &QPushButton::clicked, this, [this] { m_nav.back(); });
        m_tab = store().value(QStringLiteral("State/Tab"), 0).toInt();
        fill();
    }

private:
    void setTab(int tab)
    {
        m_tab = tab;
        fill();
    }

    void fill()
    {
        int important = 0, optional = 0;
        for (const Update &u : std::as_const(m_work)) {
            (u.important ? important : optional)++;
        }
        m_importantBtn->setText(tr7("Important: %1").arg(important));
        m_optionalBtn->setText(tr7("Optional: %1").arg(optional));
        QFont b = m_importantBtn->font();
        b.setBold(m_tab == 0);
        m_importantBtn->setFont(b);
        b.setBold(m_tab == 1);
        m_optionalBtn->setFont(b);
        m_list->blockSignals(true);
        m_list->clear();
        auto *group = new QTreeWidgetItem(m_list, {QStringLiteral("Ubuntu (%1)").arg(m_tab == 0 ? important : optional)});
        QFont gf = group->font(0);
        gf.setBold(true);
        group->setFont(0, gf);
        group->setFlags(Qt::ItemIsEnabled);
        for (int i = 0; i < m_work.size(); ++i) {
            const Update &u = m_work.at(i);
            if (u.important != (m_tab == 0)) {
                continue;
            }
            auto *it = new QTreeWidgetItem(m_list, {tr7("Update for %1 (%2)").arg(u.name, u.version), u.size ? sizeText(u.size) : QString()});
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            it->setCheckState(0, u.selected ? Qt::Checked : Qt::Unchecked);
            it->setData(0, Qt::UserRole, i);
            it->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        }
        m_list->blockSignals(false);
        showDetails(nullptr);
        updateTotal();
    }

    void showDetails(QTreeWidgetItem *it)
    {
        if (!it || it->data(0, Qt::UserRole).isNull()) {
            m_details->setText(tr7("<span style='color:#5a6779'>Select an update to see information about it.</span>"));
            return;
        }
        const Update &u = m_work.at(it->data(0, Qt::UserRole).toInt());
        m_details->setText(tr7("<b>%1</b><br><br>%2<br><br><span style='color:#5a6779'>Version: %3<br>%4</span>")
                               .arg(u.name.toHtmlEscaped(), u.summary.toHtmlEscaped().left(600), u.version.toHtmlEscaped(),
                                    u.important ? tr7("Important update") : tr7("Optional update")));
    }

    void updateTotal()
    {
        int n = 0;
        qulonglong size = 0;
        for (const Update &u : std::as_const(m_work)) {
            if (u.selected) {
                ++n;
                size += u.size;
            }
        }
        m_total->setText(n ? tr7("Total selected: %1 updates%2").arg(n).arg(size ? QStringLiteral(", ") + sizeText(size) : QString())
                           : tr7("No updates are selected."));
    }

    Nav m_nav;
    QList<Update> m_work;
    int m_tab = 0;
    Link *m_importantBtn;
    Link *m_optionalBtn;
    QTreeWidget *m_list;
    QLabel *m_details;
    QLabel *m_total;
};

/** "Change settings": Choose how Windows can install updates */
inline QWidget *buildSettingsPage(const Nav &nav)
{
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("win7page"));
    w->setAttribute(Qt::WA_StyledBackground, true);
    w->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(32, 18, 30, 16);
    v->setSpacing(10);
    v->addWidget(Win7Programs::pageTitle(tr7("Choose how Windows can install updates")));
    v->addWidget(Win7Programs::wrapped(tr7("When your computer is online, Windows can automatically check for important updates and install them using these settings. When new updates are available, you can also install them before shutting down the computer.")));
    const AutoSettings s = readAuto();
    auto section = [&](const QString &icon, const QString &title) {
        auto *t = new QLabel(title);
        t->setStyleSheet(QStringLiteral("color: #1e3287;"));
        QFont f = t->font();
        f.setPixelSize(13);
        t->setFont(f);
        v->addSpacing(8);
        v->addWidget(t);
        auto *row = new QHBoxLayout;
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(icon).pixmap(32, 32));
        ic->setFixedWidth(44);
        row->addWidget(ic, 0, Qt::AlignTop);
        auto *col = new QVBoxLayout;
        row->addLayout(col, 1);
        v->addLayout(row);
        return col;
    };
    QVBoxLayout *imp = section(QStringLiteral("seven-wu-shield"), tr7("Important updates"));
    auto *mode = new QComboBox;
    mode->addItems({tr7("Install updates automatically (recommended)"), tr7("Download updates but let me choose whether to install them"),
                    tr7("Check for updates but let me choose whether to download and install them"),
                    tr7("Never check for updates (not recommended)")});
    mode->setCurrentIndex(s.mode);
    mode->setMaximumWidth(520);
    imp->addWidget(mode);
    auto *when = new QHBoxLayout;
    auto *whenLabel = new QLabel(tr7("Install new updates:"));
    auto *day = new QComboBox;
    day->addItems({tr7("Daily"), tr7("Every Sunday"), tr7("Every Monday"), tr7("Every Tuesday"), tr7("Every Wednesday"),
                   tr7("Every Thursday"), tr7("Every Friday"), tr7("Every Saturday")});
    day->setCurrentIndex(s.day);
    auto *at = new QLabel(tr7("at"));
    auto *hour = new QComboBox;
    for (int h = 0; h < 24; ++h) {
        hour->addItem(QStringLiteral("%1:00").arg(h), h);
    }
    hour->setCurrentIndex(s.time.hour());
    when->addWidget(whenLabel);
    when->addWidget(day);
    when->addWidget(at);
    when->addWidget(hour);
    when->addStretch(1);
    imp->addLayout(when);
    auto syncWhen = [=](int m) {
        for (QWidget *x : std::initializer_list<QWidget *>{whenLabel, day, at, hour}) {
            x->setEnabled(m == 0);
        }
    };
    syncWhen(s.mode);
    QObject::connect(mode, &QComboBox::currentIndexChanged, w, syncWhen);
    QVBoxLayout *rec = section(QStringLiteral("seven-wu-recommended"), tr7("Recommended updates"));
    auto *recommended = new QCheckBox(tr7("Give me recommended updates the same way I receive important updates"));
    recommended->setChecked(s.recommended);
    rec->addWidget(recommended);
    v->addWidget(Win7Programs::wrapped(tr7("Note: Windows Update might update itself automatically first when checking for other updates.")));
    v->addStretch(1);
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
    v->addWidget(line);
    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    auto *ok = new QPushButton(QIcon::fromTheme(QStringLiteral("seven-cp-shield")), QStringLiteral("OK"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    for (QPushButton *b : {ok, cancel}) {
        b->setMinimumWidth(86);
        btns->addWidget(b);
    }
    v->addLayout(btns);
    QObject::connect(ok, &QPushButton::clicked, w, [=] {
        AutoSettings n;
        n.mode = mode->currentIndex();
        n.day = day->currentIndex();
        n.time = QTime(hour->currentData().toInt(), 0);
        n.recommended = recommended->isChecked();
        if (writeAuto(n)) {
            nav.back();
        }
    });
    QObject::connect(cancel, &QPushButton::clicked, w, [nav] { nav.back(); });
    return w;
}

/** "View update history" */
inline QWidget *buildHistoryPage(const Nav &nav)
{
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("win7page"));
    w->setAttribute(Qt::WA_StyledBackground, true);
    w->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(32, 18, 30, 16);
    v->setSpacing(10);
    v->addWidget(Win7Programs::pageTitle(tr7("Review your update history")));
    auto *intro = new QHBoxLayout;
    intro->addWidget(new QLabel(tr7("Check the Status column to ensure all important updates were successful.")));
    intro->addStretch(1);
    v->addLayout(intro);
    auto *list = new Win7Programs::DetailsTree;
    list->setHeaderLabels({tr7("Name"), tr7("Status"), tr7("Importance"), tr7("Date Installed")});
    list->setColumnWidth(0, 420);
    list->setColumnWidth(1, 90);
    list->setColumnWidth(2, 90);
    list->setColumnWidth(3, 120);
    for (const HistoryRow &r : history()) {
        auto *it = new QTreeWidgetItem(list, {r.name, r.status, tr7("Important"), r.date.toString(QStringLiteral("dd.MM.yyyy"))});
        it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-wu-important")));
    }
    v->addWidget(list, 1);
    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    ok->setMinimumWidth(86);
    btns->addWidget(ok);
    v->addLayout(btns);
    QObject::connect(ok, &QPushButton::clicked, w, [nav] { nav.back(); });
    return w;
}

/** "Restore hidden updates" */
inline QWidget *buildHiddenPage(const Nav &nav)
{
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("win7page"));
    w->setAttribute(Qt::WA_StyledBackground, true);
    w->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(32, 18, 30, 16);
    v->setSpacing(10);
    v->addWidget(Win7Programs::pageTitle(tr7("Restore Hidden Updates")));
    v->addWidget(Win7Programs::wrapped(tr7("Select the updates you want to restore. Windows will offer them again and install them automatically.")));
    auto *list = new Win7Programs::DetailsTree;
    list->setHeaderLabels({tr7("Name")});
    list->setColumnWidth(0, 520);
    const QStringList names = hiddenNames();
    for (const QString &n : names) {
        auto *it = new QTreeWidgetItem(list, {n});
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        it->setCheckState(0, Qt::Unchecked);
    }
    if (names.isEmpty()) {
        new QTreeWidgetItem(list, {tr7("There are no hidden updates.")});
    }
    v->addWidget(list, 1);
    auto *btns = new QHBoxLayout;
    btns->addStretch(1);
    auto *restore = new QPushButton(QIcon::fromTheme(QStringLiteral("seven-cp-shield")), tr7("Restore"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    restore->setEnabled(!names.isEmpty());
    for (QPushButton *b : {restore, cancel}) {
        b->setMinimumWidth(86);
        btns->addWidget(b);
    }
    v->addLayout(btns);
    QObject::connect(restore, &QPushButton::clicked, w, [list, names, nav] {
        QStringList keep;
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            if (list->topLevelItem(i)->checkState(0) != Qt::Checked) {
                keep << names.value(i);
            }
        }
        Engine::self().setHidden(keep);
        nav.back();
    });
    QObject::connect(cancel, &QPushButton::clicked, w, [nav] { nav.back(); });
    return w;
}

inline bool isUpdateView(int v)
{
    return v >= UpdateView && v <= HiddenView;
}

inline QString viewTitle(int v)
{
    switch (v) {
    case SelectView:
        return tr7("Select updates to install");
    case SettingsView:
        return tr7("Change settings", "update");
    case HistoryView:
        return tr7("Review your update history");
    case HiddenView:
        return tr7("Restore Hidden Updates");
    default:
        return tr7("Windows Update");
    }
}

inline QWidget *build(int view, const Nav &nav)
{
    switch (view) {
    case SelectView:
        return new SelectPage(nav);
    case SettingsView:
        return buildSettingsPage(nav);
    case HistoryView:
        return buildHistoryPage(nav);
    case HiddenView:
        return buildHiddenPage(nav);
    default:
        return new MainPage(nav);
    }
}

} // namespace Win7Update
