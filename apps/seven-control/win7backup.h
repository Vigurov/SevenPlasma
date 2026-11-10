/*
 * "Backup and Restore" and "Recovery" of the Windows 7 Control Panel.
 *
 *   file backup     -> rsync into <drive>/<computer>/Backup Set <date>/ (unchanged files are hard links to the
 *                      previous set, so every set is complete but only new files take space)
 *   schedule        -> a systemd user timer (seven-backup.timer) that runs "seven-control --backup-run"
 *   network place   -> an SMB share mounted with gio (gvfs)
 *   system image    -> a Timeshift snapshot of the system on another drive (Linux file system needed)
 *   System Restore  -> Timeshift snapshots on the system drive ("restore points")
 *   re-imaging      -> Timeshift restore from an image on another drive, then the computer restarts
 *
 * The internal Windows drive is never offered and never touched.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7chrome.h"

#include <AeroQt/navbuttons.h>
#include <AeroQt/wizard.h>

#include <KConfigGroup>
#include <KSharedConfig>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimeZone>
#include <QTimer>
#include <QTreeWidget>
#include <QWizard>
#include <QWizardPage>

#include <csignal>
#include <sys/stat.h>
#include <unistd.h>

namespace Win7Backup
{
enum {
    BackupView = 44,
    RecoveryView = 45,
    AdvancedView = 46,
    UserFilesView = 47,
    RestartView = 48,
};

/** "Back up your files?" was answered with Back up (the restart page warns otherwise) */
inline bool g_backupDone = false;

struct Nav {
    std::function<void(int)> open;
    std::function<void()> home;
    std::function<void()> back;
    std::function<void(int)> openOther; // a view of another module (Programs and Features)
};

/* ---------------- small helpers ---------------- */

inline QString host()
{
    return QSysInfo::machineHostName();
}

inline QString userName()
{
    return qEnvironmentVariable("USER");
}

/** the full name of the user (as on the Start menu), else the login name */
inline QString displayName()
{
    QFile f(QStringLiteral("/etc/passwd"));
    if (f.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : f.readAll().split('\n')) {
            const QList<QByteArray> c = line.split(':');
            if (c.size() > 4 && QString::fromUtf8(c.at(0)) == userName()) {
                const QString full = QString::fromUtf8(c.at(4)).section(QLatin1Char(','), 0, 0).trimmed();
                if (!full.isEmpty()) {
                    return full;
                }
            }
        }
    }
    return userName();
}

/** the triangles of the Windows 7 tree views (hollow when closed, filled when open) */
inline QString treeArrows()
{
    auto make = [](bool open) {
        const QString path = QDir::tempPath() + QStringLiteral("/seven-bk-arrow-%1-%2.png").arg(open ? QStringLiteral("open") : QStringLiteral("closed")).arg(getuid());
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
    };
    return QStringLiteral("QTreeView::branch { border-image: none; image: none; }"
                          "QTreeView::branch:has-children:closed { image: url(%1); }"
                          "QTreeView::branch:has-children:open { image: url(%2); }")
        .arg(make(false), make(true));
}

inline QString gb(qint64 bytes)
{
    const double v = bytes / 1024.0 / 1024.0 / 1024.0;
    if (v < 1) {
        return QLocale(QLocale::Ukrainian).toString(bytes / 1024.0 / 1024.0, 'f', 1) + QStringLiteral(" МБ");
    }
    return QLocale(QLocale::Ukrainian).toString(v, 'f', 2) + QStringLiteral(" ГБ");
}

inline QString dateText(const QDateTime &d)
{
    return d.toString(QStringLiteral("dd.MM.yyyy H:mm"));
}

inline QLabel *text(const QString &t)
{
    auto *l = new QLabel(t);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    l->setOpenExternalLinks(false);
    return l;
}

inline QLabel *heading(const QString &t)
{
    auto *l = new QLabel(t);
    QFont f = l->font();
    f.setPixelSize(16);
    l->setFont(f);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: #1e3287;"));
    return l;
}

inline QLabel *iconLabel(const QString &icon, int size)
{
    auto *l = new QLabel;
    l->setPixmap(QIcon::fromTheme(icon).pixmap(size, size));
    l->setFixedSize(size, size);
    return l;
}

/** a link with the UAC shield in front of it */
inline QWidget *shieldLink(const QString &t, std::function<void()> open, bool enabled = true)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(2);
    auto *sh = iconLabel(QStringLiteral("seven-cp-shield"), 16);
    h->addWidget(sh, 0, Qt::AlignTop);
    auto *l = new Link(t, std::move(open));
    l->setWordWrap(false);
    l->setEnabled(enabled);
    if (!enabled) {
        l->setStyleSheet(QStringLiteral("color: #8d8d8d;"));
        sh->setEnabled(false);
    }
    h->addWidget(l);
    h->addStretch(1);
    return w;
}

inline QPushButton *shieldButton(const QString &t)
{
    auto *b = new QPushButton(QIcon::fromTheme(QStringLiteral("seven-cp-shield")), t);
    b->setMinimumHeight(24);
    return b;
}

/** a link-styled QLabel for wizards: blue, underlined, opens a short explanation */
inline QLabel *helpLink(QWidget *owner, const QString &t, const QString &title, const QString &help)
{
    auto *l = new QLabel(QStringLiteral("<a href=\"#\" style=\"color:#0066cc\">%1</a>").arg(t.toHtmlEscaped()));
    l->setWordWrap(true);
    QObject::connect(l, &QLabel::linkActivated, owner, [owner, title, help] {
        QMessageBox::information(owner, title, help);
    });
    return l;
}

/** runs commands as root through pkexec, waits with a running event loop; false when cancelled or failed */
inline bool runAsRoot(QWidget *parent, const QString &commands, QString *output = nullptr)
{
    QProcess p;
    QEventLoop loop;
    QObject::connect(&p, &QProcess::finished, &loop, &QEventLoop::quit);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral("pkexec"), {QStringLiteral("sh"), QStringLiteral("-c"), commands});
    if (!p.waitForStarted(3000)) {
        return false;
    }
    loop.exec();
    if (output) {
        *output = QString::fromUtf8(p.readAll());
    }
    if (p.exitCode() != 0 && p.exitCode() != 126 && p.exitCode() != 127 && parent) {
        QMessageBox::warning(parent, QStringLiteral("Архівація і відновлення"), QStringLiteral("Не вдалося виконати операцію.\n\n%1").arg(output ? output->right(600) : QString()));
    }
    return p.exitCode() == 0;
}

/* ---------------- drives ---------------- */

struct Volume {
    QString dev;   // /dev/sdb1
    QString uuid;
    QString label; // "Новий том (sdb1)"
    QString fstype;
    QString mount; // empty when not mounted
    qint64 size = 0;
    qint64 avail = -1;
    bool removable = false;
    bool sameDiskAsSystem = false;
    bool linuxFs() const
    {
        static const QStringList ok{QStringLiteral("ext4"), QStringLiteral("ext3"), QStringLiteral("btrfs"), QStringLiteral("xfs")};
        return ok.contains(fstype);
    }
    QString icon() const
    {
        return QStringLiteral("seven-bk-drive");
    }
};

/** volumes a backup may be saved on: not the system volume, not the boot volume, never the Windows drive */
inline QList<Volume> volumes()
{
    QList<Volume> out;
    QProcess p;
    p.start(QStringLiteral("lsblk"), {QStringLiteral("-J"), QStringLiteral("-b"), QStringLiteral("-o"),
                                      QStringLiteral("NAME,PATH,TYPE,FSTYPE,LABEL,UUID,SIZE,FSAVAIL,MOUNTPOINTS,PARTTYPENAME,RM,HOTPLUG,MODEL")});
    p.waitForFinished(5000);
    const QJsonArray disks = QJsonDocument::fromJson(p.readAllStandardOutput()).object().value(QStringLiteral("blockdevices")).toArray();
    static const QStringList system{QStringLiteral("/"), QStringLiteral("/boot"), QStringLiteral("/boot/efi"), QStringLiteral("/usr"), QStringLiteral("/var"),
                                    QStringLiteral("[SWAP]")};
    static const QStringList usable{QStringLiteral("ext4"), QStringLiteral("ext3"), QStringLiteral("btrfs"), QStringLiteral("xfs"), QStringLiteral("ntfs"),
                                    QStringLiteral("ntfs3"), QStringLiteral("exfat"), QStringLiteral("vfat")};
    for (const QJsonValue &dv : disks) {
        const QJsonObject d = dv.toObject();
        // a test drive (a loop device over a file) can be offered with WIN7_BACKUP_TEST=/dev/loopN
        const bool test = !qEnvironmentVariableIsEmpty("WIN7_BACKUP_TEST") && d.value(QStringLiteral("path")).toString() == qEnvironmentVariable("WIN7_BACKUP_TEST");
        if (d.value(QStringLiteral("type")).toString() != QLatin1String("disk") && !test) {
            continue;
        }
        QJsonArray parts = d.value(QStringLiteral("children")).toArray();
        if (parts.isEmpty()) {
            parts.append(d); // a file system on the whole disk
        }
        bool windows = false;
        bool hasSystem = false;
        for (const QJsonValue &pv : std::as_const(parts)) {
            const QJsonObject o = pv.toObject();
            const QString pt = o.value(QStringLiteral("parttypename")).toString();
            const QString label = o.value(QStringLiteral("label")).toString();
            windows = windows || pt == QLatin1String("Microsoft reserved") || pt.contains(QLatin1String("Windows recovery"))
                || o.value(QStringLiteral("fstype")).toString() == QLatin1String("BitLocker") || label == QLatin1String("System Reserved");
            for (const QJsonValue &m : o.value(QStringLiteral("mountpoints")).toArray()) {
                hasSystem = hasSystem || m.toString() == QLatin1String("/");
            }
        }
        if (windows) {
            continue; // Linux and Windows never meet
        }
        const bool rm = d.value(QStringLiteral("rm")).toBool() || d.value(QStringLiteral("hotplug")).toBool();
        for (const QJsonValue &pv : std::as_const(parts)) {
            const QJsonObject o = pv.toObject();
            Volume v;
            v.fstype = o.value(QStringLiteral("fstype")).toString();
            if (!usable.contains(v.fstype)) {
                continue;
            }
            bool sys = false;
            for (const QJsonValue &m : o.value(QStringLiteral("mountpoints")).toArray()) {
                const QString mp = m.toString();
                sys = sys || system.contains(mp);
                if (!mp.isEmpty() && v.mount.isEmpty()) {
                    v.mount = mp;
                }
            }
            if (sys) {
                continue;
            }
            v.dev = o.value(QStringLiteral("path")).toString();
            v.uuid = o.value(QStringLiteral("uuid")).toString();
            v.size = o.value(QStringLiteral("size")).toVariant().toLongLong();
            if (v.size < 1024LL * 1024 * 1024) {
                continue; // too small for a backup, like Windows 7 hides them
            }
            v.avail = o.value(QStringLiteral("fsavail")).isNull() ? -1 : o.value(QStringLiteral("fsavail")).toVariant().toLongLong();
            v.removable = rm;
            v.sameDiskAsSystem = hasSystem;
            const QString name = o.value(QStringLiteral("label")).toString();
            const QString kname = v.dev.section(QLatin1Char('/'), -1);
            v.label = QStringLiteral("%1 (%2)").arg(name.isEmpty() ? (rm ? QStringLiteral("Знімний диск") : QStringLiteral("Новий том")) : name, kname);
            out.append(v);
        }
    }
    return out;
}

/** mounts a volume through UDisks (no password for removable drives of the session user) */
inline QString mountVolume(const QString &uuid)
{
    for (const Volume &v : volumes()) {
        if (v.uuid != uuid) {
            continue;
        }
        if (!v.mount.isEmpty()) {
            return v.mount;
        }
        QProcess p;
        p.start(QStringLiteral("udisksctl"), {QStringLiteral("mount"), QStringLiteral("--no-user-interaction"), QStringLiteral("-b"), v.dev});
        p.waitForFinished(30000);
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        static const QRegularExpression at(QStringLiteral(" at (.+?)\\.?\\s*$"));
        const auto m = at.match(out.trimmed());
        if (m.hasMatch()) {
            return m.captured(1);
        }
        for (const Volume &again : volumes()) {
            if (again.uuid == uuid && !again.mount.isEmpty()) {
                return again.mount;
            }
        }
    }
    return {};
}

