/*
 * Control Panel\System and Security\Action Center (ActionCenterCPL.dll): the Security and Maintenance groups with
 * their messages and status rows, the message settings and the archived problem reports.
 * Everything shown is read from the system: ufw, the apt periodic settings, ClamAV, apport's crash reports in
 * /var/crash, the Backup and Restore settings of this Control Panel. Turned-off messages live in seven-actioncenterrc.
 * The expand/collapse buttons and the status orbs are ActionCenterCPL.dll's own bitmaps (seven-aero/ac-100N.png).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7backup.h"
#include "win7firewall.h"
#include "win7header.h"
#include "win7update.h"

#include <QUrl>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>

namespace Win7Action
{

enum {
    CenterView = 77,
    SettingsView = 78,
    ArchiveView = 79,
};

struct Nav {
    std::function<void(int)> open;
    std::function<void()> home;
    std::function<void()> back;
};

inline QSettings &store()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-actioncenterrc"), QSettings::IniFormat);
    return s;
}

/* the message kinds of the settings page: key, name in "Turn off messages about …" */
struct Kind {
    const char *key;
    QString name;
    bool security;
};

inline bool antivirusInstalled();

inline const QList<Kind> &kinds()
{
    static const QList<Kind> list = [] {
        QList<Kind> l{
        {"WindowsUpdate", QStringLiteral("Windows Update"), true},
        {"InternetSecurity", tr7("Internet security settings"), true},
        {"Firewall", tr7("Network firewall"), true},
        {"UAC", tr7("User Account Control"), true},
        {"Virus", tr7("Virus protection"), true},
        {"Backup", tr7("Windows Backup"), false},
        {"CheckUpdates", tr7("Check for updates"), false},
        {"ProblemReports", tr7("Windows Problem Reporting"), false},
    };
        if (!antivirusInstalled()) {
            l.removeIf([](const Kind &k) { return qstrcmp(k.key, "Virus") == 0; });
        }
        return l;
    }();
    return list;
}

inline bool watched(const char *key)
{
    return store().value(QStringLiteral("Messages/%1").arg(QLatin1String(key)), true).toBool();
}

inline void setWatched(const char *key, bool on)
{
    store().setValue(QStringLiteral("Messages/%1").arg(QLatin1String(key)), on);
}

/* ---------------- what the system says ---------------- */

/** ClamAV stands in for the antivirus and antispyware programs Windows looks for */
inline bool antivirusInstalled()
{
    return !QStandardPaths::findExecutable(QStringLiteral("clamscan")).isEmpty() || !QStandardPaths::findExecutable(QStringLiteral("clamdscan")).isEmpty();
}

inline bool antivirusRunning()
{
    QProcess p;
    p.start(QStringLiteral("systemctl"), {QStringLiteral("is-active"), QStringLiteral("clamav-daemon"), QStringLiteral("clamav-freshclam")});
    p.waitForFinished(3000);
    return QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n')).contains(QLatin1String("active"));
}

inline bool reportingEnabled()
{
    QFile f(QStringLiteral("/etc/default/apport"));
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    for (const QString &l : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        if (l.trimmed().startsWith(QLatin1String("enabled="))) {
            return l.trimmed().mid(8).trimmed() == QLatin1String("1");
        }
    }
    return false;
}

/** One problem report: apport's crash file */
struct Report {
    QString file;
    QString program; // "Dolphin"
    QString icon;
    QString path;    // /usr/bin/dolphin
    QString package;
    QString signal;
    QDateTime date;
    bool sent = false;
};

/** The name and icon the desktop file gives the program, else the file name */
inline QPair<QString, QString> prettyProgram(const QString &path)
{
    const QString base = QFileInfo(path).fileName();
    for (const QString &dir : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        for (const QString &cand : {QStringLiteral("org.kde.%1.desktop").arg(base), QStringLiteral("%1.desktop").arg(base)}) {
            const QString f = dir + QLatin1Char('/') + cand;
            if (QFile::exists(f)) {
                QSettings d(f, QSettings::IniFormat);
                d.beginGroup(QStringLiteral("Desktop Entry"));
                const QString n = Win7I18n::localizedValue(d, QStringLiteral("Name"));
                d.endGroup();
                if (!n.isEmpty()) {
                    return {n, d.value(QStringLiteral("Desktop Entry/Icon")).toString()};
                }
            }
        }
    }
    return {base, QString()};
}

