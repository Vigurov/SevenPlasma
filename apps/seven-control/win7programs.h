/*
 * "Programs" of the Windows 7 Control Panel: "Programs and Features", "Installed Updates",
 * "Windows Features", "Default Programs", "Set Default Programs", "Set Associations",
 * "Open With" and "Set Program Access and Computer Defaults".
 *
 *   programs          -> packages installed after the system (with a menu entry, or chosen by hand): apt, pacman
 *                        (system = pacman.log's first day) or dnf (system = dnf history's first day),
 *                        snaps (snapd socket), Steam games, Wine programs (registry), AppImages in ~/Applications
 *   updates           -> /var/log/apt/history.log* upgrades and snap refreshes
 *   Windows features  -> optional Kubuntu packages (games, Telnet, SSH server, ...) and Baloo file indexing
 *   default programs  -> mimeapps.list "Default Applications" (KApplicationTrader reads it)
 * Removing and installing packages goes through pkexec + apt-get / pacman / dnf.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7header.h"

#include "win7chrome.h"
#include "win7display.h"

#include <KApplicationTrader>
#include <KConfigGroup>
#include <KDesktopFile>
#include <KService>
#include <KSharedConfig>
#include <KSycoca>

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCollator>
#include <QDateTime>
#include <QDialog>
#include <QDirIterator>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QStringTokenizer>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>

namespace Win7Programs
{
enum {
    ProgramsView = 30,
    UpdatesView = 31,
    DefaultsView = 32,
    SetDefaultsView = 33,
    AssocView = 34,
    ProgramAssocView = 35,
};

struct Nav {
    std::function<void(int)> open;
    std::function<void()> home;
    std::function<void()> back;
};

/** the program "Set Associations for a Program" is about (a KService storage id) */
inline QString &assocProgram()
{
    static QString id;
    return id;
}

/* ---------------- small helpers ---------------- */

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

inline QLabel *wrapped(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    return l;
}

/** "348 КБ", "62,0 МБ", "1,13 ГБ" — three significant digits like Windows 7 */
inline QString sizeText(qint64 bytes)
{
    if (bytes < 0) {
        return QString();
    }
    const QLocale uk;
    if (bytes < 1024 * 1024) {
        return tr7("%1 KB").arg(qMax<qint64>(1, (bytes + 1023) / 1024));
    }
    double v = bytes / (1024.0 * 1024.0);
    QString unit = tr7("MB");
    if (v >= 1000) {
        v /= 1024.0;
        unit = tr7("GB");
    }
    const int decimals = v < 10 ? 2 : v < 100 ? 1 : 0;
    return uk.toString(v, 'f', decimals) + QLatin1Char(' ') + unit;
}

inline QString dateText(const QDate &d)
{
    return d.isValid() ? d.toString(QStringLiteral("dd.MM.yyyy")) : QString();
}

inline QIcon themeIcon(const QString &name, const QString &fallback = QStringLiteral("seven-app-generic"))
{
    if (name.startsWith(QLatin1Char('/'))) {
        QIcon i(name);
        if (!i.isNull()) {
            return i;
        }
    } else if (!name.isEmpty() && QIcon::hasThemeIcon(name)) {
        return QIcon::fromTheme(name);
    }
    return QIcon::fromTheme(fallback);
}

inline QString run(const QString &program, const QStringList &args, int timeout = 15000)
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    p.setProcessEnvironment(env);
    p.start(program, args);
    p.waitForFinished(timeout);
    return QString::fromUtf8(p.readAllStandardOutput());
}

/** GET request to snapd's REST socket, returns "result" */
inline QJsonValue snapd(const QString &path)
{
    QLocalSocket s;
    s.connectToServer(QStringLiteral("/run/snapd.socket"));
    if (!s.waitForConnected(1000)) {
        return {};
    }
    s.write("GET " + path.toUtf8() + " HTTP/1.0\r\nHost: localhost\r\n\r\n");
    s.waitForBytesWritten(1000);
    QByteArray data;
    while (s.waitForReadyRead(3000)) {
        data += s.readAll();
    }
    data += s.readAll();
    const int body = data.indexOf("\r\n\r\n");
    if (body < 0) {
        return {};
    }
    return QJsonDocument::fromJson(data.mid(body + 4)).object().value(QStringLiteral("result"));
}

/* ---------------- apt / dpkg ---------------- */

using Fields = QHash<QString, QString>;

/** one-line fields of a Debian control file ("Package: x" ...), one hash per paragraph */
inline QList<Fields> controlParagraphs(const QByteArray &data)
{
    QList<Fields> out;
    Fields cur;
    for (const QByteArray &raw : data.split('\n')) {
        if (raw.trimmed().isEmpty()) {
            if (!cur.isEmpty()) {
                out.append(cur);
            }
            cur.clear();
            continue;
        }
        if (raw.startsWith(' ') || raw.startsWith('\t')) {
            continue;
        }
        const int c = raw.indexOf(':');
        if (c > 0) {
            cur.insert(QString::fromUtf8(raw.left(c)), QString::fromUtf8(raw.mid(c + 1)).trimmed());
        }
    }
    if (!cur.isEmpty()) {
        out.append(cur);
    }
    return out;
}

inline bool isInstalled(const Fields &f)
{
    return f.value(QStringLiteral("Status")).endsWith(QLatin1String(" installed"));
}

/** installed packages by name */
inline QHash<QString, Fields> dpkgStatus()
{
    QHash<QString, Fields> out;
    QFile f(QStringLiteral("/var/lib/dpkg/status"));
    if (f.open(QIODevice::ReadOnly)) {
        for (const Fields &p : controlParagraphs(f.readAll())) {
            if (isInstalled(p) && !out.contains(p.value(QStringLiteral("Package")))) {
                out.insert(p.value(QStringLiteral("Package")), p);
            }
        }
    }
    return out;
}

/** packages that came with the system (the installer keeps their list) */
inline const QSet<QString> &systemPackages()
{
    static QSet<QString> set = [] {
        QSet<QString> s;
        QProcess p;
        p.start(QStringLiteral("zcat"), {QStringLiteral("/var/log/installer/initial-status.gz")});
        p.waitForFinished(15000);
        for (const Fields &f : controlParagraphs(p.readAllStandardOutput())) {
            if (isInstalled(f)) {
                s.insert(f.value(QStringLiteral("Package")));
            }
        }
        return s;
    }();
    return set;
}

inline QSet<QString> autoPackages()
{
    QSet<QString> s;
    QFile f(QStringLiteral("/var/lib/apt/extended_states"));
    if (f.open(QIODevice::ReadOnly)) {
        for (const Fields &p : controlParagraphs(f.readAll())) {
            if (p.value(QStringLiteral("Auto-Installed")) == QLatin1String("1")) {
                s.insert(p.value(QStringLiteral("Package")));
            }
        }
    }
    return s;
}

inline QString dpkgList(const QString &pkg, const QString &arch)
{
    const QString base = QStringLiteral("/var/lib/dpkg/info/") + pkg;
    if (QFile::exists(base + QStringLiteral(".list"))) {
        return base + QStringLiteral(".list");
    }
    return base + QLatin1Char(':') + arch + QStringLiteral(".list");
}

inline QString publisherOf(QString maintainer)
{
    maintainer = maintainer.section(QLatin1Char('<'), 0, 0).trimmed();
    if (maintainer.contains(QLatin1String("Kubuntu"), Qt::CaseInsensitive)) {
        return QStringLiteral("Kubuntu");
    }
    if (maintainer.contains(QLatin1String("Ubuntu"), Qt::CaseInsensitive)) {
        return QStringLiteral("Ubuntu");
    }
    if (maintainer.contains(QLatin1String("Debian"), Qt::CaseInsensitive)) {
        return QStringLiteral("Debian");
    }
    return maintainer;
}

/** the visible menu entries among a package's files */
inline QList<QString> visibleApps(const QStringList &files)
{
    QList<QString> apps;
    for (const QString &path : files) {
        if (!path.startsWith(QLatin1String("/usr/share/applications/")) || !path.endsWith(QLatin1String(".desktop")) || !QFile::exists(path)) {
            continue;
        }
        KDesktopFile df(path);
        if (df.noDisplay() || df.desktopGroup().readEntry("Hidden", false) || df.readType() != QLatin1String("Application")) {
            continue;
        }
        apps.append(path);
    }
    return apps;
}

/** visible menu entries a package installs (its dpkg file list) */
inline QList<QString> packageApps(const QString &listFile)
{
    QFile f(listFile);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return visibleApps(QString::fromUtf8(f.readAll()).split(QLatin1Char('\n')));
}

/* ---------------- installed programs ---------------- */

struct Program {
    enum Kind { Deb, Snap, Wine, Steam, AppImage };
    Kind kind = Deb;
    QString id; // package, snap, registry key, Steam app id or AppImage path
    QString name;
    QString publisher;
    QString version;
    QString help; // home page
    QString icon;
    QDate installed;
    qint64 size = -1;
    QStringList files; // AppImage: the image and its menu entries
};

/** the program's name and icon: of the menu entry that names the package best */
inline void nameFromApps(Program &p, const QList<QString> &apps)
{
    p.icon = QStringLiteral("seven-app-generic");
    if (apps.isEmpty()) {
        return;
    }
    QString main = apps.first();
    for (const QString &a : apps) {
        if (QFileInfo(a).completeBaseName().contains(p.id.section(QLatin1Char('-'), 0, 0), Qt::CaseInsensitive)) {
            main = a;
            break;
        }
    }
    // the user's own copy of the menu entry wins (~/.local/share/applications)
    const KService::Ptr svc = KService::serviceByStorageId(QFileInfo(main).fileName());
    if (svc) {
        p.name = svc->name();
        p.icon = svc->icon();
    } else {
        KDesktopFile df(main);
        p.name = df.readName();
        p.icon = df.readIcon();
    }
}

/** a package without a menu entry that is a part of something else, not a tool of its own */
inline bool partPackage(const QString &pkg)
{
    static const QStringList prefixes{QStringLiteral("lib"), QStringLiteral("linux"), QStringLiteral("kernel"), QStringLiteral("grub"),
                                      QStringLiteral("python3-"), QStringLiteral("python-"), QStringLiteral("perl-"), QStringLiteral("qt6-"),
                                      QStringLiteral("kf6-"), QStringLiteral("glibc-"), QStringLiteral("language-pack")};
    static const QStringList suffixes{QStringLiteral("-dev"), QStringLiteral("-devel"), QStringLiteral("-dbg"), QStringLiteral("-debuginfo"),
                                      QStringLiteral("-fonts"), QStringLiteral("-firmware"), QStringLiteral("-headers"), QStringLiteral("-libs")};
    for (const QString &x : prefixes) {
        if (pkg.startsWith(x)) {
            return true;
        }
    }
    for (const QString &x : suffixes) {
        if (pkg.endsWith(x)) {
            return true;
        }
    }
    return false;
}

inline QList<Program> debPrograms(const QHash<QString, Fields> &status)
{
    QList<Program> out;
    const QSet<QString> &system = systemPackages();
    const QSet<QString> autos = autoPackages();
    static const QSet<QString> skipSections{QStringLiteral("libs"), QStringLiteral("libdevel"), QStringLiteral("oldlibs"), QStringLiteral("kernel"),
                                            QStringLiteral("metapackages"), QStringLiteral("translations"), QStringLiteral("admin"), QStringLiteral("kde"),
                                            QStringLiteral("fonts"), QStringLiteral("localization"), QStringLiteral("debug")};
    for (auto it = status.cbegin(); it != status.cend(); ++it) {
        const QString &pkg = it.key();
        if (system.contains(pkg)) {
            continue;
        }
        const Fields &f = it.value();
        const QString list = dpkgList(pkg, f.value(QStringLiteral("Architecture")));
        const QList<QString> apps = packageApps(list);
        if (apps.isEmpty()) {
            // a tool chosen by hand (wine, cmake, nodejs...), not a library or part of the system
            const QString section = f.value(QStringLiteral("Section")).section(QLatin1Char('/'), -1);
            if (system.isEmpty() || autos.contains(pkg) || skipSections.contains(section) || pkg.startsWith(QLatin1String("lib"))
                || pkg.endsWith(QLatin1String("-dev")) || pkg.endsWith(QLatin1String("-dbg")) || pkg.startsWith(QLatin1String("linux-"))
                || pkg.startsWith(QLatin1String("grub")) || pkg.startsWith(QLatin1String("language-pack")) || pkg.startsWith(QLatin1String("python3-"))) {
                continue;
            }
        }
        Program p;
        p.kind = Program::Deb;
        p.id = pkg;
        p.name = pkg;
        nameFromApps(p, apps);
        p.publisher = publisherOf(f.value(QStringLiteral("Maintainer")));
        p.version = f.value(QStringLiteral("Version")).section(QLatin1Char(':'), -1);
        p.help = f.value(QStringLiteral("Homepage"));
        p.size = f.value(QStringLiteral("Installed-Size")).toLongLong() * 1024;
        p.installed = QFileInfo(list).lastModified().date();
        out.append(p);
    }
    return out;
}

/* ---------------- pacman (Arch) and rpm / dnf (Fedora) ---------------- */

enum class PackageSystem { Dpkg, Pacman, Rpm };

inline PackageSystem packageSystem()
{
    static const PackageSystem ps = QFile::exists(QStringLiteral("/var/lib/dpkg/status")) ? PackageSystem::Dpkg
        : QFileInfo(QStringLiteral("/var/lib/pacman/local")).isDir()                     ? PackageSystem::Pacman
                                                                                          : PackageSystem::Rpm;
    return ps;
}

/** Arch: the packages of the first day in pacman's log (the system as installed, like Ubuntu's initial-status) */
inline QSet<QString> pacmanFirstDay()
{
    QSet<QString> out;
    QFile f(QStringLiteral("/var/log/pacman.log"));
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    static const QRegularExpression line(QStringLiteral(R"(^\[([^\]]+)\] \[ALPM\] installed (\S+) )"));
    QDateTime first;
    while (!f.atEnd()) {
        const auto m = line.match(QString::fromUtf8(f.readLine()));
        if (!m.hasMatch()) {
            continue;
        }
        const QDateTime t = QDateTime::fromString(m.captured(1), Qt::ISODate);
        if (!first.isValid()) {
            first = t;
        }
        if (t.isValid() && first.secsTo(t) > 86400) {
            break;
        }
        out.insert(m.captured(2));
    }
    return out;
}

/** sections of a pacman local database file (desc, files): %NAME% -> lines */
inline QHash<QString, QStringList> pacmanSections(const QString &path)
{
    QHash<QString, QStringList> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    QString cur;
    for (const QString &l : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        if (l.startsWith(QLatin1Char('%')) && l.endsWith(QLatin1Char('%'))) {
            cur = l;
        } else if (!l.isEmpty() && !cur.isEmpty()) {
            out[cur].append(l);
        }
    }
    return out;
}

