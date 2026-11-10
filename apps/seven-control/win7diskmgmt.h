/*
 * "Disk Management" (diskmgmt.msc) of Windows 7: the volume list, the graphical disk map, the
 * context menus, "New Simple Volume" wizard, Format, Shrink, Extend and Delete Volume.
 *
 *   disks and partitions -> lsblk + /sys (start sectors), drive letters like our Explorer's "Computer"
 *   changes              -> UDisks2 over D-Bus (polkit asks for the administrator password)
 *
 * Protected disks can't be changed at all (the menu items are greyed out and every operation checks
 * again right before it runs): the disk Linux runs from (/, /boot, /boot/efi, /home, swap) and any disk
 * with Windows on it (Microsoft reserved / recovery partitions, NTFS on an internal disk).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7wrapmenubar.h"
#include "win7states.h"
#include "win7chrome.h"

#include <KFilePlacesModel>
#include <Solid/Device>
#include <Solid/OpticalDrive>
#include <Solid/StorageAccess>
#include <Solid/StorageDrive>

#include <QCheckBox>
#include <cmath>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDialog>
#include <QEventLoop>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>

namespace Win7Disk
{
/* ---------------- data ---------------- */

struct Part {
    QString dev;      // /dev/sdb1, empty for unallocated space
    QString kname;    // sdb1
    qint64 start = 0; // bytes
    qint64 size = 0;
    QString fstype, fsver, label, mount, parttype, parttypeName;
    qint64 avail = -1, used = -1;
    int number = 0;
    bool unallocated = false;
    bool extended = false;
    bool logical = false;
    bool hidden = false; // Microsoft reserved: Windows 7 doesn't show it either
    QString letter;      // "E:"
};

struct Disk {
    QString dev, kname, model, tran;
    qint64 size = 0;
    QString pttype; // gpt, dos or empty
    bool removable = false;
    bool system = false;  // Linux runs from it
    bool windows = false; // Windows is installed on it
    bool optical = false;
    bool noMedia = false;
    bool wholeDiskFs = false;
    int index = 0;
    QList<Part> parts; // by start, with unallocated gaps
    bool isProtected() const
    {
        return system || windows;
    }
    QString protectReason() const
    {
        if (system) {
            return tr7("The system the computer is running from is installed on this disk. It cannot be changed.");
        }
        if (windows) {
            return tr7("Windows is installed on this disk. To avoid damaging it, changing this disk is not allowed.");
        }
        return QString();
    }
};

inline QString sizeText(qint64 bytes)
{
    const QLocale uk;
    const double kb = bytes / 1024.0, mb = kb / 1024.0, gb = mb / 1024.0, tb = gb / 1024.0;
    if (tb >= 1) {
        return uk.toString(tb, 'f', 2) + tr7(" TB");
    }
    if (gb >= 1) {
        return uk.toString(gb, 'f', 2) + tr7(" GB");
    }
    if (mb >= 1) {
        return uk.toString(qRound(mb)) + tr7(" MB");
    }
    return uk.toString(qRound(kb)) + tr7(" KB");
}

inline QString fsName(const QString &fs)
{
    static const QHash<QString, QString> m{{QStringLiteral("ntfs"), QStringLiteral("NTFS")},   {QStringLiteral("vfat"), QStringLiteral("FAT32")},
                                           {QStringLiteral("exfat"), QStringLiteral("exFAT")}, {QStringLiteral("ext4"), QStringLiteral("ext4")},
                                           {QStringLiteral("ext3"), QStringLiteral("ext3")},   {QStringLiteral("ext2"), QStringLiteral("ext2")},
                                           {QStringLiteral("btrfs"), QStringLiteral("Btrfs")}, {QStringLiteral("xfs"), QStringLiteral("XFS")},
                                           {QStringLiteral("swap"), tr7("Page File")}, {QStringLiteral("iso9660"), QStringLiteral("CDFS")},
                                           {QStringLiteral("udf"), QStringLiteral("UDF")}};
    return m.value(fs, fs.isEmpty() ? QString() : fs.toUpper());
}

/** UDisks object path of /dev/sdb1 */
inline QString udisksPath(const QString &kname)
{
    QString n = kname;
    n.replace(QLatin1Char('-'), QStringLiteral("_2d"));
    return QStringLiteral("/org/freedesktop/UDisks2/block_devices/") + n;
}

/** drive letters exactly like the "Computer" page of our Explorer: system disk C:, other disks, removable, optical */
inline QHash<QString, QString> driveLetters()
{
    struct Entry {
        QString kname;
        int kind;
    };
    QList<Entry> entries;
    static KFilePlacesModel *model = nullptr;
    if (!model) {
        model = new KFilePlacesModel(qApp);
        // Solid adds the devices a moment after the model is made
        QEventLoop wait;
        QTimer::singleShot(400, &wait, &QEventLoop::quit);
        wait.exec();
    }
    KFilePlacesModel &places = *model;
    for (int row = 0; row < places.rowCount(); ++row) {
        const QModelIndex idx = places.index(row, 0);
        if (!places.isDevice(idx) || places.isHidden(idx)) {
            continue;
        }
        const Solid::Device device = places.deviceForIndex(idx);
        Entry e{device.udi().section(QLatin1Char('/'), -1).replace(QStringLiteral("_2d"), QStringLiteral("-")), 1};
        const auto *access = device.as<Solid::StorageAccess>();
        if (!access) {
            continue; // phones and players get no letter
        }
        Solid::StorageDrive *drive = nullptr;
        for (Solid::Device p = device.parent(); p.isValid(); p = p.parent()) {
            if (p.is<Solid::StorageDrive>()) {
                drive = p.as<Solid::StorageDrive>();
                break;
            }
        }
        if (access->isAccessible() && access->filePath() == QLatin1String("/")) {
            e.kind = 0;
        } else if (drive && drive->driveType() == Solid::StorageDrive::CdromDrive) {
            e.kind = 3;
        } else if (drive && (drive->isRemovable() || drive->driveType() != Solid::StorageDrive::HardDisk)) {
            e.kind = 2;
        }
        entries.append(e);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.kind < b.kind;
    });
    QHash<QString, QString> out;
    char letter = 'C';
    for (const Entry &e : std::as_const(entries)) {
        if (letter > 'Z') {
            break;
        }
        out.insert(e.kname, QStringLiteral("%1:").arg(QLatin1Char(letter++)));
    }
    return out;
}

inline qint64 sysValue(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed().toLongLong() : -1;
}