inline QList<Report> reports()
{
    QList<Report> list;
    const QDir dir(QStringLiteral("/var/crash"));
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.crash")}, QDir::Files, QDir::Time)) {
        Report r;
        r.file = fi.absoluteFilePath();
        r.date = fi.lastModified();
        r.sent = QFile::exists(fi.path() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".uploaded"));
        QFile f(r.file);
        if (f.open(QIODevice::ReadOnly)) {
            // the header fields come before the (huge) core dump
            for (int i = 0; i < 4000 && !f.atEnd(); ++i) {
                const QByteArray line = f.readLine(4096);
                if (line.startsWith("CoreDump:")) {
                    break;
                }
                auto value = [&line](const char *key) {
                    return QString::fromUtf8(line.mid(qstrlen(key))).trimmed();
                };
                if (line.startsWith("ExecutablePath: ")) {
                    r.path = value("ExecutablePath: ");
                } else if (line.startsWith("Package: ")) {
                    r.package = value("Package: ");
                } else if (line.startsWith("Signal: ")) {
                    r.signal = value("Signal: ");
                } else if (line.startsWith("Date: ")) {
                    const QDateTime d = QLocale::c().toDateTime(value("Date: ").simplified(), QStringLiteral("ddd MMM d HH:mm:ss yyyy"));
                    if (d.isValid()) {
                        r.date = d;
                    }
                }
            }
        }
        if (r.path.isEmpty()) {
            r.path = fi.completeBaseName().section(QLatin1Char('.'), 0, 0).replace(QLatin1Char('_'), QLatin1Char('/'));
        }
        std::tie(r.program, r.icon) = prettyProgram(r.path);
        list.append(r);
    }
    return list;
}

/** A message of the Security or Maintenance group */
struct Message {
    const char *kind;
    bool important;
    QString title;
    QString text;
    QString button;
    std::function<void(QWidget *)> action;
};

/** A status row under the messages: "Network firewall    On" and what that means */
struct Row {
    QString name;
    QString state;
    QString text;
    QList<QPair<QString, std::function<void()>>> links;
};

struct Group {
    QList<Message> messages;
    QList<Row> rows;
};

/** "Not monitored" when the messages about it are turned off */
inline QString notMonitoredText(const char *key, const QString &state)
{
    return watched(key) ? state : tr7("Not monitored");
}

inline void reportSettings(const std::function<void()> &done);

inline Group security(const Nav &nav, const std::function<void()> &refresh)
{
    Group g;
    const bool fw = Win7Firewall::enabled();
    const bool av = antivirusInstalled();
    const bool avOn = av && antivirusRunning();
    const Win7Update::AutoSettings wu = Win7Update::readAuto();

    if (!fw && watched("Firewall")) {
        g.messages.append({"Firewall", true, tr7("Network firewall"), tr7("Windows Firewall is turned off or set up incorrectly."),
                           tr7("Turn on now"), [refresh](QWidget *w) {
                               if (Win7Firewall::runAsRoot(w, QStringLiteral("ufw --force enable"))) {
                                   refresh();
                               }
                           }});
    }
    if (wu.mode != 0 && watched("WindowsUpdate")) {
        g.messages.append({"WindowsUpdate", wu.mode == 3, QStringLiteral("Windows Update"),
                           wu.mode == 3 ? tr7("Windows Update is turned off. Choose to install updates automatically.")
                                        : tr7("Updates are not installed automatically. Choose to install updates automatically."),
                           tr7("Change settings..."), [nav](QWidget *) { nav.open(Win7Update::SettingsView); }});
    }
    // Antivirus / antispyware: Linux needs neither, so they are only shown when ClamAV is installed
    // (then it is worth saying that it is switched off). No "find a program online" nagging.
    if (av && !avOn && watched("Virus")) {
        g.messages.append({"Virus", true, tr7("Virus protection"), tr7("The ClamAV antivirus program is turned off."), QString(), {}});
    }

    const auto notMonitored = notMonitoredText;
    g.rows.append({tr7("Network firewall"), notMonitored("Firewall", fw ? tr7("On", "state") : tr7("Off", "state")),
                   fw ? tr7("Windows Firewall is actively protecting your computer.") : tr7("Windows Firewall is turned off."),
                   {{tr7("View firewall settings"), [nav] { nav.open(Win7Firewall::StatusView); }}}});
    static const QString wuText[] = {tr7("Windows will install updates automatically as they become available."),
                                     tr7("Windows will download updates, but you choose whether to install them."),
                                     tr7("Windows will check for updates, but you choose whether to download and install them."),
                                     tr7("Windows will never check for updates.")};
    g.rows.append({QStringLiteral("Windows Update"), notMonitored("WindowsUpdate", wu.mode == 0 ? tr7("On", "state") : (wu.mode == 3 ? tr7("Off", "state") : tr7("Not automatic"))),
                   wuText[qBound(0, wu.mode, 3)], {}});
    if (av) {
        g.rows.append({tr7("Virus protection"), notMonitored("Virus", avOn ? tr7("On", "state") : tr7("Off", "state")),
                       avOn ? tr7("ClamAV reports that it is up to date and virus scanning is on.") : tr7("ClamAV reports that it is turned off."),
                       {}});
    }
    g.rows.append({tr7("Internet security settings"), notMonitored("InternetSecurity", QStringLiteral("OK")),
                   tr7("All Internet security settings are set to their recommended levels."), {}});
    g.rows.append({tr7("User Account Control"), notMonitored("UAC", tr7("On", "state")),
                   tr7("User Account Control will notify when programs try to make changes to the computer."), {}});
    g.rows.append({tr7("Network Access Protection"), tr7("Off", "state"), tr7("Network Access Protection Agent service is not running"), {}});
    return g;
}