inline QString rootDevice()
{
    QProcess p;
    p.start(QStringLiteral("findmnt"), {QStringLiteral("-n"), QStringLiteral("-o"), QStringLiteral("SOURCE"), QStringLiteral("/")});
    p.waitForFinished(3000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

/* ---------------- settings ---------------- */

struct Settings {
    bool configured = false;
    bool network = false;
    QString uuid;      // destination volume
    QString label;     // "Новий том (sdb1)" or \\server\share
    QString netUrl;    // smb://server/share/folder
    QString netUser;
    QString netPassword;
    bool automatic = true; // "Let Windows choose"
    QStringList items;     // "Let me choose": absolute paths
    bool image = false;    // include a system image
    bool scheduled = true;
    int frequency = 1; // 0 daily, 1 weekly, 2 monthly
    int day = 7;       // 1..7 (Monday..Sunday) or 1..28
    int hour = 19;
    qint64 lastBackup = 0;
    bool lastFailed = false;
    QString lastError;
    qint64 setSize = -1;
};

inline KSharedConfig::Ptr config()
{
    return KSharedConfig::openConfig(QStringLiteral("seven-backuprc"), KConfig::SimpleConfig);
}

inline Settings load()
{
    config()->reparseConfiguration();
    const KConfigGroup g(config(), QStringLiteral("Backup"));
    Settings s;
    s.configured = g.readEntry("Configured", false);
    s.network = g.readEntry("Network", false);
    s.uuid = g.readEntry("Uuid", QString());
    s.label = g.readEntry("Label", QString());
    s.netUrl = g.readEntry("NetUrl", QString());
    s.netUser = g.readEntry("NetUser", QString());
    s.netPassword = QString::fromUtf8(QByteArray::fromBase64(g.readEntry("NetPassword", QByteArray())));
    s.automatic = g.readEntry("Automatic", true);
    s.items = g.readEntry("Items", QStringList());
    s.image = g.readEntry("Image", false);
    s.scheduled = g.readEntry("Scheduled", true);
    s.frequency = g.readEntry("Frequency", 1);
    s.day = g.readEntry("Day", 7);
    s.hour = g.readEntry("Hour", 19);
    s.lastBackup = g.readEntry("LastBackup", qint64(0));
    s.lastFailed = g.readEntry("LastFailed", false);
    s.lastError = g.readEntry("LastError", QString());
    s.setSize = g.readEntry("SetSize", qint64(-1));
    return s;
}

inline void save(const Settings &s)
{
    KConfigGroup g(config(), QStringLiteral("Backup"));
    g.writeEntry("Configured", s.configured);
    g.writeEntry("Network", s.network);
    g.writeEntry("Uuid", s.uuid);
    g.writeEntry("Label", s.label);
    g.writeEntry("NetUrl", s.netUrl);
    g.writeEntry("NetUser", s.netUser);
    g.writeEntry("NetPassword", s.netPassword.toUtf8().toBase64());
    g.writeEntry("Automatic", s.automatic);
    g.writeEntry("Items", s.items);
    g.writeEntry("Image", s.image);
    g.writeEntry("Scheduled", s.scheduled);
    g.writeEntry("Frequency", s.frequency);
    g.writeEntry("Day", s.day);
    g.writeEntry("Hour", s.hour);
    g.writeEntry("LastBackup", s.lastBackup);
    g.writeEntry("LastFailed", s.lastFailed);
    g.writeEntry("LastError", s.lastError);
    g.writeEntry("SetSize", s.setSize);
    g.sync();
    // the network password lives here: only the owner may read the file
    ::chmod(QFile::encodeName(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-backuprc")).constData(), 0600);
}

inline QString weekdayEvery(int d)
{
    static const QStringList n{QStringLiteral("Щопонеділка"), QStringLiteral("Щовівторка"), QStringLiteral("Щосереди"), QStringLiteral("Щочетверга"),
                               QStringLiteral("Щоп'ятниці"), QStringLiteral("Щосуботи"), QStringLiteral("Щонеділі")};
    return n.value(qBound(1, d, 7) - 1);
}

inline QString weekdayName(int d)
{
    return QLocale(QLocale::Ukrainian).dayName(qBound(1, d, 7), QLocale::LongFormat);
}

inline QString scheduleText(const Settings &s)
{
    const QString at = QStringLiteral("о %1:00").arg(s.hour);
    switch (s.frequency) {
    case 0:
        return QStringLiteral("Щодня ") + at;
    case 2:
        return QStringLiteral("Щомісяця %1-го числа ").arg(s.day) + at;
    }
    return weekdayEvery(s.day) + QLatin1Char(' ') + at;
}

inline QDateTime nextRun(const Settings &s)
{
    QDateTime t(QDate::currentDate(), QTime(s.hour, 0));
    for (int i = 0; i < 400; ++i, t = t.addDays(1)) {
        if (t <= QDateTime::currentDateTime()) {
            continue;
        }
        if (s.frequency == 0 || (s.frequency == 1 && t.date().dayOfWeek() == s.day) || (s.frequency == 2 && t.date().day() == s.day)) {
            return t;
        }
    }
    return {};
}

/** writes and enables the systemd user timer, or removes it */
inline void applySchedule(const Settings &s)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/systemd/user");
    QDir().mkpath(dir);
    auto systemctl = [](const QStringList &a) {
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(QStringLiteral("systemctl"), QStringList{QStringLiteral("--user")} + a);
        p.waitForFinished(20000);
        if (p.exitCode() != 0) {
            qWarning("seven-backup: systemctl %s: %s", qPrintable(a.join(QLatin1Char(' '))), p.readAll().constData());
        }
    };
    if (!s.configured || !s.scheduled) {
        systemctl({QStringLiteral("disable"), QStringLiteral("--now"), QStringLiteral("seven-backup.timer")});
        return;
    }
    static const QStringList days{QStringLiteral("Mon"), QStringLiteral("Tue"), QStringLiteral("Wed"), QStringLiteral("Thu"), QStringLiteral("Fri"), QStringLiteral("Sat"), QStringLiteral("Sun")};
    QString cal;
    switch (s.frequency) {
    case 0:
        cal = QStringLiteral("*-*-* %1:00:00").arg(s.hour, 2, 10, QLatin1Char('0'));
        break;
    case 2:
        cal = QStringLiteral("*-*-%1 %2:00:00").arg(s.day, 2, 10, QLatin1Char('0')).arg(s.hour, 2, 10, QLatin1Char('0'));
        break;
    default:
        cal = QStringLiteral("%1 *-*-* %2:00:00").arg(days.value(s.day - 1), QString::number(s.hour).rightJustified(2, QLatin1Char('0')));
    }
    QFile svc(dir + QStringLiteral("/seven-backup.service"));
    if (svc.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        svc.write(QStringLiteral("[Unit]\nDescription=Архівація даних (Windows 7)\n\n[Service]\nType=oneshot\nNice=10\nIOSchedulingClass=idle\nExecStart=%1 --backup-run\n")
                      .arg(QCoreApplication::applicationFilePath())
                      .toUtf8());
        svc.close();
    }
    QFile timer(dir + QStringLiteral("/seven-backup.timer"));
    if (timer.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        timer.write(QStringLiteral("[Unit]\nDescription=Розклад архівації даних\n\n[Timer]\nOnCalendar=%1\nPersistent=true\n\n[Install]\nWantedBy=timers.target\n").arg(cal).toUtf8());
        timer.close(); // an unflushed (empty) unit file counts as masked
    }
    systemctl({QStringLiteral("daemon-reload")});
    systemctl({QStringLiteral("enable"), QStringLiteral("--now"), QStringLiteral("seven-backup.timer")});
    systemctl({QStringLiteral("restart"), QStringLiteral("seven-backup.timer")});
}

/* ---------------- what gets backed up ---------------- */

struct Library {
    QString title;
    QString path;
};

inline QList<Library> libraries()
{
    QList<Library> l;
    auto add = [&l](const QString &title, QStandardPaths::StandardLocation loc) {
        const QString p = QStandardPaths::writableLocation(loc);
        if (!p.isEmpty() && p != QDir::homePath() && QFileInfo::exists(p)) {
            l.append({title, p});
        }
    };
    add(QStringLiteral("Документи"), QStandardPaths::DocumentsLocation);
    add(QStringLiteral("Зображення"), QStandardPaths::PicturesLocation);
    add(QStringLiteral("Музика"), QStandardPaths::MusicLocation);
    add(QStringLiteral("Відео"), QStandardPaths::MoviesLocation);
    add(QStringLiteral("Робочий стіл"), QStandardPaths::DesktopLocation);
    add(QStringLiteral("Завантаження"), QStandardPaths::DownloadLocation);
    return l;
}

/** "Which files are excluded by default": caches, the Trash and game libraries, like Windows 7 skips temporary files */
inline QStringList defaultExcludes()
{
    return {QStringLiteral(".cache/"),       QStringLiteral(".local/share/Trash/"), QStringLiteral(".steam/"),     QStringLiteral(".local/share/Steam/"),
            QStringLiteral("snap/*/*/.cache/"), QStringLiteral(".var/app/*/cache/"),   QStringLiteral(".thumbnails/"), QStringLiteral("*.tmp"),
            QStringLiteral(".npm/"),          QStringLiteral(".gradle/caches/"),     QStringLiteral(".local/share/baloo/"), QStringLiteral("*.part"),
            QStringLiteral("lost+found/"),    QStringLiteral(".Trash-*/")};
}

inline QStringList sources(const Settings &s)
{
    return s.automatic ? QStringList{QDir::homePath()} : s.items;
}

/* ---------------- backup sets ---------------- */

struct BackupSet {
    QString path; // .../<computer>/Backup Set 2026-10-03 190000
    QString computer;
    QDateTime when;
};

inline QList<BackupSet> setsIn(const QString &root)
{
    QList<BackupSet> out;
    for (const QFileInfo &c : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        for (const QFileInfo &s : QDir(c.absoluteFilePath()).entryInfoList({QStringLiteral("Backup Set *")}, QDir::Dirs)) {
            const QDateTime when = QDateTime::fromString(s.fileName().mid(11), QStringLiteral("yyyy-MM-dd HHmmss"));
            if (when.isValid() && !QFileInfo::exists(s.absoluteFilePath() + QStringLiteral("/.incomplete"))) {
                out.append({s.absoluteFilePath(), c.fileName(), when});
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const BackupSet &a, const BackupSet &b) {
        return a.when > b.when;
    });
    return out;
}

/** gvfs path of a mounted SMB share, mounting it first; empty on failure */
inline QString mountNetwork(const QString &url, const QString &user, const QString &password, QString *error = nullptr)
{
    const QUrl u(url);
    const QString server = u.host().toLower();
    const QString share = u.path().section(QLatin1Char('/'), 1, 1).toLower();
    const QString sub = u.path().section(QLatin1Char('/'), 2);
    const QString gvfs = QStringLiteral("/run/user/%1/gvfs").arg(getuid());
    auto find = [&]() -> QString {
        for (const QString &d : QDir(gvfs).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (d.startsWith(QLatin1String("smb-share:")) && d.contains(QStringLiteral("server=") + server) && d.contains(QStringLiteral("share=") + share)) {
                return gvfs + QLatin1Char('/') + d + (sub.isEmpty() ? QString() : QLatin1Char('/') + sub);
            }
        }
        return {};
    };
    QString found = find();
    if (!found.isEmpty()) {
        return found;
    }
    QProcess p;
    p.start(QStringLiteral("gio"), {QStringLiteral("mount"), QStringLiteral("smb://%1/%2").arg(u.host(), u.path().section(QLatin1Char('/'), 1, 1))});
    p.waitForStarted(3000);
    p.write((user + QStringLiteral("\n\n") + password + QStringLiteral("\n")).toUtf8());
    p.closeWriteChannel();
    p.waitForFinished(30000);
    found = find();
    if (found.isEmpty() && error) {
        *error = QString::fromUtf8(p.readAllStandardError()).trimmed();
    }
    return found;
}

/** the folder the backups go to: the root of the drive or the network folder */
inline QString destinationRoot(const Settings &s, QString *error = nullptr)
{
    if (s.network) {
        return mountNetwork(s.netUrl, s.netUser, s.netPassword, error);
    }
    const QString mp = mountVolume(s.uuid);
    if (mp.isEmpty() && error) {
        *error = QStringLiteral("Не вдалося знайти диск для архівації «%1». Підключіть його та повторіть спробу.").arg(s.label);
    }
    return mp;
}

inline QString networkLabel(const QString &url)
{
    QString l = url;
    l.replace(QStringLiteral("smb://"), QStringLiteral("\\\\"));
    l.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return l;
}

/* ---------------- status of a running backup ---------------- */

inline QString statusFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/seven-backup");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/status.json");
}

struct Status {
    bool running = false;
    int percent = 0;
    QString phase;
    QString file;
    qint64 pid = 0;
    qint64 child = 0;
};

inline Status status()
{
    QFile f(statusFile());
    Status s;
    if (!f.open(QIODevice::ReadOnly)) {
        return s;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    s.pid = o.value(QStringLiteral("pid")).toInteger();
    s.child = o.value(QStringLiteral("child")).toInteger();
    s.running = o.value(QStringLiteral("running")).toBool() && s.pid > 0 && ::kill(pid_t(s.pid), 0) == 0;
    s.percent = o.value(QStringLiteral("percent")).toInt();
    s.phase = o.value(QStringLiteral("phase")).toString();
    s.file = o.value(QStringLiteral("file")).toString();
    return s;
}

inline void writeStatus(bool running, int percent, const QString &phase, const QString &file = QString(), qint64 child = 0)
{
    QFile f(statusFile() + QStringLiteral(".new"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(QJsonObject{{QStringLiteral("running"), running},
                                          {QStringLiteral("percent"), percent},
                                          {QStringLiteral("phase"), phase},
                                          {QStringLiteral("file"), file},
                                          {QStringLiteral("pid"), qint64(getpid())},
                                          {QStringLiteral("child"), child}})
                    .toJson(QJsonDocument::Compact));
        f.close();
        QFile::remove(statusFile());
        f.rename(statusFile());
    }
}

inline void stopBackup()
{
    const Status s = status();
    if (s.child > 0) {
        ::kill(pid_t(s.child), SIGTERM);
    }
    if (s.pid > 0) {
        ::kill(pid_t(s.pid), SIGTERM);
    }
}

inline void startBackup()
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--backup-run")});
}

/* ---------------- Timeshift: restore points and system images ---------------- */

inline bool haveTimeshift()
{
    return !QStandardPaths::findExecutable(QStringLiteral("timeshift")).isEmpty();
}

/** asks once to install Timeshift, the engine of System Restore and system images */
inline bool ensureTimeshift(QWidget *parent)
{
    if (haveTimeshift() || !qEnvironmentVariableIsEmpty("WIN7_BACKUP_TEST")) {
        return true;
    }
    if (QMessageBox::question(parent, QStringLiteral("Архівація і відновлення"),
                              QStringLiteral("Для точок відновлення та образів системи потрібен компонент Timeshift.\n\nВстановити його зараз? (потрібне підключення до Інтернету)"))
        != QMessageBox::Yes) {
        return false;
    }
    QDialog wait(parent);
    wait.setWindowTitle(QStringLiteral("Встановлення компонента"));
    auto *v = new QVBoxLayout(&wait);
    v->addWidget(new QLabel(QStringLiteral("Зачекайте, триває встановлення Timeshift...")));
    auto *bar = new QProgressBar;
    bar->setRange(0, 0);
    v->addWidget(bar);
    wait.setFixedSize(360, 90);
    wait.show();
    QString out;
    const bool ok = runAsRoot(parent, QStringLiteral("DEBIAN_FRONTEND=noninteractive apt-get install -y timeshift"), &out);
    wait.close();
    return ok && haveTimeshift();
}

struct Snapshot {
    QString name; // 2026-10-03_19-00-01
    QDateTime when;
    QString comment;
    QString tags;
};

/** snapshots under <root>/timeshift/snapshots (info.json is readable without root) */
inline QList<Snapshot> snapshotsIn(const QString &root)
{
    QList<Snapshot> out;
    const QString dir = root + QStringLiteral("/timeshift/snapshots");
    for (const QFileInfo &fi : QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        Snapshot s;
        s.name = fi.fileName();
        s.when = QDateTime::fromString(s.name, QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
        QFile f(fi.absoluteFilePath() + QStringLiteral("/info.json"));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            s.comment = o.value(QStringLiteral("comments")).toString();
            s.tags = o.value(QStringLiteral("tags")).toString();
            const qint64 created = o.value(QStringLiteral("created")).toString().toLongLong();
            if (created > 0) {
                s.when = QDateTime::fromSecsSinceEpoch(created);
            }
        }
        if (s.when.isValid()) {
            out.append(s);
        }
    }
    std::sort(out.begin(), out.end(), [](const Snapshot &a, const Snapshot &b) {
        return a.when > b.when;
    });
    return out;
}

/** progress window of a Timeshift run as root; returns true on success */
inline bool runTimeshift(QWidget *parent, const QString &title, const QString &label, const QStringList &args, const QString &stopText = QString())
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-bk-image")));
    dlg.setFixedSize(560, 230);
    auto *outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *white = new QWidget;
    white->setObjectName(QStringLiteral("win7bkwhite"));
    white->setAttribute(Qt::WA_StyledBackground, true);
    white->setStyleSheet(QStringLiteral("#win7bkwhite { background: white; }"));
    auto *wv = new QVBoxLayout(white);
    wv->setContentsMargins(16, 16, 16, 16);
    auto *caption = new QLabel(label);
    wv->addWidget(caption);
    auto *bar = new QProgressBar;
    bar->setRange(0, 100);
    bar->setTextVisible(false);
    bar->setFixedHeight(16);
    wv->addWidget(bar);
    wv->addStretch(1);
    outer->addWidget(white, 1);
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
    outer->addWidget(line);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(10, 10, 10, 10);
    row->addStretch(1);
    auto *stop = new QPushButton(stopText.isEmpty() ? QStringLiteral("Припинити архівацію") : stopText);
    stop->setMinimumWidth(150);
    row->addWidget(stop);
    outer->addLayout(row);
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    QString log;
    static const QRegularExpression pct(QStringLiteral("([0-9]+(?:\\.[0-9]+)?)% complete"));
    QObject::connect(&p, &QProcess::readyRead, &dlg, [&] {
        const QString chunk = QString::fromUtf8(p.readAll());
        log += chunk;
        auto it = pct.globalMatch(chunk);
        while (it.hasNext()) {
            bar->setValue(int(it.next().captured(1).toDouble()));
        }
    });
    bool finished = false;
    QObject::connect(&p, &QProcess::finished, &dlg, [&] {
        finished = true;
        dlg.accept();
    });
    QObject::connect(stop, &QPushButton::clicked, &dlg, [&] {
        if (QMessageBox::question(&dlg, title, QStringLiteral("Припинити операцію?")) == QMessageBox::Yes) {
            QProcess::execute(QStringLiteral("pkexec"), {QStringLiteral("pkill"), QStringLiteral("-INT"), QStringLiteral("-x"), QStringLiteral("timeshift")});
        }
    });
    p.start(QStringLiteral("pkexec"), QStringList{QStringLiteral("timeshift")} + args);
    if (!p.waitForStarted(5000)) {
        return false;
    }
    dlg.exec();
    if (!finished) {
        p.waitForFinished(-1);
    }
    const bool ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    if (p.exitCode() == 126 || p.exitCode() == 127) {
        return false; // password dialog dismissed
    }
    // the result window, like "The backup completed successfully."
    QDialog done(parent);
    done.setWindowTitle(title);
    done.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-bk-image")));
    done.setFixedSize(560, 230);
    auto *dv = new QVBoxLayout(&done);
    dv->setContentsMargins(0, 0, 0, 0);
    dv->setSpacing(0);
    auto *dw = new QWidget;
    dw->setObjectName(QStringLiteral("win7bkwhite"));
    dw->setAttribute(Qt::WA_StyledBackground, true);
    dw->setStyleSheet(QStringLiteral("#win7bkwhite { background: white; }"));
    auto *dwv = new QVBoxLayout(dw);
    dwv->setContentsMargins(16, 16, 16, 16);
    dwv->addWidget(new QLabel(ok ? QStringLiteral("Операцію виконано успішно.") : QStringLiteral("Не вдалося виконати операцію.")));
    auto *full = new QProgressBar;
    full->setRange(0, 100);
    full->setValue(ok ? 100 : bar->value());
    full->setTextVisible(false);
    full->setFixedHeight(16);
    dwv->addWidget(full);
    if (!ok) {
        auto *err = new QLabel(log.trimmed().section(QLatin1Char('\n'), -3).toHtmlEscaped());
        err->setWordWrap(true);
        err->setStyleSheet(QStringLiteral("color: #8d1414;"));
        dwv->addWidget(err);
    }
    dwv->addStretch(1);
    dv->addWidget(dw, 1);
    auto *dl = new QFrame;
    dl->setFixedHeight(1);
    dl->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
    dv->addWidget(dl);
    auto *dr = new QHBoxLayout;
    dr->setContentsMargins(10, 10, 10, 10);
    dr->addStretch(1);
    auto *close = new QPushButton(QStringLiteral("Закрити"));
    close->setMinimumWidth(150);
    close->setDefault(true);
    dr->addWidget(close);
    dv->addLayout(dr);
    QObject::connect(close, &QPushButton::clicked, &done, &QDialog::accept);
    done.exec();
    return ok;
}

/* ---------------- the backup itself (seven-control --backup-run) ---------------- */

inline void notify(const QString &title, const QString &body, const QString &icon = QStringLiteral("seven-cpi-backup"))
{
    QProcess::startDetached(QStringLiteral("notify-send"), {QStringLiteral("-a"), QStringLiteral("Архівація даних"), QStringLiteral("-i"), icon, title, body});
}

/** runs a file backup (and the system image when it is part of the plan); exit code 0 on success */
inline int runBackup()
{
    if (status().running) {
        return 0; // one backup at a time
    }
    Settings s = load();
    if (!s.configured) {
        return 1;
    }
    signal(SIGTERM, [](int) {
        writeStatus(false, 0, QStringLiteral("Архівацію зупинено"));
        _exit(3);
    });
    writeStatus(true, 0, QStringLiteral("Підготовка до архівації..."));
    auto fail = [&s](const QString &why) {
        s = load();
        s.lastFailed = true;
        s.lastError = why;
        s.lastBackup = QDateTime::currentSecsSinceEpoch();
        save(s);
        writeStatus(false, 0, why);
        notify(QStringLiteral("Не вдалося виконати архівацію"), why, QStringLiteral("seven-bk-nodrive"));
        return 2;
    };
    QString error;
    const QString root = destinationRoot(s, &error);
    if (root.isEmpty()) {
        return fail(error.isEmpty() ? QStringLiteral("Розташування архіву недоступне.") : error);
    }
    const QString computerDir = root + QLatin1Char('/') + host();
    const QList<BackupSet> before = setsIn(root);
    QString previous;
    for (const BackupSet &b : before) {
        if (b.computer == host()) {
            previous = b.path;
            break;
        }
    }
    const QString target = computerDir + QStringLiteral("/Backup Set ") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HHmmss"));
    if (!QDir().mkpath(target)) {
        return fail(QStringLiteral("Не вдалося створити папку архіву в «%1».").arg(root));
    }
    QFile marker(target + QStringLiteral("/.incomplete"));
    marker.open(QIODevice::WriteOnly);
    marker.close();

    QStringList args{QStringLiteral("-aR"), QStringLiteral("--info=progress2,name1"), QStringLiteral("--no-inc-recursive"), QStringLiteral("--exclude=/.incomplete")};
    for (const QString &x : defaultExcludes()) {
        args << QStringLiteral("--exclude=") + x;
    }
    if (!previous.isEmpty()) {
        args << QStringLiteral("--link-dest=") + previous;
    }
    // never back up the backup into itself
    args << QStringLiteral("--exclude=") + root + QLatin1Char('/');
    for (const QString &src : sources(s)) {
        if (QFileInfo::exists(src)) {
            args << src;
        }
    }
    args << target + QLatin1Char('/');
    QProcess rsync;
    rsync.setProcessChannelMode(QProcess::SeparateChannels);
    rsync.start(QStringLiteral("rsync"), args);
    if (!rsync.waitForStarted(5000)) {
        return fail(QStringLiteral("Не вдалося запустити копіювання файлів."));
    }
    static const QRegularExpression pct(QStringLiteral("\\s([0-9]{1,3})%\\s"));
    QString current;
    int lastPercent = -1;
    QByteArray pending;
    const int imageShare = s.image ? 70 : 100; // the files take this part of the progress bar
    while (rsync.state() != QProcess::NotRunning) {
        rsync.waitForReadyRead(1000);
        pending += rsync.readAllStandardOutput();
        int cut;
        while ((cut = qMax(pending.indexOf('\r'), pending.indexOf('\n'))) >= 0) {
            const int a = pending.indexOf('\r'), b = pending.indexOf('\n');
            cut = a < 0 ? b : b < 0 ? a : qMin(a, b);
            const QString line = QString::fromUtf8(pending.left(cut));
            pending.remove(0, cut + 1);
            const auto m = pct.match(line);
            if (m.hasMatch()) {
                const int p = m.captured(1).toInt() * imageShare / 100;
                if (p != lastPercent) {
                    lastPercent = p;
                    writeStatus(true, p, QStringLiteral("Копіювання файлів..."), current, rsync.processId());
                }
            } else if (!line.trimmed().isEmpty()) {
                current = line.trimmed().startsWith(QLatin1Char('/')) ? line.trimmed() : QLatin1Char('/') + line.trimmed();
            }
        }
    }
    const int rc = rsync.exitCode();
    // 23/24: some files could not be read or vanished meanwhile, the rest is saved (Windows 7 reports it as a warning)
    if (rc != 0 && rc != 23 && rc != 24) {
        QDir(target).removeRecursively();
        return fail(QStringLiteral("Помилка під час копіювання файлів (код %1). %2").arg(rc).arg(QString::fromUtf8(rsync.readAllStandardError()).trimmed().section(QLatin1Char('\n'), -1)));
    }
    QFile::remove(target + QStringLiteral("/.incomplete"));

    if (s.image && !s.network) {
        writeStatus(true, imageShare, QStringLiteral("Створення образу системи..."));
        QProcess ts;
        ts.setProcessChannelMode(QProcess::MergedChannels);
        ts.start(QStringLiteral("pkexec"), {QStringLiteral("timeshift"), QStringLiteral("--create"), QStringLiteral("--scripted"), QStringLiteral("--comments"),
                                            QStringLiteral("Образ системи"), QStringLiteral("--snapshot-device"), QStringLiteral("UUID=") + s.uuid});
        static const QRegularExpression tpct(QStringLiteral("([0-9]+(?:\\.[0-9]+)?)% complete"));
        while (ts.state() != QProcess::NotRunning) {
            ts.waitForReadyRead(1000);
            auto it = tpct.globalMatch(QString::fromUtf8(ts.readAll()));
            while (it.hasNext()) {
                writeStatus(true, imageShare + int(it.next().captured(1).toDouble() * (100 - imageShare) / 100), QStringLiteral("Створення образу системи..."),
                            QString(), ts.processId());
            }
        }
    }

    // size of this set on the drive (hard-linked files of older sets are not counted twice)
    QProcess du;
    du.start(QStringLiteral("du"), {QStringLiteral("-sb"), computerDir});
    du.waitForFinished(600000);
    s = load();
    s.setSize = QString::fromUtf8(du.readAllStandardOutput()).section(QLatin1Char('\t'), 0, 0).toLongLong();
    s.lastBackup = QDateTime::currentSecsSinceEpoch();
    s.lastFailed = false;
    s.lastError.clear();
    save(s);
    writeStatus(false, 100, rc == 0 ? QStringLiteral("Архівацію виконано успішно") : QStringLiteral("Архівацію виконано, але деякі файли пропущено"));
    notify(QStringLiteral("Архівацію виконано"), rc == 0 ? QStringLiteral("Резервну копію збережено в «%1».").arg(s.label)
                                                          : QStringLiteral("Деякі файли не вдалося прочитати, решту збережено в «%1».").arg(s.label));
    return 0;
}

/* ---------------- wizard plumbing ---------------- */

/** a wizard page whose behaviour is given by lambdas */
class Page : public QWizardPage
{
public:
    std::function<bool()> complete;
    std::function<int()> next;
    std::function<void()> enter;
    std::function<bool()> validate;
    void changed()
    {
        Q_EMIT completeChanged();
    }
    bool isComplete() const override
    {
        return complete ? complete() : true;
    }
    int nextId() const override
    {
        return next ? next() : QWizardPage::nextId();
    }
    void initializePage() override
    {
        if (enter) {
            enter();
        }
    }
    bool validatePage() override
    {
        return validate ? validate() : true;
    }
};

/** the Windows 7 Aero wizard: glass header with the round back button, blue instruction, grey button band */
class AeroWizard : public QWizard
{
public:
    AeroWizard(QWidget *parent, const QString &title, const QString &icon, QSize size)
        : QWizard(parent)
    {
        setWindowTitle(title);
        setWindowIcon(QIcon::fromTheme(icon));
        setOption(QWizard::NoBackButtonOnStartPage, true);
        setOption(QWizard::NoBackButtonOnLastPage, true);
        setButtonText(QWizard::NextButton, QStringLiteral("Далі"));
        setButtonText(QWizard::CancelButton, QStringLiteral("Скасувати"));
        setButtonText(QWizard::FinishButton, QStringLiteral("Готово"));
        setButtonText(QWizard::BackButton, QStringLiteral("Назад"));
        // Windows 7 has no Back button at the bottom: the round arrow in the title area goes back
        setButtonLayout({QWizard::Stretch, QWizard::NextButton, QWizard::FinishButton, QWizard::CancelButton});
        Aero::Wizard::convert(this);
        resize(size);
    }

    Page *addPage(int id, const QString &instruction, QVBoxLayout *&v)
    {
        auto *p = new Page;
        p->setTitle(instruction);
        v = new QVBoxLayout(p);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(10);
        setPage(id, p);
        return p;
    }
};

/* ---------------- "Set up backup" ---------------- */

class NetworkDialog : public AeroWizard
{
public:
    explicit NetworkDialog(QWidget *parent)
        : AeroWizard(parent, QStringLiteral("Настроювання архівації"), QStringLiteral("seven-cpi-backup"), QSize(640, 500))
    {
        QVBoxLayout *v;
        Page *p = addPage(0, QStringLiteral("Виберіть мережеве розташування"), v);
        v->addWidget(text(QStringLiteral("Укажіть мережеве розташування архівних файлів і облікові дані, які використовуватимуться програмою архівації даних для доступу до розташування.")));
        v->addSpacing(6);
        v->addWidget(new QLabel(QStringLiteral("Мережеве розташування:")));
        auto *row = new QHBoxLayout;
        m_path = new QLineEdit;
        row->addWidget(m_path, 1);
        auto *browse = new QPushButton(QStringLiteral("Огляд..."));
        row->addWidget(browse);
        v->addLayout(row);
        v->addWidget(new QLabel(QStringLiteral("Приклад: \\\\сервер\\спільний_ресурс")));
        v->addSpacing(10);
        v->addWidget(new QLabel(QStringLiteral("Мережеві облікові дані")));
        v->addWidget(text(QStringLiteral("Програмі архівації даних знадобляться ім'я користувача та пароль для доступу до мережевого розташування під час збереження архіву.")));
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(30);
        m_user = new QLineEdit;
        m_password = new QLineEdit;
        m_password->setEchoMode(QLineEdit::Password);
        m_user->setFixedWidth(270);
        m_password->setFixedWidth(270);
        g->addWidget(new QLabel(QStringLiteral("Користувач:")), 0, 0);
        g->addWidget(m_user, 0, 1, Qt::AlignLeft);
        g->addWidget(new QLabel(QStringLiteral("Пароль:")), 1, 0);
        g->addWidget(m_password, 1, 1, Qt::AlignLeft);
        g->setColumnStretch(1, 1);
        v->addLayout(g);
        v->addStretch(1);
        setButtonText(QWizard::FinishButton, QStringLiteral("OK"));
        p->setFinalPage(true);
        p->complete = [this] {
            return m_path->text().trimmed().startsWith(QLatin1String("\\\\")) || m_path->text().trimmed().startsWith(QLatin1String("smb://"));
        };
        connect(m_path, &QLineEdit::textChanged, p, [p] {
            p->changed();
        });
        connect(browse, &QPushButton::clicked, this, [this] {
            const QUrl u = QFileDialog::getExistingDirectoryUrl(this, QStringLiteral("Огляд папок"), QUrl(QStringLiteral("smb://")), QFileDialog::ShowDirsOnly,
                                                                {QStringLiteral("smb")});
            if (u.isValid() && !u.isEmpty()) {
                m_path->setText(networkLabel(u.toString(QUrl::RemoveUserInfo)));
            }
        });
        p->validate = [this] {
            QString url = m_path->text().trimmed();
            if (url.startsWith(QLatin1String("\\\\"))) {
                url = QStringLiteral("smb://") + url.mid(2).replace(QLatin1Char('\\'), QLatin1Char('/'));
            }
            while (url.endsWith(QLatin1Char('/'))) {
                url.chop(1);
            }
            QString error;
            QApplication::setOverrideCursor(Qt::WaitCursor);
            const QString mounted = mountNetwork(url, m_user->text(), m_password->text(), &error);
            QApplication::restoreOverrideCursor();
            if (mounted.isEmpty() || !QFileInfo(mounted).isDir()) {
                QMessageBox::warning(this, QStringLiteral("Настроювання архівації"),
                                     QStringLiteral("Не вдалося отримати доступ до мережевого розташування «%1».\n\n%2").arg(m_path->text(), error));
                return false;
            }
            m_url = url;
            m_mounted = mounted;
            return true;
        };
    }
    QString url() const
    {
        return m_url;
    }
    QString mounted() const
    {
        return m_mounted;
    }
    QString user() const
    {
        return m_user->text();
    }
    QString password() const
    {
        return m_password->text();
    }

private:
    QLineEdit *m_path;
    QLineEdit *m_user;
    QLineEdit *m_password;
    QString m_url;
    QString m_mounted;
};

/** "How often do you want to back up?" */
class ScheduleDialog : public AeroWizard
{
public:
    ScheduleDialog(QWidget *parent, Settings &s)
        : AeroWizard(parent, QStringLiteral("Настроювання архівації"), QStringLiteral("seven-cpi-backup"), QSize(640, 500))
    {
        QVBoxLayout *v;
        Page *p = addPage(0, QStringLiteral("Як часто слід виконувати архівацію?"), v);
        v->addWidget(text(QStringLiteral("Змінені файли та нові файли, створені після останньої архівації, додаватимуться до архіву відповідно до вказаного нижче розкладу.")));
        v->addSpacing(8);
        auto *on = new QCheckBox(QStringLiteral("Виконувати резервне копіювання за розкладом (рекомендовано)"));
        on->setChecked(s.scheduled);
        v->addWidget(on);
        v->addSpacing(6);
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(14);
        g->setVerticalSpacing(14);
        auto *freq = new QComboBox;
        freq->addItems({QStringLiteral("Щодня"), QStringLiteral("Щотижня"), QStringLiteral("Щомісяця")});
        freq->setCurrentIndex(s.frequency);
        auto *day = new QComboBox;
        auto *time = new QComboBox;
        for (int h = 0; h < 24; ++h) {
            time->addItem(QStringLiteral("%1:00").arg(h));
        }
        time->setCurrentIndex(s.hour);
        for (QComboBox *c : {freq, day, time}) {
            c->setFixedWidth(260);
        }
        auto fillDays = [day, freq, &s] {
            day->clear();
            if (freq->currentIndex() == 1) {
                for (int d = 1; d <= 7; ++d) {
                    day->addItem(weekdayName(d), d);
                }
                day->setCurrentIndex(qBound(0, s.day - 1, 6));
            } else if (freq->currentIndex() == 2) {
                for (int d = 1; d <= 28; ++d) {
                    day->addItem(QString::number(d), d);
                }
                day->setCurrentIndex(qBound(0, s.day - 1, 27));
            }
            day->setEnabled(freq->currentIndex() != 0 && freq->isEnabled());
        };
        fillDays();
        g->addWidget(new QLabel(QStringLiteral("Як часто:")), 0, 0);
        g->addWidget(freq, 0, 1, Qt::AlignLeft);
        g->addWidget(new QLabel(QStringLiteral("У які дні:")), 1, 0);
        g->addWidget(day, 1, 1, Qt::AlignLeft);
        g->addWidget(new QLabel(QStringLiteral("О котрій годині:")), 2, 0);
        g->addWidget(time, 2, 1, Qt::AlignLeft);
        g->setColumnStretch(1, 1);
        v->addLayout(g);
        v->addStretch(1);
        auto enable = [on, freq, day, time] {
            freq->setEnabled(on->isChecked());
            time->setEnabled(on->isChecked());
            day->setEnabled(on->isChecked() && freq->currentIndex() != 0);
        };
        enable();
        connect(on, &QCheckBox::toggled, this, enable);
        connect(freq, &QComboBox::currentIndexChanged, this, [fillDays, enable] {
            fillDays();
            enable();
        });
        p->setFinalPage(true);
        setButtonText(QWizard::FinishButton, QStringLiteral("OK"));
        p->validate = [&s, on, freq, day, time] {
            s.scheduled = on->isChecked();
            s.frequency = freq->currentIndex();
            s.day = s.frequency == 0 ? s.day : day->currentData().toInt();
            s.hour = time->currentIndex();
            return true;
        };
    }
};

class SetupWizard : public AeroWizard
{
public:
    enum { WherePage, WhatPage, ChoosePage, ReviewPage };

    explicit SetupWizard(QWidget *parent)
        : AeroWizard(parent, QStringLiteral("Настроювання архівації"), QStringLiteral("seven-cpi-backup"), QSize(690, 560))
        , m_s(load())
    {
        buildWhere();
        buildWhat();
        buildChoose();
        buildReview();
        setStartId(WherePage);
    }

    Settings settings() const
    {
        return m_s;
    }

private:
    void buildWhere()
    {
        QVBoxLayout *v;
        Page *p = addPage(WherePage, QStringLiteral("Виберіть, де зберігати створювані архіви"), v);
        auto *intro = new QHBoxLayout;
        intro->setSpacing(4);
        intro->addWidget(new QLabel(QStringLiteral("Рекомендовано зберігати архів на зовнішньому жорсткому диску.")));
        intro->addWidget(helpLink(this, QStringLiteral("Рекомендації щодо вибору розташування архіву"), QStringLiteral("Вибір розташування архіву"),
                                  QStringLiteral("Найкраще зберігати архів на зовнішньому жорсткому диску: якщо внутрішній диск вийде з ладу, архів залишиться неушкодженим.\n\n"
                                                 "Диск із Windows не пропонується: Linux його не змінює.")),
                         1);
        v->addLayout(intro);
        v->addSpacing(4);
        v->addWidget(new QLabel(QStringLiteral("Зберегти резервну копію на:")));
        m_where = new QTreeWidget;
        m_where->setRootIsDecorated(false);
        m_where->setHeaderLabels({QStringLiteral("Місце призначення архівації"), QStringLiteral("Вільно"), QStringLiteral("Повний розмір")});
        m_where->header()->setStretchLastSection(false);
        m_where->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_where->setColumnWidth(1, 110);
        m_where->setColumnWidth(2, 110);
        m_where->setIconSize(QSize(32, 32));
        m_where->setMinimumHeight(200);
        v->addWidget(m_where, 1);
        auto *row = new QHBoxLayout;
        auto *refresh = new QPushButton(QStringLiteral("Оновити"));
        auto *net = new QPushButton(QStringLiteral("Зберегти в мережі..."));
        refresh->setMinimumWidth(120);
        net->setMinimumWidth(220);
        row->addWidget(refresh);
        row->addStretch(1);
        row->addWidget(net);
        v->addLayout(row);
        auto *info = new QFrame;
        info->setObjectName(QStringLiteral("win7bkinfo"));
        info->setStyleSheet(QStringLiteral("#win7bkinfo { border: 1px solid #c4c4c4; background: white; }"));
        info->setMinimumHeight(58);
        auto *ih = new QHBoxLayout(info);
        ih->setContentsMargins(10, 8, 10, 8);
        m_infoIcon = iconLabel(QStringLiteral("seven-ev-warning"), 16);
        ih->addWidget(m_infoIcon, 0, Qt::AlignTop);
        m_info = new QLabel;
        m_info->setWordWrap(true);
        ih->addWidget(m_info, 1);
        v->addWidget(info);
        p->complete = [this] {
            return m_where->currentItem() != nullptr;
        };
        connect(m_where, &QTreeWidget::currentItemChanged, p, [this, p] {
            updateInfo();
            p->changed();
        });
        connect(refresh, &QPushButton::clicked, this, [this] {
            fillWhere();
        });
        connect(net, &QPushButton::clicked, this, [this] {
            NetworkDialog dlg(this);
            if (dlg.exec() == QDialog::Accepted) {
                m_netUrl = dlg.url();
                m_netUser = dlg.user();
                m_netPassword = dlg.password();
                m_netMounted = dlg.mounted();
                fillWhere();
                m_where->setCurrentItem(m_where->topLevelItem(m_where->topLevelItemCount() - 1));
            }
        });
        p->validate = [this] {
            QTreeWidgetItem *it = m_where->currentItem();
            if (!it) {
                return false;
            }
            m_s.network = it->data(0, Qt::UserRole + 1).toBool();
            if (m_s.network) {
                m_s.netUrl = m_netUrl;
                m_s.netUser = m_netUser;
                m_s.netPassword = m_netPassword;
                m_s.label = networkLabel(m_netUrl);
                m_s.uuid.clear();
            } else {
                m_s.uuid = it->data(0, Qt::UserRole).toString();
                m_s.label = it->text(0);
                const QString mp = mountVolume(m_s.uuid);
                if (mp.isEmpty()) {
                    QMessageBox::warning(this, windowTitle(), QStringLiteral("Не вдалося підключити диск «%1».").arg(m_s.label));
                    return false;
                }
                // a drive with a Linux file system belongs to root: the folder of this computer is made ours once
                const QString dir = mp + QLatin1Char('/') + host();
                if (!QFileInfo(dir).isWritable() && !QDir().mkpath(dir)) {
                    if (!runAsRoot(this, QStringLiteral("install -d -o %1 -g %2 '%3'").arg(getuid()).arg(getgid()).arg(QString(dir).replace(QLatin1Char('\''), QStringLiteral("'\\''"))))) {
                        return false;
                    }
                }
            }
            return true;
        };
        fillWhere();
    }

    void fillWhere()
    {
        const QString keep = m_where->currentItem() ? m_where->currentItem()->data(0, Qt::UserRole).toString() : m_s.uuid;
        m_where->clear();
        for (Volume vol : volumes()) {
            if (vol.mount.isEmpty()) {
                vol.mount = mountVolume(vol.uuid); // Windows 7 lists the free space of every drive
            }
            const QStorageInfo st(vol.mount);
            if (!vol.mount.isEmpty() && st.isValid()) {
                vol.avail = st.bytesAvailable();
            }
            auto *it = new QTreeWidgetItem(m_where, {vol.label, vol.avail >= 0 ? gb(vol.avail) : QString(), gb(vol.size)});
            it->setIcon(0, QIcon::fromTheme(vol.icon()));
            it->setData(0, Qt::UserRole, vol.uuid);
            it->setData(0, Qt::UserRole + 2, vol.sameDiskAsSystem);
            it->setData(0, Qt::UserRole + 3, vol.fstype);
            if (vol.uuid == keep) {
                m_where->setCurrentItem(it);
            }
        }
        if (!m_netUrl.isEmpty()) {
            const QStorageInfo st(m_netMounted);
            auto *it = new QTreeWidgetItem(m_where, {networkLabel(m_netUrl), st.isValid() ? gb(st.bytesAvailable()) : QString(), st.isValid() ? gb(st.bytesTotal()) : QString()});
            it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-netdrive")));
            it->setData(0, Qt::UserRole + 1, true);
        }
        updateInfo();
    }

    void updateInfo()
    {
        QTreeWidgetItem *it = m_where->currentItem();
        QString msg;
        if (m_where->topLevelItemCount() == 0) {
            msg = QStringLiteral("Архівація даних не виявила дисків для збереження архівів. Підключіть зовнішній диск і натисніть «Оновити».");
        } else if (!it) {
            msg = QString();
        } else if (it->data(0, Qt::UserRole + 1).toBool()) {
            msg = QStringLiteral("Інші користувачі мережі можуть мати доступ до вашого архіву.");
        } else if (it->data(0, Qt::UserRole + 2).toBool()) {
            msg = QStringLiteral("Вибраний диск розташовано на тому самому фізичному диску, що й система. У разі збою цього диска архіви буде втрачено.");
        } else if (it->data(0, Qt::UserRole + 3).toString() == QLatin1String("vfat")) {
            msg = QStringLiteral("Файлова система FAT32 не зберігає файли розміром понад 4 ГБ, такі файли не потраплять до архіву.");
        }
        m_info->setText(msg);
        m_infoIcon->setVisible(!msg.isEmpty());
    }

    void buildWhat()
    {
        QVBoxLayout *v;
        Page *p = addPage(WhatPage, QStringLiteral("Що слід архівувати?"), v);
        m_auto = new QRadioButton(QStringLiteral("Надати вибір Windows (рекомендовано)"));
        m_manual = new QRadioButton(QStringLiteral("Надати мені вибір"));
        auto *autoText = text(QStringLiteral("Windows виконає архівацію файлів, збережених у бібліотеках, на робочому столі та в стандартних папках вашого профілю. "
                                             "Ці об'єкти регулярно архівуватимуться за розкладом."));
        auto *manualText = text(QStringLiteral("Можна вибрати бібліотеки та папки, а також указати, чи слід включати до архіву образ системи. "
                                               "Вибрані елементи регулярно архівуватимуться за розкладом."));
        autoText->setContentsMargins(20, 0, 0, 0);
        manualText->setContentsMargins(20, 0, 0, 0);
        auto *how = helpLink(this, QStringLiteral("Як вибираються файли для архівації?"), QStringLiteral("Вибір файлів"),
                             QStringLiteral("Архівуються всі файли вашої домашньої папки, крім тимчасових: кешів програм, кошика, бібліотек ігор Steam "
                                            "та файлів, що ще завантажуються."));
        how->setContentsMargins(20, 0, 0, 0);
        (m_s.automatic ? m_auto : m_manual)->setChecked(true);
        v->addWidget(m_auto);
        v->addWidget(autoText);
        v->addWidget(how);
        v->addSpacing(14);
        v->addWidget(m_manual);
        v->addWidget(manualText);
        v->addStretch(1);
        p->next = [this] {
            return m_manual->isChecked() ? ChoosePage : ReviewPage;
        };
        p->validate = [this] {
            m_s.automatic = m_auto->isChecked();
            if (m_s.automatic) {
                m_s.image = false;
            }
            return true;
        };
    }

    void buildChoose()
    {
        QVBoxLayout *v;
        Page *p = addPage(ChoosePage, QStringLiteral("Виберіть об'єкти для архівації"), v);
        auto *intro = new QLabel(QStringLiteral("Установіть прапорці для елементів, які необхідно включити до резервної копії. "
                                                "<a href=\"#\" style=\"color:#0066cc\">Які файли за замовчуванням виключаються з архіву?</a>"));
        intro->setWordWrap(true);
        connect(intro, &QLabel::linkActivated, this, [this] {
            QMessageBox::information(this, QStringLiteral("Виключені файли"),
                                     QStringLiteral("До архіву не потрапляють тимчасові файли: кеші програм (.cache), кошик, бібліотеки ігор Steam, "
                                                    "незавершені завантаження (*.part) і тимчасові файли (*.tmp)."));
        });
        v->addWidget(intro);
        m_tree = new QTreeWidget;
        m_tree->setHeaderHidden(true);
        m_tree->setIconSize(QSize(16, 16));
        m_tree->setStyleSheet(treeArrows());
        v->addWidget(m_tree, 1);
        auto *data = new QTreeWidgetItem(m_tree, {QStringLiteral("Файли даних")});
        data->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-users")));
        auto *libs = new QTreeWidgetItem(data, {QStringLiteral("%1 — Бібліотеки").arg(displayName())});
        libs->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-users")));
        QFont bold = libs->font(0);
        bold.setBold(true);
        libs->setFont(0, bold);
        libs->setFlags(libs->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        for (const Library &l : libraries()) {
            auto *it = new QTreeWidgetItem(libs, {l.title});
            it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-folder")));
            it->setData(0, Qt::UserRole, l.path);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(0, m_s.automatic || m_s.items.contains(l.path) ? Qt::Checked : Qt::Unchecked);
        }
        auto *computer = new QTreeWidgetItem(m_tree, {QStringLiteral("Комп'ютер")});
        computer->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-computer")));
        auto *disk = dirItem(computer, QStringLiteral("Локальний диск (/)"), QStringLiteral("/"));
        disk->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-sysdrive")));
        data->setExpanded(true);
        computer->setExpanded(true);
        connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *it) {
            populate(it);
        });
        m_imageBox = new QCheckBox(QStringLiteral("Включити образ системи дисків: (/)"));
        m_imageBox->setChecked(m_s.image);
        v->addWidget(m_imageBox);
        auto *imgText = text(QStringLiteral("Образ системи — це копія дисків, необхідних для роботи системи. Його можна використати для відновлення "
                                            "комп'ютера в разі несправностей."));
        imgText->setContentsMargins(20, 0, 0, 0);
        v->addWidget(imgText);
        p->complete = [this] {
            return !checkedPaths().isEmpty() || m_imageBox->isChecked();
        };
        connect(m_tree, &QTreeWidget::itemChanged, p, [p] {
            p->changed();
        });
        connect(m_imageBox, &QCheckBox::toggled, p, [p] {
            p->changed();
        });
        p->validate = [this] {
            m_s.items = checkedPaths();
            m_s.image = m_imageBox->isChecked();
            if (m_s.image && m_s.network) {
                QMessageBox::information(this, windowTitle(), QStringLiteral("Образ системи не можна зберегти в мережевому розташуванні. Буде виконано лише архівацію файлів."));
                m_s.image = false;
            }
            return true;
        };
        p->next = [] {
            return int(ReviewPage);
        };
    }

    QTreeWidgetItem *dirItem(QTreeWidgetItem *parent, const QString &name, const QString &path)
    {
        auto *it = new QTreeWidgetItem(parent, {name});
        it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-folder")));
        it->setData(0, Qt::UserRole, path);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(0, m_s.items.contains(path) ? Qt::Checked : Qt::Unchecked);
        if (QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size()) {
            new QTreeWidgetItem(it, {QStringLiteral("…")}); // placeholder until expanded
        }
        return it;
    }

    void populate(QTreeWidgetItem *it)
    {
        if (it->childCount() != 1 || it->child(0)->text(0) != QStringLiteral("…")) {
            return;
        }
        delete it->takeChild(0);
        const QString path = it->data(0, Qt::UserRole).toString();
        static const QStringList skip{QStringLiteral("/proc"), QStringLiteral("/sys"), QStringLiteral("/dev"), QStringLiteral("/run"), QStringLiteral("/tmp"),
                                      QStringLiteral("/snap"), QStringLiteral("/lost+found")};
        for (const QFileInfo &fi : QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name | QDir::IgnoreCase)) {
            if (skip.contains(fi.absoluteFilePath()) || fi.isSymLink()) {
                continue;
            }
            QTreeWidgetItem *c = dirItem(it, fi.fileName(), fi.absoluteFilePath());
            if (it->checkState(0) == Qt::Checked) {
                c->setCheckState(0, Qt::Checked);
                c->setDisabled(true); // included through the parent
            }
        }
    }

    QStringList checkedPaths() const
    {
        QStringList out;
        std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) {
            const QString path = it->data(0, Qt::UserRole).toString();
            if (!path.isEmpty() && it->checkState(0) == Qt::Checked && !it->isDisabled()) {
                out << path;
                return;
            }
            for (int i = 0; i < it->childCount(); ++i) {
                walk(it->child(i));
            }
        };
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            walk(m_tree->topLevelItem(i));
        }
        return out;
    }

    void buildReview()
    {
        QVBoxLayout *v;
        Page *p = addPage(ReviewPage, QStringLiteral("Перевірте ще раз параметри архівації"), v);
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(30);
        m_reviewWhere = new QLabel;
        g->addWidget(new QLabel(QStringLiteral("Розташування архіву:")), 0, 0);
        g->addWidget(m_reviewWhere, 0, 1);
        g->setColumnStretch(1, 1);
        v->addLayout(g);
        v->addWidget(new QLabel(QStringLiteral("Зведення архівації:")));
        m_summary = new QTreeWidget;
        m_summary->setRootIsDecorated(false);
        m_summary->setHeaderLabels({QStringLiteral("Елементи"), QStringLiteral("Включено до резервної копії")});
        m_summary->setColumnWidth(0, 330);
        m_summary->setMinimumHeight(180);
        v->addWidget(m_summary, 1);
        auto *sched = new QHBoxLayout;
        sched->setSpacing(30);
        sched->addWidget(new QLabel(QStringLiteral("Розклад:")));
        m_reviewSchedule = new QLabel;
        sched->addWidget(m_reviewSchedule);
        auto *change = new QLabel(QStringLiteral("<a href=\"#\" style=\"color:#0066cc\">Змінити розклад</a>"));
        sched->addWidget(change);
        sched->addStretch(1);
        v->addLayout(sched);
        v->addStretch(1);
        connect(change, &QLabel::linkActivated, this, [this] {
            Settings copy = m_s;
            ScheduleDialog dlg(this, copy);
            if (dlg.exec() == QDialog::Accepted) {
                m_s = copy;
                fillReview();
            }
        });
        p->setFinalPage(true);
        p->setButtonText(QWizard::FinishButton, QStringLiteral("Зберегти параметри й запустити архівацію"));
        p->enter = [this] {
            fillReview();
        };
    }

    void fillReview()
    {
        m_reviewWhere->setText(m_s.label);
        m_reviewSchedule->setText(m_s.scheduled ? scheduleText(m_s) : QStringLiteral("Відсутній"));
        m_summary->clear();
        if (m_s.automatic) {
            auto *it = new QTreeWidgetItem(m_summary, {QStringLiteral("%1 — Бібліотеки").arg(displayName()), QStringLiteral("Стандартні папки та бібліотеки")});
            it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-users")));
        } else {
            for (const QString &path : std::as_const(m_s.items)) {
                QString name = path;
                for (const Library &l : libraries()) {
                    if (l.path == path) {
                        name = l.title;
                    }
                }
                auto *it = new QTreeWidgetItem(m_summary, {name, QStringLiteral("Усі локальні файли даних")});
                it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-folder")));
            }
            if (m_s.image) {
                auto *it = new QTreeWidgetItem(m_summary, {QStringLiteral("Образ системи"), QStringLiteral("Включено")});
                it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-sysdrive")));
            }
        }
    }

    bool validateCurrentPage() override
    {
        if (!QWizard::validateCurrentPage()) {
            return false;
        }
        if (currentId() == ReviewPage) {
            m_s.configured = true;
            save(m_s);
            applySchedule(m_s);
        }
        return true;
    }

    Settings m_s;
    QTreeWidget *m_where = nullptr;
    QLabel *m_info = nullptr;
    QLabel *m_infoIcon = nullptr;
    QString m_netUrl, m_netUser, m_netPassword, m_netMounted;
    QRadioButton *m_auto = nullptr;
    QRadioButton *m_manual = nullptr;
    QTreeWidget *m_tree = nullptr;
    QCheckBox *m_imageBox = nullptr;
    QLabel *m_reviewWhere = nullptr;
    QLabel *m_reviewSchedule = nullptr;
    QTreeWidget *m_summary = nullptr;
};

