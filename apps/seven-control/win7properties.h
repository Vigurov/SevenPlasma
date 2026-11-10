/*
 * The Properties dialog of Windows 7 for local files, folders, shortcuts and selections, laid out from the
 * templates of shell32.dll (General 1041/1042/1044, Shortcut 1040) and aclui.dll (Security 103).
 * KIO hands every local "Properties" over to `seven-control --properties <paths>` (kio-win7, KPropertiesDialog).
 * What Linux has is wired: renaming, read-only (write bits), hidden (dot files and the folder's .hidden list),
 * the program a type opens with, the .desktop entries of a shortcut, the POSIX permissions as Windows'
 * Allow column, and the metadata KFileMetaData extracts. What it has not stays greyed out, as Windows greys
 * what a policy forbids.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7folderopts.h"
#include "win7header.h"

#include <KApplicationTrader>
#include <KDesktopFile>
#include <KFileMetaData/ExtractorCollection>
#include <KFileMetaData/PropertyInfo>
#include <KFileMetaData/SimpleExtractionResult>
#include <KIconDialog>
#include <KOpenWithDialog>
#include <KService>

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDirIterator>
#include <QFileInfo>
#include <QFrame>
#include <QHostInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QStorageInfo>
#include <QTabWidget>
#include <QThread>
#include <QTreeWidget>

#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>

namespace Win7Props
{

using Win7FolderOpts::dlu;

template<typename T>
T *place(QWidget *parent, T *w, const QRect &r)
{
    w->setParent(parent);
    w->setGeometry(r);
    return w;
}

/* ---------------- texts ---------------- */

/** "73,3 КБ (75 123 байт)": three significant digits, then the exact count, like Explorer */
inline QString sizeText(qint64 bytes, bool exact = true)
{
    const QLocale uk;
    QString shortText;
    if (bytes < 1024) {
        shortText = tr7("%1 bytes").arg(bytes);
        return shortText;
    }
    static const char *units[] = {"KB", "MB", "GB", "TB"};
    double v = bytes / 1024.0;
    int u = 0;
    while (v >= 1000 && u < 3) {
        v /= 1024.0;
        ++u;
    }
    const int decimals = v < 10 ? 2 : (v < 100 ? 1 : 0);
    // Explorer truncates instead of rounding
    const double p = std::pow(10.0, decimals);
    v = std::floor(v * p) / p;
    shortText = uk.toString(v, 'f', decimals) + QLatin1Char(' ') + tr7(units[u]);
    return exact ? tr7("%1 (%2 bytes)").arg(shortText, uk.toString(bytes)) : shortText;
}

/** "4 жовтня 2026 р., 13:33:12" */
inline QString dateText(const QDateTime &t)
{
    if (!t.isValid()) {
        return tr7("Unknown");
    }
    static const char *months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    const QDate d = t.date();
    return tr7("%2 %1, %3, %4").arg(d.day()).arg(tr7(months[d.month() - 1])).arg(d.year()).arg(t.toString(QStringLiteral("HH:mm:ss")));
}

inline QString userName(uid_t uid)
{
    const passwd *pw = getpwuid(uid);
    return pw ? QString::fromLocal8Bit(pw->pw_name) : QString::number(uid);
}

inline QString groupName(gid_t gid)
{
    const group *gr = getgrgid(gid);
    return gr ? QString::fromLocal8Bit(gr->gr_name) : QString::number(gid);
}

inline bool isShortcut(const QString &path)
{
    return path.endsWith(QLatin1String(".desktop")) && QFileInfo(path).isFile();
}

inline QString typeText(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isDir()) {
        return tr7("File folder");
    }
    if (isShortcut(path)) {
        return tr7("Shortcut (.desktop)");
    }
    const QMimeType mt = QMimeDatabase().mimeTypeForFile(path);
    const QString suffix = fi.suffix();
    return suffix.isEmpty() ? mt.comment() : QStringLiteral("%1 (.%2)").arg(mt.comment(), suffix);
}

inline QIcon fileIcon(const QString &path)
{
    if (isShortcut(path)) {
        const KDesktopFile df(path);
        if (!df.readIcon().isEmpty()) {
            return QIcon::fromTheme(df.readIcon(), QIcon::fromTheme(QStringLiteral("application-x-executable")));
        }
    }
    if (QFileInfo(path).isDir()) {
        return QIcon::fromTheme(QStringLiteral("folder"));
    }
    return QIcon::fromTheme(QMimeDatabase().mimeTypeForFile(path).iconName(), QIcon::fromTheme(QStringLiteral("unknown")));
}

/* ---------------- the hidden attribute: dot files and the folder's .hidden list ---------------- */