inline QList<Program> pacmanPrograms()
{
    QList<Program> out;
    const QSet<QString> system = pacmanFirstDay();
    QDirIterator it(QStringLiteral("/var/lib/pacman/local"), QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QString dir = it.next();
        const QHash<QString, QStringList> desc = pacmanSections(dir + QStringLiteral("/desc"));
        const QString pkg = desc.value(QStringLiteral("%NAME%")).value(0);
        if (pkg.isEmpty() || system.contains(pkg)) {
            continue;
        }
        QStringList files;
        for (const QString &f : pacmanSections(dir + QStringLiteral("/files")).value(QStringLiteral("%FILES%"))) {
            if (f.startsWith(QLatin1String("usr/share/applications/"))) {
                files.append(QLatin1Char('/') + f);
            }
        }
        const QList<QString> apps = visibleApps(files);
        const bool byHand = desc.value(QStringLiteral("%REASON%")).value(0, QStringLiteral("0")) == QLatin1String("0");
        if (apps.isEmpty() && (system.isEmpty() || !byHand || partPackage(pkg))) {
            continue;
        }
        Program p;
        p.kind = Program::Deb;
        p.id = pkg;
        p.name = pkg;
        nameFromApps(p, apps);
        const QString packager = desc.value(QStringLiteral("%PACKAGER%")).value(0);
        p.publisher = packager.contains(QLatin1String("archlinux.org")) ? QStringLiteral("Arch Linux") : packager.section(QLatin1Char('<'), 0, 0).trimmed();
        p.version = desc.value(QStringLiteral("%VERSION%")).value(0).section(QLatin1Char(':'), -1).section(QLatin1Char('-'), 0, 0);
        p.help = desc.value(QStringLiteral("%URL%")).value(0);
        p.size = desc.value(QStringLiteral("%SIZE%")).value(0).toLongLong();
        p.installed = QDateTime::fromSecsSinceEpoch(desc.value(QStringLiteral("%INSTALLDATE%")).value(0).toLongLong()).date();
        out.append(p);
    }
    return out;
}

/** Fedora: the packages the system came with (dnf's history of the first day: the image build) */
inline QSet<QString> rpmFirstDay()
{
    const QString py = QStringLiteral(
        "import sqlite3\n"
        "c = sqlite3.connect('file:/usr/lib/sysimage/libdnf5/transaction_history.sqlite?mode=ro&immutable=1', uri=True)\n"
        "t0 = c.execute('select min(dt_begin) from trans').fetchone()[0]\n"
        "for (n,) in c.execute('select distinct pkg_name.name from trans join trans_item on trans_item.trans_id = trans.id '\n"
        "        'join rpm on rpm.item_id = trans_item.item_id join pkg_name on pkg_name.id = rpm.name_id where trans.dt_begin < ?', (t0 + 86400,)):\n"
        "    print(n)\n");
    const QStringList names = run(QStringLiteral("python3"), {QStringLiteral("-c"), py}).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return QSet<QString>(names.cbegin(), names.cend());
}

/** Fedora: why dnf installed each package (User, Dependency, Weak Dependency, Group, External User) */
inline QHash<QString, QString> rpmReasons()
{
    QHash<QString, QString> out;
    QFile f(QStringLiteral("/usr/lib/sysimage/libdnf5/packages.toml"));
    if (f.open(QIODevice::ReadOnly)) {
        static const QRegularExpression re(QStringLiteral(R"re(^"(.+)\.[^.]+" = \{reason = "([^"]+)"\})re"), QRegularExpression::MultilineOption);
        auto m = re.globalMatch(QString::fromUtf8(f.readAll()));
        while (m.hasNext()) {
            const auto x = m.next();
            out.insert(x.captured(1), x.captured(2));
        }
    }
    return out;
}

/** Fedora: the packages of the groups installed with dnf (a desktop, "KDE Applications"...): part of the system */
inline QSet<QString> rpmGroupPackages()
{
    QSet<QString> out;
    QFile f(QStringLiteral("/usr/lib/sysimage/libdnf5/groups.toml"));
    if (f.open(QIODevice::ReadOnly)) {
        static const QRegularExpression item(QStringLiteral(R"re(^\s+"([^"]+)",?\s*$)re"), QRegularExpression::MultilineOption);
        auto m = item.globalMatch(QString::fromUtf8(f.readAll()));
        while (m.hasNext()) {
            out.insert(m.next().captured(1));
        }
    }
    return out;
}

inline QList<Program> rpmPrograms()
{
    QList<Program> out;
    const QSet<QString> system = rpmFirstDay() + rpmGroupPackages();
    const QHash<QString, QString> reasons = rpmReasons();
    // the packages of the menu entries: every installed file with its package, only the menu entries kept
    QHash<QString, QStringList> entries;
    const QString files = run(QStringLiteral("rpm"), {QStringLiteral("-qa"), QStringLiteral("--qf"), QStringLiteral("[%{FILENAMES}\t%{=NAME}\n]")}, 60000);
    for (const QStringView line : QStringTokenizer(files, QLatin1Char('\n'))) {
        if (line.startsWith(u"/usr/share/applications/") && line.contains(u".desktop\t")) {
            const qsizetype tab = line.lastIndexOf(QLatin1Char('\t'));
            entries[line.mid(tab + 1).toString()].append(line.left(tab).toString());
        }
    }
    const QString all = run(QStringLiteral("rpm"), {QStringLiteral("-qa"), QStringLiteral("--qf"),
                                                    QStringLiteral("%{NAME}\t%{VERSION}\t%{VENDOR}\t%{URL}\t%{INSTALLTIME}\t%{SIZE}\n")}, 30000);
    for (const QString &line : all.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() < 6 || system.contains(f.at(0)) || f.at(0).startsWith(QLatin1String("gpg-pubkey"))) {
            continue;
        }
        const QString &pkg = f.at(0);
        const QString reason = reasons.value(pkg);
        if (reason == QLatin1String("Group")) {
            continue;
        }
        const QList<QString> apps = visibleApps(entries.value(pkg));
        if (apps.isEmpty() && (system.isEmpty() || reason != QLatin1String("User") || partPackage(pkg))) {
            continue;
        }
        Program p;
        p.kind = Program::Deb;
        p.id = pkg;
        p.name = pkg;
        nameFromApps(p, apps);
        p.publisher = f.at(2) == QLatin1String("(none)") ? QString() : f.at(2);
        p.version = f.at(1);
        p.help = f.at(3) == QLatin1String("(none)") ? QString() : f.at(3);
        p.installed = QDateTime::fromSecsSinceEpoch(f.at(4).toLongLong()).date();
        p.size = f.at(5).toLongLong();
        out.append(p);
    }
    return out;
}

inline QList<Program> snapPrograms()
{
    QList<Program> out;
    const QJsonArray snaps = snapd(QStringLiteral("/v2/snaps")).toArray();
    for (const QJsonValue &v : snaps) {
        const QJsonObject s = v.toObject();
        if (s.value(QStringLiteral("type")).toString() != QLatin1String("app") || s.value(QStringLiteral("apps")).toArray().isEmpty()) {
            continue; // bases, themes and other content snaps
        }
        Program p;
        p.kind = Program::Snap;
        p.id = s.value(QStringLiteral("name")).toString();
        p.name = s.value(QStringLiteral("title")).toString();
        if (p.name.isEmpty()) {
            p.name = p.id;
        }
        p.publisher = s.value(QStringLiteral("publisher")).toObject().value(QStringLiteral("display-name")).toString();
        p.version = s.value(QStringLiteral("version")).toString();
        p.help = s.value(QStringLiteral("website")).toString();
        p.size = s.value(QStringLiteral("installed-size")).toVariant().toLongLong();
        p.installed = QDateTime::fromString(s.value(QStringLiteral("install-date")).toString(), Qt::ISODateWithMs).date();
        p.icon = QStringLiteral("seven-app-generic");
        bool hasMenuEntry = false;
        QDir dir(QStringLiteral("/var/lib/snapd/desktop/applications"));
        for (const QString &d : dir.entryList({p.id + QStringLiteral("_*.desktop")})) {
            KDesktopFile df(dir.filePath(d));
            if (!df.noDisplay()) {
                hasMenuEntry = true;
                p.icon = df.readIcon();
                if (s.value(QStringLiteral("title")).toString().isEmpty()) {
                    p.name = df.readName();
                }
                break;
            }
        }
        if (hasMenuEntry) { // helpers like mesa-2404 have none
            out.append(p);
        }
    }
    return out;
}

inline QStringList steamLibraries()
{
    QStringList libs;
    const QString home = QDir::homePath();
    for (const QString &root : {home + QStringLiteral("/.steam/steam"), home + QStringLiteral("/.local/share/Steam")}) {
        QFile f(root + QStringLiteral("/steamapps/libraryfolders.vdf"));
        if (f.open(QIODevice::ReadOnly)) {
            static const QRegularExpression re(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
            auto m = re.globalMatch(QString::fromUtf8(f.readAll()));
            while (m.hasNext()) {
                libs.append(m.next().captured(1));
            }
        }
        libs.append(root);
    }
    QStringList out;
    for (const QString &l : libs) {
        const QString c = QFileInfo(l + QStringLiteral("/steamapps")).canonicalFilePath();
        if (!c.isEmpty() && !out.contains(c)) {
            out.append(c);
        }
    }
    return out;
}

inline QList<Program> steamPrograms()
{
    QList<Program> out;
    for (const QString &lib : steamLibraries()) {
        QDir dir(lib);
        for (const QString &acf : dir.entryList({QStringLiteral("appmanifest_*.acf")})) {
            QFile f(dir.filePath(acf));
            if (!f.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QString text = QString::fromUtf8(f.readAll());
            auto field = [&text](const QString &key) {
                const QRegularExpression re(QStringLiteral("\"%1\"\\s+\"([^\"]*)\"").arg(key));
                return re.match(text).captured(1);
            };
            Program p;
            p.kind = Program::Steam;
            p.id = field(QStringLiteral("appid"));
            p.name = field(QStringLiteral("name"));
            if (p.id.isEmpty() || p.name.isEmpty()) {
                continue;
            }
            p.size = field(QStringLiteral("SizeOnDisk")).toLongLong();
            p.installed = QDateTime::fromSecsSinceEpoch(field(QStringLiteral("LastUpdated")).toLongLong()).date();
            p.icon = QIcon::hasThemeIcon(QStringLiteral("steam_icon_") + p.id) ? QStringLiteral("steam_icon_") + p.id : QStringLiteral("steam");
            p.help = QStringLiteral("https://store.steampowered.com/app/") + p.id;
            out.append(p);
        }
    }
    return out;
}

/** a string value of a Wine .reg file: \\ \" \n and \xNNNN escapes */
inline QString regString(const QString &s)
{
    QString out;
    for (int i = 0; i < s.size(); ++i) {
        if (s.at(i) != QLatin1Char('\\') || i + 1 >= s.size()) {
            out += s.at(i);
            continue;
        }
        const QChar n = s.at(++i);
        if (n == QLatin1Char('x')) {
            int j = i + 1;
            QString hex;
            while (j < s.size() && hex.size() < 4 && QStringLiteral("0123456789abcdefABCDEF").contains(s.at(j))) {
                hex += s.at(j++);
            }
            out += QChar(hex.toUShort(nullptr, 16));
            i = j - 1;
        } else if (n == QLatin1Char('n')) {
            out += QLatin1Char('\n');
        } else {
            out += n;
        }
    }
    return out;
}

inline QList<Program> winePrograms()
{
    QList<Program> out;
    const QString prefix = qEnvironmentVariable("WINEPREFIX", QDir::homePath() + QStringLiteral("/.wine"));
    QFile f(prefix + QStringLiteral("/system.reg"));
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    // menu entries Wine made, for the icons
    QList<QPair<QString, QString>> menu; // name, icon
    QDirIterator it(QDir::homePath() + QStringLiteral("/.local/share/applications/wine"), {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        KDesktopFile df(it.next());
        menu.append({df.readName(), df.readIcon()});
    }
    static const QRegularExpression section(QStringLiteral(R"(^\[Software\\\\(?:Wow6432Node\\\\)?Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\(.+)\])"));
    static const QRegularExpression value(QStringLiteral(R"re(^"([^"]+)"=(.*)$)re"));
    Program cur;
    bool inKey = false;
    bool hidden = false;
    auto flush = [&] {
        if (inKey && !hidden && !cur.name.isEmpty()) {
            cur.icon = QStringLiteral("wine");
            for (const auto &m : std::as_const(menu)) {
                if (cur.name.contains(m.first, Qt::CaseInsensitive) || m.first.contains(cur.name.section(QLatin1Char(' '), 0, 0), Qt::CaseInsensitive)) {
                    cur.icon = m.second;
                    break;
                }
            }
            out.append(cur);
        }
        cur = Program();
        inKey = false;
        hidden = false;
    };
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1Char('['))) {
            flush();
            const auto m = section.match(line);
            if (m.hasMatch()) {
                inKey = true;
                cur.kind = Program::Wine;
                cur.id = regString(m.captured(1));
            }
            continue;
        }
        if (!inKey) {
            continue;
        }
        const auto m = value.match(line);
        if (!m.hasMatch()) {
            continue;
        }
        const QString key = m.captured(1);
        QString v = m.captured(2);
        const bool isString = v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"'));
        if (isString) {
            v = regString(v.mid(1, v.size() - 2));
        }
        if (key == QLatin1String("DisplayName")) {
            cur.name = v;
        } else if (key == QLatin1String("Publisher")) {
            cur.publisher = v;
        } else if (key == QLatin1String("DisplayVersion")) {
            cur.version = v;
        } else if (key == QLatin1String("URLInfoAbout") || (key == QLatin1String("HelpLink") && cur.help.isEmpty())) {
            cur.help = v;
        } else if (key == QLatin1String("InstallDate")) {
            cur.installed = QDate::fromString(v, QStringLiteral("yyyyMMdd"));
        } else if (key == QLatin1String("EstimatedSize") && v.startsWith(QLatin1String("dword:"))) {
            cur.size = v.mid(6).toLongLong(nullptr, 16) * 1024;
        } else if (key == QLatin1String("SystemComponent") && v == QLatin1String("dword:00000001")) {
            hidden = true;
        }
    }
    flush();
    return out;
}

inline QList<Program> appImages()
{
    QList<Program> out;
    QDir dir(QDir::homePath() + QStringLiteral("/Applications"));
    const QString apps = QDir::homePath() + QStringLiteral("/.local/share/applications");
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.AppImage"), QStringLiteral("*.appimage")}, QDir::Files)) {
        Program p;
        p.kind = Program::AppImage;
        p.id = fi.absoluteFilePath();
        p.name = fi.completeBaseName();
        p.icon = QStringLiteral("seven-app-generic");
        p.size = fi.size();
        p.installed = fi.lastModified().date();
        p.files.append(p.id);
        for (const QFileInfo &d : QDir(apps).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files)) {
            KDesktopFile df(d.absoluteFilePath());
            if (df.desktopGroup().readEntry("Exec", QString()).contains(p.id)) {
                p.name = df.readName();
                p.icon = df.readIcon();
                p.files.append(d.absoluteFilePath());
            }
        }
        out.append(p);
    }
    return out;
}

inline QList<Program> installedPrograms()
{
    QList<Program> all;
    switch (packageSystem()) {
    case PackageSystem::Dpkg:
        all = debPrograms(dpkgStatus());
        break;
    case PackageSystem::Pacman:
        all = pacmanPrograms();
        break;
    case PackageSystem::Rpm:
        all = rpmPrograms();
        break;
    }
    all += snapPrograms();
    all += steamPrograms();
    all += winePrograms();
    all += appImages();
    return all;
}

/* ---------------- installed updates ---------------- */

struct Update {
    QString name;
    QString program;
    QString version;
    QString publisher;
    QDate installed;
};

inline QList<Update> installedUpdates(const QList<Program> &programs)
{
    QList<Update> out;
    QHash<QString, QString> debNames; // package -> program name in "Programs and Features"
    for (const Program &p : programs) {
        if (p.kind == Program::Deb) {
            debNames.insert(p.id, p.name);
        }
    }
    const QHash<QString, Fields> status = dpkgStatus();
    QByteArray log;
    QDir dir(QStringLiteral("/var/log/apt"));
    for (const QString &f : dir.entryList({QStringLiteral("history.log*")}, QDir::Files)) {
        if (f.endsWith(QLatin1String(".gz"))) {
            QProcess p;
            p.start(QStringLiteral("zcat"), {dir.filePath(f)});
            p.waitForFinished(15000);
            log += p.readAllStandardOutput() + '\n';
        } else {
            QFile file(dir.filePath(f));
            if (file.open(QIODevice::ReadOnly)) {
                log += file.readAll() + '\n';
            }
        }
    }
    static const QRegularExpression entry(QStringLiteral(R"(([a-z0-9][a-z0-9+.-]*)(?::[a-z0-9]+)? \(([^,()]+), ([^()]+)\))"));
    QDate date;
    for (const QByteArray &raw : log.split('\n')) {
        const QString line = QString::fromUtf8(raw);
        if (line.startsWith(QLatin1String("Start-Date:"))) {
            date = QDate::fromString(line.mid(11).trimmed().left(10), QStringLiteral("yyyy-MM-dd"));
        } else if (line.startsWith(QLatin1String("Upgrade:"))) {
            auto m = entry.globalMatch(line.mid(8));
            while (m.hasNext()) {
                const auto u = m.next();
                const QString pkg = u.captured(1);
                Update up;
                up.program = debNames.value(pkg, QStringLiteral("Kubuntu"));
                up.name = debNames.contains(pkg) ? tr7("Update for %1 (%2)").arg(up.program, pkg) : tr7("Update for Kubuntu (%1)").arg(pkg);
                up.version = u.captured(3).trimmed().section(QLatin1Char(':'), -1);
                up.publisher = status.contains(pkg) ? publisherOf(status.value(pkg).value(QStringLiteral("Maintainer"))) : QStringLiteral("Ubuntu");
                up.installed = date;
                out.append(up);
            }
        }
    }
    QHash<QString, QString> snapNames;
    for (const Program &p : programs) {
        if (p.kind == Program::Snap) {
            snapNames.insert(p.id, p.name);
        }
    }
    const QJsonArray changes = snapd(QStringLiteral("/v2/changes?select=all")).toArray();
    for (const QJsonValue &v : changes) {
        const QJsonObject c = v.toObject();
        const QString kind = c.value(QStringLiteral("kind")).toString();
        if ((kind != QLatin1String("auto-refresh") && kind != QLatin1String("refresh-snap")) || c.value(QStringLiteral("status")).toString() != QLatin1String("Done")) {
            continue;
        }
        for (const QJsonValue &n : c.value(QStringLiteral("data")).toObject().value(QStringLiteral("snap-names")).toArray()) {
            Update up;
            up.program = snapNames.value(n.toString(), n.toString());
            up.name = tr7("Update for %1").arg(up.program);
            up.publisher = QStringLiteral("Snapcraft");
            up.installed = QDateTime::fromString(c.value(QStringLiteral("ready-time")).toString(), Qt::ISODateWithMs).date();
            out.append(up);
        }
    }
    return out;
}

/* ---------------- running apt and friends ---------------- */

/** "Please wait..." box with a marquee bar while a command runs */
class BusyDialog : public QDialog
{
public:
    BusyDialog(QWidget *parent, const QString &title, const QString &icon, const QString &text)
        : QDialog(parent)
    {
        setWindowTitle(title);
        setWindowIcon(QIcon::fromTheme(icon));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        setModal(true);
        setFixedWidth(420);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(14, 14, 14, 14);
        auto *row = new QHBoxLayout;
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(icon).pixmap(32, 32));
        row->addWidget(ic, 0, Qt::AlignTop);
        row->addWidget(wrapped(text), 1);
        v->addLayout(row);
        auto *bar = new QProgressBar;
        bar->setRange(0, 0);
        bar->setTextVisible(false);
        v->addSpacing(6);
        v->addWidget(bar);
    }

protected:
    void reject() override
    {
    } // can't be cancelled half way
};

/** runs a command behind a BusyDialog; done(ok, output). pkexec's "not authorized" counts as cancelled */
inline void runTask(QWidget *parent, const QString &title, const QString &icon, const QString &text, const QString &program, const QStringList &args,
                    std::function<void(bool ok, bool cancelled, const QString &output)> done)
{
    auto *dlg = new BusyDialog(parent, title, icon, text);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    auto *proc = new QProcess(dlg);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(proc, &QProcess::finished, dlg, [dlg, proc, done](int code, QProcess::ExitStatus st) {
        const QString out = QString::fromLocal8Bit(proc->readAll());
        dlg->hide();
        dlg->deleteLater();
        const bool cancelled = code == 126 || code == 127;
        if (done) {
            done(st == QProcess::NormalExit && code == 0, cancelled, out);
        }
    });
    proc->start(program, args);
}

inline bool safePackageName(const QString &p)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9+._-]*(:[a-z0-9]+)?$"));
    return re.match(p).hasMatch();
}

/** what apt-get would remove together with these packages */
inline QStringList removalOf(const QStringList &pkgs)
{
    QStringList out;
    if (packageSystem() == PackageSystem::Pacman) {   // the packages and what only they needed
        return run(QStringLiteral("pacman"), QStringList{QStringLiteral("-Rsp"), QStringLiteral("--print-format"), QStringLiteral("%n")} + pkgs)
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    if (packageSystem() == PackageSystem::Rpm) {   // dnf's table: the package lines under "Removing..."
        bool in = false;
        for (const QString &line : run(QStringLiteral("dnf"), QStringList{QStringLiteral("remove"), QStringLiteral("--assumeno")} + pkgs, 60000).split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("Removing"))) {
                in = true;
            } else if (!line.startsWith(QLatin1Char(' '))) {
                in = false;
            } else if (in && line.simplified().count(QLatin1Char(' ')) >= 3) {
                out.append(line.simplified().section(QLatin1Char(' '), 0, 0));
            }
        }
        return out;
    }
    const QString sim = run(QStringLiteral("apt-get"), QStringList{QStringLiteral("-s"), QStringLiteral("remove")} + pkgs);
    for (const QString &line : sim.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("Remv "))) {
            out.append(line.section(QLatin1Char(' '), 1, 1));
        }
    }
    return out;
}