/** "Change settings": the same wizard; the schedule alone has its own dialog */
inline bool setupBackup(QWidget *parent)
{
    SetupWizard w(parent);
    if (w.exec() != QDialog::Accepted) {
        return false;
    }
    startBackup();
    return true;
}

/* ---------------- running backup: "View Details" ---------------- */

class DetailsDialog : public QDialog
{
public:
    explicit DetailsDialog(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Архівація даних"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-bk-running")));
        setFixedSize(520, 210);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(16, 14, 16, 12);
        auto *top = new QHBoxLayout;
        top->addWidget(iconLabel(QStringLiteral("seven-bk-running"), 32), 0, Qt::AlignTop);
        m_phase = heading(QStringLiteral("Виконується архівація..."));
        top->addWidget(m_phase, 1);
        v->addLayout(top);
        m_bar = new QProgressBar;
        m_bar->setRange(0, 100);
        m_bar->setTextVisible(false);
        m_bar->setFixedHeight(16);
        v->addWidget(m_bar);
        m_file = new QLabel;
        m_file->setMinimumWidth(10);
        v->addWidget(m_file);
        v->addStretch(1);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *stop = new QPushButton(QStringLiteral("Зупинити архівацію"));
        auto *close = new QPushButton(QStringLiteral("Закрити"));
        for (QPushButton *b : {stop, close}) {
            b->setMinimumWidth(130);
            row->addWidget(b);
        }
        close->setDefault(true);
        v->addLayout(row);
        connect(close, &QPushButton::clicked, this, &QDialog::accept);
        connect(stop, &QPushButton::clicked, this, [this] {
            if (QMessageBox::question(this, QStringLiteral("Архівація даних"),
                                      QStringLiteral("Зупинити архівацію?\n\nУже скопійовані файли цього разу до архіву не потраплять.")) == QMessageBox::Yes) {
                stopBackup();
                accept();
            }
        });
        auto *t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [this] {
            refresh();
        });
        t->start(500);
        refresh();
    }