inline QList<Disk> scan()
{
    QProcess p;
    p.start(QStringLiteral("lsblk"), {QStringLiteral("-J"), QStringLiteral("-b"), QStringLiteral("-o"),
                                       QStringLiteral("NAME,KNAME,PATH,TYPE,SIZE,FSTYPE,FSVER,LABEL,MOUNTPOINTS,FSAVAIL,FSUSED,PARTTYPE,PARTTYPENAME,PTTYPE,RM,HOTPLUG,TRAN,MODEL"),
                                       QStringLiteral("-e"), qEnvironmentVariableIsEmpty("WIN7_DISKMGMT_TEST") ? QStringLiteral("1,7,251,252") : QStringLiteral("1,251,252")}); // no ram, loop, zram
    p.waitForFinished(10000);
    const QJsonArray devs = QJsonDocument::fromJson(p.readAllStandardOutput()).object().value(QStringLiteral("blockdevices")).toArray();
    const QHash<QString, QString> letters = driveLetters();
    static const QStringList systemMounts{QStringLiteral("/"), QStringLiteral("/boot"), QStringLiteral("/boot/efi"), QStringLiteral("/home"), QStringLiteral("[SWAP]")};
    QList<Disk> disks;
    auto partOf = [&letters](const QJsonObject &o) {
        Part pt;
        pt.dev = o.value(QStringLiteral("path")).toString();
        pt.kname = o.value(QStringLiteral("kname")).toString();
        pt.size = o.value(QStringLiteral("size")).toVariant().toLongLong();
        pt.fstype = o.value(QStringLiteral("fstype")).toString();
        pt.fsver = o.value(QStringLiteral("fsver")).toString();
        pt.label = o.value(QStringLiteral("label")).toString();
        for (const QJsonValue &m : o.value(QStringLiteral("mountpoints")).toArray()) {
            if (!m.isNull() && pt.mount.isEmpty()) {
                pt.mount = m.toString();
            }
        }
        pt.avail = o.value(QStringLiteral("fsavail")).isNull() ? -1 : o.value(QStringLiteral("fsavail")).toVariant().toLongLong();
        pt.used = o.value(QStringLiteral("fsused")).isNull() ? -1 : o.value(QStringLiteral("fsused")).toVariant().toLongLong();
        pt.parttype = o.value(QStringLiteral("parttype")).toString().toLower();
        pt.parttypeName = o.value(QStringLiteral("parttypename")).toString();
        pt.letter = letters.value(pt.kname);
        return pt;
    };
    for (const QJsonValue &v : devs) {
        const QJsonObject o = v.toObject();
        const QString type = o.value(QStringLiteral("type")).toString();
        // a test disk (a loop device over a file) can be shown with WIN7_DISKMGMT_TEST=/dev/loopN
        const bool testDisk = !qEnvironmentVariableIsEmpty("WIN7_DISKMGMT_TEST") && o.value(QStringLiteral("path")).toString() == qEnvironmentVariable("WIN7_DISKMGMT_TEST");
        if (type != QLatin1String("disk") && type != QLatin1String("rom") && !testDisk) {
            continue;
        }
        Disk d;
        d.dev = o.value(QStringLiteral("path")).toString();
        d.kname = o.value(QStringLiteral("kname")).toString();
        d.size = o.value(QStringLiteral("size")).toVariant().toLongLong();
        d.model = o.value(QStringLiteral("model")).toString().trimmed();
        d.tran = o.value(QStringLiteral("tran")).toString();
        d.pttype = o.value(QStringLiteral("pttype")).toString();
        d.removable = o.value(QStringLiteral("rm")).toBool();
        d.optical = type == QLatin1String("rom");
        d.noMedia = d.size == 0;
        bool msReserved = false;
        bool ntfs = false;
        const QJsonArray children = o.value(QStringLiteral("children")).toArray();
        if (children.isEmpty() && !o.value(QStringLiteral("fstype")).toString().isEmpty()) {
            // a filesystem on the whole disk (USB stick formatted without a partition table)
            d.wholeDiskFs = true;
            Part pt = partOf(o);
            pt.start = 0;
            d.parts.append(pt);
        }
        for (const QJsonValue &c : children) {
            const QJsonObject co = c.toObject();
            if (co.value(QStringLiteral("type")).toString() != QLatin1String("part")) {
                continue;
            }
            Part pt = partOf(co);
            pt.start = sysValue(QStringLiteral("/sys/class/block/%1/start").arg(pt.kname)) * 512;
            pt.number = int(sysValue(QStringLiteral("/sys/class/block/%1/partition").arg(pt.kname)));
            if (d.pttype == QLatin1String("dos")) {
                static const QStringList ext{QStringLiteral("0x5"), QStringLiteral("0xf"), QStringLiteral("0x85")};
                pt.extended = ext.contains(pt.parttype);
                pt.logical = pt.number >= 5;
                if (pt.extended) {
                    pt.size = sysValue(QStringLiteral("/sys/class/block/%1/size").arg(pt.kname)) * 512;
                }
            }
            pt.hidden = pt.parttypeName == QLatin1String("Microsoft reserved");
            // Windows: its reserved / recovery partitions (GPT) or "System Reserved" (MBR installs)
            static const QStringList reservedLabels{QStringLiteral("System Reserved"), QStringLiteral("Зарезервовано системою"),
                                                    QStringLiteral("Зарезервировано системой"), QStringLiteral("Recovery")};
            msReserved = msReserved || pt.hidden || pt.parttypeName.contains(QLatin1String("Windows recovery")) || pt.parttype == QLatin1String("0x27")
                || (pt.fstype == QLatin1String("ntfs") && reservedLabels.contains(pt.label));
            ntfs = ntfs || pt.fstype == QLatin1String("ntfs");
            for (const QJsonValue &m : co.value(QStringLiteral("mountpoints")).toArray()) {
                d.system = d.system || systemMounts.contains(m.toString());
            }
            d.parts.append(pt);
        }
        for (const QJsonValue &m : o.value(QStringLiteral("mountpoints")).toArray()) {
            d.system = d.system || systemMounts.contains(m.toString());
        }
        Q_UNUSED(ntfs)
        d.windows = msReserved;
        if (d.windows) {
            continue; // the Windows disk stays out of sight: Linux and Windows never meet
        }
        std::sort(d.parts.begin(), d.parts.end(), [](const Part &a, const Part &b) {
            return a.start < b.start;
        });
        // unallocated space between partitions (only gaps bigger than 8 MB, like Windows)
        if (!d.optical && !d.wholeDiskFs && !d.pttype.isEmpty()) {
            QList<Part> withGaps;
            qint64 pos = 1024 * 1024;
            const qint64 end = d.size - (d.pttype == QLatin1String("gpt") ? 34 * 512 : 0);
            auto gap = [&withGaps](qint64 from, qint64 to, bool logical) {
                if (to - from > 8 * 1024 * 1024) {
                    Part u;
                    u.unallocated = true;
                    u.start = from;
                    u.size = to - from;
                    u.logical = logical;
                    withGaps.append(u);
                }
            };
            for (const Part &pt : std::as_const(d.parts)) {
                if (pt.logical) {
                    withGaps.append(pt);
                    continue;
                }
                gap(pos, pt.start, false);
                withGaps.append(pt);
                pos = pt.start + pt.size;
            }
            gap(pos, end, false);
            d.parts = withGaps;
        }
        disks.append(d);
    }
    // Disk 0 is the one Linux runs from (like "C:" on Disk 0 in Windows 7), then by name; optical drives last
    std::stable_sort(disks.begin(), disks.end(), [](const Disk &a, const Disk &b) {
        if (a.optical != b.optical) {
            return !a.optical;
        }
        if (a.system != b.system) {
            return a.system;
        }
        return a.kname < b.kname;
    });
    int n = 0, cd = 0;
    for (Disk &d : disks) {
        d.index = d.optical ? cd++ : n++;
    }
    return disks;
}

/** "Справний (Завантаження, Основний розділ)" */
inline QString status(const Disk &d, const Part &p)
{
    if (p.unallocated) {
        return p.logical ? tr7("Free space") : tr7("Unallocated");
    }
    QStringList what;
    if (p.mount == QLatin1String("/")) {
        what << tr7("System") << tr7("Downloads");
    }
    if (p.mount == QLatin1String("[SWAP]") || p.fstype == QLatin1String("swap")) {
        what << tr7("Page File");
    }
    if (p.parttypeName == QLatin1String("EFI System")) {
        what << tr7("EFI System Partition");
    } else if (p.parttypeName.contains(QLatin1String("recovery"))) {
        what << tr7("Recovery Partition");
    } else if (p.extended) {
        return QString();
    } else if (p.logical) {
        what << tr7("Logical Drive");
    } else if (!d.wholeDiskFs) {
        what << tr7("Primary Partition");
    }
    if (p.fstype.isEmpty() && !p.extended) {
        return tr7("Healthy") + (what.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(what.join(QStringLiteral(", "))));
    }
    return tr7("Healthy (%1)").arg(what.join(QStringLiteral(", ")));
}

inline QString volumeName(const Part &p)
{
    QString n = p.label;
    if (!p.letter.isEmpty()) {
        n += QStringLiteral("  (%1)").arg(p.letter); // Windows 7 puts two spaces before the letter, even without a label
    }
    return n;
}

/* ---------------- UDisks2 ---------------- */

const QString kUD = QStringLiteral("org.freedesktop.UDisks2");

/** a UDisks call that may ask polkit for the password; returns an error text, "cancelled" or empty */
inline QString udisks(const QString &path, const QString &iface, const QString &method, const QVariantList &args, QVariantList *result = nullptr)
{
    QDBusMessage m = QDBusMessage::createMethodCall(kUD, path, kUD + QLatin1Char('.') + iface, method);
    m.setArguments(args);
    m.setInteractiveAuthorizationAllowed(true);
    const QDBusMessage r = QDBusConnection::systemBus().call(m, QDBus::BlockWithGui, 30 * 60 * 1000);
    if (r.type() == QDBusMessage::ErrorMessage) {
        if (r.errorName().contains(QLatin1String("NotAuthorized")) && r.errorMessage().contains(QLatin1String("dismissed"))) {
            return QStringLiteral("cancelled");
        }
        return r.errorMessage();
    }
    if (result) {
        *result = r.arguments();
    }
    return QString();
}

inline QVariantMap noOptions()
{
    return {};
}

/** checks again, right before a change, that the disk isn't protected (the list may be old) */
inline bool stillAllowed(QWidget *parent, const QString &diskKname)
{
    for (const Disk &d : scan()) {
        if (d.kname == diskKname) {
            if (d.isProtected()) {
                QMessageBox::warning(parent, tr7("Disk Management"), d.protectReason());
                return false;
            }
            return true;
        }
    }
    QMessageBox::warning(parent, tr7("Disk Management"), tr7("The disk can no longer be found. It might have been disconnected."));
    return false;
}

inline QString unmount(const Part &p)
{
    if (p.mount.isEmpty()) {
        return QString();
    }
    return udisks(udisksPath(p.kname), QStringLiteral("Filesystem"), QStringLiteral("Unmount"), {QVariantMap{}});
}

/** UDisks filesystem types: "ntfs", "vfat", "exfat", "ext4" */
inline QStringList formatTypes()
{
    return {QStringLiteral("ntfs"), QStringLiteral("vfat"), QStringLiteral("exfat"), QStringLiteral("ext4")};
}

inline QString partitionType(const QString &pttype, const QString &fs)
{
    if (pttype == QLatin1String("gpt")) {
        return fs == QLatin1String("ext4") ? QStringLiteral("0fc63daf-8483-4772-8e79-3d69d8477de4") : QStringLiteral("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7");
    }
    if (fs == QLatin1String("vfat")) {
        return QStringLiteral("0x0c");
    }
    return fs == QLatin1String("ext4") ? QStringLiteral("0x83") : QStringLiteral("0x07");
}

inline QVariantMap formatOptions(const QString &label, bool quick)
{
    QVariantMap o{{QStringLiteral("label"), label}, {QStringLiteral("take-ownership"), true}, {QStringLiteral("update-partition-type"), true}};
    if (!quick) {
        o.insert(QStringLiteral("erase"), QStringLiteral("zero")); // full format: every sector is written
    }
    return o;
}

/** the label limits of the filesystem (FAT: 11 capital letters) */
inline QString fitLabel(const QString &fs, QString label)
{
    if (fs == QLatin1String("vfat")) {
        return label.toUpper().left(11);
    }
    if (fs == QLatin1String("exfat")) {
        return label.left(15);
    }
    if (fs == QLatin1String("ext4")) {
        return label.left(16);
    }
    return label.left(32);
}

/* ---------------- dialogs ---------------- */

inline void failed(QWidget *parent, const QString &what, const QString &error)
{
    if (error.isEmpty() || error == QLatin1String("cancelled")) {
        return;
    }
    QMessageBox box(QMessageBox::Critical, tr7("Virtual Disk Service"), what, QMessageBox::Ok, parent);
    box.setDetailedText(error);
    box.exec();
}

