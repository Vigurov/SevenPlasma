/*
 * "Disk Cleanup" (cleanmgr.exe) of Windows 7 for the drive Linux runs from ("C:").
 *
 *   Temporary Internet Files -> browser caches (Chrome, Chromium, Firefox), skipped while the browser runs
 *   Temporary files          -> ~/.cache and the user's files in /tmp not changed for 7 days
 *   Thumbnails               -> ~/.cache/thumbnails
 *   Recycle Bin              -> ktrash6 --empty
 *   Error reports            -> the user's reports in /var/crash
 * "Clean up system files" adds what needs the administrator password (one pkexec at the end):
 *   System error reports, old system logs (archived journals, rotated logs), downloaded update
 *   packages (apt and snap caches), previous versions of programs (disabled snap revisions,
 *   packages apt no longer needs).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7chrome.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDirIterator>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTreeWidget>

#include <unistd.h>

namespace Win7Cleanup
{
struct Category {
    QString key;
    QString title;
    QString icon;
    QString description;
    bool system = false;
    bool checked = false;
    qint64 size = 0;
    QStringList files; // what gets deleted (user categories)
    QString view;      // folder "View Files" opens
    QString note;      // why it can't be cleaned right now
};

/** "0 байт", "996 байт", "55,8 КБ", "1,13 ГБ" like Disk Cleanup */
inline QString sizeText(qint64 bytes)
{
    const QLocale uk;
    if (bytes < 1024) {
        return tr7("%1 bytes").arg(bytes);
    }
    double v = bytes / 1024.0;
    QString unit = tr7("KB");
    for (const QString &u : {tr7("MB"), tr7("GB")}) {
        if (v < 1000) {
            break;
        }
        v /= 1024.0;
        unit = u;
    }
    return uk.toString(v, 'f', v < 10 ? 2 : v < 100 ? 1 : 0) + QLatin1Char(' ') + unit;
}

inline bool running(const QStringList &names)
{
    for (const QString &n : names) {
        if (QProcess::execute(QStringLiteral("pgrep"), {QStringLiteral("-x"), n}) == 0) {
            return true;
        }
    }
    return false;
}

/** walks a folder; `keep` decides per file; calls `tick` now and then so the window stays alive */
inline qint64 collect(const QString &dir, QStringList *files, const std::function<bool(const QFileInfo &)> &keep, const std::function<bool()> &tick)
{
    qint64 total = 0;
    int n = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (keep && !keep(fi)) {
            continue;
        }
        total += fi.size();
        if (files) {
            files->append(fi.absoluteFilePath());
        }
        if (++n % 400 == 0 && tick && !tick()) {
            break;
        }
    }
    return total;
}

inline QStringList browserCaches()
{
    const QString home = QDir::homePath();
    return {home + QStringLiteral("/.cache/google-chrome"), home + QStringLiteral("/.cache/chromium"), home + QStringLiteral("/.cache/mozilla"),
            home + QStringLiteral("/snap/firefox/common/.cache"), home + QStringLiteral("/snap/chromium/common/.cache")};
}

/** the size of what `apt-get autoremove` would remove */
inline qint64 autoremoveSize()
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("apt-get"), {QStringLiteral("-s"), QStringLiteral("autoremove")});
    p.waitForFinished(30000);
    QStringList pkgs;
    for (const QString &l : QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'))) {
        if (l.startsWith(QLatin1String("Remv "))) {
            pkgs.append(l.section(QLatin1Char(' '), 1, 1));
        }
    }
    if (pkgs.isEmpty()) {
        return 0;
    }
    QProcess q;
    q.start(QStringLiteral("dpkg-query"), QStringList{QStringLiteral("-W"), QStringLiteral("-f=${Installed-Size}\\n")} + pkgs);
    q.waitForFinished(10000);
    qint64 kb = 0;
    for (const QString &l : QString::fromUtf8(q.readAllStandardOutput()).split(QLatin1Char('\n'))) {
        kb += l.trimmed().toLongLong();
    }
    return kb * 1024;
}