private:
    void refresh()
    {
        const Status s = status();
        if (!s.running) {
            m_phase->setText(s.phase.isEmpty() ? QStringLiteral("Архівацію завершено") : s.phase);
            m_bar->setValue(100);
            m_file->clear();
            return;
        }
        m_phase->setText(QStringLiteral("Виконується архівація... (%1%)").arg(s.percent));
        m_bar->setValue(s.percent);
        m_file->setText(fontMetrics().elidedText(s.file.isEmpty() ? s.phase : QStringLiteral("Копіювання: ") + s.file, Qt::ElideMiddle, 480));
    }

    QLabel *m_phase;
    QProgressBar *m_bar;
    QLabel *m_file;
};

/* ---------------- "Manage space" ---------------- */

inline void manageSpace(QWidget *parent, const Settings &s, const std::function<void()> &changed)
{
    QString error;
    const QString root = destinationRoot(s, &error);
    if (root.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Керування простором"), error);
        return;
    }
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("Керування місцем на диску для архівації даних"));
    dlg.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cpi-backup")));
    dlg.resize(560, 430);
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(heading(QStringLiteral("Виберіть місце, яке займатимуть архіви")));
    auto *g = new QGridLayout;
    g->setHorizontalSpacing(20);
    const QStorageInfo st(root);
    g->addWidget(new QLabel(QStringLiteral("Розташування архіву:")), 0, 0);
    g->addWidget(new QLabel(s.label), 0, 1);
    g->addWidget(new QLabel(QStringLiteral("Вільне місце:")), 1, 0);
    g->addWidget(new QLabel(QStringLiteral("%1 вільно з %2").arg(gb(st.bytesAvailable()), gb(st.bytesTotal()))), 1, 1);
    g->setColumnStretch(1, 1);
    v->addLayout(g);
    v->addSpacing(6);
    v->addWidget(new QLabel(QStringLiteral("Архіви файлів даних цього комп'ютера:")));
    auto *list = new QTreeWidget;
    list->setRootIsDecorated(false);
    list->setHeaderLabels({QStringLiteral("Період архівації"), QStringLiteral("Комп'ютер")});
    list->setColumnWidth(0, 300);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const BackupSet &b : setsIn(root)) {
        auto *it = new QTreeWidgetItem(list, {dateText(b.when), b.computer});
        it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-folder")));
        it->setData(0, Qt::UserRole, b.path);
    }
    v->addWidget(list, 1);
    v->addWidget(text(QStringLiteral("Незмінені файли спільні для всіх архівів, тому видалення старого архіву звільняє місце лише від файлів, яких більше немає в новіших.")));
    const QList<Snapshot> images = snapshotsIn(root);
    v->addWidget(new QLabel(images.isEmpty() ? QStringLiteral("Образ системи: немає")
                                             : QStringLiteral("Образ системи: %1 (останній від %2)").arg(images.size()).arg(dateText(images.first().when))));
    auto *row = new QHBoxLayout;
    auto *del = new QPushButton(QStringLiteral("Видалити"));
    del->setEnabled(false);
    row->addWidget(del);
    row->addStretch(1);
    auto *close = new QPushButton(QStringLiteral("Закрити"));
    close->setMinimumWidth(90);
    row->addWidget(close);
    v->addLayout(row);
    QObject::connect(list, &QTreeWidget::itemSelectionChanged, &dlg, [list, del] {
        del->setEnabled(!list->selectedItems().isEmpty() && list->selectedItems().size() < list->topLevelItemCount());
    });
    QObject::connect(del, &QPushButton::clicked, &dlg, [&dlg, list, changed] {
        if (QMessageBox::question(&dlg, dlg.windowTitle(), QStringLiteral("Видалити вибрані архіви?")) != QMessageBox::Yes) {
            return;
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        for (QTreeWidgetItem *it : list->selectedItems()) {
            QDir(it->data(0, Qt::UserRole).toString()).removeRecursively();
            delete it;
        }
        QApplication::restoreOverrideCursor();
        changed();
    });
    QObject::connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

/* ---------------- "Restore my files" ---------------- */

class RestoreWizard : public AeroWizard
{
public:
    enum { PickPage, FilesPage, WherePage, DonePage };

    /** pick = true: "Select another backup to restore files from" starts by choosing the backup */
    RestoreWizard(QWidget *parent, bool pick, const QString &root)
        : AeroWizard(parent, QStringLiteral("Відновлення файлів"), QStringLiteral("seven-bk-disc"), QSize(700, 540))
        , m_root(root)
    {
        buildPick();
        buildFiles();
        buildWhere();
        buildDone();
        setStartId(pick ? PickPage : FilesPage);
        if (!pick) {
            const QList<BackupSet> sets = setsIn(root);
            for (const BackupSet &b : sets) {
                if (b.computer == host()) {
                    m_set = b;
                    break;
                }
            }
            if (m_set.path.isEmpty() && !sets.isEmpty()) {
                m_set = sets.first();
            }
        }
    }

private:
    void buildPick()
    {
        QVBoxLayout *v;
        Page *p = addPage(PickPage, QStringLiteral("Виберіть резервну копію, з якої потрібно відновити файли"), v);
        m_sets = new QTreeWidget;
        m_sets->setRootIsDecorated(false);
        m_sets->setHeaderLabels({QStringLiteral("Період архівації"), QStringLiteral("Комп'ютер"), QStringLiteral("Розташування резервної копії")});
        m_sets->setColumnWidth(0, 170);
        m_sets->setColumnWidth(1, 130);
        v->addWidget(m_sets, 1);
        auto *row = new QHBoxLayout;
        auto *refresh = new QPushButton(QStringLiteral("Оновити"));
        auto *browse = new QPushButton(QStringLiteral("Огляд мережевого розташування..."));
        row->addWidget(refresh);
        row->addStretch(1);
        row->addWidget(browse);
        v->addLayout(row);
        auto fill = [this] {
            m_sets->clear();
            QStringList roots;
            for (const Volume &vol : volumes()) {
                if (!vol.mount.isEmpty()) {
                    roots << vol.mount;
                }
            }
            if (!m_extra.isEmpty()) {
                roots << m_extra;
            }
            for (const QString &r : std::as_const(roots)) {
                for (const BackupSet &b : setsIn(r)) {
                    auto *it = new QTreeWidgetItem(m_sets, {dateText(b.when), b.computer, r});
                    it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-drive")));
                    it->setData(0, Qt::UserRole, b.path);
                    it->setData(0, Qt::UserRole + 1, b.when);
                    it->setData(0, Qt::UserRole + 2, b.computer);
                }
            }
        };
        fill();
        connect(refresh, &QPushButton::clicked, this, fill);
        connect(browse, &QPushButton::clicked, this, [this, fill] {
            const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Огляд папок"), QDir::homePath());
            if (!dir.isEmpty()) {
                m_extra = dir;
                fill();
            }
        });
        p->complete = [this] {
            return m_sets->currentItem() != nullptr;
        };
        connect(m_sets, &QTreeWidget::currentItemChanged, p, [p] {
            p->changed();
        });
        p->validate = [this] {
            QTreeWidgetItem *it = m_sets->currentItem();
            m_set = {it->data(0, Qt::UserRole).toString(), it->data(0, Qt::UserRole + 2).toString(), it->data(0, Qt::UserRole + 1).toDateTime()};
            m_root = it->text(2);
            return true;
        };
    }

    void buildFiles()
    {
        QVBoxLayout *v;
        Page *p = addPage(FilesPage, QStringLiteral("Перегляньте або знайдіть у резервній копії файли та папки для відновлення"), v);
        m_dateLine = new QLabel;
        m_dateLine->setTextFormat(Qt::RichText);
        v->addWidget(m_dateLine);
        connect(m_dateLine, &QLabel::linkActivated, this, [this] {
            chooseDate();
        });
        auto *h = new QHBoxLayout;
        m_list = new QTreeWidget;
        m_list->setRootIsDecorated(false);
        m_list->setHeaderLabels({QStringLiteral("Ім'я"), QStringLiteral("У папці"), QStringLiteral("Дата зміни")});
        m_list->setColumnWidth(0, 170);
        m_list->setColumnWidth(1, 210);
        m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_list->setIconSize(QSize(16, 16));
        h->addWidget(m_list, 1);
        auto *buttons = new QVBoxLayout;
        auto *search = new QPushButton(QStringLiteral("Пошук..."));
        auto *files = new QPushButton(QStringLiteral("Огляд файлів"));
        auto *folders = new QPushButton(QStringLiteral("Огляд папок"));
        auto *remove = new QPushButton(QStringLiteral("Видалити"));
        auto *clear = new QPushButton(QStringLiteral("Видалити все"));
        for (QPushButton *b : {search, files, folders}) {
            b->setMinimumWidth(110);
            buttons->addWidget(b);
        }
        buttons->addSpacing(16);
        buttons->addWidget(remove);
        buttons->addWidget(clear);
        buttons->addStretch(1);
        h->addLayout(buttons);
        v->addLayout(h, 1);
        p->enter = [this] {
            updateDateLine();
        };
        p->complete = [this] {
            return m_list->topLevelItemCount() > 0;
        };
        auto add = [this, p](const QString &inSet) {
            if (inSet.isEmpty()) {
                return;
            }
            const QFileInfo fi(inSet);
            const QString original = inSet.mid(m_set.path.size());
            for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
                if (m_list->topLevelItem(i)->data(0, Qt::UserRole).toString() == inSet) {
                    return;
                }
            }
            auto *it = new QTreeWidgetItem(m_list, {fi.fileName(), QFileInfo(original).path(), fi.lastModified().toString(QStringLiteral("dd.MM.yyyy H:mm"))});
            it->setIcon(0, QIcon::fromTheme(fi.isDir() ? QStringLiteral("seven-bk-folder") : QStringLiteral("text-x-generic")));
            it->setData(0, Qt::UserRole, inSet);
            p->changed();
        };
        connect(files, &QPushButton::clicked, this, [this, add] {
            QFileDialog dlg(this, QStringLiteral("Огляд архіву для пошуку файлів"), m_set.path + QDir::homePath());
            dlg.setFileMode(QFileDialog::ExistingFiles);
            dlg.setOption(QFileDialog::DontUseNativeDialog, true); // stays inside the backup
            if (dlg.exec() == QDialog::Accepted) {
                for (const QString &f : dlg.selectedFiles()) {
                    if (f.startsWith(m_set.path)) {
                        add(f);
                    }
                }
            }
        });
        connect(folders, &QPushButton::clicked, this, [this, add] {
            QFileDialog dlg(this, QStringLiteral("Огляд архіву для пошуку папок або дисків"), m_set.path + QDir::homePath());
            dlg.setFileMode(QFileDialog::Directory);
            dlg.setOption(QFileDialog::ShowDirsOnly, true);
            dlg.setOption(QFileDialog::DontUseNativeDialog, true);
            if (dlg.exec() == QDialog::Accepted) {
                for (const QString &f : dlg.selectedFiles()) {
                    if (f.startsWith(m_set.path) && f != m_set.path) {
                        add(f);
                    }
                }
            }
        });
        connect(search, &QPushButton::clicked, this, [this, add] {
            searchDialog(add);
        });
        connect(remove, &QPushButton::clicked, this, [this, p] {
            qDeleteAll(m_list->selectedItems());
            p->changed();
        });
        connect(clear, &QPushButton::clicked, this, [this, p] {
            m_list->clear();
            p->changed();
        });
    }

    void updateDateLine()
    {
        m_dateLine->setText(QStringLiteral("Усі файли буде відновлено з архіву від %1. <a href=\"#\" style=\"color:#0066cc\">Вибрати іншу дату</a>").arg(dateText(m_set.when)));
    }

    void chooseDate()
    {
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Відновлення файлів"));
        dlg.resize(420, 320);
        auto *v = new QVBoxLayout(&dlg);
        v->addWidget(new QLabel(QStringLiteral("Виберіть дату, з якої потрібно відновити файли:")));
        auto *list = new QTreeWidget;
        list->setRootIsDecorated(false);
        list->setHeaderLabels({QStringLiteral("Дата й час"), QStringLiteral("Комп'ютер")});
        list->setColumnWidth(0, 200);
        for (const BackupSet &b : setsIn(m_root)) {
            auto *it = new QTreeWidgetItem(list, {dateText(b.when), b.computer});
            it->setData(0, Qt::UserRole, b.path);
            it->setData(0, Qt::UserRole + 1, b.when);
            if (b.path == m_set.path) {
                list->setCurrentItem(it);
            }
        }
        v->addWidget(list, 1);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        row->addWidget(ok);
        row->addWidget(cancel);
        v->addLayout(row);
        connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
        if (dlg.exec() == QDialog::Accepted && list->currentItem()) {
            m_set.path = list->currentItem()->data(0, Qt::UserRole).toString();
            m_set.when = list->currentItem()->data(0, Qt::UserRole + 1).toDateTime();
            m_set.computer = list->currentItem()->text(1);
            m_list->clear();
            updateDateLine();
        }
    }

    void searchDialog(const std::function<void(const QString &)> &add)
    {
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Пошук файлів для відновлення"));
        dlg.resize(560, 400);
        auto *v = new QVBoxLayout(&dlg);
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(QStringLiteral("Шукати:")));
        auto *edit = new QLineEdit;
        row->addWidget(edit, 1);
        auto *go = new QPushButton(QStringLiteral("Пошук"));
        row->addWidget(go);
        v->addLayout(row);
        auto *results = new QTreeWidget;
        results->setRootIsDecorated(false);
        results->setHeaderLabels({QStringLiteral("Ім'я"), QStringLiteral("У папці"), QStringLiteral("Дата зміни")});
        results->setColumnWidth(0, 170);
        results->setColumnWidth(1, 220);
        v->addWidget(results, 1);
        auto *all = new QCheckBox(QStringLiteral("Вибрати всі"));
        v->addWidget(all);
        auto *bottom = new QHBoxLayout;
        bottom->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        bottom->addWidget(ok);
        bottom->addWidget(cancel);
        v->addLayout(bottom);
        connect(go, &QPushButton::clicked, &dlg, [this, edit, results] {
            results->clear();
            const QString needle = edit->text().trimmed();
            if (needle.isEmpty()) {
                return;
            }
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QDirIterator it(m_set.path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
            int n = 0;
            while (it.hasNext() && n < 2000) {
                const QString f = it.next();
                if (it.fileName().contains(needle, Qt::CaseInsensitive)) {
                    auto *r = new QTreeWidgetItem(results, {it.fileName(), QFileInfo(f.mid(m_set.path.size())).path(),
                                                            it.fileInfo().lastModified().toString(QStringLiteral("dd.MM.yyyy H:mm"))});
                    r->setFlags(r->flags() | Qt::ItemIsUserCheckable);
                    r->setCheckState(0, Qt::Unchecked);
                    r->setData(0, Qt::UserRole, f);
                    ++n;
                }
            }
            QApplication::restoreOverrideCursor();
        });
        connect(edit, &QLineEdit::returnPressed, go, &QPushButton::click);
        connect(all, &QCheckBox::toggled, &dlg, [results](bool on) {
            for (int i = 0; i < results->topLevelItemCount(); ++i) {
                results->topLevelItem(i)->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
            }
        });
        connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
        if (dlg.exec() == QDialog::Accepted) {
            for (int i = 0; i < results->topLevelItemCount(); ++i) {
                if (results->topLevelItem(i)->checkState(0) == Qt::Checked) {
                    add(results->topLevelItem(i)->data(0, Qt::UserRole).toString());
                }
            }
        }
    }

    void buildWhere()
    {
        QVBoxLayout *v;
        Page *p = addPage(WherePage, QStringLiteral("Куди відновити файли?"), v);
        m_original = new QRadioButton(QStringLiteral("У вихідне розташування"));
        m_other = new QRadioButton(QStringLiteral("У таке розташування:"));
        m_original->setChecked(true);
        v->addWidget(m_original);
        v->addWidget(m_other);
        auto *row = new QHBoxLayout;
        row->setContentsMargins(20, 0, 0, 0);
        m_otherPath = new QLineEdit;
        auto *browse = new QPushButton(QStringLiteral("Огляд..."));
        row->addWidget(m_otherPath, 1);
        row->addWidget(browse);
        v->addLayout(row);
        m_subfolders = new QCheckBox(QStringLiteral("Відновлювати файли у вихідні вкладені папки"));
        m_subfolders->setChecked(true);
        m_subfolders->setContentsMargins(20, 0, 0, 0);
        v->addWidget(m_subfolders);
        v->addStretch(1);
        auto enable = [this] {
            m_otherPath->setEnabled(m_other->isChecked());
            m_subfolders->setEnabled(m_other->isChecked());
        };
        enable();
        connect(m_other, &QRadioButton::toggled, this, [enable, p] {
            enable();
            p->changed();
        });
        connect(m_otherPath, &QLineEdit::textChanged, p, [p] {
            p->changed();
        });
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Огляд папок"), QDir::homePath());
            if (!d.isEmpty()) {
                m_other->setChecked(true);
                m_otherPath->setText(d);
            }
        });
        p->complete = [this] {
            return m_original->isChecked() || QFileInfo(m_otherPath->text()).isDir();
        };
        p->setButtonText(QWizard::NextButton, QStringLiteral("Відновити"));
        p->validate = [this] {
            return restore();
        };
    }

    bool restore()
    {
        QStringList items;
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            items << m_list->topLevelItem(i)->data(0, Qt::UserRole).toString();
        }
        // Windows 7 asks what to do with files that already exist
        bool exists = false;
        for (const QString &inSet : std::as_const(items)) {
            exists = exists || QFileInfo::exists(targetFor(inSet));
        }
        bool replace = true;
        if (exists) {
            QMessageBox box(QMessageBox::Question, QStringLiteral("Копіювання файлу"), QStringLiteral("У цьому розташуванні вже є файли з такими самими іменами."),
                            QMessageBox::NoButton, this);
            QPushButton *rep = box.addButton(QStringLiteral("Копіювати й замінити"), QMessageBox::AcceptRole);
            QPushButton *keep = box.addButton(QStringLiteral("Не копіювати"), QMessageBox::RejectRole);
            box.addButton(QStringLiteral("Скасувати"), QMessageBox::DestructiveRole);
            box.exec();
            if (box.clickedButton() == rep) {
                replace = true;
            } else if (box.clickedButton() == keep) {
                replace = false;
            } else {
                return false;
            }
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        int failed = 0;
        for (const QString &inSet : std::as_const(items)) {
            const QString target = targetFor(inSet);
            QDir().mkpath(QFileInfo(target).path());
            QStringList args{QStringLiteral("-a")};
            if (!replace) {
                args << QStringLiteral("--ignore-existing");
            }
            const bool dir = QFileInfo(inSet).isDir();
            args << (dir ? inSet + QLatin1Char('/') : inSet) << (dir ? target + QLatin1Char('/') : target);
            QProcess p;
            p.start(QStringLiteral("rsync"), args);
            while (!p.waitForFinished(100)) {
                QCoreApplication::processEvents();
            }
            failed += p.exitCode() != 0;
        }
        QApplication::restoreOverrideCursor();
        m_restoredTo = m_original->isChecked() ? QFileInfo(targetFor(items.value(0))).path() : m_otherPath->text();
        m_failed = failed;
        return true;
    }

    QString targetFor(const QString &inSet) const
    {
        const QString original = inSet.mid(m_set.path.size());
        if (m_original->isChecked()) {
            return original;
        }
        return m_otherPath->text() + (m_subfolders->isChecked() ? original : QLatin1Char('/') + QFileInfo(original).fileName());
    }

    void buildDone()
    {
        QVBoxLayout *v;
        Page *p = addPage(DonePage, QStringLiteral("Файли відновлено"), v);
        auto *view = new QLabel(QStringLiteral("<a href=\"#\" style=\"color:#0066cc\">Переглянути відновлені файли</a>"));
        auto *result = new QLabel;
        result->setWordWrap(true);
        v->addWidget(result);
        v->addWidget(view);
        v->addStretch(1);
        connect(view, &QLabel::linkActivated, this, [this] {
            launch(QStringLiteral("dolphin"), {m_restoredTo});
        });
        p->enter = [this, p, result] {
            p->setTitle(m_failed ? QStringLiteral("Деякі файли не вдалося відновити") : QStringLiteral("Файли відновлено"));
            result->setText(m_failed ? QStringLiteral("Не вдалося відновити об'єктів: %1. Перевірте, чи достатньо місця на диску.").arg(m_failed) : QString());
        };
        p->setFinalPage(true);
    }

    QString m_root;
    QString m_extra;
    BackupSet m_set;
    QTreeWidget *m_sets = nullptr;
    QLabel *m_dateLine = nullptr;
    QTreeWidget *m_list = nullptr;
    QRadioButton *m_original = nullptr;
    QRadioButton *m_other = nullptr;
    QLineEdit *m_otherPath = nullptr;
    QCheckBox *m_subfolders = nullptr;
    QString m_restoredTo;
    int m_failed = 0;
};

