#include "win7kwin.h"
#include "win7i18n.h"
/*
 * "Windows Task Manager" in the style of Windows 7, showing the real state of this Linux machine:
 * Applications (open windows), Processes, Services (systemd), Performance, Networking, Users.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../seven-common/win7wrapmenubar.h"
#include <KWindowInfo>
#include <KWindowSystem>
#include <KX11Extras>
#include <netwm.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QCheckBox>

#include "../seven-common/win7header.h"
#include <QTreeView>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QUrl>
#include <QDesktopServices>
#include <QSet>
#include <sys/syscall.h>

#include <csignal>
#include <pwd.h>
#include <unistd.h>

namespace
{
/* ---------------- small helpers ---------------- */

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

QString run(const QString &program, const QStringList &args, int timeout = 3000)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(timeout)) {
        p.kill();
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

qint64 meminfo(const QString &text, const char *key)
{
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String(key)) && line.at(qstrlen(key)) == QLatin1Char(':')) {
            return line.mid(qstrlen(key) + 1).trimmed().section(QLatin1Char(' '), 0, 0).toLongLong(); // kB
        }
    }
    return 0;
}

QString userName(uint uid)
{
    static QHash<uint, QString> cache;
    if (!cache.contains(uid)) {
        const passwd *pw = getpwuid(uid);
        cache.insert(uid, pw ? QString::fromLocal8Bit(pw->pw_name) : QString::number(uid));
    }
    return cache.value(uid);
}

QString kbText(qint64 kb)
{
    return QLocale{}.toString(kb) + tr7(" K");
}

QIcon shieldIcon()
{
    return QIcon::fromTheme(QStringLiteral("seven-cp-shield"), QIcon::fromTheme(QStringLiteral("security-medium")));
}