/** shell script for pkexec: install, then remove */
inline QString aptScript(const QStringList &install, const QStringList &remove)
{
    if (packageSystem() != PackageSystem::Dpkg) {
        const bool arch = packageSystem() == PackageSystem::Pacman;
        QStringList steps{QStringLiteral("export LC_ALL=C")};
        if (!install.isEmpty()) {
            steps.append((arch ? QStringLiteral("pacman -S --noconfirm --needed ") : QStringLiteral("dnf install -y ")) + install.join(QLatin1Char(' ')));
        }
        if (!remove.isEmpty()) {
            steps.append((arch ? QStringLiteral("pacman -Rs --noconfirm ") : QStringLiteral("dnf remove -y ")) + remove.join(QLatin1Char(' ')));
        }
        return steps.join(QStringLiteral(" && "));
    }
    QStringList steps{QStringLiteral("export LC_ALL=C DEBIAN_FRONTEND=noninteractive")};
    if (!install.isEmpty()) {
        steps.append(QStringLiteral("apt-get install -y ") + install.join(QLatin1Char(' ')));
    }
    if (!remove.isEmpty()) {
        steps.append(QStringLiteral("apt-get remove -y ") + remove.join(QLatin1Char(' ')));
    }
    return steps.join(QStringLiteral(" && "));
}

inline void showFailure(QWidget *parent, const QString &title, const QString &text, const QString &output)
{
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
    if (!output.trimmed().isEmpty()) {
        box.setDetailedText(output.right(6000));
    }
    box.exec();
}

/** "Uninstall" of one program; refresh() runs when something may have changed */
inline void uninstall(QWidget *parent, const Program &p, std::function<void()> refresh)
{
    const QString title = tr7("Programs and Features");
    if (p.kind == Program::Wine) {
        // the program's own uninstaller, like on Windows
        auto *proc = new QProcess(parent);
        QObject::connect(proc, &QProcess::finished, parent, [proc, refresh] {
            proc->deleteLater();
            refresh();
        });
        proc->start(QStringLiteral("wine"), {QStringLiteral("uninstaller"), QStringLiteral("--remove"), p.id});
        return;
    }
    if (p.kind == Program::Steam) {
        QProcess::startDetached(QStringLiteral("steam"), {QStringLiteral("steam://uninstall/") + p.id});
        return;
    }
    QMessageBox ask(QMessageBox::Question, title, tr7("Are you sure you want to uninstall %1?").arg(p.name), QMessageBox::Yes | QMessageBox::No, parent);
    ask.button(QMessageBox::Yes)->setText(tr7("Yes"));
    ask.button(QMessageBox::No)->setText(tr7("No"));
    if (ask.exec() != QMessageBox::Yes) {
        return;
    }
    if (p.kind == Program::AppImage) {
        for (const QString &f : p.files) {
            QFile::remove(f);
        }
        refresh();
        return;
    }
    const QString wait = tr7("Please wait while %1 is uninstalled from your computer.").arg(p.name);
    auto finished = [parent, title, p, refresh](bool ok, bool cancelled, const QString &out) {
        if (!ok && !cancelled) {
            showFailure(parent, title, tr7("%1 could not be uninstalled.").arg(p.name), out);
        }
        refresh();
    };
    if (p.kind == Program::Snap) {
        runTask(parent, title, QStringLiteral("seven-appwiz"), wait, QStringLiteral("snap"), {QStringLiteral("remove"), p.id}, finished);
        return;
    }
    if (!safePackageName(p.id)) {
        return;
    }
    QStringList extra = removalOf({p.id});
    extra.removeAll(p.id);
    if (!extra.isEmpty()) {
        QMessageBox more(QMessageBox::Warning, title,
                         tr7("Together with %1, the packages that depend on it will be removed:").arg(p.name), QMessageBox::Yes | QMessageBox::No, parent);
        more.setInformativeText(extra.mid(0, 25).join(QStringLiteral(", ")) + (extra.size() > 25 ? QStringLiteral(" …") : QString())
                                + tr7("\n\nDo you want to continue?"));
        more.button(QMessageBox::Yes)->setText(tr7("Yes"));
        more.button(QMessageBox::No)->setText(tr7("No"));
        if (more.exec() != QMessageBox::Yes) {
            return;
        }
    }
    runTask(parent, title, QStringLiteral("seven-appwiz"), wait, QStringLiteral("pkexec"), {QStringLiteral("/bin/sh"), QStringLiteral("-c"), aptScript({}, {p.id})}, finished);
}

/* ---------------- list widget of Programs and Features / Installed Updates ---------------- */

enum Roles {
    SortRole = Qt::UserRole + 1,
    IndexRole,
    GroupRole,
};

class SortItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override
    {
        const int c = treeWidget() ? treeWidget()->sortColumn() : 0;
        const QVariant a = data(c, SortRole);
        const QVariant b = other.data(c, SortRole);
        if (a.isValid() && b.isValid()) {
            return QVariant::compare(a, b) == QPartialOrdering::Less;
        }
        static QCollator coll(QLocale(QLocale::English));
        return coll.compare(text(c), other.text(c)) < 0;
    }
};

/** group rows ("Kubuntu (58)") in blue with a line and a chevron, like Windows 7 */
class GroupDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        if (!idx.data(GroupRole).toBool()) {
            QStyledItemDelegate::paint(p, opt, idx);
            return;
        }
        if (idx.column() != 0) {
            return;
        }
        p->save();
        const auto *view = qobject_cast<const QTreeView *>(opt.widget);
        const QRect r(opt.rect.left(), opt.rect.top(), view ? view->viewport()->width() - opt.rect.left() : opt.rect.width(), opt.rect.height());
        if (opt.state & QStyle::State_MouseOver) {
            drawHover(*p, QRectF(r).adjusted(1, 1, -2, -1), false);
        }
        QFont f = opt.font;
        p->setFont(f);
        p->setPen(QColor(0x1e, 0x39, 0x87));
        const QString text = idx.data(Qt::DisplayRole).toString();
        const QRect tr = r.adjusted(6, 0, -30, 0);
        p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, text);
        const int x = tr.left() + QFontMetrics(f).horizontalAdvance(text) + 8;
        const qreal y = r.center().y() + 1.5;
        const int lineEnd = idx.model()->hasChildren(idx) ? r.right() - 26 : r.right() - 6;
        QLinearGradient g(x, 0, lineEnd, 0);
        g.setColorAt(0, QColor(0xe2, 0xe2, 0xe2));
        g.setColorAt(1, QColor(0xe2, 0xe2, 0xe2));
        p->setPen(QPen(QBrush(g), 1));
        p->drawLine(QPointF(x, y), QPointF(lineEnd, y));
        if (!idx.model()->hasChildren(idx)) { // a plain heading ("Протоколи")
            p->restore();
            return;
        }
        // chevron: up when open
        const bool open = view && view->isExpanded(idx);
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor(0x4d, 0x6b, 0x9c), 1.6));
        const qreal cx = r.right() - 14, cy = r.center().y() + 0.5;
        if (open) {
            p->drawPolyline(QPolygonF({QPointF(cx - 4, cy + 2), QPointF(cx, cy - 2), QPointF(cx + 4, cy + 2)}));
        } else {
            p->drawPolyline(QPolygonF({QPointF(cx - 4, cy - 2), QPointF(cx, cy + 2), QPointF(cx + 4, cy - 2)}));
        }
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        s.setHeight(idx.data(GroupRole).toBool() ? 24 : qMax(s.height(), 20));
        return s;
    }
};

/** details list with the faint column lines of Windows 7 Explorer */
class DetailsTree : public QTreeWidget
{
public:
    DetailsTree()
    {
        Win7HeaderView::install(this); // Explorer's header pictures
        setRootIsDecorated(false);
        setIndentation(0);
        setUniformRowHeights(false);
        setAllColumnsShowFocus(true);
        setFrameShape(QFrame::NoFrame);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setMouseTracking(true);
        setItemDelegate(new GroupDelegate(this));
        header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        header()->setStretchLastSection(false);
        header()->setSectionsMovable(false);
        connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) {
            if (it->data(0, GroupRole).toBool()) {
                it->setExpanded(!it->isExpanded());
            }
        });
    }

protected:
    void drawRow(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        QTreeWidget::drawRow(p, opt, idx);
        // Windows 7: the hot / selected box covers the column lines
        const bool hot = (opt.state & QStyle::State_MouseOver) || selectionModel()->isSelected(idx);
        if (!idx.data(GroupRole).toBool() && !hot) {
            columnLines(p, opt.rect.top(), opt.rect.bottom());
        }
    }
    void paintEvent(QPaintEvent *e) override
    {
        QTreeWidget::paintEvent(e);
        // the lines go on below the last row
        int bottom = 0;
        QTreeWidgetItem *last = nullptr;
        for (QTreeWidgetItem *it = topLevelItemCount() ? topLevelItem(topLevelItemCount() - 1) : nullptr; it;) {
            last = it;
            it = (it->isExpanded() && it->childCount()) ? it->child(it->childCount() - 1) : nullptr;
        }
        if (last) {
            bottom = visualItemRect(last).bottom() + 1;
        }
        if (bottom < viewport()->height()) {
            QPainter p(viewport());
            columnLines(&p, bottom, viewport()->height());
        }
    }