/* ---------------- "Create a system image" ---------------- */

class ImageWizard : public AeroWizard
{
public:
    enum { WherePage, ConfirmPage };

    explicit ImageWizard(QWidget *parent)
        : AeroWizard(parent, QStringLiteral("Створення образу системи"), QStringLiteral("seven-bk-image"), QSize(680, 520))
    {
        QVBoxLayout *v;
        Page *p = addPage(WherePage, QStringLiteral("Де слід зберігати архів?"), v);
        auto *intro = new QLabel(QStringLiteral("Образ системи — це копія дисків, необхідних для роботи системи. Образ системи можна використовувати для відновлення "
                                                "комп'ютера в разі несправності жорсткого диска або комп'ютера, але при цьому не можна вибирати окремі елементи "
                                                "для відновлення. <a href=\"#\" style=\"color:#0066cc\">Як відновити комп'ютер за допомогою образу системи?</a>"));
        intro->setWordWrap(true);
        connect(intro, &QLabel::linkActivated, this, [this] {
            QMessageBox::information(this, windowTitle(),
                                     QStringLiteral("Відкрийте «Панель керування» → «Відновлення» → «Розширені методи відновлення» і виберіть "
                                                    "«Використати образ системи, створений раніше для відновлення комп'ютера». Після перезапуску "
                                                    "системні файли буде повернуто до стану з образу."));
        });
        v->addWidget(intro);
        v->addSpacing(8);
        m_hdd = new QRadioButton(QStringLiteral("На жорсткому диску"));
        m_hdd->setChecked(true);
        v->addWidget(m_hdd);
        m_disks = new QComboBox;
        m_disks->setIconSize(QSize(16, 16));
        m_disks->setFixedWidth(340);
        auto *dl = new QHBoxLayout;
        dl->setContentsMargins(20, 0, 0, 0);
        dl->addWidget(m_disks);
        dl->addStretch(1);
        v->addLayout(dl);
        auto *warnRow = new QHBoxLayout;
        warnRow->setContentsMargins(20, 0, 0, 0);
        m_warnIcon = iconLabel(QStringLiteral("seven-ev-warning"), 16);
        m_warn = new QLabel;
        m_warn->setWordWrap(true);
        warnRow->addWidget(m_warnIcon, 0, Qt::AlignTop);
        warnRow->addWidget(m_warn, 1);
        v->addLayout(warnRow);
        v->addSpacing(12);
        m_dvd = new QRadioButton(QStringLiteral("На DVD-дисках"));
        v->addWidget(m_dvd);
        auto *dvdCombo = new QComboBox;
        dvdCombo->setFixedWidth(340);
        dvdCombo->setEnabled(false);
        const bool hasDvd = QFileInfo::exists(QStringLiteral("/dev/sr0"));
        if (hasDvd) {
            dvdCombo->addItem(QIcon::fromTheme(QStringLiteral("seven-bk-dvd")), QStringLiteral("DVD RW дисковод (sr0)"));
        }
        m_dvd->setEnabled(false); // writing images to DVD is not supported here
        auto *dvl = new QHBoxLayout;
        dvl->setContentsMargins(20, 0, 0, 0);
        dvl->addWidget(dvdCombo);
        dvl->addStretch(1);
        v->addLayout(dvl);
        v->addStretch(1);
        connect(m_disks, &QComboBox::activated, this, [this](int i) {
            if (m_disks->itemData(i).toString() == QLatin1String("refresh")) {
                fill();
            }
        });
        connect(m_disks, &QComboBox::currentIndexChanged, p, [this, p] {
            updateWarning();
            p->changed();
        });
        p->complete = [this] {
            return !m_disks->currentData().toString().isEmpty() && m_disks->currentData().toString() != QLatin1String("refresh");
        };
        p->validate = [this] {
            const Volume vol = current();
            if (!vol.linuxFs()) {
                QMessageBox::warning(this, windowTitle(),
                                     QStringLiteral("Диск «%1» має файлову систему %2. Образ системи можна зберегти лише на диску з файловою системою "
                                                    "Linux (ext4, btrfs або xfs).\n\nВідформатуйте диск у «Керування дисками» або виберіть інший.")
                                         .arg(vol.label, vol.fstype.toUpper()));
                return false;
            }
            return ensureTimeshift(this);
        };
        QVBoxLayout *c;
        Page *cp = addPage(ConfirmPage, QStringLiteral("Підтвердження параметрів архівації"), c);
        c->addWidget(new QLabel(QStringLiteral("Розташування резервної копії:")));
        auto *where = new QHBoxLayout;
        where->setContentsMargins(20, 0, 0, 0);
        where->addWidget(iconLabel(QStringLiteral("seven-bk-drive"), 32));
        m_confirmWhere = new QLabel;
        where->addWidget(m_confirmWhere, 1);
        c->addLayout(where);
        m_need = new QLabel;
        c->addWidget(m_need);
        c->addSpacing(30);
        c->addWidget(new QLabel(QStringLiteral("Буде виконано архівацію таких дисків:")));
        auto *list = new QTreeWidget;
        list->setHeaderHidden(true);
        list->setRootIsDecorated(false);
        list->setIconSize(QSize(32, 32));
        list->setFixedHeight(120);
        auto *sys = new QTreeWidgetItem(list, {QStringLiteral("(/) (система)")});
        sys->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-sysdrive")));
        c->addWidget(list);
        c->addStretch(1);
        cp->setFinalPage(true);
        cp->setButtonText(QWizard::FinishButton, QStringLiteral("Архівувати"));
        cp->enter = [this] {
            const Volume vol = current();
            m_confirmWhere->setText(vol.label);
            // the image holds the system folders; the home folders go to the file backup
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QProcess du;
            du.start(QStringLiteral("du"), {QStringLiteral("-scx"), QStringLiteral("--block-size=1"), QStringLiteral("/usr"), QStringLiteral("/opt"), QStringLiteral("/etc"),
                                            QStringLiteral("/var"), QStringLiteral("/boot"), QStringLiteral("/srv"), QStringLiteral("/root")});
            du.waitForFinished(15000);
            QApplication::restoreOverrideCursor();
            const QStringList lines = QString::fromUtf8(du.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            qint64 need = lines.isEmpty() ? 0 : lines.last().section(QLatin1Char('\t'), 0, 0).toLongLong();
            if (need <= 0) {
                const QStorageInfo root(QStringLiteral("/"));
                need = (root.bytesTotal() - root.bytesFree()) / 4;
            }
            m_need->setText(QStringLiteral("Для архівації може знадобитися близько %1.").arg(gb(need)));
        };
        // "Looking for backup devices..."
        QTimer::singleShot(0, this, [this] {
            fill();
        });
    }

    Volume current() const
    {
        for (const Volume &v : m_vols) {
            if (v.uuid == m_disks->currentData().toString()) {
                return v;
            }
        }
        return {};
    }

private:
    void fill()
    {
        QDialog wait(this);
        wait.setWindowTitle(QStringLiteral("Створення образу системи"));
        wait.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        auto *wv = new QVBoxLayout(&wait);
        auto *frame = new QFrame;
        frame->setObjectName(QStringLiteral("win7bkwait"));
        frame->setStyleSheet(QStringLiteral("#win7bkwait { background: white; border: 1px solid #8e8f8f; }"));
        auto *fv = new QVBoxLayout(frame);
        fv->addWidget(new QLabel(QStringLiteral("Створення образу системи")));
        fv->addWidget(new QLabel(QStringLiteral("Очікування пристроїв архівації...")));
        auto *bar = new QProgressBar;
        bar->setRange(0, 0);
        bar->setTextVisible(false);
        bar->setFixedHeight(14);
        fv->addWidget(bar);
        wv->setContentsMargins(0, 0, 0, 0);
        wv->addWidget(frame);
        wait.setFixedSize(470, 120);
        wait.show();
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 700) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        m_vols = volumes();
        wait.close();
        m_disks->clear();
        for (const Volume &vol : std::as_const(m_vols)) {
            QString mp = vol.mount;
            if (mp.isEmpty() && vol.linuxFs()) {
                mp = mountVolume(vol.uuid);
            }
            const QStorageInfo st(mp);
            const QString free = st.isValid() && !mp.isEmpty() ? QStringLiteral("  Вільно %1").arg(gb(st.bytesAvailable())) : QString();
            m_disks->addItem(QIcon::fromTheme(QStringLiteral("seven-bk-drive")), vol.label + free, vol.uuid);
        }
        m_disks->addItem(QStringLiteral("<Оновити>"), QStringLiteral("refresh"));
        m_disks->setCurrentIndex(m_vols.isEmpty() ? -1 : 0);
        updateWarning();
    }

