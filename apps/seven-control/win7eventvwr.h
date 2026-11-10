/*
 * "Event Viewer" (eventvwr.msc) of Windows 7 on top of the systemd journal.
 *
 *   Журнали Windows:  Програма    -> the user's journal (journalctl --user)
 *                     Безпека     -> auth / authpriv facility (sudo, polkit, logins), audit success / failure
 *                     Інсталяція  -> /var/log/dpkg.log* (packages installed, upgraded, removed)
 *                     Система     -> the system journal without the above
 *                     Перенаправлені події -> empty, like on a computer without subscriptions
 *   Журнали програм і служб: kernel (hardware), network, power, Bluetooth, printing, snaps, AppArmor
 *   levels:  0-2 Critical, 3 Error, 4 Warning, 5-6 Information, 7 Verbose (syslog priorities)
 *   event code: a stable number for the source and level (journald has no event IDs)
 *
 * Icons come from Windows 7 itself (EventViewer.dll, els.dll, mmc.exe, user32.dll).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7wrapmenubar.h"
#include "win7states.h"
#include "win7chrome.h"
#include "win7diskmgmt.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>

#include <cmath>
#include <pwd.h>
#include <unistd.h>

namespace Win7Event
{
/* ---------------- events ---------------- */

enum Level {
    Critical = 1,
    Error = 2,
    Warning = 3,
    Information = 4,
    Verbose = 5,
    AuditSuccess = 10,
    AuditFailure = 11,
};

inline QString levelName(int l)
{
    switch (l) {
    case Critical:
        return QStringLiteral("Критичний");
    case Error:
        return QStringLiteral("Помилка");
    case Warning:
        return QStringLiteral("Попередження");
    case Verbose:
        return QStringLiteral("Докладно");
    case AuditSuccess:
        return QStringLiteral("Аудит успіху");
    case AuditFailure:
        return QStringLiteral("Аудит відмови");
    }
    return QStringLiteral("Відомості");
}

inline QString levelIcon(int l)
{
    switch (l) {
    case Critical:
        return QStringLiteral("seven-ev-critical");
    case Error:
        return QStringLiteral("seven-ev-error");
    case Warning:
        return QStringLiteral("seven-ev-warning");
    case AuditSuccess:
        return QStringLiteral("seven-ev-auditok");
    case AuditFailure:
        return QStringLiteral("seven-ev-auditfail");
    }
    return QStringLiteral("seven-ev-info");
}

inline int levelFromPriority(int prio)
{
    if (prio <= 2) {
        return Critical;
    }
    if (prio == 3) {
        return Error;
    }
    if (prio == 4) {
        return Warning;
    }
    return prio >= 7 ? Verbose : Information;
}

/** a number that stays the same for a source and level, like the event IDs of a Windows event source */
inline int eventCode(const QString &source, int level)
{
    quint32 h = 2166136261u;
    for (const QChar c : source.toLower()) {
        h = (h ^ c.unicode()) * 16777619u;
    }
    return int(h % 9000) + 1000 + (level == Error || level == Critical ? 0 : level * 3) % 7;
}

struct Event {
    qint64 usec = 0; // __REALTIME_TIMESTAMP
    int level = Information;
    QString source;
    int code = 0;
    QString message;
    QString log;      // "Система"
    QString channel;  // "System"
    QString user;
    QString computer;
    QString cursor;   // to read all fields later
    qint64 record = 0;
    int pid = 0;
    QDateTime when() const
    {
        return QDateTime::fromMSecsSinceEpoch(usec / 1000);
    }
};

inline QString userName(const QString &uid)
{
    static QHash<QString, QString> cache;
    if (uid.isEmpty()) {
        return QStringLiteral("Н/Д");
    }
    if (!cache.contains(uid)) {
        const passwd *pw = getpwuid(uid_t(uid.toUInt()));
        cache.insert(uid, pw ? QString::fromLocal8Bit(pw->pw_name) : uid);
    }
    return cache.value(uid);
}

inline QString jsonString(const QJsonValue &v)
{
    if (v.isString()) {
        return v.toString();
    }
    if (v.isArray()) { // non-UTF-8 messages come as byte arrays
        QByteArray b;
        for (const QJsonValue &x : v.toArray()) {
            b.append(char(x.toInt()));
        }
        return QString::fromUtf8(b);
    }
    return QString();
}

/* ---------------- logs ---------------- */

struct LogDef {
    QString key;     // config / path key
    QString title;   // "Система"
    QString channel; // "System"
    QStringList args;                            // journalctl arguments, empty = not a journal log
    std::function<bool(const QJsonObject &)> keep; // further filtering of the lines
    bool dpkg = false;
    bool security = false;
    QString file; // an opened saved log (*.journal)
};

inline bool isAuth(const QJsonObject &o)
{
    const QString f = o.value(QStringLiteral("SYSLOG_FACILITY")).toString();
    return f == QLatin1String("4") || f == QLatin1String("10");
}

inline bool isKernelAudit(const QJsonObject &o)
{
    return o.value(QStringLiteral("_TRANSPORT")).toString() == QLatin1String("kernel") && (jsonString(o.value(QStringLiteral("MESSAGE"))).startsWith(QLatin1String("audit:"))
                                                                                              || jsonString(o.value(QStringLiteral("MESSAGE"))).startsWith(QLatin1String("kauditd_printk_skb")));
}

inline QList<LogDef> windowsLogs()
{
    return {
        {QStringLiteral("Application"), QStringLiteral("Програма"), QStringLiteral("Application"), {QStringLiteral("--user")}, {}},
        {QStringLiteral("Security"), QStringLiteral("Безпека"), QStringLiteral("Security"), {QStringLiteral("--system"), QStringLiteral("--facility=auth,authpriv")}, {}, false, true},
        {QStringLiteral("Setup"), QStringLiteral("Інсталяція"), QStringLiteral("Setup"), {}, {}, true},
        {QStringLiteral("System"), QStringLiteral("Система"), QStringLiteral("System"), {QStringLiteral("--system")},
         [](const QJsonObject &o) {
             return !isAuth(o) && !isKernelAudit(o);
         }},
        {QStringLiteral("ForwardedEvents"), QStringLiteral("Перенаправлені події"), QStringLiteral("ForwardedEvents"), {}, {}},
    };
}

inline QList<LogDef> serviceLogs()
{
    auto ids = [](std::initializer_list<const char *> l) {
        QStringList a{QStringLiteral("--system")};
        for (const char *x : l) {
            a << QStringLiteral("SYSLOG_IDENTIFIER=%1").arg(QLatin1String(x));
        }
        return a;
    };
    return {
        {QStringLiteral("Hardware"), QStringLiteral("Події обладнання"), QStringLiteral("HardwareEvents"), {QStringLiteral("-k")},
         [](const QJsonObject &o) {
             return !isKernelAudit(o);
         }},
        {QStringLiteral("Network"), QStringLiteral("Мережа"), QStringLiteral("Network"), ids({"NetworkManager", "wpa_supplicant", "dhclient", "systemd-resolved", "avahi-daemon"}), {}},
        {QStringLiteral("Power"), QStringLiteral("Електроживлення"), QStringLiteral("Power"), ids({"org_kde_powerdevil", "upowerd", "systemd-logind", "systemd-sleep"}), {}},
        {QStringLiteral("Bluetooth"), QStringLiteral("Bluetooth"), QStringLiteral("Bluetooth"), ids({"bluetoothd"}), {}},
        {QStringLiteral("Print"), QStringLiteral("Служба друку"), QStringLiteral("PrintService"), ids({"cupsd", "cups-browsed"}), {}},
        {QStringLiteral("Snap"), QStringLiteral("Служба програм Snap"), QStringLiteral("Snapd"), ids({"snapd"}), {}},
        {QStringLiteral("AppArmor"), QStringLiteral("AppArmor"), QStringLiteral("AppArmor"), {QStringLiteral("-k"), QStringLiteral("--grep=apparmor")}, {}},
    };
}

constexpr int kMaxEvents = 5000;

inline QString sizeText(double bytes)
{
    const QStringList units{QStringLiteral("байт"), QStringLiteral("КБ"), QStringLiteral("МБ"), QStringLiteral("ГБ"), QStringLiteral("ТБ")};
    int u = 0;
    while (bytes >= 1024 && u < units.size() - 1) {
        bytes /= 1024;
        ++u;
    }
    return u ? QLocale(QLocale::Ukrainian).toString(bytes, 'f', 2) + QLatin1Char(' ') + units.at(u) : QStringLiteral("%1 байт").arg(qint64(bytes));
}

/** "Archived and active journals take up 730.1M in the file system." -> "730,10 МБ" */
inline QString diskUsage(const QByteArray &out)
{
    QString v = QString::fromUtf8(out).section(QStringLiteral("take up "), 1).section(QLatin1Char(' '), 0, 0);
    if (v.isEmpty()) {
        return QStringLiteral("Н/Д");
    }
    const QString suffixes = QStringLiteral("BKMGT");
    double mul = 1;
    const int idx = suffixes.indexOf(v.back());
    if (idx >= 0) {
        mul = std::pow(1024.0, idx);
        v.chop(1);
    }
    return sizeText(v.toDouble() * mul);
}

/** security events: success or failure, from the words in the message */
inline int auditLevel(const QString &msg)
{
    static const QStringList bad{QStringLiteral("fail"), QStringLiteral("incorrect"), QStringLiteral("denied"), QStringLiteral("invalid"), QStringLiteral("error"),
                                 QStringLiteral("not in sudoers"), QStringLiteral("refused"), QStringLiteral("unknown user")};
    const QString m = msg.toLower();
    for (const QString &b : bad) {
        if (m.contains(b)) {
            return AuditFailure;
        }
    }
    return AuditSuccess;
}

/** one journal line in JSON */
inline Event eventFromJson(const QJsonObject &o, const LogDef &def)
{
    Event e;
    e.usec = o.value(QStringLiteral("__REALTIME_TIMESTAMP")).toString().toLongLong();
    e.message = jsonString(o.value(QStringLiteral("MESSAGE")));
    e.source = o.value(QStringLiteral("SYSLOG_IDENTIFIER")).toString();
    if (e.source.isEmpty()) {
        e.source = o.value(QStringLiteral("_COMM")).toString();
    }
    if (e.source.isEmpty()) {
        e.source = o.value(QStringLiteral("_TRANSPORT")).toString() == QLatin1String("kernel") ? QStringLiteral("kernel") : QStringLiteral("Невідоме джерело");
    }
    const int prio = o.value(QStringLiteral("PRIORITY")).toString(QStringLiteral("6")).toInt();
    e.level = def.security ? auditLevel(e.message) : levelFromPriority(prio);
    if (def.security && prio <= 3) {
        e.level = AuditFailure;
    }
    e.code = eventCode(e.source, e.level);
    e.user = userName(o.value(QStringLiteral("_UID")).toString());
    e.computer = o.value(QStringLiteral("_HOSTNAME")).toString();
    e.cursor = o.value(QStringLiteral("__CURSOR")).toString();
    e.record = o.value(QStringLiteral("__SEQNUM")).toString().toLongLong();
    e.pid = o.value(QStringLiteral("_PID")).toString().toInt();
    e.log = def.title;
    e.channel = def.channel;
    return e;
}

inline QStringList journalArgs(const LogDef &def, int max = kMaxEvents)
{
    QStringList a{QStringLiteral("-q"), QStringLiteral("--no-pager"), QStringLiteral("-r"), QStringLiteral("-o"), QStringLiteral("json"), QStringLiteral("-n"), QString::number(max)};
    if (!def.file.isEmpty()) {
        a << QStringLiteral("--file=%1").arg(def.file);
    } else {
        a << def.args;
    }
    return a;
}

/** reads a log: journalctl or dpkg.log; runs in a thread */
inline QList<Event> readLog(const LogDef &def, int max = kMaxEvents)
{
    QList<Event> out;
    const QString host = QSysInfo::machineHostName();
    if (def.dpkg) {
        // dpkg.log, dpkg.log.1 and the gzipped older ones, newest first
        QStringList files = QDir(QStringLiteral("/var/log")).entryList({QStringLiteral("dpkg.log*")}, QDir::Files, QDir::Name);
        std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
            auto n = [](const QString &f) {
                const QString s = f.section(QLatin1Char('.'), 2, 2);
                return s.isEmpty() ? 0 : s.toInt();
            };
            return n(a) < n(b);
        });
        int seq = 0;
        for (const QString &f : std::as_const(files)) {
            QByteArray data;
            if (f.endsWith(QLatin1String(".gz"))) {
                QProcess p;
                p.start(QStringLiteral("zcat"), {QStringLiteral("/var/log/") + f});
                p.waitForFinished(30000);
                data = p.readAllStandardOutput();
            } else {
                QFile file(QStringLiteral("/var/log/") + f);
                if (file.open(QIODevice::ReadOnly)) {
                    data = file.readAll();
                }
            }
            QList<Event> chunk;
            for (const QByteArray &raw : data.split('\n')) {
                const QList<QByteArray> c = raw.split(' ');
                if (c.size() < 6) {
                    continue;
                }
                const QByteArray act = c.at(2);
                if (act != "install" && act != "upgrade" && act != "remove" && act != "purge") {
                    continue;
                }
                Event e;
                e.usec = QDateTime::fromString(QString::fromLatin1(c.at(0) + ' ' + c.at(1)), QStringLiteral("yyyy-MM-dd HH:mm:ss")).toMSecsSinceEpoch() * 1000;
                const QString pkg = QString::fromUtf8(c.at(3)).section(QLatin1Char(':'), 0, 0);
                const QString from = QString::fromUtf8(c.at(4)), to = QString::fromUtf8(c.at(5));
                if (act == "install") {
                    e.message = QStringLiteral("Пакет «%1» версії %2 було успішно встановлено.").arg(pkg, to);
                    e.code = 2;
                } else if (act == "upgrade") {
                    e.message = QStringLiteral("Пакет «%1» було успішно оновлено з версії %2 до версії %3.").arg(pkg, from, to);
                    e.code = 3;
                } else {
                    e.message = QStringLiteral("Пакет «%1» версії %2 було успішно видалено.").arg(pkg, from);
                    e.code = 4;
                }
                e.source = QStringLiteral("dpkg");
                e.level = Information;
                e.user = QStringLiteral("root");
                e.computer = host;
                e.log = def.title;
                e.channel = def.channel;
                e.record = ++seq;
                chunk.append(e);
            }
            std::reverse(chunk.begin(), chunk.end());
            out += chunk;
            if (out.size() >= max) {
                break;
            }
        }
        return out.mid(0, max);
    }
    if (def.args.isEmpty() && def.file.isEmpty()) {
        return out; // forwarded events
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(QStringLiteral("journalctl"), journalArgs(def, def.keep ? max * 3 : max));
    p.waitForFinished(120000);
    const QByteArray data = p.readAllStandardOutput();
    for (const QByteArray &line : data.split('\n')) {
        if (line.isEmpty()) {
            continue;
        }
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (o.isEmpty() || (def.keep && !def.keep(o))) {
            continue;
        }
        out.append(eventFromJson(o, def));
        if (out.size() >= max) {
            break;
        }
    }
    return out;
}

/** all fields of one journal entry */
inline QJsonObject fullEntry(const Event &e, const LogDef &def)
{
    if (e.cursor.isEmpty()) {
        return {};
    }
    QStringList a{QStringLiteral("-q"), QStringLiteral("--no-pager"), QStringLiteral("-o"), QStringLiteral("json"), QStringLiteral("-n"), QStringLiteral("1"),
                  QStringLiteral("--cursor=%1").arg(e.cursor)};
    if (!def.file.isEmpty()) {
        a << QStringLiteral("--file=%1").arg(def.file);
    } else if (def.args.contains(QStringLiteral("--user"))) {
        a << QStringLiteral("--user");
    }
    QProcess p;
    p.start(QStringLiteral("journalctl"), a);
    p.waitForFinished(10000);
    return QJsonDocument::fromJson(p.readAllStandardOutput().split('\n').value(0)).object();
}