/** disabled snap revisions: /var/lib/snapd/snaps/NAME_REV.snap */
inline qint64 oldSnapsSize()
{
    QProcess p;
    p.start(QStringLiteral("snap"), {QStringLiteral("list"), QStringLiteral("--all")});
    p.waitForFinished(15000);
    qint64 total = 0;
    for (const QString &l : QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'))) {
        const QStringList c = l.simplified().split(QLatin1Char(' '));
        if (c.size() >= 3 && l.contains(QLatin1String("disabled"))) {
            total += QFileInfo(QStringLiteral("/var/lib/snapd/snaps/%1_%2.snap").arg(c.at(0), c.at(2))).size();
        }
    }
    return total;
}

/** fills the sizes; returns false when cancelled */
inline bool scan(QList<Category> &cats, bool system, const std::function<bool(const QString &)> &progress)
{
    const QString home = QDir::homePath();
    const QDateTime weekAgo = QDateTime::currentDateTime().addDays(-7);
    const QStringList browsers = browserCaches();
    const QString thumbs = home + QStringLiteral("/.cache/thumbnails");
    cats.clear();
    auto add = [&cats](Category c) -> Category & {
        cats.append(c);
        return cats.last();
    };
    bool alive = true;
    auto tick = [&alive, &progress](const QString &what) {
        return [&alive, &progress, what] {
            alive = alive && progress(what);
            return alive;
        };
    };

    Category &net = add({QStringLiteral("internet"), tr7("Temporary Internet Files"), QStringLiteral("seven-clean-internet"),
                         tr7("The Temporary Internet Files folder contains Web pages stored on your hard disk by web browsers (Chrome, Firefox) for quick viewing. Your personalized settings for Web pages will be left intact."),
                         false, true});
    net.view = home + QStringLiteral("/.cache");
    if (running({QStringLiteral("chrome"), QStringLiteral("chromium"), QStringLiteral("firefox"), QStringLiteral("firefox-bin")})) {
        net.checked = false;
        net.note = tr7("Close the web browser to clean up these files.");
    }
    for (const QString &d : browsers) {
        net.size += collect(d, net.note.isEmpty() ? &net.files : nullptr, {}, tick(net.title));
    }
    if (!alive) {
        return false;
    }

    Category &tmp = add({QStringLiteral("temp"), tr7("Temporary files"), QStringLiteral("seven-clean-temp"),
                         tr7("Programs sometimes store temporary information in cache and TEMP folders. Before a program closes, it usually deletes this information.\n\nYou can safely delete temporary files that have not been modified in over a week."),
                         false, false});
    tmp.view = home + QStringLiteral("/.cache");
    const uint me = getuid();
    tmp.size = collect(home + QStringLiteral("/.cache"), &tmp.files,
                       [&](const QFileInfo &fi) {
                           const QString p = fi.absoluteFilePath();
                           if (p.startsWith(thumbs)) {
                               return false;
                           }
                           for (const QString &b : browsers) {
                               if (p.startsWith(b)) {
                                   return false;
                               }
                           }
                           return fi.lastModified() < weekAgo;
                       },
                       tick(tmp.title));
    tmp.size += collect(QStringLiteral("/tmp"), &tmp.files,
                        [&](const QFileInfo &fi) {
                            return fi.ownerId() == me && fi.lastModified() < weekAgo;
                        },
                        tick(tmp.title));
    if (!alive) {
        return false;
    }

    Category &th = add({QStringLiteral("thumbs"), tr7("Thumbnails"), QStringLiteral("seven-clean-thumbs"),
                        tr7("Windows keeps a copy of all of your picture, video, and document thumbnails so they can be displayed quickly when you open a folder. If you delete these thumbnails, they will be automatically recreated as needed."),
                        false, true});
    th.view = thumbs;
    th.size = collect(thumbs, &th.files, {}, tick(th.title));

    const QString trashDir = home + QStringLiteral("/.local/share/Trash");
    Category &trash = add({QStringLiteral("trash"), tr7("Recycle Bin"), QStringLiteral("seven-clean-trash-full"),
                           tr7("The Recycle Bin contains files you have deleted from your computer. These files are not permanently removed until you empty the Recycle Bin."), false, false});
    trash.view = QStringLiteral("trash:/");
    trash.size = collect(trashDir + QStringLiteral("/files"), nullptr, {}, tick(trash.title));
    if (trash.size == 0) {
        trash.icon = QStringLiteral("seven-clean-trash-empty");
    }

    Category &rep = add({QStringLiteral("reports"), tr7("Error report files"), QStringLiteral("seven-clean-reports"),
                         tr7("Files created when programs stopped working. They are used to find the causes of errors; deleting them does not affect how programs work."),
                         false, false});
    rep.view = QStringLiteral("/var/crash");
    rep.size = collect(QStringLiteral("/var/crash"), &rep.files, [me](const QFileInfo &fi) { return fi.ownerId() == me; }, tick(rep.title));
    if (!alive) {
        return false;
    }

    if (!system) {
        return true;
    }
    Category &srep = add({QStringLiteral("sysreports"), tr7("System error report files"), QStringLiteral("seven-clean-reports"),
                          tr7("Reports of system services and other users' programs that stopped working."), true, false});
    srep.view = QStringLiteral("/var/crash");
    srep.size = collect(QStringLiteral("/var/crash"), nullptr, [me](const QFileInfo &fi) { return fi.ownerId() != me; }, tick(srep.title));

    Category &logs = add({QStringLiteral("logs"), tr7("Old event log files"), QStringLiteral("seven-clean-logs"),
                          tr7("Archived system logs and old copies of log files. The current log is left unchanged."), true, false});
    logs.view = QStringLiteral("/var/log");
    static const QRegularExpression rotated(QStringLiteral("(\\.gz|\\.xz|\\.old|\\.\\d+)$"));
    logs.size = collect(QStringLiteral("/var/log"), nullptr,
                        [](const QFileInfo &fi) {
                            const QString n = fi.fileName();
                            return (n.endsWith(QLatin1String(".journal")) && n.contains(QLatin1Char('@'))) || n.endsWith(QLatin1String(".journal~")) || rotated.match(n).hasMatch();
                        },
                        tick(logs.title));

    Category &upd = add({QStringLiteral("updates"), tr7("Downloaded update files"), QStringLiteral("seven-clean-updates"),
                         tr7("Program and update packages that are already installed. They are not needed after installation: they are downloaded again if needed."), true, true});
    upd.view = QStringLiteral("/var/cache/apt/archives");
    upd.size = collect(QStringLiteral("/var/cache/apt/archives"), nullptr, [](const QFileInfo &fi) { return fi.suffix() == QLatin1String("deb"); }, tick(upd.title));
    upd.size += collect(QStringLiteral("/var/lib/snapd/cache"), nullptr, {}, tick(upd.title));
    if (!alive || !progress(tr7("Previous versions of programs"))) {
        return false;
    }

    Category &old = add({QStringLiteral("old"), tr7("Previous versions of programs"), QStringLiteral("seven-appwiz"),
                         tr7("Old versions of programs kept after an update (snap), and packages no longer needed by any program (for example, old system kernels)."),
                         true, false});
    old.size = oldSnapsSize() + autoremoveSize();
    return alive;
}

