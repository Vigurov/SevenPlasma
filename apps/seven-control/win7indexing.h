/*
 * Indexing Options (srchadmin.dll): the main dialog, "Indexed Locations" and the "Advanced Options" sheet laid
 * out from the dialog templates 101–104 of srchadmin.dll, the magnifier animation is its AVI 1301
 * (seven-aero/idx-anim.png). The index behind it is KDE's Baloo: IndexerConfig for the locations and
 * file types, balooctl6 for the status, pause, rebuild. Options with no Baloo counterpart stay greyed out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7folderopts.h"
#include "win7header.h"

#include <Baloo/IndexerConfig>
#include <KFileMetaData/ExtractorCollection>

#include <QCollator>
#include <QDialog>
#include <QGroupBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QStorageInfo>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>

namespace Win7Indexing
{

using Win7FolderOpts::dlu;

template<typename T>
T *place(QWidget *parent, T *w, const QRect &r)
{
    w->setParent(parent);
    w->setGeometry(r);
    return w;
}

/** "/home/user/" -> "/home/user" (Baloo keeps the trailing slash, the comparisons below do not want it) */
inline QString norm(QString p)
{
    p = QDir::cleanPath(p);
    while (p.size() > 1 && p.endsWith(QLatin1Char('/'))) {
        p.chop(1);
    }
    return p;
}

inline QStringList normList(const QStringList &l)
{
    QStringList out;
    for (const QString &p : l) {
        out << norm(p);
    }
    return out;
}

inline bool under(const QString &path, const QString &folder)
{
    return path == folder || folder == QLatin1String("/") || path.startsWith(folder + QLatin1Char('/'));
}

/** Baloo's rule: the deepest included or excluded folder above a path decides */
inline bool included(const QString &path, const QStringList &inc, const QStringList &exc)
{
    QString best;
    bool in = false;
    for (const QString &f : inc) {
        if (under(path, f) && f.size() > best.size()) {
            best = f;
            in = true;
        }
    }
    for (const QString &f : exc) {
        if (under(path, f) && f.size() > best.size()) {
            best = f;
            in = false;
        }
    }
    return in;
}

inline QString displayName(const QString &path)
{
    if (path == QLatin1String("/")) {
        return QStringLiteral("Локальний диск (/)");
    }
    return QFileInfo(path).fileName();
}

inline QIcon folderIcon(const QString &path)
{
    if (path == QLatin1String("/")) {
        return QIcon::fromTheme(QStringLiteral("drive-harddisk"));
    }
    if (path == norm(QDir::homePath())) {
        return QIcon::fromTheme(QStringLiteral("user-home"));
    }
    return QIcon::fromTheme(QStringLiteral("folder"));
}

/** The "Included Locations | Exclude" list of the main dialog and of the summary */
inline void fillSummary(QTreeWidget *list, const QStringList &inc, const QStringList &exc)
{
    list->clear();
    QStringList sorted = inc;
    QCollator coll(QLocale(QLocale::Ukrainian));
    std::sort(sorted.begin(), sorted.end(), [&coll](const QString &a, const QString &b) { return coll.compare(displayName(a), displayName(b)) < 0; });
    for (const QString &f : sorted) {
        QStringList ex;
        for (const QString &e : exc) {
            if (e != f && under(e, f)) {
                ex << displayName(e);
            }
        }
        auto *it = new QTreeWidgetItem(list, {displayName(f), ex.join(QStringLiteral("; "))});
        it->setIcon(0, folderIcon(f));
        it->setToolTip(0, f);
    }
}

inline QTreeWidget *summaryList()
{
    auto *list = new QTreeWidget;
    list->setRootIsDecorated(false);
    list->setHeaderLabels({QStringLiteral("Включені розташування"), QStringLiteral("Виключити")});
    Win7HeaderView::install(list);
    list->header()->setSectionsClickable(false);
    return list;
}

/* ---------------- Indexed Locations (dialog 102) ---------------- */

class LocationsDialog : public QDialog
{
public:
    QStringList inc, exc;