private:
    void columnLines(QPainter *p, int top, int bottom) const
    {
        p->save();
        p->setPen(QColor(0xe8, 0xf1, 0xfb)); // Windows 7 (measured on Programs and Features)
        for (int c = 0; c < columnCount(); ++c) {
            if (isColumnHidden(c)) {
                continue;
            }
            const int x = header()->sectionViewportPosition(c) + header()->sectionSize(c) - 1;
            p->drawLine(x, top, x, bottom);
        }
        p->restore();
    }
};

/** the light blue strip above the list ("Organize ▾  Uninstall") */
inline QToolBar *commandBar()
{
    auto *bar = new QToolBar;
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bar->setContextMenuPolicy(Qt::PreventContextMenu);
    return bar;
}

/** the small dark "▾" of Windows 7 command bars (7x4, written once to the cache) */
inline QString win7CmdArrow()
{
    static const QString path = [] {
        const QString f = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/seven-cmd-arrow.png");
        if (!QFile::exists(f)) {
            QDir().mkpath(QFileInfo(f).path());
            QImage img(7, 4, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            for (int i = 0; i < 4; ++i) {
                for (int x = i; x < 7 - i; ++x) {
                    img.setPixel(x, i, qRgb(0x1e, 0x1e, 0x28));
                }
            }
            img.save(f);
        }
        return f;
    }();
    return path;
}

inline QToolButton *menuButton(const QString &text, QMenu *menu)
{
    auto *b = new QToolButton;
    b->setText(text);
    b->setPopupMode(QToolButton::InstantPopup);
    b->setMenu(menu);
    // Windows 7: "Organize ▾" with the arrow 7 px after the text
    b->setStyleSheet(QStringLiteral("QToolButton { padding-left: 9px; padding-right: 18px; }"
                                    "QToolButton::menu-indicator { image: url(%1); subcontrol-origin: padding; subcontrol-position: right center; right: 9px; width: 7px; height: 4px; }")
                         .arg(win7CmdArrow()));
    return b;
}

/** The view button (picture, then its own "▾") and the help button at the right end of a command bar */
inline void addViewAndHelp(QToolBar *bar, QMenu *viewMenu, const QString &viewTip)
{
    // Windows 7: the picture and its "▾" are two parts of one split button (measured: arrow 13 px after the picture)
    auto *viewBox = new QWidget;
    auto *vh = new QHBoxLayout(viewBox);
    vh->setContentsMargins(0, 0, 0, 0);
    vh->setSpacing(0);
    auto *viewBtn = new QToolButton;
    viewBtn->setIcon(QIcon::fromTheme(QStringLiteral("seven-view-details"), QIcon::fromTheme(QStringLiteral("view-list-details"))));
    viewBtn->setIconSize(QSize(16, 16));
    viewBtn->setToolTip(viewTip);
    viewBtn->setFixedWidth(28);
    QObject::connect(viewBtn, &QToolButton::clicked, viewMenu, [viewMenu, viewBtn] {
        // a click on the picture moves on to the next view, like Windows 7
        const QList<QAction *> acts = viewMenu->actions();
        for (int i = 0; i < acts.size(); ++i) {
            if (acts.at(i)->isChecked()) {
                acts.at((i + 1) % acts.size())->trigger();
                return;
            }
        }
        Q_UNUSED(viewBtn)
    });
    struct ArrowButton : QToolButton {
        void paintEvent(QPaintEvent *e) override
        {
            QToolButton::paintEvent(e);
            QPainter p(this);
            p.drawPixmap((width() - 7) / 2, (height() - 4) / 2 + 1, QPixmap(win7CmdArrow()));
        }
    };
    auto *arrowBtn = new ArrowButton;
    arrowBtn->setToolTip(tr7("Other settings"));
    arrowBtn->setFixedWidth(18);
    arrowBtn->setPopupMode(QToolButton::InstantPopup);
    arrowBtn->setMenu(viewMenu);
    arrowBtn->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0px; }"));
    vh->addWidget(viewBtn);
    vh->addWidget(arrowBtn);
    bar->addWidget(viewBox);
    auto *help = new QToolButton;
    help->setIcon(QIcon(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-aero/help.png"))));
    help->setIconSize(QSize(16, 16));
    help->setToolTip(tr7("Help"));
    QObject::connect(help, &QToolButton::clicked, help, [] { QProcess::startDetached(QStringLiteral("khelpcenter"), {}); });
    bar->addWidget(help);
}

/** "Programs and Features" (Uninstall or change a program) and "Installed Updates" */
class ProgramsPage : public QWidget
{
public:
    ProgramsPage(bool updates, Nav nav)
        : m_updates(updates)
        , m_nav(std::move(nav))
    {
        auto *side = Win7Display::sidePane(m_nav.home,
                                           updates ? QList<Win7Display::SideItem>{{tr7("Uninstall a program"), [this] { m_nav.open(ProgramsView); }},
                                                                                  {tr7("Turn Windows features on or off"), [this] { openFeatures(); }, {}, true}}
                                                   : QList<Win7Display::SideItem>{{tr7("View installed updates"), [this] { m_nav.open(UpdatesView); }},
                                                                                  {tr7("Turn Windows features on or off"), [this] { openFeatures(); }, {}, true}},
                                           {});
        auto *content = new QWidget;
        auto *v = new QVBoxLayout(content);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        auto *head = new QWidget;
        auto *hv = new QVBoxLayout(head);
        hv->setContentsMargins(32, 22, 30, 16);
        hv->setSpacing(14);
        hv->addWidget(pageTitle(updates ? tr7("Uninstall an update") : tr7("Uninstall or change a program")));
        hv->addWidget(wrapped(updates ? tr7("To uninstall an update, select it from the list and then click Uninstall or Change.")
                                      : tr7("To uninstall a program, select it from the list and then click Uninstall, Change, or Repair.")));
        v->addWidget(head);

        // Windows 7: a light line between the page and the command bar (measured #d2ddec)
        auto *barLine = new QWidget;
        barLine->setFixedHeight(1);
        barLine->setAutoFillBackground(true);
        barLine->setStyleSheet(QStringLiteral("background: #d2ddec;"));
        v->addWidget(barLine);
        m_bar = commandBar();
        auto *organize = new QMenu(this);
        m_organizeRemove = organize->addAction(tr7("Delete"), this, [this] { removeSelected(); });
        organize->addSeparator();
        auto *layoutMenu = organize->addMenu(tr7("Layout"));
        m_detailsToggle = layoutMenu->addAction(tr7("Details pane"));
        m_detailsToggle->setCheckable(true);
        m_detailsToggle->setChecked(!updates);
        connect(m_detailsToggle, &QAction::toggled, this, [this](bool on) { m_details->setVisible(on); });
        m_bar->addWidget(menuButton(tr7("Organize"), organize));
        m_remove = m_bar->addAction(tr7("Delete"), this, [this] { removeSelected(); });
        m_remove->setToolTip(updates ? tr7("Uninstall an update.") : tr7("Uninstall a program."));
        auto *spacer = new QWidget;
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_bar->addWidget(spacer);
        auto *viewMenu = new QMenu(this);
        auto *viewGroup = new QActionGroup(viewMenu);
        auto *table = viewMenu->addAction(tr7("Details"), this, [this] { setListMode(false); });
        auto *list = viewMenu->addAction(tr7("List"), this, [this] { setListMode(true); });
        for (QAction *a : {table, list}) {
            a->setCheckable(true);
            viewGroup->addAction(a);
        }
        table->setChecked(true);
        addViewAndHelp(m_bar, viewMenu, tr7("Change your view."));
        v->addWidget(m_bar);

        m_tree = new DetailsTree;
        if (updates) {
            m_tree->setHeaderLabels({tr7("Name"), tr7("Application"), tr7("Version"), tr7("Publisher"), tr7("Installed On")});
            for (int c : {280, 120, 110, 130, 85}) {
                m_tree->setColumnWidth(m_widths++, c);
            }
        } else {
            m_tree->setHeaderLabels({tr7("Name"), tr7("Publisher"), tr7("Installed On"), tr7("Size"), tr7("Version")});
            for (int c : {270, 170, 85, 70, 120}) {
                m_tree->setColumnWidth(m_widths++, c);
            }
        }
        m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            QTreeWidgetItem *it = m_tree->itemAt(pos);
            if (!it || it->data(0, GroupRole).toBool() || m_updates) {
                return;
            }
            QMenu menu;
            menu.addAction(tr7("Delete"), this, [this] { removeSelected(); });
            menu.exec(m_tree->viewport()->mapToGlobal(pos));
        });
        connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] { selectionChanged(); });
        connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
            if (!m_updates && it && !it->data(0, GroupRole).toBool()) {
                removeSelected();
            }
        });
        // Windows 7: a short window shrinks the list (it scrolls), the page itself never scrolls
        m_tree->setMinimumHeight(40);
        m_tree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        v->addWidget(m_tree, 1);

        m_details = new QWidget;
        m_details->setObjectName(QStringLiteral("win7details"));
        m_details->setAttribute(Qt::WA_StyledBackground, true);
        m_details->setStyleSheet(QStringLiteral("#win7details { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #e9eff8);"
                                                " border-top: 1px solid #d5dfe9; }"));
        m_details->setFixedHeight(72);
        auto *dh = new QHBoxLayout(m_details);
        dh->setContentsMargins(14, 8, 14, 8);
        dh->setSpacing(12);
        m_detailsIcon = new QLabel;
        m_detailsIcon->setFixedSize(48, 48);
        dh->addWidget(m_detailsIcon);
        m_detailsText = new QLabel;
        m_detailsText->setTextFormat(Qt::RichText);
        m_detailsText->setOpenExternalLinks(true);
        dh->addWidget(m_detailsText, 1);
        m_details->setVisible(!updates);
        v->addWidget(m_details);

        auto *root = Win7Display::pageWithSide(side, content);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addWidget(root);
        m_tree->addTopLevelItem(new QTreeWidgetItem({tr7("Please wait...")}));
        QTimer::singleShot(30, this, [this] { reload(); });
    }

    void setFilter(const QString &text)
    {
        m_filter = text;
        fill();
    }

    void reload()
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_programs = installedPrograms();
        if (m_updates) {
            m_updateList = installedUpdates(m_programs);
        }
        QApplication::restoreOverrideCursor();
        fill();
    }

private:
    void openFeatures();

    void setListMode(bool list)
    {
        for (int c = 1; c < m_tree->columnCount(); ++c) {
            m_tree->setColumnHidden(c, list);
        }
        m_tree->setHeaderHidden(list);
    }

    void fill()
    {
        m_tree->setSortingEnabled(false);
        m_tree->clear();
        const auto match = [this](const QString &s) {
            return m_filter.isEmpty() || s.contains(m_filter, Qt::CaseInsensitive);
        };
        if (m_updates) {
            QMap<QString, QTreeWidgetItem *> groups;
            for (int i = 0; i < m_updateList.size(); ++i) {
                const Update &u = m_updateList.at(i);
                if (!match(u.name) && !match(u.program)) {
                    continue;
                }
                QTreeWidgetItem *g = groups.value(u.program);
                if (!g) {
                    g = new SortItem(m_tree);
                    g->setData(0, GroupRole, true);
                    g->setFirstColumnSpanned(true);
                    g->setFlags(Qt::ItemIsEnabled);
                    groups.insert(u.program, g);
                }
                auto *it = new SortItem(g, {u.name, u.program, u.version, u.publisher, dateText(u.installed)});
                it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-app-generic")));
                it->setData(4, SortRole, u.installed);
                it->setData(0, IndexRole, i);
            }
            for (auto it = groups.begin(); it != groups.end(); ++it) {
                it.value()->setText(0, QStringLiteral("%1 (%2)").arg(it.key()).arg(it.value()->childCount()));
                it.value()->setData(0, SortRole, it.key().toLower());
                it.value()->setExpanded(true);
            }
            m_tree->setSortingEnabled(true);
            m_tree->sortItems(4, Qt::DescendingOrder);
        } else {
            for (int i = 0; i < m_programs.size(); ++i) {
                const Program &p = m_programs.at(i);
                if (!match(p.name) && !match(p.publisher)) {
                    continue;
                }
                auto *it = new SortItem(m_tree, {p.name, p.publisher, dateText(p.installed), sizeText(p.size), p.version});
                it->setIcon(0, themeIcon(p.icon));
                it->setData(2, SortRole, p.installed);
                it->setData(3, SortRole, p.size);
                it->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
                it->setData(0, IndexRole, i);
                it->setToolTip(0, p.name);
            }
            m_tree->setSortingEnabled(true);
            m_tree->sortItems(0, Qt::AscendingOrder);
        }
        selectionChanged();
    }

    const Program *selected() const
    {
        const auto sel = m_tree->selectedItems();
        if (sel.isEmpty() || sel.first()->data(0, GroupRole).toBool() || !sel.first()->data(0, IndexRole).isValid()) {
            return nullptr;
        }
        const int i = sel.first()->data(0, IndexRole).toInt();
        return m_updates || i >= m_programs.size() ? nullptr : &m_programs.at(i);
    }

    void selectionChanged()
    {
        const Program *p = selected();
        m_remove->setVisible(p != nullptr);
        m_organizeRemove->setEnabled(p != nullptr);
        if (m_updates) {
            return;
        }
        auto grey = [](const QString &s) {
            return QStringLiteral("<span style='color:#5a677a'>%1</span>").arg(s.toHtmlEscaped());
        };
        if (!p) {
            qint64 total = 0;
            for (const Program &q : std::as_const(m_programs)) {
                total += qMax<qint64>(0, q.size);
            }
            m_detailsIcon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-appwiz")).pixmap(48, 48));
            m_detailsText->setText(tr7("<span style='font-size:15px'>Currently installed programs</span>&nbsp;&nbsp;%1 %2<br>%3 %4")
                                       .arg(grey(tr7("Total size:")), sizeText(total), grey(tr7("Programs installed:")))
                                       .arg(m_programs.size()));
            return;
        }
        m_detailsIcon->setPixmap(themeIcon(p->icon).pixmap(48, 48));
        QString text = QStringLiteral("<span style='font-size:15px'>%1</span>").arg((p->publisher.isEmpty() ? p->name : p->publisher).toHtmlEscaped());
        if (!p->version.isEmpty()) {
            text += QStringLiteral("&nbsp;&nbsp;%1 %2").arg(grey(tr7("Product version:")), p->version.toHtmlEscaped());
        }
        QStringList second;
        if (p->size >= 0) {
            second.append(grey(tr7("Size:")) + QLatin1Char(' ') + sizeText(p->size));
        }
        if (!p->help.isEmpty()) {
            second.append(grey(tr7("Help link:")) + QStringLiteral(" <a href='%1' style='color:#0066cc; text-decoration:none'>%1</a>").arg(p->help.toHtmlEscaped()));
        }
        if (!second.isEmpty()) {
            text += QStringLiteral("<br>") + second.join(QStringLiteral("&nbsp;&nbsp;&nbsp;"));
        }
        m_detailsText->setText(text);
    }

    void removeSelected()
    {
        const Program *p = selected();
        if (!p) {
            return;
        }
        const Program copy = *p;
        QPointer<ProgramsPage> self(this);
        uninstall(window(), copy, [self] {
            if (self) {
                self->reload();
            }
        });
    }

    bool m_updates;
    Nav m_nav;
    int m_widths = 0;
    QString m_filter;
    QList<Program> m_programs;
    QList<Update> m_updateList;
    QToolBar *m_bar;
    QAction *m_remove;
    QAction *m_organizeRemove;
    QAction *m_detailsToggle;
    DetailsTree *m_tree;
    QWidget *m_details;
    QLabel *m_detailsIcon;
    QLabel *m_detailsText;
};