inline QStringList hiddenList(const QString &dir)
{
    QFile f(dir + QStringLiteral("/.hidden"));
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

inline bool fileHidden(const QString &path)
{
    const QFileInfo fi(path);
    return fi.fileName().startsWith(QLatin1Char('.')) || hiddenList(fi.absolutePath()).contains(fi.fileName());
}

inline bool setFileHidden(const QString &path, bool hidden)
{
    const QFileInfo fi(path);
    if (fi.fileName().startsWith(QLatin1Char('.'))) {
        return hidden;
    }
    QStringList list = hiddenList(fi.absolutePath());
    list.removeAll(fi.fileName());
    if (hidden) {
        list << fi.fileName();
    }
    QFile f(fi.absolutePath() + QStringLiteral("/.hidden"));
    if (list.isEmpty()) {
        f.remove();
        return true;
    }
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(list.join(QLatin1Char('\n')).toUtf8() + '\n');
    return true;
}

inline bool isReadOnly(const QString &path)
{
    struct stat st;
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 && !(st.st_mode & S_IWUSR);
}

inline void setReadOnly(const QString &path, bool ro)
{
    struct stat st;
    if (::stat(QFile::encodeName(path).constData(), &st) != 0) {
        return;
    }
    const mode_t mode = ro ? (st.st_mode & ~(S_IWUSR | S_IWGRP | S_IWOTH)) : (st.st_mode | S_IWUSR);
    ::chmod(QFile::encodeName(path).constData(), mode & 07777);
}

/* ---------------- totals of a folder or a selection, counted in a thread ---------------- */

struct Totals {
    qint64 size = 0;
    qint64 onDisk = 0;
    int files = 0;
    int folders = 0;
};

inline void addPath(Totals &t, const QString &path, const std::atomic_bool &stop)
{
    struct stat st;
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0) {
        return;
    }
    if (!S_ISDIR(st.st_mode)) {
        ++t.files;
        t.size += st.st_size;
        t.onDisk += qint64(st.st_blocks) * 512;
        return;
    }
    QDirIterator it(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext() && !stop) {
        const QString p = it.next();
        if (::lstat(QFile::encodeName(p).constData(), &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            ++t.folders;
        } else {
            ++t.files;
            t.size += st.st_size;
            t.onDisk += qint64(st.st_blocks) * 512;
        }
    }
}

/* ---------------- the dialog ---------------- */

class PropertiesDialog : public QDialog
{
public:
    explicit PropertiesDialog(const QStringList &paths)
        : m_paths(paths)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        const bool single = paths.size() == 1;
        const QString first = paths.value(0);
        const QFileInfo fi(first);
        setWindowTitle(tr7("%1 Properties").arg(single ? displayName(first) : fi.fileName() + QStringLiteral(", ...")));
        setWindowIcon(single ? fileIcon(first) : QIcon::fromTheme(QStringLiteral("document-multiple")));

        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 7, 7, 7);
        m_tabs = new QTabWidget;
        if (!single) {
            m_tabs->addTab(multiPage(), tr7("General"));
        } else if (isShortcut(first)) {
            m_tabs->addTab(filePage(), tr7("General"));
            m_tabs->addTab(shortcutPage(), tr7("Shortcut"));
            m_tabs->addTab(securityPage(), tr7("Security"));
            m_tabs->addTab(detailsPage(), tr7("Verbose"));
            m_tabs->setCurrentIndex(1); // Windows opens a shortcut on its Shortcut tab
        } else if (fi.isDir()) {
            m_tabs->addTab(folderPage(), tr7("General"));
            m_tabs->addTab(securityPage(), tr7("Security"));
        } else {
            m_tabs->addTab(filePage(), tr7("General"));
            m_tabs->addTab(securityPage(), tr7("Security"));
            m_tabs->addTab(detailsPage(), tr7("Verbose"));
        }
        v->addWidget(m_tabs);
        auto *btns = new QHBoxLayout;
        btns->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        m_apply = new QPushButton(tr7("Apply"));
        for (QPushButton *b : {ok, cancel, m_apply}) {
            b->setMinimumWidth(75);
            btns->addWidget(b);
        }
        ok->setDefault(true);
        m_apply->setEnabled(false);
        v->addLayout(btns);
        connect(ok, &QPushButton::clicked, this, [this] {
            if (apply()) {
                accept();
            }
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] {
            if (apply()) {
                m_apply->setEnabled(false);
            }
        });
        setFixedSize(sizeHint());
    }

    ~PropertiesDialog() override
    {
        m_stop = true;
        if (m_worker) {
            m_worker->wait();
        }
    }