    LocationsDialog(QWidget *parent, const QStringList &includes, const QStringList &excludes)
        : QDialog(parent)
        , inc(includes)
        , exc(excludes)
    {
        setWindowTitle(QStringLiteral("Індексовані розташування"));
        setFixedSize(dlu(0, 0, 265, 334).size());
        place(this, new QGroupBox(QStringLiteral("Змінення вибраних розташувань")), dlu(7, 7, 251, 163));
        m_tree = place(this, new QTreeWidget, dlu(15, 18, 235, 144));
        m_tree->setHeaderHidden(true);
        place(this, new QGroupBox(QStringLiteral("Підсумок вибраних розташувань")), dlu(7, 176, 251, 123));
        m_summary = place(this, summaryList(), dlu(15, 187, 235, 104));
        auto *ok = place(this, new QPushButton(QStringLiteral("OK")), dlu(156, 313, 50, 14));
        auto *cancel = place(this, new QPushButton(QStringLiteral("Скасувати")), dlu(209, 313, 50, 14));
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

        // the Windows partition stays hidden: no NVMe mount is offered
        for (const QStorageInfo &s : QStorageInfo::mountedVolumes()) {
            if (s.device().startsWith("/dev/nvme") && s.rootPath() != QLatin1String("/")) {
                m_hidden << norm(s.rootPath());
            }
        }
        QStringList roots{QStringLiteral("/")};
        for (const QStorageInfo &s : QStorageInfo::mountedVolumes()) {
            const QString r = norm(s.rootPath());
            if ((r.startsWith(QLatin1String("/media/")) || r.startsWith(QLatin1String("/run/media/")) || r.startsWith(QLatin1String("/mnt/"))) && !m_hidden.contains(r)) {
                roots << r;
            }
        }
        for (const QString &r : roots) {
            auto *it = makeItem(nullptr, r);
            if (r != QLatin1String("/")) {
                it->setIcon(0, QIcon::fromTheme(QStringLiteral("drive-removable-media")));
                it->setText(0, QStringLiteral("%1 (%2)").arg(QStorageInfo(r).displayName(), r));
            }
        }
        connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *it) { populate(it); });
        connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it) {
            // only a click on the box counts, not the other changes of the item
            if (m_updating || it->checkState(0) == it->data(0, Qt::UserRole + 2).value<Qt::CheckState>()) {
                return;
            }
            toggle(it->data(0, Qt::UserRole).toString(), it->checkState(0) == Qt::Checked);
        });
        // open the tree down to the home folder, as Windows opens it to Users
        QTreeWidgetItem *cur = m_tree->topLevelItem(0);
        QString path = QStringLiteral("/");
        for (const QString &part : norm(QDir::homePath()).split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            cur->setExpanded(true);
            path = norm(path + QLatin1Char('/') + part);
            QTreeWidgetItem *next = nullptr;
            for (int i = 0; i < cur->childCount(); ++i) {
                if (cur->child(i)->data(0, Qt::UserRole).toString() == path) {
                    next = cur->child(i);
                }
            }
            if (!next) {
                break;
            }
            cur = next;
        }
        refreshStates();
    }

private:
    QTreeWidgetItem *makeItem(QTreeWidgetItem *parent, const QString &path)
    {
        auto *it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        it->setText(0, displayName(path));
        it->setIcon(0, folderIcon(path));
        it->setData(0, Qt::UserRole, path);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        it->setCheckState(0, Qt::Unchecked);
        it->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        return it;
    }

    void populate(QTreeWidgetItem *it)
    {
        if (it->data(0, Qt::UserRole + 1).toBool()) {
            return;
        }
        m_updating = true;
        it->setData(0, Qt::UserRole + 1, true);
        const QString path = it->data(0, Qt::UserRole).toString();
        static const QStringList virtualDirs{QStringLiteral("/proc"), QStringLiteral("/sys"), QStringLiteral("/dev"), QStringLiteral("/run"), QStringLiteral("/tmp"),
                                             QStringLiteral("/lost+found"), QStringLiteral("/snap")};
        QDir dir(path);
        const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &fi : entries) {
            const QString p = norm(fi.absoluteFilePath());
            if (virtualDirs.contains(p) || m_hidden.contains(p) || fi.fileName().startsWith(QLatin1Char('.'))) {
                continue;
            }
            auto *c = makeItem(it, p);
            if (QDir(p).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks).isEmpty()) {
                c->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicatorWhenChildless);
            }
        }
        if (it->childCount() == 0) {
            it->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicatorWhenChildless);
        }
        m_updating = false;
        refreshStates();
    }

    void toggle(const QString &path, bool on)
    {
        auto dropBelow = [&path](QStringList &l) {
            l.erase(std::remove_if(l.begin(), l.end(), [&path](const QString &f) { return under(f, path); }), l.end());
        };
        dropBelow(inc);
        dropBelow(exc);
        const QString parent = path == QLatin1String("/") ? QString() : norm(QFileInfo(path).path());
        const bool parentIn = !parent.isEmpty() && included(parent, inc, exc);
        if (on && !parentIn) {
            inc << path;
        } else if (!on && parentIn) {
            exc << path;
        }
        refreshStates();
    }

    void refreshStates()
    {
        m_updating = true;
        std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) {
            const QString p = it->data(0, Qt::UserRole).toString();
            Qt::CheckState st = Qt::Unchecked;
            if (included(p, inc, exc)) {
                st = Qt::Checked;
            } else {
                for (const QString &f : std::as_const(inc)) {
                    if (f != p && under(f, p)) {
                        st = Qt::PartiallyChecked;
                    }
                }
            }
            it->setCheckState(0, st);
            it->setData(0, Qt::UserRole + 2, st);
            for (int i = 0; i < it->childCount(); ++i) {
                walk(it->child(i));
            }
        };
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            walk(m_tree->topLevelItem(i));
        }
        m_updating = false;
        fillSummary(m_summary, inc, exc);
        m_summary->setColumnWidth(0, m_summary->width() / 2);
    }

    QTreeWidget *m_tree = nullptr;
    QTreeWidget *m_summary = nullptr;
    QStringList m_hidden;
    bool m_updating = false;
};