/* ---------------- "Windows Features" ---------------- */

struct Feature {
    QString title;
    QString tip;
    QStringList packages; // all of them installed = on
    QList<Feature> children;
    bool baloo = false; // "Windows Search" is Baloo file indexing
};

/** the name of a Kubuntu package on Arch / Fedora ("" = none there) */
inline QString nativePackage(const QString &deb)
{
    if (packageSystem() == PackageSystem::Dpkg) {
        return deb;
    }
    const bool arch = packageSystem() == PackageSystem::Pacman;
    static const QHash<QString, QPair<QString, QString>> names{
        {QStringLiteral("tftp-hpa"), {QStringLiteral("tftp-hpa"), QStringLiteral("tftp")}},
        {QStringLiteral("inetutils-telnetd"), {QStringLiteral("inetutils"), QStringLiteral("telnet-server")}},
        {QStringLiteral("telnet"), {QStringLiteral("inetutils"), QStringLiteral("telnet")}},
        {QStringLiteral("openssh-server"), {QStringLiteral("openssh"), QStringLiteral("openssh-server")}},
        {QStringLiteral("printer-driver-cups-pdf"), {QStringLiteral("cups-pdf"), QStringLiteral("cups-pdf")}},
        {QStringLiteral("nfs-common"), {QStringLiteral("nfs-utils"), QStringLiteral("nfs-utils")}},
        {QStringLiteral("mono-runtime"), {QStringLiteral("mono"), QStringLiteral("mono-core")}},
        {QStringLiteral("default-jre"), {QStringLiteral("jre-openjdk"), QStringLiteral("java-latest-openjdk")}},
    };
    const auto it = names.constFind(deb);
    return it == names.cend() ? deb : arch ? it->first : it->second;
}

inline QList<Feature> baseFeatures()
{
    return {
        {tr7("Games"), tr7("Games that come with the system."), {},
         {{tr7("Solitaire games (Solitaire, Spider, FreeCell)"), tr7("KPatience - over ten card solitaire games."), {QStringLiteral("kpat")}, {}},
          {tr7("Minesweeper"), tr7("KMines - the classic Minesweeper game."), {QStringLiteral("kmines")}, {}},
          {tr7("Mahjong"), tr7("KMahjongg - Mahjong tile solitaire."), {QStringLiteral("kmahjongg")}, {}},
          {tr7("Chess"), tr7("Knights - chess against the computer or another player."), {QStringLiteral("knights")}, {}},
          {tr7("Reversi"), tr7("KReversi - the Reversi game."), {QStringLiteral("kreversi")}, {}},
          {tr7("Sudoku"), tr7("KSudoku - sudoku puzzles."), {QStringLiteral("ksudoku")}, {}}}},
        {tr7("Telnet Client"), tr7("Lets you connect to remote computers with the Telnet protocol."), {QStringLiteral("telnet")}, {}},
        {tr7("TFTP Client"), tr7("Transfers files with the TFTP protocol."), {QStringLiteral("tftp-hpa")}, {}},
        {tr7("Telnet Server"), tr7("Lets other computers connect to this computer with the Telnet protocol."), {QStringLiteral("inetutils-telnetd")}, {}},
        {tr7("SSH Server"), tr7("Remote command line access to this computer with the SSH protocol."), {QStringLiteral("openssh-server")}, {}},
        {tr7("Media Features"), tr7("Programs for playing music and video."), {},
         {{tr7("Elisa Music Player"), tr7("Plays music and manages the music library."), {QStringLiteral("elisa")}, {}},
          {tr7("Haruna Video Player"), tr7("Plays video files and DVDs."), {QStringLiteral("haruna")}, {}}}},
        {tr7("Print and Document Services"), tr7("Printing, printer management and printing to PDF."), {},
         {{tr7("CUPS Print Service"), tr7("Manages printers and the print queue."), {QStringLiteral("cups")}, {}},
          {tr7("Print to PDF"), tr7("A virtual printer that saves documents to PDF files."), {QStringLiteral("printer-driver-cups-pdf")}, {}}}},
        {tr7("Services for NFS"), tr7("Access to shared folders on NFS networks."), {},
         {{tr7("Client for NFS"), tr7("Connects to NFS shared folders."), {QStringLiteral("nfs-common")}, {}}}},
        {tr7("File Sharing (SMB)"), tr7("Lets you share the folders of this computer with other computers on the network."), {QStringLiteral("samba")}, {}},
        {tr7("Windows Search"), tr7("Indexes files for fast search in the Start menu and Explorer."), {}, {}, true},
        {tr7("Virtual PC"), tr7("Creates and runs virtual machines."), {QStringLiteral("virt-manager")}, {}},
        {QStringLiteral("Microsoft .NET Framework (Mono)"), tr7("A runtime for .NET programs."), {QStringLiteral("mono-runtime")}, {}},
        {tr7("Java Runtime Environment"), tr7("Runs programs written in Java."), {QStringLiteral("default-jre")}, {}},
        {tr7("Subsystem for Windows programs (Wine)"), tr7("Runs programs made for Windows."), {QStringLiteral("wine")}, {}},
        {tr7("Tablet PC Components"), tr7("On-screen keyboard for typing without a physical keyboard."), {},
         {{tr7("On-Screen Keyboard"), tr7("Onboard - a keyboard on the screen."), {QStringLiteral("onboard")}, {}}}},
    };
}

inline QList<Feature> features()
{
    QList<Feature> list = baseFeatures();
    std::function<void(QList<Feature> &)> native = [&native](QList<Feature> &l) {
        for (Feature &f : l) {
            for (QString &p : f.packages) {
                p = nativePackage(p);
            }
            native(f.children);
        }
    };
    native(list);
    return list;
}

inline bool balooEnabled()
{
    // the config may say nothing while Baloo is off, so ask Baloo itself
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    p.setProcessEnvironment(env);
    p.setProcessChannelMode(QProcess::MergedChannels); // it reports on stderr
    p.start(QStringLiteral("balooctl6"), {QStringLiteral("status")});
    p.waitForFinished(5000);
    return !p.readAll().contains("disabled");
}

/** SysTreeView32 of Windows 7 dialogs: +/- boxes without lines, 18 px rows, no hover, only the text selected */
class Win7ClassicTree : public QTreeWidget
{
public:
    Win7ClassicTree()
    {
        setIndentation(19);
        setRootIsDecorated(true);
        setAllColumnsShowFocus(false);
        setMouseTracking(false);
        setAttribute(Qt::WA_Hover, false);
        viewport()->setAttribute(Qt::WA_Hover, false);
        setStyleSheet(QStringLiteral("QTreeView { show-decoration-selected: 0; outline: 0; background: #ffffff; border: 1px solid #828790; }"
                                     "QTreeView::item { height: 18px; padding: 0px; border: none; color: black; }"
                                     "QTreeView::item:hover { background: transparent; border: none; }"
                                     "QTreeView::item:selected { background: #3399ff; color: white; }"
                                     "QTreeView::item:selected:!active { background: #f0f0f0; color: black; }"
                                     "QTreeView::branch { background: transparent; border-image: none; image: none; }"));
    }

protected:
    void drawBranches(QPainter *p, const QRect &rect, const QModelIndex &index) const override
    {
        if (!model()->hasChildren(index)) {
            return;
        }
        // the Windows 7 box: 9x9, grey frame, white to light grey, a dark + or -
        const QRect box(rect.right() - indentation() + (indentation() - 9) / 2 + 1, rect.center().y() - 4, 9, 9);
        p->save();
        QLinearGradient g(box.topLeft(), box.bottomRight());
        g.setColorAt(0, Qt::white);
        g.setColorAt(1, QColor(0xc6, 0xc6, 0xc6));
        p->fillRect(box.adjusted(1, 1, -1, -1), g);
        p->setPen(QColor(0x91, 0x91, 0x91));
        p->drawRect(box.adjusted(0, 0, -1, -1));
        p->setPen(QColor(0x2a, 0x2a, 0x2a));
        p->drawLine(box.left() + 2, box.center().y(), box.right() - 2, box.center().y());
        if (!isExpanded(index)) {
            p->drawLine(box.center().x(), box.top() + 2, box.center().x(), box.bottom() - 2);
        }
        p->restore();
    }
};