inline Group maintenance(const Nav &nav, const std::function<void()> &refresh)
{
    Group g;
    const QList<Report> reps = reports();
    int unsent = 0;
    for (const Report &r : reps) {
        unsent += r.sent ? 0 : 1;
    }
    const Win7Backup::Settings b = Win7Backup::load();
    if (unsent > 0 && watched("ProblemReports")) {
        g.messages.append({"ProblemReports", false, tr7("Check for solutions to problem reports"),
                           tr7("Some programs have stopped working (reports: %1). Review the reports to send them to the developers.").arg(unsent),
                           tr7("View reports"), [nav](QWidget *) { nav.open(ArchiveView); }});
    }
    if (watched("Backup")) {
        if (!b.configured) {
            g.messages.append({"Backup", false, tr7("Set up backup"), tr7("Your files are not being backed up."), tr7("Set up backup", "button"),
                               [nav](QWidget *) { nav.open(Win7Backup::BackupView); }});
        } else if (b.lastFailed) {
            g.messages.append({"Backup", true, tr7("Check your backup results"), tr7("The last backup did not complete successfully."),
                               tr7("Options"), [nav](QWidget *) { nav.open(Win7Backup::BackupView); }});
        }
    }
    const bool rep = reportingEnabled();
    g.rows.append({tr7("Check for solutions to problem reports", "task"), notMonitoredText("ProblemReports", rep ? tr7("On", "state") : tr7("Off", "state")),
                   QString(),
                   {{tr7("Check for solutions"), [] { QProcess::startDetached(QStringLiteral("drkonqi-coredump-gui"), {}); }},
                    {tr7("Options"), [refresh] { reportSettings(refresh); }},
                    {tr7("View archived messages"), [nav] { nav.open(ArchiveView); }}}});
    QString backupState, backupText;
    if (!b.configured) {
        backupState = tr7("Not set up");
        backupText = tr7("Windows Backup has not been set up.");
    } else {
        backupState = b.lastFailed ? tr7("Error") : tr7("On", "state");
        backupText = tr7("Last backup: %1\nLocation: %2")
                         .arg(b.lastBackup > 0 ? Win7Update::whenText(QDateTime::fromSecsSinceEpoch(b.lastBackup)) : tr7("Never"), b.label);
    }
    g.rows.append({tr7("Backup"), notMonitoredText("Backup", backupState), backupText,
                   {{b.configured ? tr7("Change settings") : tr7("Set up backup", "button"), [nav] { nav.open(Win7Backup::BackupView); }}}});
    const QDateTime checked = QFileInfo(QStringLiteral("/var/lib/apt/periodic/update-success-stamp")).lastModified();
    g.rows.append({tr7("Check for updates"), notMonitoredText("CheckUpdates", tr7("On", "state")),
                   tr7("Last checked: %1").arg(Win7Update::whenText(checked)),
                   {{tr7("Check for updates", "button"), [nav] {
                         Win7Update::Engine::self().check();
                         nav.open(Win7Update::UpdateView);
                     }}}});
    return g;
}