/** Item that sorts by a number stored in UserRole+1 instead of its text */
class NumItem : public QStandardItem
{
public:
    NumItem(const QString &text, double value)
        : QStandardItem(text)
    {
        setData(value, Qt::UserRole + 1);
        setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    bool operator<(const QStandardItem &other) const override
    {
        return data(Qt::UserRole + 1).toDouble() < other.data(Qt::UserRole + 1).toDouble();
    }
};

QTreeView *makeList(QWidget *parent)
{
    auto *view = new QTreeView(parent);
    Win7HeaderView::install(view);
    view->setRootIsDecorated(false);
    view->setUniformRowHeights(true);
    view->setSortingEnabled(true);
    view->setAlternatingRowColors(false);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view->header()->setStretchLastSection(false);
    view->header()->setSectionsMovable(true);
    view->setStyleSheet(QStringLiteral(
        "QTreeView { show-decoration-selected: 1; outline: 0; }"
        "QTreeView::item { height: 17px; color: black; }"
        "QTreeView::item:hover:!selected { background: transparent; }" // Task Manager's lists do not light up under the mouse
        "QTreeView::item:selected:active { background: #3399ff; color: white; }"
        "QTreeView::item:selected:!active { background: #f0f0f0; color: black; }"));
    return view;
}

/** Remembers the selected row (by a key column) across a model refresh */
QString selectedKey(QTreeView *view, int column)
{
    const QModelIndex idx = view->currentIndex();
    return idx.isValid() ? idx.siblingAtColumn(column).data(Qt::UserRole).toString() : QString();
}

void reselect(QTreeView *view, QAbstractItemModel *model, int column, const QString &key)
{
    if (key.isEmpty()) {
        return;
    }
    for (int r = 0; r < model->rowCount(); ++r) {
        const QModelIndex idx = model->index(r, column);
        if (idx.data(Qt::UserRole).toString() == key) {
            view->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            return;
        }
    }
}

/* ---------------- the green meters and graphs ---------------- */

/** "CPU Usage" / "Memory" LED meter */
class Meter : public QWidget
{
public:
    explicit Meter(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(66, 88);
    }
    void setValue(double fraction, const QString &text)
    {
        m_value = qBound(0.0, fraction, 1.0);
        m_text = text;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        const int rows = 20;
        const int top = 6;
        const int bottom = height() - 20;
        const qreal step = qreal(bottom - top) / rows;
        const int lit = qRound(m_value * rows);
        for (int r = 0; r < rows; ++r) {
            const qreal y = bottom - (r + 1) * step;
            const QColor c = r < lit ? QColor(0x00, 0xff, 0x00) : QColor(0x00, 0x60, 0x00);
            for (int side : {0, 1}) {
                const qreal x = side == 0 ? 14 : width() / 2.0 + 1;
                p.fillRect(QRectF(x, y + 1, width() / 2.0 - 15, step - 2), c);
            }
        }
        p.setPen(QColor(0x00, 0xff, 0x00));
        p.drawText(QRect(0, height() - 18, width(), 16), Qt::AlignCenter, m_text);
    }

private:
    double m_value = 0;
    QString m_text;
};

/** "CPU Usage History" style graph */
class Graph : public QWidget
{
public:
    explicit Graph(const QColor &line, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_line(line)
    {
        setMinimumSize(60, 60);
    }
    void addValue(double fraction)
    {
        m_values.append(qBound(0.0, fraction, 1.0));
        while (m_values.size() > 400) {
            m_values.removeFirst();
        }
        m_scroll = (m_scroll + 2) % 12;
        update();
    }
    void setLabels(bool labels)
    {
        m_labels = labels;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        const int left = m_labels ? 46 : 0;
        const QRect area(left, 0, width() - left, height());
        // scrolling dark green grid
        p.setPen(QColor(0x00, 0x80, 0x40));
        for (int x = area.right() - m_scroll; x > area.left(); x -= 12) {
            p.drawLine(x, area.top(), x, area.bottom());
        }
        for (int y = area.bottom(); y > area.top(); y -= 12) {
            p.drawLine(area.left(), y, area.right(), y);
        }
        if (m_labels) {
            // the Networking graph's scale is yellow-orange in Windows 7
            p.setPen(QColor(0xf0, 0xa8, 0x18));
            p.fillRect(QRect(0, 0, left, height()), Qt::black);
            p.drawText(QRect(0, 0, left - 4, 14), Qt::AlignRight | Qt::AlignTop, m_topLabel);
            p.drawText(QRect(0, height() / 2 - 7, left - 4, 14), Qt::AlignRight | Qt::AlignVCenter, m_midLabel);
            p.drawText(QRect(0, height() - 17, left - 4, 15), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0 %"));
            p.setPen(QColor(0xf0, 0xa8, 0x18));
            p.drawLine(left - 1, 0, left - 1, height());
        }
        if (m_values.size() < 2) {
            return;
        }
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        const int n = m_values.size();
        for (int i = 0; i < n; ++i) {
            const qreal x = area.right() - (n - 1 - i) * 2;
            const qreal y = area.bottom() - m_values.at(i) * (area.height() - 2);
            if (x < area.left()) {
                continue;
            }
            if (path.elementCount() == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }
        p.setPen(QPen(m_line, 1));
        p.drawPath(path);
    }

public:
    QString m_topLabel = QStringLiteral("100 %");
    QString m_midLabel = QStringLiteral("50 %");

private:
    QColor m_line;
    QList<double> m_values;
    int m_scroll = 0;
    bool m_labels = false;
};

/* ---------------- process sampling ---------------- */

struct ProcSample {
    qint64 ticks = 0;
    QString name;
};

struct CpuTimes {
    quint64 busy = 0, total = 0;
};

QList<CpuTimes> readCpuTimes()
{
    QList<CpuTimes> out;
    for (const QString &line : readFile(QStringLiteral("/proc/stat")).split(QLatin1Char('\n'))) {
        if (!line.startsWith(QLatin1String("cpu"))) {
            break;
        }
        const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        CpuTimes t;
        for (int i = 1; i < f.size(); ++i) {
            t.total += f.at(i).toULongLong();
        }
        const quint64 idle = f.value(4).toULongLong() + f.value(5).toULongLong();
        t.busy = t.total - idle;
        out.append(t);
    }
    return out; // [0] = all CPUs, then one per logical processor
}

#include "resmon.h"

/* ---------------- main window ---------------- */

class TaskManager : public QMainWindow
{
public:
    TaskManager()
    {
        setWindowTitle(tr7("Windows Task Manager"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("utilities-system-monitor")));

        m_tabs = new QTabWidget(this);
        m_tabs->setDocumentMode(false);
        m_tabs->tabBar()->setExpanding(false);
        m_tabs->tabBar()->setUsesScrollButtons(false);
        // Compact Windows 7 tabs: grey gradient, the selected one white and a bit taller
        m_tabs->setStyleSheet(QStringLiteral(
            "QTabWidget::pane { border: 1px solid #898c95; top: -1px; background: #ffffff; }"
            "QTabBar::tab { padding: 2px 6px; margin-right: -1px; margin-top: 2px; color: black;"
            " border: 1px solid #898c95; border-bottom: none; border-top-left-radius: 2px; border-top-right-radius: 2px;"
            " background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f2f2f2, stop:0.5 #ebebeb, stop:0.51 #dddddd, stop:1 #cfcfcf); }"
            "QTabBar::tab:hover:!selected { border-color: #3c7fb1;"
            " background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #eaf6fd, stop:0.5 #d9f0fc, stop:0.51 #bee6fd, stop:1 #a7d9f5); }"
            "QTabBar::tab:selected { background: #ffffff; margin-top: 0px; padding-bottom: 3px; }"));
        auto *central = new QWidget(this);
        auto *cl = new QVBoxLayout(central);
        cl->setContentsMargins(6, 6, 6, 4);
        cl->addWidget(m_tabs);
        setCentralWidget(central);

        buildApplications();
        buildProcesses();
        buildServices();
        buildPerformance();
        buildNetwork();
        buildUsers();
        buildMenus();

        // Status bar: "Processes: 98 | CPU Usage: 19% | Physical Memory: 55%"
        m_stProcs = new QLabel;
        m_stCpu = new QLabel;
        m_stMem = new QLabel;
        // Fixed sections separated by thin lines, like the Windows 7 status bar
        const QFontMetrics fm(font());
        m_stProcs->setFixedWidth(fm.horizontalAdvance(tr7("Processes: 9999")) + 16);
        m_stCpu->setFixedWidth(fm.horizontalAdvance(tr7("CPU Usage: 100%")) + 16);
        for (QLabel *l : {m_stProcs, m_stCpu, m_stMem}) {
            l->setContentsMargins(4, 0, 4, 0);
        }
        statusBar()->addWidget(m_stProcs);
        statusBar()->addWidget(m_stCpu);
        statusBar()->addWidget(m_stMem, 1);
        statusBar()->setStyleSheet(QStringLiteral("QStatusBar::item { border: none; border-right: 1px solid #c3c3c3; }"));

        connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
            refresh(true);
        });
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this] {
            refresh(false);
        });
        m_timer->start(1000);
        refresh(true);
        // Kvantum draws the right edge of the last tab one pixel past the tab bar; leave room for it
        m_tabs->tabBar()->setMinimumWidth(m_tabs->tabBar()->sizeHint().width() + 2);
        resize(766, 446); // as wide as Windows 7 opens it
    }

private:
    /* ----- Applications ----- */
    void buildApplications()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        m_appModel = new QStandardItemModel(0, 2, this);
        m_appModel->setHorizontalHeaderLabels({tr7("Task"), tr7("Status")});
        m_appView = makeList(page);
        m_appView->setModel(m_appModel);
        m_appView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_appView->header()->resizeSection(1, 90);
        v->addWidget(m_appView);
        auto *buttons = new QHBoxLayout;
        buttons->addStretch(1);
        auto *end = new QPushButton(tr7("End Task"));
        auto *sw = new QPushButton(tr7("Switch To"));
        auto *task = new QPushButton(tr7("New Task..."));
        for (QPushButton *b : {end, sw, task}) {
            buttons->addWidget(b);
        }
        v->addLayout(buttons);
        connect(end, &QPushButton::clicked, this, &TaskManager::endTask);
        connect(sw, &QPushButton::clicked, this, &TaskManager::switchTo);
        connect(task, &QPushButton::clicked, this, &TaskManager::newTask);
        connect(m_appView, &QTreeView::doubleClicked, this, &TaskManager::switchTo);
        connect(m_appView, &QWidget::customContextMenuRequested, this, [this, end](const QPoint &pos) {
            if (!m_appView->indexAt(pos).isValid()) {
                return;
            }
            QMenu menu(this);
            QAction *s = menu.addAction(tr7("Switch To"), this, &TaskManager::switchTo);
            QFont f = s->font();
            f.setBold(true);
            s->setFont(f);
            menu.addAction(tr7("Bring To Front"), this, &TaskManager::switchTo);
            menu.addSeparator();
            menu.addAction(tr7("Minimize"), this, [this] {
                if (const WId w = currentWindow()) {
                    KX11Extras::minimizeWindow(w);
                }
            });
            menu.addSeparator();
            menu.addAction(tr7("End Task"), end, &QPushButton::click);
            menu.addSeparator();
            menu.addAction(tr7("Go To Process"), this, [this] {
                const WId w = currentWindow();
                const int pid = w ? KWindowInfo(w, NET::WMPid).pid() : 0;
                if (pid > 0) {
                    m_tabs->setCurrentWidget(m_procPage);
                    refresh(true);
                    reselect(m_procView, m_procProxy, 0, QString::number(pid));
                }
            });
            menu.exec(m_appView->viewport()->mapToGlobal(pos));
        });
        m_tabs->addTab(page, tr7("Applications"));
    }

    WId currentWindow() const
    {
        const QModelIndex idx = m_appView->currentIndex();
        return idx.isValid() ? WId(idx.siblingAtColumn(0).data(Qt::UserRole).toULongLong()) : 0;
    }

    void refreshApplications()
    {
        const QString key = selectedKey(m_appView, 0);
        m_appModel->removeRows(0, m_appModel->rowCount());
        const WId self = winId();
        for (WId w : KX11Extras::stackingOrder()) {
            KWindowInfo info(w, NET::WMName | NET::WMWindowType | NET::WMState, NET::WM2TransientFor | NET::WM2WindowClass);
            const NET::WindowType type = info.windowType(NET::NormalMask | NET::DialogMask | NET::DockMask | NET::DesktopMask);
            if (w == self || info.hasState(NET::SkipTaskbar) || (type != NET::Normal && type != NET::Dialog && type != NET::Unknown)) {
                continue;
            }
            auto *name = new QStandardItem(KX11Extras::icon(w, 16, 16, true), info.name());
            name->setData(QString::number(w), Qt::UserRole);
            auto *state = new QStandardItem(tr7("Running"));
            m_appModel->insertRow(0, {name, state});
        }
        reselect(m_appView, m_appModel, 0, key);
    }

    void endTask()
    {
        const WId w = currentWindow();
        if (!w) {
            return;
        }
        auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
        if (x11) {
            NETRootInfo root(x11->connection(), NET::CloseWindow);
            root.closeWindowRequest(w);
        }
        QTimer::singleShot(600, this, [this] {
            refreshApplications();
        });
    }

    void switchTo()
    {
        if (const WId w = currentWindow()) {
            KX11Extras::forceActiveWindow(w);
            if (m_minimizeOnUse) {
                showMinimized();
            }
        }
    }

    void newTask()
    {
        QDialog dlg(this);
        dlg.setWindowTitle(tr7("Create New Task"));
        auto *v = new QVBoxLayout(&dlg);
        auto *top = new QHBoxLayout;
        auto *icon = new QLabel;
        icon->setPixmap(QIcon::fromTheme(QStringLiteral("system-run")).pixmap(32, 32));
        top->addWidget(icon, 0, Qt::AlignTop);
        top->addWidget(new QLabel(tr7("Type the name of a program, folder, document, or Internet\nresource, and Windows will open it for you.")), 1);
        v->addLayout(top);
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr7("Open:")));
        auto *edit = new QLineEdit;
        row->addWidget(edit, 1);
        v->addLayout(row);
        auto *buttons = new QDialogButtonBox;
        buttons->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
        buttons->addButton(tr7("Cancel"), QDialogButtonBox::RejectRole);
        auto *browse = buttons->addButton(tr7("Browse..."), QDialogButtonBox::ActionRole);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        connect(browse, &QPushButton::clicked, &dlg, [edit] {
            const QString path = run(QStringLiteral("kdialog"), {QStringLiteral("--getopenfilename"), QDir::homePath()}, 600000).trimmed();
            if (!path.isEmpty()) {
                edit->setText(path);
            }
        });
        v->addWidget(buttons);
        if (dlg.exec() == QDialog::Accepted && !edit->text().trimmed().isEmpty()) {
            const QString cmd = edit->text().trimmed();
            if (QFileInfo::exists(cmd) && !QFileInfo(cmd).isExecutable()) {
                QProcess::startDetached(QStringLiteral("xdg-open"), {cmd});
            } else {
                QProcess::startDetached(QStringLiteral("sh"), {QStringLiteral("-c"), cmd});
            }
        }
    }

    /* ----- Processes ----- */
    void buildProcesses()
    {
        m_procPage = new QWidget;
        auto *v = new QVBoxLayout(m_procPage);
        m_procModel = new QStandardItemModel(0, 5, this);
        m_procModel->setHorizontalHeaderLabels({tr7("Image Name"), tr7("User Name"), tr7("CPU"),
                                                tr7("Memory (Private Working Set)"), tr7("Description")});
        m_procProxy = new QSortFilterProxyModel(this);
        m_procProxy->setSourceModel(m_procModel);
        m_procProxy->setSortRole(Qt::UserRole + 1);
        m_procView = makeList(m_procPage);
        m_procView->setModel(m_procProxy);
        m_procView->sortByColumn(0, Qt::AscendingOrder);
        const int widths[] = {96, 76, 30, 108, 150};
        for (int c = 0; c < 5; ++c) {
            m_procView->header()->resizeSection(c, widths[c]);
        }
        m_procView->header()->setSectionResizeMode(4, QHeaderView::Stretch);
        v->addWidget(m_procView);
        auto *buttons = new QHBoxLayout;
        // a check box, as Windows 7 shows it to administrators: Linux lists everyone's processes without elevation
        auto *all = new QCheckBox(tr7("Show processes from all users"));
        auto *end = new QPushButton(tr7("End Process"));
        buttons->addWidget(all);
        buttons->addStretch(1);
        buttons->addWidget(end);
        v->addLayout(buttons);
        connect(all, &QCheckBox::toggled, this, [this](bool on) {
            m_allUsers = on;
            refresh(true);
        });
        connect(end, &QPushButton::clicked, this, [this] {
            endProcess(false);
        });
        connect(m_procView, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            if (!m_procView->indexAt(pos).isValid()) {
                return;
            }
            QMenu menu(this);
            menu.addAction(tr7("Open File Location"), this, [this] {
                const QString exe = QFileInfo(QStringLiteral("/proc/%1/exe").arg(currentPid())).symLinkTarget();
                if (!exe.isEmpty()) {
                    QProcess::startDetached(QStringLiteral("dolphin"), {QStringLiteral("--select"), exe});
                }
            });
            menu.addSeparator();
            menu.addAction(tr7("End Process"), this, [this] {
                endProcess(false);
            });
            menu.addAction(tr7("End Process Tree"), this, [this] {
                endProcess(true);
            });
            menu.addSeparator();
            QMenu *prio = menu.addMenu(tr7("Set Priority"));
            const QList<QPair<QString, int>> levels{{tr7("Realtime"), -20}, {tr7("High"), -10},
                                                    {tr7("AboveNormal"), -5},  {tr7("Normal"), 0},
                                                    {tr7("BelowNormal"), 5},  {tr7("Low"), 19}};
            for (const auto &[text, nice] : levels) {
                prio->addAction(text, this, [this, nice] {
                    QProcess::startDetached(QStringLiteral("renice"), {QString::number(nice), QStringLiteral("-p"), QString::number(currentPid())});
                });
            }
            menu.addSeparator();
            menu.addAction(tr7("Properties"), this, [this] {
                const QString exe = QFileInfo(QStringLiteral("/proc/%1/exe").arg(currentPid())).symLinkTarget();
                QMessageBox::information(this, tr7("Properties"),
                                         tr7("PID: %1\nFile: %2\nCommand: %3")
                                             .arg(currentPid())
                                             .arg(exe.isEmpty() ? tr7("(access denied)") : exe,
                                                  readFile(QStringLiteral("/proc/%1/cmdline").arg(currentPid())).replace(QLatin1Char('\0'), QLatin1Char(' '))));
            });
            menu.exec(m_procView->viewport()->mapToGlobal(pos));
        });
        m_tabs->addTab(m_procPage, tr7("Processes"));
    }

    int currentPid() const
    {
        const QModelIndex idx = m_procView->currentIndex();
        return idx.isValid() ? idx.siblingAtColumn(0).data(Qt::UserRole).toInt() : 0;
    }

    void endProcess(bool tree)
    {
        const int pid = currentPid();
        if (pid <= 0) {
            return;
        }
        const QString name = m_procView->currentIndex().siblingAtColumn(0).data().toString();
        QMessageBox box(this);
        box.setWindowTitle(tr7("Windows Task Manager"));
        box.setIcon(QMessageBox::Warning);
        box.setText(tree ? tr7("Do you want to end the process tree of \"%1\"?").arg(name) : tr7("Do you want to end \"%1\"?").arg(name));
        box.setInformativeText(tr7("If an open program is associated with this process, it will close and you will lose any unsaved data. If you end a system process, it might result in system instability."));
        QPushButton *yes = box.addButton(tree ? tr7("End Process Tree") : tr7("End Process"), QMessageBox::AcceptRole);
        box.addButton(tr7("Cancel"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != yes) {
            return;
        }
        QList<int> pids{pid};
        if (tree) {
            for (int i = 0; i < pids.size(); ++i) {
                for (const QString &d : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs)) {
                    const int child = d.toInt();
                    if (child > 0 && readFile(QStringLiteral("/proc/%1/stat").arg(child)).section(QLatin1Char(')'), -1).split(QLatin1Char(' '), Qt::SkipEmptyParts).value(1).toInt() == pids.at(i)) {
                        pids.append(child);
                    }
                }
            }
        }
        for (int p : pids) {
            if (::kill(p, SIGTERM) != 0) {
                QMessageBox::warning(this, tr7("Unable to Terminate Process"),
                                     tr7("The operation could not be completed.\n\nAccess is denied."));
                break;
            }
        }
        QTimer::singleShot(500, this, [this] {
            refresh(true);
        });
    }

    void refreshProcesses(bool updateView)
    {
        const long hz = sysconf(_SC_CLK_TCK);
        const qint64 elapsedMs = m_procClock.isValid() ? m_procClock.restart() : 0;
        m_procClock.start();
        const uint me = getuid();

        QHash<int, ProcSample> now;
        struct Row {
            int pid;
            QString name, user, desc;
            double cpu;
            qint64 mem;
        };
        QList<Row> rows;
        int threads = 0;
        for (const QString &d : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs)) {
            const int pid = d.toInt();
            if (pid <= 0) {
                continue;
            }
            const QString stat = readFile(QStringLiteral("/proc/%1/stat").arg(pid));
            if (stat.isEmpty()) {
                continue;
            }
            const QString name = stat.section(QLatin1Char('('), 1).section(QLatin1Char(')'), 0, -2);
            const QStringList f = stat.section(QLatin1Char(')'), -1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            const qint64 ticks = f.value(11).toLongLong() + f.value(12).toLongLong();
            threads += f.value(17).toInt();
            now.insert(pid, {ticks, name});
            if (!updateView) {
                continue;
            }
            const QString status = readFile(QStringLiteral("/proc/%1/status").arg(pid));
            const uint uid = status.section(QStringLiteral("Uid:"), 1).trimmed().section(QLatin1Char('\t'), 0, 0).toUInt();
            if (!m_allUsers && uid != me) {
                continue;
            }
            double cpu = 0;
            if (elapsedMs > 0 && m_lastProc.contains(pid)) {
                cpu = double(ticks - m_lastProc.value(pid).ticks) * 1000.0 / hz / elapsedMs * 100.0 / qMax(1, m_cpuCount);
            }
            const qint64 mem = meminfo(status, "RssAnon");
            // Description: the program name of the executable, like the file description in Windows
            QString desc = QFileInfo(QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget()).fileName();
            if (desc.isEmpty()) {
                desc = name;
            }
            rows.append({pid, name, userName(uid), desc, cpu, mem});
        }
        m_lastProc = now;
        m_procCount = now.size();
        m_threads = threads;
        if (!updateView) {
            return;
        }
        const QString key = selectedKey(m_procView, 0);
        m_procModel->removeRows(0, m_procModel->rowCount());
        for (const Row &r : rows) {
            auto *name = new QStandardItem(r.name);
            name->setData(QString::number(r.pid), Qt::UserRole);
            name->setData(r.name.toLower(), Qt::UserRole + 1);
            auto *user = new QStandardItem(r.user);
            user->setData(r.user, Qt::UserRole + 1);
            auto *cpu = new NumItem(QStringLiteral("%1").arg(qRound(r.cpu), 2, 10, QLatin1Char('0')), r.cpu);
            auto *mem = new NumItem(kbText(r.mem), r.mem);
            auto *desc = new QStandardItem(r.desc);
            desc->setData(r.desc.toLower(), Qt::UserRole + 1);
            m_procModel->appendRow({name, user, cpu, mem, desc});
        }
        reselect(m_procView, m_procProxy, 0, key);
    }

    /* ----- Services ----- */
    void buildServices()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        m_svcModel = new QStandardItemModel(0, 5, this);
        m_svcModel->setHorizontalHeaderLabels({tr7("Name"), tr7("PID"), tr7("Description"), tr7("Status"), tr7("Group")});
        m_svcProxy = new QSortFilterProxyModel(this);
        m_svcProxy->setSourceModel(m_svcModel);
        m_svcProxy->setSortRole(Qt::UserRole + 1);
        m_svcView = makeList(page);
        m_svcView->setModel(m_svcProxy);
        const int widths[] = {90, 40, 110, 70, 100};
        for (int c = 0; c < 5; ++c) {
            m_svcView->header()->resizeSection(c, widths[c]);
        }
        m_svcView->header()->setSectionResizeMode(4, QHeaderView::Stretch);
        v->addWidget(m_svcView);
        auto *buttons = new QHBoxLayout;
        buttons->addStretch(1);
        auto *services = new QPushButton(shieldIcon(), tr7("Services..."));
        buttons->addWidget(services);
        v->addLayout(buttons);
        connect(services, &QPushButton::clicked, this, [] {
            QProcess::startDetached(QStringLiteral("konsole"), {QStringLiteral("--hold"), QStringLiteral("-e"), QStringLiteral("systemctl"),
                                                                QStringLiteral("list-units"), QStringLiteral("--type=service"), QStringLiteral("--all")});
        });
        connect(m_svcView, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            const QModelIndex idx = m_svcView->indexAt(pos);
            if (!idx.isValid()) {
                return;
            }
            const QString unit = idx.siblingAtColumn(0).data(Qt::UserRole).toString();
            const bool running = idx.siblingAtColumn(3).data().toString() == tr7("Running");
            auto systemctl = [this, unit](const QString &verb) {
                // pkexec asks for the password like the UAC prompt in Windows
                QProcess::startDetached(QStringLiteral("pkexec"), {QStringLiteral("systemctl"), verb, unit});
                QTimer::singleShot(1500, this, [this] {
                    refresh(true);
                });
            };
            QMenu menu(this);
            menu.addAction(tr7("Start Service"), this, [systemctl] {
                systemctl(QStringLiteral("start"));
            })->setEnabled(!running);
            menu.addAction(tr7("Stop Service"), this, [systemctl] {
                systemctl(QStringLiteral("stop"));
            })->setEnabled(running);
            menu.addSeparator();
            menu.addAction(tr7("Go To Process"), this, [this, idx] {
                const QString pid = idx.siblingAtColumn(1).data().toString();
                if (!pid.isEmpty()) {
                    m_tabs->setCurrentWidget(m_procPage);
                    m_allUsers = true;
                    refresh(true);
                    reselect(m_procView, m_procProxy, 0, pid);
                }
            })->setEnabled(!idx.siblingAtColumn(1).data().toString().isEmpty());
            menu.exec(m_svcView->viewport()->mapToGlobal(pos));
        });
        m_tabs->addTab(page, tr7("Services"));
    }

    void refreshServices()
    {
        const QJsonArray units = QJsonDocument::fromJson(run(QStringLiteral("systemctl"), {QStringLiteral("list-units"), QStringLiteral("--type=service"), QStringLiteral("--all"),
                                                                                           QStringLiteral("--no-pager"), QStringLiteral("--output=json")})
                                                             .toUtf8())
                                     .array();
        // PIDs and slices of all services in one call
        QHash<QString, QPair<QString, QString>> extra;
        const QString show = run(QStringLiteral("systemctl"), {QStringLiteral("show"), QStringLiteral("--type=service"), QStringLiteral("--all"),
                                                               QStringLiteral("--property=Id,MainPID,Slice")});
        for (const QString &block : show.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts)) {
            QString id, pid, slice;
            for (const QString &line : block.split(QLatin1Char('\n'))) {
                if (line.startsWith(QLatin1String("Id="))) {
                    id = line.mid(3);
                } else if (line.startsWith(QLatin1String("MainPID="))) {
                    pid = line.mid(8);
                } else if (line.startsWith(QLatin1String("Slice="))) {
                    slice = line.mid(6);
                }
            }
            extra.insert(id, {pid == QLatin1String("0") ? QString() : pid, slice.section(QLatin1Char('.'), 0, 0)});
        }
        const QString key = selectedKey(m_svcView, 0);
        m_svcModel->removeRows(0, m_svcModel->rowCount());
        for (const QJsonValue &u : units) {
            const QJsonObject o = u.toObject();
            const QString unit = o.value(QStringLiteral("unit")).toString();
            const QString shortName = unit.section(QStringLiteral(".service"), 0, 0);
            const bool running = o.value(QStringLiteral("sub")).toString() == QLatin1String("running");
            auto *name = new QStandardItem(shortName);
            name->setData(unit, Qt::UserRole);
            name->setData(shortName.toLower(), Qt::UserRole + 1);
            const QString pid = extra.value(unit).first;
            auto *pidItem = new NumItem(pid, pid.toDouble());
            auto *desc = new QStandardItem(o.value(QStringLiteral("description")).toString());
            desc->setData(desc->text().toLower(), Qt::UserRole + 1);
            auto *state = new QStandardItem(running ? tr7("Running") : tr7("Stopped"));
            state->setData(state->text(), Qt::UserRole + 1);
            auto *group = new QStandardItem(extra.value(unit).second);
            group->setData(group->text(), Qt::UserRole + 1);
            m_svcModel->appendRow({name, pidItem, desc, state, group});
        }
        reselect(m_svcView, m_svcProxy, 0, key);
    }

    /* ----- Performance ----- */
    void buildPerformance()
    {
        auto *page = new QWidget;
        auto *g = new QGridLayout(page);
        g->setHorizontalSpacing(6);
        g->setVerticalSpacing(6);
        g->setContentsMargins(8, 8, 8, 6);

        auto box = [](const QString &title, QWidget *content) {
            auto *b = new QGroupBox(title);
            auto *l = new QVBoxLayout(b);
            l->setContentsMargins(6, 2, 6, 4);
            l->addWidget(content);
            return b;
        };

        m_cpuMeter = new Meter;
        auto *cpuMeterWrap = new QWidget;
        auto *cmw = new QHBoxLayout(cpuMeterWrap);
        cmw->setContentsMargins(0, 0, 0, 0);
        cmw->addWidget(m_cpuMeter, 0, Qt::AlignCenter);
        g->addWidget(box(tr7("CPU Usage"), cpuMeterWrap), 0, 0);

        // one small graph per logical processor, like Windows 7
        m_cpuCount = readCpuTimes().size() - 1;
        auto *cores = new QWidget;
        auto *cg = new QGridLayout(cores);
        cg->setContentsMargins(0, 0, 0, 0);
        cg->setSpacing(3);
        const int perRow = m_cpuCount > 8 ? 8 : m_cpuCount;
        for (int i = 0; i < m_cpuCount; ++i) {
            auto *graph = new Graph(QColor(0x00, 0xff, 0x00));
            graph->setMinimumSize(20, 32);
            m_coreGraphs.append(graph);
            cg->addWidget(graph, i / qMax(1, perRow), i % qMax(1, perRow));
        }
        g->addWidget(box(tr7("CPU Usage History"), cores), 0, 1);

        m_memMeter = new Meter;
        auto *memMeterWrap = new QWidget;
        auto *mmw = new QHBoxLayout(memMeterWrap);
        mmw->setContentsMargins(0, 0, 0, 0);
        mmw->addWidget(m_memMeter, 0, Qt::AlignCenter);
        g->addWidget(box(tr7("Memory"), memMeterWrap), 1, 0);
        m_memGraph = new Graph(QColor(0x00, 0xb4, 0xff));
        g->addWidget(box(tr7("Physical Memory Usage History"), m_memGraph), 1, 1);

        auto table = [](const QList<QPair<QString, QLabel **>> &rows) {
            auto *w = new QWidget;
            auto *f = new QGridLayout(w);
            f->setContentsMargins(0, 0, 0, 0);
            f->setVerticalSpacing(1);
            for (int r = 0; r < rows.size(); ++r) {
                f->addWidget(new QLabel(rows[r].first), r, 0);
                auto *val = new QLabel;
                val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                *rows[r].second = val;
                f->addWidget(val, r, 1);
            }
            f->setColumnStretch(0, 1);
            return w;
        };
        auto *left = new QVBoxLayout;
        left->addWidget(box(tr7("Physical Memory (MB)"),
                            table({{tr7("Total"), &m_lTotal}, {tr7("Cached"), &m_lCached}, {tr7("Available"), &m_lAvail}, {tr7("Free"), &m_lFree}})));
        left->addWidget(box(tr7("Kernel Memory (MB)"), table({{tr7("Paged"), &m_lPaged}, {tr7("Nonpaged"), &m_lNonPaged}})));
        auto *right = new QVBoxLayout;
        right->addWidget(box(tr7("System"),
                             table({{tr7("Handles"), &m_lHandles},
                                    {tr7("Threads"), &m_lThreads},
                                    {tr7("Processes"), &m_lProcs},
                                    {tr7("Up Time"), &m_lUptime},
                                    {tr7("Commit (MB)"), &m_lCommit}})));
        auto *monitor = new QPushButton(tr7("Resource Monitor..."));
        connect(monitor, &QPushButton::clicked, this, [] {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--resmon")});
        });
        right->addWidget(monitor);
        right->addStretch(1);
        auto *bottom = new QHBoxLayout;
        bottom->addLayout(left, 1);
        bottom->addLayout(right, 1);
        g->addLayout(bottom, 2, 0, 1, 2);
        g->setColumnStretch(1, 1);
        g->setRowStretch(0, 1);
        g->setRowStretch(1, 1);
        m_tabs->addTab(page, tr7("Performance"));
    }

    void refreshPerformance(const QList<CpuTimes> &cpu)
    {
        const QString mi = readFile(QStringLiteral("/proc/meminfo"));
        const qint64 total = meminfo(mi, "MemTotal"), avail = meminfo(mi, "MemAvailable"), free = meminfo(mi, "MemFree");
        const qint64 cached = meminfo(mi, "Cached") + meminfo(mi, "Buffers");
        const double memUsed = total > 0 ? double(total - avail) / total : 0;

        // CPU
        double overall = 0;
        if (!m_lastCpu.isEmpty() && m_lastCpu.size() == cpu.size()) {
            for (int i = 0; i < cpu.size(); ++i) {
                const double dt = double(cpu[i].total - m_lastCpu[i].total);
                const double v = dt > 0 ? double(cpu[i].busy - m_lastCpu[i].busy) / dt : 0;
                if (i == 0) {
                    overall = v;
                } else if (i - 1 < m_coreGraphs.size()) {
                    m_coreGraphs[i - 1]->addValue(v);
                }
            }
        }
        m_lastCpu = cpu;
        m_cpuUsage = overall;
        m_memUsage = memUsed;
        m_cpuMeter->setValue(overall, QStringLiteral("%1 %").arg(qRound(overall * 100)));
        m_memGraph->addValue(memUsed);
        m_memMeter->setValue(memUsed, QLocale{}.toString((total - avail) / 1048576.0, 'f', 2) + tr7(" GB"));

        auto mb = [](qint64 kb) {
            return QString::number(kb / 1024);
        };
        m_lTotal->setText(mb(total));
        m_lCached->setText(mb(cached));
        m_lAvail->setText(mb(avail));
        m_lFree->setText(mb(free));
        m_lPaged->setText(mb(meminfo(mi, "SReclaimable")));
        m_lNonPaged->setText(mb(meminfo(mi, "SUnreclaim")));
        m_lHandles->setText(readFile(QStringLiteral("/proc/sys/fs/file-nr")).section(QLatin1Char('\t'), 0, 0));
        m_lThreads->setText(QString::number(m_threads));
        m_lProcs->setText(QString::number(m_procCount));
        const qint64 up = qint64(readFile(QStringLiteral("/proc/uptime")).section(QLatin1Char(' '), 0, 0).toDouble());
        m_lUptime->setText(QStringLiteral("%1:%2:%3:%4")
                               .arg(up / 86400)
                               .arg((up / 3600) % 24, 2, 10, QLatin1Char('0'))
                               .arg((up / 60) % 60, 2, 10, QLatin1Char('0'))
                               .arg(up % 60, 2, 10, QLatin1Char('0')));
        m_lCommit->setText(QStringLiteral("%1 / %2").arg(mb(meminfo(mi, "Committed_AS")), mb(meminfo(mi, "CommitLimit"))));
    }

    /* ----- Networking ----- */
    void buildNetwork()
    {
        m_netPage = new QWidget;
        auto *v = new QVBoxLayout(m_netPage);
        m_netGraphs = new QVBoxLayout;
        v->addLayout(m_netGraphs, 1);
        m_netModel = new QStandardItemModel(0, 4, this);
        m_netModel->setHorizontalHeaderLabels({tr7("Adapter Name"), tr7("Network Utilization"), tr7("Link Speed"), tr7("Status")});
        m_netView = makeList(m_netPage);
        m_netView->setModel(m_netModel);
        m_netView->setMaximumHeight(90);
        m_netView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        v->addWidget(m_netView);
        m_tabs->addTab(m_netPage, tr7("Networking"));
    }

    void refreshNetwork(bool updateView)
    {
        // bytes per interface from /proc/net/dev
        QHash<QString, quint64> bytes;
        for (const QString &line : readFile(QStringLiteral("/proc/net/dev")).split(QLatin1Char('\n')).mid(2)) {
            const QString ifname = line.section(QLatin1Char(':'), 0, 0).trimmed();
            const QStringList f = line.section(QLatin1Char(':'), 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (ifname.isEmpty() || ifname == QLatin1String("lo") || f.size() < 9) {
                continue;
            }
            bytes.insert(ifname, f.value(0).toULongLong() + f.value(8).toULongLong());
        }
        const qint64 ms = m_netClock.isValid() ? m_netClock.restart() : 0;
        m_netClock.start();

        QStringList ifaces = bytes.keys();
        ifaces.sort();
        for (const QString &ifname : ifaces) {
            const bool wireless = QFileInfo::exists(QStringLiteral("/sys/class/net/%1/wireless").arg(ifname));
            if (!QFileInfo::exists(QStringLiteral("/sys/class/net/%1/device").arg(ifname))) {
                continue; // virtual interfaces (docker, bridges...) are not shown, like Windows hides them
            }
            if (!m_net.contains(ifname)) {
                NetInfo n;
                n.title = wireless ? tr7("Wireless Network Connection") : tr7("Local Area Connection");
                auto *box = new QGroupBox(n.title);
                auto *l = new QVBoxLayout(box);
                l->setContentsMargins(4, 4, 4, 4);
                n.graph = new Graph(QColor(0x00, 0xff, 0x00));
                n.graph->setLabels(true);
                l->addWidget(n.graph);
                m_netGraphs->addWidget(box, 1);
                m_net.insert(ifname, n);
            }
            NetInfo &n = m_net[ifname];
            QString speedText;
            double linkBps = readFile(QStringLiteral("/sys/class/net/%1/speed").arg(ifname)).trimmed().toDouble() * 1e6;
            if (wireless) {
                const QString link = run(QStringLiteral("iw"), {QStringLiteral("dev"), ifname, QStringLiteral("link")}, 1000);
                const double mbit = link.section(QStringLiteral("tx bitrate:"), 1).trimmed().section(QLatin1Char(' '), 0, 0).toDouble();
                if (mbit > 0) {
                    linkBps = mbit * 1e6;
                }
            }
            if (linkBps <= 0) {
                linkBps = 1e9;
            } else {
                speedText = linkBps >= 1e9 ? tr7("%1 Gbps").arg(linkBps / 1e9) : tr7("%1 Mbps").arg(qRound(linkBps / 1e6));
            }
            double usage = 0;
            if (ms > 0 && n.lastBytes > 0) {
                usage = double(bytes.value(ifname) - n.lastBytes) * 8.0 / (ms / 1000.0) / linkBps;
            }
            n.lastBytes = bytes.value(ifname);
            n.usage = usage;
            n.speed = speedText;
            n.up = readFile(QStringLiteral("/sys/class/net/%1/operstate").arg(ifname)).trimmed() == QLatin1String("up");
            // Windows scales the graph to the busiest recent value (1 %, 10 %, 100 %)
            n.peak = qMax(n.peak * 0.98, usage);
            const double scale = n.peak > 0.1 ? 1.0 : n.peak > 0.01 ? 0.1 : 0.01;
            // "1 %", "0,50 %", "0 %" like Windows 7: two decimals below one per cent
            auto pct = [](double v) {
                return QStringLiteral("%1 %").arg(QLocale{}.toString(v, 'f', v < 1 ? 2 : (v < 10 && v != std::floor(v) ? 1 : 0)));
            };
            n.graph->m_topLabel = pct(scale * 100);
            n.graph->m_midLabel = pct(scale * 50);
            n.graph->addValue(usage / scale);
        }
        if (!updateView) {
            return;
        }
        m_netModel->removeRows(0, m_netModel->rowCount());
        for (auto it = m_net.cbegin(); it != m_net.cend(); ++it) {
            const NetInfo &n = it.value();
            m_netModel->appendRow({new QStandardItem(n.title),
                                   new QStandardItem(QLocale{}.toString(n.usage * 100, 'f', 2) + QStringLiteral(" %")),
                                   new QStandardItem(n.speed),
                                   new QStandardItem(n.up ? tr7("Connected") : tr7("Disconnected"))});
        }
    }

    /* ----- Users ----- */
    void buildUsers()
    {
        auto *page = new QWidget;
        auto *v = new QVBoxLayout(page);
        m_userModel = new QStandardItemModel(0, 5, this);
        m_userModel->setHorizontalHeaderLabels({tr7("User Name"), tr7("ID"), tr7("Status"), tr7("Client Name"), tr7("Session")});
        m_userView = makeList(page);
        m_userView->setModel(m_userModel);
        m_userView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        {
            const int widths[] = {96, 40, 70, 90, 70};
            for (int c = 0; c < 5; ++c) {
                m_userView->header()->resizeSection(c, widths[c]);
            }
        }
        v->addWidget(m_userView);
        auto *buttons = new QHBoxLayout;
        buttons->addStretch(1);
        auto *disconnect = new QPushButton(tr7("Disconnect"));
        auto *logout = new QPushButton(tr7("Logoff"));
        auto *message = new QPushButton(tr7("Send Message..."));
        message->setEnabled(false);
        for (QPushButton *b : {disconnect, logout, message}) {
            buttons->addWidget(b);
        }
        v->addLayout(buttons);
        connect(disconnect, &QPushButton::clicked, this, [] {
            QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("lock-session")});
        });
        connect(logout, &QPushButton::clicked, this, [] {
            QProcess::startDetached(QStringLiteral("qdbus6"), {QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"), QStringLiteral("promptLogout")});
        });
        m_tabs->addTab(page, tr7("Users"));
    }

    void refreshUsers()
    {
        m_userModel->removeRows(0, m_userModel->rowCount());
        for (const QString &line : run(QStringLiteral("loginctl"), {QStringLiteral("list-sessions"), QStringLiteral("--no-legend")}).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (f.size() < 3 || f.value(5) == QLatin1String("manager")) {
                continue;
            }
            const QString sid = f.at(0);
            const QString state = run(QStringLiteral("loginctl"), {QStringLiteral("show-session"), sid, QStringLiteral("-p"), QStringLiteral("State"), QStringLiteral("--value")}).trimmed();
            auto *user = new QStandardItem(QIcon::fromTheme(QStringLiteral("system-users")), f.at(2));
            m_userModel->appendRow({user, new QStandardItem(sid), new QStandardItem(state == QLatin1String("active") ? tr7("Active") : tr7("Disconnected", "session")),
                                    new QStandardItem(QString()), new QStandardItem(f.value(6) != QLatin1String("-") ? QStringLiteral("Console") : f.value(6))});
        }
    }

    /* ----- menus ----- */
    void buildMenus()
    {
        QMenu *file = menuBar()->addMenu(tr7("&File"));
        file->addAction(tr7("New Task (Run...)"), this, &TaskManager::newTask);
        file->addSeparator();
        file->addAction(tr7("Exit Task Manager"), this, &QWidget::close);

        QMenu *options = menuBar()->addMenu(tr7("&Options"));
        QAction *onTop = options->addAction(tr7("Always On Top"));
        onTop->setCheckable(true);
        onTop->setChecked(true);
        connect(onTop, &QAction::toggled, this, [this](bool on) {
            Win7KWin::setKeepAbove(this, on);
        });
        Win7KWin::setKeepAbove(this, true);
        QAction *minimize = options->addAction(tr7("Minimize On Use"));
        minimize->setCheckable(true);
        minimize->setChecked(true);
        connect(minimize, &QAction::toggled, this, [this](bool on) {
            m_minimizeOnUse = on;
        });

        QMenu *view = menuBar()->addMenu(tr7("&View"));
        view->addAction(tr7("Refresh Now"), this, [this] {
            refresh(true);
        });
        QMenu *speed = view->addMenu(tr7("Update Speed"));
        auto *group = new QActionGroup(speed);
        const QList<QPair<QString, int>> speeds{{tr7("High", "speed"), 500}, {tr7("Normal", "speed"), 1000}, {tr7("Low", "speed"), 4000}, {tr7("Paused"), 0}};
        for (const auto &[text, ms] : speeds) {
            QAction *a = speed->addAction(text, this, [this, ms] {
                if (ms > 0) {
                    m_timer->start(ms);
                } else {
                    m_timer->stop();
                }
            });
            a->setCheckable(true);
            a->setChecked(ms == 1000);
            group->addAction(a);
        }

        QMenu *help = menuBar()->addMenu(tr7("&Help"));
        help->addAction(tr7("About Task Manager"), this, [this] {
            QMessageBox::about(this, tr7("Windows Task Manager"), tr7("Task Manager in the Windows 7 style.\nShows the programs, processes, services and performance of this Linux computer."));
        });

        menuBar()->setStyleSheet(QStringLiteral(
            "QMenuBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fafbff, stop:0.45 #eaeef9, stop:0.5 #d4daeb, stop:1 #d4d9ee); border: none; }"
            "QMenuBar::item { background: transparent; padding: 3px 7px; color: black; font-size: 12px; }"
            "QMenuBar::item:selected { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fdfeff, stop:1 #e4effa); border: 1px solid #a8c8ea; border-radius: 3px; }"));
        win7WrapMenuBar(this); // Windows 7: the menu bar wraps instead of showing a "»" arrow
    }

    /* ----- refresh loop ----- */
    void refresh(bool force)
    {
        const QList<CpuTimes> cpu = readCpuTimes();
        QWidget *current = m_tabs->currentWidget();
        refreshProcesses(current == m_procPage);
        refreshPerformance(cpu);
        refreshNetwork(current == m_netPage);
        const int tab = m_tabs->currentIndex();
        if (tab == 0) {
            refreshApplications();
        } else if (tab == 2 && (force || ++m_slowTick % 5 == 0)) {
            refreshServices();
        } else if (tab == 5 && (force || ++m_slowTick % 5 == 0)) {
            refreshUsers();
        }
        m_stProcs->setText(tr7("Processes: %1").arg(m_procCount));
        m_stCpu->setText(tr7("CPU Usage: %1%").arg(qRound(m_cpuUsage * 100)));
        m_stMem->setText(tr7("Physical Memory: %1%").arg(qRound(m_memUsage * 100)));
    }

    struct NetInfo {
        QString title, speed;
        Graph *graph = nullptr;
        quint64 lastBytes = 0;
        double usage = 0, peak = 0;
        bool up = false;
    };

    QTabWidget *m_tabs;
    QTimer *m_timer;
    QLabel *m_stProcs, *m_stCpu, *m_stMem;

    QStandardItemModel *m_appModel;
    QTreeView *m_appView;
    bool m_minimizeOnUse = true;

    QWidget *m_procPage;
    QStandardItemModel *m_procModel;
    QSortFilterProxyModel *m_procProxy;
    QTreeView *m_procView;
    QHash<int, ProcSample> m_lastProc;
    QElapsedTimer m_procClock;
    bool m_allUsers = false;
    int m_procCount = 0, m_threads = 0;

    QStandardItemModel *m_svcModel;
    QSortFilterProxyModel *m_svcProxy;
    QTreeView *m_svcView;

    Meter *m_cpuMeter, *m_memMeter;
    QList<Graph *> m_coreGraphs;
    Graph *m_memGraph;
    QList<CpuTimes> m_lastCpu;
    int m_cpuCount = 1;
    double m_cpuUsage = 0, m_memUsage = 0;
    QLabel *m_lTotal, *m_lCached, *m_lAvail, *m_lFree, *m_lPaged, *m_lNonPaged, *m_lHandles, *m_lThreads, *m_lProcs, *m_lUptime, *m_lCommit;

    QWidget *m_netPage;
    QVBoxLayout *m_netGraphs;
    QStandardItemModel *m_netModel;
    QTreeView *m_netView;
    QMap<QString, NetInfo> m_net;
    QElapsedTimer m_netClock;

    QStandardItemModel *m_userModel;
    QTreeView *m_userView;
    int m_slowTick = 0;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Win7I18n::install(&app);
    app.setApplicationName(QStringLiteral("seven-taskmgr"));
    app.setApplicationDisplayName(tr7("Windows Task Manager"));
    app.setDesktopFileName(QStringLiteral("seven-taskmgr"));
    QFont font = app.font();
    font.setPixelSize(12);
    app.setFont(font);
    app.setStyleSheet(QStringLiteral("QGroupBox, QTabBar::tab, QLabel, QPushButton, QTreeView, QHeaderView::section, QStatusBar QLabel { font-size: 12px; }"
                                     "QGroupBox { font-weight: normal; }"));
    if (app.arguments().contains(QStringLiteral("--resmon"))) {
        app.setApplicationDisplayName(QString());
        app.setDesktopFileName(QStringLiteral("seven-resmon"));
        app.setStyleSheet(QStringLiteral("QGroupBox { font-weight: normal; }")); // a QTabBar::tab rule would elide the tabs
        auto *rm = new ResMon::ResourceMonitor;
        rm->setAttribute(Qt::WA_DeleteOnClose);
        const int t = app.arguments().indexOf(QStringLiteral("--tab"));
        if (t > 0) {
            rm->setTab(app.arguments().value(t + 1).toInt());
        }
        rm->show();
        if (!qEnvironmentVariableIsEmpty("WIN7_GRAB")) {
            QTimer::singleShot(qEnvironmentVariableIntValue("WIN7_GRAB_WAIT") ? qEnvironmentVariableIntValue("WIN7_GRAB_WAIT") : 4500, rm, [rm] {
                rm->grab().save(qEnvironmentVariable("WIN7_GRAB"));
                QApplication::quit();
            });
        }
        return app.exec();
    }
    TaskManager w;
    const int tabArg = app.arguments().indexOf(QStringLiteral("--tab"));
    if (tabArg > 0) {
        w.findChild<QTabWidget *>()->setCurrentIndex(app.arguments().value(tabArg + 1).toInt());
    }
    w.show();
    // testing: WIN7_GRAB=<file.png> saves the window after a few refreshes and quits
    if (!qEnvironmentVariableIsEmpty("WIN7_GRAB")) {
        QTimer::singleShot(2600, &w, [&w] {
            w.grab().save(qEnvironmentVariable("WIN7_GRAB"));
            QApplication::quit();
        });
    }
    return app.exec();
}