class FeaturesDialog : public QDialog
{
public:
    explicit FeaturesDialog(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(tr7("Windows Features"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-optfeatures")));
        setAttribute(Qt::WA_DeleteOnClose);
        resize(434 - 16, 374 - 38); // Windows 7: 434x374 with the frame (measured)
        // Windows 7: one grey (#f0f0f0) dialog, the title in blue with the help button on its right
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(13, 12, 11, 11);
        v->setSpacing(0);
        auto *top = new QWidget;
        auto *tv = new QVBoxLayout(top);
        tv->setContentsMargins(0, 0, 0, 0);
        tv->setSpacing(0);
        auto *titleRow = new QHBoxLayout;
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->addWidget(pageTitle(tr7("Turn Windows features on or off")), 1);
        auto *help = new QLabel;
        help->setPixmap(QPixmap(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-aero/help.png"))));
        help->setCursor(Qt::PointingHandCursor);
        help->setToolTip(tr7("Help"));
        titleRow->addWidget(help, 0, Qt::AlignTop);
        tv->addLayout(titleRow);
        tv->addSpacing(12);
        tv->addWidget(wrapped(tr7("To turn a feature on, select its check box. To turn a feature off, clear its check box. A filled box means that only part of the feature is turned on.")));
        tv->addSpacing(6);
        m_stack = new QStackedWidget;
        m_tree = new Win7ClassicTree;
        m_tree->setHeaderHidden(true);
        m_tree->setIconSize(QSize(16, 16));
        m_tree->setUniformRowHeights(true);
        m_tree->addTopLevelItem(new QTreeWidgetItem({tr7("Please wait...")}));
        m_stack->addWidget(m_tree);
        auto *wait = new QWidget;
        auto *wv = new QVBoxLayout(wait);
        wv->setContentsMargins(0, 30, 0, 0);
        wv->addWidget(wrapped(tr7("Please wait while Windows makes changes to features. This might take several minutes.")));
        auto *bar = new QProgressBar;
        bar->setRange(0, 0);
        bar->setTextVisible(false);
        wv->addWidget(bar);
        wv->addStretch(1);
        m_stack->addWidget(wait);
        tv->addWidget(m_stack, 1);
        v->addWidget(top, 1);
        auto *bh = new QHBoxLayout;
        bh->setContentsMargins(0, 10, 0, 0);
        bh->setSpacing(8);
        bh->addStretch(1);
        m_ok = new QPushButton(QStringLiteral("OK"));
        m_cancel = new QPushButton(tr7("Cancel"));
        for (QPushButton *b : {m_ok, m_cancel}) {
            b->setMinimumWidth(75);
            b->setFixedHeight(23);
            bh->addWidget(b);
        }
        m_ok->setDefault(true);
        m_ok->setEnabled(false);
        v->addLayout(bh);
        connect(m_ok, &QPushButton::clicked, this, [this] { apply(); });
        connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
        QTimer::singleShot(30, this, [this] { load(); });
    }

    std::function<void()> changed;

private:
    void load()
    {
        QStringList pkgs;
        std::function<void(const QList<Feature> &)> collect = [&](const QList<Feature> &list) {
            for (const Feature &f : list) {
                pkgs += f.packages;
                collect(f.children);
            }
        };
        const QList<Feature> all = features();
        collect(all);
        m_baloo = balooEnabled();
        auto *proc = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        proc->setProcessEnvironment(env);
        if (packageSystem() != PackageSystem::Dpkg) {
            // "I name" for an installed package, "A name" for one the repositories have
            connect(proc, &QProcess::finished, this, [this, proc, all] {
                for (const QString &line : QString::fromUtf8(proc->readAllStandardOutput()).split(QLatin1Char('\n'))) {
                    if (line.startsWith(QLatin1String("I "))) {
                        m_installed.insert(line.mid(2).trimmed());
                    } else if (line.startsWith(QLatin1String("A "))) {
                        m_available.insert(line.mid(2).trimmed());
                    }
                }
                proc->deleteLater();
                build(all);
            });
            const QString sh = packageSystem() == PackageSystem::Pacman
                ? QStringLiteral("pacman -Qq \"$@\" 2>/dev/null | sed 's/^/I /'; pacman -Si \"$@\" 2>/dev/null | awk '/^Name/{print \"A\", $3}'")
                : QStringLiteral("rpm -q --qf 'I %{NAME}\\n' \"$@\" | grep '^I '; dnf repoquery --qf 'A %{name}\\n' \"$@\" 2>/dev/null");
            proc->start(QStringLiteral("sh"), QStringList{QStringLiteral("-c"), sh, QStringLiteral("sh")} + pkgs);
            return;
        }
        connect(proc, &QProcess::finished, this, [this, proc, all] {
            QString pkg;
            for (const QString &line : QString::fromUtf8(proc->readAllStandardOutput()).split(QLatin1Char('\n'))) {
                if (!line.startsWith(QLatin1Char(' ')) && line.endsWith(QLatin1Char(':'))) {
                    pkg = line.chopped(1).section(QLatin1Char(':'), 0, 0);
                } else if (line.trimmed().startsWith(QLatin1String("Installed:"))) {
                    if (!line.contains(QLatin1String("(none)"))) {
                        m_installed.insert(pkg);
                    }
                } else if (line.trimmed().startsWith(QLatin1String("Candidate:"))) {
                    if (!line.contains(QLatin1String("(none)"))) {
                        m_available.insert(pkg);
                    }
                }
            }
            proc->deleteLater();
            build(all);
        });
        proc->start(QStringLiteral("apt-cache"), QStringList{QStringLiteral("policy")} + pkgs);
    }

    bool shown(const Feature &f) const
    {
        if (f.baloo) {
            return true;
        }
        for (const QString &p : f.packages) {
            if (!m_available.contains(p) && !m_installed.contains(p)) {
                return false;
            }
        }
        if (f.packages.isEmpty()) {
            for (const Feature &c : f.children) {
                if (shown(c)) {
                    return true;
                }
            }
            return false;
        }
        return true;
    }

    bool on(const Feature &f) const
    {
        if (f.baloo) {
            return m_baloo;
        }
        for (const QString &p : f.packages) {
            if (!m_installed.contains(p)) {
                return false;
            }
        }
        return true;
    }

    void build(const QList<Feature> &all)
    {
        m_tree->clear();
        QList<Feature> list = all;
        QCollator coll(QLocale(QLocale::English));
        std::sort(list.begin(), list.end(), [&coll](const Feature &a, const Feature &b) {
            return coll.compare(a.title, b.title) < 0;
        });
        const QIcon folder = QIcon::fromTheme(QStringLiteral("folder"));
        std::function<void(QTreeWidgetItem *, const Feature &)> add = [&](QTreeWidgetItem *parent, const Feature &f) {
            if (!shown(f)) {
                return;
            }
            auto *it = parent ? new QTreeWidgetItem(parent, {f.title}) : new QTreeWidgetItem(m_tree, {f.title});
            it->setIcon(0, folder);
            it->setToolTip(0, f.tip);
            if (f.children.isEmpty()) {
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
                it->setCheckState(0, on(f) ? Qt::Checked : Qt::Unchecked);
                it->setData(0, Qt::UserRole, f.packages);
                it->setData(0, Qt::UserRole + 1, f.baloo);
                it->setData(0, Qt::UserRole + 2, on(f));
            } else {
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
                for (const Feature &c : f.children) {
                    add(it, c);
                }
            }
        };
        for (const Feature &f : list) {
            add(nullptr, f);
        }
        // Windows 7 opens with the first feature selected and the list focused
        if (m_tree->topLevelItemCount() > 0) {
            m_tree->setCurrentItem(m_tree->topLevelItem(0));
        }
        m_tree->setFocus();
        m_ok->setEnabled(true);
    }

    void apply()
    {
        QStringList install, remove;
        int baloo = -1;
        QTreeWidgetItemIterator it(m_tree);
        for (; *it; ++it) {
            QTreeWidgetItem *item = *it;
            if (item->childCount() > 0 || !item->data(0, Qt::UserRole + 2).isValid()) {
                continue;
            }
            const bool want = item->checkState(0) == Qt::Checked;
            if (want == item->data(0, Qt::UserRole + 2).toBool()) {
                continue;
            }
            if (item->data(0, Qt::UserRole + 1).toBool()) {
                baloo = want ? 1 : 0;
                continue;
            }
            for (const QString &p : item->data(0, Qt::UserRole).toStringList()) {
                if (!safePackageName(p)) {
                    continue;
                }
                if (want && !m_installed.contains(p)) {
                    install.append(p);
                } else if (!want && m_installed.contains(p)) {
                    remove.append(p);
                }
            }
        }
        if (baloo >= 0) {
            QProcess::startDetached(QStringLiteral("balooctl6"), {baloo ? QStringLiteral("enable") : QStringLiteral("disable")});
        }
        if (install.isEmpty() && remove.isEmpty()) {
            accept();
            return;
        }
        if (!remove.isEmpty()) {
            QStringList extra = removalOf(remove);
            for (const QString &p : std::as_const(remove)) {
                extra.removeAll(p);
            }
            if (!extra.isEmpty()) {
                QMessageBox more(QMessageBox::Warning, windowTitle(), tr7("Together with the features turned off, the packages that depend on them will be removed:"),
                                 QMessageBox::Yes | QMessageBox::No, this);
                more.setInformativeText(extra.mid(0, 25).join(QStringLiteral(", ")) + (extra.size() > 25 ? QStringLiteral(" …") : QString())
                                        + tr7("\n\nDo you want to continue?"));
                more.button(QMessageBox::Yes)->setText(tr7("Yes"));
                more.button(QMessageBox::No)->setText(tr7("No"));
                if (more.exec() != QMessageBox::Yes) {
                    return;
                }
            }
        }
        m_stack->setCurrentIndex(1);
        m_ok->setEnabled(false);
        m_cancel->setEnabled(false);
        auto *proc = new QProcess(this);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus st) {
            const QString out = QString::fromLocal8Bit(proc->readAll());
            if (changed) {
                changed();
            }
            if (st == QProcess::NormalExit && code == 0) {
                accept();
                return;
            }
            if (code != 126 && code != 127) {
                showFailure(this, windowTitle(), tr7("Windows couldn't change some features."), out);
            }
            m_stack->setCurrentIndex(0);
            m_cancel->setEnabled(true);
            m_installed.clear();
            m_ok->setEnabled(false);
            load();
        });
        proc->start(QStringLiteral("pkexec"), {QStringLiteral("/bin/sh"), QStringLiteral("-c"), aptScript(install, remove)});
    }

    QStackedWidget *m_stack;
    QTreeWidget *m_tree;
    QPushButton *m_ok;
    QPushButton *m_cancel;
    QSet<QString> m_installed;
    QSet<QString> m_available;
    bool m_baloo = false;
};

inline void ProgramsPage::openFeatures()
{
    auto *dlg = new FeaturesDialog(window());
    QPointer<ProgramsPage> self(this);
    dlg->changed = [self] {
        if (self) {
            self->reload();
        }
    };
    dlg->show();
}

inline void showFeatures(QWidget *parent = nullptr)
{
    (new FeaturesDialog(parent))->show();
}

/* ---------------- default programs: data ---------------- */

inline KSharedConfig::Ptr mimeApps()
{
    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("mimeapps.list"), KConfig::NoGlobals, QStandardPaths::GenericConfigLocation);
    cfg->reparseConfiguration();
    return cfg;
}

/** makes `storageId` the default for these MIME types (one write for all of them) */
inline void setDefaults(const QStringList &mimes, const QString &storageId)
{
    KSharedConfig::Ptr cfg = mimeApps();
    KConfigGroup def(cfg, QStringLiteral("Default Applications"));
    KConfigGroup added(cfg, QStringLiteral("Added Associations"));
    for (const QString &m : mimes) {
        def.writeXdgListEntry(m, {storageId});
        QStringList list = added.readXdgListEntry(m);
        list.removeAll(storageId);
        list.prepend(storageId);
        added.writeXdgListEntry(m, list);
    }
    cfg->sync();
    KSycoca::self()->ensureCacheValid();
}

/** back to what the system chose */
inline void clearDefaults(const QStringList &mimes)
{
    KSharedConfig::Ptr cfg = mimeApps();
    KConfigGroup def(cfg, QStringLiteral("Default Applications"));
    for (const QString &m : mimes) {
        def.deleteEntry(m);
    }
    cfg->sync();
    KSycoca::self()->ensureCacheValid();
}

inline bool isDefaultFor(const KService::Ptr &s, const QString &mime)
{
    const KService::Ptr cur = KApplicationTrader::preferredService(mime);
    return cur && cur->storageId() == s->storageId();
}

inline QList<KService::Ptr> defaultablePrograms()
{
    QList<KService::Ptr> list = KApplicationTrader::query([](const KService::Ptr &s) {
        return !s->noDisplay() && !s->mimeTypes().isEmpty();
    });
    QCollator coll(QLocale(QLocale::English));
    std::sort(list.begin(), list.end(), [&coll](const KService::Ptr &a, const KService::Ptr &b) {
        return coll.compare(a->name(), b->name()) < 0;
    });
    return list;
}

/** "Mozilla", "Ubuntu" ... for a menu entry: the snap publisher or the Debian maintainer */
inline QString programPublisher(const KService::Ptr &s)
{
    static QHash<QString, QString> cache;
    const QString path = s->entryPath();
    if (cache.contains(path)) {
        return cache.value(path);
    }
    QString pub;
    if (path.startsWith(QLatin1String("/var/lib/snapd/"))) {
        const QString snap = QFileInfo(path).completeBaseName().section(QLatin1Char('_'), 0, 0);
        pub = snapd(QStringLiteral("/v2/snaps/") + snap).toObject().value(QStringLiteral("publisher")).toObject().value(QStringLiteral("display-name")).toString();
    } else if (path.startsWith(QLatin1String("/usr/"))) {
        const QString pkg = run(QStringLiteral("dpkg"), {QStringLiteral("-S"), path}).section(QLatin1Char(':'), 0, 0).trimmed();
        if (!pkg.isEmpty() && !pkg.contains(QLatin1Char(' '))) {
            pub = publisherOf(run(QStringLiteral("dpkg-query"), {QStringLiteral("-W"), QStringLiteral("-f=${Maintainer}"), pkg}));
        }
    }
    cache.insert(path, pub);
    return pub;
}

/** suffixes of a MIME type with the dot (".jpg"), protocols as "HTTP" */
inline QStringList extensionsOf(const QMimeType &m)
{
    QStringList out;
    for (const QString &s : m.suffixes()) {
        out.append(QLatin1Char('.') + s);
    }
    return out;
}

inline QString protocolName(const QString &mime)
{
    return mime.mid(QStringLiteral("x-scheme-handler/").size()).toUpper();
}

/* ---------------- "Open With" ("Choose the program you want to use") ---------------- */

class OpenWithDialog : public QDialog
{
public:
    OpenWithDialog(QWidget *parent, const QString &mime, const QString &shownName)
        : QDialog(parent)
        , m_mime(mime)
    {
        setWindowTitle(tr7("Open With"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-sud-assoc")));
        resize(500, 520);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 10);
        v->setSpacing(0);
        auto *top = new QWidget;
        top->setObjectName(QStringLiteral("win7owtop"));
        top->setAttribute(Qt::WA_StyledBackground, true);
        top->setStyleSheet(QStringLiteral("#win7owtop { background: #ffffff; }"));
        auto *tv = new QVBoxLayout(top);
        tv->setContentsMargins(16, 14, 16, 10);
        tv->setSpacing(8);
        auto *head = new QHBoxLayout;
        auto *ic = new QLabel;
        const QMimeType mt = QMimeDatabase().mimeTypeForName(mime);
        ic->setPixmap(themeIcon(mt.iconName(), mt.genericIconName()).pixmap(32, 32));
        head->addWidget(ic, 0, Qt::AlignTop);
        auto *hl = new QVBoxLayout;
        hl->addWidget(wrapped(tr7("Choose the program you want to use to open this file:", "open with")));
        hl->addWidget(grey(tr7("File:  ") + shownName));
        head->addLayout(hl, 1);
        tv->addLayout(head);
        tv->addSpacing(4);
        tv->addWidget(new SectionHeader(tr7("Recommended Programs")));
        m_recommended = makeList();
        tv->addWidget(m_recommended, 2);
        auto *other = new QToolButton;
        other->setText(tr7("Other Programs  ▾"));
        other->setAutoRaise(true);
        other->setCursor(Qt::PointingHandCursor);
        tv->addWidget(other, 0, Qt::AlignLeft);
        m_other = makeList();
        m_other->hide();
        tv->addWidget(m_other, 3);
        connect(other, &QToolButton::clicked, this, [this, other] {
            m_other->setVisible(!m_other->isVisible());
            other->setText(m_other->isVisible() ? tr7("Other Programs  ▴") : tr7("Other Programs  ▾"));
        });
        auto *always = new QCheckBox(tr7("Always use the selected program to open this kind of file", "open with"));
        always->setChecked(true);
        always->setEnabled(false);
        tv->addWidget(always);
        auto *browseRow = new QHBoxLayout;
        browseRow->addWidget(wrapped(tr7("If the program you want is not in the list, click Browse and select its file.")), 1);
        auto *browse = new QPushButton(tr7("Browse..."));
        browseRow->addWidget(browse, 0, Qt::AlignBottom);
        tv->addLayout(browseRow);
        v->addWidget(top, 1);
        auto *line = new QFrame;
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background: #dfdfdf;"));
        v->addWidget(line);
        auto *bh = new QHBoxLayout;
        bh->setContentsMargins(10, 10, 14, 0);
        bh->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        for (QPushButton *b : {ok, cancel}) {
            b->setMinimumWidth(75);
            bh->addWidget(b);
        }
        ok->setDefault(true);
        v->addLayout(bh);

        const KService::List rec = KApplicationTrader::queryByMimeType(mime);
        QSet<QString> seen;
        const KService::Ptr cur = KApplicationTrader::preferredService(mime);
        for (const KService::Ptr &s : rec) {
            if (!seen.contains(s->storageId())) {
                seen.insert(s->storageId());
                addItem(m_recommended, s);
            }
        }
        for (const KService::Ptr &s : defaultablePrograms()) {
            if (!seen.contains(s->storageId())) {
                seen.insert(s->storageId());
                addItem(m_other, s);
            }
        }
        for (QListWidget *l : {m_recommended, m_other}) {
            for (int i = 0; i < l->count(); ++i) {
                if (cur && l->item(i)->data(Qt::UserRole).toString() == cur->storageId()) {
                    l->setCurrentRow(i);
                }
            }
            connect(l, &QListWidget::itemDoubleClicked, this, [this] { choose(); });
            connect(l, &QListWidget::currentItemChanged, this, [this, l](QListWidgetItem *it) {
                if (it) {
                    (l == m_recommended ? m_other : m_recommended)->clearSelection();
                    m_chosen = it->data(Qt::UserRole).toString();
                }
            });
        }
        if (cur) {
            m_chosen = cur->storageId();
        }
        connect(ok, &QPushButton::clicked, this, [this] { choose(); });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(browse, &QPushButton::clicked, this, [this] { browseProgram(); });
    }

    QString chosen() const
    {
        return m_chosen;
    }

private:
    static QListWidget *makeList()
    {
        auto *l = new QListWidget;
        l->setViewMode(QListView::ListMode);
        l->setFlow(QListView::LeftToRight);
        l->setWrapping(true);
        l->setResizeMode(QListView::Adjust);
        l->setIconSize(QSize(32, 32));
        l->setGridSize(QSize(205, 42));
        l->setUniformItemSizes(true);
        l->setFrameShape(QFrame::NoFrame);
        l->setWordWrap(true);
        return l;
    }

    void addItem(QListWidget *l, const KService::Ptr &s)
    {
        auto *it = new QListWidgetItem(themeIcon(s->icon()), s->name(), l);
        it->setData(Qt::UserRole, s->storageId());
        it->setToolTip(s->comment());
    }

    void choose()
    {
        if (m_chosen.isEmpty()) {
            return;
        }
        accept();
    }

    /** "Browse...": a menu entry of our own for any program file */
    void browseProgram()
    {
        const QString file = QFileDialog::getOpenFileName(this, tr7("Open with..."), QStringLiteral("/usr/bin"));
        if (file.isEmpty()) {
            return;
        }
        const QString base = QFileInfo(file).completeBaseName();
        const QString id = QStringLiteral("seven-openwith-%1.desktop").arg(base.toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-")));
        const QString path = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + QLatin1Char('/') + id;
        {
            KDesktopFile df(path);
            KConfigGroup g = df.desktopGroup();
            g.writeEntry("Type", QStringLiteral("Application"));
            g.writeEntry("Name", base);
            g.writeEntry("Exec", QStringLiteral("\"%1\" %f").arg(file));
            g.writeEntry("Icon", QStringLiteral("seven-app-generic"));
            g.writeEntry("NoDisplay", true);
            g.writeXdgListEntry("MimeType", {m_mime});
            df.sync();
        }
        KSycoca::self()->ensureCacheValid();
        m_chosen = id;
        accept();
    }

    QString m_mime;
    QString m_chosen;
    QListWidget *m_recommended;
    QListWidget *m_other;
};

/* ---------------- "Default Programs" ---------------- */

/** the green arrow of Windows 7 command links */
inline QPixmap commandArrow()
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 2, 0, 14);
    g.setColorAt(0, QColor(0x6c, 0xd4, 0x4b));
    g.setColorAt(1, QColor(0x1f, 0x8b, 0x1a));
    p.setBrush(g);
    p.setPen(QPen(QColor(0x1a, 0x6e, 0x16), 1));
    p.drawPolygon(QPolygonF({QPointF(1.5, 6), QPointF(8, 6), QPointF(8, 2), QPointF(14.5, 8), QPointF(8, 14), QPointF(8, 10), QPointF(1.5, 10)}));
    return pm;
}

/** command link with the green arrow: title, grey explanation below */
class ArrowLink : public QWidget
{
public:
    ArrowLink(const QString &title, const QString &desc, std::function<void()> action)
        : m_action(std::move(action))
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(8, 6, 10, 8);
        h->setSpacing(8);
        auto *ic = new QLabel;
        ic->setPixmap(commandArrow());
        h->addWidget(ic, 0, Qt::AlignTop);
        auto *v = new QVBoxLayout;
        v->setSpacing(2);
        auto *t = new QLabel(title);
        QFont f = t->font();
        f.setPixelSize(14);
        t->setFont(f);
        t->setStyleSheet(QStringLiteral("color: #151c55;"));
        v->addWidget(t);
        auto *d = wrapped(desc);
        d->setStyleSheet(QStringLiteral("color: #151c55;"));
        v->addWidget(d);
        h->addLayout(v, 1);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!underMouse()) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor(0xc6, 0xc6, 0xc6));
        p.setBrush(QColor(0xf8, 0xf8, 0xf8));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
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
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && m_action) {
            m_action();
        }
    }