/** the root part of the cleanup, run once through pkexec */
inline QString systemScript(const QList<Category> &cats)
{
    QStringList steps{QStringLiteral("export LC_ALL=C DEBIAN_FRONTEND=noninteractive")};
    for (const Category &c : cats) {
        if (!c.system || !c.checked) {
            continue;
        }
        if (c.key == QLatin1String("sysreports")) {
            steps << QStringLiteral("rm -f /var/crash/*");
        } else if (c.key == QLatin1String("logs")) {
            steps << QStringLiteral("journalctl --rotate; journalctl --vacuum-time=1s")
                  << QStringLiteral("find /var/log -type f \\( -name '*.gz' -o -name '*.xz' -o -name '*.old' -o -regex '.*\\.[0-9]+' \\) -delete");
        } else if (c.key == QLatin1String("updates")) {
            steps << QStringLiteral("apt-get clean") << QStringLiteral("rm -f /var/lib/snapd/cache/*");
        } else if (c.key == QLatin1String("old")) {
            steps << QStringLiteral("snap list --all | awk '/disabled/{print $1, $3}' | while read n r; do snap remove \"$n\" --revision=\"$r\"; done")
                  << QStringLiteral("apt-get -y autoremove");
        }
    }
    return steps.size() > 1 ? steps.join(QStringLiteral("; ")) : QString();
}

