#include "win7i18n.h"
/*
 * "Control Panel" results of the Start menu search, as a KRunner D-Bus plugin (org.kde.krunner1).
 * seven-control --krunner is started by D-Bus when the Start menu searches; every match opens a
 * Control Panel page or tool and is shown in the "Панель керування" group, like Windows 7.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

struct RemoteMatch {
    QString id;
    QString text;
    QString iconName;
    int categoryRelevance = 0;
    double relevance = 0;
    QVariantMap properties;
};
using RemoteMatches = QList<RemoteMatch>;

struct RemoteAction {
    QString id;
    QString text;
    QString iconName;
};
using RemoteActions = QList<RemoteAction>;

Q_DECLARE_METATYPE(RemoteMatch)
Q_DECLARE_METATYPE(RemoteMatches)
Q_DECLARE_METATYPE(RemoteAction)
Q_DECLARE_METATYPE(RemoteActions)

QDBusArgument &operator<<(QDBusArgument &a, const RemoteMatch &m)
{
    a.beginStructure();
    a << m.id << m.text << m.iconName << m.categoryRelevance << m.relevance << m.properties;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, RemoteMatch &m)
{
    a.beginStructure();
    a >> m.id >> m.text >> m.iconName >> m.categoryRelevance >> m.relevance >> m.properties;
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const RemoteAction &x)
{
    a.beginStructure();
    a << x.id << x.text << x.iconName;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, RemoteAction &x)
{
    a.beginStructure();
    a >> x.id >> x.text >> x.iconName;
    a.endStructure();
    return a;
}

namespace
{
struct Task {
    QString text;
    QString icon;
    QStringList command; // first item "" = seven-control itself
    QString keywords;
};

const QList<Task> &tasks()
{
    static const QList<Task> list{
        {tr7("Change your Windows password"), QStringLiteral("seven-cpi-users"), {QString(), QStringLiteral("--users")}, tr7("password account user")},
        {tr7("Create an account"), QStringLiteral("seven-cpi-users"), {QString(), QStringLiteral("--manage-users")}, tr7("user add guest")},
        {tr7("Manage another account"), QStringLiteral("seven-cpi-users"), {QString(), QStringLiteral("--manage-users")}, tr7("user account delete remove")},
        {tr7("Change your account picture", "personalization"), QStringLiteral("seven-cpi-users"), {QString(), QStringLiteral("--users")}, tr7("avatar picture photo user")},
        {tr7("Change the theme"), QStringLiteral("seven-cpi-personalize"), {QString(), QStringLiteral("--personalize")}, tr7("theme appearance personalization aero")},
        {tr7("Change desktop background"), QStringLiteral("seven-cpi-personalize"), {QStringLiteral("systemsettings"), QStringLiteral("kcm_wallpaper")}, tr7("wallpaper background picture")},
        {tr7("Change sound effects"), QStringLiteral("seven-cpi-sound"), {QString(), QStringLiteral("--sound"), QStringLiteral("2")}, tr7("sound scheme sounds")},
        {tr7("Manage audio devices"), QStringLiteral("seven-cpi-sound"), {QString(), QStringLiteral("--sound")}, tr7("sound speakers headphones microphone volume")},
        {tr7("Adjust screen resolution"), QStringLiteral("seven-cpi-display"), {QString(), QStringLiteral("--resolution")}, tr7("screen monitor resolution refresh")},
        {tr7("Make text and other items larger or smaller"), QStringLiteral("seven-cpi-display"), {QString(), QStringLiteral("--display")}, tr7("screen font scale text")},
        {tr7("Change mouse settings"), QStringLiteral("seven-cpi-mouse"), {QString(), QStringLiteral("--mouse")}, tr7("mouse buttons click speed")},
        {tr7("Change how the mouse pointer looks"), QStringLiteral("seven-cpi-mouse"), {QString(), QStringLiteral("--mouse"), QStringLiteral("1")}, tr7("mouse cursor pointer")},
        {tr7("Change keyboard settings"), QStringLiteral("seven-cpi-keyboard"), {QString(), QStringLiteral("--keyboard")}, tr7("keyboard repeat cursor")},
        {tr7("Change keyboards or other input methods"), QStringLiteral("seven-cpi-region"), {QString(), QStringLiteral("--text-services")}, tr7("language layout keyboard")},
        {tr7("Change the date, time, or number format"), QStringLiteral("seven-cpi-region"), {QString(), QStringLiteral("--region")}, tr7("region language format")},
        {tr7("Set the time and date"), QStringLiteral("seven-cpi-datetime"), {QString(), QStringLiteral("--set-datetime")}, tr7("date time clock")},
        {tr7("Change the time zone"), QStringLiteral("seven-cpi-datetime"), {QString(), QStringLiteral("--timezone")}, tr7("time zone clock")},
        {tr7("Change battery settings"), QStringLiteral("seven-cpi-power"), {QString(), QStringLiteral("--power")}, tr7("power energy battery plan")},
        {tr7("Change what the power buttons do"), QStringLiteral("seven-cpi-power"), {QString(), QStringLiteral("--power"), QStringLiteral("buttons")}, tr7("power button shutdown")},
        {tr7("Change when the computer sleeps"), QStringLiteral("seven-cpi-power"), {QString(), QStringLiteral("--power"), QStringLiteral("edit")}, tr7("sleep standby screen turn off")},
        {tr7("View network status and tasks"), QStringLiteral("seven-cpi-network"), {QString(), QStringLiteral("--net-center")}, tr7("network internet wi-fi wifi")},
        {tr7("View network connections"), QStringLiteral("seven-cpi-network"), {QString(), QStringLiteral("--net-connections")}, tr7("network adapter connection")},
        {tr7("Check firewall status"), QStringLiteral("seven-cpi-firewall"), {QString(), QStringLiteral("--firewall")}, tr7("firewall protection network")},
        {tr7("View devices and printers"), QStringLiteral("seven-cpi-printers"), {QString(), QStringLiteral("--devices")}, tr7("device printer bluetooth")},
        {tr7("Add a printer"), QStringLiteral("seven-cpi-printers"), {QString(), QStringLiteral("--add-printer")}, tr7("printer print add")},
        {tr7("Device Manager"), QStringLiteral("seven-cpi-devmgr"), {QStringLiteral("seven-devmgr")}, tr7("device driver hardware")},
        {tr7("Play CDs or other media automatically"), QStringLiteral("seven-cpi-autostart"), {QString(), QStringLiteral("--autoplay")}, tr7("autoplay disc cd dvd usb flash drive")},
        {tr7("Windows Update"), QStringLiteral("seven-cpi-update"), {QString(), QStringLiteral("--windows-update")}, tr7("update updates windows update")},
        {tr7("Action Center"), QStringLiteral("seven-cpi-actioncenter"), {QString(), QStringLiteral("--action-center")}, tr7("action center security maintenance messages problems reports antivirus")},
        {tr7("Change how Windows searches"), QStringLiteral("seven-cpi-indexing"), {QString(), QStringLiteral("--indexing")}, tr7("indexing index search baloo")},
        {tr7("Review your computer's status and resolve issues"), QStringLiteral("seven-cpi-actioncenter"), {QString(), QStringLiteral("--action-center")}, tr7("status problems problem reports crash")},
        {tr7("Folder Options"), QStringLiteral("seven-cpi-folderopts"), {QString(), QStringLiteral("--folder-options")}, tr7("folder folders options hidden files click")},
        {tr7("View installed fonts"), QStringLiteral("seven-cpi-fonts"), {QString(), QStringLiteral("--fonts")}, tr7("font fonts")},
        {tr7("Adjust ClearType text"), QStringLiteral("seven-cttune"), {QString(), QStringLiteral("--cttune")}, tr7("cleartype font text smoothing sharpness")},
        {tr7("Find a character"), QStringLiteral("seven-charmap"), {QString(), QStringLiteral("--charmap")}, tr7("character character map charmap symbol")},
        {tr7("Uninstall a program"), QStringLiteral("seven-cpi-programs"), {QString(), QStringLiteral("--programs")}, tr7("program uninstall installed")},
        {tr7("View installed updates"), QStringLiteral("seven-cpi-programs"), {QString(), QStringLiteral("--updates")}, tr7("update program")},
        {tr7("Turn Windows features on or off"), QStringLiteral("seven-cpi-programs"), {QString(), QStringLiteral("--features")}, tr7("features components games")},
        {tr7("Set your default programs"), QStringLiteral("seven-cpi-defaults"), {QString(), QStringLiteral("--set-defaults")}, tr7("program default browser web")},
        {tr7("Associate a file type with a program"), QStringLiteral("seven-cpi-defaults"), {QString(), QStringLiteral("--associations")}, tr7("file type extension open")},
        {tr7("Customize icons on the taskbar"), QStringLiteral("seven-cpi-trayicons"), {QString(), QStringLiteral("--tray-icons")}, tr7("icons notifications tray")},
        {tr7("Taskbar and Start Menu"), QStringLiteral("seven-cpi-taskbar"), {QString(), QStringLiteral("--taskbar-props")}, tr7("taskbar start menu")},
        {tr7("Ease of Access Center"), QStringLiteral("seven-cpi-access"), {QString(), QStringLiteral("--ease")}, tr7("ease of access accessibility")},
        {tr7("Magnify portions of the screen using Magnifier"), QStringLiteral("seven-cpi-access"), {QString(), QStringLiteral("--magnifier")}, tr7("magnifier zoom screen")},
        {tr7("Type text using the On-Screen Keyboard"), QStringLiteral("seven-cpi-access"), {QString(), QStringLiteral("--osk")}, tr7("keyboard on-screen")},
        {tr7("Hear text read aloud with Narrator"), QStringLiteral("seven-cpi-access"), {QString(), QStringLiteral("--narrator")}, tr7("narrator read voice")},
        {tr7("Make the keyboard easier to use"), QStringLiteral("seven-cpi-access"), {QString(), QStringLiteral("--ease"), QStringLiteral("keyboard")}, tr7("sticky keys keyboard")},
        {tr7("Free up disk space by deleting unnecessary files"), QStringLiteral("seven-cleanmgr"), {QString(), QStringLiteral("--cleanmgr")}, tr7("disk cleanup space cache temporary")},
        {tr7("Create and format hard disk partitions"), QStringLiteral("seven-cpi-admin"), {QString(), QStringLiteral("--diskmgmt")}, tr7("disk partition format disk management")},
        {tr7("Back up your computer"), QStringLiteral("seven-cpi-backup"), {QString(), QStringLiteral("--backup")}, tr7("backup archive restore")},
        {tr7("Restore data, files, or computer from backup"), QStringLiteral("seven-cpi-backup"), {QString(), QStringLiteral("--restore-files")}, tr7("restore files backup archive")},
        {tr7("Create a system image"), QStringLiteral("seven-cpi-backup"), {QString(), QStringLiteral("--sysimage")}, tr7("system image backup timeshift")},
        {tr7("Restore your computer to the state it was in before the selected event"), QStringLiteral("seven-cpi-recovery"), {QString(), QStringLiteral("--rstrui")}, tr7("system restore restore point rstrui")},
        {tr7("View event logs"), QStringLiteral("seven-cpi-admin"), {QString(), QStringLiteral("--eventvwr")}, tr7("log events event viewer errors eventvwr")},
        {tr7("View amount of RAM and processor speed"), QStringLiteral("seven-cpi-system"), {QStringLiteral("seven-system")}, tr7("system processor memory ram computer")},
        {tr7("View running processes with Task Manager"), QStringLiteral("seven-cpi-perf"), {QStringLiteral("seven-taskmgr")}, tr7("processes task manager")},
    };
    return list;
}

/** every word of the query starts a word of the text or the keywords */
int score(const Task &t, const QStringList &words)
{
    const QStringList hay = (t.text + QLatin1Char(' ') + t.keywords).toLower().split(QRegularExpression(QStringLiteral("[\\s,«»()]+")), Qt::SkipEmptyParts);
    int s = 0;
    for (const QString &w : words) {
        bool found = false;
        for (const QString &h : hay) {
            if (h.startsWith(w)) {
                found = true;
                s += h == w ? 3 : 2;
                break;
            }
        }
        if (!found) {
            return 0;
        }
    }
    return s;
}
}