private:
    std::function<void()> m_action;
};

class AccessDialog;
inline void showAccessDialog(QWidget *parent);

/** "Choose the programs that Windows uses by default" */
inline QWidget *buildDefaultsPage(const Nav &nav, std::function<void()> autoplay)
{
    auto *side = Win7Display::sidePane(nav.home, {}, {{tr7("Programs and Features"), [nav] { nav.open(ProgramsView); }}});
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(26, 22, 30, 20);
    v->setSpacing(16);
    v->addWidget(pageTitle(tr7("Choose the programs that Windows uses by default")));
    auto entry = [v](const QString &icon, const QString &title, const QString &desc, std::function<void()> open) {
        auto *row = new QHBoxLayout;
        row->setSpacing(10);
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(icon).pixmap(24, 24));
        ic->setFixedSize(26, 26);
        row->addWidget(ic, 0, Qt::AlignTop);
        auto *col = new QVBoxLayout;
        col->setSpacing(2);
        col->addWidget(new Link(title, std::move(open)));
        col->addWidget(wrapped(desc));
        row->addLayout(col, 1);
        auto *w = new QWidget;
        w->setMaximumWidth(640);
        w->setLayout(row);
        v->addWidget(w);
    };
    entry(QStringLiteral("seven-sud"), tr7("Set your default programs"),
          tr7("Make a program always open all file types and protocols it can open."), [nav] { nav.open(SetDefaultsView); });
    entry(QStringLiteral("seven-sud-assoc"), tr7("Associate a file type or protocol with a program"),
          tr7("Make a file type or protocol (such as .mp3 or http://) always open in a specific program."), [nav] { nav.open(AssocView); });
    entry(QStringLiteral("seven-sud-autoplay"), tr7("Change AutoPlay settings"),
          tr7("Play CDs or other media automatically.", "default programs"), std::move(autoplay));
    entry(QStringLiteral("seven-sud-access"), tr7("Set program access and computer defaults"),
          tr7("Control access to certain programs and set defaults for this computer."), [content] { showAccessDialog(content->window()); });
    v->addStretch(1);
    return Win7Display::pageWithSide(side, content);
}

/** grey strip with OK at the bottom of the full-width pages */
inline QWidget *footer(const QList<QPushButton *> &buttons)
{
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("win7pgbar"));
    bar->setAttribute(Qt::WA_StyledBackground, true);
    bar->setStyleSheet(QStringLiteral("#win7pgbar { background: #f0f0f0; border-top: 1px solid #dfdfdf; }"));
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(10, 9, 14, 9);
    h->addStretch(1);
    for (QPushButton *b : buttons) {
        b->setMinimumWidth(75);
        h->addWidget(b);
    }
    return bar;
}

/** "Set your default programs": programs on the left, what they do on the right */
class SetDefaultsPage : public QWidget
{
public:
    explicit SetDefaultsPage(Nav nav)
        : m_nav(std::move(nav))
    {
        setObjectName(QStringLiteral("win7page"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto *body = new QWidget;
        auto *v = new QVBoxLayout(body);
        v->setContentsMargins(20, 18, 20, 16);
        v->setSpacing(8);
        v->addWidget(pageTitle(tr7("Set Default Programs")));
        v->addWidget(wrapped(tr7("To set a program as the default for all file types and protocols it can open, click the program and then click Set as default.")));
        v->addSpacing(10);
        m_stack = new QStackedWidget;
        // "Loading..." first, like Windows 7
        auto *loading = new QWidget;
        auto *lv = new QVBoxLayout(loading);
        lv->addStretch(1);
        auto *lt = new QLabel(tr7("Loading..."));
        lt->setAlignment(Qt::AlignHCenter);
        lv->addWidget(lt);
        m_progress = new QProgressBar;
        m_progress->setTextVisible(false);
        lv->addWidget(m_progress);
        lv->addSpacing(20);
        m_stack->addWidget(loading);
        auto *main = new QWidget;
        auto *mh = new QHBoxLayout(main);
        mh->setContentsMargins(0, 0, 0, 0);
        mh->setSpacing(20);
        m_list = new QTreeWidget;
        m_list->setHeaderLabels({tr7("Programs")});
        m_list->setRootIsDecorated(false);
        m_list->setFixedWidth(262);
        m_list->setIconSize(QSize(16, 16));
        m_list->setSortingEnabled(true);
        m_list->sortByColumn(0, Qt::AscendingOrder);
        mh->addWidget(m_list);
        auto *right = new QVBoxLayout;
        right->setContentsMargins(0, 50, 0, 0);
        m_detailsBox = new QScrollArea;
        m_detailsBox->setWidgetResizable(true);
        m_detailsBox->setMaximumHeight(256);
        m_detailsBox->setStyleSheet(QStringLiteral("QScrollArea { background: #ffffff; border: 1px solid #c8c8c8; }"));
        right->addWidget(m_detailsBox);
        right->addStretch(1);
        mh->addLayout(right, 1);
        m_stack->addWidget(main);
        v->addWidget(m_stack, 1);
        outer->addWidget(body, 1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        connect(ok, &QPushButton::clicked, this, [this] { m_nav.back(); });
        outer->addWidget(footer({ok}));
        connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) { showProgram(it); });
        showProgram(nullptr);
        QTimer::singleShot(40, this, [this] { loadStep(); });
    }

private:
    void loadStep()
    {
        if (m_services.isEmpty() && m_next == 0) {
            m_services = defaultablePrograms();
            m_progress->setRange(0, qMax(1, int(m_services.size())));
        }
        // a few at a time so the bar moves
        const int end = qMin(int(m_services.size()), m_next + 12);
        for (; m_next < end; ++m_next) {
            const KService::Ptr &s = m_services.at(m_next);
            auto *it = new SortItem(m_list, {s->name()});
            it->setIcon(0, themeIcon(s->icon()));
            it->setData(0, Qt::UserRole, m_next);
        }
        m_progress->setValue(m_next);
        if (m_next < m_services.size()) {
            QTimer::singleShot(15, this, [this] { loadStep(); });
            return;
        }
        m_stack->setCurrentIndex(1);
        if (!assocProgram().isEmpty()) { // back from "Set associations": the same program again
            for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
                if (m_services.at(m_list->topLevelItem(i)->data(0, Qt::UserRole).toInt())->storageId() == assocProgram()) {
                    m_list->setCurrentItem(m_list->topLevelItem(i));
                }
            }
        }
    }

    void showProgram(QTreeWidgetItem *it)
    {
        auto *w = new QWidget;
        w->setObjectName(QStringLiteral("win7sdbox"));
        w->setAttribute(Qt::WA_StyledBackground, true);
        w->setStyleSheet(QStringLiteral("#win7sdbox { background: #ffffff; }"));
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(10, 8, 10, 8);
        if (!it) {
            auto *l = grey(tr7("(Select a program to see information about it)"));
            v->addWidget(l);
            v->addStretch(1);
            m_detailsBox->setWidget(w);
            return;
        }
        const KService::Ptr s = m_services.at(it->data(0, Qt::UserRole).toInt());
        auto *head = new QHBoxLayout;
        auto *ic = new QLabel;
        ic->setPixmap(themeIcon(s->icon()).pixmap(32, 32));
        head->addWidget(ic, 0, Qt::AlignTop);
        auto *names = new QVBoxLayout;
        names->setSpacing(0);
        auto *name = new QLabel(s->name());
        QFont nf = name->font();
        nf.setBold(true);
        name->setFont(nf);
        names->addWidget(name);
        const QString pub = programPublisher(s);
        if (!pub.isEmpty()) {
            names->addWidget(grey(pub));
        }
        head->addLayout(names, 1);
        v->addLayout(head);
        const QString about = !s->comment().isEmpty() ? s->comment() : s->genericName();
        if (!about.isEmpty()) {
            v->addWidget(wrapped(about));
        }
        v->addSpacing(4);
        const QStringList mimes = s->mimeTypes();
        int used = 0;
        for (const QString &m : mimes) {
            used += isDefaultFor(s, m) ? 1 : 0;
        }
        auto *count = wrapped(used == mimes.size() ? tr7("This program has all its defaults.")
                                                  : tr7("This program has %1 out of %2 defaults.").arg(used).arg(mimes.size()));
        QFont cf = count->font();
        cf.setBold(true);
        count->setFont(cf);
        v->addWidget(count);
        v->addSpacing(6);
        QPointer<SetDefaultsPage> self(this);
        v->addWidget(new ArrowLink(tr7("Set this program as default"),
                                   tr7("Use the selected program to open all file types and protocols it can open by default."),
                                   [self, s] {
                                       setDefaults(s->mimeTypes(), s->storageId());
                                       if (self) {
                                           self->showProgram(self->m_list->currentItem());
                                       }
                                   }));
        v->addWidget(new ArrowLink(tr7("Choose defaults for this program"),
                                   tr7("Choose which file types and protocols the selected program opens by default."), [self, s] {
                                       assocProgram() = s->storageId();
                                       if (self) {
                                           self->m_nav.open(ProgramAssocView);
                                       }
                                   }));
        v->addStretch(1);
        m_detailsBox->setWidget(w);
    }

    Nav m_nav;
    QStackedWidget *m_stack;
    QProgressBar *m_progress;
    QTreeWidget *m_list;
    QScrollArea *m_detailsBox;
    QList<KService::Ptr> m_services;
    int m_next = 0;
};

/** rows of the association lists: one per extension, then one per protocol */
struct AssocRow {
    QString name;  // ".mp3" or "HTTP"
    QString mime;
    QString description;
    QString icon;
    bool protocol = false;
};

inline QList<AssocRow> assocRows(const QStringList &onlyMimes = {})
{
    QList<AssocRow> rows;
    QMimeDatabase db;
    QSet<QString> protocols;
    if (onlyMimes.isEmpty()) {
        const KService::List all = KApplicationTrader::query([](const KService::Ptr &) {
            return true;
        });
        for (const KService::Ptr &s : all) {
            for (const QString &m : s->mimeTypes()) {
                if (m.startsWith(QLatin1String("x-scheme-handler/"))) {
                    protocols.insert(m);
                }
            }
        }
        for (const QMimeType &mt : db.allMimeTypes()) {
            for (const QString &ext : extensionsOf(mt)) {
                rows.append({ext, mt.name(), mt.comment(), mt.iconName(), false});
            }
        }
    } else {
        for (const QString &m : onlyMimes) {
            if (m.startsWith(QLatin1String("x-scheme-handler/"))) {
                protocols.insert(m);
                continue;
            }
            const QMimeType mt = db.mimeTypeForName(m);
            if (!mt.isValid()) {
                continue;
            }
            const QStringList exts = extensionsOf(mt);
            if (exts.isEmpty()) {
                rows.append({mt.name(), mt.name(), mt.comment(), mt.iconName(), false});
            }
            for (const QString &ext : exts) {
                rows.append({ext, mt.name(), mt.comment(), mt.iconName(), false});
            }
        }
    }
    std::sort(rows.begin(), rows.end(), [](const AssocRow &a, const AssocRow &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    QStringList prot = protocols.values();
    std::sort(prot.begin(), prot.end());
    for (const QString &p : prot) {
        rows.append({protocolName(p), p, tr7("URL:%1 Protocol").arg(protocolName(p)), QStringLiteral("text-html"), true});
    }
    return rows;
}

/** "Associate a file type or protocol with a specific program" */
class AssocPage : public QWidget
{
public:
    explicit AssocPage(Nav nav)
        : m_nav(std::move(nav))
    {
        setObjectName(QStringLiteral("win7page"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto *body = new QWidget;
        auto *v = new QVBoxLayout(body);
        v->setContentsMargins(20, 18, 20, 12);
        v->setSpacing(8);
        v->addWidget(pageTitle(tr7("Associate a file type or protocol with a specific program")));
        v->addWidget(wrapped(tr7("Click on an extension to view the program that currently opens it by default. To change the default program, click Change program.")));
        v->addSpacing(6);
        auto *info = new QHBoxLayout;
        m_icon = new QLabel;
        m_icon->setFixedSize(32, 32);
        info->addWidget(m_icon, 0, Qt::AlignTop);
        auto *iv = new QVBoxLayout;
        iv->setSpacing(1);
        m_name = new QLabel;
        QFont nf = m_name->font();
        nf.setBold(true);
        m_name->setFont(nf);
        iv->addWidget(m_name);
        m_current = grey(QString());
        iv->addWidget(m_current);
        info->addLayout(iv, 1);
        m_change = new QPushButton(tr7("Change program..."));
        m_change->setEnabled(false);
        info->addWidget(m_change, 0, Qt::AlignTop);
        v->addLayout(info);
        m_tree = new DetailsTree;
        m_tree->setFrameShape(QFrame::StyledPanel);
        m_tree->setHeaderLabels({tr7("Name"), tr7("Description"), tr7("Current Default")});
        m_tree->setColumnWidth(0, 130);
        m_tree->setColumnWidth(1, 330);
        m_tree->setColumnWidth(2, 260);
        m_tree->setUniformRowHeights(true);
        v->addWidget(m_tree, 1);
        outer->addWidget(body, 1);
        auto *close = new QPushButton(tr7("Close"));
        connect(close, &QPushButton::clicked, this, [this] { m_nav.back(); });
        outer->addWidget(footer({close}));
        connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] { selectionChanged(); });
        connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
            if (it && !it->data(0, GroupRole).toBool()) {
                change();
            }
        });
        connect(m_change, &QPushButton::clicked, this, [this] { change(); });
        m_tree->addTopLevelItem(new QTreeWidgetItem({tr7("Please wait...")}));
        QTimer::singleShot(30, this, [this] { fill(); });
    }

    void setFilter(const QString &text)
    {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = m_tree->topLevelItem(i);
            if (it->data(0, GroupRole).toBool()) {
                continue;
            }
            it->setHidden(!text.isEmpty() && !it->text(0).contains(text, Qt::CaseInsensitive) && !it->text(1).contains(text, Qt::CaseInsensitive));
        }
    }