/* ---------------- filters ---------------- */

struct Filter {
    int hours = 0; // 0 = any time
    QSet<int> levels; // empty = all
    QStringList sources;
    QString codes; // "1,3,5-99,-76"
    QString user;
    QString computer;
    bool active() const
    {
        return hours || !levels.isEmpty() || !sources.isEmpty() || !codes.trimmed().isEmpty() || !user.isEmpty() || !computer.isEmpty();
    }
    bool codeMatches(int code) const
    {
        const QString c = codes.trimmed();
        if (c.isEmpty()) {
            return true;
        }
        bool anyInclude = false, included = false;
        for (QString part : c.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            part = part.trimmed();
            const bool exclude = part.startsWith(QLatin1Char('-'));
            if (exclude) {
                part.remove(0, 1);
            }
            const int lo = part.section(QLatin1Char('-'), 0, 0).toInt();
            const int hi = part.contains(QLatin1Char('-')) ? part.section(QLatin1Char('-'), 1, 1).toInt() : lo;
            const bool hit = code >= lo && code <= hi;
            if (exclude && hit) {
                return false;
            }
            if (!exclude) {
                anyInclude = true;
                included = included || hit;
            }
        }
        return !anyInclude || included;
    }
    bool matches(const Event &e) const
    {
        if (hours && e.usec < (QDateTime::currentMSecsSinceEpoch() - qint64(hours) * 3600000) * 1000) {
            return false;
        }
        if (!levels.isEmpty() && !levels.contains(e.level == AuditSuccess || e.level == AuditFailure ? Information : e.level)) {
            return false;
        }
        if (!sources.isEmpty() && !sources.contains(e.source, Qt::CaseInsensitive)) {
            return false;
        }
        if (!user.isEmpty() && e.user.compare(user, Qt::CaseInsensitive) != 0) {
            return false;
        }
        if (!computer.isEmpty() && !e.computer.contains(computer, Qt::CaseInsensitive)) {
            return false;
        }
        return codeMatches(e.code);
    }
    QString describe(const QString &log) const
    {
        QStringList l{QStringLiteral("Журнал: %1").arg(log)};
        if (!levels.isEmpty()) {
            QStringList n;
            for (int x : {Critical, Error, Warning, Information, Verbose}) {
                if (levels.contains(x)) {
                    n << levelName(x);
                }
            }
            l << QStringLiteral("Рівень: %1").arg(n.join(QStringLiteral(", ")));
        }
        l << QStringLiteral("Джерело: %1").arg(sources.join(QStringLiteral(", ")));
        if (!codes.trimmed().isEmpty()) {
            l << QStringLiteral("Код події: %1").arg(codes.trimmed());
        }
        return QStringLiteral("Відфільтровано: ") + l.join(QStringLiteral("; ")) + QLatin1Char('.');
    }
    QString xml(const QString &channel) const
    {
        QStringList conds;
        if (!levels.isEmpty()) {
            QStringList lv;
            for (int x : levels) {
                lv << QStringLiteral("Level=%1").arg(x == Information ? 4 : x);
            }
            conds << QStringLiteral("(") + lv.join(QStringLiteral(" or ")) + QStringLiteral(")");
        }
        if (hours) {
            conds << QStringLiteral("TimeCreated[timediff(@SystemTime) &lt;= %1]").arg(qint64(hours) * 3600000);
        }
        QString sel = conds.isEmpty() ? QStringLiteral("*") : QStringLiteral("*[System[%1]]").arg(conds.join(QStringLiteral(" and ")));
        return QStringLiteral("<QueryList>\n  <Query Id=\"0\" Path=\"%1\">\n    <Select Path=\"%1\">%2</Select>\n  </Query>\n</QueryList>").arg(channel, sel);
    }
    void save(KConfigGroup g) const
    {
        g.writeEntry("Hours", hours);
        QList<int> lv(levels.begin(), levels.end());
        g.writeEntry("Levels", lv);
        g.writeEntry("Sources", sources);
        g.writeEntry("Codes", codes);
        g.writeEntry("User", user);
        g.writeEntry("Computer", computer);
    }
    static Filter load(const KConfigGroup &g)
    {
        Filter f;
        f.hours = g.readEntry("Hours", 0);
        const QList<int> lv = g.readEntry("Levels", QList<int>());
        f.levels = QSet<int>(lv.begin(), lv.end());
        f.sources = g.readEntry("Sources", QStringList());
        f.codes = g.readEntry("Codes", QString());
        f.user = g.readEntry("User", QString());
        f.computer = g.readEntry("Computer", QString());
        return f;
    }
};

/** "Filter Current Log" / "Create Custom View" */
class FilterDialog : public QDialog
{
public:
    FilterDialog(QWidget *parent, const QString &title, const Filter &f, const QStringList &logs, const QStringList &knownSources, const QString &channel)
        : QDialog(parent)
        , m_channel(channel)
    {
        setWindowTitle(title);
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setFixedSize(530, 560);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(8, 8, 8, 8);
        auto *tabs = new QTabWidget;
        auto *page = new QWidget;
        auto *g = new QGridLayout(page);
        g->setContentsMargins(10, 12, 10, 10);
        g->setVerticalSpacing(8);
        g->addWidget(new QLabel(QStringLiteral("Записано:")), 0, 0);
        m_when = new QComboBox;
        const QList<QPair<QString, int>> times{{QStringLiteral("Будь-коли"), 0},           {QStringLiteral("Остання година"), 1},       {QStringLiteral("Останні 12 годин"), 12},
                                               {QStringLiteral("Останні 24 години"), 24}, {QStringLiteral("Останні 7 днів"), 24 * 7}, {QStringLiteral("Останні 30 днів"), 24 * 30}};
        for (const auto &[t, h] : times) {
            m_when->addItem(t, h);
        }
        m_when->setCurrentIndex(qMax(0, m_when->findData(f.hours)));
        g->addWidget(m_when, 0, 1, 1, 3);
        g->addWidget(new QLabel(QStringLiteral("Рівень події:")), 1, 0, Qt::AlignTop);
        auto *lv = new QGridLayout;
        const QList<QPair<int, QString>> levels{{Critical, QStringLiteral("Критичний")}, {Warning, QStringLiteral("Попередження")}, {Verbose, QStringLiteral("Докладно")},
                                                {Error, QStringLiteral("Помилка")},       {Information, QStringLiteral("Відомості")}};
        for (int i = 0; i < levels.size(); ++i) {
            auto *c = new QCheckBox(levels.at(i).second);
            c->setChecked(f.levels.contains(levels.at(i).first));
            c->setProperty("level", levels.at(i).first);
            m_levels.append(c);
            lv->addWidget(c, i / 3, i % 3);
        }
        g->addLayout(lv, 1, 1, 1, 3);
        m_byLog = new QRadioButton(QStringLiteral("За журналом"));
        m_bySource = new QRadioButton(QStringLiteral("За джерелом"));
        m_byLog->setChecked(true);
        g->addWidget(m_byLog, 2, 0);
        auto *logsBox = new QComboBox;
        logsBox->addItem(logs.join(QStringLiteral(", ")));
        logsBox->setEnabled(false);
        g->addWidget(new QLabel(QStringLiteral("Журнали подій:")), 2, 1);
        g->addWidget(logsBox, 2, 2, 1, 2);
        g->addWidget(m_bySource, 3, 0);
        g->addWidget(new QLabel(QStringLiteral("Джерела подій:")), 3, 1);
        m_sources = new QComboBox;
        m_sources->setEditable(true);
        m_sources->addItem(QString());
        m_sources->addItems(knownSources);
        m_sources->setEditText(f.sources.join(QStringLiteral(",")));
        g->addWidget(m_sources, 3, 2, 1, 2);
        auto *codesHelp = new QLabel(QStringLiteral("Включає або виключає коди подій. Введіть коди та діапазони кодів, розділяючи їх комами. "
                                                    "Щоб виключити умову, спочатку введіть знак мінус. Наприклад: 1,3,5-99,-76"));
        codesHelp->setWordWrap(true);
        g->addWidget(codesHelp, 4, 0, 1, 4);
        m_codes = new QLineEdit(f.codes);
        m_codes->setPlaceholderText(QStringLiteral("<Усі коди подій>"));
        g->addWidget(m_codes, 5, 0, 1, 4);
        g->addWidget(new QLabel(QStringLiteral("Категорія завдання:")), 6, 0);
        auto *cat = new QComboBox;
        cat->setEnabled(false);
        g->addWidget(cat, 6, 1, 1, 3);
        g->addWidget(new QLabel(QStringLiteral("Ключові слова:")), 7, 0);
        auto *kw = new QComboBox;
        kw->setEnabled(false);
        g->addWidget(kw, 7, 1, 1, 3);
        g->addWidget(new QLabel(QStringLiteral("Користувач:")), 8, 0);
        m_user = new QLineEdit(f.user);
        m_user->setPlaceholderText(QStringLiteral("<Усі користувачі>"));
        g->addWidget(m_user, 8, 1, 1, 3);
        g->addWidget(new QLabel(QStringLiteral("Комп'ютери:")), 9, 0);
        m_computer = new QLineEdit(f.computer);
        m_computer->setPlaceholderText(QStringLiteral("<Усі комп'ютери>"));
        g->addWidget(m_computer, 9, 1, 1, 3);
        auto *clear = new QPushButton(QStringLiteral("Очистити"));
        g->addWidget(clear, 10, 3, Qt::AlignRight);
        g->setRowStretch(11, 1);
        tabs->addTab(page, QStringLiteral("Фільтр"));
        m_xml = new QPlainTextEdit;
        m_xml->setReadOnly(true);
        tabs->addTab(m_xml, QStringLiteral("XML"));
        v->addWidget(tabs, 1);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        ok->setDefault(true);
        for (QPushButton *b : {ok, cancel}) {
            b->setMinimumWidth(80);
            row->addWidget(b);
        }
        v->addLayout(row);
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(clear, &QPushButton::clicked, this, [this] {
            m_when->setCurrentIndex(0);
            for (QCheckBox *c : std::as_const(m_levels)) {
                c->setChecked(false);
            }
            m_sources->setEditText(QString());
            m_codes->clear();
            m_user->clear();
            m_computer->clear();
        });
        connect(tabs, &QTabWidget::currentChanged, this, [this](int i) {
            if (i == 1) {
                m_xml->setPlainText(filter().xml(m_channel));
            }
        });
    }

    Filter filter() const
    {
        Filter f;
        f.hours = m_when->currentData().toInt();
        for (QCheckBox *c : m_levels) {
            if (c->isChecked()) {
                f.levels.insert(c->property("level").toInt());
            }
        }
        for (const QString &s : m_sources->currentText().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            f.sources << s.trimmed();
        }
        f.codes = m_codes->text();
        f.user = m_user->text().trimmed();
        f.computer = m_computer->text().trimmed();
        return f;
    }

private:
    QString m_channel;
    QComboBox *m_when;
    QList<QCheckBox *> m_levels;
    QRadioButton *m_byLog;
    QRadioButton *m_bySource;
    QComboBox *m_sources;
    QLineEdit *m_codes;
    QLineEdit *m_user;
    QLineEdit *m_computer;
    QPlainTextEdit *m_xml;
};

/* ---------------- event details (preview pane and "Event Properties") ---------------- */

inline QString xmlEscape(QString s)
{
    s.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    s.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    s.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    s.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return s;
}

inline int xmlLevel(int l)
{
    return l == AuditSuccess || l == AuditFailure ? 0 : l;
}

/** the event in the Windows event schema, with every journal field as EventData */
inline QString eventXml(const Event &e, const QJsonObject &full)
{
    QString x = QStringLiteral("<Event xmlns=\"http://schemas.microsoft.com/win/2004/08/events/event\">\n  <System>\n");
    x += QStringLiteral("    <Provider Name=\"%1\" />\n").arg(xmlEscape(e.source));
    x += QStringLiteral("    <EventID>%1</EventID>\n    <Level>%2</Level>\n    <Task>0</Task>\n    <Keywords>0x80000000000000</Keywords>\n").arg(e.code).arg(xmlLevel(e.level));
    x += QStringLiteral("    <TimeCreated SystemTime=\"%1\" />\n").arg(e.when().toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz000000Z")));
    x += QStringLiteral("    <EventRecordID>%1</EventRecordID>\n").arg(e.record);
    x += QStringLiteral("    <Execution ProcessID=\"%1\" ThreadID=\"0\" />\n").arg(e.pid);
    x += QStringLiteral("    <Channel>%1</Channel>\n    <Computer>%2</Computer>\n    <Security />\n  </System>\n  <EventData>\n").arg(e.channel, xmlEscape(e.computer));
    x += QStringLiteral("    <Data Name=\"Message\">%1</Data>\n").arg(xmlEscape(e.message));
    QStringList keys = full.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &k : std::as_const(keys)) {
        if (k == QLatin1String("MESSAGE") || k.startsWith(QLatin1String("__"))) {
            continue;
        }
        x += QStringLiteral("    <Data Name=\"%1\">%2</Data>\n").arg(xmlEscape(k), xmlEscape(jsonString(full.value(k))));
    }
    x += QStringLiteral("  </EventData>\n</Event>");
    return x;
}

/** "Загальні" and "Докладно" tabs, used by the preview pane and the properties dialog */
class EventView : public QTabWidget
{
public:
    explicit EventView(QWidget *parent = nullptr)
        : QTabWidget(parent)
    {
        auto *general = new QWidget;
        auto *g = new QGridLayout(general);
        g->setContentsMargins(8, 8, 8, 8);
        g->setHorizontalSpacing(14);
        g->setVerticalSpacing(5);
        m_message = new QPlainTextEdit;
        m_message->setReadOnly(true);
        m_message->setFixedHeight(64);
        g->addWidget(m_message, 0, 0, 1, 4);
        auto field = [this, g](int row, int col, const QString &label) {
            g->addWidget(new QLabel(label), row, col);
            auto *v = new QLabel;
            v->setTextInteractionFlags(Qt::TextSelectableByMouse);
            g->addWidget(v, row, col + 1);
            return v;
        };
        m_log = field(1, 0, QStringLiteral("Ім'я журналу:"));
        m_source = field(2, 0, QStringLiteral("Джерело:"));
        m_date = field(2, 2, QStringLiteral("Дата:"));
        m_code = field(3, 0, QStringLiteral("Код події:"));
        m_category = field(3, 2, QStringLiteral("Категорія завдання:"));
        m_level = field(4, 0, QStringLiteral("Рівень:"));
        m_keywords = field(4, 2, QStringLiteral("Ключові слова:"));
        m_user = field(5, 0, QStringLiteral("Користувач:"));
        m_computer = field(5, 2, QStringLiteral("Комп'ютер:"));
        m_opcode = field(6, 0, QStringLiteral("Код операції:"));
        g->addWidget(new QLabel(QStringLiteral("Докладніше:")), 7, 0);
        m_help = new Link(QStringLiteral("Довідка журналу подій в Інтернеті"), [this] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.google.com/search?q=") + QUrl::toPercentEncoding(m_current.source + QLatin1Char(' ') + m_current.message.left(80))));
        });
        m_help->setWordWrap(false);
        g->addWidget(m_help, 7, 1, 1, 3, Qt::AlignLeft);
        g->setColumnStretch(1, 1);
        g->setColumnStretch(3, 1);
        g->setRowStretch(8, 1);
        addTab(general, QStringLiteral("Загальні"));