/** For the tray flag (`--action-center-status`): the messages of both groups as JSON */
inline QByteArray statusJson()
{
    const Nav nav{[](int) {}, [] {}, [] {}};
    QJsonArray list;
    for (const Group &g : {security(nav, [] {}), maintenance(nav, [] {})}) {
        for (const Message &m : g.messages) {
            // the flyout of the tray flag names what to do, like Windows 7's does
            static const QHash<QString, QString> flyout{{QStringLiteral("Firewall"), tr7("Turn on Windows Firewall")},
                                                        {QStringLiteral("Virus"), tr7("Find an antivirus program online")},
                                                        {QStringLiteral("Spyware"), tr7("Find an antispyware program online")},
                                                        {QStringLiteral("WindowsUpdate"), tr7("Change Windows Update settings")},
                                                        {QStringLiteral("ProblemReports"), tr7("Check for solutions to problem reports")},
                                                        {QStringLiteral("Backup"), m.important ? tr7("Check your backup results", "task") : tr7("Set up backup")}};
            const QString kind = QLatin1String(m.kind);
            list.append(QJsonObject{{QStringLiteral("title"), m.title},
                                    {QStringLiteral("text"), m.text},
                                    {QStringLiteral("flyout"), flyout.value(kind, m.title) + (m.important ? tr7(" (Important)") : QString())},
                                    {QStringLiteral("important"), m.important}});
        }
    }
    return QJsonDocument(list).toJson(QJsonDocument::Compact);
}

/* ---------------- widgets ---------------- */

/** ActionCenterCPL's round expand/collapse button: normal, hot and pressed pictures */
class Chevron : public QWidget
{
public:
    std::function<void()> clicked;
    bool expanded = true;

    Chevron()
    {
        setFixedSize(19, 21);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        // 1001/1002 normal, 1006/1007 hot, 1008/1009 pressed; even = "up" (collapse)
        const int base = m_pressed ? 1008 : (m_hot ? 1006 : 1001);
        QPainter p(this);
        p.drawImage(0, 0, win7AeroImage(QStringLiteral("ac-%1.png").arg(base + (expanded ? 1 : 0))));
    }
    void enterEvent(QEnterEvent *) override
    {
        m_hot = true;
        update();
    }
    void leaveEvent(QEvent *) override
    {
        m_hot = m_pressed = false;
        update();
    }
    void mousePressEvent(QMouseEvent *) override
    {
        m_pressed = true;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        const bool fire = m_pressed && rect().contains(e->position().toPoint());
        m_pressed = false;
        update();
        if (fire && clicked) {
            clicked();
        }
    }

private:
    bool m_hot = false;
    bool m_pressed = false;
};

/** A message: red (important) or yellow strip on the left, its colour fading into white */
class MessageFrame : public QWidget
{
public:
    explicit MessageFrame(bool important)
        : m_important(important)
    {
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect r = rect().adjusted(0, 0, -1, -1);
        const QColor tint = m_important ? QColor(252, 222, 222) : QColor(255, 246, 204);
        QLinearGradient bg(r.topLeft(), QPointF(r.left() + r.width() * 0.6, r.top()));
        bg.setColorAt(0, tint);
        bg.setColorAt(1, Qt::white);
        p.fillRect(r, bg);
        QLinearGradient band(r.topLeft(), r.bottomLeft());
        band.setColorAt(0, m_important ? QColor(172, 1, 0) : QColor(242, 177, 0));
        band.setColorAt(1, m_important ? QColor(222, 1, 0) : QColor(255, 206, 73));
        p.fillRect(QRect(r.left(), r.top(), 6, r.height() + 1), band);
        p.setPen(QColor(0xdc, 0xe2, 0xea));
        p.drawLine(r.left() + 6, r.bottom(), r.right(), r.bottom());
    }

private:
    bool m_important;
};

/** The thin rule that runs from a group's title to its chevron */
class Rule : public QWidget
{
public:
    Rule()
    {
        setFixedHeight(21);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(QColor(0xd5, 0xdf, 0xe5));
        p.drawLine(4, height() / 2, width() - 5, height() / 2);
    }
};

/** A link that keeps to one line (Link wraps, which squeezes it when it is aligned left) */
inline Link *link(const QString &text, std::function<void()> action)
{
    auto *l = new Link(text, std::move(action));
    l->setWordWrap(false);
    return l;
}

inline QLabel *grey(const QString &t)
{
    auto *l = new QLabel(t);
    l->setStyleSheet(QStringLiteral("color: #5a6779;"));
    l->setWordWrap(true);
    return l;
}

inline QList<Win7Display::SideItem> sideLinks(const Nav &nav)
{
    return {{tr7("Change Action Center settings"), [nav] { nav.open(SettingsView); }},
            {tr7("View archived messages"), [nav] { nav.open(ArchiveView); }}};
}

inline QList<Win7Display::SideItem> seeAlso(const Nav &nav)
{
    return {{tr7("Backup and Restore"), [nav] { nav.open(Win7Backup::BackupView); }},
            {tr7("Windows Update"), [nav] { nav.open(Win7Update::UpdateView); }}};
}

