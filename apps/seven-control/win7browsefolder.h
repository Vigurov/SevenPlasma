#pragma once
#include "win7i18n.h"
/* Windows 7 "Browse For Folder" (SHBrowseForFolder) dialog: prompt text, folder tree rooted at the Desktop
 * (Libraries, the user folder, Computer, Network, Control Panel, Recycle Bin, the Desktop's own folders),
 * a "Папка:" field and OK / Скасувати. Measured on the reference VM (refs/browsefolder_dlg.png, 340 x 384 with frame).
 * Win7BrowseFolder::getFolder(parent, prompt, start) -> chosen path or empty. */
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "win7states.h"

namespace Win7BrowseFolder
{
enum { PathRole = Qt::UserRole + 1, FilledRole };

class Dialog : public QDialog
{
public:
    Dialog(QWidget *parent, const QString &prompt, const QString &start)
        : QDialog(parent)
    {
        setWindowTitle(tr7("Browse For Folder"));
        QFont f(QStringLiteral("Segoe UI"));
        f.setPointSize(9);
        setFont(f);
        setFixedSize(324, 346);

        auto *text = new QLabel(prompt, this);
        text->setWordWrap(true);
        text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        text->setGeometry(10, 9, 300, 34);

        m_tree = new QTreeWidget(this);
        m_tree->setGeometry(10, 52, 304, 216);
        m_tree->setHeaderHidden(true);
        m_tree->setIconSize(QSize(16, 16));
        m_tree->setIndentation(14);
        m_tree->setUniformRowHeights(true);
        m_tree->setFrameShape(QFrame::StyledPanel);
        // Explorer-style tree like Windows 7: no lines, the theme's hollow/filled triangles, Aero hover/selection
        const QString t = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-aero/tree"), QStandardPaths::LocateDirectory);
        m_tree->setStyleSheet(QStringLiteral("QTreeView { background: white; }"
                                             "QTreeView::branch { background: white; border-image: none; image: none; }"
                                             "QTreeView::branch:has-children:closed { image: url(%1/collapsed.png); }"
                                             "QTreeView::branch:has-children:closed:hover { image: url(%1/collapsed-hot.png); }"
                                             "QTreeView::branch:has-children:open { image: url(%1/expanded.png); }"
                                             "QTreeView::branch:has-children:open:hover { image: url(%1/expanded-hot.png); }").arg(t)
                              + Win7States::itemViewSheet(QStringLiteral("QTreeView"), 21));
        m_tree->setRootIsDecorated(false);
        QObject::connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *it) { fill(it); });
        QObject::connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
            m_edit->setText(it ? it->text(0) : QString());
            m_ok->setEnabled(it && !it->data(0, PathRole).toString().isEmpty());
        });

        auto *lab = new QLabel(tr7("Folder:"), this);
        lab->setGeometry(16, 282, 50, 16);
        m_edit = new QLineEdit(this);
        m_edit->setGeometry(70, 278, 244, 22);

        m_ok = new QPushButton(QStringLiteral("OK"), this);
        m_ok->setGeometry(158, 314, 75, 23);
        m_ok->setDefault(true);
        auto *cancel = new QPushButton(tr7("Cancel"), this);
        cancel->setGeometry(239, 314, 75, 23);
        QObject::connect(m_ok, &QPushButton::clicked, this, &QDialog::accept);
        QObject::connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

        build(start);
    }

    QString path() const
    {
        const QTreeWidgetItem *it = m_tree->currentItem();
        return it ? it->data(0, PathRole).toString() : QString();
    }

