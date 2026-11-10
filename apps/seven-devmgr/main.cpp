#include "win7i18n.h"
/*
 * Device Manager in the style of Windows 7, listing the real hardware of this Linux machine
 * (PCI and USB devices, disks, monitors, input devices, processors, network, battery...).
 * The device tree itself is in devmgrview.h (Computer Management embeds it too).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../seven-common/win7wrapmenubar.h"
#include "devmgrview.h"

#include <QDesktopServices>

using namespace Win7DevMgr;

namespace
{
class DeviceManager : public QMainWindow
{
public:
    DeviceManager()
    {
        setWindowTitle(tr7("Device Manager"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-devmgr-w7"), QIcon::fromTheme(QStringLiteral("computer"))));

        m_devices = new DeviceTree(this);
        m_devices->layout()->setContentsMargins(0, 6, 0, 0); // Windows 7: 6 px of window colour under the toolbar
        setCentralWidget(m_devices);

        // Actions (the original MMC / devmgr.dll toolbar pictures)
        auto *back = new QAction(win7Icon(QStringLiteral("seven-mmc-back")), tr7("Back"), this);
        auto *forward = new QAction(win7Icon(QStringLiteral("seven-mmc-forward")), tr7("Forward"), this);
        back->setEnabled(false);
        forward->setEnabled(false);
        auto *console = new QAction(win7Icon(QStringLiteral("seven-mmc-console")), tr7("Show/Hide Console Tree"), this);
        console->setEnabled(false);
        auto *actionPane = new QAction(win7Icon(QStringLiteral("seven-mmc-actionpane")), tr7("Show/Hide Action Pane"), this);
        actionPane->setEnabled(false);
        QAction *props = m_devices->propertiesAction();
        QAction *scan = m_devices->scanAction();
        auto *help = new QAction(win7Icon(QStringLiteral("seven-mmc-help")), tr7("Help"), this);
        auto *exit = new QAction(tr7("Exit"), this);
        auto *showHidden = new QAction(tr7("Show hidden devices"), this);
        showHidden->setCheckable(true);
        connect(exit, &QAction::triggered, this, &QWidget::close);
        connect(help, &QAction::triggered, this, []() {
            QProcess::startDetached(QStringLiteral("khelpcenter"), {});
        });
        connect(showHidden, &QAction::toggled, m_devices, &DeviceTree::setShowHidden);

        // Menu bar: File, Action, View, Help
        QMenu *file = menuBar()->addMenu(tr7("&File"));
        file->addAction(exit);
        QMenu *action = menuBar()->addMenu(tr7("&Action"));
        action->addAction(scan);
        action->addSeparator();
        action->addAction(props);
        QMenu *view = menuBar()->addMenu(tr7("&View"));
        QAction *byType = view->addAction(tr7("Devices by type"));
        byType->setCheckable(true);
        byType->setChecked(true);
        view->addSeparator();
        view->addAction(showHidden);
        // Help menu of mmc.exe: no pictures
        QMenu *helpMenu = menuBar()->addMenu(tr7("&Help"));
        connect(helpMenu->addAction(tr7("Help Topics")), &QAction::triggered, help, &QAction::trigger);
        helpMenu->addAction(tr7("TechCenter Web Site"), this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://learn.microsoft.com/windows-hardware/drivers/install/overview-of-device-manager")));
        });
        helpMenu->addSeparator();
        helpMenu->addAction(tr7("About Microsoft Management Console..."), this, [this]() {
            QMessageBox::about(this, tr7("Microsoft Management Console"),
                               tr7("Microsoft Management Console\nVersion 3.0"));
        });
        helpMenu->addAction(tr7("About Device Manager..."), this, [this]() {
            QMessageBox::about(this, tr7("Device Manager"),
                               tr7("Device Manager in the Windows 7 style.\nShows the hardware Linux sees."));
        });

        // Toolbar like Windows 7, it follows the selection:
        //   nothing selected:  back, forward | console tree | help, action pane
        //   the computer:      ... | help, action pane | scan
        //   a device type:     ... | console tree | properties (disabled) | help, action pane | scan
        //   a device:          ... | properties | help, action pane | scan | update driver, uninstall
        auto *update = new QAction(win7Icon(QStringLiteral("seven-devmgr-update")), tr7("Update Driver Software..."), this);
        auto *uninstall = new QAction(win7Icon(QStringLiteral("seven-devmgr-uninstall")), tr7("Uninstall"), this);
        connect(update, &QAction::triggered, props, &QAction::trigger);
        connect(uninstall, &QAction::triggered, props, &QAction::trigger);
        QToolBar *bar = addToolBar(tr7("Standard"));
        bar->setMovable(false);
        bar->setIconSize(QSize(16, 16));
        bar->setFixedHeight(24); // Windows 7 (measured): 24 px with its two lines
        bar->addAction(back);
        bar->addAction(forward);
        bar->addSeparator();
        bar->addAction(console);
        bar->addSeparator();
        bar->addAction(props);
        QAction *propsSep = bar->addSeparator();
        bar->addAction(help);
        bar->addAction(actionPane);
        QAction *scanSep = bar->addSeparator();
        bar->addAction(scan);
        QAction *deviceSep = bar->addSeparator();
        bar->addAction(update);
        bar->addAction(uninstall);
        for (QAction *a : bar->actions()) {
            if (QWidget *b = bar->widgetForAction(a); b && !a->isSeparator()) {
                b->setFixedSize(23, 22); // Windows 7 (measured)
            }
        }
        QTreeView *tree = m_devices->view();
        auto follow = [=]() {
            const QModelIndex idx = tree->selectionModel()->currentIndex();
            const bool any = idx.isValid() && tree->selectionModel()->isSelected(idx);
            const bool root = any && !idx.parent().isValid();
            const bool device = any && !root && !tree->model()->hasChildren(idx);
            const bool type = any && !root && !device;
            for (QAction *a : {props, propsSep}) {
                a->setVisible(type || device);
            }
            for (QAction *a : {scanSep, scan}) {
                a->setVisible(any);
            }
            for (QAction *a : {deviceSep, update, uninstall}) {
                a->setVisible(device);
            }
        };
        connect(tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, follow);
        connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this, follow);
        follow();

        statusBar()->setSizeGripEnabled(true);
        menuBar()->setStyleSheet(QStringLiteral(
            // measured on Windows 7: 9 rows of light gradient, then flat
            "QMenuBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fefeff, stop:0.39 #e5eaf5, stop:0.4 #d3daed, stop:1 #d3daed);"
            " border: none; padding: 1px 0px 1px 1px; }"
            // the frames of mmc.exe's menu bar, cut from Windows 7: light under the mouse, dark while a menu is open
            "QMenuBar::item { background: transparent; border: 3px solid transparent; border-left-width: 5px; border-right-width: 5px; padding: -1px 3px; color: black; }"
            "QMenuBar::item:selected { border-image: url(%1/hot.png) 3 5 3 5 stretch; }"
            "QMenuBar::item:pressed { border-image: url(%1/pressed.png) 3 5 3 5 stretch; }")
            .arg(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-aero/menubar"), QStandardPaths::LocateDirectory)));
        win7WrapMenuBar(this); // Windows 7: the menu bar wraps instead of showing a "»" arrow
        bar->setStyleSheet(QStringLiteral(
            // measured on Windows 7: a grey and a white line, flat, then lighter towards the bottom line
            "QToolBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f4f7fc, stop:0.08 #f4f7fc, stop:0.081 #d3daed, stop:0.37 #d3daed, stop:0.95 #e1e6f6, stop:1 #e1e6f6);"
            " border: none; border-top: 1px solid #b4b4b4; border-bottom: 1px solid #b6bccc; spacing: 0px; padding: 0px 2px; }"
            // measured: 8 px, a dark and a light line 2 px in, from the 4th row to the 3rd from the bottom
            "QToolBar::separator { width: 0px; border-left: 1px solid #747882; border-right: 1px solid rgba(255, 255, 255, 77);"
            " margin: 3px 4px 1px 2px; }")
            + Win7States::toolButtonSheet()
            + QStringLiteral("QToolButton { padding-right: 1px; }")); // buttons 23 px wide, the picture 3 px in

        resize(640, 560);
    }

private:
    DeviceTree *m_devices;
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Win7I18n::install(&app);
    Win7TabEdgeFix::install(&app);
    // in Windows 7 Device Manager runs in mmc.exe, so the taskbar shows the MMC toolbox: the taskbar
    // finds seven-devmgr-mmc.desktop by WM_CLASS, while the title bar keeps the window's own icon.
    // KWin takes the title bar icon from the desktop file named in _KDE_NET_WM_DESKTOP_FILE, so that
    // names a file that does not exist (otherwise Qt fills in "seven-devmgr")
    app.setApplicationName(QStringLiteral("seven-devmgr-mmc"));
    app.setApplicationDisplayName(tr7("Device Manager"));
    app.setDesktopFileName(QStringLiteral("seven-devmgr-window"));
    // "--properties <name>": only the properties of one device (Sound > Properties > Controller)
    const int propArg = app.arguments().indexOf(QStringLiteral("--properties"));
    if (propArg > 0) {
        const QString want = app.arguments().value(propArg + 1);
        app.setApplicationDisplayName(QString());
        const QStringList words = want.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        Device best;
        int bestScore = 0;
        for (const Device &d : scan()) {
            int score = 0;
            for (const QString &w : words) {
                score += d.name.contains(w, Qt::CaseInsensitive) ? 1 : 0;
            }
            if (score > bestScore) {
                bestScore = score;
                best = d;
            }
        }
        if (best.name.isEmpty()) {
            best = {QStringLiteral("sound"), want, {{tr7("Device type"), tr7("Sound, video and game controllers")}}};
        }
        QString icon = QStringLiteral("seven-devmgr-sound");
        for (const Category &c : categories()) {
            if (c.key == best.category) {
                icon = c.icon;
            }
        }
        showProperties(nullptr, best.name, QIcon::fromTheme(icon), best.details);
        return 0;
    }
    DeviceManager w;
    w.show();
    return app.exec();
}