/** "Review recent messages and resolve problems" */
class CenterPage : public QWidget
{
public:
    explicit CenterPage(Nav nav)
        : m_nav(std::move(nav))
    {
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        m_content = new QWidget;
        outer->addWidget(Win7Display::pageWithSide(Win7Display::sidePane(m_nav.home, sideLinks(m_nav), seeAlso(m_nav)), m_content));
        auto *v = new QVBoxLayout(m_content);
        v->setContentsMargins(32, 18, 30, 18);
        v->setSpacing(6);
        v->addWidget(Win7Programs::pageTitle(tr7("Review recent messages and resolve problems")));
        m_summary = Win7Programs::wrapped(QString());
        v->addWidget(m_summary);
        v->addSpacing(10);
        m_groups = new QVBoxLayout;
        m_groups->setSpacing(4);
        v->addLayout(m_groups);
        v->addSpacing(16);
        v->addWidget(new QLabel(tr7("If you don't see your problem listed, try one of these:")));
        auto *tile = new QHBoxLayout;
        tile->setContentsMargins(10, 4, 0, 0);
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cpi-recovery")).pixmap(32, 32));
        tile->addWidget(ic, 0, Qt::AlignTop);
        auto *tv = new QVBoxLayout;
        tv->setSpacing(1);
        tv->addWidget(new Link(tr7("Recovery"), [this] { m_nav.open(Win7Backup::RecoveryView); }));
        auto *sub = grey(tr7("Restore your computer to an earlier time"));
        sub->setWordWrap(false);
        tv->addWidget(sub);
        tile->addLayout(tv);
        tile->addStretch(1);
        v->addLayout(tile);
        v->addStretch(1);
        rebuild();
    }

private:
    void rebuild()
    {
        while (QLayoutItem *it = m_groups->takeAt(0)) {
            delete it->widget();
            delete it;
        }
        const auto refresh = [this] { QTimer::singleShot(0, this, [this] { rebuild(); }); };
        const Group sec = security(m_nav, refresh);
        const Group mnt = maintenance(m_nav, refresh);
        const int issues = sec.messages.size() + mnt.messages.size();
        m_summary->setText(issues ? tr7("Action Center has detected one or more issues for you to review.")
                                  : tr7("No issues have been detected by Action Center."));
        m_groups->addWidget(group(QStringLiteral("Security"), tr7("Security"), sec, refresh));
        m_groups->addWidget(group(QStringLiteral("Maintenance"), tr7("Maintenance"), mnt, refresh));
    }

