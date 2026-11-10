/*
 * Windows 7 "System Properties" (sysdm.cpl): Computer Name (netid.dll 1020 / 106), Hardware
 * (sysdm 2000), Advanced (sysdm 115) and System Protection (srrstr.dll 12 / 1240 / 1301).
 *
 *   computer name / description   -> hostnamectl (static and pretty host name), through pkexec
 *   workgroup                     -> [global] workgroup of /etc/samba/smb.conf, if Samba is set up
 *   System Restore                -> Timeshift (installed on first use), the same as rstrui
 *   Performance / User Profiles   -> our Performance Options (win7visualfx.h) / User Accounts
 *   Environment Variables         -> ~/.config/environment.d/90-seven-user.conf and /etc/environment
 * Remote Assistance / Remote Desktop (no such service here) and "Device Installation Settings"
 * (Windows Update driver downloads) are left out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7backup.h"

#include <QDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTreeWidget>

namespace Win7SysProps
{
inline QRect dlu(int x, int y, int w, int h)
{
    auto px = [](int v) { return int(std::lround(v * 7 / 4.0)); };
    auto py = [](int v) { return int(std::lround(v * 15 / 8.0)); };
    return QRect(px(x), py(y), px(w), py(h));
}

template<typename W>
W *place(QWidget *parent, W *w, const QRect &r)
{
    w->setParent(parent);
    w->setGeometry(r);
    return w;
}

inline QLabel *text(QWidget *parent, const QString &t, const QRect &r)
{
    auto *l = place(parent, new QLabel(t), r);
    l->setWordWrap(true);
    l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return l;
}

inline QLabel *icon(QWidget *parent, const QString &name, const QRect &r)
{
    auto *l = place(parent, new QLabel, r);
    l->setPixmap(QIcon::fromTheme(name).pixmap(32, 32));
    l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return l;
}

/** "Section ────────" headings of the System Protection page */
inline void sectionLine(QWidget *parent, const QString &title, int x, int y, int lineX, int lineW)
{
    place(parent, new QLabel(title), dlu(x, y, lineX - x - 2, 9));
    auto *line = place(parent, new QFrame, dlu(lineX, y + 4, lineW, 1));
    line->setFrameShape(QFrame::NoFrame);
    line->setStyleSheet(QStringLiteral("QFrame { background: #d5dfe5; border: none; }"));
}