private:
    void fill()
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_rows = assocRows();
        m_tree->clear();
        QHash<QString, QString> defaults; // mime -> program name
        bool protocolHeader = false;
        QList<QTreeWidgetItem *> items;
        for (int i = 0; i < m_rows.size(); ++i) {
            const AssocRow &r = m_rows.at(i);
            if (r.protocol && !protocolHeader) {
                auto *g = new QTreeWidgetItem({tr7("Protocols")});
                g->setData(0, GroupRole, true);
                g->setFlags(Qt::ItemIsEnabled);
                items.append(g);
                protocolHeader = true;
            }
            if (!defaults.contains(r.mime)) {
                const KService::Ptr s = KApplicationTrader::preferredService(r.mime);
                defaults.insert(r.mime, s ? s->name() : tr7("Unknown application"));
            }
            auto *it = new QTreeWidgetItem({r.name, r.description, defaults.value(r.mime)});
            it->setIcon(0, themeIcon(r.icon, QStringLiteral("unknown")));
            it->setData(0, IndexRole, i);
            items.append(it);
        }
        m_tree->addTopLevelItems(items);
        for (QTreeWidgetItem *it : std::as_const(items)) {
            if (it->data(0, GroupRole).toBool()) {
                it->setFirstColumnSpanned(true);
            }
        }
        QApplication::restoreOverrideCursor();
        if (m_tree->topLevelItemCount()) {
            m_tree->setCurrentItem(m_tree->topLevelItem(0));
        }
    }

    const AssocRow *row() const
    {
        QTreeWidgetItem *it = m_tree->currentItem();
        if (!it || it->data(0, GroupRole).toBool() || !it->data(0, IndexRole).isValid()) {
            return nullptr;
        }
        return &m_rows.at(it->data(0, IndexRole).toInt());
    }

    void selectionChanged()
    {
        const AssocRow *r = row();
        m_change->setEnabled(r != nullptr);
        if (!r) {
            m_icon->clear();
            m_name->clear();
            m_current->clear();
            return;
        }
        m_icon->setPixmap(themeIcon(r->icon, QStringLiteral("unknown")).pixmap(32, 32));
        m_name->setText(r->name + QStringLiteral("    ") + r->description);
        const KService::Ptr s = KApplicationTrader::preferredService(r->mime);
        m_current->setText(tr7("Current Default:  ") + (s ? s->name() : tr7("Unknown application")));
    }

    void change()
    {
        const AssocRow *r = row();
        if (!r) {
            return;
        }
        const QString mime = r->mime;
        OpenWithDialog dlg(window(), mime, r->name);
        if (dlg.exec() != QDialog::Accepted || dlg.chosen().isEmpty()) {
            return;
        }
        setDefaults({mime}, dlg.chosen());
        const KService::Ptr s = KService::serviceByStorageId(dlg.chosen());
        const QString name = s ? s->name() : dlg.chosen();
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = m_tree->topLevelItem(i);
            if (it->data(0, IndexRole).isValid() && m_rows.at(it->data(0, IndexRole).toInt()).mime == mime) {
                it->setText(2, name);
            }
        }
        selectionChanged();
    }

    Nav m_nav;
    QList<AssocRow> m_rows;
    DetailsTree *m_tree;
    QLabel *m_icon;
    QLabel *m_name;
    QLabel *m_current;
    QPushButton *m_change;
};

/** "Set associations for a program": tick what the program should open */
inline QWidget *buildProgramAssocPage(const Nav &nav)
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("win7page"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    const KService::Ptr s = KService::serviceByStorageId(assocProgram());
    auto *body = new QWidget;
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(20, 18, 20, 12);
    v->setSpacing(8);
    v->addWidget(pageTitle(tr7("Set Program Associations")));
    v->addWidget(wrapped(tr7("Select the extensions you want this program to open by default, and then click Save.")));
    if (!s) {
        v->addWidget(grey(tr7("The program was not found.")));
        v->addStretch(1);
        outer->addWidget(body, 1);
        auto *close = new QPushButton(tr7("Close"));
        QObject::connect(close, &QPushButton::clicked, page, [nav] { nav.back(); });
        outer->addWidget(footer({close}));
        return page;
    }
    auto *head = new QHBoxLayout;
    auto *ic = new QLabel;
    ic->setPixmap(themeIcon(s->icon()).pixmap(32, 32));
    head->addWidget(ic);
    auto *names = new QVBoxLayout;
    names->setSpacing(0);
    auto *name = new QLabel(s->name());
    QFont nf = name->font();
    nf.setBold(true);
    name->setFont(nf);
    names->addWidget(name);
    const QString pub = programPublisher(s);
    if (!pub.isEmpty()) {
        names->addWidget(grey(pub));
    }
    head->addLayout(names, 1);
    v->addLayout(head);
    auto *all = new QCheckBox(tr7("Select All"));
    v->addWidget(all);
    auto *tree = new DetailsTree;
    tree->setFrameShape(QFrame::StyledPanel);
    tree->setHeaderLabels({tr7("Name"), tr7("Description"), tr7("Current Default")});
    tree->setColumnWidth(0, 150);
    tree->setColumnWidth(1, 330);
    tree->setColumnWidth(2, 260);
    v->addWidget(tree, 1);
    const QList<AssocRow> rows = assocRows(s->mimeTypes());
    bool protocolHeader = false;
    bool extHeader = false;
    for (const AssocRow &r : rows) {
        if (!r.protocol && !extHeader) {
            auto *g = new QTreeWidgetItem(tree, {tr7("Extension")});
            g->setData(0, GroupRole, true);
            g->setFlags(Qt::ItemIsEnabled);
            g->setFirstColumnSpanned(true);
            extHeader = true;
        }
        if (r.protocol && !protocolHeader) {
            auto *g = new QTreeWidgetItem(tree, {tr7("Protocols")});
            g->setData(0, GroupRole, true);
            g->setFlags(Qt::ItemIsEnabled);
            g->setFirstColumnSpanned(true);
            protocolHeader = true;
        }
        const KService::Ptr cur = KApplicationTrader::preferredService(r.mime);
        auto *it = new QTreeWidgetItem(tree, {r.name, r.description, cur ? cur->name() : tr7("Unknown application")});
        it->setIcon(0, themeIcon(r.icon, QStringLiteral("unknown")));
        it->setData(0, Qt::UserRole, r.mime);
        const bool already = cur && cur->storageId() == s->storageId();
        it->setFlags(already ? Qt::ItemIsUserCheckable : Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        it->setCheckState(0, already ? Qt::Checked : Qt::Unchecked);
    }
    // the rows of one MIME type go together
    QObject::connect(tree, &QTreeWidget::itemChanged, tree, [tree](QTreeWidgetItem *changed, int col) {
        if (col != 0 || !changed->data(0, Qt::UserRole).isValid()) {
            return;
        }
        const QString mime = changed->data(0, Qt::UserRole).toString();
        QSignalBlocker block(tree);
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = tree->topLevelItem(i);
            if (it != changed && it->data(0, Qt::UserRole).toString() == mime && (it->flags() & Qt::ItemIsEnabled)) {
                it->setCheckState(0, changed->checkState(0));
            }
        }
    });
    QObject::connect(all, &QCheckBox::toggled, tree, [tree](bool on) {
        QSignalBlocker block(tree);
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = tree->topLevelItem(i);
            if (it->data(0, Qt::UserRole).isValid() && (it->flags() & Qt::ItemIsEnabled)) {
                it->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
            }
        }
    });
    outer->addWidget(body, 1);
    auto *save = new QPushButton(tr7("Save"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    QObject::connect(save, &QPushButton::clicked, page, [tree, s, nav] {
        QStringList mimes;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = tree->topLevelItem(i);
            if (it->data(0, Qt::UserRole).isValid() && (it->flags() & Qt::ItemIsEnabled) && it->checkState(0) == Qt::Checked) {
                mimes.append(it->data(0, Qt::UserRole).toString());
            }
        }
        mimes.removeDuplicates();
        if (!mimes.isEmpty()) {
            setDefaults(mimes, s->storageId());
        }
        nav.back();
    });
    QObject::connect(cancel, &QPushButton::clicked, page, [nav] { nav.back(); });
    outer->addWidget(footer({save, cancel}));
    return page;
}

/* ---------------- "Set Program Access and Computer Defaults" ---------------- */

struct AccessCategory {
    QString title;      // "Виберіть веб-оглядач за замовчуванням:"
    QString current;    // "Використовувати поточний веб-оглядач"
    QString probe;      // MIME type the candidates must open
    QStringList mimes;  // what becomes theirs
    bool media = false; // all audio / video types the chosen player knows
    QString kdeKey;     // kdeglobals [General] entry
};

inline QList<AccessCategory> accessCategories()
{
    return {
        {tr7("Choose a default web browser:"), tr7("Use my current Web browser"), QStringLiteral("x-scheme-handler/https"),
         {QStringLiteral("x-scheme-handler/http"), QStringLiteral("x-scheme-handler/https"), QStringLiteral("text/html"), QStringLiteral("application/xhtml+xml")}, false,
         QStringLiteral("BrowserApplication")},
        {tr7("Choose a default e-mail program:"), tr7("Use my current e-mail program"), QStringLiteral("x-scheme-handler/mailto"),
         {QStringLiteral("x-scheme-handler/mailto")}, false, {}},
        {tr7("Choose a default media player:"), tr7("Use my current media player"), QStringLiteral("audio/mpeg"), {}, true, {}},
        {tr7("Choose a default file manager:"), tr7("Use my current file manager"), QStringLiteral("inode/directory"),
         {QStringLiteral("inode/directory")}, false, {}},
    };
}

class AccessDialog : public QDialog
{
public:
    explicit AccessDialog(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(tr7("Set program access and computer defaults"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-sud-access")));
        setAttribute(Qt::WA_DeleteOnClose);
        resize(720, 560);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 10);
        v->setSpacing(0);
        auto *top = new QWidget;
        top->setObjectName(QStringLiteral("win7spad"));
        top->setAttribute(Qt::WA_StyledBackground, true);
        top->setStyleSheet(QStringLiteral("#win7spad { background: #ffffff; }"));
        auto *tv = new QVBoxLayout(top);
        tv->setContentsMargins(16, 14, 16, 10);
        tv->setSpacing(8);
        tv->addWidget(wrapped(tr7("A configuration specifies default programs for certain activities, such as Web browsing or sending e-mail, and which programs are accessible from the Start menu, desktop, and other locations.")));
        tv->addWidget(new QLabel(tr7("Choose a configuration:")));
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setStyleSheet(QStringLiteral("QScrollArea { border: 1px solid #c8c8c8; background: #ffffff; }"));
        auto *inner = new QWidget;
        inner->setObjectName(QStringLiteral("win7spadin"));
        inner->setAttribute(Qt::WA_StyledBackground, true);
        inner->setStyleSheet(QStringLiteral("#win7spadin { background: #ffffff; }"));
        auto *iv = new QVBoxLayout(inner);
        iv->setContentsMargins(8, 8, 8, 8);
        iv->setSpacing(4);
        m_system = new QRadioButton(QStringLiteral("Kubuntu"));
        QFont bf = m_system->font();
        bf.setBold(true);
        m_system->setFont(bf);
        iv->addWidget(m_system);
        auto *sysDesc = grey(tr7("The default programs Kubuntu suggests."));
        sysDesc->setContentsMargins(22, 0, 0, 6);
        iv->addWidget(sysDesc);
        m_custom = new QRadioButton(tr7("Custom", "configuration"));
        m_custom->setFont(bf);
        m_custom->setChecked(true);
        iv->addWidget(m_custom);
        m_customBox = new QWidget;
        auto *cv = new QVBoxLayout(m_customBox);
        cv->setContentsMargins(22, 2, 0, 0);
        cv->setSpacing(3);
        for (const AccessCategory &c : accessCategories()) {
            cv->addSpacing(6);
            auto *t = new QLabel(c.title);
            t->setStyleSheet(QStringLiteral("color: #1e3287;"));
            cv->addWidget(t);
            auto *group = new QButtonGroup(this);
            auto *keep = new QRadioButton(c.current);
            keep->setChecked(true);
            group->addButton(keep);
            cv->addWidget(keep);
            QStringList ids;
            KService::List cands = KApplicationTrader::queryByMimeType(c.probe);
            if (c.media) {
                cands += KApplicationTrader::queryByMimeType(QStringLiteral("video/mp4"));
            }
            for (const KService::Ptr &s : cands) {
                if (s->noDisplay() || ids.contains(s->storageId())) {
                    continue;
                }
                ids.append(s->storageId());
                auto *r = new QRadioButton(s->name());
                r->setIcon(themeIcon(s->icon()));
                r->setProperty("service", s->storageId());
                group->addButton(r);
                cv->addWidget(r);
            }
            m_groups.append(group);
        }
        iv->addWidget(m_customBox);
        iv->addStretch(1);
        connect(m_custom, &QRadioButton::toggled, m_customBox, &QWidget::setEnabled);
        scroll->setWidget(inner);
        tv->addWidget(scroll, 1);
        v->addWidget(top, 1);
        auto *bh = new QHBoxLayout;
        bh->setContentsMargins(10, 10, 14, 0);
        bh->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        for (QPushButton *b : {ok, cancel}) {
            b->setMinimumWidth(75);
            bh->addWidget(b);
        }
        ok->setDefault(true);
        v->addLayout(bh);
        connect(ok, &QPushButton::clicked, this, [this] {
            apply();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

private:
    void apply()
    {
        const QList<AccessCategory> cats = accessCategories();
        KConfigGroup general(KSharedConfig::openConfig(QStringLiteral("kdeglobals")), QStringLiteral("General"));
        for (int i = 0; i < cats.size(); ++i) {
            const AccessCategory &c = cats.at(i);
            QStringList mimes = c.mimes;
            if (m_system->isChecked()) {
                if (c.media) {
                    for (const KService::Ptr &s : KApplicationTrader::queryByMimeType(c.probe)) {
                        for (const QString &m : s->mimeTypes()) {
                            if (m.startsWith(QLatin1String("audio/")) || m.startsWith(QLatin1String("video/"))) {
                                mimes.append(m);
                            }
                        }
                    }
                }
                clearDefaults(mimes);
                if (!c.kdeKey.isEmpty()) {
                    general.deleteEntry(c.kdeKey.toUtf8().constData(), KConfig::Notify);
                }
                continue;
            }
            QAbstractButton *b = m_groups.at(i)->checkedButton();
            const QString id = b ? b->property("service").toString() : QString();
            if (id.isEmpty()) {
                continue; // "use my current ..."
            }
            const KService::Ptr s = KService::serviceByStorageId(id);
            if (!s) {
                continue;
            }
            if (c.media) {
                for (const QString &m : s->mimeTypes()) {
                    if (m.startsWith(QLatin1String("audio/")) || m.startsWith(QLatin1String("video/"))) {
                        mimes.append(m);
                    }
                }
            }
            setDefaults(mimes, id);
            if (!c.kdeKey.isEmpty()) {
                general.writeEntry(c.kdeKey.toUtf8().constData(), id, KConfig::Notify);
            }
        }
        general.sync();
    }

    QRadioButton *m_system;
    QRadioButton *m_custom;
    QWidget *m_customBox;
    QList<QButtonGroup *> m_groups;
};

inline void showAccessDialog(QWidget *parent)
{
    (new AccessDialog(parent))->show();
}
} // namespace Win7Programs