class ControlPanelRunner : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krunner1")
public:
    using QObject::QObject;

public Q_SLOTS:
    RemoteActions Actions()
    {
        return {};
    }

    RemoteMatches Match(const QString &query)
    {
        keepAlive();
        RemoteMatches out;
        const QStringList words = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.isEmpty() || query.trimmed().size() < 2) {
            return out;
        }
        for (int i = 0; i < tasks().size(); ++i) {
            const int s = score(tasks().at(i), words);
            if (!s) {
                continue;
            }
            RemoteMatch m;
            m.id = QString::number(i);
            m.text = tasks().at(i).text;
            m.iconName = tasks().at(i).icon;
            m.categoryRelevance = 50; // "possible match"
            m.relevance = qMin(0.9, 0.5 + s * 0.05);
            m.properties.insert(QStringLiteral("category"), tr7("Control Panel"));
            out.append(m);
        }
        return out;
    }

    void Run(const QString &id, const QString &)
    {
        keepAlive();
        const int i = id.toInt();
        if (i < 0 || i >= tasks().size()) {
            return;
        }
        QStringList cmd = tasks().at(i).command;
        QString program = cmd.takeFirst();
        if (program.isEmpty()) {
            program = QCoreApplication::applicationFilePath();
        } else if (program.startsWith(QLatin1String("seven-"))) {
            program = QDir::homePath() + QStringLiteral("/.local/bin/") + program;
        }
        QProcess::startDetached(program, cmd);
    }

    void Teardown()
    {
    }

private:
    /** D-Bus starts us again when needed, so quit after a while without searches */
    void keepAlive()
    {
        if (!m_idle) {
            m_idle = new QTimer(this);
            m_idle->setSingleShot(true);
            connect(m_idle, &QTimer::timeout, qApp, &QCoreApplication::quit);
        }
        m_idle->start(10 * 60 * 1000);
    }
    QTimer *m_idle = nullptr;
};

int runKRunner()
{
    qDBusRegisterMetaType<RemoteMatch>();
    qDBusRegisterMetaType<RemoteMatches>();
    qDBusRegisterMetaType<RemoteAction>();
    qDBusRegisterMetaType<RemoteActions>();
    auto *runner = new ControlPanelRunner(qApp);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/runner"), runner, QDBusConnection::ExportAllSlots) || !bus.registerService(QStringLiteral("org.sevenplasma.ControlPanelRunner"))) {
        return 1;
    }
    QMetaObject::invokeMethod(runner, [runner] { runner->Match(QString()); }, Qt::QueuedConnection); // starts the idle timer
    return qApp->exec();
}

#include "sevenrunner.moc"
