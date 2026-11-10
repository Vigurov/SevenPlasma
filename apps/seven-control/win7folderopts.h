/*
 * Folder Options (Control Panel / Explorer "Organize > Folder and search options"): the three pages of the
 * Windows 7 dialog laid out from shell32.dll's templates (dialogs 29959–29961), wired to what Dolphin and
 * KDE really have. Options with no Linux counterpart stay visible but greyed out, as Windows greys what
 * does not apply.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QFile>
#include <QButtonGroup>
#include <QDir>
#include <QGroupBox>
#include <QMouseEvent>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <sys/xattr.h>

namespace Win7FolderOpts
{

/** Dialog units of the templates -> pixels (Segoe UI 9: 7 x 15 base units) */
inline QRect dlu(int x, int y, int w, int h)
{
    auto px = [](int v) { return int(std::lround(v * 7 / 4.0)); };
    auto py = [](int v) { return int(std::lround(v * 15 / 8.0)); };
    return QRect(px(x), py(y), px(w), py(h));
}

/* ---------------- Dolphin's view properties (stored in an extended attribute) ---------------- */

inline QString globalPropsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/dolphin/view_properties/global");
}

inline QString readProps(const QString &dir)
{
    char buf[8192];
    const ssize_t n = getxattr(QFile::encodeName(dir).constData(), "user.kde.fm.viewproperties#1", buf, sizeof buf);
    return n > 0 ? QString::fromUtf8(buf, int(n)) : QString();
}

inline void writeProps(const QString &dir, const QString &props)
{
    QDir().mkpath(dir);
    const QByteArray data = props.toUtf8();
    setxattr(QFile::encodeName(dir).constData(), "user.kde.fm.viewproperties#1", data.constData(), size_t(data.size()), 0);
}

/** key=value inside the [Dolphin] group of the properties text */
inline QString propValue(const QString &props, const QString &key)
{
    for (const QString &line : props.split(QLatin1Char('\n'))) {
        if (line.startsWith(key + QLatin1Char('='))) {
            return line.mid(key.size() + 1);
        }
    }
    return {};
}

inline QString setPropValue(QString props, const QString &key, const QString &value)
{
    if (props.trimmed().isEmpty()) {
        props = QStringLiteral("[Dolphin]\nVersion=4\nViewMode=1\n");
    }
    QStringList lines = props.split(QLatin1Char('\n'));
    bool done = false;
    for (QString &line : lines) {
        if (line.startsWith(key + QLatin1Char('='))) {
            line = key + QLatin1Char('=') + value;
            done = true;
        } else if (line.startsWith(QLatin1String("Timestamp="))) {
            line = QStringLiteral("Timestamp=") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy,M,d,H,m,s.zzz"));
        }
    }
    if (!done) {
        while (!lines.isEmpty() && lines.last().isEmpty()) {
            lines.removeLast();
        }
        lines << key + QLatin1Char('=') + value << QString();
    }
    return lines.join(QLatin1Char('\n'));
}

/** The Windows 7 default folder view: Details with Name, Date modified, Type, Size */
inline QString defaultProps()
{
    return QStringLiteral("[Dolphin]\nTimestamp=%1\nVersion=4\nViewMode=1\nVisibleRoles=Details_text,Details_modificationtime,Details_type,Details_size\n")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy,M,d,H,m,s.zzz")));
}

/* ---------------- the dialog ---------------- */