    QWidget *group(const QString &key, const QString &title, const Group &g, const std::function<void()> &refresh)
    {
        auto *box = new QWidget;
        auto *v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 6);
        v->setSpacing(0);
        // a group with messages opens by itself, like Windows does
        const QString stateKey = QStringLiteral("Expanded/%1").arg(key);
        const bool expanded = store().value(stateKey, !g.messages.isEmpty()).toBool();
        auto *head = new QHBoxLayout;
        head->setSpacing(6);
        auto *t = new QLabel(title);
        QFont tf = t->font();
        tf.setPixelSize(15);
        t->setFont(tf);
        t->setStyleSheet(QStringLiteral("color: #1e3287;"));
        head->addWidget(t);
        if (!expanded) {
            // collapsed: the orb and the most urgent message, or green when there is nothing to do
            bool important = false;
            for (const Message &m : g.messages) {
                important |= m.important;
            }
            auto *orb = new QLabel;
            orb->setPixmap(QPixmap::fromImage(win7AeroImage(QStringLiteral("ac-%1.png").arg(g.messages.isEmpty() ? 1005 : (important ? 1003 : 1004)))));
            head->addWidget(orb);
            if (!g.messages.isEmpty()) {
                const Message &m = g.messages.first();
                head->addWidget(new QLabel(m.title + (m.important ? tr7(" (Important)") : QString())));
            }
        }
        head->addWidget(new Rule, 1);
        auto *chev = new Chevron;
        chev->expanded = expanded;
        chev->clicked = [stateKey, expanded, refresh] {
            store().setValue(stateKey, !expanded);
            refresh();
        };
        head->addWidget(chev);
        v->addLayout(head);
        if (!expanded) {
            return box;
        }
        auto *body = new QVBoxLayout;
        body->setContentsMargins(4, 6, 24, 0);
        body->setSpacing(8);
        for (const Message &m : g.messages) {
            body->addWidget(messageWidget(m, refresh));
        }
        auto *grid = new QGridLayout;
        grid->setContentsMargins(16, 6, 0, 0);
        grid->setHorizontalSpacing(30);
        grid->setVerticalSpacing(2);
        int row = 0;
        for (const Row &r : g.rows) {
            auto *name = new QLabel(r.name);
            QFont nf = name->font();
            nf.setBold(true);
            name->setFont(nf);
            grid->addWidget(name, row, 0, Qt::AlignTop);
            grid->addWidget(new QLabel(r.state), row, 1, Qt::AlignTop);
            ++row;
            if (!r.text.isEmpty()) {
                grid->addWidget(grey(r.text), row++, 0, 1, 3);
            }
            for (const auto &l : r.links) {
                grid->addWidget(link(l.first, l.second), row++, 0, 1, 3, Qt::AlignLeft);
            }
            grid->addItem(new QSpacerItem(1, 10), row++, 0);
        }
        grid->setColumnStretch(2, 1);
        body->addLayout(grid);
        v->addLayout(body);
        return box;
    }

    QWidget *messageWidget(const Message &m, const std::function<void()> &refresh)
    {
        auto *f = new MessageFrame(m.important);
        auto *h = new QHBoxLayout(f);
        h->setContentsMargins(18, 9, 12, 10);
        h->setSpacing(16);
        auto *col = new QVBoxLayout;
        col->setSpacing(3);
        auto *t = new QLabel(m.title + (m.important ? tr7(" (Important)") : QString()));
        QFont tf = t->font();
        tf.setBold(true);
        t->setFont(tf);
        col->addWidget(t);
        col->addWidget(Win7Programs::wrapped(m.text));
        QString about;
        for (const Kind &k : kinds()) {
            if (qstrcmp(k.key, m.kind) == 0) {
                about = k.name.left(1).toLower() + k.name.mid(1);
            }
        }
        if (about.startsWith(QLatin1String("windows"))) {
            about[0] = QLatin1Char('W');
        }
        const char *kind = m.kind;
        col->addWidget(link(tr7("Turn off messages about \"%1\"").arg(about), [kind, refresh] {
                           setWatched(kind, false);
                           refresh();
                       }),
                       0, Qt::AlignLeft);
        h->addLayout(col, 1);
        if (!m.button.isEmpty() && m.action) {
            auto *b = new QPushButton(m.button);
            b->setMinimumWidth(150);
            const auto action = m.action;
            connect(b, &QPushButton::clicked, f, [f, action] { action(f); });
            h->addWidget(b, 0, Qt::AlignTop);
        }
        return f;
    }

    Nav m_nav;
    QWidget *m_content = nullptr;
    QLabel *m_summary = nullptr;
    QVBoxLayout *m_groups = nullptr;
};

/* ---------------- problem reporting settings ---------------- */

inline void reportSettings(const std::function<void()> &done)
{
    QDialog d;
    d.setWindowTitle(tr7("Problem Reporting Settings"));
    auto *v = new QVBoxLayout(&d);
    v->setContentsMargins(16, 14, 16, 12);
    auto *t = Win7Programs::pageTitle(tr7("Choose how to check for solutions to problem reports"));
    v->addWidget(t);
    v->addWidget(Win7Programs::wrapped(tr7("When a program stops working, Windows can save a problem report so that you can send it to the developers and check whether there is a solution.")));
    auto *on = new QRadioButton(tr7("Automatically save problem reports (recommended)"));
    auto *off = new QRadioButton(tr7("Never check for solutions (not recommended)"));
    (reportingEnabled() ? on : off)->setChecked(true);
    v->addSpacing(6);
    v->addWidget(on);
    v->addWidget(off);
    v->addSpacing(10);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
    bb->button(QDialogButtonBox::Cancel)->setText(tr7("Cancel"));
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget(bb);
    d.resize(480, d.sizeHint().height());
    if (d.exec() != QDialog::Accepted || on->isChecked() == reportingEnabled()) {
        return;
    }
    const QString val = on->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
    const QString cmd = QStringLiteral("sed -i 's/^enabled=.*/enabled=%1/' /etc/default/apport && systemctl %2 apport.service")
                            .arg(val, on->isChecked() ? QStringLiteral("restart") : QStringLiteral("stop"));
    QProcess p;
    p.start(QStringLiteral("pkexec"), {QStringLiteral("sh"), QStringLiteral("-c"), cmd});
    p.waitForFinished(-1);
    if (done) {
        done();
    }
}

/* ---------------- "Change Action Center settings" ---------------- */