inline QString run(const QString &cmd, const QStringList &args)
{
    QProcess p;
    p.start(cmd, args);
    p.waitForFinished(5000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

inline QString workgroup()
{
    QFile f(QStringLiteral("/etc/samba/smb.conf"));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    static const QRegularExpression re(QStringLiteral("^\\s*workgroup\\s*=\\s*(\\S+)"), QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    return re.match(QString::fromUtf8(f.readAll())).captured(1);
}

/* ---------------- Computer Name ---------------- */

class ComputerNamePage : public QWidget
{
public:
    std::function<void()> changed;

    ComputerNamePage()
    {
        icon(this, QStringLiteral("seven-cps-system"), dlu(7, 7, 20, 20));
        text(this, tr7("Linux uses the following information to identify your computer on the network."), dlu(40, 9, 205, 16));
        text(this, tr7("Computer description:"), dlu(7, 35, 82, 8));
        m_desc = place(this, new QLineEdit(run(QStringLiteral("hostnamectl"), {QStringLiteral("--pretty")})), dlu(90, 33, 155, 12));
        text(this, tr7("For example: \"Kitchen Computer\" or \"Mary's Computer\"."), dlu(90, 50, 155, 17));
        text(this, tr7("Full Name:"), dlu(7, 70, 82, 8));
        m_full = text(this, QHostInfo::localHostName(), dlu(90, 70, 155, 12));
        const QString wg = workgroup();
        text(this, tr7("Workgroup:"), dlu(7, 85, 82, 8));
        text(this, wg.isEmpty() ? tr7("Not set up (Samba is not installed)") : wg, dlu(90, 85, 155, 12));
        text(this, tr7("To rename this computer, click Change."), dlu(7, 106, 178, 18));
        auto *change = place(this, new QPushButton(tr7("Edit...")), dlu(195, 106, 50, 14));
        m_restart = text(this, QString(), dlu(25, 196, 209, 16));
        m_restartIcon = icon(this, QStringLiteral("dialog-information"), dlu(7, 195, 20, 20));
        m_restartIcon->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-information")).pixmap(16, 16));
        m_restartIcon->hide();
        connect(m_desc, &QLineEdit::textEdited, this, [this] {
            if (changed) {
                changed();
            }
        });
        connect(change, &QPushButton::clicked, this, [this] {
            rename();
        });
    }

    bool apply()
    {
        const QString pretty = m_desc->text().trimmed();
        if (pretty == run(QStringLiteral("hostnamectl"), {QStringLiteral("--pretty")})) {
            return true;
        }
        return QProcess::execute(QStringLiteral("pkexec"), {QStringLiteral("hostnamectl"), QStringLiteral("set-hostname"), QStringLiteral("--pretty"), pretty}) == 0;
    }

private:
    /** "Computer Name Changes" (netid.dll 106) */
    void rename()
    {
        QDialog d(this);
        d.setWindowTitle(tr7("Computer Name Changes"));
        d.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        d.setFixedSize(dlu(0, 0, 214, 221).size());
        text(&d, tr7("You can change the name of this computer. Changes might affect access to network resources."), dlu(7, 7, 200, 40));
        text(&d, tr7("Computer name:"), dlu(7, 48, 82, 8));
        auto *name = place(&d, new QLineEdit(QHostInfo::localHostName()), dlu(7, 58, 200, 14));
        name->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[A-Za-z0-9][A-Za-z0-9-]{0,62}")), name));
        text(&d, tr7("Full Name:"), dlu(7, 76, 200, 10));
        auto *full = text(&d, name->text(), dlu(7, 86, 200, 14));
        connect(name, &QLineEdit::textChanged, full, &QLabel::setText);
        auto *member = place(&d, new QGroupBox(tr7("Member of")), dlu(7, 121, 200, 70));
        text(member, tr7("Workgroup:"), dlu(6, 12, 120, 9));
        auto *wg = place(member, new QLineEdit(workgroup()), dlu(6, 23, 186, 14));
        wg->setEnabled(false);
        wg->setToolTip(tr7("The workgroup is set by Samba"));
        auto *ok = place(&d, new QPushButton(QStringLiteral("OK")), dlu(103, 200, 50, 14));
        auto *cancel = place(&d, new QPushButton(tr7("Cancel")), dlu(157, 200, 50, 14));
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
        if (d.exec() != QDialog::Accepted || name->text().trimmed().isEmpty() || name->text() == QHostInfo::localHostName()) {
            return;
        }
        if (QProcess::execute(QStringLiteral("pkexec"), {QStringLiteral("hostnamectl"), QStringLiteral("set-hostname"), QStringLiteral("--static"), name->text().trimmed()}) != 0) {
            QMessageBox::warning(this, tr7("Computer Name Changes"), tr7("The computer name could not be changed."));
            return;
        }
        m_full->setText(name->text().trimmed());
        m_restartIcon->show();
        m_restart->setText(tr7("The changes will take effect after you restart this computer."));
        QMessageBox::information(this, tr7("Computer Name Changes"), tr7("You must restart your computer to apply these changes."));
    }

    QLineEdit *m_desc;
    QLabel *m_full;
    QLabel *m_restart;
    QLabel *m_restartIcon;
};

/* ---------------- Hardware ---------------- */

inline QWidget *hardwarePage()
{
    auto *p = new QWidget;
    place(p, new QGroupBox(tr7("Device Manager")), dlu(7, 12, 242, 62));
    icon(p, QStringLiteral("seven-devmgr-w7"), dlu(14, 24, 21, 20));
    text(p, tr7("The Device Manager lists all the hardware devices installed on your computer. Use the Device Manager to change the properties of any device."),
         dlu(45, 24, 195, 30));
    auto *b = place(p, new QPushButton(tr7("Device Manager")), dlu(124, 53, 118, 14));
    QObject::connect(b, &QPushButton::clicked, p, [] {
        launchWin7Tool(QStringLiteral("seven-devmgr"), QStringLiteral("kinfocenter"));
    });
    for (QWidget *w : p->findChildren<QGroupBox *>()) {
        w->lower();
    }
    return p;
}

/* ---------------- Advanced ---------------- */

/** "Environment Variables": the user's own variables (environment.d) and the system ones (read-only) */
inline void environmentDialog(QWidget *parent)
{
    const QString userFile = QDir::homePath() + QStringLiteral("/.config/environment.d/90-seven-user.conf");
    QDialog d(parent);
    d.setWindowTitle(tr7("Environment Variables"));
    d.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    d.setFixedSize(dlu(0, 0, 252, 245).size());
    auto makeList = [&d](const QRect &r) {
        auto *t = place(&d, new QTreeWidget, r);
        t->setRootIsDecorated(false);
        t->setHeaderLabels({tr7("Variable"), tr7("Value")});
        t->setColumnWidth(0, 120);
        return t;
    };
    place(&d, new QGroupBox(tr7("User variables for %1").arg(QString::fromLocal8Bit(qgetenv("USER")))), dlu(7, 7, 238, 100));
    auto *user = makeList(dlu(14, 19, 224, 62));
    auto *add = place(&d, new QPushButton(tr7("New...")), dlu(80, 86, 50, 14));
    auto *edit = place(&d, new QPushButton(tr7("Edit...")), dlu(134, 86, 50, 14));
    auto *del = place(&d, new QPushButton(tr7("Delete")), dlu(188, 86, 50, 14));
    place(&d, new QGroupBox(tr7("System variables")), dlu(7, 113, 238, 104));
    auto *sys = makeList(dlu(14, 125, 224, 85));
    for (QGroupBox *g : d.findChildren<QGroupBox *>()) {
        g->lower();
    }
    auto *ok = place(&d, new QPushButton(QStringLiteral("OK")), dlu(141, 224, 50, 14));
    auto *cancel = place(&d, new QPushButton(tr7("Cancel")), dlu(195, 224, 50, 14));
    ok->setDefault(true);
    auto readKv = [](const QString &path) {
        QList<QPair<QString, QString>> kv;
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            for (const QString &line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
                const QString l = line.trimmed();
                if (l.isEmpty() || l.startsWith(QLatin1Char('#')) || !l.contains(QLatin1Char('='))) {
                    continue;
                }
                kv.append({l.section(QLatin1Char('='), 0, 0).trimmed(), l.section(QLatin1Char('='), 1).trimmed().remove(QLatin1Char('"'))});
            }
        }
        return kv;
    };
    for (const auto &e : readKv(userFile)) {
        new QTreeWidgetItem(user, {e.first, e.second});
    }
    for (const auto &e : readKv(QStringLiteral("/etc/environment"))) {
        new QTreeWidgetItem(sys, {e.first, e.second});
    }
    auto editVar = [&d](QTreeWidgetItem *it, QTreeWidget *list) {
        QDialog e(&d);
        e.setWindowTitle(it ? tr7("Edit User Variable") : tr7("New User Variable"));
        e.setFixedSize(dlu(0, 0, 230, 68).size());
        text(&e, tr7("Variable name:"), dlu(7, 9, 60, 8));
        auto *n = place(&e, new QLineEdit(it ? it->text(0) : QString()), dlu(70, 7, 153, 12));
        text(&e, tr7("Variable value:"), dlu(7, 27, 60, 8));
        auto *v = place(&e, new QLineEdit(it ? it->text(1) : QString()), dlu(70, 25, 153, 12));
        n->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*")), n));
        auto *okb = place(&e, new QPushButton(QStringLiteral("OK")), dlu(119, 47, 50, 14));
        auto *cb = place(&e, new QPushButton(tr7("Cancel")), dlu(173, 47, 50, 14));
        QObject::connect(okb, &QPushButton::clicked, &e, &QDialog::accept);
        QObject::connect(cb, &QPushButton::clicked, &e, &QDialog::reject);
        if (e.exec() != QDialog::Accepted || n->text().isEmpty()) {
            return;
        }
        if (!it) {
            it = new QTreeWidgetItem(list);
        }
        it->setText(0, n->text());
        it->setText(1, v->text());
    };
    QObject::connect(add, &QPushButton::clicked, &d, [=] {
        editVar(nullptr, user);
    });
    QObject::connect(edit, &QPushButton::clicked, &d, [=] {
        if (user->currentItem()) {
            editVar(user->currentItem(), user);
        }
    });
    QObject::connect(user, &QTreeWidget::itemDoubleClicked, &d, [=](QTreeWidgetItem *it) {
        editVar(it, user);
    });
    QObject::connect(del, &QPushButton::clicked, &d, [=] {
        delete user->currentItem();
    });
    QObject::connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) {
        return;
    }
    QDir().mkpath(QFileInfo(userFile).absolutePath());
    QFile f(userFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write("# Environment Variables (System Properties > Advanced). Applied at the next login.\n");
        for (int i = 0; i < user->topLevelItemCount(); ++i) {
            f.write(QStringLiteral("%1=\"%2\"\n").arg(user->topLevelItem(i)->text(0), user->topLevelItem(i)->text(1)).toUtf8());
        }
    }
}

inline QWidget *advancedPage(QWidget *dialog)
{
    auto *p = new QWidget;
    text(p, tr7("You must be logged on as an Administrator to make most of these changes."), dlu(10, 8, 240, 12));
    struct Box {
        QString title, desc;
        int y;
        std::function<void()> open;
    };
    const QList<Box> boxes{
        {tr7("Performance", "system properties"), tr7("Visual effects, processor scheduling, memory usage, and virtual memory"), 22,
         [] {
             QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--visualfx")});
         }},
        {tr7("User Profiles"), tr7("Desktop settings related to your logon"), 79,
         [] {
             QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--manage-users")});
         }},
        {tr7("Startup and Recovery"), tr7("System startup, system failure, and debugging information"), 136,
         [] {
             launch(QStringLiteral("systemsettings"), {QStringLiteral("kcm_grub2")});
         }},
    };
    for (const Box &b : boxes) {
        place(p, new QGroupBox(b.title), dlu(7, b.y, 242, 53))->lower();
        text(p, b.desc, dlu(14, b.y + 12, 226, 20));
        auto *btn = place(p, new QPushButton(tr7("Options...")), dlu(193, b.y + 32, 49, 14));
        QObject::connect(btn, &QPushButton::clicked, p, b.open);
        if (b.y == 136 && QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("applications/kcm_grub2.desktop")).isEmpty()
            && !QFileInfo::exists(QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/plugins/plasma/kcms/systemsettings/kcm_grub2.so"))) {
            btn->setEnabled(false);
            btn->setToolTip(tr7("The GRUB settings module is needed (kde-config-grub2)"));
        }
    }
    auto *env = place(p, new QPushButton(tr7("Environment Variables...")), dlu(164, 196, 85, 14));
    QObject::connect(env, &QPushButton::clicked, p, [dialog] {
        environmentDialog(dialog);
    });
    return p;
}

/* ---------------- System Protection ---------------- */

inline QString timeshiftConfig()
{
    QFile f(QStringLiteral("/etc/timeshift/timeshift.json"));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

/** on = Timeshift installed and a schedule (or at least one restore point) exists */
inline bool protectionOn()
{
    if (!Win7Backup::haveTimeshift()) {
        return false;
    }
    const QString cfg = timeshiftConfig();
    for (const char *k : {"schedule_monthly", "schedule_weekly", "schedule_daily", "schedule_hourly", "schedule_boot"}) {
        if (cfg.contains(QStringLiteral("\"%1\" : \"true\"").arg(QLatin1String(k))) || cfg.contains(QStringLiteral("\"%1\": \"true\"").arg(QLatin1String(k)))) {
            return true;
        }
    }
    return !Win7Backup::snapshotsIn(QString()).isEmpty();
}

/** turns the daily schedule of Timeshift on or off (the JSON belongs to root) */
inline bool setProtection(QWidget *parent, bool on)
{
    if (on && !Win7Backup::ensureTimeshift(parent)) {
        return false;
    }
    const QString uuid = run(QStringLiteral("findmnt"), {QStringLiteral("-no"), QStringLiteral("UUID"), QStringLiteral("/")});
    const QString py = QStringLiteral(
                           "import json,os\n"
                           "p='/etc/timeshift/timeshift.json'\n"
                           "d=json.load(open(p)) if os.path.exists(p) else json.load(open('/etc/timeshift/default.json')) if os.path.exists('/etc/timeshift/default.json') else {}\n"
                           "d['btrfs_mode']='false'\n"
                           "d.setdefault('backup_device_uuid','%1') if '%1' else None\n"
                           "d['schedule_daily']='%2'\n"
                           "d['schedule_boot']='false'\n"
                           "d['count_daily']=d.get('count_daily','5')\n"
                           "os.makedirs('/etc/timeshift',exist_ok=True)\n"
                           "json.dump(d,open(p,'w'),indent=2)\n")
                           .arg(uuid, on ? QStringLiteral("true") : QStringLiteral("false"));
    return Win7Backup::runAsRoot(parent, QStringLiteral("python3 - <<'PYEOF'\n%1PYEOF").arg(py));
}

class ProtectionPage : public QWidget
{
public:
    ProtectionPage()
    {
        icon(this, QStringLiteral("seven-rstrui"), dlu(7, 7, 20, 20));
        text(this, tr7("Use system protection to undo unwanted system changes and restore previous versions of files."),
             dlu(35, 7, 210, 24));
        sectionLine(this, tr7("System Restore"), 7, 41, 80, 165);
        text(this, tr7("You can undo system changes by reverting your computer to a previous restore point."), dlu(10, 58, 140, 24));
        auto *restore = place(this, new QPushButton(tr7("System Restore...")), dlu(151, 59, 95, 14));
        sectionLine(this, tr7("Protection Settings"), 7, 100, 80, 165);
        m_list = place(this, new QTreeWidget, dlu(14, 112, 227, 50));
        m_list->setRootIsDecorated(false);
        m_list->setHeaderLabels({tr7("Available Drives"), tr7("Protection")});
        m_list->setColumnWidth(0, dlu(0, 0, 150, 0).width());
        text(this, tr7("Configure restore settings, manage disk space, and delete restore points."), dlu(14, 170, 155, 24));
        auto *configure = place(this, new QPushButton(tr7("Configure...", "protection")), dlu(172, 170, 70, 14));
        text(this, tr7("Create a restore point right now for the drives that have system protection turned on."), dlu(14, 198, 155, 24));
        m_create = place(this, new QPushButton(tr7("New...")), dlu(172, 198, 70, 14));
        connect(restore, &QPushButton::clicked, this, [] {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--rstrui")});
        });
        connect(configure, &QPushButton::clicked, this, [this] {
            configureDialog();
        });
        connect(m_create, &QPushButton::clicked, this, [this] {
            createDialog();
        });
        refresh();
    }

private:
    void refresh()
    {
        m_list->clear();
        const bool on = protectionOn();
        auto *it = new QTreeWidgetItem(m_list, {tr7("Linux (/) (System)"), on ? tr7("Enabled") : tr7("Off", "state")});
        it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-drive-system"), QIcon::fromTheme(QStringLiteral("drive-harddisk"))));
        m_create->setEnabled(on);
    }

    /** "System Protection for Linux (/)" (srrstr.dll 1240) */
    void configureDialog()
    {
        QDialog d(this);
        d.setWindowTitle(tr7("System Protection for Linux (/)"));
        d.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        d.setFixedSize(dlu(0, 0, 270, 200).size());
        sectionLine(&d, tr7("Restore Settings"), 7, 4, 90, 170);
        text(&d, tr7("System Protection can keep copies of system settings and files. Select what you would like to be able to restore:"), dlu(14, 16, 240, 16));
        auto *onRb = place(&d, new QRadioButton(tr7("Restore system settings and files")), dlu(24, 36, 230, 10));
        auto *offRb = place(&d, new QRadioButton(tr7("Turn off system protection")), dlu(24, 50, 230, 10));
        (protectionOn() ? onRb : offRb)->setChecked(true);
        sectionLine(&d, tr7("Disk Space Usage"), 7, 70, 110, 150);
        const QList<Win7Backup::Snapshot> points = Win7Backup::snapshotsIn(QString());
        text(&d, tr7("Restore points are kept on the system drive; the oldest are deleted automatically (5 daily ones are kept)."),
             dlu(14, 82, 246, 20));
        text(&d, tr7("Restore points: %1").arg(points.size()), dlu(14, 106, 200, 8));
        text(&d, tr7("Delete all restore points (this includes system settings and previous versions of files)."), dlu(14, 124, 176, 30));
        auto *del = place(&d, new QPushButton(tr7("Delete")), dlu(210, 127, 50, 14));
        del->setEnabled(!points.isEmpty());
        auto *sep = place(&d, new QFrame, dlu(7, 168, 256, 1));
        sep->setFrameShape(QFrame::HLine);
        auto *ok = place(&d, new QPushButton(QStringLiteral("OK")), dlu(99, 178, 50, 14));
        auto *cancel = place(&d, new QPushButton(tr7("Cancel")), dlu(154, 178, 50, 14));
        auto *apply = place(&d, new QPushButton(tr7("Apply")), dlu(209, 178, 50, 14));
        apply->setEnabled(false);
        ok->setDefault(true);
        const bool wasOn = onRb->isChecked();
        connect(onRb, &QRadioButton::toggled, &d, [apply] {
            apply->setEnabled(true);
        });
        auto doApply = [&] {
            if (onRb->isChecked() != wasOn || apply->isEnabled()) {
                setProtection(&d, onRb->isChecked());
            }
            apply->setEnabled(false);
        };
        connect(apply, &QPushButton::clicked, &d, doApply);
        connect(ok, &QPushButton::clicked, &d, [&] {
            doApply();
            d.accept();
        });
        connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
        connect(del, &QPushButton::clicked, &d, [&] {
            if (QMessageBox::question(&d, tr7("System Protection"), tr7("You cannot undo the deletion of restore points on this drive. Do you want to continue?"))
                != QMessageBox::Yes) {
                return;
            }
            if (Win7Backup::runTimeshift(&d, tr7("System Protection"), tr7("Deleting restore points..."),
                                         {QStringLiteral("--delete-all"), QStringLiteral("--scripted")})) {
                QMessageBox::information(&d, tr7("System Protection"), tr7("The restore points were deleted successfully."));
                del->setEnabled(false);
            }
        });
        d.exec();
        refresh();
    }

    /** "Create a restore point" (srrstr.dll 1301) */
    void createDialog()
    {
        QDialog d(this);
        d.setWindowTitle(tr7("System Protection"));
        d.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        d.setFixedSize(dlu(0, 0, 270, 110).size());
        auto *t = text(&d, tr7("Create a restore point"), dlu(7, 7, 254, 14));
        QFont f = t->font();
        f.setPixelSize(15);
        t->setFont(f);
        t->setStyleSheet(QStringLiteral("color: #1e3287;"));
        text(&d, tr7("Type a description to help you identify the restore point. The current date and time are added automatically."), dlu(7, 25, 259, 23));
        auto *desc = place(&d, new QLineEdit, dlu(7, 53, 256, 14));
        auto *sep = place(&d, new QFrame, dlu(0, 80, 271, 1));
        sep->setFrameShape(QFrame::HLine);
        auto *create = place(&d, new QPushButton(tr7("Create")), dlu(158, 88, 50, 14));
        auto *cancel = place(&d, new QPushButton(tr7("Cancel")), dlu(212, 88, 50, 14));
        create->setDefault(true);
        create->setEnabled(false);
        connect(desc, &QLineEdit::textChanged, create, [create](const QString &s) {
            create->setEnabled(!s.trimmed().isEmpty());
        });
        connect(create, &QPushButton::clicked, &d, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
        if (d.exec() != QDialog::Accepted) {
            return;
        }
        if (Win7Backup::runTimeshift(this, tr7("System Protection"), tr7("Creating a restore point..."),
                                     {QStringLiteral("--create"), QStringLiteral("--scripted"), QStringLiteral("--comments"), desc->text().trimmed()})) {
            QMessageBox::information(this, tr7("System Protection"), tr7("The restore point was created successfully."));
        }
        refresh();
    }

    QTreeWidget *m_list;
    QPushButton *m_create;
};

/* ---------------- the dialog ---------------- */

class SystemProperties : public QDialog
{
public:
    explicit SystemProperties(int tab = 0)
    {
        setWindowTitle(tr7("System properties"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cps-system")));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_DeleteOnClose);
        const QSize page = dlu(0, 0, 256, 226).size();
        setFixedSize(page.width() + 26, page.height() + 76);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(8, 8, 8, 8);
        auto *tabs = new QTabWidget;
        m_name = new ComputerNamePage;
        tabs->addTab(m_name, tr7("Computer Name"));
        tabs->addTab(hardwarePage(), tr7("Hardware"));
        tabs->addTab(advancedPage(this), tr7("Advanced"));
        tabs->addTab(new ProtectionPage, tr7("System Protection"));
        tabs->setCurrentIndex(qBound(0, tab, tabs->count() - 1));
        v->addWidget(tabs, 1);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        m_apply = new QPushButton(tr7("Apply"));
        for (QPushButton *b : {ok, cancel, m_apply}) {
            b->setMinimumWidth(75);
            row->addWidget(b);
        }
        ok->setDefault(true);
        m_apply->setEnabled(false);
        v->addLayout(row);
        m_name->changed = [this] {
            m_apply->setEnabled(true);
        };
        connect(ok, &QPushButton::clicked, this, [this] {
            if (!m_apply->isEnabled() || m_name->apply()) {
                accept();
            }
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] {
            if (m_name->apply()) {
                m_apply->setEnabled(false);
            }
        });
    }

private:
    ComputerNamePage *m_name;
    QPushButton *m_apply;
};
}