        auto *details = new QWidget;
        auto *dv = new QVBoxLayout(details);
        dv->setContentsMargins(8, 6, 8, 8);
        auto *mode = new QHBoxLayout;
        m_friendlyMode = new QRadioButton(QStringLiteral("Подання для зручного читання"));
        m_xmlMode = new QRadioButton(QStringLiteral("Подання XML"));
        m_friendlyMode->setChecked(true);
        mode->addWidget(m_friendlyMode);
        mode->addWidget(m_xmlMode);
        mode->addStretch(1);
        dv->addLayout(mode);
        m_detailStack = new QStackedWidget;
        m_friendly = new QTreeWidget;
        m_friendly->setHeaderHidden(true);
        m_friendly->setRootIsDecorated(false); // Windows 7 shows "- System" as plain text, without tree lines
        m_friendly->setItemsExpandable(false);
        m_friendly->setIndentation(24);
        m_friendly->setFrameShape(QFrame::StyledPanel);
        m_friendly->setColumnCount(2);
        m_friendly->setColumnWidth(0, 200);
        m_xml = new QPlainTextEdit;
        m_xml->setReadOnly(true);
        m_xml->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_detailStack->addWidget(m_friendly);
        m_detailStack->addWidget(m_xml);
        dv->addWidget(m_detailStack, 1);
        addTab(details, QStringLiteral("Докладно"));
        connect(m_xmlMode, &QRadioButton::toggled, m_detailStack, [this](bool on) { m_detailStack->setCurrentIndex(on ? 1 : 0); });
        connect(this, &QTabWidget::currentChanged, this, [this](int i) {
            if (i == 1) {
                fillDetails();
            }
        });
    }

    void show(const Event &e, const LogDef &def)
    {
        m_current = e;
        m_def = def;
        m_full = QJsonObject();
        m_message->setPlainText(e.message);
        m_log->setText(e.log);
        m_source->setText(e.source);
        m_date->setText(e.when().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")));
        m_code->setText(QString::number(e.code));
        m_category->setText(QStringLiteral("Відсутня"));
        m_level->setText(levelName(e.level));
        m_keywords->setText(e.level == AuditSuccess ? QStringLiteral("Аудит успіху") : e.level == AuditFailure ? QStringLiteral("Аудит відмови") : QStringLiteral("Класичний"));
        m_user->setText(e.user);
        m_computer->setText(e.computer);
        m_opcode->setText(QStringLiteral("Відомості"));
        if (currentIndex() == 1) {
            fillDetails();
        }
    }

    QString plainText() const
    {
        QJsonObject full = m_full;
        if (full.isEmpty()) {
            full = fullEntry(m_current, m_def);
        }
        return QStringLiteral("Ім'я журналу: %1\nДжерело:      %2\nДата:         %3\nКод події:    %4\nКатегорія завдання: Відсутня\nРівень:       %5\n"
                              "Ключові слова: %6\nКористувач:   %7\nКомп'ютер:    %8\nОпис:\n%9\nXML події:\n%10\n")
            .arg(m_current.log, m_current.source, m_current.when().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")), QString::number(m_current.code),
                 levelName(m_current.level), m_keywords->text(), m_current.user, m_current.computer, m_current.message)
            .arg(eventXml(m_current, full));
    }

private:
    void fillDetails()
    {
        if (m_full.isEmpty()) {
            m_full = fullEntry(m_current, m_def);
        }
        m_xml->setPlainText(eventXml(m_current, m_full));
        m_friendly->clear();
        auto *sys = new QTreeWidgetItem(m_friendly, {QStringLiteral("- System")});
        auto add = [](QTreeWidgetItem *p, const QString &k, const QString &v) {
            new QTreeWidgetItem(p, {k, v});
        };
        add(sys, QStringLiteral("Provider [ Name]"), m_current.source);
        add(sys, QStringLiteral("EventID"), QString::number(m_current.code));
        add(sys, QStringLiteral("Level"), QString::number(xmlLevel(m_current.level)));
        add(sys, QStringLiteral("Task"), QStringLiteral("0"));
        add(sys, QStringLiteral("Keywords"), QStringLiteral("0x80000000000000"));
        add(sys, QStringLiteral("TimeCreated [ SystemTime]"), m_current.when().toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz000000Z")));
        add(sys, QStringLiteral("EventRecordID"), QString::number(m_current.record));
        add(sys, QStringLiteral("Execution [ ProcessID]"), QString::number(m_current.pid));
        add(sys, QStringLiteral("Channel"), m_current.channel);
        add(sys, QStringLiteral("Computer"), m_current.computer);
        auto *data = new QTreeWidgetItem(m_friendly, {QStringLiteral("- EventData")});
        add(data, QStringLiteral("Message"), m_current.message);
        QStringList keys = m_full.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString &k : std::as_const(keys)) {
            if (k != QLatin1String("MESSAGE") && !k.startsWith(QLatin1String("__"))) {
                add(data, k, jsonString(m_full.value(k)));
            }
        }
        for (QTreeWidgetItem *top : {sys, data}) {
            QFont f = top->font(0);
            f.setBold(true);
            top->setFont(0, f);
        }
        m_friendly->expandAll();
    }

    Event m_current;
    LogDef m_def;
    QJsonObject m_full;
    QPlainTextEdit *m_message;
    QLabel *m_log, *m_source, *m_date, *m_code, *m_category, *m_level, *m_keywords, *m_user, *m_computer, *m_opcode;
    Link *m_help;
    QRadioButton *m_friendlyMode;
    QRadioButton *m_xmlMode;
    QStackedWidget *m_detailStack;
    QTreeWidget *m_friendly;
    QPlainTextEdit *m_xml;
};

/* ---------------- painted parts of the Windows 7 Event Viewer ---------------- */

/** list header of the Windows 7 Event Viewer: Vista style, white top half, faint grey bottom half, thin separators */
class VistaHeader : public QHeaderView
{
public:
    explicit VistaHeader(QWidget *parent = nullptr)
        : QHeaderView(Qt::Horizontal, parent)
    {
        setSectionsClickable(true);
        setHighlightSections(false);
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setFixedHeight(24);
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        {
            QPainter p(viewport());
            paintBack(p, viewport()->rect(), false, false);
        }
        QHeaderView::paintEvent(e);
    }
    static void paintBack(QPainter &p, const QRect &r, bool hover, bool pressed)
    {
        const int mid = r.top() + r.height() / 2 - 2;
        if (pressed) {
            p.fillRect(r, QColor(0xbc, 0xe4, 0xf9));
        } else if (hover) {
            p.fillRect(QRect(r.left(), r.top(), r.width(), mid - r.top()), QColor(0xe3, 0xf7, 0xff));
            QLinearGradient g(0, mid, 0, r.bottom());
            g.setColorAt(0, QColor(0xbd, 0xed, 0xff));
            g.setColorAt(1, QColor(0xb7, 0xe7, 0xfb));
            p.fillRect(QRect(r.left(), mid, r.width(), r.bottom() - mid), g);
        } else {
            p.fillRect(QRect(r.left(), r.top(), r.width(), mid - r.top()), Qt::white);
            QLinearGradient g(0, mid, 0, r.bottom());
            g.setColorAt(0, QColor(0xf7, 0xf7, 0xf9));
            g.setColorAt(1, QColor(0xed, 0xef, 0xf1));
            p.fillRect(QRect(r.left(), mid, r.width(), r.bottom() - mid), g);
        }
        p.setPen(QColor(0xd5, 0xd5, 0xd7));
        p.drawLine(r.left(), r.bottom(), r.right(), r.bottom());
    }
    void paintSection(QPainter *p, const QRect &rect, int logical) const override
    {
        if (!rect.isValid()) {
            return;
        }
        p->save();
        const QPoint cur = viewport()->mapFromGlobal(QCursor::pos());
        const bool hover = rect.contains(cur) && viewport()->underMouse();
        paintBack(*p, rect, hover, hover && (QApplication::mouseButtons() & Qt::LeftButton));
        p->setPen(hover ? QColor(0x96, 0xd9, 0xf9) : QColor(0xe2, 0xe4, 0xe6));
        p->drawLine(rect.right(), rect.top(), rect.right(), rect.bottom() - 1);
        p->setPen(Qt::black);
        const QString t = model() ? model()->headerData(logical, orientation()).toString() : QString();
        const QRect tr = rect.adjusted(6, 0, -5, -1);
        const int align = model() ? model()->headerData(logical, orientation(), Qt::TextAlignmentRole).toInt() : 0;
        p->drawText(tr, (align & Qt::AlignRight ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, fontMetrics().elidedText(t, Qt::ElideRight, tr.width()));
        if (isSortIndicatorShown() && sortIndicatorSection() == logical) {
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0x97, 0xa5, 0xb6));
            const qreal cx = rect.center().x() + 0.5, y = rect.top() + 1;
            if (sortIndicatorOrder() == Qt::AscendingOrder) {
                p->drawPolygon(QPolygonF({QPointF(cx - 3.5, y + 4), QPointF(cx + 3.5, y + 4), QPointF(cx, y + 0.5)}));
            } else {
                p->drawPolygon(QPolygonF({QPointF(cx - 3.5, y + 0.5), QPointF(cx + 3.5, y + 0.5), QPointF(cx, y + 4)}));
            }
        }
        p->restore();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        QHeaderView::mouseMoveEvent(e);
        viewport()->update();
    }
    void leaveEvent(QEvent *e) override
    {
        QHeaderView::leaveEvent(e);
        viewport()->update();
    }
};

/** an empty status bar panel: just the etched line on its left */
class StatusPanel : public QWidget
{
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(QColor(0xd9, 0xd7, 0xd5));
        p.drawLine(0, 0, 0, height());
    }
};

/** list text of the summary page: dark grey like in Windows 7, unless the item has its own colour */
class GreyText : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

public:
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        // the widget style paints item text black whatever the palette says, so the text is drawn here
        QStyleOptionViewItem o = option;
        initStyleOption(&o, index);
        const QString text = o.text;
        o.text.clear();
        const QWidget *w = option.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &o, p, w);
        const QVariant fg = index.data(Qt::ForegroundRole);
        const QRect r = style->subElementRect(QStyle::SE_ItemViewItemText, &o, w).adjusted(2, 0, -2, 0);
        p->save();
        p->setFont(o.font);
        p->setPen(o.state & QStyle::State_Selected ? o.palette.color(QPalette::HighlightedText) : fg.isValid() ? fg.value<QBrush>().color() : QColor(0x57, 0x57, 0x57));
        p->drawText(r, int(o.displayAlignment), o.fontMetrics.elidedText(text, Qt::ElideRight, r.width()));
        p->restore();
    }
};

/** the frames of the three panes: the tree has a thin grey line, the middle and the action pane a sunken client edge */
class PaneFrame : public QWidget
{
public:
    PaneFrame(QWidget *child, bool clientEdge)
        : m_edge(clientEdge)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(clientEdge ? QMargins(2, 2, 1, 1) : QMargins(1, 1, 1, 1));
        v->setSpacing(0);
        v->addWidget(child);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect r = rect();
        if (!m_edge) {
            p.setPen(QColor(0x85, 0x85, 0x85));
            p.drawRect(r.adjusted(0, 0, -1, -1));
            return;
        }
        p.setPen(QColor(0xa2, 0xa2, 0xa2));
        p.drawLine(r.topLeft(), r.topRight());
        p.drawLine(r.topLeft(), r.bottomLeft());
        p.setPen(QColor(0x69, 0x69, 0x69));
        p.drawLine(r.left() + 1, r.top() + 1, r.right() - 1, r.top() + 1);
        p.drawLine(r.left() + 1, r.top() + 1, r.left() + 1, r.bottom() - 1);
        p.setPen(QColor(0xe3, 0xe3, 0xe3));
        p.drawLine(r.bottomLeft(), r.bottomRight());
        p.drawLine(r.topRight(), r.bottomRight());
    }

private:
    bool m_edge;
};

/** the dark grey band at the top of the middle pane ("Перегляд подій (Локальний)", "Система  Кількість подій: 5") */
class DarkHeader : public QWidget
{
public:
    explicit DarkHeader(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(22);
    }
    void setText(const QString &bold, const QString &rest = QString(), const QString &icon = QString())
    {
        m_bold = bold;
        m_rest = rest;
        m_icon = icon;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x68, 0x68, 0x69));
        p.setPen(QColor(0x8c, 0x8c, 0x8d));
        p.drawLine(0, height() - 2, width(), height() - 2);
        p.setPen(QColor(0xf8, 0xf8, 0xf9));
        p.drawLine(0, height() - 1, width(), height() - 1);
        int x = 5;
        if (!m_icon.isEmpty()) {
            p.drawPixmap(x, (height() - 2 - 16) / 2, QIcon::fromTheme(m_icon).pixmap(16, 16));
            x += 22;
        }
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::white);
        const QRect r(x, 0, width() - x - 6, height() - 2);
        p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, m_bold);
        if (!m_rest.isEmpty()) {
            const int w = QFontMetrics(f).horizontalAdvance(m_bold) + 24;
            p.setFont(font());
            p.drawText(r.adjusted(w, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, m_rest);
        }
    }

private:
    QString m_bold, m_rest, m_icon;
};

/** "Огляд і зведення" with "Останнє оновлення: ..." on the right */
class TitleBand : public QWidget
{
public:
    explicit TitleBand(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(29);
    }
    void setRight(const QString &t)
    {
        m_right = t;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x9f, 0x9f, 0x9f));
        p.setPen(QColor(0x9d, 0x9d, 0x9e));
        p.drawLine(0, 0, width(), 0);
        p.setPen(QColor(0x69, 0x69, 0x6a));
        p.drawLine(0, 1, width(), 1);
        QFont f = font();
        f.setPixelSize(18);
        f.setWeight(QFont::Normal);
        f.setHintingPreference(QFont::PreferNoHinting);
        p.setFont(f);
        p.setPen(QColor(0xf7, 0xf7, 0xf7));
        p.drawText(rect().adjusted(17, 2, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Огляд і зведення"));
        p.setFont(font());
        p.setPen(QColor(0xf4, 0xf4, 0xf4));
        p.drawText(rect().adjusted(0, 0, -6, -3), Qt::AlignRight | Qt::AlignBottom, m_right);
    }

private:
    QString m_right;
};

/** collapsible block of the summary page: grey gradient bar with ▲, then the content */
class Section : public QWidget
{
public:
    Section(const QString &title, QWidget *content)
        : m_title(title)
        , m_content(content)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        m_bar = new QWidget;
        m_bar->setFixedHeight(24);
        m_bar->setCursor(Qt::PointingHandCursor);
        m_bar->installEventFilter(this);
        v->addWidget(m_bar);
        auto *frame = new QWidget;
        frame->setObjectName(QStringLiteral("win7evsect"));
        frame->setAttribute(Qt::WA_StyledBackground, true);
        frame->setStyleSheet(QStringLiteral("#win7evsect { background: #e8e8e8; border: none; }"));
        auto *fv = new QVBoxLayout(frame);
        fv->setContentsMargins(10, 10, 10, 10);
        fv->addWidget(content);
        m_frame = frame;
        v->addWidget(frame, 1);
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_bar && e->type() == QEvent::Paint) {
            QPainter p(m_bar);
            QLinearGradient g(0, 0, 0, m_bar->height());
            g.setColorAt(0, QColor(0xe7, 0xe7, 0xe7));
            g.setColorAt(0.92, QColor(0xaf, 0xaf, 0xaf));
            g.setColorAt(1, QColor(0xa8, 0xa8, 0xa8));
            p.fillRect(m_bar->rect(), g);
            p.setPen(QColor(0x65, 0x65, 0x65));
            p.drawRect(m_bar->rect().adjusted(0, 0, -1, -1));
            p.setPen(Qt::black);
            p.drawText(m_bar->rect().adjusted(8, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, m_title);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x30, 0x30, 0x30));
            p.setBrush(Qt::black);
            const qreal cx = m_bar->width() - 11.5, cy = m_bar->height() / 2.0 + 0.5;
            if (m_frame->isVisible()) {
                p.drawPolygon(QPolygonF({QPointF(cx - 3, cy + 1.5), QPointF(cx + 3, cy + 1.5), QPointF(cx, cy - 1.5)}));
            } else {
                p.drawPolygon(QPolygonF({QPointF(cx - 3, cy - 1.5), QPointF(cx + 3, cy - 1.5), QPointF(cx, cy + 1.5)}));
            }
            return true;
        }
        if (o == m_bar && e->type() == QEvent::MouseButtonRelease) {
            m_frame->setVisible(!m_frame->isVisible());
            m_bar->update();
            return true;
        }
        return QWidget::eventFilter(o, e);
    }

