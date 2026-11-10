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
        text(this, QStringLiteral("Linux використовує наведені нижче відомості для визначення комп'ютера в мережі."), dlu(40, 9, 205, 16));
        text(this, QStringLiteral("Опис комп'ютера:"), dlu(7, 35, 82, 8));
        m_desc = place(this, new QLineEdit(run(QStringLiteral("hostnamectl"), {QStringLiteral("--pretty")})), dlu(90, 33, 155, 12));
        text(this, QStringLiteral("Наприклад: \"Комп'ютер у вітальні\" або \"Комп'ютер Марії\"."), dlu(90, 50, 155, 17));
        text(this, QStringLiteral("Повне ім'я:"), dlu(7, 70, 82, 8));
        m_full = text(this, QHostInfo::localHostName(), dlu(90, 70, 155, 12));
        const QString wg = workgroup();
        text(this, QStringLiteral("Робоча група:"), dlu(7, 85, 82, 8));
        text(this, wg.isEmpty() ? QStringLiteral("Не налаштовано (Samba не встановлено)") : wg, dlu(90, 85, 155, 12));
        text(this, QStringLiteral("Щоб перейменувати цей комп'ютер, натисніть кнопку «Змінити»."), dlu(7, 106, 178, 18));
        auto *change = place(this, new QPushButton(QStringLiteral("Змінити...")), dlu(195, 106, 50, 14));
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
        d.setWindowTitle(QStringLiteral("Зміна імені комп'ютера"));
        d.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        d.setFixedSize(dlu(0, 0, 214, 221).size());
        text(&d, QStringLiteral("Ім'я комп'ютера можна змінити. Зміни можуть вплинути на доступ до мережних ресурсів."), dlu(7, 7, 200, 40));
        text(&d, QStringLiteral("Ім'я комп'ютера:"), dlu(7, 48, 82, 8));
        auto *name = place(&d, new QLineEdit(QHostInfo::localHostName()), dlu(7, 58, 200, 14));
        name->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[A-Za-z0-9][A-Za-z0-9-]{0,62}")), name));
        text(&d, QStringLiteral("Повне ім'я:"), dlu(7, 76, 200, 10));
        auto *full = text(&d, name->text(), dlu(7, 86, 200, 14));
        connect(name, &QLineEdit::textChanged, full, &QLabel::setText);
        auto *member = place(&d, new QGroupBox(QStringLiteral("Член")), dlu(7, 121, 200, 70));
        text(member, QStringLiteral("Робоча група:"), dlu(6, 12, 120, 9));
        auto *wg = place(member, new QLineEdit(workgroup()), dlu(6, 23, 186, 14));
        wg->setEnabled(false);
        wg->setToolTip(QStringLiteral("Робочу групу задає Samba"));
        auto *ok = place(&d, new QPushButton(QStringLiteral("OK")), dlu(103, 200, 50, 14));
        auto *cancel = place(&d, new QPushButton(QStringLiteral("Скасувати")), dlu(157, 200, 50, 14));
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
        if (d.exec() != QDialog::Accepted || name->text().trimmed().isEmpty() || name->text() == QHostInfo::localHostName()) {
            return;
        }
        if (QProcess::execute(QStringLiteral("pkexec"), {QStringLiteral("hostnamectl"), QStringLiteral("set-hostname"), QStringLiteral("--static"), name->text().trimmed()}) != 0) {
            QMessageBox::warning(this, QStringLiteral("Зміна імені комп'ютера"), QStringLiteral("Не вдалося змінити ім'я комп'ютера."));
            return;
        }
        m_full->setText(name->text().trimmed());
        m_restartIcon->show();
        m_restart->setText(QStringLiteral("Зміни наберуть сили після перезавантаження комп'ютера."));
        QMessageBox::information(this, QStringLiteral("Зміна імені комп'ютера"), QStringLiteral("Щоб зміни набрали сили, перезавантажте комп'ютер."));
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
    place(p, new QGroupBox(QStringLiteral("Диспетчер пристроїв")), dlu(7, 12, 242, 62));
    icon(p, QStringLiteral("seven-devmgr-w7"), dlu(14, 24, 21, 20));
    text(p, QStringLiteral("У диспетчері пристроїв наведено всі пристрої, установлені на комп'ютері. За його допомогою можна змінювати властивості будь-якого пристрою."),
         dlu(45, 24, 195, 30));
    auto *b = place(p, new QPushButton(QStringLiteral("Диспетчер пристроїв")), dlu(124, 53, 118, 14));
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
    d.setWindowTitle(QStringLiteral("Змінні середовища"));
    d.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    d.setFixedSize(dlu(0, 0, 252, 245).size());
    auto makeList = [&d](const QRect &r) {
        auto *t = place(&d, new QTreeWidget, r);
        t->setRootIsDecorated(false);
        t->setHeaderLabels({QStringLiteral("Змінна"), QStringLiteral("Значення")});
        t->setColumnWidth(0, 120);
        return t;
    };
    place(&d, new QGroupBox(QStringLiteral("Змінні середовища користувача %1").arg(QString::fromLocal8Bit(qgetenv("USER")))), dlu(7, 7, 238, 100));
    auto *user = makeList(dlu(14, 19, 224, 62));
    auto *add = place(&d, new QPushButton(QStringLiteral("Створити...")), dlu(80, 86, 50, 14));
    auto *edit = place(&d, new QPushButton(QStringLiteral("Змінити...")), dlu(134, 86, 50, 14));
    auto *del = place(&d, new QPushButton(QStringLiteral("Видалити")), dlu(188, 86, 50, 14));
    place(&d, new QGroupBox(QStringLiteral("Системні змінні")), dlu(7, 113, 238, 104));
    auto *sys = makeList(dlu(14, 125, 224, 85));
    for (QGroupBox *g : d.findChildren<QGroupBox *>()) {
        g->lower();
    }
    auto *ok = place(&d, new QPushButton(QStringLiteral("OK")), dlu(141, 224, 50, 14));
    auto *cancel = place(&d, new QPushButton(QStringLiteral("Скасувати")), dlu(195, 224, 50, 14));
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
        e.setWindowTitle(it ? QStringLiteral("Змінення змінної користувача") : QStringLiteral("Нова змінна користувача"));
        e.setFixedSize(dlu(0, 0, 230, 68).size());
        text(&e, QStringLiteral("Ім'я змінної:"), dlu(7, 9, 60, 8));
        auto *n = place(&e, new QLineEdit(it ? it->text(0) : QString()), dlu(70, 7, 153, 12));
        text(&e, QStringLiteral("Значення змінної:"), dlu(7, 27, 60, 8));
        auto *v = place(&e, new QLineEdit(it ? it->text(1) : QString()), dlu(70, 25, 153, 12));
        n->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*")), n));
        auto *okb = place(&e, new QPushButton(QStringLiteral("OK")), dlu(119, 47, 50, 14));
        auto *cb = place(&e, new QPushButton(QStringLiteral("Скасувати")), dlu(173, 47, 50, 14));
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
    text(p, QStringLiteral("Щоб внести більшість цих змін, потрібні права адміністратора."), dlu(10, 8, 240, 12));
    struct Box {
        QString title, desc;
        int y;
        std::function<void()> open;
    };
    const QList<Box> boxes{
        {QStringLiteral("Швидкодія"), QStringLiteral("Візуальні ефекти, використання процесора та пам'яті"), 22,
         [] {
             QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--visualfx")});
         }},
        {QStringLiteral("Профілі користувачів"), QStringLiteral("Параметри робочого столу, пов'язані з входом у систему"), 79,
         [] {
             QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--manage-users")});
         }},
        {QStringLiteral("Завантаження й відновлення"), QStringLiteral("Вибір операційної системи під час завантаження та час показу списку"), 136,
         [] {
             launch(QStringLiteral("systemsettings"), {QStringLiteral("kcm_grub2")});
         }},
    };
    for (const Box &b : boxes) {
        place(p, new QGroupBox(b.title), dlu(7, b.y, 242, 53))->lower();
        text(p, b.desc, dlu(14, b.y + 12, 226, 20));
        auto *btn = place(p, new QPushButton(QStringLiteral("Параметри...")), dlu(193, b.y + 32, 49, 14));
        QObject::connect(btn, &QPushButton::clicked, p, b.open);
        if (b.y == 136 && QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("applications/kcm_grub2.desktop")).isEmpty()
            && !QFileInfo::exists(QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/plugins/plasma/kcms/systemsettings/kcm_grub2.so"))) {
            btn->setEnabled(false);
            btn->setToolTip(QStringLiteral("Потрібен модуль налаштування GRUB (kde-config-grub2)"));
        }
    }
    auto *env = place(p, new QPushButton(QStringLiteral("Змінні середовища...")), dlu(164, 196, 85, 14));
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
        text(this, QStringLiteral("Використовуйте захист системи, щоб скасовувати небажані зміни системи й відновлювати попередні стани системних файлів."),
             dlu(35, 7, 210, 24));
        sectionLine(this, QStringLiteral("Відновлення системи"), 7, 41, 80, 165);
        text(this, QStringLiteral("Скасувати зміни системи можна, повернувши комп'ютер до попередньої точки відновлення."), dlu(10, 58, 140, 24));
        auto *restore = place(this, new QPushButton(QStringLiteral("Відновлення системи...")), dlu(151, 59, 95, 14));
        sectionLine(this, QStringLiteral("Параметри захисту"), 7, 100, 80, 165);
        m_list = place(this, new QTreeWidget, dlu(14, 112, 227, 50));
        m_list->setRootIsDecorated(false);
        m_list->setHeaderLabels({QStringLiteral("Доступні диски"), QStringLiteral("Захист")});
        m_list->setColumnWidth(0, dlu(0, 0, 150, 0).width());
        text(this, QStringLiteral("Налаштування параметрів відновлення, керування місцем на диску й видалення точок відновлення."), dlu(14, 170, 155, 24));
        auto *configure = place(this, new QPushButton(QStringLiteral("Налаштувати...")), dlu(172, 170, 70, 14));
        text(this, QStringLiteral("Негайно створити точку відновлення для дисків, на яких увімкнуто захист системи."), dlu(14, 198, 155, 24));
        m_create = place(this, new QPushButton(QStringLiteral("Створити...")), dlu(172, 198, 70, 14));
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
        auto *it = new QTreeWidgetItem(m_list, {QStringLiteral("Linux (/) (Система)"), on ? QStringLiteral("Увімкнуто") : QStringLiteral("Вимкнуто")});
        it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-drive-system"), QIcon::fromTheme(QStringLiteral("drive-harddisk"))));
        m_create->setEnabled(on);
    }

    /** "System Protection for Linux (/)" (srrstr.dll 1240) */
    void configureDialog()
    {
        QDialog d(this);
        d.setWindowTitle(QStringLiteral("Захист системи для Linux (/)"));
        d.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        d.setFixedSize(dlu(0, 0, 270, 200).size());
        sectionLine(&d, QStringLiteral("Параметри відновлення"), 7, 4, 90, 170);
        text(&d, QStringLiteral("Захист системи може зберігати копії системних параметрів і файлів. Виберіть, що потрібно мати змогу відновлювати:"), dlu(14, 16, 240, 16));
        auto *onRb = place(&d, new QRadioButton(QStringLiteral("Відновлювати системні параметри й файли")), dlu(24, 36, 230, 10));
        auto *offRb = place(&d, new QRadioButton(QStringLiteral("Вимкнути захист системи")), dlu(24, 50, 230, 10));
        (protectionOn() ? onRb : offRb)->setChecked(true);
        sectionLine(&d, QStringLiteral("Використання місця на диску"), 7, 70, 110, 150);
        const QList<Win7Backup::Snapshot> points = Win7Backup::snapshotsIn(QString());
        text(&d, QStringLiteral("Точки відновлення зберігаються на системному диску; найстаріші видаляються автоматично (зберігаються 5 щоденних)."),
             dlu(14, 82, 246, 20));
        text(&d, QStringLiteral("Точок відновлення: %1").arg(points.size()), dlu(14, 106, 200, 8));
        text(&d, QStringLiteral("Видалити всі точки відновлення (включно із системними параметрами й попередніми версіями файлів)."), dlu(14, 124, 176, 30));
        auto *del = place(&d, new QPushButton(QStringLiteral("Видалити")), dlu(210, 127, 50, 14));
        del->setEnabled(!points.isEmpty());
        auto *sep = place(&d, new QFrame, dlu(7, 168, 256, 1));
        sep->setFrameShape(QFrame::HLine);
        auto *ok = place(&d, new QPushButton(QStringLiteral("OK")), dlu(99, 178, 50, 14));
        auto *cancel = place(&d, new QPushButton(QStringLiteral("Скасувати")), dlu(154, 178, 50, 14));
        auto *apply = place(&d, new QPushButton(QStringLiteral("Застосувати")), dlu(209, 178, 50, 14));
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
            if (QMessageBox::question(&d, QStringLiteral("Захист системи"), QStringLiteral("Видалення точок відновлення на цьому диску неможливо скасувати. Продовжити?"))
                != QMessageBox::Yes) {
                return;
            }
            if (Win7Backup::runTimeshift(&d, QStringLiteral("Захист системи"), QStringLiteral("Видалення точок відновлення..."),
                                         {QStringLiteral("--delete-all"), QStringLiteral("--scripted")})) {
                QMessageBox::information(&d, QStringLiteral("Захист системи"), QStringLiteral("Точки відновлення успішно видалено."));
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
        d.setWindowTitle(QStringLiteral("Захист системи"));
        d.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        d.setFixedSize(dlu(0, 0, 270, 110).size());
        auto *t = text(&d, QStringLiteral("Створення точки відновлення"), dlu(7, 7, 254, 14));
        QFont f = t->font();
        f.setPixelSize(15);
        t->setFont(f);
        t->setStyleSheet(QStringLiteral("color: #1e3287;"));
        text(&d, QStringLiteral("Введіть опис, щоб легше було знайти точку відновлення. Поточні дата й час додаються автоматично."), dlu(7, 25, 259, 23));
        auto *desc = place(&d, new QLineEdit, dlu(7, 53, 256, 14));
        auto *sep = place(&d, new QFrame, dlu(0, 80, 271, 1));
        sep->setFrameShape(QFrame::HLine);
        auto *create = place(&d, new QPushButton(QStringLiteral("Створити")), dlu(158, 88, 50, 14));
        auto *cancel = place(&d, new QPushButton(QStringLiteral("Скасувати")), dlu(212, 88, 50, 14));
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
        if (Win7Backup::runTimeshift(this, QStringLiteral("Захист системи"), QStringLiteral("Створення точки відновлення..."),
                                     {QStringLiteral("--create"), QStringLiteral("--scripted"), QStringLiteral("--comments"), desc->text().trimmed()})) {
            QMessageBox::information(this, QStringLiteral("Захист системи"), QStringLiteral("Точку відновлення успішно створено."));
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
        setWindowTitle(QStringLiteral("Властивості системи"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cps-system")));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_DeleteOnClose);
        const QSize page = dlu(0, 0, 256, 226).size();
        setFixedSize(page.width() + 26, page.height() + 76);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(8, 8, 8, 8);
        auto *tabs = new QTabWidget;
        m_name = new ComputerNamePage;
        tabs->addTab(m_name, QStringLiteral("Ім'я комп'ютера"));
        tabs->addTab(hardwarePage(), QStringLiteral("Устаткування"));
        tabs->addTab(advancedPage(this), QStringLiteral("Додатково"));
        tabs->addTab(new ProtectionPage, QStringLiteral("Захист системи"));
        tabs->setCurrentIndex(qBound(0, tab, tabs->count() - 1));
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
