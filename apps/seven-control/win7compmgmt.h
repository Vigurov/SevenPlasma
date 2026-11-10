/*
 * Computer Management (compmgmt.msc): one MMC console with System Tools (Task Scheduler, Event Viewer,
 * Shared Folders, Local Users and Groups, Performance, Device Manager), Storage (Disk Management) and
 * Services and Applications (Services). Built on the event viewer's console (tree, middle, action pane);
 * Disk Management is the same widget as diskmgmt.msc, moved into the middle pane.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "../seven-devmgr/devmgrview.h"
#include "win7diskmgmt.h"
#include "win7eventvwr.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Win7Compmgmt
{

enum NodeId { Root = 1, Tools, Scheduler, Shares, Users, UsersFolder, GroupsFolder, Perf, DevMgr, Storage, Disks, ServApps, Services };

inline QString run(const QString &program, const QStringList &args)
{
    QProcess p;
    p.start(program, args);
    p.waitForFinished(8000);
    return QString::fromUtf8(p.readAllStandardOutput());
}

/** a white list like the MMC result pane, with the Explorer-style header */
inline QTreeWidget *resultList(const QStringList &columns, const QList<int> &widths)
{
    auto *t = new QTreeWidget;
    t->setRootIsDecorated(false);
    t->setUniformRowHeights(true);
    t->setHeader(new Win7Disk::Win7Header(t));
    t->setHeaderLabels(columns);
    t->setFrameShape(QFrame::NoFrame);
    t->setSortingEnabled(true);
    t->setTextElideMode(Qt::ElideRight);
    t->setStyleSheet(QStringLiteral("QTreeView { background: white; outline: 0; } QTreeView::item { height: 18px; color: black; }"));
    for (int i = 0; i < widths.size(); ++i) {
        t->setColumnWidth(i, widths.at(i));
    }
    return t;
}

inline QString dateText(qint64 usec)
{
    if (usec <= 0) {
        return QStringLiteral("Ніколи");
    }
    return QDateTime::fromMSecsSinceEpoch(usec / 1000).toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"));
}