/* ---------------- Advanced Options (dialogs 103, 104) ---------------- */

inline void runBaloo(const QStringList &args)
{
    QProcess::startDetached(QStringLiteral("balooctl6"), args);
}

class AdvancedDialog : public QDialog
{
public:
    explicit AdvancedDialog(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("Додаткові параметри"));
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 7, 7, 7);
        auto *tabs = new QTabWidget;
        tabs->addTab(settingsPage(), QStringLiteral("Параметри індексування"));
        tabs->addTab(typesPage(), QStringLiteral("Типи файлів"));
        v->addWidget(tabs);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
        for (QPushButton *b : {ok, cancel}) {
            b->setMinimumWidth(75);
            btns->addWidget(b);
        }
        ok->setDefault(true);
        v->addLayout(btns);
        connect(ok, &QPushButton::clicked, this, [this] {
            save();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        setFixedSize(sizeHint());
    }

private:
    QWidget *page()
    {
        auto *p = new QWidget;
        p->setFixedSize(dlu(0, 0, 240, 240).size());
        return p;
    }

    QWidget *settingsPage()
    {
        QWidget *p = page();
        place(p, new QGroupBox(QStringLiteral("Параметри файлів")), dlu(7, 7, 222, 50));
        // Baloo neither reads encrypted files nor folds diacritics differently: greyed, as Windows greys a policy
        place(p, new QCheckBox(QStringLiteral("Індексувати зашифровані файли")), dlu(16, 18, 204, 10))->setEnabled(false);
        auto *dia = place(p, new QCheckBox, dlu(16, 32, 18, 10));
        dia->setEnabled(false);
        auto *diaText = place(p, new QLabel(QStringLiteral("Розглядати схожі слова з діакритичними знаками як різні слова")), dlu(16, 32, 204, 18).adjusted(19, 0, 0, 0));
        diaText->setWordWrap(true);
        diaText->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        diaText->setEnabled(false);

        place(p, new QGroupBox(QStringLiteral("Усунення неполадок")), dlu(7, 64, 222, 57));
        place(p, new QLabel(QStringLiteral("Видалити й перебудувати індекс")), dlu(16, 78, 150, 10));
        auto *rebuild = place(p, new QPushButton(QStringLiteral("Перебудувати")), dlu(170, 75, 50, 14));
        connect(rebuild, &QPushButton::clicked, this, [this] {
            if (QMessageBox::question(this, QStringLiteral("Перебудування індексу"),
                                      QStringLiteral("Перебудування індексу може тривати довго. Деякі подання й результати пошуку можуть бути неповними, "
                                                     "доки перебудування не завершиться."),
                                      QMessageBox::Ok | QMessageBox::Cancel)
                == QMessageBox::Ok) {
                runBaloo({QStringLiteral("purge")});
            }
        });
        auto *trouble = place(p, new Link(QStringLiteral("Усунення неполадок пошуку й індексування"), [] { runBaloo({QStringLiteral("check")}); }), dlu(16, 105, 168, 9));
        trouble->setWordWrap(false);

        place(p, new QGroupBox(QStringLiteral("Розташування індексу")), dlu(7, 128, 222, 86));
        place(p, new QLabel(QStringLiteral("Поточне розташування:")), dlu(16, 139, 204, 8));
        auto *cur = place(p, new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/baloo")), dlu(16, 149, 204, 13));
        cur->setReadOnly(true);
        place(p, new QLabel(QStringLiteral("Нове розташування після перезапуску служби:")), dlu(16, 166, 204, 8))->setEnabled(false);
        place(p, new QLineEdit, dlu(16, 176, 204, 13))->setEnabled(false);
        place(p, new QPushButton(QStringLiteral("Вибрати нове")), dlu(150, 193, 70, 14))->setEnabled(false);
        return p;
    }

    QWidget *typesPage()
    {
        QWidget *p = page();
        Baloo::IndexerConfig cfg;
        const QStringList filters = cfg.excludeFilters();
        const QStringList mimes = cfg.excludeMimetypes();
        m_types = place(p, new QTreeWidget, dlu(7, 7, 226, 159));
        m_types->setRootIsDecorated(false);
        m_types->setHeaderLabels({QStringLiteral("Розширення"), QStringLiteral("Опис фільтра")});
        Win7HeaderView::install(m_types);
        KFileMetaData::ExtractorCollection extractors;
        QMap<QString, QString> extMime; // sorted by extension
        const QMimeDatabase db;
        for (const QMimeType &m : db.allMimeTypes()) {
            for (const QString &s : m.suffixes()) {
                const QString ext = s.toLower();
                if (!ext.isEmpty() && !ext.contains(QLatin1Char('*')) && !extMime.contains(ext)) {
                    extMime.insert(ext, m.name());
                }
            }
        }
        for (auto it = extMime.cbegin(); it != extMime.cend(); ++it) {
            const bool content = !extractors.fetchExtractors(it.value()).isEmpty();
            auto *row = new QTreeWidgetItem(m_types, {it.key(), content ? QStringLiteral("Фільтр вмісту й властивостей") : QStringLiteral("Фільтр властивостей файлу")});
            row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
            row->setData(0, Qt::UserRole, it.value());
            row->setData(1, Qt::UserRole, content);
            const bool off = filters.contains(QStringLiteral("*.") + it.key()) || mimes.contains(it.value());
            row->setCheckState(0, off ? Qt::Unchecked : Qt::Checked);
        }
        m_types->setColumnWidth(0, 90);
        m_types->sortItems(0, Qt::AscendingOrder);

        // Baloo reads the contents of every type or of none, so the choice here is shared by all types
        place(p, new QGroupBox(QStringLiteral("Як слід індексувати цей файл?")), dlu(7, 170, 226, 40));
        m_props = place(p, new QRadioButton(QStringLiteral("Індексувати лише властивості")), dlu(16, 181, 207, 9));
        m_contents = place(p, new QRadioButton(QStringLiteral("Індексувати властивості й вміст файлу")), dlu(16, 194, 207, 9));
        (cfg.onlyBasicIndexing() ? m_props : m_contents)->setChecked(true);
        auto sync = [this] {
            QTreeWidgetItem *cur = m_types->currentItem();
            const bool on = cur && cur->checkState(0) == Qt::Checked;
            m_props->setEnabled(on);
            m_contents->setEnabled(on && cur->data(1, Qt::UserRole).toBool());
        };
        sync();
        connect(m_types, &QTreeWidget::currentItemChanged, this, sync);
        connect(m_types, &QTreeWidget::itemChanged, this, sync);

        place(p, new QLabel(QStringLiteral("Додати нове розширення до списку:")), dlu(7, 214, 225, 8));
        auto *edit = place(p, new QLineEdit, dlu(7, 225, 50, 12));
        auto *add = place(p, new QPushButton(QStringLiteral("Додати")), dlu(61, 224, 48, 14));
        add->setEnabled(false);
        connect(edit, &QLineEdit::textChanged, add, [add](const QString &t) { add->setEnabled(!t.trimmed().isEmpty()); });
        connect(add, &QPushButton::clicked, this, [this, edit] {
            QString ext = edit->text().trimmed().toLower();
            while (ext.startsWith(QLatin1Char('.')) || ext.startsWith(QLatin1Char('*'))) {
                ext.remove(0, 1);
            }
            if (ext.isEmpty()) {
                return;
            }
            const auto found = m_types->findItems(ext, Qt::MatchExactly, 0);
            QTreeWidgetItem *row = found.isEmpty() ? nullptr : found.first();
            if (!row) {
                row = new QTreeWidgetItem(m_types, {ext, QStringLiteral("Фільтр властивостей файлу")});
                row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
                row->setData(1, Qt::UserRole, false);
            }
            row->setCheckState(0, Qt::Checked);
            m_types->setCurrentItem(row);
            m_types->scrollToItem(row);
            edit->clear();
        });
        return p;
    }

    void save()
    {
        Baloo::IndexerConfig cfg;
        // keep Baloo's own patterns (moc_*.cpp, *~ ...) and rewrite only the "*.ext" ones
        QStringList filters;
        for (const QString &f : cfg.excludeFilters()) {
            if (!(f.startsWith(QLatin1String("*.")) && !f.mid(2).contains(QLatin1Char('*')))) {
                filters << f;
            }
        }
        QStringList mimes = cfg.excludeMimetypes();
        for (int i = 0; i < m_types->topLevelItemCount(); ++i) {
            QTreeWidgetItem *row = m_types->topLevelItem(i);
            const QString mime = row->data(0, Qt::UserRole).toString();
            if (row->checkState(0) == Qt::Checked) {
                mimes.removeAll(mime);
            } else if (!mimes.contains(mime)) {
                filters << QStringLiteral("*.") + row->text(0);
            }
        }
        cfg.setExcludeFilters(filters);
        cfg.setExcludeMimetypes(mimes);
        cfg.setOnlyBasicIndexing(m_props->isChecked());
        cfg.refresh();
    }

    QTreeWidget *m_types = nullptr;
    QRadioButton *m_props = nullptr;
    QRadioButton *m_contents = nullptr;
};

/* ---------------- Indexing Options (dialog 101) ---------------- */

class IndexingDialog : public QDialog
{
public:
    IndexingDialog()
    {
        setWindowTitle(QStringLiteral("Параметри індексування"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cpi-indexing")));
        setAttribute(Qt::WA_DeleteOnClose);
        setFixedSize(dlu(0, 0, 326, 313).size());
        m_anim = place(this, new QLabel, dlu(7, 7, 48, 48));
        m_anim->setFixedSize(48, 48);
        m_count = place(this, new QLabel, dlu(62, 7, 257, 14));
        QFont bf = m_count->font();
        bf.setBold(true);
        m_count->setFont(bf);
        m_state = place(this, new QLabel, dlu(62, 25, 257, 30));
        m_state->setWordWrap(true);
        m_state->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        place(this, new QLabel(QStringLiteral("Індексувати ці розташування:")), dlu(7, 62, 312, 14));
        m_list = place(this, summaryList(), dlu(7, 80, 311, 167));
        m_list->setColumnWidth(0, dlu(0, 0, 150, 0).width());
        auto *modify = place(this, new QPushButton(QStringLiteral("Змінити")), dlu(7, 251, 75, 14));
        auto *advanced = place(this, new QPushButton(QStringLiteral("Додатково")), dlu(86, 251, 75, 14));
        m_pause = place(this, new QPushButton(QStringLiteral("Призупинити")), dlu(165, 251, 75, 14));
        auto *trouble = place(this, new Link(QStringLiteral("Усунення неполадок пошуку й індексування"), [this] {
                                  runBaloo({QStringLiteral("check")});
                                  poll();
                              }),
                              dlu(7, 282, 200, 9));
        trouble->setWordWrap(false);
        auto *close = place(this, new QPushButton(QStringLiteral("Закрити")), dlu(244, 292, 75, 14));
        close->setDefault(true);
        connect(close, &QPushButton::clicked, this, &QDialog::close);
        connect(modify, &QPushButton::clicked, this, [this] {
            Baloo::IndexerConfig cfg;
            LocationsDialog d(this, normList(cfg.includeFolders()), normList(cfg.excludeFolders()));
            if (d.exec() == QDialog::Accepted) {
                QStringList inc, exc;
                for (const QString &f : std::as_const(d.inc)) {
                    inc << (f == QLatin1String("/") ? f : f + QLatin1Char('/'));
                }
                for (const QString &f : std::as_const(d.exc)) {
                    exc << (f == QLatin1String("/") ? f : f + QLatin1Char('/'));
                }
                cfg.setIncludeFolders(inc);
                cfg.setExcludeFolders(exc);
                cfg.refresh();
                loadLocations();
            }
        });
        connect(advanced, &QPushButton::clicked, this, [this] {
            AdvancedDialog d(this);
            d.exec();
            poll();
        });
        connect(m_pause, &QPushButton::clicked, this, [this] {
            if (m_mode == Disabled || m_mode == Stopped) {
                runBaloo({QStringLiteral("enable")});
            } else if (m_mode == Paused) {
                runBaloo({QStringLiteral("resume")});
            } else {
                runBaloo({QStringLiteral("suspend")});
            }
            QTimer::singleShot(700, this, [this] { poll(); });
        });
        connect(&m_frames, &QTimer::timeout, this, [this] {
            m_frame = (m_frame + 1) % 8;
            drawFrame();
        });
        m_frames.setInterval(100);
        connect(&m_poll, &QTimer::timeout, this, [this] { poll(); });
        m_poll.start(2000);
        drawFrame();
        loadLocations();
        m_count->setText(QStringLiteral("Проіндексовано елементів: 0"));
        m_state->setText(QStringLiteral("Очікування отримання стану індексування..."));
        poll();
    }

private:
    enum Mode { Disabled, Stopped, Idle, Working, Paused };

    void drawFrame()
    {
        const QImage strip = win7AeroImage(QStringLiteral("idx-anim.png"));
        m_anim->setPixmap(QPixmap::fromImage(strip.copy(m_frame * 48, 0, 48, 48)));
    }

    void loadLocations()
    {
        Baloo::IndexerConfig cfg;
        fillSummary(m_list, normList(cfg.includeFolders()), normList(cfg.excludeFolders()));
    }

    void poll()
    {
        if (m_busy) {
            return;
        }
        m_busy = true;
        auto *p = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
        env.insert(QStringLiteral("LANGUAGE"), QStringLiteral("C"));
        p->setProcessEnvironment(env);
        connect(p, &QProcess::finished, this, [this, p] {
            m_busy = false;
            p->deleteLater();
            applyStatus(QString::fromUtf8(p->readAllStandardOutput()));
        });
        p->start(QStringLiteral("balooctl6"), {QStringLiteral("status")});
    }

    void applyStatus(const QString &out)
    {
        Mode mode = Working;
        QString state;
        static const QRegularExpression stateRe(QStringLiteral("Indexer state: ([^\\n]+)"));
        static const QRegularExpression countRe(QStringLiteral("Total files indexed: (\\d+)"));
        const QString s = stateRe.match(out).captured(1).trimmed();
        if (!Baloo::IndexerConfig().fileIndexingEnabled() || out.contains(QLatin1String("currently disabled"))) {
            mode = Disabled;
        } else if (out.contains(QLatin1String("not running")) || s == QLatin1String("Not Running")) {
            mode = Stopped;
        } else if (s == QLatin1String("Idle")) {
            mode = Idle;
        } else if (s == QLatin1String("Suspended")) {
            mode = Paused;
        }
        switch (mode) {
        case Disabled:
        case Stopped:
            state = QStringLiteral("Індексування не виконується.");
            break;
        case Idle:
            state = QStringLiteral("Індексування завершено.");
            break;
        case Paused:
            state = QStringLiteral("Індексування призупинено.");
            break;
        case Working:
            if (s == QLatin1String("Low Battery")) {
                state = QStringLiteral("Індексування призупинено для збереження заряду акумулятора.");
            } else if (s == QLatin1String("Starting")) {
                state = QStringLiteral("Запуск індексування.");
            } else {
                state = QStringLiteral("Триває індексування. Протягом цього часу результати пошуку можуть бути неповними.");
            }
            break;
        }
        m_mode = mode;
        const auto cm = countRe.match(out);
        if (cm.hasMatch() || mode == Disabled) {
            m_count->setText(QStringLiteral("Проіндексовано елементів: %1").arg(QLocale(QLocale::Ukrainian).toString(cm.captured(1).toLongLong())));
        }
        m_state->setText(state);
        // Windows shows "Pause" only while it works; with Baloo turned off the same button turns it on
        m_pause->setVisible(mode != Idle);
        m_pause->setText(mode == Disabled || mode == Stopped ? QStringLiteral("Увімкнути") : (mode == Paused ? QStringLiteral("Продовжити") : QStringLiteral("Призупинити")));
        if (mode == Working) {
            if (!m_frames.isActive()) {
                m_frames.start();
            }
        } else {
            m_frames.stop();
            m_frame = 0;
            drawFrame();
        }
    }

    QLabel *m_anim = nullptr;
    QLabel *m_count = nullptr;
    QLabel *m_state = nullptr;
    QTreeWidget *m_list = nullptr;
    QPushButton *m_pause = nullptr;
    QTimer m_frames, m_poll;
    int m_frame = 0;
    bool m_busy = false;
    Mode m_mode = Working;
};

} // namespace Win7Indexing