private:
    static QString displayName(const QString &path)
    {
        if (isShortcut(path)) {
            const KDesktopFile df(path);
            if (!df.readName().isEmpty()) {
                return df.readName();
            }
        }
        return QFileInfo(path).fileName();
    }

    QWidget *page()
    {
        auto *p = new QWidget;
        p->setFixedSize(dlu(0, 0, 227, 215).size());
        return p;
    }

    /** Explorer's values are borderless read-only edits: they look like labels but can be selected */
    QLineEdit *value(QWidget *p, const QString &text, const QRect &r)
    {
        auto *e = place(p, new QLineEdit(text), r);
        e->setReadOnly(true);
        e->setFrame(false);
        e->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; }"));
        e->setCursorPosition(0);
        return e;
    }

    void rule(QWidget *p, int y)
    {
        // the etched line of a Windows dialog: grey over white
        auto *line = place(p, new QWidget, QRect(dlu(7, y, 213, 1).topLeft(), QSize(dlu(7, y, 213, 1).width(), 2)));
        line->setAttribute(Qt::WA_StyledBackground, true);
        line->setStyleSheet(QStringLiteral("border-top: 1px solid #a0a0a0; border-bottom: 1px solid #ffffff;"));
    }

    QLabel *bigIcon(QWidget *p, const QIcon &icon)
    {
        auto *l = place(p, new QLabel, dlu(7, 7, 21, 20));
        l->setPixmap(icon.pixmap(32, 32));
        return l;
    }

    void changed()
    {
        m_apply->setEnabled(true);
    }

    QCheckBox *check(QWidget *p, const QString &text, const QRect &r)
    {
        auto *c = place(p, new QCheckBox(text), r);
        connect(c, &QCheckBox::clicked, this, [this] { changed(); });
        return c;
    }

    void startCounting(const QStringList &paths, const std::function<void(const Totals &, bool)> &show)
    {
        m_worker = QThread::create([this, paths, show] {
            Totals t;
            QElapsedTimer timer;
            timer.start();
            for (const QString &p : paths) {
                addPath(t, p, m_stop);
            }
            QMetaObject::invokeMethod(this, [show, t] { show(t, true); }, Qt::QueuedConnection);
        });
        m_worker->start();
    }

    /* General of a file (1041) */
    QWidget *filePage()
    {
        QWidget *p = page();
        const QString path = m_paths.first();
        const QFileInfo fi(path);
        bigIcon(p, fileIcon(path));
        m_name = place(p, new QLineEdit(displayName(path)), dlu(56, 11, 164, 14));
        connect(m_name, &QLineEdit::textEdited, this, [this] { changed(); });
        rule(p, 32);
        place(p, new QLabel(tr7("Type of file:")), dlu(7, 40, 48, 9));
        value(p, typeText(path), dlu(56, 39, 164, 12));
        int y = 56;
        if (!isShortcut(path)) {
            place(p, new QLabel(tr7("Program:")), dlu(7, y, 48, 9));
            m_appIcon = place(p, new QLabel, dlu(56, y, 11, 10));
            m_appName = value(p, QString(), dlu(70, y - 1, 78, 12));
            auto *change = place(p, new QPushButton(tr7("Edit...")), dlu(149, y - 2, 70, 14));
            connect(change, &QPushButton::clicked, this, [this, path] { changeProgram(path); });
            showProgram(path);
        } else {
            place(p, new QLabel(tr7("Description:")), dlu(7, y, 48, 9));
            value(p, KDesktopFile(path).readComment(), dlu(56, y - 1, 164, 12));
        }
        rule(p, 72);
        place(p, new QLabel(tr7("Location:")), dlu(7, 80, 48, 9));
        value(p, fi.absolutePath(), dlu(56, 79, 164, 12));
        place(p, new QLabel(tr7("Size:")), dlu(7, 96, 48, 9));
        struct stat st;
        const bool ok = ::stat(QFile::encodeName(path).constData(), &st) == 0;
        value(p, ok ? sizeText(st.st_size) : QString(), dlu(56, 95, 164, 12));
        place(p, new QLabel(tr7("Size on disk:")), dlu(7, 112, 48, 9));
        value(p, ok ? sizeText(qint64(st.st_blocks) * 512) : QString(), dlu(56, 111, 164, 12));
        rule(p, 128);
        place(p, new QLabel(tr7("Created:")), dlu(7, 136, 48, 9));
        value(p, dateText(fi.birthTime().isValid() ? fi.birthTime() : fi.metadataChangeTime()), dlu(56, 135, 164, 12));
        place(p, new QLabel(tr7("Modified:")), dlu(7, 152, 48, 9));
        value(p, dateText(fi.lastModified()), dlu(56, 151, 164, 12));
        place(p, new QLabel(tr7("Accessed:", "file")), dlu(7, 168, 48, 9));
        value(p, dateText(fi.lastRead()), dlu(56, 167, 164, 12));
        rule(p, 184);
        attributes(p, 192, false);
        return p;
    }

    /** Read-only, Hidden and Advanced... (the Archive box Windows shows only on FAT) */
    void attributes(QWidget *p, int y, bool folder)
    {
        place(p, new QLabel(tr7("Attributes:")), dlu(7, y, 48, 9));
        const QString path = m_paths.first();
        if (folder) {
            m_readOnly = check(p, tr7("Read-only (Only applies to files in folder)"), dlu(56, y, 163, 10));
            m_readOnly->setTristate(true);
            m_readOnly->setCheckState(Qt::PartiallyChecked);
            m_hidden = check(p, tr7("Hidden"), dlu(56, y + 14, 80, 10));
            auto *adv = place(p, new QPushButton(tr7("More...")), dlu(149, y + 12, 70, 14));
            connect(adv, &QPushButton::clicked, this, [this] { advancedAttributes(); });
        } else {
            m_readOnly = check(p, tr7("Read-only"), dlu(56, y, 64, 10));
            m_readOnly->setChecked(isReadOnly(path));
            m_hidden = check(p, tr7("Hidden"), dlu(121, y, 58, 10));
            auto *adv = place(p, new QPushButton(tr7("More...")), dlu(180, y - 2, 40, 14));
            connect(adv, &QPushButton::clicked, this, [this] { advancedAttributes(); });
        }
        m_hidden->setChecked(fileHidden(path));
        // a dot file is hidden by its name: renaming it is the only way back
        m_hidden->setEnabled(!QFileInfo(path).fileName().startsWith(QLatin1Char('.')));
    }

    /* General of a folder (1044) */
    QWidget *folderPage()
    {
        QWidget *p = page();
        const QString path = m_paths.first();
        const QFileInfo fi(path);
        bigIcon(p, fileIcon(path));
        m_name = place(p, new QLineEdit(fi.fileName()), dlu(56, 11, 164, 14));
        m_name->setReadOnly(path == QDir::homePath() || path == QLatin1String("/"));
        connect(m_name, &QLineEdit::textEdited, this, [this] { changed(); });
        rule(p, 32);
        place(p, new QLabel(tr7("Type:")), dlu(7, 40, 48, 9));
        value(p, typeText(path), dlu(56, 39, 164, 12));
        place(p, new QLabel(tr7("Location:")), dlu(7, 56, 48, 9));
        value(p, fi.absolutePath(), dlu(56, 55, 164, 12));
        place(p, new QLabel(tr7("Size:")), dlu(7, 72, 48, 9));
        auto *size = value(p, tr7("Calculating...", "properties"), dlu(56, 71, 164, 12));
        place(p, new QLabel(tr7("Size on disk:")), dlu(7, 88, 48, 9));
        auto *disk = value(p, tr7("Calculating...", "properties"), dlu(56, 87, 164, 12));
        place(p, new QLabel(tr7("Contents:")), dlu(7, 104, 48, 9));
        auto *contains = value(p, tr7("Calculating...", "properties"), dlu(56, 103, 164, 12));
        rule(p, 120);
        place(p, new QLabel(tr7("Created:")), dlu(7, 128, 48, 9));
        value(p, dateText(fi.birthTime().isValid() ? fi.birthTime() : fi.metadataChangeTime()), dlu(56, 127, 164, 12));
        rule(p, 144);
        attributes(p, 152, true);
        startCounting({path}, [size, disk, contains](const Totals &t, bool) {
            size->setText(sizeText(t.size));
            disk->setText(sizeText(t.onDisk));
            contains->setText(tr7("%1 Files, %2 Folders").arg(QLocale{}.toString(t.files), QLocale{}.toString(t.folders)));
        });
        return p;
    }

    /* General of a selection (1042) */
    QWidget *multiPage()
    {
        QWidget *p = page();
        auto *icon = bigIcon(p, QIcon::fromTheme(QStringLiteral("document-multiple"), QIcon::fromTheme(QStringLiteral("folder"))));
        Q_UNUSED(icon)
        int files = 0, folders = 0;
        QSet<QString> types, dirs;
        for (const QString &path : std::as_const(m_paths)) {
            const QFileInfo fi(path);
            (fi.isDir() ? folders : files)++;
            types << typeText(path);
            dirs << fi.absolutePath();
        }
        place(p, new QLabel(tr7("%1 Files, %2 Folders").arg(files).arg(folders)), dlu(56, 11, 164, 10));
        rule(p, 32);
        place(p, new QLabel(tr7("Type:")), dlu(7, 40, 48, 9));
        value(p, types.size() == 1 ? tr7("All of type %1").arg(*types.begin()) : tr7("Multiple types"), dlu(56, 39, 164, 12));
        place(p, new QLabel(tr7("Location:")), dlu(7, 56, 48, 9));
        value(p, dirs.size() == 1 ? tr7("All in %1").arg(*dirs.begin()) : tr7("Various folders"), dlu(56, 55, 164, 12));
        place(p, new QLabel(tr7("Size:")), dlu(7, 72, 48, 9));
        auto *size = value(p, tr7("Calculating...", "properties"), dlu(56, 71, 164, 12));
        place(p, new QLabel(tr7("Size on disk:")), dlu(7, 88, 48, 9));
        auto *disk = value(p, tr7("Calculating...", "properties"), dlu(56, 87, 164, 12));
        rule(p, 104);
        place(p, new QLabel(tr7("Attributes:")), dlu(7, 112, 48, 9));
        m_readOnly = check(p, tr7("Read-only"), dlu(56, 112, 100, 10));
        m_hidden = check(p, tr7("Hidden"), dlu(56, 126, 100, 10));
        int ro = 0, hid = 0;
        for (const QString &path : std::as_const(m_paths)) {
            ro += isReadOnly(path) ? 1 : 0;
            hid += fileHidden(path) ? 1 : 0;
        }
        auto tri = [this](QCheckBox *c, int n) {
            c->setTristate(n > 0 && n < m_paths.size());
            c->setCheckState(n == 0 ? Qt::Unchecked : (n == m_paths.size() ? Qt::Checked : Qt::PartiallyChecked));
        };
        tri(m_readOnly, ro);
        tri(m_hidden, hid);
        m_readOnlyStart = m_readOnly->checkState();
        m_hiddenStart = m_hidden->checkState();
        startCounting(m_paths, [size, disk](const Totals &t, bool) {
            size->setText(sizeText(t.size));
            disk->setText(sizeText(t.onDisk));
        });
        return p;
    }

    void showProgram(const QString &path)
    {
        const QString mime = QMimeDatabase().mimeTypeForFile(path).name();
        const KService::Ptr service = KApplicationTrader::preferredService(mime);
        m_appIcon->setPixmap(QIcon::fromTheme(service ? service->icon() : QStringLiteral("unknown")).pixmap(16, 16));
        m_appName->setText(service ? service->name() : tr7("Unknown application"));
        m_appName->setCursorPosition(0);
    }

    /** "Change...": the Open With list; the choice becomes the default for the type, as in Windows */
    void changeProgram(const QString &path)
    {
        KOpenWithDialog dlg({QUrl::fromLocalFile(path)}, this);
        dlg.setWindowTitle(tr7("Open with"));
        dlg.hideRunInTerminal();
        dlg.hideNoCloseOnExit();
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        const KService::Ptr service = dlg.service();
        if (service) {
            KApplicationTrader::setPreferredService(QMimeDatabase().mimeTypeForFile(path).name(), service);
        }
        showProgram(path);
    }

    /** "Advanced Attributes": indexing is Baloo's folder exclusion, the NTFS-only ones stay grey */
    void advancedAttributes()
    {
        QDialog d(this);
        d.setWindowTitle(tr7("Advanced Attributes"));
        auto *v = new QVBoxLayout(&d);
        v->setContentsMargins(12, 12, 12, 10);
        auto *top = new QHBoxLayout;
        auto *ic = new QLabel;
        ic->setPixmap(fileIcon(m_paths.first()).pixmap(32, 32));
        top->addWidget(ic, 0, Qt::AlignTop);
        top->addWidget(Win7Programs::wrapped(tr7("Choose the settings you want for this folder.")), 1);
        v->addLayout(top);
        auto *g1 = new QGroupBox(tr7("Archive and Index attributes"));
        auto *g1v = new QVBoxLayout(g1);
        auto *archive = new QCheckBox(tr7("File is ready for archiving"));
        archive->setChecked(true);
        archive->setEnabled(false);
        auto *index = new QCheckBox(tr7("Allow this file to have contents indexed in addition to file properties"));
        g1v->addWidget(archive);
        g1v->addWidget(index);
        v->addWidget(g1);
        auto *g2 = new QGroupBox(tr7("Compress or Encrypt attributes"));
        auto *g2v = new QVBoxLayout(g2);
        for (const QString &t : {tr7("Compress contents to save disk space"), tr7("Encrypt contents to secure data")}) {
            auto *c = new QCheckBox(t);
            c->setEnabled(false);
            g2v->addWidget(c);
        }
        v->addWidget(g2);
        auto *bar = new QHBoxLayout;
        bar->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        bar->addWidget(ok);
        bar->addWidget(cancel);
        v->addLayout(bar);
        connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
        const QString path = QFileInfo(m_paths.first()).absoluteFilePath();
        const bool dir = QFileInfo(path).isDir();
        index->setEnabled(dir);
        QStringList excluded;
        {
            QProcess p;
            p.start(QStringLiteral("balooctl6"), {QStringLiteral("config"), QStringLiteral("show"), QStringLiteral("excludeFolders")});
            p.waitForFinished(3000);
            for (const QString &l : QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'))) {
                const QString t = l.trimmed();
                if (t.startsWith(QLatin1Char('/'))) {
                    excluded << QDir::cleanPath(t);
                }
            }
        }
        index->setChecked(!excluded.contains(path));
        const bool was = index->isChecked();
        if (d.exec() == QDialog::Accepted && dir && index->isChecked() != was) {
            QProcess::execute(QStringLiteral("balooctl6"),
                              {QStringLiteral("config"), index->isChecked() ? QStringLiteral("rm") : QStringLiteral("add"), QStringLiteral("excludeFolders"), path + QLatin1Char('/')});
        }
    }

    /* Shortcut (1040): the entries of a .desktop file */
    QWidget *shortcutPage()
    {
        QWidget *p = page();
        const QString path = m_paths.first();
        KDesktopFile df(path);
        const KConfigGroup g = df.desktopGroup();
        m_shortcutIcon = bigIcon(p, fileIcon(path));
        value(p, displayName(path), dlu(49, 12, 172, 12));
        rule(p, 31);
        const bool link = df.hasLinkType();
        const QString exec = g.readEntry("Exec");
        QString program = exec.section(QLatin1Char(' '), 0, 0);
        const QString found = QStandardPaths::findExecutable(program);
        place(p, new QLabel(tr7("Target type:")), dlu(7, 39, 57, 10));
        place(p, new QLabel(link ? tr7("Link") : tr7("Application")), dlu(64, 39, 156, 10));
        place(p, new QLabel(tr7("Location:")), dlu(7, 55, 57, 10));
        value(p, link ? QString() : QFileInfo(found.isEmpty() ? program : found).absolutePath().section(QLatin1Char('/'), -1), dlu(64, 54, 156, 12));
        place(p, new QLabel(tr7("Target:")), dlu(7, 71, 57, 10));
        m_target = place(p, new QLineEdit(link ? df.readUrl() : exec), dlu(64, 69, 156, 14));
        rule(p, 88);
        place(p, new QLabel(tr7("Start in:")), dlu(7, 98, 57, 10));
        m_workDir = place(p, new QLineEdit(g.readEntry("Path")), dlu(64, 96, 156, 14));
        m_workDir->setEnabled(!link);
        place(p, new QLabel(tr7("Shortcut key:")), dlu(7, 117, 57, 10));
        auto *key = place(p, new QLineEdit(tr7("None")), dlu(64, 115, 156, 14));
        key->setEnabled(false);
        place(p, new QLabel(tr7("Run:")), dlu(7, 136, 57, 10));
        auto *run = place(p, new QComboBox, dlu(64, 134, 156, 14));
        run->addItems({tr7("Normal window"), tr7("Minimized"), tr7("Maximized")});
        run->setEnabled(false);
        place(p, new QLabel(tr7("Comment:", "shortcut")), dlu(7, 154, 57, 10));
        m_comment = place(p, new QLineEdit(df.readComment()), dlu(64, 152, 156, 14));
        for (QLineEdit *e : {m_target, m_workDir, m_comment}) {
            connect(e, &QLineEdit::textEdited, this, [this] { changed(); });
        }
        auto *where = place(p, new QPushButton(tr7("Open File Location")), dlu(9, 170, 80, 14));
        where->setEnabled(!link && !found.isEmpty());
        connect(where, &QPushButton::clicked, this, [found] { QProcess::startDetached(QStringLiteral("dolphin"), {QStringLiteral("--select"), found}); });
        auto *icon = place(p, new QPushButton(tr7("Change Icon...")), dlu(93, 170, 62, 14));
        connect(icon, &QPushButton::clicked, this, [this] {
            const QString name = KIconDialog::getIcon(KIconLoader::Desktop, KIconLoader::Application, false, 32, false, this);
            if (!name.isEmpty()) {
                m_newIcon = name;
                m_shortcutIcon->setPixmap(QIcon::fromTheme(name).pixmap(32, 32));
                changed();
            }
        });
        auto *adv = place(p, new QPushButton(tr7("Advanced...")), dlu(159, 170, 61, 14));
        adv->setEnabled(false);
        return p;
    }

    /* Security (aclui 103): owner, group and everyone with Windows' permission rows */
    QWidget *securityPage()
    {
        QWidget *p = page();
        const QString path = m_paths.first();
        struct stat st;
        ::stat(QFile::encodeName(path).constData(), &st);
        m_mode = st.st_mode & 07777;
        m_uid = st.st_uid;
        place(p, new QLabel(tr7("Object name:")), dlu(7, 7, 50, 8));
        value(p, path, dlu(60, 6, 160, 10));
        place(p, new QLabel(tr7("Group or user names:")), dlu(7, 25, 213, 8));
        m_principals = place(p, new QTreeWidget, dlu(7, 35, 213, 43));
        m_principals->setHeaderHidden(true);
        m_principals->setRootIsDecorated(false);
        const QString host = QHostInfo::localHostName();
        auto add = [this](const QString &icon, const QString &text) {
            auto *it = new QTreeWidgetItem(m_principals, {text});
            it->setIcon(0, QIcon::fromTheme(icon));
        };
        add(QStringLiteral("user-identity"), QStringLiteral("%1 (%2\\%1)").arg(userName(st.st_uid), host));
        add(QStringLiteral("system-users"), QStringLiteral("%1 (%2\\%1)").arg(groupName(st.st_gid), host));
        add(QStringLiteral("system-users"), tr7("All"));
        place(p, new QLabel(tr7("To change permissions, click Edit.")), dlu(7, 84, 150, 8));
        auto *edit = place(p, new QPushButton(tr7("Edit...")), dlu(160, 82, 60, 14));
        edit->setEnabled(st.st_uid == getuid());
        m_permsFor = place(p, new QLabel, dlu(7, 104, 125, 8));
        place(p, new QLabel(tr7("Allow")), dlu(130, 104, 42, 8))->setAlignment(Qt::AlignHCenter);
        place(p, new QLabel(tr7("Deny")), dlu(172, 104, 46, 8))->setAlignment(Qt::AlignHCenter);
        m_perms = place(p, new QTreeWidget, dlu(7, 114, 213, 61));
        m_perms->setHeaderHidden(true);
        m_perms->setRootIsDecorated(false);
        m_perms->setColumnCount(3);
        m_perms->setColumnWidth(0, dlu(0, 0, 128, 0).width());
        m_perms->setColumnWidth(1, dlu(0, 0, 40, 0).width());
        m_perms->setSelectionMode(QAbstractItemView::NoSelection);
        // all six rows at once, as in Windows' check list
        m_perms->setUniformRowHeights(true);
        m_perms->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_perms->setStyleSheet(QStringLiteral("QTreeView::item { height: 16px; padding: 0; }"));
        m_perms->header()->setStretchLastSection(true);
        place(p, new QLabel(tr7("For special permissions or advanced settings, click Advanced.")), dlu(7, 179, 148, 24))->setWordWrap(true);
        place(p, new QPushButton(tr7("Advanced")), dlu(160, 179, 60, 14))->setEnabled(false);
        connect(m_principals, &QTreeWidget::currentItemChanged, this, [this] { showPermissions(); });
        m_principals->setCurrentItem(m_principals->topLevelItem(0));
        connect(edit, &QPushButton::clicked, this, [this] { editPermissions(); });
        return p;
    }

    static QStringList permissionNames()
    {
        return {tr7("Full control"), tr7("Modify", "permission"), tr7("Read & execute"), tr7("Read"), tr7("Write"),
                tr7("Special permissions")};
    }

    /** rwx of one principal (0 owner, 1 group, 2 everyone) -> Windows' Allow rows */
    static QList<bool> allowRows(int bits)
    {
        const bool r = bits & 4, w = bits & 2, x = bits & 1;
        return {r && w && x, r && w && x, r && x, r, w, false};
    }

    int principal() const
    {
        return m_principals->currentItem() ? m_principals->indexOfTopLevelItem(m_principals->currentItem()) : 0;
    }

    void showPermissions()
    {
        const int who = principal();
        m_permsFor->setText(tr7("Permissions for %1").arg(m_principals->currentItem() ? m_principals->currentItem()->text(0).section(QLatin1Char(' '), 0, 0) : QString()));
        const QList<bool> allow = allowRows((m_mode >> (6 - 3 * who)) & 7);
        m_perms->clear();
        const QStringList names = permissionNames();
        for (int i = 0; i < names.size(); ++i) {
            auto *it = new QTreeWidgetItem(m_perms, {names.at(i), QString(), QString()});
            // read-only marks, like the greyed ticks of Windows' page
            it->setIcon(1, allow.at(i) ? QIcon::fromTheme(QStringLiteral("checkmark")) : QIcon());
        }
    }

    /** "Edit...": the same rows with check boxes; Allow maps to the read, write and execute bits */
    void editPermissions()
    {
        QDialog d(this);
        d.setWindowTitle(tr7("Permissions for %1").arg(QFileInfo(m_paths.first()).fileName()));
        auto *v = new QVBoxLayout(&d);
        v->addWidget(new QLabel(tr7("Group or user names:")));
        auto *who = new QTreeWidget;
        who->setHeaderHidden(true);
        who->setRootIsDecorated(false);
        for (int i = 0; i < m_principals->topLevelItemCount(); ++i) {
            auto *src = m_principals->topLevelItem(i);
            auto *it = new QTreeWidgetItem(who, {src->text(0)});
            it->setIcon(0, src->icon(0));
        }
        who->setFixedHeight(80);
        v->addWidget(who);
        auto *label = new QLabel;
        v->addWidget(label);
        auto *grid = new QGridLayout;
        grid->addWidget(new QLabel(tr7("Allow")), 0, 1);
        const QStringList names{tr7("Read"), tr7("Write"), tr7("Traverse folder / execute file")};
        QList<QCheckBox *> boxes;
        for (int i = 0; i < 3; ++i) {
            grid->addWidget(new QLabel(names.at(i)), i + 1, 0);
            auto *c = new QCheckBox;
            grid->addWidget(c, i + 1, 1, Qt::AlignHCenter);
            boxes << c;
        }
        v->addLayout(grid);
        mode_t mode = m_mode;
        bool loading = false;
        auto load = [&] {
            loading = true;
            const int w = qMax(0, who->indexOfTopLevelItem(who->currentItem()));
            label->setText(tr7("Permissions for %1").arg(who->currentItem() ? who->currentItem()->text(0).section(QLatin1Char(' '), 0, 0) : QString()));
            const int bits = (mode >> (6 - 3 * w)) & 7;
            boxes[0]->setChecked(bits & 4);
            boxes[1]->setChecked(bits & 2);
            boxes[2]->setChecked(bits & 1);
            loading = false;
        };
        for (int i = 0; i < 3; ++i) {
            connect(boxes[i], &QCheckBox::toggled, &d, [&, i](bool on) {
                if (loading) {
                    return;
                }
                const int w = qMax(0, who->indexOfTopLevelItem(who->currentItem()));
                const mode_t bit = mode_t(4 >> i) << (6 - 3 * w);
                mode = on ? (mode | bit) : (mode & ~bit);
            });
        }
        connect(who, &QTreeWidget::currentItemChanged, &d, [&] { load(); });
        who->setCurrentItem(who->topLevelItem(principal()));
        auto *bar = new QHBoxLayout;
        bar->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        bar->addWidget(ok);
        bar->addWidget(cancel);
        v->addLayout(bar);
        connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
        d.resize(360, d.sizeHint().height());
        if (d.exec() == QDialog::Accepted && mode != m_mode) {
            if (::chmod(QFile::encodeName(m_paths.first()).constData(), mode) == 0) {
                m_mode = mode;
                showPermissions();
            } else {
                QMessageBox::warning(this, windowTitle(), tr7("The permissions could not be changed."));
            }
        }
    }

    /* Details: what KFileMetaData reads from the file, then the file itself */
    QWidget *detailsPage()
    {
        QWidget *p = page();
        const QString path = m_paths.first();
        auto *tree = place(p, new QTreeWidget, dlu(7, 7, 213, 186));
        tree->setRootIsDecorated(false);
        tree->setHeaderLabels({tr7("Property"), tr7("Value")});
        Win7HeaderView::install(tree);
        tree->setColumnWidth(0, dlu(0, 0, 80, 0).width());
        auto section = [tree](const QString &title) {
            auto *it = new QTreeWidgetItem(tree, {title});
            QFont f = it->font(0);
            f.setBold(true);
            it->setFont(0, f);
            it->setForeground(0, QColor(0x1e, 0x32, 0x87));
            it->setFirstColumnSpanned(true);
            it->setFlags(Qt::ItemIsEnabled);
        };
        auto row = [tree](const QString &k, const QString &v) {
            new QTreeWidgetItem(tree, {k, v});
        };
        const QMimeType mt = QMimeDatabase().mimeTypeForFile(path);
        KFileMetaData::ExtractorCollection collection;
        KFileMetaData::SimpleExtractionResult result(path, mt.name(), KFileMetaData::ExtractionResult::ExtractMetaData);
        for (KFileMetaData::Extractor *ex : collection.fetchExtractors(mt.name())) {
            ex->extract(&result);
        }
        const auto props = result.properties();
        if (!props.isEmpty()) {
            section(tr7("Description"));
            QList<KFileMetaData::Property::Property> keys = props.uniqueKeys();
            for (KFileMetaData::Property::Property k : keys) {
                const KFileMetaData::PropertyInfo info(k);
                QStringList values;
                for (const QVariant &val : props.values(k)) {
                    values << info.formatAsDisplayString(val);
                }
                row(info.displayName(), values.join(QStringLiteral("; ")));
            }
        }
        const QFileInfo fi(path);
        struct stat st;
        ::stat(QFile::encodeName(path).constData(), &st);
        section(tr7("File"));
        row(tr7("Name"), fi.fileName());
        row(tr7("Item type"), typeText(path));
        row(tr7("Folder Path"), fi.absolutePath());
        row(tr7("Date created"), dateText(fi.birthTime().isValid() ? fi.birthTime() : fi.metadataChangeTime()));
        row(tr7("Date modified", "properties"), dateText(fi.lastModified()));
        row(tr7("Size"), sizeText(st.st_size, false));
        QStringList attrs;
        if (isReadOnly(path)) {
            attrs << QStringLiteral("R");
        }
        if (fileHidden(path)) {
            attrs << QStringLiteral("H");
        }
        row(tr7("Attributes"), attrs.join(QString()));
        row(tr7("Owner"), QStringLiteral("%1\\%2").arg(QHostInfo::localHostName(), userName(st.st_uid)));
        row(tr7("Computer"), tr7("%1 (This computer)").arg(QHostInfo::localHostName()));
        return p;
    }

    /* ---------------- OK / Apply ---------------- */

    bool apply()
    {
        const QString path = m_paths.first();
        if (m_paths.size() > 1) {
            for (const QString &p : std::as_const(m_paths)) {
                if (m_readOnly->checkState() != m_readOnlyStart && m_readOnly->checkState() != Qt::PartiallyChecked) {
                    setReadOnly(p, m_readOnly->checkState() == Qt::Checked);
                }
                if (m_hidden->checkState() != m_hiddenStart && m_hidden->checkState() != Qt::PartiallyChecked) {
                    setFileHidden(p, m_hidden->checkState() == Qt::Checked);
                }
            }
            m_readOnlyStart = m_readOnly->checkState();
            m_hiddenStart = m_hidden->checkState();
            return true;
        }
        const QFileInfo fi(path);
        const bool dir = fi.isDir();
        // attributes first, while the old name is still valid
        if (m_readOnly) {
            if (!dir) {
                setReadOnly(path, m_readOnly->isChecked());
            } else if (m_readOnly->checkState() != Qt::PartiallyChecked) {
                // the folder's box means the files in it
                QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    setReadOnly(it.next(), m_readOnly->checkState() == Qt::Checked);
                }
                m_readOnly->setCheckState(Qt::PartiallyChecked);
            }
        }
        if (m_hidden && m_hidden->isEnabled() && m_hidden->isChecked() != fileHidden(path)) {
            setFileHidden(path, m_hidden->isChecked());
        }
        if (isShortcut(path)) {
            KDesktopFile df(path);
            KConfigGroup g = df.desktopGroup();
            if (m_name && m_name->text() != displayName(path) && !m_name->text().trimmed().isEmpty()) {
                g.writeEntry("Name", m_name->text().trimmed());
                g.deleteEntry("Name[uk]");
            }
            if (m_target) {
                g.writeEntry(df.hasLinkType() ? "URL" : "Exec", m_target->text());
            }
            if (m_workDir && m_workDir->isEnabled()) {
                if (m_workDir->text().isEmpty()) {
                    g.deleteEntry("Path");
                } else {
                    g.writeEntry("Path", m_workDir->text());
                }
            }
            if (m_comment && m_comment->text() != df.readComment()) {
                g.writeEntry("Comment", m_comment->text());
                g.deleteEntry("Comment[uk]");
            }
            if (!m_newIcon.isEmpty()) {
                g.writeEntry("Icon", m_newIcon);
            }
            g.sync();
            return true;
        }
        if (m_name && !m_name->isReadOnly()) {
            const QString newName = m_name->text().trimmed();
            if (!newName.isEmpty() && newName != fi.fileName()) {
                if (newName.contains(QLatin1Char('/'))) {
                    QMessageBox::warning(this, windowTitle(), tr7("A file name can't contain the character \"/\"."));
                    return false;
                }
                const QString target = fi.absolutePath() + QLatin1Char('/') + newName;
                if (QFileInfo::exists(target)) {
                    QMessageBox::warning(this, windowTitle(), tr7("A file or folder named \"%1\" already exists.").arg(newName));
                    return false;
                }
                if (!QFile::rename(path, target)) {
                    QMessageBox::warning(this, windowTitle(), tr7("Could not rename \"%1\".").arg(fi.fileName()));
                    return false;
                }
                m_paths[0] = target;
                setWindowTitle(tr7("%1 Properties").arg(newName));
            }
        }
        return true;
    }

    QStringList m_paths;
    QTabWidget *m_tabs = nullptr;
    QPushButton *m_apply = nullptr;
    QLineEdit *m_name = nullptr;
    QLabel *m_appIcon = nullptr;
    QLineEdit *m_appName = nullptr;
    QCheckBox *m_readOnly = nullptr;
    QCheckBox *m_hidden = nullptr;
    Qt::CheckState m_readOnlyStart = Qt::Unchecked;
    Qt::CheckState m_hiddenStart = Qt::Unchecked;
    QLabel *m_shortcutIcon = nullptr;
    QLineEdit *m_target = nullptr;
    QLineEdit *m_workDir = nullptr;
    QLineEdit *m_comment = nullptr;
    QString m_newIcon;
    QTreeWidget *m_principals = nullptr;
    QTreeWidget *m_perms = nullptr;
    QLabel *m_permsFor = nullptr;
    mode_t m_mode = 0;
    uid_t m_uid = 0;
    QThread *m_worker = nullptr;
    std::atomic_bool m_stop{false};
};

} // namespace Win7Props