class ComputerManagement
{
public:
    ComputerManagement()
    {
        m_ev = new Win7Event::EventViewer(true);
        m_ev->setAttribute(Qt::WA_DeleteOnClose);
        m_ev->setWindowTitle(QStringLiteral("Керування комп'ютером"));
        m_ev->setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-at-compmgmt")));
        m_ev->resize(1080, 700);

        // the console tree of compmgmt.msc
        QTreeWidget *tree = m_ev->tree();
        m_root = m_ev->addHostNode(nullptr, QStringLiteral("Керування комп'ютером (локальний)"), QStringLiteral("seven-at-compmgmt"), Root);
        auto *tools = m_ev->addHostNode(m_root, QStringLiteral("Службові програми"), QStringLiteral("seven-cmp-tools"), Tools);
        m_ev->addHostNode(tools, QStringLiteral("Планувальник завдань"), QStringLiteral("seven-cmp-sched"), Scheduler);
        m_ev->addHostNode(tools, QStringLiteral("Спільні папки"), QStringLiteral("seven-cmp-shares"), Shares);
        auto *users = m_ev->addHostNode(tools, QStringLiteral("Локальні користувачі й групи"), QStringLiteral("seven-cmp-users"), Users);
        m_ev->addHostNode(users, QStringLiteral("Користувачі"), QStringLiteral("seven-cmp-usersfolder"), UsersFolder);
        m_ev->addHostNode(users, QStringLiteral("Групи"), QStringLiteral("seven-cmp-groupsfolder"), GroupsFolder);
        m_ev->addHostNode(tools, QStringLiteral("Продуктивність"), QStringLiteral("seven-cmp-perf"), Perf);
        m_ev->addHostNode(tools, QStringLiteral("Диспетчер пристроїв"), QStringLiteral("seven-cpi-devmgr"), DevMgr);
        auto *storage = m_ev->addHostNode(m_root, QStringLiteral("Запам'ятовувальні пристрої"), QStringLiteral("seven-cmp-storage"), Storage);
        m_ev->addHostNode(storage, QStringLiteral("Керування дисками"), QStringLiteral("seven-at-disks"), Disks);
        auto *servApps = m_ev->addHostNode(m_root, QStringLiteral("Служби й програми"), QStringLiteral("seven-cmp-servapps"), ServApps);
        m_ev->addHostNode(servApps, QStringLiteral("Служби"), QStringLiteral("seven-cmp-services"), Services);
        // Event Viewer sits second under System Tools, as in Windows 7
        m_ev->attachEvents(tools, 1);
        m_ev->eventsRoot()->setExpanded(false);
        m_root->setExpanded(true);
        tools->setExpanded(true);
        storage->setExpanded(true);

        // result pages
        m_children = resultList({QStringLiteral("Ім'я")}, {480});
        m_children->setSortingEnabled(false);
        m_childrenPage = addPage(m_children);
        QObject::connect(m_children, &QTreeWidget::itemDoubleClicked, m_ev, [this](QTreeWidgetItem *it) {
            if (auto *target = static_cast<QTreeWidgetItem *>(it->data(0, Qt::UserRole).value<void *>())) {
                target->parent()->setExpanded(true);
                m_ev->tree()->setCurrentItem(target);
            }
        });
        m_sched = resultList({QStringLiteral("Ім'я"), QStringLiteral("Стан"), QStringLiteral("Запускає"), QStringLiteral("Час наступного запуску"), QStringLiteral("Час останнього запуску")},
                             {200, 70, 200, 150, 150});
        m_schedPage = addPage(m_sched);
        m_shares = resultList({QStringLiteral("Спільний ресурс"), QStringLiteral("Шлях до папки"), QStringLiteral("Тип"), QStringLiteral("Опис")}, {140, 260, 80, 200});
        m_sharesPage = addPage(m_shares);
        m_users = resultList({QStringLiteral("Ім'я"), QStringLiteral("Повне ім'я"), QStringLiteral("Опис")}, {140, 180, 300});
        m_usersPage = addPage(m_users);
        m_groups = resultList({QStringLiteral("Ім'я"), QStringLiteral("Опис")}, {160, 400});
        m_groupsPage = addPage(m_groups);
        m_services = resultList({QStringLiteral("Ім'я"), QStringLiteral("Опис"), QStringLiteral("Стан"), QStringLiteral("Тип запуску"), QStringLiteral("Вхід від імені")},
                                {190, 260, 70, 100, 120});
        m_servicesPage = addPage(m_services);
        QObject::connect(m_services, &QTreeWidget::currentItemChanged, m_ev, [this] { m_ev->setActionGroups(actions(m_ev->tree()->currentItem())); });
        m_perfPage = addPage(launcher(QStringLiteral("seven-cmp-perf"), QStringLiteral("Продуктивність"),
                                      QStringLiteral("Монітор ресурсів показує, як програми використовують процесор, пам'ять, диск і мережу."),
                                      QStringLiteral("Відкрити монітор ресурсів"), [] {
                                          QProcess::startDetached(QDir::homePath() + QStringLiteral("/.local/bin/seven-taskmgr"), {QStringLiteral("--tab"), QStringLiteral("3")});
                                      }));
        // Device Manager: the devmgr snap-in itself, as in Windows 7 (not a link to the separate window)
        m_devices = new Win7DevMgr::DeviceTree;
        m_devPage = addPage(m_devices);

        // Disk Management: the very widget of diskmgmt.msc, with its toolbar buttons
        m_disks = new Win7Disk::DiskManagement;
        m_disks->setParent(m_ev, Qt::Window);
        m_disks->hide();
        m_disksPage = m_ev->middle()->addWidget(m_disks->takeCentralWidget());
        if (QToolBar *bar = m_disks->findChild<QToolBar *>()) {
            int separators = 0;
            for (QAction *a : bar->actions()) {
                if (a->isSeparator()) {
                    ++separators;
                    continue;
                }
                if (separators >= 3) { // after back/forward | console | help, action pane |
                    m_diskActions << a;
                }
            }
        }
        QToolBar *bar = m_ev->toolBar();
        // "Up one level" sits after back/forward in Computer Management
        auto *up = new QAction(Win7Disk::flatIcon(QStringLiteral("seven-mmc-up")), QStringLiteral("Вгору на один рівень"), m_ev);
        QObject::connect(up, &QAction::triggered, m_ev, [this] {
            QTreeWidgetItem *it = m_ev->tree()->currentItem();
            if (it && it->parent()) {
                m_ev->tree()->setCurrentItem(it->parent());
            }
        });
        const QList<QAction *> barActions = bar->actions();
        bar->insertAction(barActions.value(2), up);
        m_diskSeparator = bar->addSeparator();
        bar->addActions(m_diskActions);

        Win7Event::EventViewer::Host host;
        host.open = [this](QTreeWidgetItem *it) { open(it); };
        host.actions = [this](QTreeWidgetItem *it) { return actions(it); };
        m_ev->setHost(host);
        tree->setCurrentItem(m_root);
        open(m_root);
        m_ev->setActionGroups(actions(m_root));
    }