/** "File system / Allocation unit size / Volume label / Quick format / compression" */
struct FormatChoice {
    QComboBox *fs;
    QComboBox *cluster;
    QLineEdit *label;
    QCheckBox *quick;
    QCheckBox *compress;
};

inline FormatChoice formatFields(QGridLayout *g, int row, const QString &defaultFs, const QString &defaultLabel, qint64 size)
{
    FormatChoice c;
    c.fs = new QComboBox;
    for (const QString &t : formatTypes()) {
        // FAT32 volumes can't be bigger than 2 TB
        if (t == QLatin1String("vfat") && size > 2LL * 1024 * 1024 * 1024 * 1024) {
            continue;
        }
        c.fs->addItem(fsName(t), t);
    }
    c.fs->setCurrentIndex(qMax(0, c.fs->findData(defaultFs)));
    c.cluster = new QComboBox;
    c.cluster->addItem(tr7("Default"));
    c.cluster->setEnabled(false);
    c.cluster->setToolTip(tr7("The allocation unit size is chosen automatically."));
    c.label = new QLineEdit(defaultLabel);
    c.quick = new QCheckBox(tr7("Perform a quick format"));
    c.quick->setChecked(true);
    c.compress = new QCheckBox(tr7("Enable file and folder compression"));
    c.compress->setEnabled(false);
    c.compress->setToolTip(tr7("NTFS compression is not available on this system."));
    g->addWidget(new QLabel(tr7("File system:")), row, 0);
    g->addWidget(c.fs, row, 1);
    g->addWidget(new QLabel(tr7("Allocation unit size:")), row + 1, 0);
    g->addWidget(c.cluster, row + 1, 1);
    g->addWidget(new QLabel(tr7("Volume label:")), row + 2, 0);
    g->addWidget(c.label, row + 2, 1);
    g->addWidget(c.quick, row + 3, 0, 1, 2);
    g->addWidget(c.compress, row + 4, 0, 1, 2);
    return c;
}

inline bool confirm(QWidget *parent, const QString &title, const QString &text)
{
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Yes | QMessageBox::No, parent);
    box.button(QMessageBox::Yes)->setText(tr7("Yes"));
    box.button(QMessageBox::No)->setText(tr7("No"));
    box.setDefaultButton(QMessageBox::No);
    return box.exec() == QMessageBox::Yes;
}

/** "Format" dialog of a volume */
inline bool formatDialog(QWidget *parent, const Disk &d, const Part &p)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Format %1").arg(p.letter.isEmpty() ? volumeName(p) : p.letter));
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dlg.setFixedWidth(380);
    auto *g = new QGridLayout(&dlg);
    g->setContentsMargins(14, 14, 14, 12);
    g->setVerticalSpacing(8);
    const FormatChoice c = formatFields(g, 0, formatTypes().contains(p.fstype) ? p.fstype : QStringLiteral("ntfs"), p.label, p.size);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    ok->setDefault(true);
    row->addWidget(ok);
    row->addWidget(cancel);
    g->addLayout(row, 5, 0, 1, 2);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    if (!confirm(parent, tr7("Format %1").arg(volumeName(p)),
                 tr7("Formatting this volume will erase all data on it. Back up any data you want to keep before formatting. Do you want to continue?"))) {
        return false;
    }
    if (!stillAllowed(parent, d.kname)) {
        return false;
    }
    const QString fs = c.fs->currentData().toString();
    QString err = unmount(p);
    if (err.isEmpty()) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        err = udisks(udisksPath(p.kname), QStringLiteral("Block"), QStringLiteral("Format"), {fs, formatOptions(fitLabel(fs, c.label->text()), c.quick->isChecked())});
        QApplication::restoreOverrideCursor();
    }
    failed(parent, tr7("The volume could not be formatted."), err);
    return err.isEmpty();
}

inline bool deleteVolume(QWidget *parent, const Disk &d, const Part &p)
{
    if (!confirm(parent, tr7("Delete simple volume"),
                 tr7("Deleting this volume will erase all data on it. Back up any data you want to keep before deleting. Do you want to continue?"))) {
        return false;
    }
    if (!stillAllowed(parent, d.kname)) {
        return false;
    }
    QString err = unmount(p);
    if (err.isEmpty()) {
        if (d.wholeDiskFs) {
            // a stick without a partition table: an empty MBR table leaves the whole disk unallocated
            err = udisks(udisksPath(d.kname), QStringLiteral("Block"), QStringLiteral("Format"), {QStringLiteral("dos"), QVariantMap{}});
        } else {
            err = udisks(udisksPath(p.kname), QStringLiteral("Partition"), QStringLiteral("Delete"), {QVariantMap{}});
        }
    }
    failed(parent, tr7("The volume could not be deleted."), err);
    return err.isEmpty();
}

/** can UDisks shrink / grow this filesystem: bit 2 = offline shrink, 4 = offline grow, 8 = online shrink, 16 = online grow */
inline quint64 resizeModes(const QString &fs)
{
    QDBusMessage m = QDBusMessage::createMethodCall(kUD, QStringLiteral("/org/freedesktop/UDisks2/Manager"), kUD + QStringLiteral(".Manager"), QStringLiteral("CanResize"));
    m.setArguments({fs});
    const QDBusMessage r = QDBusConnection::systemBus().call(m);
    if (r.type() == QDBusMessage::ErrorMessage) {
        return 0;
    }
    const QDBusArgument arg = r.arguments().value(0).value<QDBusArgument>();
    bool available = false;
    quint64 mode = 0;
    QString util;
    arg.beginStructure();
    arg >> available >> mode >> util;
    arg.endStructure();
    return available ? mode : 0;
}

/** "Shrink" dialog: sizes in MB like Windows 7 */
inline bool shrinkDialog(QWidget *parent, const Disk &d, const Part &p)
{
    const qint64 mb = 1024 * 1024;
    const qint64 total = p.size / mb;
    // what is used plus 10 % (and at least 64 MB) stays
    const qint64 keep = p.used >= 0 ? qMax<qint64>(64, qint64(p.used * 1.1) / mb + 32) : total;
    const qint64 can = qMax<qint64>(0, total - keep);
    if (can < 8) {
        QMessageBox::information(parent, tr7("Shrink %1").arg(volumeName(p)), tr7("There is no space on this volume that can be shrunk."));
        return false;
    }
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Shrink %1").arg(volumeName(p)));
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dlg.setFixedWidth(460);
    auto *g = new QGridLayout(&dlg);
    g->setContentsMargins(14, 14, 14, 12);
    g->setVerticalSpacing(10);
    auto *spin = new QSpinBox;
    spin->setRange(8, int(qMin<qint64>(can, INT_MAX)));
    spin->setValue(spin->maximum());
    auto *after = new QLabel;
    g->addWidget(new QLabel(tr7("Total size before shrink in MB:")), 0, 0);
    g->addWidget(new QLabel(QString::number(total)), 0, 1);
    g->addWidget(new QLabel(tr7("Size of available shrink space in MB:")), 1, 0);
    g->addWidget(new QLabel(QString::number(can)), 1, 1);
    g->addWidget(new QLabel(tr7("Enter the amount of space to shrink in MB:")), 2, 0);
    g->addWidget(spin, 2, 1);
    g->addWidget(new QLabel(tr7("Total size after shrink in MB:")), 3, 0);
    g->addWidget(after, 3, 1);
    auto update = [after, spin, total] {
        after->setText(QString::number(total - spin->value()));
    };
    QObject::connect(spin, &QSpinBox::valueChanged, &dlg, update);
    update();
    auto *note = new QLabel(tr7("The volume will be temporarily unmounted while it is shrunk."));
    note->setWordWrap(true);
    g->addWidget(note, 4, 0, 1, 2);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *ok = new QPushButton(tr7("Shrink"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    ok->setDefault(true);
    row->addWidget(ok);
    row->addWidget(cancel);
    g->addLayout(row, 5, 0, 1, 2);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted || !stillAllowed(parent, d.kname)) {
        return false;
    }
    const quint64 newSize = quint64(total - spin->value()) * mb;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err = unmount(p);
    if (err.isEmpty()) {
        err = udisks(udisksPath(p.kname), QStringLiteral("Filesystem"), QStringLiteral("Resize"), {QVariant::fromValue(newSize), QVariantMap{}});
    }
    if (err.isEmpty()) {
        err = udisks(udisksPath(p.kname), QStringLiteral("Partition"), QStringLiteral("Resize"), {QVariant::fromValue(newSize), QVariantMap{}});
    }
    QApplication::restoreOverrideCursor();
    failed(parent, tr7("The volume could not be shrunk."), err);
    return err.isEmpty();
}

/** "Extend Volume": takes the unallocated space right after the volume */
inline bool extendDialog(QWidget *parent, const Disk &d, const Part &p, qint64 freeAfter)
{
    const qint64 mb = 1024 * 1024;
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Extend Volume Wizard"));
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dlg.setFixedWidth(460);
    auto *g = new QGridLayout(&dlg);
    g->setContentsMargins(14, 14, 14, 12);
    g->setVerticalSpacing(10);
    auto *title = new QLabel(tr7("Select Disks"));
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    g->addWidget(title, 0, 0, 1, 2);
    g->addWidget(new QLabel(tr7("Total volume size in megabytes (MB):")), 1, 0);
    g->addWidget(new QLabel(QString::number(p.size / mb)), 1, 1);
    g->addWidget(new QLabel(tr7("Maximum available space in MB:")), 2, 0);
    g->addWidget(new QLabel(QString::number(freeAfter / mb)), 2, 1);
    auto *spin = new QSpinBox;
    spin->setRange(1, int(qMin<qint64>(freeAfter / mb, INT_MAX)));
    spin->setValue(spin->maximum());
    g->addWidget(new QLabel(tr7("Select the amount of space in MB:")), 3, 0);
    g->addWidget(spin, 3, 1);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *ok = new QPushButton(tr7("Extend"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    ok->setDefault(true);
    row->addWidget(ok);
    row->addWidget(cancel);
    g->addLayout(row, 4, 0, 1, 2);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted || !stillAllowed(parent, d.kname)) {
        return false;
    }
    const quint64 newSize = quint64(p.size) + quint64(spin->value()) * mb;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool online = resizeModes(p.fstype) & 16;
    QString err = online ? QString() : unmount(p);
    if (err.isEmpty()) {
        err = udisks(udisksPath(p.kname), QStringLiteral("Partition"), QStringLiteral("Resize"), {QVariant::fromValue(newSize), QVariantMap{}});
    }
    if (err.isEmpty()) {
        err = udisks(udisksPath(p.kname), QStringLiteral("Filesystem"), QStringLiteral("Resize"), {QVariant::fromValue(quint64(0)), QVariantMap{}}); // 0 = fill the partition
    }
    QApplication::restoreOverrideCursor();
    failed(parent, tr7("The volume could not be extended."), err);
    return err.isEmpty();
}

/** "Initialize Disk": MBR or GPT for a new disk */
inline bool initializeDisk(QWidget *parent, const Disk &d)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Initialize Disk"));
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dlg.setFixedWidth(430);
    auto *v = new QVBoxLayout(&dlg);
    v->setContentsMargins(14, 14, 14, 12);
    auto *t = new QLabel(tr7("You must initialize a disk before Logical Disk Manager can access it."));
    t->setWordWrap(true);
    v->addWidget(t);
    v->addWidget(new QLabel(tr7("Select disks:")));
    auto *list = new QLabel(tr7("    Disk %1").arg(d.index));
    v->addWidget(list);
    v->addWidget(new QLabel(tr7("Use the following partition style for the selected disks:")));
    auto *mbr = new QRadioButton(tr7("MBR (Master Boot Record)"));
    auto *gpt = new QRadioButton(tr7("GPT (GUID Partition Table)"));
    (d.size > 2LL * 1024 * 1024 * 1024 * 1024 ? gpt : mbr)->setChecked(true);
    v->addWidget(mbr);
    v->addWidget(gpt);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    row->addWidget(ok);
    row->addWidget(cancel);
    v->addLayout(row);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted || !stillAllowed(parent, d.kname)) {
        return false;
    }
    const QString err = udisks(udisksPath(d.kname), QStringLiteral("Block"), QStringLiteral("Format"), {gpt->isChecked() ? QStringLiteral("gpt") : QStringLiteral("dos"), QVariantMap{}});
    failed(parent, tr7("The disk could not be initialized."), err);
    return err.isEmpty();
}