    void updateWarning()
    {
        const Volume vol = current();
        QString msg;
        if (m_vols.isEmpty()) {
            msg = QStringLiteral("Не знайдено дисків для збереження образу. Підключіть зовнішній жорсткий диск і виберіть «<Оновити>».");
        } else if (vol.sameDiskAsSystem) {
            msg = QStringLiteral("Вибраний диск розташовано на тому самому фізичному диску, архівація якого виконується. У разі збою цього диска архіви буде втрачено.");
        } else if (!vol.uuid.isEmpty() && !vol.linuxFs()) {
            msg = QStringLiteral("Для образу системи потрібна файлова система Linux (ext4).");
        }
        m_warn->setText(msg);
        m_warnIcon->setVisible(!msg.isEmpty());
        m_warn->setVisible(!msg.isEmpty());
    }

    QRadioButton *m_hdd;
    QRadioButton *m_dvd;
    QComboBox *m_disks;
    QLabel *m_warn;
    QLabel *m_warnIcon;
    QLabel *m_confirmWhere;
    QLabel *m_need;
    QList<Volume> m_vols;
};

inline void createSystemImage(QWidget *parent)
{
    ImageWizard w(parent);
    if (w.exec() != QDialog::Accepted) {
        return;
    }
    const Volume vol = w.current();
    runTimeshift(parent, QStringLiteral("Створення образу системи"), QStringLiteral("Виконується збереження архіву..."),
                 {QStringLiteral("--create"), QStringLiteral("--scripted"), QStringLiteral("--comments"), QStringLiteral("Образ системи"), QStringLiteral("--snapshot-device"),
                  vol.dev});
}

/* ---------------- System Restore (rstrui.exe) ---------------- */