private:
    QString m_title;
    QWidget *m_content;
    QWidget *m_bar;
    QWidget *m_frame;
};

/* ---------------- action pane ---------------- */

struct ActionItem {
    QString text;
    QString icon;
    std::function<void()> run;
    bool enabled = true;
    bool separatorBefore = false;
    std::function<void(QMenu *)> submenu; // "Вигляд ▶", "Довідка ▶"
};

struct ActionGroup {
    QString title;
    QList<ActionItem> items;
};

/** one clickable row of the action pane */
class ActionRow : public QWidget
{
public:
    explicit ActionRow(const ActionItem &a)
        : m_a(a)
    {
        setFixedHeight(24);
        setAttribute(Qt::WA_Hover);
        setCursor(m_a.enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setEnabled(m_a.enabled);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        if (underMouse() && m_a.enabled) {
            drawHover(p, QRectF(rect()).adjusted(2.5, 0.5, -2.5, -0.5), false);
        }
        if (!m_a.icon.isEmpty()) {
            p.drawPixmap(6, (height() - 16) / 2, QIcon::fromTheme(m_a.icon).pixmap(16, 16, m_a.enabled ? QIcon::Normal : QIcon::Disabled));
        }
        p.setPen(m_a.enabled ? QColor(Qt::black) : QColor(0x8d, 0x8d, 0x8d));
        const QRect tr = rect().adjusted(31, 0, m_a.submenu ? -20 : -6, 0);
        p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(m_a.text, Qt::ElideRight, tr.width()));
        if (m_a.submenu) {
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::black);
            const qreal cx = width() - 12, cy = height() / 2.0;
            p.drawPolygon(QPolygonF({QPointF(cx - 2, cy - 4), QPointF(cx + 2, cy), QPointF(cx - 2, cy + 4)}));
        }
    }
    void enterEvent(QEnterEvent *) override
    {
        update();
    }
    void leaveEvent(QEvent *) override
    {
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton || !m_a.enabled) {
            return;
        }
        if (m_a.submenu) {
            QMenu m;
            m_a.submenu(&m);
            m.exec(mapToGlobal(QPoint(width() - 6, 0)));
            return;
        }
        if (m_a.run) {
            m_a.run();
        }
    }

private:
    ActionItem m_a;
};

class ActionGroupHeader : public QWidget
{
public:
    ActionGroupHeader(const QString &title, QWidget *body)
        : m_title(title)
        , m_body(body)
    {
        setFixedHeight(26);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(0, 0, width(), 1, Qt::white);
        QLinearGradient g(0, 1, 0, height());
        g.setColorAt(0, QColor(0xc7, 0xd6, 0xe6));
        g.setColorAt(0.5, QColor(0xae, 0xcc, 0xe8));
        g.setColorAt(0.52, QColor(0xa2, 0xc1, 0xdd));
        g.setColorAt(1, QColor(0xb6, 0xc4, 0xdc));
        p.fillRect(rect().adjusted(0, 1, 0, 0), g);
        p.setPen(Qt::black);
        p.drawText(rect().adjusted(8, 1, -24, 0), Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(m_title, Qt::ElideRight, width() - 32));
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        const qreal cx = width() - 14, cy = height() / 2.0;
        if (m_body->isVisible()) {
            p.drawPolygon(QPolygonF({QPointF(cx - 4, cy + 2), QPointF(cx + 4, cy + 2), QPointF(cx, cy - 2.5)}));
        } else {
            p.drawPolygon(QPolygonF({QPointF(cx - 4, cy - 2), QPointF(cx + 4, cy - 2), QPointF(cx, cy + 2.5)}));
        }
    }
    void mouseReleaseEvent(QMouseEvent *) override
    {
        m_body->setVisible(!m_body->isVisible());
        update();
    }

private:
    QString m_title;
    QWidget *m_body;
};

class ActionPane : public QWidget
{
public:
    explicit ActionPane(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("win7evactions"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("#win7evactions { background: white; }"));
        m_v = new QVBoxLayout(this);
        m_v->setContentsMargins(0, 0, 0, 0);
        m_v->setSpacing(0);
    }
    void setGroups(const QList<ActionGroup> &groups)
    {
        while (QLayoutItem *it = m_v->takeAt(0)) {
            if (it->widget()) {
                it->widget()->deleteLater();
            }
            delete it;
        }
        auto *head = new QLabel(QStringLiteral("Дії"));
        QFont f = head->font();
        f.setBold(true);
        head->setFont(f);
        head->setFixedHeight(21);
        head->setContentsMargins(8, 0, 0, 0);
        head->setStyleSheet(QStringLiteral("QLabel { background: #e8e8e8; border-top: 1px solid #fdfdfd; border-bottom: 1px solid #9e9e9e; }"));
        m_v->addWidget(head);
        for (const ActionGroup &g : groups) {
            auto *body = new QWidget;
            auto *bv = new QVBoxLayout(body);
            bv->setContentsMargins(0, 1, 0, 2);
            bv->setSpacing(0);
            for (const ActionItem &a : g.items) {
                if (a.separatorBefore) {
                    auto *line = new QFrame;
                    line->setFixedHeight(1);
                    line->setStyleSheet(QStringLiteral("background: #e8e8e8; margin-right: 4px;"));
                    bv->addWidget(line);
                }
                bv->addWidget(new ActionRow(a));
            }
            m_v->addWidget(new ActionGroupHeader(g.title, body));
            m_v->addWidget(body);
        }
        m_v->addStretch(1);
    }

private:
    QVBoxLayout *m_v;
};

/* ---------------- the window ---------------- */

/** sorts dates and numbers by value */
class EventItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &o) const override
    {
        const int c = treeWidget() ? treeWidget()->sortColumn() : 0;
        const QVariant a = data(c, Qt::UserRole + 5), b = o.data(c, Qt::UserRole + 5);
        if (a.isValid() && b.isValid()) {
            return QVariant::compare(a, b) == QPartialOrdering::Less;
        }
        return text(c).localeAwareCompare(o.text(c)) < 0;
    }
};

struct Node {
    enum Kind { Root, Folder, Log, CustomView, AdminView, SavedLog };
    Kind kind = Root;
    LogDef def;
    Filter filter;
    QString name;
    QList<LogDef> logs; // custom views read these
};

class EventViewer : public QMainWindow
{
public:
    /** Computer Management hosts the event viewer as one branch of its console tree; these are its nodes */
    struct Host {
        std::function<void(QTreeWidgetItem *)> open;                  // shows the node's page in middle()
        std::function<QList<ActionGroup>(QTreeWidgetItem *)> actions; // its part of the action pane
    };
    static constexpr int HostRole = Qt::UserRole + 40;