/* ---------------- "New Simple Volume" wizard ---------------- */

/** the teal picture on the left of the first and last wizard page */
class WizardSide : public QWidget
{
public:
    WizardSide()
    {
        setFixedWidth(164);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // the picture of dmdlgs.dll (Windows 7), taken from the top like Windows does
        static const QPixmap side(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/seven-control/wizard-side.png"));
        if (!side.isNull()) {
            p.drawPixmap(0, 0, side);
            return;
        }
        p.fillRect(rect(), QColor(0x1a, 0x52, 0x5a));
    }
};

class NewVolumeWizard : public QDialog
{
public:
    NewVolumeWizard(QWidget *parent, const Disk &d, const Part &space, const QString &nextLetter)
        : QDialog(parent)
        , m_disk(d)
        , m_space(space)
        , m_letter(nextLetter)
    {
        setWindowTitle(tr7("New Simple Volume Wizard"));
        setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        setFixedSize(500, 390);
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        m_stack = new QStackedWidget;
        v->addWidget(m_stack, 1);
        auto *line = new QFrame;
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background: #a0a0a0;"));
        v->addWidget(line);
        auto *row = new QHBoxLayout;
        row->setContentsMargins(10, 10, 12, 10);
        row->addStretch(1);
        m_back = new QPushButton(tr7("< Back"));
        m_next = new QPushButton(tr7("Next >"));
        auto *cancel = new QPushButton(tr7("Cancel"));
        for (QPushButton *b : {m_back, m_next, cancel}) {
            b->setMinimumWidth(80);
            row->addWidget(b);
        }
        row->insertSpacing(3, 8);
        m_next->setDefault(true);
        v->addLayout(row);
        m_stack->addWidget(edgePage(tr7("Welcome to the New Simple Volume Wizard"),
                                    tr7("This wizard helps you create a simple volume on a disk.\n\nA simple volume can only be on a single disk.\n\nTo continue, click Next."),
                                    nullptr));
        m_stack->addWidget(sizePage());
        m_stack->addWidget(letterPage());
        m_stack->addWidget(formatPage());
        m_summary = new QPlainTextEdit;
        m_summary->setReadOnly(true);
        m_stack->addWidget(edgePage(tr7("Completing the New Simple Volume Wizard"),
                                    tr7("You have successfully completed the New Simple Volume Wizard.\n\nYou selected the following settings:"), m_summary));
        connect(m_back, &QPushButton::clicked, this, [this] { go(m_stack->currentIndex() - 1); });
        connect(m_next, &QPushButton::clicked, this, [this] {
            if (m_stack->currentIndex() == m_stack->count() - 1) {
                finish();
            } else {
                go(m_stack->currentIndex() + 1);
            }
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        go(0);
    }

private:
    QWidget *edgePage(const QString &title, const QString &text, QWidget *extra)
    {
        auto *w = new QWidget;
        w->setObjectName(QStringLiteral("win7nvwhite"));
        w->setAttribute(Qt::WA_StyledBackground, true);
        w->setStyleSheet(QStringLiteral("#win7nvwhite { background: #ffffff; }"));
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(new WizardSide);
        auto *v = new QVBoxLayout;
        v->setContentsMargins(16, 20, 20, 12);
        auto *t = new QLabel(title);
        QFont f = t->font();
        f.setPixelSize(17);
        f.setBold(true);
        t->setFont(f);
        t->setWordWrap(true);
        v->addWidget(t);
        v->addSpacing(12);
        auto *l = new QLabel(text);
        l->setWordWrap(true);
        v->addWidget(l);
        if (extra) {
            v->addWidget(extra, 1);
            v->addWidget(new QLabel(tr7("To close this wizard, click Finish.")));
        } else {
            v->addStretch(1);
        }
        h->addLayout(v, 1);
        return w;
    }

    QWidget *innerPage(const QString &title, const QString &subtitle, QWidget *content)
    {
        auto *w = new QWidget;
        auto *v = new QVBoxLayout(w);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        auto *head = new QWidget;
        head->setObjectName(QStringLiteral("win7nvhead"));
        head->setAttribute(Qt::WA_StyledBackground, true);
        head->setStyleSheet(QStringLiteral("#win7nvhead { background: #ffffff; border-bottom: 1px solid #a0a0a0; }"));
        auto *hv = new QVBoxLayout(head);
        hv->setContentsMargins(24, 10, 20, 10);
        auto *t = new QLabel(title);
        QFont f = t->font();
        f.setBold(true);
        t->setFont(f);
        hv->addWidget(t);
        auto *s = new QLabel(subtitle);
        s->setWordWrap(true);
        s->setContentsMargins(14, 0, 0, 0);
        hv->addWidget(s);
        v->addWidget(head);
        content->setContentsMargins(40, 16, 30, 10);
        v->addWidget(content, 1);
        return w;
    }

    QWidget *sizePage()
    {
        const qint64 mb = 1024 * 1024;
        auto *c = new QWidget;
        auto *g = new QGridLayout(c);
        g->setVerticalSpacing(14);
        m_maxMb = int(qMin<qint64>(m_space.size / mb, INT_MAX));
        g->addWidget(new QLabel(tr7("Maximum disk space in MB:")), 0, 0);
        g->addWidget(new QLabel(QString::number(m_maxMb)), 0, 1);
        g->addWidget(new QLabel(tr7("Minimum disk space in MB:")), 1, 0);
        g->addWidget(new QLabel(QStringLiteral("8")), 1, 1);
        g->addWidget(new QLabel(tr7("Simple volume size in MB:")), 2, 0);
        m_size = new QSpinBox;
        m_size->setRange(8, m_maxMb);
        m_size->setValue(m_maxMb);
        m_size->setFixedWidth(90);
        g->addWidget(m_size, 2, 1);
        g->setColumnStretch(2, 1);
        g->setRowStretch(3, 1);
        return innerPage(tr7("Specify Volume Size"), tr7("Choose a volume size that is between the maximum and minimum sizes."), c);
    }

    QWidget *letterPage()
    {
        auto *c = new QWidget;
        auto *g = new QGridLayout(c);
        g->setVerticalSpacing(10);
        auto *assign = new QRadioButton(tr7("Assign the following drive letter (A-Z):"));
        assign->setChecked(true);
        m_letterBox = new QComboBox;
        m_letterBox->addItem(m_letter.left(1));
        m_letterBox->setToolTip(tr7("Drive letters are assigned in order: the new volume gets the next free letter."));
        m_letterBox->setFixedWidth(50);
        auto *folder = new QRadioButton(tr7("Mount in the following empty NTFS folder:"));
        folder->setEnabled(false);
        auto *path = new QLineEdit;
        path->setEnabled(false);
        auto *browse = new QPushButton(tr7("Browse..."));
        browse->setEnabled(false);
        m_noLetter = new QRadioButton(tr7("Do not assign a drive letter or drive path"));
        g->addWidget(assign, 0, 0);
        g->addWidget(m_letterBox, 0, 1, Qt::AlignLeft);
        g->addWidget(folder, 1, 0, 1, 2);
        auto *pr = new QHBoxLayout;
        pr->setContentsMargins(20, 0, 0, 0);
        pr->addWidget(path, 1);
        pr->addWidget(browse);
        g->addLayout(pr, 2, 0, 1, 2);
        g->addWidget(m_noLetter, 3, 0, 1, 2);
        g->setRowStretch(4, 1);
        return innerPage(tr7("Assign Drive Letter or Path"), tr7("For easier access, you can assign a drive letter or drive path to your partition."), c);
    }

    QWidget *formatPage()
    {
        auto *c = new QWidget;
        auto *v = new QVBoxLayout(c);
        v->setSpacing(8);
        auto *t = new QLabel(tr7("Choose whether you want to format this volume, and if so, what settings you want to use."));
        t->setWordWrap(true);
        v->addWidget(t);
        m_noFormat = new QRadioButton(tr7("Do not format this volume"));
        auto *doFormat = new QRadioButton(tr7("Format this volume with the following settings:"));
        doFormat->setChecked(true);
        v->addWidget(m_noFormat);
        v->addWidget(doFormat);
        auto *box = new QWidget;
        auto *g = new QGridLayout(box);
        g->setContentsMargins(20, 0, 0, 0);
        m_format = formatFields(g, 0, m_disk.removable ? QStringLiteral("exfat") : QStringLiteral("ntfs"), tr7("New Volume"), m_space.size);
        v->addWidget(box);
        connect(doFormat, &QRadioButton::toggled, box, &QWidget::setEnabled);
        return innerPage(tr7("Format Partition"), tr7("To store data on this partition, you must format it first."), c);
    }

    void go(int page)
    {
        page = qBound(0, page, m_stack->count() - 1);
        m_stack->setCurrentIndex(page);
        m_back->setEnabled(page > 0);
        m_next->setText(page == m_stack->count() - 1 ? tr7("Done") : tr7("Next >"));
        if (page == m_stack->count() - 1) {
            const bool format = !m_noFormat->isChecked();
            QStringList s{tr7("Volume type: Simple Volume"), tr7("Disks selected: Disk %1").arg(m_disk.index),
                          tr7("Volume size: %1 MB").arg(m_size->value()),
                          tr7("Drive letter or path: %1").arg(m_noLetter->isChecked() ? tr7("None") : m_letter.left(1))};
            if (format) {
                s << tr7("File system: %1").arg(m_format.fs->currentText()) << tr7("Allocation unit size: Default")
                  << tr7("Volume label: %1").arg(m_format.label->text())
                  << tr7("Quick format: %1").arg(m_format.quick->isChecked() ? tr7("Yes") : tr7("No"))
                  << tr7("Enable file and folder compression: No");
            } else {
                s << tr7("File system: None");
            }
            m_summary->setPlainText(s.join(QLatin1Char('\n')));
        }
    }

    void finish()
    {
        if (!stillAllowed(this, m_disk.kname)) {
            return;
        }
        const qint64 mb = 1024 * 1024;
        // the last MB of a disk stays free for GPT; a volume of the whole space takes all of it
        const qint64 size = m_size->value() >= m_maxMb ? m_space.size : qint64(m_size->value()) * mb;
        const bool format = !m_noFormat->isChecked();
        const QString fs = m_format.fs->currentData().toString();
        const QString type = partitionType(m_disk.pttype, format ? fs : QStringLiteral("ntfs"));
        QVariantMap opts;
        if (m_disk.pttype == QLatin1String("dos")) {
            opts.insert(QStringLiteral("partition-type"), m_space.logical ? QStringLiteral("logical") : QStringLiteral("primary"));
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString err;
        if (format) {
            err = udisks(udisksPath(m_disk.kname), QStringLiteral("PartitionTable"), QStringLiteral("CreatePartitionAndFormat"),
                         {QVariant::fromValue(quint64(m_space.start)), QVariant::fromValue(quint64(size)), type, QString(), opts, fs,
                          formatOptions(fitLabel(fs, m_format.label->text()), m_format.quick->isChecked())});
        } else {
            err = udisks(udisksPath(m_disk.kname), QStringLiteral("PartitionTable"), QStringLiteral("CreatePartition"),
                         {QVariant::fromValue(quint64(m_space.start)), QVariant::fromValue(quint64(size)), type, QString(), opts});
        }
        QApplication::restoreOverrideCursor();
        failed(this, tr7("The volume could not be created."), err);
        if (err.isEmpty()) {
            accept();
        }
    }

    Disk m_disk;
    Part m_space;
    QString m_letter;
    QStackedWidget *m_stack;
    QPushButton *m_back;
    QPushButton *m_next;
    QSpinBox *m_size;
    int m_maxMb = 8;
    QComboBox *m_letterBox;
    QRadioButton *m_noLetter;
    QRadioButton *m_noFormat;
    FormatChoice m_format;
    QPlainTextEdit *m_summary;
};

/* ---------------- graphical view ---------------- */

/** the disk map at the bottom: one row per disk, a box per partition */
class DiskMap : public QWidget
{
public:
    std::function<void(int disk, int part)> selected;
    std::function<void(int disk, int part, const QPoint &global)> menu;
    std::function<void(int disk, int part)> activated;

    void setDisks(const QList<Disk> &disks)
    {
        m_disks = disks;
        if (m_selDisk >= m_disks.size() || (m_selDisk >= 0 && m_selPart >= m_disks.at(m_selDisk).parts.size())) {
            m_selDisk = m_selPart = -1;
        }
        setMinimumHeight(rowHeight() * m_disks.size() + 4);
        update();
    }
    void select(int disk, int part)
    {
        m_selDisk = disk;
        m_selPart = part;
        update();
    }

protected:
    static int rowHeight()
    {
        return 110;
    }
    static constexpr int labelWidth = 116;

    QList<std::tuple<QRect, int, int>> layout() const
    {
        QList<std::tuple<QRect, int, int>> out;
        const int fullArea = width() - labelWidth - 8;
        qint64 biggest = 1;
        for (const Disk &d : m_disks) {
            biggest = qMax(biggest, d.size);
        }
        for (int i = 0; i < m_disks.size(); ++i) {
            const Disk &d = m_disks.at(i);
            const int top = i * rowHeight() + 2;
            out.append({QRect(0, top, labelWidth, rowHeight() - 4), i, -1});
            if (d.optical || d.noMedia) {
                continue;
            }
            // smaller disks get a somewhat shorter bar, like Windows 7 (not to scale)
            const double frac = qMax(0.5, 1.0 - 0.06 * std::log2(double(biggest) / qMax<qint64>(1, d.size)));
            const int area = int(fullArea * frac);
            QList<int> visible;
            for (int k = 0; k < d.parts.size(); ++k) {
                if (!d.parts.at(k).hidden && !d.parts.at(k).extended) {
                    visible.append(k);
                }
            }
            if (visible.isEmpty()) {
                out.append({QRect(labelWidth + 4, top, area, rowHeight() - 4), i, -2}); // uninitialised or empty
                continue;
            }
            // Windows 7 gives small partitions a minimum width so their text stays readable
            const int minW = 70;
            double totalSize = 0;
            for (int k : visible) {
                totalSize += d.parts.at(k).size;
            }
            QList<int> widths;
            int sumMin = 0;
            for (int k : visible) {
                const int w = qMax(minW, int(area * d.parts.at(k).size / totalSize));
                widths.append(w);
                sumMin += w;
            }
            const double scale = sumMin > area ? double(area) / sumMin : 1.0;
            int x = labelWidth + 4;
            for (int j = 0; j < visible.size(); ++j) {
                const int w = j == visible.size() - 1 ? labelWidth + 4 + area - x : int(widths.at(j) * scale);
                out.append({QRect(x, top, w, rowHeight() - 4), i, visible.at(j)});
                x += w;
            }
        }
        return out;
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0xf0, 0xf0, 0xf0));
        QFont bold = font();
        bold.setBold(true);
        for (const auto &[r, di, pi] : layout()) {
            const Disk &d = m_disks.at(di);
            if (pi == -1) {
                // "Disk 0 / Basic / 931,51 GB / Online"
                // the label column: window grey with a white edge, a line under each disk
                p.setPen(Qt::white);
                p.drawLine(r.right(), r.top(), r.right(), r.bottom());
                p.setPen(QColor(0xa0, 0xa0, 0xa0));
                p.drawLine(r.right() + 1, r.top(), r.right() + 1, r.bottom());
                p.drawLine(0, r.bottom() + 2, width(), r.bottom() + 2);
                p.setPen(Qt::white);
                p.drawLine(0, r.bottom() + 3, width(), r.bottom() + 3);
                const QIcon icon = QIcon::fromTheme(d.optical ? QStringLiteral("seven-devmgr-dvd") : QStringLiteral("seven-devmgr-disk"));
                p.drawPixmap(r.left() + 10, r.top() + 10, icon.pixmap(16, 16));
                p.setPen(Qt::black);
                p.setFont(bold);
                p.drawText(QRect(r.left() + 28, r.top() + 8, r.width() - 30, 20), Qt::AlignVCenter,
                           d.optical ? QStringLiteral("CD-ROM %1").arg(d.index) : QStringLiteral("%1 %2").arg(d.removable ? tr7("Disk") : tr7("Disk")).arg(d.index));
                p.setFont(font());
                QStringList lines;
                if (d.optical) {
                    lines << QStringLiteral("CD-ROM") << QString() << (d.noMedia ? tr7("No Media") : sizeText(d.size));
                } else {
                    lines << (d.removable ? tr7("Removable") : tr7("Basic")) << sizeText(d.size)
                          << (d.pttype.isEmpty() && !d.wholeDiskFs ? tr7("Not Initialized") : tr7("Online"));
                }
                for (int k = 0; k < lines.size(); ++k) {
                    p.drawText(QRect(r.left() + 12, r.top() + 28 + k * 17, r.width() - 14, 17), Qt::AlignVCenter, lines.at(k));
                }

                continue;
            }
            if (pi == -2) {
                p.setPen(QColor(0xa0, 0xa0, 0xa0));
                p.setBrush(Qt::white);
                p.drawRect(r.adjusted(0, 0, -1, -1));
                continue;
            }
            const Part &pt = d.parts.at(pi);
            const bool sel = di == m_selDisk && pi == m_selPart;
            const QColor bar = pt.unallocated ? (pt.logical ? QColor(0x66, 0xcc, 0x66) : Qt::black) : pt.logical ? QColor(0x00, 0x66, 0xff) : QColor(0x00, 0x00, 0x80);
            // coloured bar on top and right under it the white volume box with a sunken edge (diskmgmt.msc)
            const QRect box = r.adjusted(2, 6, -1, -6);
            p.fillRect(QRect(box.left(), box.top(), box.width(), 13), bar);
            QRect inner = QRect(box.left(), box.top() + 15, box.width(), box.height() - 15);
            p.fillRect(inner, Qt::white);
            p.setPen(QColor(0x9d, 0xa1, 0xa5)); // dark top and left
            p.drawLine(inner.topLeft(), inner.topRight());
            p.drawLine(inner.topLeft(), inner.bottomLeft());
            p.setPen(QColor(0xdc, 0xdf, 0xe2)); // light bottom and right
            p.drawLine(inner.bottomLeft(), inner.bottomRight());
            p.drawLine(inner.topRight(), inner.bottomRight());
            inner.adjust(2, 2, -2, -2);
            if (sel) {
                // Windows 7 hatches the selected volume
                p.save();
                p.setClipRect(inner);
                p.setPen(QColor(0xc4, 0xc4, 0xc4));
                for (int x = inner.left() - inner.height(); x < inner.right(); x += 6) {
                    p.drawLine(x, inner.bottom(), x + inner.height(), inner.top());
                }
                p.restore();
                p.setPen(QPen(Qt::black, 1, Qt::DotLine));
                p.setBrush(Qt::NoBrush);
                p.drawRect(inner);
            }
            p.setPen(Qt::black);
            const QRect tr = inner.adjusted(3, 2, -3, 0);
            QStringList lines;
            QString title = volumeName(pt);
            if (pt.unallocated) {
                lines << sizeText(pt.size) << status(d, pt);
            } else {
                lines << QStringLiteral("%1 %2").arg(sizeText(pt.size), fsName(pt.fstype)).trimmed() << status(d, pt);
            }
            int y = tr.top();
            if (!title.isEmpty()) {
                p.setFont(bold);
                p.drawText(QRect(tr.left(), y, tr.width(), 17), Qt::AlignVCenter, QFontMetrics(bold).elidedText(title, Qt::ElideRight, tr.width()));
                y += 17;
            } else if (!pt.unallocated) {
                y += 17;
            }
            p.setFont(font());
            for (const QString &l : std::as_const(lines)) {
                p.drawText(QRect(tr.left(), y, tr.width(), 17), Qt::AlignVCenter, QFontMetrics(font()).elidedText(l, Qt::ElideRight, tr.width()));
                y += 17;
            }
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        for (const auto &[r, di, pi] : layout()) {
            if (r.contains(e->pos())) {
                m_selDisk = di;
                m_selPart = pi;
                update();
                if (selected) {
                    selected(di, pi);
                }
                if (e->button() == Qt::RightButton && menu) {
                    menu(di, pi, e->globalPosition().toPoint());
                }
                return;
            }
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        for (const auto &[r, di, pi] : layout()) {
            if (r.contains(e->pos()) && pi >= 0 && activated) {
                activated(di, pi);
            }
        }
    }

private:
    QList<Disk> m_disks;
    int m_selDisk = -1;
    int m_selPart = -1;
};

/* ---------------- main window ---------------- */

inline QIcon flatIcon(const QString &name)
{
    const QPixmap pm = QIcon::fromTheme(name).pixmap(16, 16);
    QIcon icon;
    icon.addPixmap(pm, QIcon::Normal);
    icon.addPixmap(pm, QIcon::Active);
    icon.addPixmap(pm, QIcon::Disabled); // MMC doesn't grey these out (back/forward are grey already)
    return icon;
}

/** menu bar and toolbar of MMC consoles in Windows 7 (the lavender gradient) */
inline void mmcChrome(QMenuBar *menu, QToolBar *bar)
{
    menu->setStyleSheet(QStringLiteral(
        "QMenuBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f9fcff, stop:0.45 #e5e8fa, stop:0.5 #cdd1e6, stop:1 #cbd0e7); padding: 1px 2px; }"
        "QMenuBar::item { background: transparent; padding: 3px 8px; color: black; }"
        "QMenuBar::item:selected { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fdfeff, stop:1 #dfe8f6); border: 1px solid #a8c5ea; border-radius: 2px; }"));
    bar->setStyleSheet(QStringLiteral(
        "QToolBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #e6ebff, stop:0.06 #cbd0e8, stop:1 #dadbee); border: none; border-bottom: 1px solid #b7b9c3; spacing: 0px; padding: 1px 3px; }"
        "QToolBar::separator { width: 1px; background: #a3a6b8; margin: 3px 5px; }"
        ) + Win7States::toolButtonSheet());
}

/** the column header of Windows 7 list views: flat grey, thin separators, "Располо..." when too narrow */
class Win7Header : public QHeaderView
{
public:
    explicit Win7Header(QWidget *parent = nullptr)
        : QHeaderView(Qt::Horizontal, parent)
    {
        setSectionsClickable(true);
        setHighlightSections(false);
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setFixedHeight(QFontMetrics(font()).height() + 8);
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        {
            QPainter p(viewport());
            paintBackground(p, viewport()->rect());
        }
        QHeaderView::paintEvent(e);
    }
    /** MMC uses the classic raised header: white top/left edge, grey and dark grey bottom/right edge */
    static void paintBackground(QPainter &p, const QRect &r)
    {
        p.fillRect(r, QColor(0xeb, 0xed, 0xee));
        p.setPen(Qt::white);
        p.drawLine(r.left(), r.top(), r.right(), r.top());
        p.drawLine(r.left(), r.top(), r.left(), r.bottom());
        p.setPen(QColor(0xa0, 0xa0, 0xa0));
        p.drawLine(r.left() + 1, r.bottom() - 1, r.right() - 1, r.bottom() - 1);
        p.drawLine(r.right() - 1, r.top() + 1, r.right() - 1, r.bottom() - 1);
        p.setPen(QColor(0x69, 0x69, 0x69));
        p.drawLine(r.left(), r.bottom(), r.right(), r.bottom());
        p.drawLine(r.right(), r.top(), r.right(), r.bottom());
    }
    void paintSection(QPainter *p, const QRect &rect, int logical) const override
    {
        if (!rect.isValid()) {
            return;
        }
        p->save();
        paintBackground(*p, rect);
        const QString text = model() ? model()->headerData(logical, orientation()).toString() : QString();
        const QRect tr = rect.adjusted(5, 0, -5, 0);
        p->setPen(Qt::black);
        p->drawText(tr, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text, Qt::ElideRight, tr.width()));
        p->restore();
    }
};

/** Windows 7 list views select only the name, not the whole row */
class FirstColumnSelection : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        QStyleOptionViewItem o(opt);
        if (idx.column() > 0) {
            o.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus | QStyle::State_MouseOver);
        }
        QStyledItemDelegate::paint(p, o, idx);
    }
};