class FolderOptionsDialog : public QDialog
{
public:
    explicit FolderOptionsDialog(const QString &folder = QString())
        : m_folder(folder)
    {
        setWindowTitle(QStringLiteral("Параметри папок"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cpi-folderopts")));
        setAttribute(Qt::WA_DeleteOnClose);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 7, 7, 7);
        m_tabs = new QTabWidget;
        m_tabs->addTab(generalPage(), QStringLiteral("Загальні"));
        m_tabs->addTab(viewPage(), QStringLiteral("Вигляд"));
        m_tabs->addTab(searchPage(), QStringLiteral("Пошук"));
        v->addWidget(m_tabs);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        m_apply = new QPushButton(QStringLiteral("Застосувати"));
        for (QPushButton *b : {ok, cancel, m_apply}) {
            b->setMinimumWidth(75);
            btns->addWidget(b);
        }
        ok->setDefault(true);
        m_apply->setEnabled(false);
        v->addLayout(btns);
        connect(ok, &QPushButton::clicked, this, [this] {
            save();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] {
            save();
            m_apply->setEnabled(false);
        });
        load();
        // only the user's own changes enable "Apply"
        for (QAbstractButton *b : findChildren<QAbstractButton *>()) {
            if (b->isCheckable()) {
                connect(b, &QAbstractButton::toggled, this, [this] { m_apply->setEnabled(true); });
            }
        }
        setFixedSize(sizeHint());
    }

private:
    QWidget *page()
    {
        auto *p = new QWidget;
        p->setFixedSize(dlu(0, 0, 240, 230).size());
        return p;
    }
    template<typename T>
    T *place(QWidget *parent, T *w, const QRect &r)
    {
        w->setParent(parent);
        w->setGeometry(r);
        return w;
    }
    /** Radio button / check box whose text wraps onto a second line (Qt's own text never wraps) */
    template<typename T>
    T *wrapped(QWidget *parent, T *b, const QString &text, const QRect &r)
    {
        place(parent, b, QRect(r.left(), r.top(), 18, 18));
        auto *l = place(parent, new QLabel(text), QRect(r.left() + 18, r.top() + 1, r.width() - 18, r.height()));
        l->setWordWrap(true);
        l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        l->setEnabled(b->isEnabled());
        // clicking the words clicks the button, like the real control
        l->installEventFilter(this);
        m_labelFor.insert(l, b);
        return b;
    }
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonRelease) {
            if (QAbstractButton *b = m_labelFor.value(static_cast<QWidget *>(o))) {
                if (b->isEnabled()) {
                    b->click();
                }
                return true;
            }
        }
        return QDialog::eventFilter(o, e);
    }

    QLabel *icon(QWidget *parent, const QString &name, int x, int y)
    {
        auto *l = place(parent, new QLabel, dlu(x, y, 21, 20));
        l->setPixmap(QIcon::fromTheme(name).pixmap(32, 32));
        l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        return l;
    }

    QWidget *generalPage()
    {
        QWidget *p = page();
        place(p, new QGroupBox(QStringLiteral("Огляд папок")), dlu(7, 7, 226, 38));
        m_browseIcon = icon(p, QStringLiteral("seven-fo-samewindow"), 14, 19);
        m_sameWindow = place(p, new QRadioButton(QStringLiteral("Відкривати кожну папку в тому самому вікні")), dlu(41, 17, 185, 9));
        m_ownWindow = place(p, new QRadioButton(QStringLiteral("Відкривати кожну папку в окремому вікні")), dlu(41, 30, 185, 9));
        auto *browse = new QButtonGroup(p);
        browse->addButton(m_sameWindow);
        browse->addButton(m_ownWindow);

        place(p, new QGroupBox(QStringLiteral("Клацання мишею")), dlu(7, 52, 226, 60));
        m_clickIcon = icon(p, QStringLiteral("seven-fo-doubleclick"), 14, 64);
        m_single = place(p, new QRadioButton(QStringLiteral("Відкривати одним клацанням, виділяти наведенням")), dlu(41, 64, 188, 9));
        // Dolphin underlines nothing; Windows greys these out too while double-click is chosen
        m_under1 = place(p, new QRadioButton(QStringLiteral("Підкреслювати підписи значків як у браузері")), dlu(54, 75, 172, 9));
        m_under2 = place(p, new QRadioButton(QStringLiteral("Підкреслювати підписи лише під час наведення")), dlu(54, 86, 172, 9));
        m_double = place(p, new QRadioButton(QStringLiteral("Відкривати подвійним, а виділяти одним клацанням")), dlu(41, 97, 185, 9));
        auto *click = new QButtonGroup(p);
        click->addButton(m_single);
        click->addButton(m_double);
        auto *under = new QButtonGroup(p);
        under->addButton(m_under1);
        under->addButton(m_under2);
        m_under2->setChecked(true);
        m_under1->setEnabled(false);
        m_under2->setEnabled(false);

        place(p, new QGroupBox(QStringLiteral("Область переходів")), dlu(7, 119, 226, 38));
        icon(p, QStringLiteral("seven-fo-navpane"), 14, 131);
        m_showAll = place(p, new QCheckBox(QStringLiteral("Показати всі папки")), dlu(41, 129, 185, 9));
        m_showAll->setEnabled(false); // the tree has no Desktop root like Explorer's
        m_autoExpand = place(p, new QCheckBox(QStringLiteral("Автоматично розгортати до поточної папки")), dlu(41, 142, 185, 9));

        auto *restore = place(p, new QPushButton(QStringLiteral("Відновити типові")), dlu(161, 161, 72, 14));
        connect(restore, &QPushButton::clicked, this, [this] {
            m_sameWindow->setChecked(true);
            m_double->setChecked(true);
            m_autoExpand->setChecked(false);
        });
        // the pictures follow the choice, as in Windows 7
        connect(m_ownWindow, &QRadioButton::toggled, this, [this](bool own) {
            m_browseIcon->setPixmap(QIcon::fromTheme(own ? QStringLiteral("seven-fo-newwindow") : QStringLiteral("seven-fo-samewindow")).pixmap(32, 32));
        });
        connect(m_single, &QRadioButton::toggled, this, [this](bool single) {
            m_clickIcon->setPixmap(QIcon::fromTheme(single ? QStringLiteral("seven-fo-singleclick") : QStringLiteral("seven-fo-doubleclick")).pixmap(32, 32));
        });
        return p;
    }

    QTreeWidgetItem *option(QTreeWidgetItem *parent, const QString &text, bool radio = false, bool enabled = true)
    {
        auto *it = new QTreeWidgetItem(parent);
        auto *w = radio ? static_cast<QAbstractButton *>(new QRadioButton(text)) : static_cast<QAbstractButton *>(new QCheckBox(text));
        w->setEnabled(enabled);
        w->setStyleSheet(QStringLiteral("background: transparent;"));
        m_tree->setItemWidget(it, 0, w);
        m_treeButtons.insert(it, w);
        return it;
    }
    QAbstractButton *button(QTreeWidgetItem *it) const
    {
        return m_treeButtons.value(it);
    }

    QWidget *viewPage()
    {
        QWidget *p = page();
        place(p, new QGroupBox(QStringLiteral("Подання папок")), dlu(10, 5, 220, 55));
        icon(p, QStringLiteral("seven-fo-folderviews"), 20, 20);
        auto *text = place(p, new QLabel(QStringLiteral("Подання цієї папки (наприклад, «Таблиця» або «Значки») можна застосувати до всіх папок.")),
                           dlu(56, 14, 169, 20));
        text->setWordWrap(true);
        auto *applyAll = place(p, new QPushButton(QStringLiteral("Застосувати до папок")), dlu(56, 35, 77, 15));
        auto *reset = place(p, new QPushButton(QStringLiteral("Скинути папки")), dlu(143, 35, 77, 15));
        // like Windows 7 opened from Control Panel: there is no "this folder" to copy from
        applyAll->setEnabled(!m_folder.isEmpty() && !readProps(m_folder).isEmpty());
        connect(applyAll, &QPushButton::clicked, this, [this] {
            writeProps(globalPropsDir(), readProps(m_folder));
        });
        connect(reset, &QPushButton::clicked, this, [] {
            // Dolphin forgets per-folder views older than this moment; the global one goes back to Details
            KConfigGroup g(KSharedConfig::openConfig(QStringLiteral("dolphinrc")), QStringLiteral("General"));
            g.writeEntry("ViewPropsTimestamp", QDateTime::currentDateTime());
            g.sync();
            const QString hidden = propValue(readProps(globalPropsDir()), QStringLiteral("HiddenFilesShown"));
            QString props = defaultProps();
            if (!hidden.isEmpty()) {
                props = setPropValue(props, QStringLiteral("HiddenFilesShown"), hidden);
            }
            writeProps(globalPropsDir(), props);
        });

        place(p, new QLabel(QStringLiteral("Додаткові параметри:")), dlu(10, 70, 108, 8));
        m_tree = place(p, new QTreeWidget, dlu(10, 80, 220, 120));
        m_tree->setHeaderHidden(true);
        m_tree->setRootIsDecorated(false);
        m_tree->setIndentation(18);
        m_tree->setSelectionMode(QAbstractItemView::NoSelection);
        m_tree->setFocusPolicy(Qt::NoFocus);
        auto *files = new QTreeWidgetItem(m_tree, {QStringLiteral("Файли й папки")});
        files->setIcon(0, QIcon::fromTheme(QStringLiteral("folder")));
        m_icons = option(files, QStringLiteral("Завжди відображати значки, а не ескізи"));
        m_menu = option(files, QStringLiteral("Завжди відображати меню"));
        m_fullPath = option(files, QStringLiteral("Відображати повний шлях у рядку заголовка"));
        auto *hidden = new QTreeWidgetItem(files, {QStringLiteral("Приховані файли й папки")});
        hidden->setIcon(0, QIcon::fromTheme(QStringLiteral("folder")));
        m_hide = option(hidden, QStringLiteral("Не відображати приховані файли, папки й диски"), true);
        m_show = option(hidden, QStringLiteral("Відображати приховані файли, папки й диски"), true);
        auto *hiddenGroup = new QButtonGroup(p);
        hiddenGroup->addButton(button(m_hide));
        hiddenGroup->addButton(button(m_show));
        option(files, QStringLiteral("Приховувати розширення для зареєстрованих типів файлів"), false, false);
        m_tips = option(files, QStringLiteral("Відображати спливаючі описи для папок і елементів робочого столу"));
        m_checks = option(files, QStringLiteral("Використовувати прапорці для вибору елементів"));
        m_restoreWindows = option(files, QStringLiteral("Відновлювати попередні вікна папок під час входу в систему"));
        auto *nav = new QTreeWidgetItem(m_tree, {QStringLiteral("Область переходів")});
        nav->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-fo-navpane")));
        m_navExpand = option(nav, QStringLiteral("Автоматично розгортати до поточної папки"));
        option(nav, QStringLiteral("Показати всі папки"), false, false);
        m_tree->expandAll();
        // the two copies of "expand to current folder" are one setting
        connect(button(m_navExpand), &QAbstractButton::toggled, m_autoExpand, &QCheckBox::setChecked);
        connect(m_autoExpand, &QCheckBox::toggled, button(m_navExpand), &QAbstractButton::setChecked);

        auto *restore = place(p, new QPushButton(QStringLiteral("Відновити типові")), dlu(160, 210, 70, 14));
        connect(restore, &QPushButton::clicked, this, [this] {
            button(m_icons)->setChecked(false);
            button(m_menu)->setChecked(false);
            button(m_fullPath)->setChecked(false);
            button(m_hide)->setChecked(true);
            button(m_tips)->setChecked(true);
            button(m_checks)->setChecked(false);
            button(m_restoreWindows)->setChecked(false);
            button(m_navExpand)->setChecked(false);
        });
        return p;
    }

    QWidget *searchPage()
    {
        QWidget *p = page();
        place(p, new QGroupBox(QStringLiteral("Що шукати")), dlu(7, 7, 224, 58));
        m_indexed = wrapped(p, new QRadioButton, QStringLiteral("У проіндексованих розташуваннях шукати імена та вміст файлів. У непроіндексованих — лише імена файлів."),
                            dlu(20, 18, 200, 20));
        m_always = wrapped(p, new QRadioButton, QStringLiteral("Завжди шукати імена та вміст файлів (це може тривати кілька хвилин)"), dlu(20, 39, 200, 20));
        auto *what = new QButtonGroup(p);
        what->addButton(m_indexed);
        what->addButton(m_always);

        place(p, new QGroupBox(QStringLiteral("Як шукати")), dlu(7, 72, 224, 78));
        // Dolphin always searches subfolders and matches parts of names
        auto *sub = new QCheckBox;
        sub->setChecked(true);
        sub->setEnabled(false);
        wrapped(p, sub, QStringLiteral("Включати вкладені папки в результати пошуку під час пошуку в папках"), dlu(20, 83, 200, 20));
        auto *partial = place(p, new QCheckBox(QStringLiteral("Шукати часткові збіги")), dlu(20, 104, 200, 10));
        partial->setChecked(true);
        partial->setEnabled(false);
        place(p, new QCheckBox(QStringLiteral("Використовувати пошук природною мовою")), dlu(20, 115, 200, 10))->setEnabled(false);
        m_noIndex = wrapped(p, new QCheckBox, QStringLiteral("Не використовувати індекс під час пошуку в папках (пошук може тривати довше)"), dlu(20, 126, 200, 20));

        place(p, new QGroupBox(QStringLiteral("Під час пошуку в непроіндексованих розташуваннях")), dlu(7, 158, 224, 38));
        place(p, new QCheckBox(QStringLiteral("Включати системні каталоги")), dlu(20, 169, 200, 10))->setEnabled(false);
        place(p, new QCheckBox(QStringLiteral("Включати стиснуті файли (ZIP, CAB...)")), dlu(20, 180, 200, 10))->setEnabled(false);

        auto *restore = place(p, new QPushButton(QStringLiteral("Відновити типові")), dlu(161, 202, 70, 14));
        connect(restore, &QPushButton::clicked, this, [this] {
            m_indexed->setChecked(true);
            m_noIndex->setChecked(false);
        });
        return p;
    }

    void load()
    {
        const KSharedConfig::Ptr dolphin = KSharedConfig::openConfig(QStringLiteral("dolphinrc"));
        const KConfigGroup general(dolphin, QStringLiteral("General"));
        const KConfigGroup fo(dolphin, QStringLiteral("Win7FolderOptions"));
        const KConfigGroup nav(dolphin, QStringLiteral("Win7NavigationPane"));
        const KConfigGroup search(dolphin, QStringLiteral("Search"));
        const KConfigGroup mainWindow(dolphin, QStringLiteral("MainWindow"));
        const KConfigGroup kde(KSharedConfig::openConfig(QStringLiteral("kdeglobals")), QStringLiteral("KDE"));
        const QString props = readProps(globalPropsDir());

        (fo.readEntry("OpenFoldersInOwnWindow", false) ? m_ownWindow : m_sameWindow)->setChecked(true);
        (kde.readEntry("SingleClick", false) ? m_single : m_double)->setChecked(true);
        m_autoExpand->setChecked(nav.readEntry("AutoExpandToCurrentFolder", false));
        button(m_icons)->setChecked(propValue(props, QStringLiteral("PreviewsShown")) == QLatin1String("false"));
        button(m_menu)->setChecked(mainWindow.readEntry("MenuBar", QStringLiteral("Disabled")) == QLatin1String("Enabled"));
        button(m_fullPath)->setChecked(general.readEntry("ShowFullPathInTitlebar", false));
        (propValue(props, QStringLiteral("HiddenFilesShown")) == QLatin1String("true") ? button(m_show) : button(m_hide))->setChecked(true);
        button(m_tips)->setChecked(general.readEntry("ShowToolTips", false));
        button(m_checks)->setChecked(general.readEntry("ShowSelectionToggle", true));
        button(m_restoreWindows)->setChecked(general.readEntry("RememberOpenedTabs", true));
        button(m_navExpand)->setChecked(m_autoExpand->isChecked());
        const bool filenameSearch = search.readEntry("SearchTool", QStringLiteral("Baloo")) == QLatin1String("Filenamesearch");
        const bool contents = search.readEntry("What", QStringLiteral("FileNames")) == QLatin1String("FileContents");
        (filenameSearch && contents ? m_always : m_indexed)->setChecked(true);
        m_noIndex->setChecked(filenameSearch && !contents);
    }

    void save()
    {
        KSharedConfig::Ptr dolphin = KSharedConfig::openConfig(QStringLiteral("dolphinrc"));
        KConfigGroup general(dolphin, QStringLiteral("General"));
        KConfigGroup fo(dolphin, QStringLiteral("Win7FolderOptions"));
        KConfigGroup nav(dolphin, QStringLiteral("Win7NavigationPane"));
        KConfigGroup search(dolphin, QStringLiteral("Search"));
        KConfigGroup mainWindow(dolphin, QStringLiteral("MainWindow"));
        fo.writeEntry("OpenFoldersInOwnWindow", m_ownWindow->isChecked());
        nav.writeEntry("AutoExpandToCurrentFolder", m_autoExpand->isChecked());
        mainWindow.writeEntry("MenuBar", button(m_menu)->isChecked() ? QStringLiteral("Enabled") : QStringLiteral("Disabled"));
        general.writeEntry("ShowFullPathInTitlebar", button(m_fullPath)->isChecked());
        general.writeEntry("ShowToolTips", button(m_tips)->isChecked());
        general.writeEntry("ShowSelectionToggle", button(m_checks)->isChecked());
        general.writeEntry("RememberOpenedTabs", button(m_restoreWindows)->isChecked());
        if (m_always->isChecked()) {
            search.writeEntry("SearchTool", QStringLiteral("Filenamesearch"));
            search.writeEntry("What", QStringLiteral("FileContents"));
        } else {
            search.writeEntry("SearchTool", m_noIndex->isChecked() ? QStringLiteral("Filenamesearch") : QStringLiteral("Baloo"));
            search.writeEntry("What", m_noIndex->isChecked() ? QStringLiteral("FileNames") : QStringLiteral("FileContents"));
        }
        dolphin->sync();

        QString props = readProps(globalPropsDir());
        if (props.isEmpty()) {
            props = defaultProps();
        }
        props = setPropValue(props, QStringLiteral("PreviewsShown"), button(m_icons)->isChecked() ? QStringLiteral("false") : QStringLiteral("true"));
        props = setPropValue(props, QStringLiteral("HiddenFilesShown"), button(m_show)->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        writeProps(globalPropsDir(), props);

        // single click is read live by every KDE program
        KConfigGroup kde(KSharedConfig::openConfig(QStringLiteral("kdeglobals")), QStringLiteral("KDE"));
        kde.writeEntry("SingleClick", m_single->isChecked(), KConfig::Notify);
        kde.sync();
    }

    QString m_folder;
    QHash<QWidget *, QAbstractButton *> m_labelFor;
    QTabWidget *m_tabs;
    QPushButton *m_apply;
    QLabel *m_browseIcon;
    QLabel *m_clickIcon;
    QRadioButton *m_sameWindow;
    QRadioButton *m_ownWindow;
    QRadioButton *m_single;
    QRadioButton *m_double;
    QRadioButton *m_under1;
    QRadioButton *m_under2;
    QCheckBox *m_showAll;
    QCheckBox *m_autoExpand;
    QTreeWidget *m_tree = nullptr;
    QHash<QTreeWidgetItem *, QAbstractButton *> m_treeButtons;
    QTreeWidgetItem *m_icons;
    QTreeWidgetItem *m_menu;
    QTreeWidgetItem *m_fullPath;
    QTreeWidgetItem *m_hide;
    QTreeWidgetItem *m_show;
    QTreeWidgetItem *m_tips;
    QTreeWidgetItem *m_checks;
    QTreeWidgetItem *m_restoreWindows;
    QTreeWidgetItem *m_navExpand;
    QRadioButton *m_indexed;
    QRadioButton *m_always;
    QCheckBox *m_noIndex;
};

} // namespace Win7FolderOpts