    explicit EventViewer(bool hosted = false)
    {
        setWindowTitle(QStringLiteral("Перегляд подій"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-ev-app")));
        resize(1040, 680);
        buildUi();
        if (hosted) {
            return; // the host builds its tree, then calls attachEvents()
        }
        fillTree();
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
        showRoot();
    }

    QTreeWidget *tree() const
    {
        return m_tree;
    }
    QStackedWidget *middle() const
    {
        return m_middle;
    }
    QToolBar *toolBar() const
    {
        return m_bar;
    }
    void setHost(const Host &host)
    {
        m_host = host;
    }
    /** a node of the host (not an event log) */
    QTreeWidgetItem *addHostNode(QTreeWidgetItem *parent, const QString &text, const QString &icon, int id)
    {
        auto *it = parent ? new QTreeWidgetItem(parent, {text}) : new QTreeWidgetItem(m_tree, {text});
        it->setIcon(0, QIcon::fromTheme(icon));
        it->setData(0, Qt::UserRole, -1);
        it->setData(0, HostRole, id);
        return it;
    }
    /** puts "Event Viewer (Local)" with its logs under `parent` of the host's tree */
    void attachEvents(QTreeWidgetItem *parent, int index)
    {
        m_hostParent = parent;
        m_hostIndex = index;
        fillTree();
    }
    QTreeWidgetItem *eventsRoot() const
    {
        return m_evRoot;
    }
    void setActionGroups(const QList<ActionGroup> &groups)
    {
        m_groups = groups;
        m_actions->setGroups(groups);
    }

private:
    static bool isHostItem(const QTreeWidgetItem *it)
    {
        return it && it->data(0, HostRole).isValid();
    }

    /* ----- layout ----- */
    void buildUi()
    {
        m_back = new QAction(Win7Disk::flatIcon(QStringLiteral("seven-mmc-back")), QStringLiteral("Назад"), this);
        m_forward = new QAction(Win7Disk::flatIcon(QStringLiteral("seven-mmc-forward")), QStringLiteral("Вперед"), this);
        m_back->setEnabled(false);
        m_forward->setEnabled(false);
        auto *console = new QAction(Win7Disk::flatIcon(QStringLiteral("seven-mmc-console")), QStringLiteral("Показати або приховати дерево консолі"), this);
        auto *help = new QAction(Win7Disk::flatIcon(QStringLiteral("seven-mmc-help")), QStringLiteral("Довідка"), this);
        auto *actionPane = new QAction(Win7Disk::flatIcon(QStringLiteral("seven-mmc-actionpane")), QStringLiteral("Показати або приховати область дій"), this);
        console->setCheckable(true);
        console->setChecked(true);
        actionPane->setCheckable(true);
        actionPane->setChecked(true);
        connect(m_back, &QAction::triggered, this, [this] { navigate(-1); });
        connect(m_forward, &QAction::triggered, this, [this] { navigate(+1); });
        connect(help, &QAction::triggered, this, [this] { showHelp(); });

        QMenu *file = menuBar()->addMenu(QStringLiteral("&Файл"));
        file->addAction(QIcon::fromTheme(QStringLiteral("seven-mmc-open")), QStringLiteral("Відкрити збережений журнал..."), this, [this] { openSavedLog(); });
        file->addSeparator();
        file->addAction(QStringLiteral("Параметри..."))->setEnabled(false);
        file->addSeparator();
        file->addAction(QStringLiteral("Вихід"), this, &QWidget::close);
        m_actionMenu = menuBar()->addMenu(QStringLiteral("&Дія"));
        connect(m_actionMenu, &QMenu::aboutToShow, this, [this] {
            m_actionMenu->clear();
            for (const ActionGroup &g : std::as_const(m_groups)) {
                for (const ActionItem &a : g.items) {
                    if (a.separatorBefore) {
                        m_actionMenu->addSeparator();
                    }
                    if (a.submenu) {
                        QMenu *sub = m_actionMenu->addMenu(QIcon::fromTheme(a.icon), a.text);
                        a.submenu(sub);
                        continue;
                    }
                    QAction *x = m_actionMenu->addAction(QIcon::fromTheme(a.icon), a.text);
                    x->setEnabled(a.enabled);
                    const auto run = a.run;
                    connect(x, &QAction::triggered, this, [run] {
                        if (run) {
                            run();
                        }
                    });
                }
                m_actionMenu->addSeparator();
            }
        });
        m_viewMenu = menuBar()->addMenu(QStringLiteral("&Вигляд"));
        connect(m_viewMenu, &QMenu::aboutToShow, this, [this] {
            m_viewMenu->clear();
            fillViewMenu(m_viewMenu);
        });
        QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("&Довідка"));
        helpMenu->addAction(QIcon::fromTheme(QStringLiteral("seven-mmc-help")), QStringLiteral("Довідка"), this, [this] { showHelp(); });
        helpMenu->addSeparator();
        helpMenu->addAction(QStringLiteral("Про програму «Перегляд подій»"), this, [this] {
            QMessageBox::about(this, QStringLiteral("Перегляд подій"),
                               QStringLiteral("Перегляд подій у стилі Windows 7.\nПоказує журнал systemd (journald) і журнал встановлення пакетів."));
        });

        QToolBar *bar = addToolBar(QStringLiteral("Стандартна"));
        m_bar = bar;
        bar->setMovable(false);
        bar->setIconSize(QSize(16, 16));
        bar->addAction(m_back);
        bar->addAction(m_forward);
        bar->addSeparator();
        bar->addAction(console);
        bar->addSeparator();
        bar->addAction(help);
        bar->addAction(actionPane);
        Win7Disk::mmcChrome(menuBar(), bar);
        win7WrapMenuBar(this); // Windows 7: the menu bar wraps instead of showing a "»" arrow

        m_tree = new QTreeWidget;
        m_tree->setHeaderHidden(true);
        m_tree->setIconSize(QSize(16, 16));
        m_tree->setFrameShape(QFrame::NoFrame);
        m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
        m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn); // like the MMC console tree
        m_tree->header()->setStretchLastSection(false);
        m_tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_tree->setStyleSheet(QString(
            "QTreeView { background: white; outline: 0; show-decoration-selected: 0; }"
            "QTreeView::branch { background: white; border-image: none; image: none; }"
            "QTreeView::branch:has-children:closed { image: url(" + arrowPath(false) + "); }"
            "QTreeView::branch:has-children:open { image: url(" + arrowPath(true) + "); }") + Win7States::itemViewSheet());
        m_middle = new QStackedWidget;
        m_actions = new ActionPane;
        auto *actionScroll = new QScrollArea;
        actionScroll->setWidget(m_actions);
        actionScroll->setWidgetResizable(true);
        actionScroll->setFrameShape(QFrame::NoFrame);
        actionScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_split = new QSplitter(Qt::Horizontal);
        auto *treePane = new PaneFrame(m_tree, false);
        auto *actionsPane = new PaneFrame(actionScroll, true);
        m_split->addWidget(treePane);
        m_split->addWidget(new PaneFrame(m_middle, true));
        m_split->addWidget(actionsPane);
        m_split->setChildrenCollapsible(false);
        m_split->setStretchFactor(1, 1);
        m_split->setSizes({200, 600, 220});
        m_split->setHandleWidth(3);
        m_split->setStyleSheet(QStringLiteral("QSplitter::handle { background: #f0f0f0; }"));
        connect(console, &QAction::toggled, treePane, &QWidget::setVisible);
        connect(actionPane, &QAction::toggled, actionsPane, &QWidget::setVisible);
        auto *frame = new QWidget;
        frame->setObjectName(QStringLiteral("win7evframe"));
        frame->setAttribute(Qt::WA_StyledBackground, true);
        frame->setStyleSheet(QStringLiteral("#win7evframe { background: #f0f0f0; }"));
        auto *fv = new QVBoxLayout(frame);
        fv->setContentsMargins(0, 2, 0, 2);
        fv->addWidget(m_split);
        setCentralWidget(frame);
        statusBar()->setSizeGripEnabled(true);
        statusBar()->setStyleSheet(QStringLiteral("QStatusBar { background: #f1eeec; } QStatusBar::item { border: none; }"));
        // the two empty panels on the right of the MMC status bar
        for (int w : {133, 85}) {
            auto *panel = new StatusPanel;
            panel->setFixedSize(w, 20);
            statusBar()->addPermanentWidget(panel);
        }

        // pages: summary, log, folder
        m_middle->addWidget(buildSummaryPage());
        m_middle->addWidget(buildLogPage());
        m_middle->addWidget(buildFolderPage());

        connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it, QTreeWidgetItem *prev) {
            if (!it) {
                return;
            }
            if (!m_navigating && prev) {
                m_history.append(prev);
                m_future.clear();
            }
            m_back->setEnabled(!m_history.isEmpty());
            m_forward->setEnabled(!m_future.isEmpty());
            openNode(it);
        });
        connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            QTreeWidgetItem *it = m_tree->itemAt(pos);
            if (!it) {
                return;
            }
            m_tree->setCurrentItem(it);
            QMenu m;
            for (const ActionGroup &g : std::as_const(m_groups)) {
                for (const ActionItem &a : g.items) {
                    if (a.separatorBefore) {
                        m.addSeparator();
                    }
                    if (a.submenu) {
                        a.submenu(m.addMenu(a.text));
                        continue;
                    }
                    QAction *x = m.addAction(QIcon::fromTheme(a.icon), a.text);
                    x->setEnabled(a.enabled);
                    const auto run = a.run;
                    connect(x, &QAction::triggered, this, [run] {
                        if (run) {
                            run();
                        }
                    });
                }
                break; // only the group of the node
            }
            m.exec(m_tree->viewport()->mapToGlobal(pos));
        });
    }

    QWidget *pageWithHeader(DarkHeader *&h)
    {
        auto *w = new QWidget;
        w->setObjectName(QStringLiteral("win7evpage"));
        w->setAttribute(Qt::WA_StyledBackground, true);
        w->setStyleSheet(QStringLiteral("#win7evpage { background: white; }"));
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        h = new DarkHeader;
        v->addWidget(h);
        return w;
    }

    /* ----- summary page ----- */
    QTreeWidget *table(const QStringList &headers, const QList<int> &widths)
    {
        auto *t = new QTreeWidget;
        t->setHeader(new VistaHeader(t));
        t->setHeaderLabels(headers);
        t->setRootIsDecorated(false);
        t->setUniformRowHeights(true);
        t->setFrameShape(QFrame::StyledPanel);
        for (int i = 0; i < widths.size(); ++i) {
            t->setColumnWidth(i, widths.at(i));
        }
        t->setItemDelegate(new GreyText(t));
        t->setStyleSheet(QStringLiteral("QTreeView { background: white; border: 1px solid #858585; color: #575757; } QTreeView::item { height: 19px; color: #575757; }"));
        return t;
    }

    QWidget *buildSummaryPage()
    {
        DarkHeader *h;
        QWidget *page = pageWithHeader(h);
        h->setText(QStringLiteral("Перегляд подій (Локальний)"));
        m_title = new TitleBand;
        page->layout()->addWidget(m_title);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *inner = new QWidget;
        inner->setObjectName(QStringLiteral("win7evsum"));
        inner->setAttribute(Qt::WA_StyledBackground, true);
        inner->setStyleSheet(QStringLiteral("#win7evsum { background: white; }"));
        auto *v = new QVBoxLayout(inner);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(10);
        auto *overview = new QWidget;
        auto *oh = new QHBoxLayout(overview);
        oh->setContentsMargins(0, 0, 0, 0);
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-ev-app")).pixmap(32, 32));
        oh->addWidget(ic, 0, Qt::AlignTop);
        auto *ot = new QLabel(QStringLiteral("Щоб переглянути події, що відбулися на комп'ютері, виберіть у дереві консолі відповідне джерело, журнал або вузол "
                                             "настроюваного подання. Настроюване подання «Адміністративні події» містить усі адміністративні події незалежно "
                                             "від джерела. Зведене подання всіх журналів наведено нижче."));
        ot->setWordWrap(true);
        oh->addWidget(ot, 1);
        v->addWidget(new Section(QStringLiteral("Огляд"), overview));
        m_admin = new QTreeWidget;
        m_admin->setHeader(new VistaHeader(m_admin));
        m_admin->setHeaderLabels({QStringLiteral("Тип події"), QStringLiteral("Код події"), QStringLiteral("Джерело"), QStringLiteral("Журнал"), QStringLiteral("Остання година"),
                                  QStringLiteral("24 години"), QStringLiteral("7 днів")});
        for (int i : {0, 1, 2, 3, 4, 5, 6}) {
            m_admin->setColumnWidth(i, QList<int>{130, 75, 120, 80, 85, 70, 70}.at(i));
        }
        m_admin->setFrameShape(QFrame::StyledPanel);
        m_admin->setMinimumHeight(160);
        m_admin->setItemDelegate(new GreyText(m_admin));
        m_admin->setStyleSheet(QStringLiteral("QTreeView { background: white; border: 1px solid #858585; color: #575757; } QTreeView::item { height: 19px; color: #575757; }"
                                              "QTreeView::branch { background: white; border-image: none; image: none; }"
                                              "QTreeView::branch:has-children:closed { image: url(%1); }"
                                              "QTreeView::branch:has-children:open { image: url(%2); }")
                                  .arg(boxPath(false), boxPath(true)));
        connect(m_admin, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) { openSummaryItem(it); });
        v->addWidget(new Section(QStringLiteral("Зведення адміністративних подій"), m_admin));
        m_recent = table({QStringLiteral("Ім'я"), QStringLiteral("Опис"), QStringLiteral("Змінено"), QStringLiteral("Створено")}, {220, 110, 150, 150});
        m_recent->setMinimumHeight(130);
        connect(m_recent, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) { selectPath(it->data(0, Qt::UserRole).toString()); });
        v->addWidget(new Section(QStringLiteral("Нещодавно переглянуті вузли"), m_recent));
        m_logSummary = table({QStringLiteral("Ім'я журналу"), QStringLiteral("Розмір (поточний/максимальний)"), QStringLiteral("Змінено"), QStringLiteral("Увімкнуто"),
                              QStringLiteral("Політика збереження")},
                             {170, 190, 150, 90, 260});
        m_logSummary->setMinimumHeight(170);
        connect(m_logSummary, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) { selectPath(it->data(0, Qt::UserRole).toString()); });
        v->addWidget(new Section(QStringLiteral("Зведення журналу"), m_logSummary));
        v->addStretch(1);
        scroll->setWidget(inner);
        page->layout()->addWidget(scroll);
        return page;
    }

    /* ----- log page ----- */
    QWidget *buildLogPage()
    {
        QWidget *page = pageWithHeader(m_logHeader);
        m_filterBar = new QLabel;
        m_filterBar->setStyleSheet(QStringLiteral("QLabel { background: white; border-bottom: 1px solid #d5d5d5; padding: 4px 6px 4px 6px; }"));
        m_filterBar->hide();
        page->layout()->addWidget(m_filterBar);
        auto *split = new QSplitter(Qt::Vertical);
        m_list = new QTreeWidget;
        m_list->setHeader(new VistaHeader(m_list));
        m_list->setHeaderLabels({QStringLiteral("Рівень"), QStringLiteral("Дата й час"), QStringLiteral("Джерело"), QStringLiteral("Код події"), QStringLiteral("Категорія завдання")});
        for (int i : {0, 1, 2, 3, 4}) {
            m_list->setColumnWidth(i, QList<int>{115, 135, 140, 80, 130}.at(i));
        }
        m_list->setRootIsDecorated(false);
        m_list->setUniformRowHeights(true);
        m_list->setFrameShape(QFrame::NoFrame);
        m_list->setSortingEnabled(true);
        m_list->setAllColumnsShowFocus(true);
        m_list->setContextMenuPolicy(Qt::CustomContextMenu);
        m_list->setStyleSheet(QStringLiteral("QTreeView { background: white; outline: 0; } QTreeView::item { height: 18px; }"
                                             "QTreeView::item:selected { background: #3399ff; color: white; }"));
        split->addWidget(m_list);
        auto *preview = new QWidget;
        auto *pv = new QVBoxLayout(preview);
        pv->setContentsMargins(0, 0, 0, 0);
        pv->setSpacing(0);
        auto *ph = new QHBoxLayout;
        ph->setContentsMargins(6, 2, 4, 2);
        m_previewTitle = new QLabel;
        ph->addWidget(m_previewTitle, 1);
        auto *close = new QToolButton;
        close->setText(QStringLiteral("✕"));
        close->setAutoRaise(true);
        ph->addWidget(close);
        auto *phw = new QWidget;
        phw->setLayout(ph);
        phw->setStyleSheet(QStringLiteral("QWidget { background: #f0f0f0; } QLabel { color: black; }"));
        pv->addWidget(phw);
        m_view = new EventView;
        pv->addWidget(m_view, 1);
        split->addWidget(preview);
        split->setSizes({330, 250});
        m_preview = preview;
        page->layout()->addWidget(split);
        connect(close, &QToolButton::clicked, this, [this] {
            m_preview->hide();
            m_previewOn = false;
        });
        connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
            if (!it) {
                return;
            }
            const Event &e = m_shown.at(it->data(0, Qt::UserRole).toInt());
            m_previewTitle->setText(QStringLiteral("Подія %1, %2").arg(e.code).arg(e.source));
            m_view->show(e, defFor(e));
            refreshActions();
        });
        connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this] { eventProperties(); });
        connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            if (!m_list->itemAt(pos)) {
                return;
            }
            QMenu m;
            for (const ActionItem &a : eventActions()) {
                if (a.separatorBefore) {
                    m.addSeparator();
                }
                if (a.submenu) {
                    a.submenu(m.addMenu(QIcon::fromTheme(a.icon), a.text));
                    continue;
                }
                QAction *x = m.addAction(QIcon::fromTheme(a.icon), a.text);
                x->setEnabled(a.enabled);
                const auto run = a.run;
                connect(x, &QAction::triggered, this, [run] {
                    if (run) {
                        run();
                    }
                });
            }
            m.exec(m_list->viewport()->mapToGlobal(pos));
        });
        return page;
    }

    QWidget *buildFolderPage()
    {
        QWidget *page = pageWithHeader(m_folderHeader);
        m_folderList = table({QStringLiteral("Ім'я"), QStringLiteral("Тип"), QStringLiteral("Кількість подій"), QStringLiteral("Розмір")}, {220, 130, 120, 100});
        m_folderList->setItemDelegate(new QStyledItemDelegate(m_folderList));
        m_folderList->setStyleSheet(QStringLiteral("QTreeView { background: white; border: none; } QTreeView::item { height: 19px; }"));
        connect(m_folderList, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) { selectPath(it->data(0, Qt::UserRole).toString()); });
        page->layout()->addWidget(m_folderList);
        return page;
    }

    /* ----- tree ----- */
    QTreeWidgetItem *addNode(QTreeWidgetItem *parent, const QString &text, const QString &icon, const Node &n)
    {
        auto *it = parent ? new QTreeWidgetItem(parent, {text}) : new QTreeWidgetItem(m_tree, {text});
        it->setIcon(0, QIcon::fromTheme(icon));
        const int id = m_nodes.size();
        m_nodes.append(n);
        it->setData(0, Qt::UserRole, id);
        return it;
    }

    QString pathOf(QTreeWidgetItem *it) const
    {
        QStringList p;
        for (; it; it = it->parent()) {
            p.prepend(it->text(0));
        }
        return p.join(QLatin1Char('\\'));
    }

    void fillTree()
    {
        m_history.clear();
        m_future.clear();
        if (m_hostParent) {
            // only our branch of the host's tree is rebuilt
            delete m_evRoot;
        } else {
            m_tree->clear();
        }
        m_nodes.clear();
        Node root;
        root.kind = Node::Root;
        QTreeWidgetItem *r;
        if (m_hostParent) {
            r = new QTreeWidgetItem({QStringLiteral("Перегляд подій")});
            m_hostParent->insertChild(qMin(m_hostIndex, m_hostParent->childCount()), r);
            r->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-ev-app")));
            r->setData(0, Qt::UserRole, int(m_nodes.size()));
            m_nodes.append(root);
        } else {
            r = addNode(nullptr, QStringLiteral("Перегляд подій (Локальний)"), QStringLiteral("seven-ev-app"), root);
        }
        m_evRoot = r;
        Node folder;
        folder.kind = Node::Folder;
        auto *custom = addNode(r, QStringLiteral("Настроювані подання"), QStringLiteral("seven-ev-connect"), folder);
        Node admin;
        admin.kind = Node::AdminView;
        admin.name = QStringLiteral("Адміністративні події");
        admin.filter.levels = {Critical, Error, Warning};
        admin.logs = {windowsLogs().at(0), windowsLogs().at(1), windowsLogs().at(3)};
        addNode(custom, admin.name, QStringLiteral("seven-ev-filter"), admin);
        KConfigGroup views(KSharedConfig::openConfig(QStringLiteral("seven-eventvwrrc")), QStringLiteral("CustomViews"));
        for (const QString &name : views.groupList()) {
            Node cv;
            cv.kind = Node::CustomView;
            cv.name = name;
            cv.filter = Filter::load(views.group(name));
            const QStringList keys = views.group(name).readEntry("Logs", QStringList());
            for (const LogDef &d : windowsLogs() + serviceLogs()) {
                if (keys.contains(d.key)) {
                    cv.logs << d;
                }
            }
            addNode(custom, name, QStringLiteral("seven-ev-filter"), cv);
        }
        auto *win = addNode(r, QStringLiteral("Журнали Windows"), QStringLiteral("seven-ev-winlogs"), folder);
        for (const LogDef &d : windowsLogs()) {
            Node n;
            n.kind = Node::Log;
            n.def = d;
            addNode(win, d.title, d.args.isEmpty() && !d.dpkg ? QStringLiteral("seven-ev-log") : QStringLiteral("seven-ev-lognode"), n);
        }
        auto *apps = addNode(r, QStringLiteral("Журнали програм і служб"), QStringLiteral("seven-ev-view"), folder);
        for (const LogDef &d : serviceLogs()) {
            Node n;
            n.kind = Node::Log;
            n.def = d;
            addNode(apps, d.title, QStringLiteral("seven-ev-lognode"), n);
        }
        Node subs;
        subs.kind = Node::Folder;
        subs.name = QStringLiteral("subscriptions");
        addNode(r, QStringLiteral("Підписки"), QStringLiteral("seven-ev-subscr"), subs);
        if (!m_saved.isEmpty()) {
            auto *saved = addNode(r, QStringLiteral("Збережені журнали"), QStringLiteral("seven-ev-folder"), folder);
            for (const LogDef &d : std::as_const(m_saved)) {
                Node n;
                n.kind = Node::SavedLog;
                n.def = d;
                addNode(saved, d.title, QStringLiteral("seven-ev-lognode"), n);
            }
        }
        r->setExpanded(true);
    }

    Node *currentNode()
    {
        QTreeWidgetItem *it = m_tree->currentItem();
        return it && !isHostItem(it) ? &m_nodes[it->data(0, Qt::UserRole).toInt()] : nullptr;
    }

    void selectPath(const QString &path)
    {
        QTreeWidgetItemIterator it(m_tree);
        for (; *it; ++it) {
            if (pathOf(*it) == path) {
                if ((*it)->parent()) {
                    (*it)->parent()->setExpanded(true);
                }
                m_tree->setCurrentItem(*it);
                return;
            }
        }
    }

    void navigate(int dir)
    {
        QList<QTreeWidgetItem *> &from = dir < 0 ? m_history : m_future;
        QList<QTreeWidgetItem *> &to = dir < 0 ? m_future : m_history;
        if (from.isEmpty()) {
            return;
        }
        to.append(m_tree->currentItem());
        m_navigating = true;
        m_tree->setCurrentItem(from.takeLast());
        m_navigating = false;
        m_back->setEnabled(!m_history.isEmpty());
        m_forward->setEnabled(!m_future.isEmpty());
    }

    void openNode(QTreeWidgetItem *it)
    {
        if (isHostItem(it)) {
            statusBar()->clearMessage();
            if (m_host.open) {
                m_host.open(it);
            }
            setActionGroups(m_host.actions ? m_host.actions(it) : QList<ActionGroup>());
            return;
        }
        Node &n = m_nodes[it->data(0, Qt::UserRole).toInt()];
        statusBar()->clearMessage();
        if (n.kind == Node::Root) {
            showRoot();
        } else if (n.kind == Node::Folder) {
            showFolder(it);
        } else {
            remember(it);
            showLog(it, n);
        }
        refreshActions();
    }

    /* ----- summary ----- */
    void showRoot()
    {
        m_middle->setCurrentIndex(0);
        fillRecent();
        if (m_summaryLoaded) {
            return;
        }
        m_summaryLoaded = true;
        m_title->setRight(QStringLiteral("Читання даних. Зачекайте, будь ласка..."));
        m_admin->clear();
        new QTreeWidgetItem(m_admin, {QStringLiteral("Читання журналу «Система»...")});
        QPointer<EventViewer> self(this);
        QThread *t = QThread::create([self] {
            // counts per type, source and log for the last hour / 24 hours / 7 days
            struct Count {
                int h = 0, d = 0, w = 0;
            };
            QMap<int, QMap<QPair<QString, QString>, Count>> data; // level -> (source, log) -> counts
            const QList<QPair<QString, int>> windows{{QStringLiteral("-1h"), 0}, {QStringLiteral("-24h"), 1}, {QStringLiteral("-7d"), 2}};
            const QList<QPair<QString, int>> prios{{QStringLiteral("0..2"), Critical}, {QStringLiteral("3"), Error}, {QStringLiteral("4"), Warning}, {QStringLiteral("5..6"), Information}};
            const QList<QPair<QString, QString>> scopes{{QStringLiteral("--system"), QStringLiteral("Система")}, {QStringLiteral("--user"), QStringLiteral("Програма")}};
            for (const auto &[scope, logName] : scopes) {
                for (const auto &[prio, level] : prios) {
                    for (const auto &[since, idx] : windows) {
                        QProcess p;
                        p.start(QStringLiteral("journalctl"), {scope, QStringLiteral("-q"), QStringLiteral("--no-pager"), QStringLiteral("-p"), prio, QStringLiteral("--since=%1").arg(since),
                                                               QStringLiteral("-o"), QStringLiteral("cat"), QStringLiteral("--output-fields=SYSLOG_IDENTIFIER")});
                        p.waitForFinished(120000);
                        for (const QByteArray &l : p.readAllStandardOutput().split('\n')) {
                            if (l.isEmpty()) {
                                continue;
                            }
                            Count &c = data[level][{QString::fromUtf8(l), logName}];
                            (idx == 0 ? c.h : idx == 1 ? c.d : c.w)++;
                        }
                    }
                }
            }
            // security: success / failure
            QProcess p;
            p.start(QStringLiteral("journalctl"), {QStringLiteral("--system"), QStringLiteral("--facility=auth,authpriv"), QStringLiteral("-q"), QStringLiteral("--no-pager"),
                                                   QStringLiteral("--since=-7d"), QStringLiteral("-o"), QStringLiteral("json"),
                                                   QStringLiteral("--output-fields=MESSAGE,SYSLOG_IDENTIFIER,PRIORITY")});
            p.waitForFinished(120000);
            const qint64 now = QDateTime::currentMSecsSinceEpoch() * 1000;
            for (const QByteArray &l : p.readAllStandardOutput().split('\n')) {
                const QJsonObject o = QJsonDocument::fromJson(l).object();
                if (o.isEmpty()) {
                    continue;
                }
                const qint64 t = o.value(QStringLiteral("__REALTIME_TIMESTAMP")).toString().toLongLong();
                const int lv = o.value(QStringLiteral("PRIORITY")).toString().toInt() <= 3 ? AuditFailure : auditLevel(jsonString(o.value(QStringLiteral("MESSAGE"))));
                Count &c = data[lv][{o.value(QStringLiteral("SYSLOG_IDENTIFIER")).toString(), QStringLiteral("Безпека")}];
                c.w++;
                if (now - t < 24LL * 3600 * 1000000) {
                    c.d++;
                }
                if (now - t < 3600LL * 1000000) {
                    c.h++;
                }
            }
            // log summary: last entry of each log
            QList<QStringList> logRows;
            QProcess du;
            du.start(QStringLiteral("journalctl"), {QStringLiteral("--disk-usage")});
            du.waitForFinished(10000);
            static const QRegularExpression sz(QStringLiteral("([0-9.,]+[KMGT]?)B?"));
            const QString used = diskUsage(du.readAllStandardOutput());
            for (const LogDef &d : windowsLogs() + serviceLogs()) {
                QString modified = QStringLiteral("Н/Д");
                if (!d.args.isEmpty()) {
                    QProcess last;
                    last.start(QStringLiteral("journalctl"), QStringList{QStringLiteral("-q"), QStringLiteral("--no-pager"), QStringLiteral("-r"), QStringLiteral("-n"), QStringLiteral("1"),
                                                                         QStringLiteral("-o"), QStringLiteral("short-unix")}
                                                                 + d.args);
                    last.waitForFinished(15000);
                    const double sec = QString::fromUtf8(last.readAllStandardOutput()).section(QLatin1Char(' '), 0, 0).toDouble();
                    if (sec > 0) {
                        modified = QDateTime::fromMSecsSinceEpoch(qint64(sec * 1000)).toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"));
                    }
                } else if (d.dpkg) {
                    modified = QFileInfo(QStringLiteral("/var/log/dpkg.log")).lastModified().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"));
                }
                logRows << QStringList{d.title, d.args.isEmpty() && !d.dpkg ? QStringLiteral("0 байт/20,00 МБ") : used + QStringLiteral("/4,00 ГБ"), modified,
                                       QStringLiteral("Увімкнуто"), QStringLiteral("Перезаписувати події за потреби (спочатку найстаріші)"), d.key};
            }
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self,
                [self, data, logRows] {
                    if (!self) {
                        return;
                    }
                    self->fillSummary(data, logRows);
                },
                Qt::QueuedConnection);
        });
        connect(t, &QThread::finished, t, &QObject::deleteLater);
        t->start(QThread::LowPriority);
    }

    template<typename Data>
    void fillSummary(const Data &data, const QList<QStringList> &logRows)
    {
        m_admin->clear();
        for (int level : {Critical, Error, Warning, Information, AuditSuccess, AuditFailure}) {
            auto *top = new EventItem(m_admin);
            top->setText(0, levelName(level));
            int h = 0, d = 0, w = 0;
            const auto groups = data.value(level);
            for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
                h += it.value().h;
                d += it.value().d;
                w += it.value().w;
                auto *c = new EventItem(top);
                c->setText(1, QString::number(eventCode(it.key().first, level)));
                c->setText(2, it.key().first);
                c->setText(3, it.key().second);
                c->setText(4, QString::number(it.value().h));
                c->setText(5, QString::number(it.value().d));
                c->setText(6, QString::number(it.value().w));
                c->setData(0, Qt::UserRole, level);
                for (int col : {4, 5, 6}) {
                    c->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
                    c->setData(col, Qt::UserRole + 5, c->text(col).toInt());
                }
            }
            for (int col : {1, 2, 3}) {
                top->setText(col, QStringLiteral("-"));
            }
            top->setText(4, QLocale().toString(h));
            top->setText(5, QLocale().toString(d));
            top->setText(6, QLocale().toString(w));
            for (int col : {4, 5, 6}) {
                top->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
            }
            if (w == 0) {
                for (int col = 0; col < 7; ++col) {
                    top->setForeground(col, QColor(0x6d, 0x6d, 0x6d));
                }
                if (level == AuditFailure || level == AuditSuccess) {
                    delete top;
                    continue;
                }
            }
            top->sortChildren(6, Qt::DescendingOrder);
        }
        m_logSummary->clear();
        for (const QStringList &r : logRows) {
            auto *it = new QTreeWidgetItem(m_logSummary, r.mid(0, 5));
            QString path = QStringLiteral("Перегляд подій (Локальний)\\") + (windowsLogsKeys().contains(r.at(5)) ? QStringLiteral("Журнали Windows\\") : QStringLiteral("Журнали програм і служб\\")) + r.at(0);
            it->setData(0, Qt::UserRole, path);
        }
        m_title->setRight(QStringLiteral("Останнє оновлення: %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"))));
    }

    static QStringList windowsLogsKeys()
    {
        QStringList k;
        for (const LogDef &d : windowsLogs()) {
            k << d.key;
        }
        return k;
    }

    void openSummaryItem(QTreeWidgetItem *it)
    {
        if (!it->parent()) {
            it->setExpanded(!it->isExpanded());
            return;
        }
        const QString log = it->text(3);
        const QString source = it->text(2);
        const int level = it->data(0, Qt::UserRole).toInt();
        // like Windows 7: a view with the events of this source and level
        for (const LogDef &d : windowsLogs()) {
            if (d.title == log) {
                Node n;
                n.kind = Node::CustomView;
                n.name = QStringLiteral("Події зведення");
                n.logs = {d};
                n.filter.sources = {source};
                if (level != AuditSuccess && level != AuditFailure) {
                    n.filter.levels = {level};
                }
                Node &slot = summaryNode();
                slot = n;
                showLog(m_summaryItem, slot);
                refreshActions();
            }
        }
    }

    Node &summaryNode()
    {
        if (!m_summaryItem) {
            Node n;
            n.kind = Node::CustomView;
            m_summaryItem = addNode(m_evRoot->child(0), QStringLiteral("Події зведення"), QStringLiteral("seven-ev-filter"), n);
            m_summaryItem->setHidden(true);
        }
        return m_nodes[m_summaryItem->data(0, Qt::UserRole).toInt()];
    }

    void remember(QTreeWidgetItem *it)
    {
        KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("seven-eventvwrrc")), QStringLiteral("Recent"));
        QStringList paths = g.readEntry("Paths", QStringList());
        const QString path = pathOf(it);
        paths.removeAll(path);
        paths.prepend(path);
        g.writeEntry("Paths", paths.mid(0, 12));
        g.writeEntry(path, QDateTime::currentDateTime().toString(Qt::ISODate));
        if (!g.hasKey(QStringLiteral("Created ") + path)) {
            g.writeEntry(QStringLiteral("Created ") + path, QDateTime::currentDateTime().toString(Qt::ISODate));
        }
        g.sync();
    }

    void fillRecent()
    {
        m_recent->clear();
        KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("seven-eventvwrrc")), QStringLiteral("Recent"));
        for (const QString &path : g.readEntry("Paths", QStringList())) {
            const QString rel = path.section(QLatin1Char('\\'), 1);
            auto *it = new QTreeWidgetItem(m_recent, {rel, rel.startsWith(QLatin1String("Настроювані")) ? QStringLiteral("Події з усіх журналів") : QStringLiteral("Н/Д"),
                                                      QDateTime::fromString(g.readEntry(path, QString()), Qt::ISODate).toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")),
                                                      QDateTime::fromString(g.readEntry(QStringLiteral("Created ") + path, QString()), Qt::ISODate).toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"))});
            it->setData(0, Qt::UserRole, path);
        }
    }

    /* ----- folders ----- */
    void showFolder(QTreeWidgetItem *it)
    {
        m_middle->setCurrentIndex(2);
        m_folderHeader->setText(it->text(0), QStringLiteral("Кількість журналів: %1").arg(it->childCount()));
        m_folderList->clear();
        for (int i = 0; i < it->childCount(); ++i) {
            QTreeWidgetItem *c = it->child(i);
            if (c->isHidden()) {
                continue;
            }
            const Node &n = m_nodes[c->data(0, Qt::UserRole).toInt()];
            auto *row = new QTreeWidgetItem(m_folderList, {c->text(0), n.kind == Node::Log ? QStringLiteral("Журнал адміністрування") : n.kind == Node::Folder ? QStringLiteral("Папка") : QStringLiteral("Настроюване подання"), QString(), QString()});
            row->setIcon(0, c->icon(0));
            row->setData(0, Qt::UserRole, pathOf(c));
        }
    }

    /* ----- logs ----- */
    LogDef defFor(const Event &e) const
    {
        for (const LogDef &d : windowsLogs() + serviceLogs() + m_saved) {
            if (d.title == e.log) {
                return d;
            }
        }
        return {};
    }

    void showLog(QTreeWidgetItem *it, Node &n)
    {
        m_middle->setCurrentIndex(1);
        m_current = it;
        const QString title = n.kind == Node::Log || n.kind == Node::SavedLog ? n.def.title : n.name;
        m_logHeader->setText(title, QStringLiteral("Читання даних. Зачекайте, будь ласка..."),
                             n.kind == Node::Log || n.kind == Node::SavedLog ? QStringLiteral("seven-ev-lognode") : QStringLiteral("seven-ev-filter"));
        m_list->clear();
        m_events.clear();
        m_shown.clear();
        m_preview->setVisible(m_previewOn);
        const int ticket = ++m_ticket;
        QList<LogDef> defs = n.kind == Node::Log || n.kind == Node::SavedLog ? QList<LogDef>{n.def} : n.logs;
        const bool adminOnly = n.kind == Node::AdminView;
        QPointer<EventViewer> self(this);
        QThread *t = QThread::create([self, defs, ticket, adminOnly] {
            QList<Event> all;
            for (LogDef d : defs) {
                if (adminOnly && !d.args.isEmpty()) {
                    d.args << QStringLiteral("-p") << QStringLiteral("0..4");
                }
                all += readLog(d);
            }
            std::sort(all.begin(), all.end(), [](const Event &a, const Event &b) {
                return a.usec > b.usec;
            });
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self,
                [self, all, ticket] {
                    if (self && self->m_ticket == ticket) {
                        self->m_events = all;
                        self->applyFilter();
                    }
                },
                Qt::QueuedConnection);
        });
        connect(t, &QThread::finished, t, &QObject::deleteLater);
        t->start();
    }

    void applyFilter()
    {
        Node *n = currentNode();
        if (m_current == m_summaryItem) {
            n = &summaryNode();
        }
        if (!n) {
            return;
        }
        Filter f = n->filter;
        if (n->kind == Node::Log || n->kind == Node::SavedLog) {
            f = m_logFilters.value(n->def.key + n->def.file);
        }
        m_shown.clear();
        for (const Event &e : std::as_const(m_events)) {
            if (f.matches(e)) {
                m_shown.append(e);
            }
        }
        m_list->setSortingEnabled(false);
        m_list->clear();
        QList<QTreeWidgetItem *> items;
        items.reserve(m_shown.size());
        for (int i = 0; i < m_shown.size(); ++i) {
            const Event &e = m_shown.at(i);
            auto *it = new EventItem;
            it->setText(0, levelName(e.level));
            it->setIcon(0, QIcon::fromTheme(levelIcon(e.level)));
            it->setText(1, e.when().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")));
            it->setData(1, Qt::UserRole + 5, e.usec);
            it->setText(2, e.source);
            it->setText(3, QString::number(e.code));
            it->setData(3, Qt::UserRole + 5, e.code);
            it->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
            it->setText(4, QStringLiteral("Відсутня"));
            it->setData(0, Qt::UserRole, i);
            items.append(it);
        }
        m_list->addTopLevelItems(items);
        m_list->setSortingEnabled(true);
        m_list->sortByColumn(1, Qt::DescendingOrder);
        const QString count = QStringLiteral("Кількість подій: %1").arg(QLocale().toString(m_events.size()));
        const bool logNode = n->kind == Node::Log || n->kind == Node::SavedLog;
        const QString title = logNode ? n->def.title : n->name;
        if (f.active() && logNode) {
            m_filterBar->setText(QStringLiteral("<img src=\"%1\" width=16 height=16>&nbsp;&nbsp;%2 Кількість подій: %3")
                                     .arg(filterIconPath(), f.describe(n->def.channel).toHtmlEscaped(), QLocale().toString(m_shown.size())));
            m_filterBar->show();
        } else {
            m_filterBar->hide();
        }
        m_logHeader->setText(title, logNode ? count : QStringLiteral("Кількість подій: %1").arg(QLocale().toString(m_shown.size())),
                             logNode ? QStringLiteral("seven-ev-lognode") : QStringLiteral("seven-ev-filter"));
        if (m_list->topLevelItemCount()) {
            m_list->setCurrentItem(m_list->topLevelItem(0));
        } else {
            m_previewTitle->clear();
        }
        refreshActions();
    }

    /** the boxed plus and minus of the summary list */
    static QString boxPath(bool open)
    {
        const QString path = QDir::tempPath() + QStringLiteral("/seven-ev-box-%1-%2.png").arg(open ? QStringLiteral("open") : QStringLiteral("closed")).arg(getuid());
        QImage img(16, 16, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.fillRect(4, 4, 9, 9, Qt::white);
        p.setPen(QColor(0x30, 0x30, 0x30));
        p.drawRect(4, 4, 8, 8);
        p.drawLine(6, 8, 10, 8);
        if (!open) {
            p.drawLine(8, 6, 8, 10);
        }
        p.end();
        img.save(path);
        return path;
    }

    /** the hollow and the filled triangle of the Windows 7 tree views */
    static QString arrowPath(bool open)
    {
        const QString path = QDir::tempPath() + QStringLiteral("/seven-ev-arrow-%1-%2.png").arg(open ? QStringLiteral("open") : QStringLiteral("closed")).arg(getuid());
        QImage img(16, 16, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        if (open) {
            p.setPen(QPen(QColor(0x26, 0x26, 0x26), 1));
            p.setBrush(QColor(0x59, 0x59, 0x59));
            p.drawPolygon(QPolygonF({QPointF(10.5, 5.5), QPointF(10.5, 10.5), QPointF(5.5, 10.5)}));
        } else {
            p.setPen(QPen(QColor(0x94, 0x94, 0x94), 1));
            p.setBrush(Qt::white);
            p.drawPolygon(QPolygonF({QPointF(6.5, 4.5), QPointF(10.5, 8.5), QPointF(6.5, 12.5)}));
        }
        p.end();
        img.save(path);
        return path;
    }

    void connectComputer()
    {
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Вибір комп'ютера"));
        dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        auto *v = new QVBoxLayout(&dlg);
        auto *box = new QGroupBox(QStringLiteral("Виберіть комп'ютер, яким має керувати ця оснастка."));
        auto *g = new QGridLayout(box);
        g->addWidget(new QLabel(QStringLiteral("Ця оснастка завжди керуватиме:")), 0, 0, 1, 3);
        auto *local = new QRadioButton(QStringLiteral("Локальний комп'ютер: (комп'ютер, на якому запущено консоль)"));
        auto *other = new QRadioButton(QStringLiteral("Інший комп'ютер:"));
        local->setChecked(true);
        auto *name = new QLineEdit;
        name->setEnabled(false);
        auto *browse = new QPushButton(QStringLiteral("Огляд..."));
        browse->setEnabled(false);
        g->addWidget(local, 1, 0, 1, 3);
        g->addWidget(other, 2, 0);
        g->addWidget(name, 2, 1);
        g->addWidget(browse, 2, 2);
        auto *note = new QLabel(QStringLiteral("Перегляд журналів іншого комп'ютера через мережу в цій системі не підтримується."));
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color: #6d6d6d;"));
        g->addWidget(note, 3, 0, 1, 3);
        v->addWidget(box);
        connect(other, &QRadioButton::toggled, name, &QWidget::setEnabled);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        for (QPushButton *b : {ok, cancel}) {
            b->setMinimumWidth(80);
            row->addWidget(b);
        }
        v->addLayout(row);
        connect(ok, &QPushButton::clicked, &dlg, [&dlg, other] {
            if (other->isChecked()) {
                QMessageBox::warning(&dlg, QStringLiteral("Перегляд подій"), QStringLiteral("Не вдалося підключитися до іншого комп'ютера: віддалений перегляд журналів не підтримується."));
                return;
            }
            dlg.accept();
        });
        connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
        dlg.exec();
    }

    QString filterIconPath() const
    {
        static QString path;
        if (path.isEmpty()) {
            path = QDir::tempPath() + QStringLiteral("/seven-ev-filter-%1.png").arg(getuid());
            QIcon::fromTheme(QStringLiteral("seven-ev-filter")).pixmap(16, 16).save(path);
        }
        return path;
    }

    QStringList knownSources() const
    {
        QSet<QString> s;
        for (const Event &e : m_events) {
            s.insert(e.source);
        }
        QStringList l(s.begin(), s.end());
        l.sort(Qt::CaseInsensitive);
        return l;
    }

    /* ----- actions ----- */
    QList<ActionItem> commonTail()
    {
        return {
            {QStringLiteral("Вигляд"), QString(), {}, true, true, [this](QMenu *m) { fillViewMenu(m); }},
            {QStringLiteral("Оновити"), QStringLiteral("seven-mmc-refresh"), [this] { refreshNode(); }, true, true},
            {QStringLiteral("Довідка"), QStringLiteral("seven-mmc-help"), {}, true, true,
             [this](QMenu *m) {
                 m->addAction(QIcon::fromTheme(QStringLiteral("seven-mmc-help")), QStringLiteral("Довідка"), this, [this] { showHelp(); });
                 m->addAction(QStringLiteral("Про програму «Перегляд подій»"), this, [this] {
                     QMessageBox::about(this, QStringLiteral("Перегляд подій"), QStringLiteral("Перегляд подій у стилі Windows 7.\nПоказує журнал systemd (journald)."));
                 });
             }},
        };
    }

    QList<ActionItem> eventActions()
    {
        const bool has = m_list->currentItem();
        return {
            {QStringLiteral("Властивості події"), QStringLiteral("seven-mmc-props"), [this] { eventProperties(); }, has},
            {QStringLiteral("Прив'язати завдання до цієї події..."), QStringLiteral("seven-ev-attachtask"), {}, false},
            {QStringLiteral("Копіювати"), QStringLiteral("seven-ev-copy"), {}, has, true,
             [this](QMenu *m) {
                 m->addAction(QStringLiteral("Копіювати відомості як текст"), this, [this] { QApplication::clipboard()->setText(m_view->plainText()); });
             }},
            {QStringLiteral("Зберегти вибрані події..."), QStringLiteral("seven-ev-save"), [this] { saveEvents(true); }, has},
            {QStringLiteral("Оновити"), QStringLiteral("seven-mmc-refresh"), [this] { refreshNode(); }, true, true},
            {QStringLiteral("Довідка"), QStringLiteral("seven-mmc-help"), {}, true, true, [this](QMenu *m) {
                 m->addAction(QIcon::fromTheme(QStringLiteral("seven-mmc-help")), QStringLiteral("Довідка"), this, [this] { showHelp(); });
             }},
        };
    }

    void refreshActions()
    {
        Node *n = currentNode();
        QList<ActionGroup> groups;
        if (!n) {
            return;
        }
        const QString name = m_current == m_summaryItem && m_middle->currentIndex() == 1 ? QStringLiteral("Події зведення") : m_tree->currentItem()->text(0);
        QList<ActionItem> items{
            {QStringLiteral("Відкрити збережений журнал..."), QStringLiteral("seven-mmc-open"), [this] { openSavedLog(); }},
            {QStringLiteral("Створити настроюване подання..."), QStringLiteral("seven-ev-newview"), [this] { createCustomView(); }},
            {QStringLiteral("Імпортувати настроюване подання..."), QString(), [this] { importCustomView(); }},
        };
        const bool onLog = m_middle->currentIndex() == 1;
        if (n->kind == Node::Root || n->kind == Node::Folder) {
            items << ActionItem{QStringLiteral("Підключитися до іншого комп'ютера..."), QString(), [this] { connectComputer(); }, true, true};
        }
        if (onLog && (n->kind == Node::Log || n->kind == Node::SavedLog)) {
            const bool journal = !n->def.args.isEmpty() || !n->def.file.isEmpty();
            const bool filtered = m_logFilters.value(n->def.key + n->def.file).active();
            items << ActionItem{QStringLiteral("Очистити журнал..."), QString(), [this] { clearLog(); }, journal && n->def.file.isEmpty(), true}
                  << ActionItem{QStringLiteral("Фільтр поточного журналу..."), QStringLiteral("seven-ev-filter"), [this] { filterLog(); }}
                  << ActionItem{QStringLiteral("Очистити фільтр"), QString(), [this] {
                                    Node *c = currentNode();
                                    m_logFilters.remove(c->def.key + c->def.file);
                                    applyFilter();
                                },
                                filtered}
                  << ActionItem{QStringLiteral("Властивості"), QStringLiteral("seven-mmc-props"), [this] { logProperties(); }}
                  << ActionItem{QStringLiteral("Знайти..."), QStringLiteral("seven-ev-find"), [this] { find(); }}
                  << ActionItem{QStringLiteral("Зберегти всі події як..."), QStringLiteral("seven-ev-save"), [this] { saveEvents(false); }}
                  << ActionItem{QStringLiteral("Прив'язати завдання до цього журналу..."), QStringLiteral("seven-ev-attachtask"), {}, false}
                  << ActionItem{QStringLiteral("Зберегти фільтр у настроюване подання..."), QStringLiteral("seven-ev-savefilter"), [this] { saveFilterAsView(); }, filtered && n->kind == Node::Log};
        }
        if (onLog && (n->kind == Node::CustomView || n->kind == Node::AdminView)) {
            items << ActionItem{QStringLiteral("Фільтр поточного настроюваного подання..."), QStringLiteral("seven-ev-filter"), [this] { filterView(); }, n->kind == Node::CustomView, true}
                  << ActionItem{QStringLiteral("Властивості"), QStringLiteral("seven-mmc-props"), [this] { filterView(); }, n->kind == Node::CustomView}
                  << ActionItem{QStringLiteral("Знайти..."), QStringLiteral("seven-ev-find"), [this] { find(); }}
                  << ActionItem{QStringLiteral("Зберегти всі події в настроюваному поданні як..."), QStringLiteral("seven-ev-save"), [this] { saveEvents(false); }}
                  << ActionItem{QStringLiteral("Експортувати настроюване подання..."), QString(), [this] { exportCustomView(); }, n->kind == Node::CustomView}
                  << ActionItem{QStringLiteral("Копіювати настроюване подання..."), QString(), {}, false}
                  << ActionItem{QStringLiteral("Видалити"), QStringLiteral("seven-mmc-delete"), [this] { deleteCustomView(); }, n->kind == Node::CustomView, true};
        }
        items += commonTail();
        groups << ActionGroup{name, items};
        if (onLog && m_list->currentItem()) {
            const Event &e = m_shown.at(m_list->currentItem()->data(0, Qt::UserRole).toInt());
            groups << ActionGroup{QStringLiteral("Подія %1, %2").arg(e.code).arg(e.source), eventActions()};
        }
        m_groups = groups;
        m_actions->setGroups(groups);
    }

    void fillViewMenu(QMenu *m)
    {
        QAction *preview = m->addAction(QStringLiteral("Область попереднього перегляду"));
        preview->setCheckable(true);
        preview->setChecked(m_previewOn);
        connect(preview, &QAction::toggled, this, [this](bool on) {
            m_previewOn = on;
            m_preview->setVisible(on);
        });
        QMenu *sort = m->addMenu(QStringLiteral("Сортувати за"));
        const QStringList cols{QStringLiteral("Рівень"), QStringLiteral("Дата й час"), QStringLiteral("Джерело"), QStringLiteral("Код події"), QStringLiteral("Категорія завдання")};
        for (int i = 0; i < cols.size(); ++i) {
            sort->addAction(cols.at(i), this, [this, i] { m_list->sortByColumn(i, i == 1 ? Qt::DescendingOrder : Qt::AscendingOrder); });
        }
        m->addSeparator();
        QMenu *columns = m->addMenu(QStringLiteral("Додати або видалити стовпці"));
        for (int i = 0; i < cols.size(); ++i) {
            QAction *a = columns->addAction(cols.at(i));
            a->setCheckable(true);
            a->setChecked(!m_list->isColumnHidden(i));
            a->setEnabled(i > 0);
            connect(a, &QAction::toggled, this, [this, i](bool on) { m_list->setColumnHidden(i, !on); });
        }
    }

    void refreshNode()
    {
        if (m_middle->currentIndex() == 0) {
            m_summaryLoaded = false;
            showRoot();
            return;
        }
        if (QTreeWidgetItem *it = m_current) {
            Node &n = m_nodes[it->data(0, Qt::UserRole).toInt()];
            showLog(it, n);
        }
    }

    void eventProperties()
    {
        QTreeWidgetItem *it = m_list->currentItem();
        if (!it) {
            return;
        }
        QDialog dlg(this);
        const auto title = [this] {
            const Event &e = m_shown.at(m_list->currentItem()->data(0, Qt::UserRole).toInt());
            return QStringLiteral("Властивості події - Подія %1, %2").arg(e.code).arg(e.source);
        };
        dlg.setWindowTitle(title());
        dlg.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-ev-app")));
        dlg.resize(640, 470);
        auto *v = new QVBoxLayout(&dlg);
        auto *view = new EventView;
        v->addWidget(view, 1);
        auto show = [this, view, &dlg, title] {
            const Event &e = m_shown.at(m_list->currentItem()->data(0, Qt::UserRole).toInt());
            view->show(e, defFor(e));
            dlg.setWindowTitle(title());
        };
        show();
        auto *row = new QHBoxLayout;
        auto *copy = new QPushButton(QIcon::fromTheme(QStringLiteral("seven-ev-copyclip")), QStringLiteral("Копіювати"));
        row->addWidget(copy);
        row->addStretch(1);
        auto *up = new QToolButton;
        up->setIcon(QIcon::fromTheme(QStringLiteral("seven-ev-up")));
        up->setToolTip(QStringLiteral("Попередня подія"));
        auto *down = new QToolButton;
        down->setIcon(QIcon::fromTheme(QStringLiteral("seven-ev-down")));
        down->setToolTip(QStringLiteral("Наступна подія"));
        row->addWidget(up);
        row->addWidget(down);
        row->addSpacing(12);
        auto *close = new QPushButton(QStringLiteral("Закрити"));
        close->setDefault(true);
        close->setMinimumWidth(80);
        row->addWidget(close);
        v->addLayout(row);
        connect(copy, &QPushButton::clicked, &dlg, [view] { QApplication::clipboard()->setText(view->plainText()); });
        connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
        auto step = [this, show](int d) {
            QTreeWidgetItem *cur = m_list->currentItem();
            QTreeWidgetItem *next = d < 0 ? m_list->itemAbove(cur) : m_list->itemBelow(cur);
            if (next) {
                m_list->setCurrentItem(next);
                show();
            }
        };
        connect(up, &QToolButton::clicked, &dlg, [step] { step(-1); });
        connect(down, &QToolButton::clicked, &dlg, [step] { step(+1); });
        dlg.exec();
    }

    void filterLog()
    {
        Node *n = currentNode();
        const QString key = n->def.key + n->def.file;
        FilterDialog dlg(this, QStringLiteral("Фільтр поточного журналу"), m_logFilters.value(key), {n->def.title}, knownSources(), n->def.channel);
        if (dlg.exec() == QDialog::Accepted) {
            m_logFilters.insert(key, dlg.filter());
            applyFilter();
        }
    }

    QStringList allLogTitles() const
    {
        QStringList l;
        for (const LogDef &d : windowsLogs() + serviceLogs()) {
            l << d.title;
        }
        return l;
    }

    void createCustomView()
    {
        Node *n = currentNode();
        QList<LogDef> logs = n && (n->kind == Node::Log) ? QList<LogDef>{n->def} : QList<LogDef>{windowsLogs().at(0), windowsLogs().at(1), windowsLogs().at(3)};
        QStringList titles;
        for (const LogDef &d : logs) {
            titles << d.title;
        }
        FilterDialog dlg(this, QStringLiteral("Створення настроюваного подання"), Filter(), titles, knownSources(), logs.first().channel);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Збереження фільтра як настроюваного подання"), QStringLiteral("Ім'я:"), QLineEdit::Normal,
                                                   QStringLiteral("Нове подання"), &ok)
                                 .trimmed();
        if (!ok || name.isEmpty()) {
            return;
        }
        KConfigGroup views(KSharedConfig::openConfig(QStringLiteral("seven-eventvwrrc")), QStringLiteral("CustomViews"));
        KConfigGroup g = views.group(name);
        dlg.filter().save(g);
        QStringList keys;
        for (const LogDef &d : logs) {
            keys << d.key;
        }
        g.writeEntry("Logs", keys);
        g.sync();
        m_summaryItem = nullptr;
        fillTree();
        selectPath(QStringLiteral("Перегляд подій (Локальний)\\Настроювані подання\\") + name);
    }

    void saveFilterAsView()
    {
        Node *n = currentNode();
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Збереження фільтра як настроюваного подання"), QStringLiteral("Ім'я:"), QLineEdit::Normal,
                                                   n->def.title + QStringLiteral(" (відфільтровано)"), &ok)
                                 .trimmed();
        if (!ok || name.isEmpty()) {
            return;
        }
        KConfigGroup g = KConfigGroup(KSharedConfig::openConfig(QStringLiteral("seven-eventvwrrc")), QStringLiteral("CustomViews")).group(name);
        m_logFilters.value(n->def.key).save(g);
        g.writeEntry("Logs", QStringList{n->def.key});
        g.sync();
        m_summaryItem = nullptr;
        fillTree();
        selectPath(QStringLiteral("Перегляд подій (Локальний)\\Настроювані подання\\") + name);
    }

    void filterView()
    {
        Node *n = currentNode();
        if (!n || n->kind != Node::CustomView || m_current == m_summaryItem) {
            return;
        }
        QStringList titles;
        for (const LogDef &d : n->logs) {
            titles << d.title;
        }
        FilterDialog dlg(this, QStringLiteral("Фільтр поточного настроюваного подання"), n->filter, titles, knownSources(), n->logs.value(0).channel);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        n->filter = dlg.filter();
        KConfigGroup g = KConfigGroup(KSharedConfig::openConfig(QStringLiteral("seven-eventvwrrc")), QStringLiteral("CustomViews")).group(n->name);
        n->filter.save(g);
        g.sync();
        applyFilter();
    }

    void deleteCustomView()
    {
        Node *n = currentNode();
        if (!n || n->kind != Node::CustomView) {
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Перегляд подій"), QStringLiteral("Видалити настроюване подання «%1»?").arg(n->name)) != QMessageBox::Yes) {
            return;
        }
        KConfigGroup views(KSharedConfig::openConfig(QStringLiteral("seven-eventvwrrc")), QStringLiteral("CustomViews"));
        views.deleteGroup(n->name);
        views.sync();
        m_summaryItem = nullptr;
        fillTree();
        m_tree->setCurrentItem(m_evRoot);
    }

    void exportCustomView()
    {
        Node *n = currentNode();
        if (!n || n->kind != Node::CustomView) {
            return;
        }
        const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Експортувати настроюване подання"), QDir::homePath() + QLatin1Char('/') + n->name + QStringLiteral(".xml"),
                                                       QStringLiteral("Файли настроюваних подань (*.xml)"));
        if (f.isEmpty()) {
            return;
        }
        QFile file(f);
        if (file.open(QIODevice::WriteOnly)) {
            QString xml = QStringLiteral("<ViewerConfig>\n  <QueryConfig>\n    <QueryParams>\n      <UserQuery />\n    </QueryParams>\n    <QueryNode>\n      <Name>%1</Name>\n      <QueryList>\n")
                              .arg(xmlEscape(n->name));
            xml += n->filter.xml(n->logs.value(0).channel).section(QStringLiteral("<QueryList>\n"), 1).section(QStringLiteral("</QueryList>"), 0, 0);
            xml += QStringLiteral("      </QueryList>\n    </QueryNode>\n  </QueryConfig>\n  <Win7Filter hours=\"%1\" levels=\"%2\" sources=\"%3\" codes=\"%4\" />\n</ViewerConfig>\n")
                       .arg(n->filter.hours)
                       .arg([&] {
                           QStringList l;
                           for (int x : n->filter.levels) {
                               l << QString::number(x);
                           }
                           return l.join(QLatin1Char(','));
                       }())
                       .arg(xmlEscape(n->filter.sources.join(QLatin1Char(','))), xmlEscape(n->filter.codes));
            file.write(xml.toUtf8());
        }
    }

    void importCustomView()
    {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Імпортувати настроюване подання"), QDir::homePath(), QStringLiteral("Файли настроюваних подань (*.xml)"));
        if (f.isEmpty()) {
            return;
        }
        QFile file(f);
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        const QString xml = QString::fromUtf8(file.readAll());
        static const QRegularExpression nameRe(QStringLiteral("<Name>([^<]*)</Name>"));
        static const QRegularExpression w7(QStringLiteral("<Win7Filter hours=\"(\\d*)\" levels=\"([^\"]*)\" sources=\"([^\"]*)\" codes=\"([^\"]*)\""));
        Filter flt;
        const auto m = w7.match(xml);
        if (m.hasMatch()) {
            flt.hours = m.captured(1).toInt();
            for (const QString &l : m.captured(2).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                flt.levels.insert(l.toInt());
            }
            flt.sources = m.captured(3).split(QLatin1Char(','), Qt::SkipEmptyParts);
            flt.codes = m.captured(4);
        } else {
            static const QRegularExpression level(QStringLiteral("Level=(\\d)"));
            for (auto it = level.globalMatch(xml); it.hasNext();) {
                flt.levels.insert(it.next().captured(1).toInt());
            }
        }
        const QString name = nameRe.match(xml).captured(1).isEmpty() ? QFileInfo(f).completeBaseName() : nameRe.match(xml).captured(1);
        KConfigGroup g = KConfigGroup(KSharedConfig::openConfig(QStringLiteral("seven-eventvwrrc")), QStringLiteral("CustomViews")).group(name);
        flt.save(g);
        g.writeEntry("Logs", QStringList{QStringLiteral("Application"), QStringLiteral("Security"), QStringLiteral("System")});
        g.sync();
        m_summaryItem = nullptr;
        fillTree();
    }

    void openSavedLog()
    {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Відкрити збережений журнал"), QStringLiteral("/var/log/journal"),
                                                       QStringLiteral("Файли журналів (*.journal *.journal~);;Усі файли (*)"));
        if (f.isEmpty()) {
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Відкрити збережений журнал"), QStringLiteral("Ім'я:"), QLineEdit::Normal,
                                                   QStringLiteral("Збережено: %1").arg(QFileInfo(f).completeBaseName()), &ok);
        if (!ok) {
            return;
        }
        LogDef d;
        d.key = QStringLiteral("Saved");
        d.title = name;
        d.channel = name;
        d.file = f;
        m_saved.append(d);
        m_summaryItem = nullptr;
        fillTree();
        selectPath(QStringLiteral("Перегляд подій (Локальний)\\Збережені журнали\\") + name);
    }

    void clearLog()
    {
        Node *n = currentNode();
        QMessageBox box(QMessageBox::Question, QStringLiteral("Перегляд подій"),
                        QStringLiteral("Зберегти вміст журналу «%1» перед очищенням?\n\nУ цій системі всі журнали зберігаються в спільному журналі systemd, тому під "
                                       "час очищення буде видалено архівні записи всіх журналів.")
                            .arg(n->def.title),
                        QMessageBox::NoButton, this);
        QPushButton *saveClear = box.addButton(QStringLiteral("Зберегти й очистити"), QMessageBox::AcceptRole);
        QPushButton *clear = box.addButton(QStringLiteral("Очистити"), QMessageBox::DestructiveRole);
        box.addButton(QStringLiteral("Скасувати"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == saveClear) {
            if (!saveEvents(false)) {
                return;
            }
        } else if (box.clickedButton() != clear) {
            return;
        }
        const int code = QProcess::execute(QStringLiteral("pkexec"), {QStringLiteral("/bin/sh"), QStringLiteral("-c"), QStringLiteral("journalctl --rotate && journalctl --vacuum-time=1s")});
        if (code == 0) {
            refreshNode();
        } else if (code != 126 && code != 127) {
            QMessageBox::warning(this, QStringLiteral("Перегляд подій"), QStringLiteral("Не вдалося очистити журнал."));
        }
    }

    void logProperties()
    {
        Node *n = currentNode();
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Властивості журналу - %1 (Тип: Адміністрування)").arg(n->def.title));
        dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        dlg.setFixedSize(480, 430);
        auto *v = new QVBoxLayout(&dlg);
        auto *tabs = new QTabWidget;
        auto *page = new QWidget;
        auto *g = new QGridLayout(page);
        g->setVerticalSpacing(8);
        QProcess du;
        du.start(QStringLiteral("journalctl"), {QStringLiteral("--disk-usage")});
        du.waitForFinished(10000);
        const QString used = diskUsage(du.readAllStandardOutput());
        const QString path = n->def.dpkg ? QStringLiteral("/var/log/dpkg.log") : !n->def.file.isEmpty() ? n->def.file : QStringLiteral("/var/log/journal/");
        const QList<QPair<QString, QString>> rows{{QStringLiteral("Повне ім'я:"), n->def.channel},
                                                  {QStringLiteral("Шлях до журналу:"), path},
                                                  {QStringLiteral("Розмір журналу:"), n->def.dpkg ? sizeText(QFileInfo(path).size()) : used},
                                                  {QStringLiteral("Змінено:"), QFileInfo(path).lastModified().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"))},
                                                  {QStringLiteral("Доступ:"), QFileInfo(path).lastRead().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"))}};
        int r = 0;
        for (const auto &[k, val] : rows) {
            g->addWidget(new QLabel(k), r, 0);
            auto *l = new QLabel(val);
            l->setTextInteractionFlags(Qt::TextSelectableByMouse);
            g->addWidget(l, r++, 1);
        }
        auto *enabled = new QCheckBox(QStringLiteral("Увімкнути ведення журналу"));
        enabled->setChecked(true);
        enabled->setEnabled(false);
        g->addWidget(enabled, r++, 0, 1, 2);
        g->addWidget(new QLabel(QStringLiteral("Максимальний розмір журналу (КБ):")), r, 0);
        auto *max = new QLineEdit(QStringLiteral("4194304"));
        max->setEnabled(false);
        g->addWidget(max, r++, 1);
        g->addWidget(new QLabel(QStringLiteral("Після досягнення максимального розміру журналу подій:")), r++, 0, 1, 2);
        auto *over = new QRadioButton(QStringLiteral("Перезаписувати події за потреби (спочатку найстаріші)"));
        over->setChecked(true);
        auto *archive = new QRadioButton(QStringLiteral("Архівувати журнал після заповнення, не перезаписувати події"));
        auto *never = new QRadioButton(QStringLiteral("Не перезаписувати події (очистити журнал вручну)"));
        for (QRadioButton *b : {over, archive, never}) {
            b->setEnabled(b == over);
            g->addWidget(b, r++, 0, 1, 2);
        }
        g->setRowStretch(r, 1);
        g->setColumnStretch(1, 1);
        tabs->addTab(page, QStringLiteral("Загальні"));
        v->addWidget(tabs, 1);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        for (QPushButton *b : {ok, cancel}) {
            b->setMinimumWidth(80);
            row->addWidget(b);
        }
        v->addLayout(row);
        connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
        dlg.exec();
    }

    void find()
    {
        bool ok = false;
        const QString what = QInputDialog::getText(this, QStringLiteral("Знайти"), QStringLiteral("Знайти:"), QLineEdit::Normal, m_lastFind, &ok);
        if (!ok || what.isEmpty()) {
            return;
        }
        m_lastFind = what;
        QTreeWidgetItem *start = m_list->currentItem();
        QTreeWidgetItem *it = start ? m_list->itemBelow(start) : m_list->topLevelItem(0);
        for (; it; it = m_list->itemBelow(it)) {
            const Event &e = m_shown.at(it->data(0, Qt::UserRole).toInt());
            if (e.message.contains(what, Qt::CaseInsensitive) || e.source.contains(what, Qt::CaseInsensitive) || QString::number(e.code) == what) {
                m_list->setCurrentItem(it);
                m_list->scrollToItem(it);
                return;
            }
        }
        QMessageBox::information(this, QStringLiteral("Перегляд подій"), QStringLiteral("Більше збігів для «%1» не знайдено.").arg(what));
    }

    bool saveEvents(bool selectedOnly)
    {
        QString filter;
        const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Зберегти як"), QDir::homePath() + QStringLiteral("/Події.txt"),
                                                       QStringLiteral("Текст (розділювач — табуляція) (*.txt);;CSV (розділювач — кома) (*.csv);;XML (*.xml)"), &filter);
        if (f.isEmpty()) {
            return false;
        }
        QList<Event> list;
        if (selectedOnly) {
            for (QTreeWidgetItem *it : m_list->selectedItems()) {
                list << m_shown.at(it->data(0, Qt::UserRole).toInt());
            }
        } else {
            for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
                list << m_shown.at(m_list->topLevelItem(i)->data(0, Qt::UserRole).toInt());
            }
        }
        QFile file(f);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, QStringLiteral("Перегляд подій"), QStringLiteral("Не вдалося зберегти файл."));
            return false;
        }
        QTextStream out(&file);
        if (f.endsWith(QLatin1String(".xml"))) {
            out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Events>\n";
            for (const Event &e : std::as_const(list)) {
                out << eventXml(e, {}) << "\n";
            }
            out << "</Events>\n";
        } else {
            const QString sep = f.endsWith(QLatin1String(".csv")) ? QStringLiteral(",") : QStringLiteral("\t");
            auto q = [&sep](QString s) {
                s.replace(QLatin1Char('\n'), QLatin1Char(' '));
                return sep == QLatin1String(",") ? QLatin1Char('"') + s.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"') : s;
            };
            out << QStringList{QStringLiteral("Рівень"), QStringLiteral("Дата й час"), QStringLiteral("Джерело"), QStringLiteral("Код події"), QStringLiteral("Категорія завдання"),
                               QStringLiteral("Опис")}
                       .join(sep)
                << "\n";
            for (const Event &e : std::as_const(list)) {
                out << QStringList{levelName(e.level), e.when().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")), q(e.source), QString::number(e.code), QStringLiteral("Відсутня"), q(e.message)}.join(sep)
                    << "\n";
            }
        }
        return true;
    }

    void showHelp()
    {
        QMessageBox::information(this, QStringLiteral("Перегляд подій"),
                                 QStringLiteral("«Перегляд подій» показує повідомлення системи та програм.\n\n"
                                                "• Журнали Windows: «Програма» — події ваших програм, «Безпека» — входи в систему та підтвердження паролем, "
                                                "«Інсталяція» — встановлення й видалення пакетів, «Система» — події системних служб і ядра.\n"
                                                "• Двічі клацніть подію, щоб відкрити її властивості.\n"
                                                "• «Фільтр поточного журналу» залишає лише потрібні рівні, джерела або коди подій.\n\n"
                                                "Показуються останні %1 подій кожного журналу.")
                                     .arg(kMaxEvents));
    }

    QTreeWidget *m_tree;
    QStackedWidget *m_middle;
    QToolBar *m_bar = nullptr;
    Host m_host;
    QTreeWidgetItem *m_hostParent = nullptr;
    int m_hostIndex = 0;
    QTreeWidgetItem *m_evRoot = nullptr;
    ActionPane *m_actions;
    QSplitter *m_split;
    QAction *m_back;
    QAction *m_forward;
    QMenu *m_actionMenu;
    QMenu *m_viewMenu;
    TitleBand *m_title;
    QTreeWidget *m_admin;
    QTreeWidget *m_recent;
    QTreeWidget *m_logSummary;
    DarkHeader *m_logHeader;
    QLabel *m_filterBar;
    QTreeWidget *m_list;
    QWidget *m_preview;
    QLabel *m_previewTitle;
    EventView *m_view;
    DarkHeader *m_folderHeader;
    QTreeWidget *m_folderList;
    QList<Node> m_nodes;
    QList<ActionGroup> m_groups;
    QList<Event> m_events;
    QList<Event> m_shown;
    QHash<QString, Filter> m_logFilters;
    QList<LogDef> m_saved;
    QList<QTreeWidgetItem *> m_history;
    QList<QTreeWidgetItem *> m_future;
    QTreeWidgetItem *m_current = nullptr;
    QTreeWidgetItem *m_summaryItem = nullptr;
    QString m_lastFind;
    bool m_navigating = false;
    bool m_summaryLoaded = false;
    bool m_previewOn = true;
    int m_ticket = 0;
};
} // namespace Win7Event