class DiskManagement : public QMainWindow
{
public:
    DiskManagement()
    {
        setWindowTitle(tr7("Disk Management"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-at-disks"))); // title bar: the snap-in; the taskbar takes the mmc.exe toolbox from seven-diskmgmt.desktop
        m_list = new QTreeWidget;
        m_list->setRootIsDecorated(false);
        m_list->setUniformRowHeights(true);
        m_list->setHeader(new Win7Header(m_list));
        m_list->setHeaderLabels({tr7("Volume"), tr7("Location"), tr7("Type"), tr7("File System"),
                                 tr7("Status"), tr7("Capacity"), tr7("Free Space"), tr7("% Free"),
                                 tr7("Fault Tolerance"), tr7("Overhead")});
        // the column widths of diskmgmt.msc (long names get "..." like there)
        const QList<int> widths{142, 85, 74, 95, 85, 107, 73, 100, 132, 85};
        m_list->setTextElideMode(Qt::ElideRight);
        m_list->setFrameShape(QFrame::StyledPanel);
        m_list->setItemDelegate(new FirstColumnSelection(m_list));
        m_list->setStyleSheet(QStringLiteral(
            "QTreeView { background: white; outline: 0; border: 2px solid; border-top-color: #696969; border-left-color: #696969;"
            " border-bottom-color: #e3e3e3; border-right-color: #e3e3e3; }"
            "QTreeView::item { height: 18px; color: black; }"
));
        for (int i = 0; i < widths.size(); ++i) {
            m_list->setColumnWidth(i, widths.at(i));
        }
        m_list->setContextMenuPolicy(Qt::CustomContextMenu);
        m_map = new DiskMap;
        auto *mapScroll = new QScrollArea;
        mapScroll->setWidgetResizable(true);
        mapScroll->setWidget(m_map);
        auto *bottom = new QWidget;
        auto *bv = new QVBoxLayout(bottom);
        bv->setContentsMargins(0, 0, 0, 0);
        bv->setSpacing(0);
        bv->addWidget(mapScroll, 1);
        bv->addWidget(legend());
        auto *split = new QSplitter(Qt::Vertical);
        // Windows 7: a plain grey band between the volume list and the disk map, no grip dots
        split->setStyleSheet(QStringLiteral(
            "QSplitter::handle { image: none; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:0.2 #f7f7f7, stop:0.5 #ececec, stop:0.75 #e3e3e3, stop:0.8 #898989, stop:1 #a3a3a3); }"));
        split->setHandleWidth(8);
        split->addWidget(m_list);
        split->addWidget(bottom);
        split->setStretchFactor(0, 1);
        split->setStretchFactor(1, 3);
        split->setSizes({240, 760});

        // MMC leaves a few pixels of window grey around the result pane
        auto *frame = new QWidget;
        frame->setObjectName(QStringLiteral("win7dmframe"));
        frame->setAttribute(Qt::WA_StyledBackground, true);
        frame->setStyleSheet(QStringLiteral("#win7dmframe { background: #f0f0f0; }"));
        auto *fv = new QVBoxLayout(frame);
        fv->setContentsMargins(0, 5, 0, 0);
        fv->addWidget(split);
        setCentralWidget(frame);
        buildMenus();
        connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
            if (it) {
                m_selDisk = it->data(0, Qt::UserRole).toInt();
                m_selPart = it->data(0, Qt::UserRole + 1).toInt();
                m_map->select(m_selDisk, m_selPart);
                updateActions();
            }
        });
        connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            if (QTreeWidgetItem *it = m_list->itemAt(pos)) {
                contextMenu(it->data(0, Qt::UserRole).toInt(), it->data(0, Qt::UserRole + 1).toInt(), m_list->viewport()->mapToGlobal(pos));
            }
        });
        connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
            openVolume(it->data(0, Qt::UserRole).toInt(), it->data(0, Qt::UserRole + 1).toInt(), false);
        });
        m_map->selected = [this](int d, int p) {
            m_selDisk = d;
            m_selPart = p;
            selectInList();
            updateActions();
        };
        m_map->menu = [this](int d, int p, const QPoint &g) { contextMenu(d, p, g); };
        m_map->activated = [this](int d, int p) { openVolume(d, p, false); };
        // disks come and go: look again every few seconds
        auto *poll = new QTimer(this);
        connect(poll, &QTimer::timeout, this, [this] {
            if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
                return;
            }
            const QByteArray sig = signature();
            if (sig != m_signature) {
                reload();
            }
        });
        poll->start(3000);
        reload();
        resize(1000, 640);
    }

