#pragma once
#include "win7i18n.h"
/*
 * "Recycle Bin Properties" (right click on the Recycle Bin > Properties), after the user's photo 1 of a
 * Windows 7 setup: the picture on the left, the list of recycle bin locations with their free space, the
 * settings of the selected location, "Display delete confirmation dialog", OK / Cancel / Apply.
 *
 * What it changes (KIO's trash):
 *  - Custom size: ~/.config/ktrashrc, [<trash dir>] UseSizeLimit / Percent (the MB of the partition size)
 *  - Don't move files to the Recycle Bin: [<trash dir>] Win7DeleteImmediately, read by kio-win7's
 *    DeleteOrTrashJob, which then deletes instead of moving to the trash
 *  - Display delete confirmation dialog: ~/.config/kiorc [Confirmations] ConfirmTrash / ConfirmDelete
 */
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <unistd.h>

namespace Win7Trash
{
struct Location {
    QString name;      // "Локальний диск (C:)"
    QString trashDir;  // the KIO trash directory on that partition
    qint64 total = 0;
    qint64 free = 0;
    // settings
    bool deleteNow = false;
    int maxMb = 0;
};

inline QString trashConfig()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/ktrashrc");
}

inline QString freeText(qint64 bytes)
{
    const double gb = bytes / 1073741824.0;
    if (gb >= 1) {
        return QLocale{}.toString(gb, 'f', gb >= 100 ? 0 : 1) + tr7(" GB");
    }
    return QLocale{}.toString(bytes / 1048576.0, 'f', 0) + tr7(" MB");
}

/** The partitions with a recycle bin: the one Linux runs from (C:), then the other writable disks (D:, E:...) */
inline QList<Location> locations()
{
    static const QStringList fs = {QStringLiteral("ext4"), QStringLiteral("ext3"), QStringLiteral("btrfs"), QStringLiteral("xfs"), QStringLiteral("f2fs"),
                                   QStringLiteral("ntfs"), QStringLiteral("ntfs3"), QStringLiteral("fuseblk"), QStringLiteral("vfat"), QStringLiteral("exfat")};
    const QStorageInfo home(QDir::homePath());
    QList<Location> out;
    QList<QStorageInfo> others;
    for (const QStorageInfo &v : QStorageInfo::mountedVolumes()) {
        if (!v.isValid() || !v.isReady() || v.isReadOnly() || !fs.contains(QString::fromLatin1(v.fileSystemType()))) {
            continue;
        }
        if (v.rootPath().startsWith(QLatin1String("/boot")) || v.rootPath().startsWith(QLatin1String("/snap"))) {
            continue;
        }
        if (v.device() == home.device()) {
            Location l;
            l.name = tr7("Local Disk (C:)");
            l.trashDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
            l.total = v.bytesTotal();
            l.free = v.bytesAvailable();
            out.prepend(l);
        } else {
            others << v;
        }
    }
    char letter = 'D';
    for (const QStorageInfo &v : std::as_const(others)) {
        Location l;
        const QString label = v.displayName() == v.rootPath() ? tr7("Local Disk") : v.displayName();
        l.name = QStringLiteral("%1 (%2:)").arg(label).arg(QLatin1Char(letter++));
        l.trashDir = v.rootPath() + QStringLiteral("/.Trash-%1").arg(getuid());
        l.total = v.bytesTotal();
        l.free = v.bytesAvailable();
        out << l;
    }
    QSettings cfg(trashConfig(), QSettings::IniFormat);
    for (Location &l : out) {
        cfg.beginGroup(l.trashDir);
        const double percent = cfg.value(QStringLiteral("Percent"), 10.0).toDouble();
        l.maxMb = int(l.total / 1048576.0 * percent / 100.0);
        l.deleteNow = cfg.value(QStringLiteral("Win7DeleteImmediately"), false).toBool();
        cfg.endGroup();
    }
    return out;
}