/** the classic Windows 7 System Restore wizard: picture strip on the left, < Back / Next > / Cancel */
class SystemRestore : public QDialog
{
public:
    explicit SystemRestore(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Відновлення системи"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-rstrui")));
        setFixedSize(620, 470);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto *body = new QWidget;
        body->setObjectName(QStringLiteral("win7rsbody"));
        body->setAttribute(Qt::WA_StyledBackground, true);
        body->setStyleSheet(QStringLiteral("#win7rsbody { background: white; }"));
        auto *bh = new QHBoxLayout(body);
        bh->setContentsMargins(0, 0, 0, 0);
        bh->setSpacing(0);
        auto *side = new QLabel;
        side->setPixmap(QPixmap(QDir::homePath() + QStringLiteral("/.local/share/seven-control/rstrui-side.png")));
        side->setFixedWidth(164);
        side->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        side->setStyleSheet(QStringLiteral("background: #e8eff8;"));
        bh->addWidget(side);
        m_stack = new QStackedWidget;
        bh->addWidget(m_stack, 1);
        outer->addWidget(body, 1);
        auto *line = new QFrame;
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
        outer->addWidget(line);
        auto *row = new QHBoxLayout;
        row->setContentsMargins(10, 10, 10, 10);
        row->addStretch(1);
        m_back = new QPushButton(QStringLiteral("< Назад"));
        m_next = new QPushButton(QStringLiteral("Далі >"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        for (QPushButton *b : {m_back, m_next, cancel}) {
            b->setMinimumWidth(80);
            row->addWidget(b);
        }
        m_next->setDefault(true);
        outer->addLayout(row);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_back, &QPushButton::clicked, this, [this] {
            go(m_stack->currentIndex() - 1);
        });
        connect(m_next, &QPushButton::clicked, this, [this] {
            next();
        });

        // 1: introduction
        auto *intro = page(QStringLiteral("Відновлення системних файлів і параметрів"));
        intro->addWidget(text(QStringLiteral("Відновлення системи може усунути проблеми, що сповільнюють роботу комп'ютера або блокують її.")));
        intro->addSpacing(14);
        auto *rev = new QLabel(QStringLiteral("Відновлення системи не впливає на документи, зображення або інші особисті дані. Нещодавно встановлені програми й "
                                              "драйвери може бути видалено. <a href=\"#\" style=\"color:#0066cc\">Чи можна скасувати цю дію?</a>"));
        rev->setWordWrap(true);
        connect(rev, &QLabel::linkActivated, this, [this] {
            QMessageBox::information(this, windowTitle(),
                                     QStringLiteral("Так. Перед відновленням автоматично створюється нова точка відновлення, тож систему можна повернути "
                                                    "до поточного стану, ще раз запустивши «Відновлення системи»."));
        });
        intro->addWidget(rev);
        intro->addStretch(1);

        // 2: restore points
        auto *pts = page(QStringLiteral("Відновлення комп'ютера до попереднього стану"));
        pts->addWidget(new QLabel(QStringLiteral("Часовий пояс: %1").arg(QString::fromUtf8(QTimeZone::systemTimeZoneId()))));
        m_points = new QTreeWidget;
        m_points->setRootIsDecorated(false);
        m_points->setHeaderLabels({QStringLiteral("Дата й час"), QStringLiteral("Опис"), QStringLiteral("Тип")});
        m_points->setColumnWidth(0, 130);
        m_points->setColumnWidth(1, 170);
        pts->addWidget(m_points, 1);
        m_empty = new QLabel(QStringLiteral("На системному диску ще немає точок відновлення. <a href=\"#\" style=\"color:#0066cc\">Створити точку відновлення зараз</a>"));
        m_empty->setWordWrap(true);
        connect(m_empty, &QLabel::linkActivated, this, [this] {
            createRestorePoint(this);
            fillPoints();
        });
        pts->addWidget(m_empty);
        connect(m_points, &QTreeWidget::currentItemChanged, this, [this] {
            m_next->setEnabled(m_points->currentItem());
        });

        // 3: confirm
        auto *conf = page(QStringLiteral("Підтвердження точки відновлення"));
        conf->addWidget(new QLabel(QStringLiteral("Комп'ютер буде повернуто до стану, у якому він перебував до події, указаної в полі «Опис».")));
        conf->addSpacing(8);
        auto *g = new QGridLayout;
        g->setHorizontalSpacing(14);
        m_confTime = new QLabel;
        m_confDesc = new QLabel;
        g->addWidget(new QLabel(QStringLiteral("<b>Час:</b>")), 0, 0);
        g->addWidget(m_confTime, 0, 1);
        g->addWidget(new QLabel(QStringLiteral("<b>Опис:</b>")), 1, 0);
        g->addWidget(m_confDesc, 1, 1);
        g->addWidget(new QLabel(QStringLiteral("<b>Диски:</b>")), 2, 0);
        g->addWidget(new QLabel(QStringLiteral("/ (система)")), 2, 1);
        g->setColumnStretch(1, 1);
        conf->addLayout(g);
        conf->addSpacing(14);
        conf->addWidget(text(QStringLiteral("Відновлення системи потребує перезапуску комп'ютера, щоб застосувати зміни. Перед продовженням збережіть усі "
                                            "відкриті файли та закрийте всі програми.")));
        conf->addStretch(1);
        go(0);
    }

    static void createRestorePoint(QWidget *parent)
    {
        if (!ensureTimeshift(parent)) {
            return;
        }
        runTimeshift(parent, QStringLiteral("Захист системи"), QStringLiteral("Створення точки відновлення..."),
                     {QStringLiteral("--create"), QStringLiteral("--scripted"), QStringLiteral("--comments"), QStringLiteral("Точка відновлення, створена вручну"),
                      QStringLiteral("--snapshot-device"), rootDevice()},
                     QStringLiteral("Скасувати"));
    }

private:
    QVBoxLayout *page(const QString &title)
    {
        auto *w = new QWidget;
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(14, 16, 18, 10);
        v->setSpacing(8);
        auto *t = new QLabel(title);
        QFont f = t->font();
        f.setPixelSize(17);
        t->setFont(f);
        t->setStyleSheet(QStringLiteral("color: #1e1e1e;"));
        v->addWidget(t);
        v->addSpacing(18);
        m_stack->addWidget(w);
        return v;
    }

    void fillPoints()
    {
        m_points->clear();
        for (const Snapshot &s : snapshotsIn(QString())) {
            auto *it = new QTreeWidgetItem(m_points, {s.when.toString(QStringLiteral("dd.MM.yyyy H:mm:ss")),
                                                      s.comment.isEmpty() ? QStringLiteral("Автоматична точка відновлення") : s.comment,
                                                      s.comment.isEmpty() ? QStringLiteral("Система") : QStringLiteral("Вручну")});
            it->setData(0, Qt::UserRole, s.name);
        }
        if (m_points->topLevelItemCount()) {
            m_points->setCurrentItem(m_points->topLevelItem(0));
        }
        m_empty->setVisible(m_points->topLevelItemCount() == 0);
        m_next->setEnabled(m_points->currentItem());
    }

    void go(int i)
    {
        m_stack->setCurrentIndex(i);
        m_back->setEnabled(i > 0);
        m_next->setText(i == 2 ? QStringLiteral("Готово") : QStringLiteral("Далі >"));
        m_next->setEnabled(true);
        if (i == 1) {
            fillPoints();
        }
        if (i == 2 && m_points->currentItem()) {
            m_confTime->setText(m_points->currentItem()->text(0));
            m_confDesc->setText(m_points->currentItem()->text(2) + QStringLiteral(": ") + m_points->currentItem()->text(1));
        }
    }

    void next()
    {
        if (m_stack->currentIndex() < 2) {
            go(m_stack->currentIndex() + 1);
            return;
        }
        if (QMessageBox::question(this, windowTitle(),
                                  QStringLiteral("Після запуску відновлення системи його неможливо перервати. Продовжити?"))
            != QMessageBox::Yes) {
            return;
        }
        const QString snap = m_points->currentItem()->data(0, Qt::UserRole).toString();
        hide();
        if (runTimeshift(parentWidget(), QStringLiteral("Відновлення системи"), QStringLiteral("Зачекайте, триває відновлення системних файлів і параметрів..."),
                         {QStringLiteral("--restore"), QStringLiteral("--snapshot"), snap, QStringLiteral("--snapshot-device"), rootDevice(), QStringLiteral("--target-device"),
                          rootDevice(), QStringLiteral("--skip-grub"), QStringLiteral("--scripted"), QStringLiteral("--yes")},
                         QStringLiteral("Скасувати"))) {
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
        }
        accept();
    }

    QStackedWidget *m_stack;
    QPushButton *m_back;
    QPushButton *m_next;
    QTreeWidget *m_points = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_confTime = nullptr;
    QLabel *m_confDesc = nullptr;
};

/* ---------------- re-image the computer ---------------- */

struct Image {
    Volume vol;
    Snapshot snap;
};

inline QList<Image> systemImages()
{
    QList<Image> out;
    for (const Volume &v : volumes()) {
        if (!v.linuxFs()) {
            continue;
        }
        const QString mp = v.mount.isEmpty() ? mountVolume(v.uuid) : v.mount;
        for (const Snapshot &s : snapshotsIn(mp)) {
            out.append({v, s});
        }
    }
    std::sort(out.begin(), out.end(), [](const Image &a, const Image &b) {
        return a.snap.when > b.snap.when;
    });
    return out;
}

/** "Re-image your computer": the latest image or one picked from the list */
inline void reimage(QWidget *parent)
{
    const QList<Image> images = systemImages();
    if (images.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Повторне створення образу комп'ютера"),
                             QStringLiteral("Не вдалося знайти образ системи на цьому комп'ютері.\n\nПідключіть диск з образом системи та повторіть спробу."));
        return;
    }
    AeroWizard w(parent, QStringLiteral("Повторне створення образу комп'ютера"), QStringLiteral("seven-bk-image"), QSize(640, 480));
    QVBoxLayout *v;
    Page *p = w.addPage(0, QStringLiteral("Виберіть резервну копію образу системи"), v);
    v->addWidget(text(QStringLiteral("Комп'ютер буде відновлено за допомогою образу системи. Усі дані на системному диску буде замінено даними з образу.")));
    auto *latest = new QRadioButton(QStringLiteral("Використати останній доступний образ системи (рекомендовано)"));
    auto *pick = new QRadioButton(QStringLiteral("Вибрати образ системи"));
    latest->setChecked(true);
    v->addWidget(latest);
    auto *info = new QLabel(QStringLiteral("Розташування: %1\nДата й час: %2").arg(images.first().vol.label, dateText(images.first().snap.when)));
    info->setContentsMargins(20, 0, 0, 0);
    v->addWidget(info);
    v->addWidget(pick);
    auto *list = new QTreeWidget;
    list->setRootIsDecorated(false);
    list->setHeaderLabels({QStringLiteral("Розташування"), QStringLiteral("Дата й час"), QStringLiteral("Опис")});
    list->setColumnWidth(0, 190);
    list->setColumnWidth(1, 140);
    for (int i = 0; i < images.size(); ++i) {
        auto *it = new QTreeWidgetItem(list, {images.at(i).vol.label, dateText(images.at(i).snap.when), images.at(i).snap.comment});
        it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-bk-drive")));
        it->setData(0, Qt::UserRole, i);
    }
    list->setEnabled(false);
    v->addWidget(list, 1);
    QObject::connect(pick, &QRadioButton::toggled, list, &QWidget::setEnabled);
    QObject::connect(pick, &QRadioButton::toggled, p, [p] {
        p->changed();
    });
    QObject::connect(list, &QTreeWidget::currentItemChanged, p, [p] {
        p->changed();
    });
    p->complete = [latest, list] {
        return latest->isChecked() || list->currentItem();
    };
    p->setFinalPage(true);
    p->setButtonText(QWizard::FinishButton, QStringLiteral("Готово"));
    if (w.exec() != QDialog::Accepted) {
        return;
    }
    const Image img = latest->isChecked() ? images.first() : images.at(list->currentItem()->data(0, Qt::UserRole).toInt());
    if (QMessageBox::warning(parent, QStringLiteral("Повторне створення образу комп'ютера"),
                             QStringLiteral("Усі системні файли буде замінено даними з образу від %1. Після відновлення комп'ютер буде перезапущено.\n\n"
                                            "Продовжити?")
                                 .arg(dateText(img.snap.when)),
                             QMessageBox::Yes | QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    if (runTimeshift(parent, QStringLiteral("Повторне створення образу комп'ютера"), QStringLiteral("Відновлення комп'ютера з образу системи..."),
                     {QStringLiteral("--restore"), QStringLiteral("--snapshot"), img.snap.name, QStringLiteral("--snapshot-device"), img.vol.dev,
                      QStringLiteral("--target-device"), rootDevice(), QStringLiteral("--skip-grub"), QStringLiteral("--scripted"), QStringLiteral("--yes")},
                     QStringLiteral("Скасувати"))) {
        QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot")});
    }
}

/* ---------------- pages ---------------- */

inline QWidget *sidePane(const QList<std::tuple<QString, std::function<void()>, bool>> &links, const QList<std::tuple<QString, std::function<void()>, bool>> &seeAlso,
                         const Nav &nav)
{
    auto *side = new QWidget;
    side->setObjectName(QStringLiteral("win7side"));
    side->setAttribute(Qt::WA_StyledBackground, true);
    side->setFixedWidth(200);
    auto *sv = new QVBoxLayout(side);
    sv->setContentsMargins(4, 16, 10, 14);
    sv->setSpacing(7);
    const QString color = QStringLiteral("#1e3a5f");
    auto addRow = [sv, color](const QString &t, std::function<void()> open, bool shield) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        auto *ic = new QLabel;
        ic->setFixedWidth(18);
        if (shield) {
            ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cp-shield")).pixmap(16, 16));
        }
        row->addWidget(ic, 0, Qt::AlignTop);
        row->addWidget(new Link(t, std::move(open), color), 1);
        sv->addLayout(row);
    };
    addRow(QStringLiteral("Панель керування — домашня сторінка"), nav.home, false);
    sv->addSpacing(9);
    for (const auto &[t, open, shield] : links) {
        addRow(t, open, shield);
    }
    sv->addStretch(1);
    if (!seeAlso.isEmpty()) {
        auto *seeRow = new QHBoxLayout;
        seeRow->setContentsMargins(22, 0, 0, 0);
        seeRow->addWidget(grey(QStringLiteral("Див. також")));
        sv->addLayout(seeRow);
        for (const auto &[t, open, shield] : seeAlso) {
            addRow(t, open, shield);
        }
    }
    return side;
}

inline QWidget *withSide(QWidget *side, QWidget *main)
{
    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("win7page"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    root->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"
                                       "#win7side { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #e3ecf8, stop:0.5 #eef3fb, stop:1 #f3f7fc);"
                                       " border-right: 1px solid #e0e8f3; }"));
    auto *h = new QHBoxLayout(root);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    if (side) {
        h->addWidget(side);
    }
    h->addWidget(main, 1);
    return root;
}