private:
    QTreeWidgetItem *node(QTreeWidgetItem *parent, const QString &name, const QString &icon, const QString &path, bool children)
    {
        auto *it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        it->setText(0, name);
        it->setIcon(0, QIcon::fromTheme(icon, QIcon::fromTheme(QStringLiteral("folder"))));
        it->setData(0, PathRole, path);
        it->setSizeHint(0, QSize(0, 21));
        it->setChildIndicatorPolicy(children ? QTreeWidgetItem::ShowIndicator : QTreeWidgetItem::DontShowIndicatorWhenChildless);
        return it;
    }
    static bool hasSubdirs(const QString &path)
    {
        return !QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
    }
    static QString xdg(QStandardPaths::StandardLocation l)
    {
        return QStandardPaths::writableLocation(l);
    }
    void fill(QTreeWidgetItem *it)
    {
        if (it->data(0, FilledRole).toBool()) return;
        it->setData(0, FilledRole, true);
        const QString p = it->data(0, PathRole).toString();
        if (p.isEmpty()) return;
        const auto dirs = QDir(p).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase | QDir::LocaleAware);
        for (const QFileInfo &d : dirs) {
            node(it, d.fileName(), QStringLiteral("folder"), d.absoluteFilePath(), hasSubdirs(d.absoluteFilePath()));
        }
        if (dirs.isEmpty()) it->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
    }
    void build(const QString &start)
    {
        const QString desk = xdg(QStandardPaths::DesktopLocation);
        auto *root = node(nullptr, tr7("Desktop"), QStringLiteral("seven-bf-desktop"), desk, true);
        root->setData(0, FilledRole, true);

        auto *libs = node(root, tr7("Libraries"), QStringLiteral("folder-library"), QString(), true);
        libs->setData(0, FilledRole, true);
        const struct { const char *name; const char *icon; QStandardPaths::StandardLocation loc; } L[] = {
            {"Documents", "folder-documents", QStandardPaths::DocumentsLocation},
            {"Pictures", "folder-pictures", QStandardPaths::PicturesLocation},
            {"Music", "folder-music", QStandardPaths::MusicLocation},
            {"Videos", "folder-videos", QStandardPaths::MoviesLocation}};
        for (const auto &l : L) node(libs, tr7(l.name), QString::fromLatin1(l.icon), xdg(l.loc), hasSubdirs(xdg(l.loc)));

        node(root, qEnvironmentVariable("USER"), QStringLiteral("user-home"), QDir::homePath(), true);

        auto *comp = node(root, tr7("Computer"), QStringLiteral("computer"), QString(), true);
        comp->setData(0, FilledRole, true);
        node(comp, tr7("Local Disk (/)"), QStringLiteral("drive-harddisk"), QStringLiteral("/"), true);
        for (const QStorageInfo &s : QStorageInfo::mountedVolumes()) {
            const QString r = s.rootPath();
            if (r == QLatin1String("/") || !(r.startsWith(QLatin1String("/media/")) || r.startsWith(QLatin1String("/mnt/")) || r.startsWith(QLatin1String("/run/media/"))))
                continue;
            node(comp, s.displayName(), QStringLiteral("drive-harddisk"), r, true);
        }
        node(root, tr7("Network"), QStringLiteral("seven-bf-network"), QString(), false);
        node(root, tr7("Control Panel"), QStringLiteral("preferences-system"), QString(), false);
        node(root, tr7("Recycle Bin"), QStringLiteral("user-trash"), QString(), false);
        const auto dd = QDir(desk).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase | QDir::LocaleAware);
        for (const QFileInfo &d : dd) node(root, d.fileName(), QStringLiteral("folder"), d.absoluteFilePath(), hasSubdirs(d.absoluteFilePath()));
        root->setExpanded(true);

        // preselect the start folder (Windows opens on the user folder by default)
        QTreeWidgetItem *sel = nullptr;
        const QString want = start.isEmpty() ? QDir::homePath() : QFileInfo(start).absoluteFilePath();
        for (int i = 0; i < root->childCount() && !sel; ++i) {
            QTreeWidgetItem *c = root->child(i);
            const QString cp = c->data(0, PathRole).toString();
            if (cp.isEmpty()) {                                  // Libraries / Computer: look one level down
                for (int j = 0; j < c->childCount(); ++j) {
                    if (c->child(j)->data(0, PathRole).toString() == want) { c->setExpanded(true); sel = c->child(j); break; }
                }
            } else if (cp == want) {
                sel = c;
            }
        }
        if (!sel) {                                              // walk down from the user folder
            for (int i = 0; i < root->childCount(); ++i) {
                if (root->child(i)->data(0, PathRole).toString() == QDir::homePath()) { sel = root->child(i); break; }
            }
            if (sel && want.startsWith(QDir::homePath() + QLatin1Char('/'))) {
                const QStringList parts = want.mid(QDir::homePath().size() + 1).split(QLatin1Char('/'), Qt::SkipEmptyParts);
                for (const QString &part : parts) {
                    sel->setExpanded(true); fill(sel);
                    QTreeWidgetItem *next = nullptr;
                    for (int j = 0; j < sel->childCount(); ++j) if (sel->child(j)->text(0) == part) { next = sel->child(j); break; }
                    if (!next) break;
                    sel = next;
                }
            }
        }
        if (sel) { m_tree->setCurrentItem(sel); m_tree->scrollToItem(sel); }
    }

    QTreeWidget *m_tree = nullptr;
    QLineEdit *m_edit = nullptr;
    QPushButton *m_ok = nullptr;
};

inline QString getFolder(QWidget *parent, const QString &prompt, const QString &start = QString())
{
    Dialog d(parent, prompt, start);
    return d.exec() == QDialog::Accepted ? d.path() : QString();
}
}