inline void save(const QList<Location> &list, bool confirm)
{
    // ktrashrc is a KConfig file: the group names are paths, written as they are (QSettings would escape them)
    QFile f(trashConfig());
    QString text;
    if (f.open(QIODevice::ReadOnly)) {
        text = QString::fromUtf8(f.readAll());
        f.close();
    }
    QStringList lines = text.split(QLatin1Char('\n'));
    for (const Location &l : list) {
        const QString head = QLatin1Char('[') + l.trashDir + QLatin1Char(']');
        int start = lines.indexOf(head);
        if (start < 0) {
            while (!lines.isEmpty() && lines.last().isEmpty()) {
                lines.removeLast();
            }
            if (!lines.isEmpty()) {
                lines << QString();
            }
            lines << head;
            start = lines.size() - 1;
        }
        int end = start + 1;
        while (end < lines.size() && !lines.at(end).startsWith(QLatin1Char('['))) {
            ++end;
        }
        const double percent = l.total > 0 ? qBound(0.001, l.maxMb * 1048576.0 / l.total * 100.0, 100.0) : 10.0;
        const QMap<QString, QString> keys{{QStringLiteral("UseSizeLimit"), QStringLiteral("true")},
                                          {QStringLiteral("Percent"), QString::number(percent, 'f', 4)},
                                          {QStringLiteral("Win7DeleteImmediately"), l.deleteNow ? QStringLiteral("true") : QStringLiteral("false")}};
        for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
            bool done = false;
            for (int i = start + 1; i < end; ++i) {
                if (lines.at(i).section(QLatin1Char('='), 0, 0) == it.key()) {
                    lines[i] = it.key() + QLatin1Char('=') + it.value();
                    done = true;
                }
            }
            if (!done) {
                int at = end;
                while (at > start + 1 && lines.at(at - 1).isEmpty()) {
                    --at;
                }
                lines.insert(at, it.key() + QLatin1Char('=') + it.value());
                ++end;
            }
        }
    }
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(lines.join(QLatin1Char('\n')).toUtf8());
        if (!lines.last().isEmpty()) {
            f.write("\n");
        }
    }
    QSettings kio(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kiorc"), QSettings::IniFormat);
    kio.beginGroup(QStringLiteral("Confirmations"));
    kio.setValue(QStringLiteral("ConfirmTrash"), confirm);
    kio.setValue(QStringLiteral("ConfirmDelete"), confirm);
    kio.endGroup();
}