inline QWidget *buildSettingsPage(const Nav &nav)
{
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("win7page"));
    w->setAttribute(Qt::WA_StyledBackground, true);
    w->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(32, 18, 30, 16);
    v->setSpacing(6);
    v->addWidget(Win7Programs::pageTitle(tr7("Turn messages on or off")));
    v->addWidget(Win7Programs::wrapped(tr7("For each selected item, Windows will check for problems and send you a message if problems are found.")));
    QList<QPair<const char *, QCheckBox *>> boxes;
    auto section = [&](const QString &title, bool sec) {
        auto *t = new QLabel(title);
        t->setStyleSheet(QStringLiteral("color: #1e3287;"));
        QFont f = t->font();
        f.setPixelSize(13);
        t->setFont(f);
        v->addSpacing(10);
        v->addWidget(t);
        auto *grid = new QGridLayout;
        grid->setContentsMargins(16, 4, 0, 0);
        grid->setHorizontalSpacing(30);
        grid->setVerticalSpacing(6);
        int i = 0;
        for (const Kind &k : kinds()) {
            if (k.security != sec) {
                continue;
            }
            auto *cb = new QCheckBox(k.name);
            cb->setChecked(watched(k.key));
            grid->addWidget(cb, i / 2, i % 2);
            boxes.append({k.key, cb});
            ++i;
        }
        grid->setColumnMinimumWidth(0, 260);
        grid->setColumnStretch(2, 1);
        v->addLayout(grid);
    };
    section(tr7("Security messages"), true);
    section(tr7("Maintenance messages"), false);
    auto *rel = new QLabel(tr7("Related settings"));
    rel->setStyleSheet(QStringLiteral("color: #1e3287;"));
    QFont rf = rel->font();
    rf.setPixelSize(13);
    rel->setFont(rf);
    v->addSpacing(10);
    v->addWidget(rel);
    auto *links = new QVBoxLayout;
    links->setContentsMargins(16, 2, 0, 0);
    links->setSpacing(5);
    links->addWidget(link(tr7("Problem Reporting Settings"), [] { reportSettings({}); }), 0, Qt::AlignLeft);
    links->addWidget(link(tr7("Windows Update settings"), [nav] { nav.open(Win7Update::SettingsView); }), 0, Qt::AlignLeft);
    v->addLayout(links);
    v->addStretch(1);
    auto *bar = new QHBoxLayout;
    bar->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    for (QPushButton *b : {ok, cancel}) {
        b->setMinimumWidth(80);
        bar->addWidget(b);
    }
    v->addLayout(bar);
    QObject::connect(ok, &QPushButton::clicked, w, [boxes, nav] {
        for (const auto &b : boxes) {
            setWatched(b.first, b.second->isChecked());
        }
        nav.back();
    });
    QObject::connect(cancel, &QPushButton::clicked, w, [nav] { nav.back(); });
    return w;
}

/* ---------------- "View archived messages" ---------------- */

inline void showReport(QWidget *parent, const Report &r)
{
    QDialog d(parent);
    d.setWindowTitle(tr7("Problem report"));
    auto *v = new QVBoxLayout(&d);
    v->setContentsMargins(16, 14, 16, 12);
    v->addWidget(Win7Programs::pageTitle(r.program));
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(20);
    const QList<QPair<QString, QString>> rows{{tr7("Summary"), tr7("Stopped working")},
                                              {tr7("Date"), r.date.toString(QStringLiteral("dd.MM.yyyy HH:mm"))},
                                              {tr7("Status"), r.sent ? tr7("Report sent") : tr7("Report not sent")},
                                              {tr7("Application Path"), r.path},
                                              {tr7("Package"), r.package},
                                              {tr7("Signal"), r.signal},
                                              {tr7("Report file"), r.file}};
    int i = 0;
    for (const auto &row : rows) {
        grid->addWidget(grey(row.first), i, 0, Qt::AlignTop);
        auto *val = new QLabel(row.second);
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        val->setWordWrap(true);
        grid->addWidget(val, i++, 1);
    }
    v->addLayout(grid);
    v->addSpacing(8);
    auto *bar = new QHBoxLayout;
    bar->addStretch(1);
    auto *close = new QPushButton(QStringLiteral("OK"));
    close->setMinimumWidth(80);
    bar->addWidget(close);
    QObject::connect(close, &QPushButton::clicked, &d, &QDialog::accept);
    v->addLayout(bar);
    d.resize(520, d.sizeHint().height());
    d.exec();
}