/** the "Backup and Restore" page: not set up, set up, or a backup running */
inline QWidget *buildBackupPage(const Nav &nav)
{
    const Settings s = load();
    const Status st = status();
    auto *main = new QWidget;
    auto *mv = new QVBoxLayout(main);
    mv->setContentsMargins(30, 18, 24, 20);
    mv->setSpacing(0);
    auto *content = new QWidget;
    content->setMaximumWidth(740);
    auto *cv = new QVBoxLayout(content);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(heading(QStringLiteral("Архівація або відновлення файлів")));
    cv->addSpacing(14);
    auto reload = [nav] {
        nav.open(BackupView);
    };

    if (st.running) {
        // the box with the grey bar on the left, like Windows 7 while a backup runs
        auto *box = new QFrame;
        box->setObjectName(QStringLiteral("win7bkrun"));
        box->setStyleSheet(QStringLiteral("#win7bkrun { border: 1px solid #d5dfe5; background: #ffffff; }"));
        auto *bh = new QHBoxLayout(box);
        bh->setContentsMargins(0, 0, 12, 0);
        auto *bar = new QFrame;
        bar->setFixedWidth(26);
        bar->setStyleSheet(QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #9a9a9a, stop:1 #7b7b7b);"));
        bh->addWidget(bar);
        bh->addSpacing(28);
        bh->addWidget(iconLabel(QStringLiteral("seven-bk-running"), 48));
        bh->addSpacing(14);
        auto *bv = new QVBoxLayout;
        bv->setContentsMargins(0, 16, 0, 16);
        auto *phase = new QLabel(QStringLiteral("Виконується архівація..."));
        bv->addWidget(phase);
        auto *progress = new QProgressBar;
        progress->setRange(0, 100);
        progress->setValue(st.percent);
        progress->setTextVisible(false);
        progress->setFixedSize(220, 16);
        bv->addWidget(progress);
        bh->addLayout(bv);
        bh->addStretch(1);
        auto *details = shieldButton(QStringLiteral("Переглянути відомості"));
        bh->addWidget(details);
        cv->addWidget(box);
        cv->addSpacing(12);
        QObject::connect(details, &QPushButton::clicked, main, [main] {
            DetailsDialog(main).exec();
        });
        auto *poll = new QTimer(main);
        QObject::connect(poll, &QTimer::timeout, main, [poll, progress, reload] {
            const Status now = status();
            if (!now.running) {
                poll->stop();
                QTimer::singleShot(0, reload);
                return;
            }
            progress->setValue(now.percent);
        });
        poll->start(700);
    }

    cv->addWidget(new SectionHeader(QStringLiteral("Архівація")));
    cv->addSpacing(8);
    if (!s.configured) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(28, 0, 0, 0);
        row->addWidget(new QLabel(QStringLiteral("Архівацію даних не настроєно.")));
        row->addStretch(1);
        row->addWidget(shieldLink(QStringLiteral("Настроїти резервне копіювання"), [main, reload] {
            if (setupBackup(main)) {
                reload();
            }
        }));
        cv->addLayout(row);
    } else {
        QString root;
        if (!s.network) {
            for (const Volume &v : volumes()) {
                if (v.uuid == s.uuid) {
                    root = v.mount;
                }
            }
        } else {
            root = mountNetwork(s.netUrl, s.netUser, s.netPassword);
        }
        auto *g = new QGridLayout;
        g->setContentsMargins(28, 0, 0, 0);
        g->setHorizontalSpacing(26);
        g->setVerticalSpacing(6);
        int r = 0;
        auto *loc = new QVBoxLayout;
        loc->setSpacing(6);
        loc->addWidget(new QLabel(s.network ? QStringLiteral("Мережевий шлях: ") + s.label : s.label));
        const QStorageInfo info(root);
        if (!root.isEmpty() && info.isValid()) {
            auto *use = new QProgressBar;
            use->setRange(0, 1000);
            use->setValue(int(1000.0 * (info.bytesTotal() - info.bytesAvailable()) / qMax<qint64>(1, info.bytesTotal())));
            use->setTextVisible(false);
            use->setFixedSize(186, 16);
            use->setStyleSheet(QStringLiteral("QProgressBar { border: 1px solid #a5a5a5; background: white; } QProgressBar::chunk { background: #2b9de4; }"));
            loc->addWidget(use);
            loc->addWidget(new QLabel(QStringLiteral("%1 вільно з %2").arg(gb(info.bytesAvailable()), gb(info.bytesTotal()))));
        } else {
            loc->addWidget(new QLabel(QStringLiteral("Розташування архіву зараз недоступне. Підключіть диск.")));
        }
        loc->addSpacing(6);
        loc->addWidget(new QLabel(QStringLiteral("Розмір архіву: %1").arg(s.setSize > 0 ? gb(s.setSize) : QStringLiteral("Недоступно"))));
        loc->addWidget(shieldLink(QStringLiteral("Керування простором"), [main, s, reload] {
            manageSpace(main, s, reload);
        }, !root.isEmpty() && !st.running));
        auto *locWrap = new QHBoxLayout;
        locWrap->addLayout(loc);
        locWrap->addStretch(1);
        auto *backupNow = shieldButton(QStringLiteral("Архівувати"));
        backupNow->setMinimumWidth(180);
        backupNow->setEnabled(!st.running);
        locWrap->addWidget(backupNow, 0, Qt::AlignTop);
        QObject::connect(backupNow, &QPushButton::clicked, main, [reload] {
            startBackup();
            QTimer::singleShot(800, reload);
        });
        auto *leftCol = new QVBoxLayout;
        leftCol->setSpacing(4);
        leftCol->addWidget(new QLabel(QStringLiteral("Розміщення:")));
        leftCol->addSpacing(8);
        leftCol->addWidget(iconLabel(s.network ? QStringLiteral("seven-bk-netdrive") : QStringLiteral("seven-bk-drive"), 48), 0, Qt::AlignHCenter);
        leftCol->addStretch(1);
        g->addLayout(leftCol, r, 0);
        g->addLayout(locWrap, r, 1);
        ++r;
        auto addRow = [g, &r](const QString &k, QWidget *val) {
            auto *key = new QLabel(k);
            key->setWordWrap(true);
            key->setFixedWidth(110);
            g->addWidget(key, r, 0, Qt::AlignTop);
            g->addWidget(val, r, 1, Qt::AlignTop);
            ++r;
        };
        g->setRowMinimumHeight(r++, 14);
        addRow(QStringLiteral("Наступна архівація:"), new QLabel(st.running ? QStringLiteral("Виконується...")
                                                                         : s.scheduled ? dateText(nextRun(s)) : QStringLiteral("Не призначено")));
        QString last = QStringLiteral("Ніколи");
        if (s.lastBackup > 0) {
            last = dateText(QDateTime::fromSecsSinceEpoch(s.lastBackup));
            if (s.lastFailed) {
                last = QStringLiteral("Не вдалося (%1)").arg(last);
            }
        }
        auto *lastLabel = new QLabel(last);
        lastLabel->setWordWrap(true);
        if (s.lastFailed) {
            lastLabel->setToolTip(s.lastError);
        }
        addRow(QStringLiteral("Остання архівація:"), lastLabel);
        auto *what = new QLabel(s.automatic ? QStringLiteral("Файли в бібліотеках і особисті папки користувача")
                                            : s.image ? QStringLiteral("Файли у вибраних папках і образ системи") : QStringLiteral("Файли у вибраних папках"));
        what->setWordWrap(true);
        addRow(QStringLiteral("Вміст:"), what);
        auto *sched = new QWidget;
        auto *schv = new QVBoxLayout(sched);
        schv->setContentsMargins(0, 0, 0, 0);
        schv->setSpacing(4);
        auto *schText = new QLabel(s.scheduled ? scheduleText(s) : QStringLiteral("Відсутній. Виберіть «Архівувати», щоб запустити архівацію вручну."));
        schText->setWordWrap(true);
        schv->addWidget(schText);
        if (!s.scheduled) {
            schv->addWidget(shieldLink(QStringLiteral("Увімкнути розклад"), [main, reload] {
                Settings cur = load();
                ScheduleDialog dlg(main, cur);
                cur.scheduled = true;
                if (dlg.exec() == QDialog::Accepted) {
                    save(cur);
                    applySchedule(cur);
                    reload();
                }
            }));
        }
        schv->addWidget(shieldLink(QStringLiteral("Змінити параметри"), [main, reload] {
            if (setupBackup(main)) {
                reload();
            }
        }, !st.running));
        addRow(QStringLiteral("Розклад:"), sched);
        g->setColumnStretch(1, 1);
        cv->addLayout(g);
    }

    cv->addSpacing(18);
    cv->addWidget(new SectionHeader(QStringLiteral("Відновлення")));
    cv->addSpacing(8);
    auto *rv = new QVBoxLayout;
    rv->setContentsMargins(28, 0, 0, 0);
    rv->setSpacing(8);
    QString root;
    if (s.configured && !s.network) {
        for (const Volume &v : volumes()) {
            if (v.uuid == s.uuid) {
                root = v.mount;
            }
        }
    } else if (s.configured) {
        root = mountNetwork(s.netUrl, s.netUser, s.netPassword);
    }
    const bool haveSets = !root.isEmpty() && !setsIn(root).isEmpty();
    if (haveSets) {
        auto *row = new QHBoxLayout;
        auto *t = text(QStringLiteral("Можна відновити файли користувача з архіву, розташованого в поточному розміщенні."));
        row->addWidget(t, 1);
        auto *restore = new QPushButton(QStringLiteral("Відновити мої файли"));
        restore->setMinimumWidth(180);
        row->addWidget(restore, 0, Qt::AlignTop);
        rv->addLayout(row);
        QObject::connect(restore, &QPushButton::clicked, main, [main, root] {
            RestoreWizard(main, false, root).exec();
        });
    } else {
        rv->addWidget(text(s.configured ? QStringLiteral("Архіви в заданому розташуванні архівів відсутні. Можна відновити файли з іншого розташування.")
                                        : QStringLiteral("Не вдалося знайти архів для цього комп'ютера.")));
    }
    rv->addWidget(shieldLink(QStringLiteral("Вибрати іншу резервну копію для відновлення файлів"), [main] {
        RestoreWizard(main, true, QString()).exec();
    }));
    rv->addSpacing(18);
    rv->addWidget(new Link(QStringLiteral("Відновити системні параметри або комп'ютер"), [nav] {
        nav.open(RecoveryView);
    }));
    cv->addLayout(rv);
    mv->addWidget(content);
    mv->addStretch(1);

    QList<std::tuple<QString, std::function<void()>, bool>> links;
    if (s.configured && s.scheduled) {
        links.append({QStringLiteral("Вимкнути розклад"), [reload] {
                          Settings cur = load();
                          cur.scheduled = false;
                          save(cur);
                          applySchedule(cur);
                          reload();
                      },
                      true});
    }
    links.append({QStringLiteral("Створення образу системи"), [main, reload] {
                      createSystemImage(main);
                      reload();
                  },
                  true});
    return withSide(sidePane(links, {}, nav), main);
}

/** "Recovery": System Restore and the advanced methods */
inline QWidget *buildRecoveryPage(const Nav &nav)
{
    auto *main = new QWidget;
    auto *mv = new QVBoxLayout(main);
    mv->setContentsMargins(30, 18, 24, 20);
    mv->setSpacing(0);
    auto *content = new QWidget;
    content->setMaximumWidth(620);
    auto *cv = new QVBoxLayout(content);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(heading(QStringLiteral("Відновлення раніше збереженого стану цього комп'ютера")));
    cv->addSpacing(10);
    cv->addWidget(text(QStringLiteral("Відновлення системи дає змогу усунути багато неполадок у системі; цей засіб рекомендовано використовувати для усунення "
                                      "неполадок у першу чергу. Для усунення серйозніших неполадок використовуйте розширені методи відновлення.")));
    cv->addSpacing(20);
    cv->addWidget(new SectionHeader(QStringLiteral("Відновлення системи")));
    cv->addSpacing(8);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(12, 0, 0, 0);
    auto *t = text(QStringLiteral("Скасування останніх змін у системі; файли користувача, наприклад документи, зображення та музика, залишаються без змін. "
                                  "Можливе видалення нещодавно встановлених програм і драйверів."));
    t->setMaximumWidth(300);
    row->addWidget(t, 0, Qt::AlignTop);
    row->addStretch(1);
    auto *start = shieldButton(QStringLiteral("Запуск відновлення системи"));
    start->setMinimumWidth(240);
    row->addWidget(start, 0, Qt::AlignTop);
    cv->addLayout(row);
    QObject::connect(start, &QPushButton::clicked, main, [main] {
        if (ensureTimeshift(main)) {
            SystemRestore(main).exec();
        }
    });
    cv->addSpacing(16);
    cv->addWidget(new Link(QStringLiteral("Розширені методи відновлення"), [nav] {
        nav.open(AdvancedView);
    }));
    mv->addWidget(content);
    mv->addStretch(1);
    return withSide(sidePane({{QStringLiteral("Видалення програми"), [nav] {
                                   nav.openOther(30); // Programs and Features
                               },
                               false},
                              {QStringLiteral("Відновлення файлів"), [nav] {
                                   nav.open(BackupView);
                               },
                               false}},
                             {}, nav),
                    main);
}

/** pages without the left pane: a white page with a line and buttons at the bottom */
inline QWidget *flowPage(const QString &title, QWidget *body, const QList<QPushButton *> &buttons, int bodyMaxWidth = 640)
{
    auto *main = new QWidget;
    auto *mv = new QVBoxLayout(main);
    mv->setContentsMargins(0, 0, 0, 0);
    mv->setSpacing(0);
    auto *inner = new QWidget;
    auto *iv = new QVBoxLayout(inner);
    iv->setContentsMargins(106, 22, 40, 14);
    iv->setSpacing(8);
    iv->addWidget(heading(title));
    body->setFixedWidth(bodyMaxWidth);
    // wrapped labels inside nested layouts don't always get their height: reserve it from the known width
    for (QLabel *l : body->findChildren<QLabel *>()) {
        if (l->wordWrap()) {
            l->setMinimumHeight(l->heightForWidth(bodyMaxWidth - 26));
        }
    }
    iv->addWidget(body);
    mv->addWidget(inner);
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: #dcdcdc; margin-left: 0px; margin-right: 0px;"));
    mv->addWidget(line);
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 12, 106, 0);
    row->setSpacing(6);
    row->addStretch(1);
    for (QPushButton *b : buttons) {
        b->setMinimumWidth(96);
        row->addWidget(b);
    }
    mv->addLayout(row);
    mv->addStretch(1);
    return withSide(nullptr, main);
}

/** "Choose an advanced recovery method" */
inline QWidget *buildAdvancedPage(const Nav &nav)
{
    auto *body = new QWidget;
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    auto *intro = new QLabel(QStringLiteral("Після вибору методу у вас буде можливість виконати архівацію файлів, наприклад документів, зображень і музики. "
                                            "<a href=\"#\" style=\"color:#0066cc\">Вибір методу відновлення</a>"));
    intro->setWordWrap(true);
    QObject::connect(intro, &QLabel::linkActivated, body, [body] {
        QMessageBox::information(body, QStringLiteral("Розширені методи відновлення"),
                                 QStringLiteral("Образ системи повертає всі системні файли до стану на момент створення образу. Файли користувачів "
                                                "зберігаються окремо в архіві файлів, тому перед відновленням варто їх заархівувати."));
    });
    v->addWidget(intro);
    v->addSpacing(50);
    auto *opt = new QHBoxLayout;
    opt->setSpacing(6);
    opt->addWidget(iconLabel(QStringLiteral("seven-cp-shield"), 16), 0, Qt::AlignTop);
    auto *ov = new QVBoxLayout;
    ov->setSpacing(2);
    auto *link = new Link(QStringLiteral("Використати образ системи, створений раніше для відновлення комп'ютера"), [nav] {
        nav.open(UserFilesView);
    }, QStringLiteral("#1e3287"));
    QFont f = link->font();
    f.setPixelSize(15);
    link->setFont(f);
    ov->addWidget(link);
    ov->addWidget(text(QStringLiteral("Якщо створено образ системи, його можна використати для заміни всього, що є на системному диску, включно з "
                                      "системою та програмами, даними, збереженими в образі.")));
    opt->addLayout(ov, 1);
    v->addLayout(opt);
    v->addSpacing(30);
    auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
    QObject::connect(cancel, &QPushButton::clicked, body, [nav] {
        nav.open(RecoveryView);
    });
    return flowPage(QStringLiteral("Виберіть розширені методи відновлення"), body, {cancel});
}

/** "Back up your files?" before re-imaging */
inline QWidget *buildUserFilesPage(const Nav &nav)
{
    const Settings s = load();
    QString root;
    if (s.configured) {
        root = s.network ? mountNetwork(s.netUrl, s.netUser, s.netPassword) : QString();
        for (const Volume &v : volumes()) {
            if (!s.network && v.uuid == s.uuid) {
                root = v.mount;
            }
        }
    }
    const bool have = !root.isEmpty() && !setsIn(root).isEmpty();
    auto *body = new QWidget;
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    v->addWidget(text(QStringLiteral("Перед продовженням можна провести архівацію файлів користувача, наприклад документів, зображень і музики, і зберегти "
                                     "архів на зовнішньому жорсткому диску або USB-пристрої флеш-пам'яті.")));
    v->addWidget(text(QStringLiteral("Після завершення процесу відновлення ці файли можна буде відновити на комп'ютері. Якщо пропустити цей крок, файли "
                                     "користувача, змінені після створення образу, можуть бути втрачені.")));
    v->addSpacing(14);
    auto *warn = new QHBoxLayout;
    warn->addWidget(iconLabel(QStringLiteral("seven-ev-warning"), 16), 0, Qt::AlignTop);
    warn->addWidget(text(have ? QStringLiteral("Останній архів файлів створено %1.").arg(dateText(setsIn(root).first().when))
                              : QStringLiteral("Не вдалося знайти на комп'ютері архіви файлів користувача. Для створення архіву файлів натисніть кнопку «Архівувати».")),
                    1);
    v->addLayout(warn);
    v->addSpacing(10);
    auto *backup = new QPushButton(QStringLiteral("Архівувати"));
    auto *skip = new QPushButton(QStringLiteral("Пропустити"));
    QObject::connect(backup, &QPushButton::clicked, body, [body, nav] {
        if (!load().configured && !setupBackup(body)) {
            return;
        }
        if (load().configured && !status().running) {
            startBackup();
        }
        QTimer::singleShot(600, body, [body] {
            DetailsDialog(body).exec();
        });
        g_backupDone = true;
        nav.open(RestartView);
    });
    QObject::connect(skip, &QPushButton::clicked, body, [nav] {
        g_backupDone = false;
        nav.open(RestartView);
    });
    return flowPage(QStringLiteral("Виконати архівацію файлів користувача?"), body, {backup, skip});
}

/** "Restart your computer and continue the recovery" */
inline QWidget *buildRestartPage(const Nav &nav)
{
    auto *body = new QWidget;
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    v->addWidget(text(QStringLiteral("Якщо виконується відновлення системи з резервної копії, за наявності кількох копій можна вибрати потрібну за датою "
                                     "архівації.")));
    if (!g_backupDone) {
        v->addSpacing(14);
        auto *warn = new QHBoxLayout;
        warn->addWidget(iconLabel(QStringLiteral("seven-ev-warning"), 16), 0, Qt::AlignTop);
        auto *t = text(QStringLiteral("<b>Ви не вибрали створення резервної копії файлів. Під час продовження змінені після створення образу системні файли "
                                      "буде замінено.</b>"));
        warn->addWidget(t, 1);
        v->addLayout(warn);
    }
    v->addSpacing(10);
    auto *restart = new QPushButton(QStringLiteral("Перезапустити"));
    auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
    QObject::connect(restart, &QPushButton::clicked, body, [body] {
        if (status().running) {
            QMessageBox::information(body, QStringLiteral("Відновлення"), QStringLiteral("Зачекайте завершення архівації файлів, а потім повторіть спробу."));
            return;
        }
        if (ensureTimeshift(body)) {
            reimage(body);
        }
    });
    QObject::connect(cancel, &QPushButton::clicked, body, [nav] {
        nav.open(RecoveryView);
    });
    return flowPage(QStringLiteral("Перезапустити комп'ютер і продовжити відновлення"), body, {restart, cancel});
}

inline QWidget *build(int view, const Nav &nav)
{
    switch (view) {
    case RecoveryView:
        return buildRecoveryPage(nav);
    case AdvancedView:
        return buildAdvancedPage(nav);
    case UserFilesView:
        return buildUserFilesPage(nav);
    case RestartView:
        return buildRestartPage(nav);
    }
    return buildBackupPage(nav);
}

inline QString viewTitle(int view)
{
    switch (view) {
    case RecoveryView:
        return QStringLiteral("Відновлення");
    case AdvancedView:
        return QStringLiteral("Розширені методи відновлення");
    case UserFilesView:
        return QStringLiteral("Архівація файлів користувача");
    case RestartView:
        return QStringLiteral("Перезапустити");
    }
    return QStringLiteral("Архівація і відновлення");
}

inline bool isBackupView(int v)
{
    return v >= BackupView && v <= RestartView;
}
} // namespace Win7Backup