inline bool confirmation()
{
    QSettings kio(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kiorc"), QSettings::IniFormat);
    kio.beginGroup(QStringLiteral("Confirmations"));
    return kio.value(QStringLiteral("ConfirmTrash"), true).toBool();
}

/** True for the desktop's Recycle Bin link (a .desktop file pointing at trash:/) */
inline bool isTrashLink(const QString &path)
{
    if (!path.endsWith(QLatin1String(".desktop"))) {
        return false;
    }
    QSettings d(path, QSettings::IniFormat);
    d.beginGroup(QStringLiteral("Desktop Entry"));
    return d.value(QStringLiteral("URL")).toString().startsWith(QLatin1String("trash:"));
}

class RecycleBinDialog : public QDialog
{
public:
    RecycleBinDialog()
    {
        setWindowTitle(tr7("Recycle Bin Properties"));
        // the Recycle Bin's own icon, empty or full like on the desktop
        const bool full = !QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files"))
                               .isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        setWindowIcon(QIcon::fromTheme(full ? QStringLiteral("seven-desk-trash-full") : QStringLiteral("seven-desk-trash-empty"),
                                       QIcon::fromTheme(QStringLiteral("user-trash"))));
        setAttribute(Qt::WA_DeleteOnClose);
        m_locations = locations();

        // property sheets of Windows 7 are in "MS Shell Dlg 2" (Tahoma 8)
        QFont dlgFont(QStringLiteral("Tahoma"));
        dlgFont.setPointSize(8);
        setFont(dlgFont);

        // measured on the user's photo 2: the page 456 x 311, everything placed in it like the dialog resource
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(7, 7, 7, 9);
        v->setSpacing(3);
        auto *tabs = new QTabWidget;
        auto *page = new QWidget;
        page->setFixedSize(456, 304);
        auto *pic = new QLabel(page);
        QPixmap banner(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-control/recyclebin-banner.png")));
        if (banner.isNull()) {
            // the picture of the user's photo is in no file of Windows 7: a blue-green band with the
            // Recycle Bin and its reflection in its place
            banner = QPixmap(135, 307);
            QPainter bp(&banner);
            QLinearGradient g(0, 0, 0, 307);
            g.setColorAt(0, QColor(150, 166, 186));
            g.setColorAt(0.08, QColor(96, 142, 186));
            g.setColorAt(0.45, QColor(62, 148, 186));
            g.setColorAt(0.72, QColor(96, 198, 216));
            g.setColorAt(0.85, QColor(80, 200, 212));
            g.setColorAt(1, QColor(16, 164, 190));
            bp.fillRect(banner.rect(), g);
            bp.setRenderHint(QPainter::Antialiasing);
            QLinearGradient shine(0, 0, 135, 307);
            shine.setColorAt(0.35, QColor(255, 255, 255, 0));
            shine.setColorAt(0.5, QColor(255, 255, 255, 46));
            shine.setColorAt(0.65, QColor(255, 255, 255, 0));
            bp.fillRect(banner.rect(), shine);
            const QPixmap bin = windowIcon().pixmap(96, 96);
            bp.drawPixmap(20, 14, bin);
            bp.save();
            bp.setOpacity(0.22);
            bp.translate(0, 14 + 96 * 2 + 2);
            bp.scale(1, -1);
            bp.drawPixmap(20, 0, bin);
            bp.restore();
        }
        pic->setPixmap(banner);
        pic->setGeometry(0, 0, 135, 307);
        pic->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        m_list = new QTreeWidget(page);
        m_list->setGeometry(146, 13, 297, 98);
        m_list->setHeaderLabels({tr7("Recycle Bin Location"), tr7("Free Space")});
        m_list->setRootIsDecorated(false);
        m_list->setUniformRowHeights(true);
        // a classic list view: the selected row in the system blue, white text
        QPalette pal = m_list->palette();
        pal.setColor(QPalette::Active, QPalette::Highlight, QColor(0x33, 0x99, 0xff));
        pal.setColor(QPalette::Active, QPalette::HighlightedText, Qt::white);
        m_list->setPalette(pal);
        m_list->setColumnWidth(0, 128);
        for (const Location &l : std::as_const(m_locations)) {
            auto *it = new QTreeWidgetItem(m_list, {l.name, freeText(l.free)});
            it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-desk-trash-empty"), QIcon::fromTheme(QStringLiteral("user-trash")))); // as on Windows: the small Recycle Bin
            it->setSizeHint(0, QSize(0, 17));
        }

        auto *box = new QGroupBox(tr7("Settings for selected location"), page);
        box->setGeometry(146, 122, 297, 96);
        m_custom = new QRadioButton(tr7("Custom size:"), box);
        m_custom->move(10, 21);
        auto *sizeLabel = new QLabel(tr7("Maximum disk space in MB:"), box);
        sizeLabel->move(24, 39);
        m_size = new QLineEdit(box);
        m_size->setValidator(new QIntValidator(1, 99999999, m_size));
        m_size->setGeometry(201, 33, 55, 22);
        m_now = new QRadioButton(box);
        m_now->move(10, 61);
        auto *nowText = new QLabel(tr7("Don't move files to the Recycle Bin. Remove files immediately when deleted."), box);
        nowText->setWordWrap(true);
        nowText->setGeometry(27, 61, 260, 30);
        nowText->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        m_confirm = new QCheckBox(tr7("Display delete confirmation dialog"), page);
        m_confirm->move(156, 232);
        m_confirm->setChecked(confirmation());
        tabs->addTab(page, tr7("General"));
        v->addWidget(tabs, 1);

        auto *buttons = new QHBoxLayout;
        buttons->setContentsMargins(0, 0, 5, 0);
        buttons->setSpacing(8);
        buttons->addStretch(1);
        auto *ok = new QPushButton(QStringLiteral("OK"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        m_apply = new QPushButton(tr7("Apply"));
        for (QPushButton *b : {ok, cancel, m_apply}) {
            b->setFixedSize(73, 21);
            buttons->addWidget(b);
        }
        ok->setDefault(true);
        m_apply->setEnabled(false);
        v->addLayout(buttons);

        connect(m_list, &QTreeWidget::currentItemChanged, this, [this] {
            showLocation();
        });
        connect(m_custom, &QRadioButton::toggled, this, [this] {
            edited();
        });
        connect(m_size, &QLineEdit::textEdited, this, [this] {
            edited();
        });
        connect(m_confirm, &QCheckBox::toggled, this, [this] {
            m_apply->setEnabled(true);
        });
        connect(nowText, &QLabel::linkActivated, m_now, &QRadioButton::click);
        nowText->installEventFilter(this);
        m_nowText = nowText;
        connect(ok, &QPushButton::clicked, this, [this] {
            apply();
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_apply, &QPushButton::clicked, this, [this] {
            apply();
        });
        if (m_list->topLevelItemCount()) {
            m_list->setCurrentItem(m_list->topLevelItem(0));
        }
        setFixedSize(sizeHint());
        m_list->setFocus(); // the location starts selected in blue, as on Windows
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_nowText && e->type() == QEvent::MouseButtonRelease) {
            m_now->setChecked(true);
        }
        return QDialog::eventFilter(o, e);
    }

private:
    void showLocation()
    {
        const int i = m_list->indexOfTopLevelItem(m_list->currentItem());
        if (i < 0) {
            return;
        }
        m_loading = true;
        const Location &l = m_locations.at(i);
        (l.deleteNow ? m_now : m_custom)->setChecked(true);
        m_size->setText(QString::number(l.maxMb));
        m_size->setEnabled(!l.deleteNow);
        m_loading = false;
    }
    void edited()
    {
        m_size->setEnabled(m_custom->isChecked());
        if (m_loading) {
            return;
        }
        const int i = m_list->indexOfTopLevelItem(m_list->currentItem());
        if (i < 0) {
            return;
        }
        m_locations[i].deleteNow = m_now->isChecked();
        m_locations[i].maxMb = qMax(1, m_size->text().toInt());
        m_apply->setEnabled(true);
    }
    void apply()
    {
        save(m_locations, m_confirm->isChecked());
        m_apply->setEnabled(false);
    }

    QList<Location> m_locations;
    QTreeWidget *m_list;
    QRadioButton *m_custom, *m_now;
    QLabel *m_nowText = nullptr;
    QLineEdit *m_size;
    QCheckBox *m_confirm;
    QPushButton *m_apply;
    bool m_loading = false;
};
}