/** small window with a progress bar used while counting and while cleaning */
class ProgressDialog : public QDialog
{
public:
    ProgressDialog(const QString &title, const QString &text, const QString &label, bool cancellable)
    {
        setWindowTitle(title);
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cleanmgr")));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setFixedWidth(360);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(12, 12, 12, 10);
        auto *top = new QHBoxLayout;
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cleanmgr")).pixmap(32, 32));
        top->addWidget(ic, 0, Qt::AlignTop);
        auto *t = new QLabel(text);
        t->setWordWrap(true);
        top->addWidget(t, 1);
        v->addLayout(top);
        v->addSpacing(4);
        v->addWidget(new QLabel(label));
        auto *row = new QHBoxLayout;
        bar = new QProgressBar;
        bar->setTextVisible(false);
        bar->setFixedHeight(14);
        row->addWidget(bar, 1);
        if (cancellable) {
            auto *cancel = new QPushButton(tr7("Cancel"));
            row->addWidget(cancel);
            connect(cancel, &QPushButton::clicked, this, [this] { cancelled = true; });
        }
        v->addLayout(row);
        status = new QLabel;
        v->addWidget(status);
    }
    QProgressBar *bar;
    QLabel *status;
    bool cancelled = false;

protected:
    void reject() override
    {
        cancelled = true;
    }
};

class CleanupDialog : public QDialog
{
public:
    explicit CleanupDialog(bool system)
        : m_system(system)
    {
        setWindowTitle(tr7("Disk Cleanup for  (C:)"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cleanmgr")));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_DeleteOnClose);
        setFixedSize(400, 470);
    }

    /** counts first, like cleanmgr.exe; false when the user cancelled */
    bool prepare()
    {
        ProgressDialog wait(tr7("Disk Cleanup"),
                            tr7("Disk Cleanup is calculating how much space you will be able to free on  (C:). This may take a few minutes to complete."),
                            tr7("Calculating..."), true);
        wait.bar->setRange(0, m_system ? 9 : 5);
        wait.show();
        QString last;
        const bool ok = scan(m_cats, m_system, [&wait, &last](const QString &what) {
            if (what != last) {
                last = what;
                wait.bar->setValue(wait.bar->value() + 1);
                wait.status->setText(tr7("Scanning:  %1").arg(what));
            }
            QCoreApplication::processEvents();
            return !wait.cancelled;
        });
        wait.hide();
        if (!ok) {
            return false;
        }
        build();
        return true;
    }

private:
    void build()
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 9, 7, 9);
        auto *tabs = new QTabWidget;
        tabs->addTab(cleanupTab(), tr7("Disk Cleanup"));
        if (m_system) {
            tabs->addTab(moreTab(), tr7("More Options"));
        }
        v->addWidget(tabs, 1);
        auto *row = new QHBoxLayout;
        row->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        for (QPushButton *b : {ok, cancel}) {
            b->setMinimumWidth(75);
            row->addWidget(b);
        }
        ok->setDefault(true);
        v->addLayout(row);
        connect(ok, &QPushButton::clicked, this, [this] { clean(); });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    }