private:
    static QByteArray signature()
    {
        QProcess p;
        p.start(QStringLiteral("lsblk"), {QStringLiteral("-b"), QStringLiteral("-n"), QStringLiteral("-o"), QStringLiteral("NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS,FSAVAIL")});
        p.waitForFinished(5000);
        return p.readAllStandardOutput();
    }

    QWidget *legend()
    {
        auto *w = new QWidget;
        w->setObjectName(QStringLiteral("win7dmlegend"));
        w->setAttribute(Qt::WA_StyledBackground, true);
        w->setStyleSheet(QStringLiteral("#win7dmlegend { background: #f0f0f0; border-top: 1px solid #c8c8c8; }"));
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(8, 4, 8, 4);
        h->setSpacing(4);
        const QList<QPair<QColor, QString>> items{{Qt::black, tr7("Unallocated")},
                                                  {QColor(0x00, 0x00, 0x80), tr7("Primary Partition")},
                                                  {QColor(0x00, 0x80, 0x00), tr7("Extended Partition")},
                                                  {QColor(0x66, 0xcc, 0x66), tr7("Free space")},
                                                  {QColor(0x00, 0x66, 0xff), tr7("Logical Drive")}};
        m_legendItems.clear();
        for (const auto &[c, t] : items) {
            auto *sw = new QLabel;
            sw->setFixedSize(11, 11);
            sw->setStyleSheet(QStringLiteral("background: %1;").arg(c.name()));
            auto *text = new QLabel(t);
            h->addWidget(sw);
            h->addWidget(text);
            h->addSpacing(8);
            m_legendItems.append(qMakePair(sw, text));
        }
        h->addStretch(1);
        return w;
    }

    void buildMenus()
    {
        // the MMC toolbar of Windows 7 (icons taken from it)
        m_refresh = new QAction(flatIcon(QStringLiteral("seven-mmc-refresh")), tr7("Refresh"), this);
        m_rescan = new QAction(flatIcon(QStringLiteral("seven-mmc-find")), tr7("Rescan Disks"), this);
        m_props = new QAction(flatIcon(QStringLiteral("seven-mmc-props")), tr7("Properties"), this);
        m_delete = new QAction(flatIcon(QStringLiteral("seven-mmc-delete")), tr7("Delete"), this);
        m_open = new QAction(flatIcon(QStringLiteral("seven-mmc-open")), tr7("Open"), this);
        auto *help = new QAction(flatIcon(QStringLiteral("seven-mmc-help")), tr7("Help"), this);
        auto *back = new QAction(flatIcon(QStringLiteral("seven-mmc-back")), tr7("Back"), this);
        auto *forward = new QAction(flatIcon(QStringLiteral("seven-mmc-forward")), tr7("Forward"), this);
        auto *console = new QAction(flatIcon(QStringLiteral("seven-mmc-console")), tr7("Show/Hide Console Tree"), this);
        auto *actionPane = new QAction(flatIcon(QStringLiteral("seven-mmc-actionpane")), tr7("Show/Hide Action Pane"), this);
        auto *settings = new QAction(flatIcon(QStringLiteral("seven-mmc-settings")), tr7("Options"), this);
        // both panes are shown, so both buttons stay pressed, as in Windows 7
        for (QAction *t : {console, actionPane}) {
            t->setCheckable(true);
            t->setChecked(true);
        }
        connect(settings, &QAction::triggered, this, [this] { properties(m_selDisk, -1); });
        connect(m_refresh, &QAction::triggered, this, [this] { reload(); });
        connect(m_rescan, &QAction::triggered, this, [this] { reload(); });
        connect(m_props, &QAction::triggered, this, [this] { properties(m_selDisk, m_selPart); });
        connect(m_delete, &QAction::triggered, this, [this] { run(m_selDisk, m_selPart, &deleteVolume); });
        connect(m_open, &QAction::triggered, this, [this] { openVolume(m_selDisk, m_selPart, false); });
        connect(help, &QAction::triggered, this, [this] { showHelp(); });
        QMenu *file = menuBar()->addMenu(tr7("&File"));
        file->addAction(tr7("Exit"), this, &QWidget::close);
        m_actionMenu = menuBar()->addMenu(tr7("&Action"));
        connect(m_actionMenu, &QMenu::aboutToShow, this, [this, help] {
            m_actionMenu->clear();
            m_actionMenu->addAction(m_refresh);
            m_actionMenu->addAction(m_rescan);
            m_actionMenu->addSeparator();
            if (m_selDisk >= 0) {
                m_actionMenu->addMenu(buildContextMenu(m_selDisk, m_selPart, m_actionMenu))->setText(tr7("All Tasks"));
            }
            m_actionMenu->addSeparator();
            m_actionMenu->addAction(help);
        });
        QMenu *view = menuBar()->addMenu(tr7("&View"));
        auto *top = view->addAction(tr7("Top: Volume List"));
        top->setCheckable(true);
        top->setChecked(true);
        connect(top, &QAction::toggled, m_list, &QWidget::setVisible);
        QMenu *helpMenu = menuBar()->addMenu(tr7("&Help"));
        helpMenu->addAction(help);
        QToolBar *bar = addToolBar(tr7("Standard"));
        bar->setMovable(false);
        bar->setIconSize(QSize(16, 16));
        bar->addAction(back);
        bar->addAction(forward);
        bar->addSeparator();
        bar->addAction(console);
        bar->addSeparator();
        bar->addAction(help);
        bar->addAction(actionPane);
        bar->addSeparator();
        bar->addAction(m_refresh);
        bar->addAction(m_delete);
        bar->addAction(m_props);
        bar->addAction(m_open);
        bar->addAction(m_rescan);
        bar->addAction(settings);
        // these buttons only look disabled in Windows 7 when there is really nothing to do
        for (QAction *a : {back, forward}) {
            a->setEnabled(false);
        }
        // menu bar and toolbar: the lavender gradient of MMC in Windows 7
        mmcChrome(menuBar(), bar);
        win7WrapMenuBar(this); // Windows 7: the menu bar wraps instead of showing a "»" arrow
    }

    void reload()
    {
        m_signature = signature();
        m_disks = scan();
        m_map->setDisks(m_disks);
        m_list->clear();
        for (int di = 0; di < m_disks.size(); ++di) {
            const Disk &d = m_disks.at(di);
            for (int pi = 0; pi < d.parts.size(); ++pi) {
                const Part &p = d.parts.at(pi);
                // Windows 7 lists only volumes: no EFI or other hidden system partitions
                if (p.unallocated || p.hidden || p.extended || p.parttypeName == QLatin1String("EFI System")) {
                    continue;
                }
                const qint64 free = p.avail;
                auto *it = new QTreeWidgetItem(m_list, {volumeName(p).simplified(), tr7("Simple"), d.removable ? tr7("Basic") : tr7("Basic"),
                                                        fsName(p.fstype), status(d, p), sizeText(p.size), free >= 0 ? sizeText(free) : QString(),
                                                        free >= 0 && p.size > 0 ? QStringLiteral("%1 %").arg(qRound(100.0 * free / p.size)) : QString(),
                                                        tr7("No"), QStringLiteral("0%")});
                it->setIcon(0, QIcon::fromTheme(QStringLiteral("seven-mmc-volume"))); // dmdskres.dll
                it->setData(0, Qt::UserRole, di);
                it->setData(0, Qt::UserRole + 1, pi);
                if (d.isProtected()) {
                    it->setToolTip(0, d.protectReason());
                }
            }
        }
        selectInList();
        updateActions();
        bool used[5] = {true, true, false, false, false};
        for (const Disk &d : std::as_const(m_disks)) {
            for (const Part &p : d.parts) {
                used[2] = used[2] || p.extended;
                used[3] = used[3] || (p.unallocated && p.logical);
                used[4] = used[4] || (!p.unallocated && p.logical);
            }
        }
        for (int i = 0; i < m_legendItems.size(); ++i) {
            m_legendItems[i].first->setVisible(used[i]);
            m_legendItems[i].second->setVisible(used[i]);
        }
    }

    void selectInList()
    {
        QSignalBlocker block(m_list);
        m_list->clearSelection();
        m_list->setCurrentItem(nullptr);
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = m_list->topLevelItem(i);
            if (it->data(0, Qt::UserRole).toInt() == m_selDisk && it->data(0, Qt::UserRole + 1).toInt() == m_selPart) {
                m_list->setCurrentItem(it);
            }
        }
    }

    const Part *part(int d, int p) const
    {
        if (d < 0 || d >= m_disks.size() || p < 0 || p >= m_disks.at(d).parts.size()) {
            return nullptr;
        }
        return &m_disks.at(d).parts.at(p);
    }

    void updateActions()
    {
        const Part *p = part(m_selDisk, m_selPart);
        const bool canChange = p && !p->unallocated && !m_disks.at(m_selDisk).isProtected();
        m_delete->setEnabled(canChange);
        m_open->setEnabled(p && !p->unallocated && !p->fstype.isEmpty() && p->fstype != QLatin1String("swap"));
        m_props->setEnabled(m_selDisk >= 0);
    }

    /** the unallocated space right after partition p (for Extend) */
    qint64 freeAfter(int d, int p) const
    {
        const QList<Part> &parts = m_disks.at(d).parts;
        return p + 1 < parts.size() && parts.at(p + 1).unallocated ? parts.at(p + 1).size : 0;
    }

    QString nextLetter() const
    {
        QSet<QString> used;
        for (const Disk &d : m_disks) {
            for (const Part &p : d.parts) {
                used.insert(p.letter);
            }
        }
        for (char c = 'C'; c <= 'Z'; ++c) {
            const QString l = QStringLiteral("%1:").arg(QLatin1Char(c));
            if (!used.contains(l)) {
                return l;
            }
        }
        return QString();
    }

    QMenu *buildContextMenu(int di, int pi, QWidget *parent)
    {
        auto *m = new QMenu(parent);
        if (di < 0 || di >= m_disks.size()) {
            return m;
        }
        const Disk d = m_disks.at(di);
        const bool locked = d.isProtected();
        auto add = [m, locked, &d](const QString &text, bool enabled, std::function<void()> f) {
            QAction *a = m->addAction(text, std::move(f));
            a->setEnabled(enabled && !locked);
            if (locked && enabled) {
                a->setToolTip(d.protectReason());
            }
            return a;
        };
        if (pi == -1 || pi == -2) {
            // the disk itself
            add(tr7("Initialize Disk", "menu"), d.pttype.isEmpty() && !d.wholeDiskFs && !d.optical && !d.noMedia, [this, d] {
                if (initializeDisk(this, d)) {
                    reload();
                }
            });
            m->addAction(tr7("Convert to Dynamic Disk..."))->setEnabled(false);
            m->addSeparator();
            m->addAction(tr7("Properties"), this, [this, di] { properties(di, -1); });
            m->addSeparator();
            m->addAction(tr7("Help"), this, [this] { showHelp(); });
            if (locked) {
                m->setToolTipsVisible(true);
            }
            return m;
        }
        const Part p = d.parts.at(pi);
        if (p.unallocated) {
            add(tr7("New Simple Volume..."), true, [this, d, p] {
                if (stillAllowed(this, d.kname)) {
                    NewVolumeWizard w(this, d, p, nextLetter());
                    if (w.exec() == QDialog::Accepted) {
                        reload();
                    }
                }
            });
            for (const QString &t : {tr7("New Spanned Volume..."), tr7("New Striped Volume..."), tr7("New Mirrored Volume..."),
                                     tr7("New RAID-5 Volume...")}) {
                m->addAction(t)->setEnabled(false);
            }
        } else {
            const bool hasFs = !p.fstype.isEmpty() && p.fstype != QLatin1String("swap");
            m->addAction(tr7("Open"), this, [this, di, pi] { openVolume(di, pi, false); })->setEnabled(hasFs);
            m->addAction(tr7("Explore"), this, [this, di, pi] { openVolume(di, pi, true); })->setEnabled(hasFs);
            m->addSeparator();
            m->addAction(tr7("Mark Partition as Active"))->setEnabled(false);
            m->addAction(tr7("Change Drive Letter and Paths..."))->setEnabled(false);
            add(tr7("Format..."), true, [this, di, pi] { run(di, pi, &formatDialog); });
            m->addSeparator();
            const quint64 modes = resizeModes(p.fstype);
            const qint64 after = freeAfter(di, pi);
            add(tr7("Extend Volume..."), after > 0 && (modes & (4 | 16)) && !d.wholeDiskFs, [this, di, pi, after] {
                const Disk dd = m_disks.at(di);
                if (extendDialog(this, dd, dd.parts.at(pi), after)) {
                    reload();
                }
            });
            add(tr7("Shrink Volume..."), (modes & (2 | 8)) && !d.wholeDiskFs, [this, di, pi] { run(di, pi, &shrinkDialog); });
            m->addAction(tr7("Add Mirror..."))->setEnabled(false);
            add(tr7("Delete Volume..."), true, [this, di, pi] { run(di, pi, &deleteVolume); });
        }
        m->addSeparator();
        m->addAction(tr7("Properties"), this, [this, di, pi] { properties(di, pi); });
        m->addSeparator();
        m->addAction(tr7("Help"), this, [this] { showHelp(); });
        m->setToolTipsVisible(true);
        return m;
    }

    void contextMenu(int di, int pi, const QPoint &global)
    {
        m_selDisk = di;
        m_selPart = pi;
        m_map->select(di, pi);
        selectInList();
        updateActions();
        QMenu *m = buildContextMenu(di, pi, this);
        m->exec(global);
        m->deleteLater();
    }

    void run(int di, int pi, bool (*op)(QWidget *, const Disk &, const Part &))
    {
        const Part *p = part(di, pi);
        if (!p || p->unallocated || m_disks.at(di).isProtected()) {
            return;
        }
        if (op(this, m_disks.at(di), *p)) {
            reload();
        }
    }

    void openVolume(int di, int pi, bool explorer)
    {
        const Part *p = part(di, pi);
        if (!p || p->unallocated || p->fstype.isEmpty() || p->fstype == QLatin1String("swap")) {
            return;
        }
        QString where = p->mount;
        if (where.isEmpty()) {
            QVariantList out;
            const QString err = udisks(udisksPath(p->kname), QStringLiteral("Filesystem"), QStringLiteral("Mount"), {QVariantMap{}}, &out);
            if (!err.isEmpty()) {
                failed(this, tr7("The volume could not be opened."), err);
                return;
            }
            where = out.value(0).toString();
        }
        Q_UNUSED(explorer)
        launch(QStringLiteral("dolphin"), {where});
    }

    void properties(int di, int pi)
    {
        if (di < 0 || di >= m_disks.size()) {
            return;
        }
        const Disk &d = m_disks.at(di);
        QStringList rows;
        if (pi < 0) {
            rows << tr7("Disk %1").arg(d.index) << tr7("Device: %1").arg(d.dev) << tr7("Model: %1").arg(d.model)
                 << tr7("Connection: %1").arg(d.tran.toUpper()) << tr7("Capacity: %1").arg(sizeText(d.size))
                 << tr7("Partition style: %1").arg(d.pttype == QLatin1String("gpt") ? tr7("GUID Partition Table (GPT)")
                                                             : d.pttype == QLatin1String("dos") ? tr7("Master Boot Record (MBR)")
                                                                                                : tr7("None"));
        } else {
            const Part &p = d.parts.at(pi);
            rows << volumeName(p) << tr7("Device: %1").arg(p.dev.isEmpty() ? QStringLiteral("—") : p.dev)
                 << tr7("File system: %1").arg(fsName(p.fstype).isEmpty() ? QStringLiteral("—") : fsName(p.fstype))
                 << tr7("Capacity: %1").arg(sizeText(p.size));
            if (p.used >= 0) {
                rows << tr7("Used: %1").arg(sizeText(p.used)) << tr7("Free: %1").arg(sizeText(p.avail));
            }
            if (!p.mount.isEmpty()) {
                rows << tr7("Mounted at: %1").arg(p.mount);
            }
            rows << tr7("Status: %1").arg(status(d, p));
        }
        if (d.isProtected()) {
            rows << QString() << d.protectReason();
        }
        QMessageBox::information(this, tr7("Properties"), rows.join(QLatin1Char('\n')));
    }

    void showHelp()
    {
        QMessageBox::information(this, tr7("Disk Management"),
                                 tr7("Right-click a volume or unallocated space to create, format, shrink, extend or delete a volume.\n\nDisks with the system the computer is running from and disks with Windows installed are protected: they cannot be changed (they are marked with a shield)."));
    }

    QList<QPair<QLabel *, QLabel *>> m_legendItems; // unallocated, primary, extended, free, logical
    QTreeWidget *m_list;
    DiskMap *m_map;
    QMenu *m_actionMenu;
    QAction *m_refresh;
    QAction *m_rescan;
    QAction *m_props;
    QAction *m_delete;
    QAction *m_open;
    QList<Disk> m_disks;
    QByteArray m_signature;
    int m_selDisk = -1;
    int m_selPart = -1;
};
} // namespace Win7Disk