    QMainWindow *window() const
    {
        return m_ev;
    }

private:
    int addPage(QWidget *w)
    {
        auto *page = new QWidget;
        page->setObjectName(QStringLiteral("win7cmppage"));
        page->setAttribute(Qt::WA_StyledBackground, true);
        page->setStyleSheet(QStringLiteral("#win7cmppage { background: white; }"));
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);
        v->addWidget(w);
        return m_ev->middle()->addWidget(page);
    }

    QWidget *launcher(const QString &icon, const QString &title, const QString &text, const QString &link, std::function<void()> action)
    {
        auto *w = new QWidget;
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(16, 14, 16, 14);
        auto *h = new QHBoxLayout;
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(icon).pixmap(32, 32));
        h->addWidget(ic, 0, Qt::AlignTop);
        auto *tv = new QVBoxLayout;
        auto *t = new QLabel(title);
        QFont f = t->font();
        f.setBold(true);
        t->setFont(f);
        tv->addWidget(t);
        auto *d = new QLabel(text);
        d->setWordWrap(true);
        tv->addWidget(d);
        tv->addSpacing(6);
        tv->addWidget(new Link(link, std::move(action)));
        h->addLayout(tv, 1);
        v->addLayout(h);
        v->addStretch(1);
        return w;
    }

    void open(QTreeWidgetItem *it)
    {
        const int id = it->data(0, Win7Event::EventViewer::HostRole).toInt();
        const bool disks = id == Disks;
        for (QAction *a : std::as_const(m_diskActions)) {
            a->setVisible(disks);
        }
        m_diskSeparator->setVisible(disks);
        switch (id) {
        case Scheduler:
            fillScheduler();
            m_ev->middle()->setCurrentIndex(m_schedPage);
            break;
        case Shares:
            fillShares();
            m_ev->middle()->setCurrentIndex(m_sharesPage);
            break;
        case UsersFolder:
            fillUsers();
            m_ev->middle()->setCurrentIndex(m_usersPage);
            break;
        case GroupsFolder:
            fillGroups();
            m_ev->middle()->setCurrentIndex(m_groupsPage);
            break;
        case Perf:
            m_ev->middle()->setCurrentIndex(m_perfPage);
            break;
        case DevMgr:
            m_ev->middle()->setCurrentIndex(m_devPage);
            break;
        case Disks:
            m_ev->middle()->setCurrentIndex(m_disksPage);
            break;
        case Services:
            fillServices();
            m_ev->middle()->setCurrentIndex(m_servicesPage);
            break;
        default: // a folder: its children, like the result pane of compmgmt.msc
            m_children->clear();
            for (int i = 0; i < it->childCount(); ++i) {
                QTreeWidgetItem *c = it->child(i);
                auto *row = new QTreeWidgetItem(m_children, {c->text(0)});
                row->setIcon(0, c->icon(0));
                row->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void *>(c)));
            }
            m_ev->middle()->setCurrentIndex(m_childrenPage);
            break;
        }
    }

    QList<Win7Event::ActionGroup> actions(QTreeWidgetItem *it)
    {
        if (!it) {
            return {};
        }
        const int id = it->data(0, Win7Event::EventViewer::HostRole).toInt();
        QList<Win7Event::ActionItem> items;
        if (id == Disks) {
            for (QAction *a : std::as_const(m_diskActions)) {
                items.append({a->text(), QString(), [a] { a->trigger(); }, a->isEnabled()});
            }
            return {{it->text(0), items}};
        }
        if (id == Services) {
            QTreeWidgetItem *svc = m_services->currentItem();
            QList<Win7Event::ActionItem> more{{QStringLiteral("Оновити"), QStringLiteral("seven-mmc-refresh"), [this] { fillServices(); }}};
            QList<Win7Event::ActionGroup> groups{{QStringLiteral("Служби"), more}};
            if (svc) {
                const QString unit = svc->data(0, Qt::UserRole).toString();
                const bool running = svc->text(2) == QLatin1String("Працює");
                auto ctl = [this, unit](const QString &verb) {
                    return [this, unit, verb] {
                        QProcess::execute(QStringLiteral("pkexec"), {QStringLiteral("systemctl"), verb, unit});
                        fillServices();
                    };
                };
                groups.append({svc->text(0),
                               {{QStringLiteral("Запустити"), QString(), ctl(QStringLiteral("start")), !running},
                                {QStringLiteral("Зупинити"), QString(), ctl(QStringLiteral("stop")), running},
                                {QStringLiteral("Перезапустити"), QString(), ctl(QStringLiteral("restart")), running}}});
            }
            return groups;
        }
        items.append({QStringLiteral("Оновити"), QStringLiteral("seven-mmc-refresh"), [this, it] { open(it); }});
        // MMC's "More Actions ▶"
        Win7Event::ActionItem more{QStringLiteral("Додаткові дії"), QString(), nullptr};
        more.submenu = [this, it](QMenu *menu) {
            menu->addAction(QStringLiteral("Оновити"), m_ev, [this, it] { open(it); });
            menu->addSeparator();
            menu->addAction(QStringLiteral("Довідка"));
        };
        if (id == DevMgr) {
            more.submenu = [this](QMenu *menu) {
                menu->addAction(m_devices->scanAction());
                menu->addSeparator();
                menu->addAction(QStringLiteral("Довідка"));
            };
        }
        if (id == Root || id == Tools || id == Storage || id == ServApps || id == Users || id == DevMgr) {
            items = {more};
        }
        return {{it->text(0), items}};
    }

    void fillScheduler()
    {
        m_sched->clear();
        for (const QString &scope : {QStringLiteral("--system"), QStringLiteral("--user")}) {
            const QJsonArray timers = QJsonDocument::fromJson(run(QStringLiteral("systemctl"), {scope, QStringLiteral("list-timers"), QStringLiteral("--all"),
                                                                                                     QStringLiteral("--output=json"), QStringLiteral("--no-pager")})
                                                                  .toUtf8())
                                          .array();
            for (const QJsonValue &v : timers) {
                const QJsonObject o = v.toObject();
                QString name = o.value(QStringLiteral("unit")).toString();
                name.chop(6); // ".timer"
                const qint64 next = qint64(o.value(QStringLiteral("next")).toDouble());
                auto *it = new QTreeWidgetItem(m_sched, {name, next > 0 ? QStringLiteral("Готово") : QStringLiteral("Вимкнуто"), o.value(QStringLiteral("activates")).toString(),
                                                         dateText(next), dateText(qint64(o.value(QStringLiteral("last")).toDouble()))});
                it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-cmp-sched")));
            }
        }
        m_sched->sortItems(0, Qt::AscendingOrder);
    }

    void fillShares()
    {
        m_shares->clear();
        // Samba user shares (net usershare); Windows also lists its hidden admin shares, Linux has none
        const QString out = run(QStringLiteral("net"), {QStringLiteral("usershare"), QStringLiteral("info")});
        QString name, path, comment;
        auto flush = [&] {
            if (!name.isEmpty()) {
                auto *it = new QTreeWidgetItem(m_shares, {name, path, QStringLiteral("Windows"), comment});
                it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-cmp-shares")));
            }
            name.clear();
            path.clear();
            comment.clear();
        };
        for (const QString &line : out.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1Char('['))) {
                flush();
                name = line.mid(1, line.indexOf(QLatin1Char(']')) - 1);
            } else if (line.startsWith(QLatin1String("path="))) {
                path = line.mid(5);
            } else if (line.startsWith(QLatin1String("comment="))) {
                comment = line.mid(8);
            }
        }
        flush();
    }

    void fillUsers()
    {
        m_users->clear();
        for (const QString &line : run(QStringLiteral("getent"), {QStringLiteral("passwd")}).split(QLatin1Char('\n'))) {
            const QStringList f = line.split(QLatin1Char(':'));
            if (f.size() < 7) {
                continue;
            }
            const int uid = f.at(2).toInt();
            if (uid != 0 && (uid < 1000 || uid >= 65534)) {
                continue;
            }
            const QString full = f.at(4).section(QLatin1Char(','), 0, 0);
            const QString desc = uid == 0 ? QStringLiteral("Вбудований обліковий запис для адміністрування комп'ютера") : QString();
            auto *it = new QTreeWidgetItem(m_users, {f.at(0), full, desc});
            it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-cmp-user")));
        }
    }

    void fillGroups()
    {
        m_groups->clear();
        static const QHash<QString, QString> known{
            {QStringLiteral("sudo"), QStringLiteral("Адміністратори мають повний доступ до комп'ютера")},
            {QStringLiteral("adm"), QStringLiteral("Читання системних журналів")},
            {QStringLiteral("lpadmin"), QStringLiteral("Керування принтерами")},
            {QStringLiteral("users"), QStringLiteral("Користувачі")},
            {QStringLiteral("audio"), QStringLiteral("Доступ до звукових пристроїв")},
            {QStringLiteral("video"), QStringLiteral("Доступ до відеопристроїв")},
            {QStringLiteral("plugdev"), QStringLiteral("Підключення знімних пристроїв")},
            {QStringLiteral("netdev"), QStringLiteral("Керування мережею")},
            {QStringLiteral("sambashare"), QStringLiteral("Надання спільного доступу до папок")},
        };
        for (const QString &line : run(QStringLiteral("getent"), {QStringLiteral("group")}).split(QLatin1Char('\n'))) {
            const QStringList f = line.split(QLatin1Char(':'));
            if (f.size() < 4) {
                continue;
            }
            const int gid = f.at(2).toInt();
            if (!known.contains(f.at(0)) && (gid < 1000 || gid >= 65534)) {
                continue;
            }
            QString desc = known.value(f.at(0));
            if (desc.isEmpty() && !f.at(3).isEmpty()) {
                desc = QStringLiteral("Члени: ") + f.at(3);
            }
            auto *it = new QTreeWidgetItem(m_groups, {f.at(0), desc});
            it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-cmp-group")));
        }
        m_groups->sortItems(0, Qt::AscendingOrder);
    }

    void fillServices()
    {
        const QString keep = m_services->currentItem() ? m_services->currentItem()->data(0, Qt::UserRole).toString() : QString();
        m_services->clear();
        QHash<QString, QString> startup;
        for (const QJsonValue &v : QJsonDocument::fromJson(run(QStringLiteral("systemctl"), {QStringLiteral("list-unit-files"), QStringLiteral("--type=service"),
                                                                                              QStringLiteral("--output=json"), QStringLiteral("--no-pager")})
                                                               .toUtf8())
                                       .array()) {
            const QJsonObject o = v.toObject();
            startup.insert(o.value(QStringLiteral("unit_file")).toString(), o.value(QStringLiteral("state")).toString());
        }
        const QJsonArray units = QJsonDocument::fromJson(run(QStringLiteral("systemctl"), {QStringLiteral("list-units"), QStringLiteral("--type=service"), QStringLiteral("--all"),
                                                                                            QStringLiteral("--output=json"), QStringLiteral("--no-pager")})
                                                             .toUtf8())
                                     .array();
        for (const QJsonValue &v : units) {
            const QJsonObject o = v.toObject();
            const QString unit = o.value(QStringLiteral("unit")).toString();
            if (o.value(QStringLiteral("load")).toString() == QLatin1String("not-found")) {
                continue;
            }
            QString name = unit;
            name.chop(8); // ".service"
            const QString state = startup.value(unit);
            const QString type = state == QLatin1String("enabled") ? QStringLiteral("Автоматично")
                : state == QLatin1String("masked")                 ? QStringLiteral("Вимкнуто")
                                                                   : QStringLiteral("Вручну");
            const bool running = o.value(QStringLiteral("sub")).toString() == QLatin1String("running");
            auto *it = new QTreeWidgetItem(m_services, {name, o.value(QStringLiteral("description")).toString(), running ? QStringLiteral("Працює") : QString(), type,
                                                        QStringLiteral("Локальна система")});
            it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-cmp-services")));
            it->setData(0, Qt::UserRole, unit);
            if (unit == keep) {
                m_services->setCurrentItem(it);
            }
        }
        m_services->sortItems(0, Qt::AscendingOrder);
    }

    Win7Event::EventViewer *m_ev;
    QTreeWidgetItem *m_root;
    QTreeWidget *m_children;
    QTreeWidget *m_sched;
    QTreeWidget *m_shares;
    QTreeWidget *m_users;
    QTreeWidget *m_groups;
    QTreeWidget *m_services;
    Win7DevMgr::DeviceTree *m_devices = nullptr;
    int m_childrenPage, m_schedPage, m_sharesPage, m_usersPage, m_groupsPage, m_servicesPage, m_perfPage, m_devPage, m_disksPage;
    Win7Disk::DiskManagement *m_disks;
    QList<QAction *> m_diskActions;
    QAction *m_diskSeparator;
};

} // namespace Win7Compmgmt