    QWidget *cleanupTab()
    {
        auto *w = new QWidget;
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(10, 10, 10, 8);
        v->setSpacing(6);
        auto *top = new QHBoxLayout;
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-clean-drive")).pixmap(32, 32));
        top->addWidget(ic, 0, Qt::AlignTop);
        qint64 all = 0;
        for (const Category &c : std::as_const(m_cats)) {
            all += c.note.isEmpty() ? c.size : 0;
        }
        auto *intro = new QLabel(tr7("You can use Disk Cleanup to free up to %1 of disk space on  (C:).").arg(sizeText(all)));
        intro->setWordWrap(true);
        top->addWidget(intro, 1);
        v->addLayout(top);
        v->addWidget(new QLabel(tr7("Files to delete:")));
        m_list = new QTreeWidget;
        m_list->setHeaderHidden(true);
        m_list->setRootIsDecorated(false);
        m_list->setColumnCount(2);
        m_list->header()->setStretchLastSection(false);
        m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_list->setFixedHeight(96);
        for (int i = 0; i < m_cats.size(); ++i) {
            const Category &c = m_cats.at(i);
            auto *it = new QTreeWidgetItem(m_list, {c.title, sizeText(c.size)});
            it->setIcon(0, QIcon::fromTheme(c.icon));
            it->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            it->setData(0, Qt::UserRole, i);
            if (c.note.isEmpty()) {
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
                it->setCheckState(0, c.checked ? Qt::Checked : Qt::Unchecked);
            } else {
                it->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                it->setCheckState(0, Qt::Unchecked);
                it->setForeground(0, QColor(0x80, 0x80, 0x80));
                it->setToolTip(0, c.note);
            }
        }
        v->addWidget(m_list);
        auto *totalRow = new QHBoxLayout;
        totalRow->addWidget(new QLabel(tr7("Total amount of disk space you gain:")));
        totalRow->addStretch(1);
        m_total = new QLabel;
        totalRow->addWidget(m_total);
        v->addLayout(totalRow);
        auto *box = new QGroupBox(tr7("Description"));
        auto *bv = new QVBoxLayout(box);
        m_desc = new QLabel;
        m_desc->setWordWrap(true);
        m_desc->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        bv->addWidget(m_desc, 1);
        auto *buttons = new QHBoxLayout;
        if (!m_system) {
            auto *sys = new QPushButton(QIcon::fromTheme(QStringLiteral("seven-cp-shield")), tr7("Clean up system files"));
            buttons->addWidget(sys);
            connect(sys, &QPushButton::clicked, this, [this] {
                QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--cleanmgr"), QStringLiteral("system")});
                reject();
            });
        }
        buttons->addStretch(1);
        m_view = new QPushButton(tr7("View Files"));
        buttons->addWidget(m_view);
        bv->addLayout(buttons);
        v->addWidget(box, 1);
        v->addWidget(new Link(tr7("How does Disk Cleanup work?"), [this] {
            QMessageBox::information(this, tr7("Disk Cleanup"),
                                     tr7("Disk Cleanup looks for files on the drive that can be safely deleted: temporary files, browser caches, thumbnails, the contents of the Recycle Bin and so on.\n\nSelect the categories to delete and click OK. The \"Clean up system files\" button adds system files to the list (the administrator password is needed)."));
        }));
        connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it) {
            m_cats[it->data(0, Qt::UserRole).toInt()].checked = it->checkState(0) == Qt::Checked;
            updateTotal();
        });
        connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
            if (!it) {
                return;
            }
            const Category &c = m_cats.at(it->data(0, Qt::UserRole).toInt());
            m_desc->setText(c.note.isEmpty() ? c.description : c.description + QStringLiteral("\n\n") + c.note);
            m_view->setEnabled(!c.view.isEmpty());
        });
        connect(m_view, &QPushButton::clicked, this, [this] {
            QTreeWidgetItem *it = m_list->currentItem();
            if (it) {
                launch(QStringLiteral("dolphin"), {m_cats.at(it->data(0, Qt::UserRole).toInt()).view});
            }
        });
        m_list->setCurrentItem(m_list->topLevelItem(0));
        updateTotal();
        return w;
    }

    QWidget *moreTab()
    {
        auto *w = new QWidget;
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(10, 10, 10, 8);
        auto *box = new QGroupBox(tr7("Programs and Features"));
        auto *g = new QGridLayout(box);
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-appwiz")).pixmap(32, 32));
        g->addWidget(ic, 0, 0, 2, 1, Qt::AlignTop);
        auto *t = new QLabel(tr7("You can free more disk space by removing programs that you do not use."));
        t->setWordWrap(true);
        g->addWidget(t, 0, 1);
        auto *b = new QPushButton(tr7("Clean up..."));
        g->addWidget(b, 1, 1, Qt::AlignRight);
        v->addWidget(box);
        v->addStretch(1);
        connect(b, &QPushButton::clicked, this, [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--programs")}); });
        return w;
    }

    void updateTotal()
    {
        qint64 sum = 0;
        for (const Category &c : std::as_const(m_cats)) {
            sum += c.checked && c.note.isEmpty() ? c.size : 0;
        }
        m_total->setText(sizeText(sum));
    }

    void clean()
    {
        QList<Category> chosen;
        for (const Category &c : std::as_const(m_cats)) {
            if (c.checked && c.note.isEmpty()) {
                chosen.append(c);
            }
        }
        if (chosen.isEmpty()) {
            accept();
            return;
        }
        QMessageBox ask(QMessageBox::Question, tr7("Disk Cleanup"), tr7("Are you sure you want to permanently delete these files?"), QMessageBox::NoButton, this);
        QPushButton *del = ask.addButton(tr7("Delete Files"), QMessageBox::AcceptRole);
        ask.addButton(tr7("Cancel"), QMessageBox::RejectRole);
        ask.exec();
        if (ask.clickedButton() != del) {
            return;
        }
        hide();
        ProgressDialog work(tr7("Disk Cleanup"), tr7("The Disk Cleanup utility is cleaning up unnecessary files on your machine."), QString(), false);
        work.setWindowTitle(tr7("Disk Cleanup"));
        work.bar->setRange(0, chosen.size() + 1);
        work.show();
        int step = 0;
        for (const Category &c : std::as_const(chosen)) {
            work.status->setText(tr7("Disk Cleanup:  %1").arg(c.title));
            work.bar->setValue(step++);
            QCoreApplication::processEvents();
            if (c.system) {
                continue;
            }
            if (c.key == QLatin1String("trash")) {
                QProcess::execute(QStringLiteral("ktrash6"), {QStringLiteral("--empty")});
                continue;
            }
            int n = 0;
            for (const QString &f : c.files) {
                QFile::remove(f);
                if (++n % 300 == 0) {
                    QCoreApplication::processEvents();
                }
            }
        }
        const QString script = systemScript(chosen);
        if (!script.isEmpty()) {
            work.status->setText(tr7("Disk Cleanup:  system files"));
            QCoreApplication::processEvents();
            QProcess p;
            p.start(QStringLiteral("pkexec"), {QStringLiteral("/bin/sh"), QStringLiteral("-c"), script});
            while (!p.waitForFinished(100)) {
                QCoreApplication::processEvents();
            }
        }
        work.bar->setValue(work.bar->maximum());
        accept();
    }

    bool m_system;
    QList<Category> m_cats;
    QTreeWidget *m_list = nullptr;
    QLabel *m_total = nullptr;
    QLabel *m_desc = nullptr;
    QPushButton *m_view = nullptr;
};
} // namespace Win7Cleanup