inline QWidget *buildArchivePage(const Nav &nav)
{
    auto *w = new QWidget;
    w->setObjectName(QStringLiteral("win7page"));
    w->setAttribute(Qt::WA_StyledBackground, true);
    w->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(32, 18, 30, 16);
    v->setSpacing(6);
    v->addWidget(Win7Programs::pageTitle(tr7("Review archived messages about computer problems")));
    v->addWidget(Win7Programs::wrapped(tr7("To view details about a problem, double-click the message. These are the reports of programs that stopped working, saved on this computer.")));
    auto *tree = new QTreeWidget;
    tree->setRootIsDecorated(true);
    tree->setHeaderLabels({tr7("Source"), tr7("Summary"), tr7("Date"), tr7("Status")});
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    Win7HeaderView::install(tree);
    const QList<Report> reps = reports();
    QHash<QString, QTreeWidgetItem *> groups;
    for (const Report &r : reps) {
        QTreeWidgetItem *parent = groups.value(r.program);
        if (!parent) {
            parent = new QTreeWidgetItem(tree, {r.program});
            parent->setIcon(0, QIcon::fromTheme(r.icon.isEmpty() ? QFileInfo(r.path).fileName() : r.icon, QIcon::fromTheme(QStringLiteral("application-x-executable"))));
            parent->setFirstColumnSpanned(false);
            groups.insert(r.program, parent);
        }
        auto *it = new QTreeWidgetItem(parent, {QString(), tr7("Stopped working"), r.date.toString(QStringLiteral("dd.MM.yyyy HH:mm")),
                                                r.sent ? tr7("Report sent") : tr7("Report not sent")});
        it->setData(0, Qt::UserRole, r.file);
    }
    tree->expandAll();
    tree->setColumnWidth(0, 220);
    tree->setColumnWidth(1, 160);
    tree->setColumnWidth(2, 130);
    v->addWidget(tree, 1);
    auto *count = grey(tr7("Messages: %1").arg(reps.size()));
    v->addWidget(count);
    QObject::connect(tree, &QTreeWidget::itemDoubleClicked, w, [w, reps](QTreeWidgetItem *it) {
        const QString file = it->data(0, Qt::UserRole).toString();
        for (const Report &r : reps) {
            if (r.file == file) {
                showReport(w, r);
            }
        }
    });
    auto *bar = new QHBoxLayout;
    auto *solutions = new QPushButton(tr7("Check for solutions"));
    auto *del = new QPushButton(tr7("Delete"));
    auto *ok = new QPushButton(QStringLiteral("OK"));
    bar->addWidget(solutions);
    bar->addStretch(1);
    bar->addWidget(del);
    bar->addWidget(ok);
    ok->setMinimumWidth(80);
    v->addLayout(bar);
    QObject::connect(solutions, &QPushButton::clicked, w, [] { QProcess::startDetached(QStringLiteral("drkonqi-coredump-gui"), {}); });
    QObject::connect(ok, &QPushButton::clicked, w, [nav] { nav.back(); });
    QObject::connect(del, &QPushButton::clicked, w, [tree, count] {
        // the files belong to the user (group whoopsie), so no password is needed
        for (QTreeWidgetItem *it : tree->selectedItems()) {
            QList<QTreeWidgetItem *> leaves;
            if (it->childCount()) {
                for (int i = 0; i < it->childCount(); ++i) {
                    leaves << it->child(i);
                }
            } else {
                leaves << it;
            }
            for (QTreeWidgetItem *leaf : leaves) {
                const QString f = leaf->data(0, Qt::UserRole).toString();
                if (!f.isEmpty() && QFile::remove(f)) {
                    QFile::remove(QFileInfo(f).path() + QLatin1Char('/') + QFileInfo(f).completeBaseName() + QStringLiteral(".upload"));
                    QFile::remove(QFileInfo(f).path() + QLatin1Char('/') + QFileInfo(f).completeBaseName() + QStringLiteral(".uploaded"));
                    leaf->setData(0, Qt::UserRole, QString());
                    leaf->setHidden(true);
                }
            }
        }
        int left = 0;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *top = tree->topLevelItem(i);
            int shown = 0;
            for (int j = 0; j < top->childCount(); ++j) {
                shown += top->child(j)->isHidden() ? 0 : 1;
            }
            top->setHidden(shown == 0);
            left += shown;
        }
        count->setText(tr7("Messages: %1").arg(left));
    });
    return w;
}

inline bool isActionView(int v)
{
    return v >= CenterView && v <= ArchiveView;
}

inline QString viewTitle(int v)
{
    switch (v) {
    case SettingsView:
        return tr7("Change Action Center settings", "page title");
    case ArchiveView:
        return tr7("View archived messages", "page title");
    default:
        return tr7("Action Center");
    }
}

inline QWidget *build(int view, const Nav &nav)
{
    switch (view) {
    case SettingsView:
        return buildSettingsPage(nav);
    case ArchiveView:
        return buildArchivePage(nav);
    default:
        return new CenterPage(nav);
    }
}

} // namespace Win7Action
