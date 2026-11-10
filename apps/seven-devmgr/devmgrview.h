/*
 * Device Manager in the style of Windows 7, listing the real hardware of this Linux machine
 * (PCI and USB devices, disks, monitors, input devices, processors, network, battery...).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once
#include "win7i18n.h"

#include "../seven-common/win7header.h"
#include "../seven-common/win7states.h"

#include <QAction>
#include <QApplication>
#include <QCollator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QGroupBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QDateTime>
#include <QLocale>
#include <cmath>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QSysInfo>
#include <QTabWidget>
#include <QToolBar>
#include <QStyledItemDelegate>
#include <QStandardPaths>
#include <QTreeView>
#include <QTimer>
#include <QVBoxLayout>

namespace Win7DevMgr
{
enum Roles {
    DetailsRole = Qt::UserRole + 1, // QVariantMap with the device properties
};

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(f.readAll()).trimmed();
}

QString run(const QString &program, const QStringList &args)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(4000)) {
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

QString driverOf(const QString &sysDevicePath)
{
    const QFileInfo link(sysDevicePath + QStringLiteral("/driver"));
    return link.exists() ? QFileInfo(link.symLinkTarget()).fileName() : QString();
}

/* ---------------- categories (as in Windows 7) ---------------- */

struct Category {
    QString key;
    QString title;
    QString icon;
};

const QList<Category> &categories()
{
    static const QList<Category> list{
        {QStringLiteral("dvd"), tr7("DVD/CD-ROM drives"), QStringLiteral("seven-devmgr-dvd")},
        {QStringLiteral("ide"), tr7("IDE ATA/ATAPI controllers"), QStringLiteral("seven-devmgr-ide")},
        {QStringLiteral("bluetooth"), tr7("Bluetooth Radios"), QStringLiteral("seven-devmgr-bluetooth")},
        {QStringLiteral("video"), tr7("Display adapters"), QStringLiteral("seven-devmgr-video")},
        {QStringLiteral("disk"), tr7("Disk drives"), QStringLiteral("seven-devmgr-disk")},
        {QStringLiteral("sound"), tr7("Sound, video and game controllers"), QStringLiteral("seven-devmgr-sound")},
        {QStringLiteral("keyboard"), tr7("Keyboards"), QStringLiteral("seven-devmgr-keyboard")},
        {QStringLiteral("computer"), tr7("Computer"), QStringLiteral("seven-devmgr-computer")},
        {QStringLiteral("usb"), tr7("Universal Serial Bus controllers"), QStringLiteral("seven-devmgr-usb")},
        {QStringLiteral("storage"), tr7("Storage controllers"), QStringLiteral("seven-devmgr-storage")},
        {QStringLiteral("battery"), tr7("Batteries"), QStringLiteral("seven-devmgr-battery")},
        {QStringLiteral("monitor"), tr7("Monitors"), QStringLiteral("seven-devmgr-monitor")},
        {QStringLiteral("mouse"), tr7("Mice and other pointing devices"), QStringLiteral("seven-devmgr-mouse")},
        {QStringLiteral("portable"), tr7("Portable Devices"), QStringLiteral("seven-devmgr-portable")},
        {QStringLiteral("imaging"), tr7("Imaging devices"), QStringLiteral("seven-devmgr-imaging")},
        {QStringLiteral("cpu"), tr7("Processors"), QStringLiteral("seven-devmgr-cpu")},
        {QStringLiteral("network"), tr7("Network adapters"), QStringLiteral("seven-devmgr-network")},
        {QStringLiteral("system"), tr7("System devices"), QStringLiteral("seven-devmgr-system")},
    };
    return list;
}

struct Device {
    QString category;
    QString name;
    QVariantMap details; // "Тип пристрою", "Виробник", "Розташування", "Драйвер", ...
};

QString pciCategory(const QString &classCode)
{
    const QString c = classCode.left(2);
    const QString sub = classCode.mid(2, 2);
    if (c == QLatin1String("03")) {
        return QStringLiteral("video");
    }
    if (c == QLatin1String("04")) {
        return QStringLiteral("sound");
    }
    if (c == QLatin1String("01")) {
        return (sub == QLatin1String("01") || sub == QLatin1String("06")) ? QStringLiteral("ide") : QStringLiteral("storage");
    }
    if (c == QLatin1String("02")) {
        return QStringLiteral("network");
    }
    if (c == QLatin1String("0c") && sub == QLatin1String("03")) {
        return QStringLiteral("usb");
    }
    return QStringLiteral("system");
}

/** All devices found on this machine */
QList<Device> scan()
{
    QList<Device> out;
    auto add = [&out](const QString &cat, const QString &name, QVariantMap details) {
        if (!name.trimmed().isEmpty()) {
            out.append({cat, name.trimmed(), std::move(details)});
        }
    };

    // --- PCI: video, sound, storage, network, USB controllers, system devices ---
    const QString lspci = run(QStringLiteral("lspci"), {QStringLiteral("-vmm"), QStringLiteral("-nn")});
    for (const QString &block : lspci.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts)) {
        QMap<QString, QString> kv;
        for (const QString &line : block.split(QLatin1Char('\n'))) {
            const int i = line.indexOf(QLatin1Char(':'));
            if (i > 0) {
                kv.insert(line.left(i), line.mid(i + 1).trimmed());
            }
        }
        auto strip = [](QString s) {
            return s.remove(QRegularExpression(QStringLiteral("\\s*\\[[0-9a-fA-F]{4}\\]$"))).trimmed();
        };
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("\\[([0-9a-fA-F]{4})\\]$")).match(kv.value(QStringLiteral("Class")));
        const QString classCode = m.hasMatch() ? m.captured(1).toLower() : QString();
        const QString vendor = strip(kv.value(QStringLiteral("Vendor")));
        const QString device = strip(kv.value(QStringLiteral("Device")));
        const QString slot = kv.value(QStringLiteral("Slot"));
        const QString sysPath = QStringLiteral("/sys/bus/pci/devices/0000:") + slot;
        QVariantMap d{{tr7("Device type"), strip(kv.value(QStringLiteral("Class")))},
                      {tr7("Manufacturer"), vendor},
                      {tr7("Location"), tr7("PCI bus %1").arg(slot)},
                      {tr7("Driver"), driverOf(sysPath)},
                      {QStringLiteral("_sys"), sysPath}};
        QString name = device;
        if (!vendor.isEmpty() && !name.contains(vendor.section(QLatin1Char(' '), 0, 0), Qt::CaseInsensitive)) {
            name = vendor.section(QLatin1Char(','), 0, 0).section(QLatin1Char('['), 0, 0).trimmed() + QLatin1Char(' ') + name;
        }
        add(pciCategory(classCode), name, d);
    }

    // --- Disks and optical drives ---
    for (const QString &blk : QDir(QStringLiteral("/sys/block")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (blk.startsWith(QLatin1String("loop")) || blk.startsWith(QLatin1String("ram")) || blk.startsWith(QLatin1String("zram"))) {
            continue;
        }
        const QString base = QStringLiteral("/sys/block/") + blk;
        QString model = readFile(base + QStringLiteral("/device/model"));
        const QString vendor = readFile(base + QStringLiteral("/device/vendor"));
        if (model.isEmpty()) {
            continue;
        }
        const bool optical = blk.startsWith(QLatin1String("sr"));
        const bool usb = QFileInfo(base).canonicalFilePath().contains(QLatin1String("/usb"));
        const double gb = readFile(base + QStringLiteral("/size")).toDouble() * 512.0 / 1e9;
        const QString name = (vendor.isEmpty() || model.startsWith(vendor) ? model : vendor + QLatin1Char(' ') + model)
            + (usb ? QStringLiteral(" USB Device") : QString());
        add(optical ? QStringLiteral("dvd") : QStringLiteral("disk"),
            name,
            {{tr7("Device type"), optical ? tr7("DVD/CD-ROM drives") : tr7("Disk drives")},
             {tr7("Location"), usb ? QStringLiteral("USB") : tr7("Internal (%1)").arg(blk)},
             {tr7("Capacity"), optical ? QString() : tr7("%1 GB").arg(QString::number(gb, 'f', 1))},
             {tr7("Driver"), driverOf(base + QStringLiteral("/device"))},
             {QStringLiteral("_sys"), base + QStringLiteral("/device")}});
    }

    // --- Keyboards and mice ---
    const QString inputs = readFile(QStringLiteral("/proc/bus/input/devices"));
    for (const QString &block : inputs.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts)) {
        QString name, handlers, phys, sysfs;
        for (const QString &line : block.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("N: Name="))) {
                name = line.mid(8).remove(QLatin1Char('"'));
            } else if (line.startsWith(QLatin1String("H: Handlers="))) {
                handlers = line.mid(12);
            } else if (line.startsWith(QLatin1String("P: Phys="))) {
                phys = line.mid(8);
            } else if (line.startsWith(QLatin1String("S: Sysfs="))) {
                sysfs = QStringLiteral("/sys") + line.mid(9);
            }
        }
        const bool isMouse = handlers.contains(QLatin1String("mouse"));
        const bool isKbd = handlers.contains(QLatin1String("kbd")) && handlers.contains(QLatin1String("sysrq"));
        if (!isMouse && !isKbd) {
            continue;
        }
        QString shown = name;
        if (isKbd && name.contains(QLatin1String("AT Translated"))) {
            shown = tr7("Standard PS/2 Keyboard");
        }
        add(isMouse ? QStringLiteral("mouse") : QStringLiteral("keyboard"),
            shown,
            {{tr7("Device type"), isMouse ? tr7("Mice and other pointing devices") : tr7("Keyboards")},
             {tr7("Location"), phys.isEmpty() ? tr7("Unknown") : phys},
             {tr7("Kernel description"), name},
             {QStringLiteral("_sys"), sysfs}});
    }

    // --- Processors: one entry per logical processor, like Windows ---
    const QString cpuinfo = readFile(QStringLiteral("/proc/cpuinfo"));
    int index = 0;
    for (const QString &block : cpuinfo.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts)) {
        QString model, vendor;
        for (const QString &line : block.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("model name"))) {
                model = line.section(QLatin1Char(':'), 1).trimmed();
            } else if (line.startsWith(QLatin1String("vendor_id"))) {
                vendor = line.section(QLatin1Char(':'), 1).trimmed();
            }
        }
        add(QStringLiteral("cpu"), model,
            {{tr7("Device type"), tr7("Processors")},
             {tr7("Manufacturer"), vendor},
             {tr7("Location"), tr7("Logical processor %1").arg(index)},
             {QStringLiteral("_sys"), QStringLiteral("/sys/devices/system/cpu/cpu%1").arg(index)}});
        ++index;
    }

    // --- Monitors (connected outputs, name from EDID) ---
    for (const QString &conn : QDir(QStringLiteral("/sys/class/drm")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString base = QStringLiteral("/sys/class/drm/") + conn;
        if (readFile(base + QStringLiteral("/status")) != QLatin1String("connected")) {
            continue;
        }
        QString name = tr7("Generic PnP Monitor");
        QFile edid(base + QStringLiteral("/edid"));
        if (edid.open(QIODevice::ReadOnly)) {
            const QByteArray e = edid.readAll();
            for (int off = 54; off + 18 <= qMin<int>(e.size(), 126); off += 18) {
                if (e[off] == 0 && e[off + 1] == 0 && uchar(e[off + 3]) == 0xFC) {
                    const QString n = QString::fromLatin1(e.mid(off + 5, 13)).section(QLatin1Char('\n'), 0, 0).trimmed();
                    if (!n.isEmpty()) {
                        name = tr7("%1 Monitor").arg(n);
                    }
                }
            }
        }
        add(QStringLiteral("monitor"), name,
            {{tr7("Device type"), tr7("Monitors")}, {tr7("Location"), conn.section(QLatin1Char('-'), 1)}, {QStringLiteral("_sys"), base}});
    }

    // --- Bluetooth radios ---
    for (const QString &hci : QDir(QStringLiteral("/sys/class/bluetooth")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (hci.contains(QLatin1Char(':'))) {
            continue;
        }
        const QString dev = QStringLiteral("/sys/class/bluetooth/") + hci + QStringLiteral("/device");
        QString name = readFile(dev + QStringLiteral("/../product"));
        if (name.isEmpty()) {
            name = tr7("Bluetooth Radio (%1)").arg(hci);
        }
        add(QStringLiteral("bluetooth"), name,
            {{tr7("Device type"), tr7("Bluetooth Radios")}, {tr7("Driver"), driverOf(dev)}, {QStringLiteral("_sys"), dev}});
    }

    // --- Batteries and AC adapters ---
    for (const QString &ps : QDir(QStringLiteral("/sys/class/power_supply")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString base = QStringLiteral("/sys/class/power_supply/") + ps;
        const QString type = readFile(base + QStringLiteral("/type"));
        if (type == QLatin1String("Battery")) {
            const QString model = readFile(base + QStringLiteral("/model_name"));
            add(QStringLiteral("battery"), tr7("Microsoft ACPI-Compliant Control Method Battery") + (model.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(model)),
                {{tr7("Device type"), tr7("Batteries")},
                 {tr7("Manufacturer"), readFile(base + QStringLiteral("/manufacturer"))},
                 {tr7("Charge"), readFile(base + QStringLiteral("/capacity")) + QStringLiteral(" %")},
                 {QStringLiteral("_sys"), base}});
        } else if (type == QLatin1String("Mains")) {
            add(QStringLiteral("battery"), tr7("Microsoft AC Adapter"), {{tr7("Device type"), tr7("Batteries")}});
        }
    }

    // --- Webcams ---
    for (const QString &v : QDir(QStringLiteral("/sys/class/video4linux")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString name = readFile(QStringLiteral("/sys/class/video4linux/") + v + QStringLiteral("/name"));
        if (readFile(QStringLiteral("/sys/class/video4linux/") + v + QStringLiteral("/index")) == QLatin1String("0")) {
            add(QStringLiteral("imaging"), name, {{tr7("Device type"), tr7("Imaging devices")},
                                                  {QStringLiteral("_sys"), QStringLiteral("/sys/class/video4linux/") + v + QStringLiteral("/device")}});
        }
    }

    // --- Computer ---
    const bool x64 = QSysInfo::currentCpuArchitecture().contains(QLatin1String("64"));
    add(QStringLiteral("computer"), x64 ? tr7("ACPI x64-based PC") : tr7("ACPI-based PC"),
        {{tr7("Device type"), tr7("Computer")},
         {tr7("Manufacturer"), readFile(QStringLiteral("/sys/class/dmi/id/sys_vendor"))},
         {tr7("Model"), readFile(QStringLiteral("/sys/class/dmi/id/product_version"))}});
    return out;
}

/* ---------------- tree with Windows 7 arrows ---------------- */

class SunkenEdge : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect r = rect();
        p.fillRect(r.left(), r.top(), r.width(), 1, QColor(0xa0, 0xa0, 0xa0));
        p.fillRect(r.left(), r.top(), 1, r.height(), QColor(0xa0, 0xa0, 0xa0));
        p.fillRect(r.left() + 1, r.top() + 1, r.width() - 2, 1, QColor(0x69, 0x69, 0x69));
        p.fillRect(r.left() + 1, r.top() + 1, 1, r.height() - 2, QColor(0x69, 0x69, 0x69));
        p.fillRect(r.left(), r.bottom(), r.width(), 1, Qt::white);
        p.fillRect(r.right(), r.top(), 1, r.height(), Qt::white);
        p.fillRect(r.left() + 1, r.bottom() - 1, r.width() - 2, 1, QColor(0xe3, 0xe3, 0xe3));
        p.fillRect(r.right() - 1, r.top() + 1, 1, r.height() - 2, QColor(0xe3, 0xe3, 0xe3));
    }
};

// One row of the MMC tree as Windows 7 draws it: the hot/selected box hugs the picture and the name
class Win7TreeDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem o(option);
        initStyleOption(&o, index);
        return QSize(2 + 16 + 6 + o.fontMetrics.horizontalAdvance(o.text) + 5, 18);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem o(option);
        initStyleOption(&o, index);
        const QRect box(o.rect.left() - 1, o.rect.top(), 2 + 16 + 6 + o.fontMetrics.horizontalAdvance(o.text) + 5, o.rect.height());
        QStyleOptionViewItem bg(o);
        bg.rect = box;
        bg.text.clear();
        bg.icon = QIcon();
        bg.features &= ~(QStyleOptionViewItem::HasDisplay | QStyleOptionViewItem::HasDecoration);
        bg.state &= ~QStyle::State_HasFocus;
        const QWidget *w = o.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &bg, p, w);
        o.icon.paint(p, QRect(box.left() + 2, box.top() + 1, 16, 16));
        p->save();
        p->setFont(o.font);
        p->setPen(Qt::black);
        p->drawText(QRect(box.left() + 24, box.top() - 1, box.width() - 24, box.height()), Qt::AlignLeft | Qt::AlignVCenter, o.text);
        p->restore();
    }
};

class Win7Tree : public QTreeView
{
public:
    explicit Win7Tree(QWidget *parent = nullptr)
        : QTreeView(parent)
    {
        setMouseTracking(true);
    }

protected:
    QRect arrowRect(const QModelIndex &index) const
    {
        const QRect item = visualRect(index);
        return QRect(item.left() - indentation(), item.top(), indentation(), item.height());
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QModelIndex idx = indexAt(event->pos());
        const QPersistentModelIndex hot = (idx.isValid() && model()->hasChildren(idx) && arrowRect(idx).contains(event->pos())) ? QPersistentModelIndex(idx)
                                                                                                                         : QPersistentModelIndex();
        if (hot != m_hot) {
            m_hot = hot;
            viewport()->update();
        }
        QTreeView::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hot = QPersistentModelIndex();
        viewport()->update();
        QTreeView::leaveEvent(event);
    }

    // Measured on Windows 7 (MMC tree, 20 px per level): the selection box starts 1 px before the item,
    // the tree line runs 9 px left of the box, dots every other pixel in #6d6d6d (no lines at the root),
    // and each arrow sits on an opaque 16x16 white cell that hides the lines under it
    void drawBranches(QPainter *p, const QRect &rect, const QModelIndex &index) const override
    {
        const int itemX = rect.right() + 1;
        const int top = rect.top();
        const int bottom = rect.bottom();
        const int mid = top + 10;
        auto lineX = [](int boxLeft) {
            return boxLeft - 9;
        };
        const QColor dot(0x6d, 0x6d, 0x6d);
        auto vline = [&](int x, int y0, int y1) {
            for (int y = y0; y <= y1; ++y) {
                if (((x + y) & 1) == 0) {
                    p->fillRect(x, y, 1, 1, dot);
                }
            }
        };
        auto hline = [&](int x0, int x1, int y) {
            for (int x = x0; x <= x1; ++x) {
                if (((x + y) & 1) == 0) {
                    p->fillRect(x, y, 1, 1, dot);
                }
            }
        };
        p->save();
        // lines of the ancestors that have more children below (the top level has none)
        QModelIndex anc = index.parent();
        int ancBox = itemX - 1 - indentation();
        while (anc.isValid() && anc.parent().isValid()) {
            if (anc.sibling(anc.row() + 1, 0).isValid()) {
                vline(lineX(ancBox), top, bottom);
            }
            anc = anc.parent();
            ancBox -= indentation();
        }
        const int lx = lineX(itemX - 1);
        if (index.parent().isValid()) {
            const bool last = !index.sibling(index.row() + 1, 0).isValid();
            vline(lx, top, last ? mid : bottom);
            hline(lx, itemX, mid);
        }
        if (model()->hasChildren(index)) {
            // the genuine glyph cells, cut from Windows 7 (opaque, they cover the lines)
            static const QString dir = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("seven-aero/tree"), QStandardPaths::LocateDirectory);
            const QString name = QStringLiteral("%1/%2%3.png").arg(dir, isExpanded(index) ? QStringLiteral("expanded") : QStringLiteral("collapsed"),
                                                                     index == m_hot ? QStringLiteral("-hot") : QString());
            p->drawPixmap(lx - 8, top + 2, QPixmap(name));
        }
        p->restore();
    }

private:
    QPersistentModelIndex m_hot;
};

/* ---------------- properties dialog ---------------- */

/* ---------------- "Properties: <device>" (devmgr.dll 103 / 106 / 372 / 110) ---------------- */

QRect dlu(int x, int y, int w, int h)
{
    auto px = [](int v) { return int(std::lround(v * 7 / 4.0)); };
    auto py = [](int v) { return int(std::lround(v * 15 / 8.0)); };
    return QRect(px(x), py(y), px(w), py(h));
}

template<typename W>
W *place(QWidget *parent, W *w, const QRect &r)
{
    w->setParent(parent);
    w->setGeometry(r);
    return w;
}

QLabel *plain(QWidget *parent, const QString &t, const QRect &r)
{
    auto *l = place(parent, new QLabel(t), r);
    l->setWordWrap(true);
    l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

/** What Linux knows about the driver of a device */
struct DriverInfo {
    QString driver;   // bound kernel driver
    QString module;   // loadable module, empty when built into the kernel
    QString provider, date, version, signer, file, license, description;
};

DriverInfo driverInfo(const QString &sys, const QString &fallbackDriver, const QString &vendor)
{
    DriverInfo d;
    // the driver may sit on a parent device (input -> usb/i2c device, block -> scsi device)
    QString dev = sys;
    for (int i = 0; i < 4 && !dev.isEmpty(); ++i) {
        if (QFileInfo::exists(dev + QStringLiteral("/driver"))) {
            d.driver = QFileInfo(QFileInfo(dev + QStringLiteral("/driver")).symLinkTarget()).fileName();
            const QFileInfo mod(dev + QStringLiteral("/driver/module"));
            if (mod.exists()) {
                d.module = QFileInfo(mod.symLinkTarget()).fileName();
            }
            break;
        }
        dev = QFileInfo(dev).canonicalFilePath().section(QLatin1Char('/'), 0, -2);
    }
    if (d.driver.isEmpty()) {
        d.driver = fallbackDriver;
    }
    const QString kernel = QSysInfo::kernelVersion();
    if (!d.module.isEmpty()) {
        auto field = [&d](const char *f) {
            return run(QStringLiteral("modinfo"), {QStringLiteral("-F"), QLatin1String(f), d.module}).trimmed().section(QLatin1Char('\n'), 0, 0);
        };
        d.file = field("filename");
        d.version = field("version");
        d.signer = field("signer");
        d.license = field("license");
        d.description = field("description");
        QString author = field("author");
        author.remove(QRegularExpression(QStringLiteral("\\s*<[^>]*>")));
        d.provider = d.file.contains(QLatin1String("nvidia")) ? QStringLiteral("NVIDIA Corporation") : author.isEmpty() ? tr7("Linux kernel") : author.trimmed();
        if (d.version.isEmpty()) {
            d.version = kernel;
        }
        QFileInfo fi(d.file);
        d.date = fi.exists() ? QLocale{}.toString(fi.lastModified().date(), QStringLiteral("dd.MM.yyyy")) : QString();
    } else if (!d.driver.isEmpty()) {
        d.provider = tr7("Linux kernel");
        d.version = kernel;
        d.file = QStringLiteral("/boot/vmlinuz-") + kernel;
        d.signer = tr7("Built into the Linux kernel");
        d.license = QStringLiteral("GPL");
    }
    if (d.date.isEmpty() && !d.driver.isEmpty()) {
        const QFileInfo vmlinuz(QStringLiteral("/boot/vmlinuz-") + kernel);
        d.date = vmlinuz.exists() ? QLocale{}.toString(vmlinuz.lastModified().date(), QStringLiteral("dd.MM.yyyy")) : QString();
    }
    if (d.signer.isEmpty() && !d.module.isEmpty()) {
        d.signer = tr7("Not digitally signed");
    }
    Q_UNUSED(vendor)
    return d;
}

/** "Hardware Ids" like Windows from a Linux modalias (pci:v000010DEd000024DD... -> PCI\VEN_10DE&DEV_24DD...) */
QStringList hardwareIds(const QString &sys)
{
    QStringList ids;
    const QString alias = readFile(sys + QStringLiteral("/modalias"));
    static const QRegularExpression pci(QStringLiteral("^pci:v0000([0-9A-F]{4})d0000([0-9A-F]{4})sv0000([0-9A-F]{4})sd0000([0-9A-F]{4})bc([0-9A-F]{2})sc([0-9A-F]{2})i([0-9A-F]{2})"),
                                        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression usb(QStringLiteral("^usb:v([0-9A-F]{4})p([0-9A-F]{4})d([0-9A-F]{4})"), QRegularExpression::CaseInsensitiveOption);
    auto m = pci.match(alias);
    if (m.hasMatch()) {
        const QString ven = m.captured(1).toUpper(), dev = m.captured(2).toUpper(), sub = m.captured(4).toUpper() + m.captured(3).toUpper();
        const QString rev = readFile(sys + QStringLiteral("/revision")).mid(2).toUpper();
        ids << QStringLiteral("PCI\\VEN_%1&DEV_%2&SUBSYS_%3&REV_%4").arg(ven, dev, sub, rev) << QStringLiteral("PCI\\VEN_%1&DEV_%2&SUBSYS_%3").arg(ven, dev, sub)
            << QStringLiteral("PCI\\VEN_%1&DEV_%2&CC_%3%4%5").arg(ven, dev, m.captured(5).toUpper(), m.captured(6).toUpper(), m.captured(7).toUpper())
            << QStringLiteral("PCI\\VEN_%1&DEV_%2").arg(ven, dev);
    }
    m = usb.match(alias);
    if (m.hasMatch()) {
        ids << QStringLiteral("USB\\VID_%1&PID_%2&REV_%3").arg(m.captured(1).toUpper(), m.captured(2).toUpper(), m.captured(3).toUpper())
            << QStringLiteral("USB\\VID_%1&PID_%2").arg(m.captured(1).toUpper(), m.captured(2).toUpper());
    }
    if (!alias.isEmpty()) {
        ids << alias;
    }
    return ids;
}

/** "Driver File Details" (devmgr.dll 110) */
void driverFileDetails(QWidget *parent, const QString &name, const QIcon &icon, const DriverInfo &d)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Driver File Details"));
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dlg.setFixedSize(dlu(0, 0, 224, 250).size());
    auto *ic = place(&dlg, new QLabel, dlu(7, 7, 20, 20));
    ic->setPixmap(icon.pixmap(32, 32));
    plain(&dlg, name, dlu(37, 9, 180, 16));
    plain(&dlg, tr7("Driver files:"), dlu(7, 34, 210, 8));
    auto *files = place(&dlg, new QListWidget, dlu(7, 45, 210, 90));
    if (!d.file.isEmpty()) {
        files->addItem(d.file);
    }
    files->setCurrentRow(0);
    const QList<QPair<QString, QString>> rows{{tr7("Provider:"), d.provider},
                                              {tr7("File version:"), d.version},
                                              {tr7("Copyright:"), d.license},
                                              {tr7("Digital Signer:"), d.signer}};
    int y = 145;
    for (const auto &r : rows) {
        plain(&dlg, r.first, dlu(7, y, 64, 8));
        plain(&dlg, r.second.isEmpty() ? tr7("Not available") : r.second, dlu(74, y, 143, 16));
        y += 18;
    }
    auto *ok = place(&dlg, new QPushButton(QStringLiteral("OK")), dlu(167, 229, 50, 14));
    ok->setDefault(true);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

void showProperties(QWidget *parent, const QString &name, const QIcon &icon, const QVariantMap &details)
{
    const QString sys = details.value(QStringLiteral("_sys")).toString();
    const DriverInfo drv = driverInfo(sys, details.value(tr7("Driver")).toString(), details.value(tr7("Manufacturer")).toString());

    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("%1 Properties").arg(name));
    dlg.setWindowIcon(icon);
    dlg.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    const QSize page = dlu(0, 0, 252, 218).size();
    dlg.setFixedSize(page.width() + 24, page.height() + 76);
    auto *v = new QVBoxLayout(&dlg);
    v->setContentsMargins(8, 8, 8, 8);
    auto *tabs = new QTabWidget(&dlg);

    auto header = [&](QWidget *p) {
        auto *ic = place(p, new QLabel, dlu(7, 7, 20, 20));
        ic->setPixmap(icon.pixmap(32, 32));
        plain(p, name, dlu(37, 9, 208, 16));
    };

    // General (103)
    auto *general = new QWidget;
    header(general);
    const QList<QPair<QString, QString>> info{{tr7("Device type:"), details.value(tr7("Device type")).toString()},
                                              {tr7("Manufacturer:"), details.value(tr7("Manufacturer")).toString()},
                                              {tr7("Location:"), details.value(tr7("Location")).toString()}};
    int y = 36;
    for (const auto &r : info) {
        plain(general, r.first, dlu(37, y, 70, 8));
        plain(general, r.second.isEmpty() ? tr7("Unknown") : r.second, dlu(108, y, 138, y == 64 ? 16 : 12));
        y += 14;
    }
    auto *statusBox = place(general, new QGroupBox(tr7("Device status")), dlu(7, 85, 238, 108));
    auto *status = place(statusBox, new QPlainTextEdit, dlu(7, 11, 224, 90));
    status->setReadOnly(true);
    const bool needsDriver = !sys.isEmpty() && sys.contains(QLatin1String("/bus/pci/")) && drv.driver.isEmpty();
    status->setPlainText(needsDriver ? tr7("The drivers for this device are not installed. (Code 28)\n\nThere is no driver selected for the device information set or element.")
                                     : tr7("This device is working properly."));
    tabs->addTab(general, tr7("General"));

    // Driver (106)
    auto *driver = new QWidget;
    header(driver);
    const QList<QPair<QString, QString>> dinfo{{tr7("Provider:"), drv.provider},
                                               {tr7("Driver Date:"), drv.date},
                                               {tr7("Driver Version:"), drv.version},
                                               {tr7("Digital Signer:"), drv.signer}};
    y = 36;
    for (const auto &r : dinfo) {
        plain(driver, r.first, dlu(37, y, 76, 8));
        plain(driver, r.second.isEmpty() ? tr7("Not available") : r.second, dlu(114, y, 132, y == 78 ? 16 : 12));
        y += 14;
    }
    struct Action {
        QString button, text;
        int y;
        bool enabled;
    };
    const QList<Action> actions{{tr7("Driver Details"), tr7("To view details about the driver files."), 100, !drv.file.isEmpty()},
                                {tr7("Update Driver..."), tr7("To update the driver software for this device."), 123, true},
                                {tr7("Roll Back Driver"), tr7("If the device fails after updating the driver, roll back to the previously installed driver."), 146, false},
                                {tr7("Disable"), tr7("Disables the selected device."), 171, false},
                                {tr7("Uninstall"), tr7("To uninstall the driver (Advanced)."), 196, false}};
    for (const Action &a : actions) {
        auto *b = place(driver, new QPushButton(a.button), dlu(7, a.y, 76, 15));
        b->setEnabled(a.enabled);
        plain(driver, a.text, dlu(91, a.y + (a.y == 146 ? 0 : 4), 154, 22));
        if (a.y == 100) {
            QObject::connect(b, &QPushButton::clicked, &dlg, [&dlg, name, icon, drv] {
                driverFileDetails(&dlg, name, icon, drv);
            });
        } else if (a.y == 123) {
            QObject::connect(b, &QPushButton::clicked, &dlg, [] {
                QProcess::startDetached(QDir::homePath() + QStringLiteral("/.local/bin/seven-control"), {QStringLiteral("--windows-update")});
            });
        }
    }
    tabs->addTab(driver, tr7("Driver"));

    // Details (372)
    auto *more = new QWidget;
    header(more);
    plain(more, tr7("Property"), dlu(7, 35, 239, 8));
    auto *prop = place(more, new QComboBox, dlu(7, 46, 239, 14));
    plain(more, tr7("Value"), dlu(7, 67, 239, 8));
    auto *value = place(more, new QListWidget, dlu(7, 78, 239, 135));
    QList<QPair<QString, QStringList>> props;
    props.append({tr7("Device description"), {name}});
    if (!sys.isEmpty()) {
        props.append({tr7("Device Instance Path"), {QFileInfo(sys).canonicalFilePath().remove(QStringLiteral("/sys"))}});
    }
    const QStringList hw = sys.isEmpty() ? QStringList() : hardwareIds(sys);
    if (!hw.isEmpty()) {
        props.append({tr7("Hardware Ids"), hw});
    }
    props.append({tr7("Class"), {details.value(tr7("Device type")).toString()}});
    if (!drv.driver.isEmpty()) {
        props.append({tr7("Service"), {drv.driver}});
    }
    if (!drv.module.isEmpty()) {
        props.append({tr7("Kernel module"), {drv.module}});
    }
    props.append({tr7("Driver provider"), {drv.provider}});
    props.append({tr7("Driver date"), {drv.date}});
    props.append({tr7("Driver version"), {drv.version}});
    if (!drv.description.isEmpty()) {
        props.append({tr7("Driver description"), {drv.description}});
    }
    props.append({tr7("Location information"), {details.value(tr7("Location")).toString()}});
    if (!sys.isEmpty()) {
        const QString canon = QFileInfo(sys).canonicalFilePath();
        props.append({tr7("Parent"), {canon.section(QLatin1Char('/'), -2, -2)}});
        const QString subsystem = QFileInfo(QFileInfo(sys + QStringLiteral("/subsystem")).symLinkTarget()).fileName();
        if (!subsystem.isEmpty()) {
            props.append({tr7("Bus"), {subsystem}});
        }
    }
    for (auto it = details.cbegin(); it != details.cend(); ++it) {
        static const QStringList shown{QStringLiteral("_sys"), tr7("Device type"), tr7("Manufacturer"), tr7("Location"), tr7("Driver")};
        if (!shown.contains(it.key()) && !it.value().toString().isEmpty()) {
            props.append({it.key(), {it.value().toString()}});
        }
    }
    for (const auto &pr : props) {
        if (!pr.second.join(QString()).trimmed().isEmpty()) {
            prop->addItem(pr.first, pr.second);
        }
    }
    QObject::connect(prop, &QComboBox::currentIndexChanged, value, [prop, value] {
        value->clear();
        value->addItems(prop->currentData().toStringList());
    });
    value->addItems(prop->currentData().toStringList());
    tabs->addTab(more, tr7("Driver Details"));

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
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    // one font for the whole sheet, the one of the labels (tabs, frames and lists come out bigger otherwise)
    const QFont f = QApplication::font("QLabel");
    for (QWidget *w : dlg.findChildren<QWidget *>()) {
        w->setFont(f);
    }
    // test: WIN7_GRAB=<prefix> saves every tab and closes
    if (!qEnvironmentVariableIsEmpty("WIN7_GRAB")) {
        for (int i = 0; i < tabs->count(); ++i) {
            QTimer::singleShot(800 + i * 400, &dlg, [&dlg, tabs, i] {
                tabs->setCurrentIndex(i);
                QTimer::singleShot(150, &dlg, [&dlg, i] {
                    dlg.grab().save(qEnvironmentVariable("WIN7_GRAB") + QString::number(i) + QStringLiteral(".png"));
                });
            });
        }
        QTimer::singleShot(800 + tabs->count() * 400 + 300, &dlg, &QDialog::accept);
    }
    dlg.exec();
}

/* ---------------- main window ---------------- */

QIcon win7Icon(const QString &name)
{
    const QPixmap pm = QIcon::fromTheme(name).pixmap(16, 16);
    QIcon icon;
    icon.addPixmap(pm, QIcon::Normal);
    icon.addPixmap(pm, QIcon::Disabled);
    icon.addPixmap(pm, QIcon::Active);
    return icon;
}


// The device tree on its own: used by the Device Manager window and embedded in Computer Management
// (Windows 7 shows the same devmgr snap-in in both).
class DeviceTree : public QWidget
{
public:
    explicit DeviceTree(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        m_model = new QStandardItemModel(this);
        m_tree = new Win7Tree(this);
        m_tree->setModel(m_model);
        m_tree->setHeaderHidden(true);
        m_tree->setIndentation(20);
        m_tree->setItemDelegate(new Win7TreeDelegate(m_tree));
        m_tree->setIconSize(QSize(16, 16));
        m_tree->setUniformRowHeights(true);
        m_tree->setAnimated(false);
        m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_tree->setFrameShape(QFrame::NoFrame);
        m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
        // Compact rows and the Windows 7 blue hover / selection (the theme would draw them yellow)
        m_tree->setStyleSheet(Win7States::itemViewSheet());
        // MMC's panes have the classic sunken client edge (measured on Windows 7): grey and dark lines
        // at the top and left, light grey and white at the bottom and right
        auto *edge = new SunkenEdge(this);
        auto *edgeLayout = new QVBoxLayout(edge);
        edgeLayout->setContentsMargins(2, 2, 2, 2);
        edgeLayout->addWidget(m_tree);
        lay->addWidget(edge);

        m_scan = new QAction(win7Icon(QStringLiteral("seven-devmgr-scan")), tr7("Scan for hardware changes"), this);
        m_props = new QAction(win7Icon(QStringLiteral("seven-devmgr-props")), tr7("Properties"), this);
        connect(m_scan, &QAction::triggered, this, &DeviceTree::reload);
        connect(m_props, &QAction::triggered, this, [this]() {
            openProperties(m_tree->currentIndex());
        });
        connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &idx) {
            if (!m_model->hasChildren(idx)) {
                openProperties(idx);
            }
        });
        connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            const QModelIndex idx = m_tree->indexAt(pos);
            QMenu menu(this);
            if (idx.isValid() && !m_model->hasChildren(idx)) {
                m_tree->setCurrentIndex(idx);
                menu.addAction(m_scan);
                menu.addSeparator();
                QAction *p = menu.addAction(tr7("Properties"), this, [this, idx]() {
                    openProperties(idx);
                });
                QFont f = p->font();
                f.setBold(true);
                p->setFont(f);
            } else {
                menu.addAction(m_scan);
            }
            menu.exec(m_tree->viewport()->mapToGlobal(pos));
        });
        connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &idx) {
            m_props->setEnabled(idx.isValid() && !m_model->hasChildren(idx));
        });
        reload();
    }

    QAction *scanAction() const { return m_scan; }
    QAction *propertiesAction() const { return m_props; }
    QTreeView *view() const { return m_tree; }
    void setShowHidden(bool on)
    {
        m_showHidden = on;
        reload();
    }

    void reload()
    {
        m_model->clear();
        auto *root = new QStandardItem(QIcon::fromTheme(QStringLiteral("seven-devmgr-root")), QSysInfo::machineHostName().toUpper()); // Windows 7 shows the computer name in capitals
        m_model->appendRow(root);

        const QList<Device> devices = scan();
        // alphabetical like Windows 7: Latin names (DVD, IDE) first, then Cyrillic
        QList<Category> sorted = categories();
        QCollator coll(QLocale(QLocale::English));
        std::sort(sorted.begin(), sorted.end(), [&coll](const Category &a, const Category &b) {
            return coll.compare(a.title, b.title) < 0;
        });
        for (const Category &cat : std::as_const(sorted)) {
            QList<Device> members;
            for (const Device &d : devices) {
                if (d.category == cat.key) {
                    members.append(d);
                }
            }
            // "System devices" are the many PCI bridges; Windows lists them too, only non-empty groups are shown
            if (members.isEmpty()) {
                continue;
            }
            if (!m_showHidden && cat.key == QLatin1String("system") && members.size() > 40) {
                members = members.mid(0, 40);
            }
            std::sort(members.begin(), members.end(), [&coll](const Device &a, const Device &b) {
                return coll.compare(a.name, b.name) < 0;
            });
            auto *group = new QStandardItem(QIcon::fromTheme(cat.icon), cat.title);
            for (const Device &d : members) {
                auto *item = new QStandardItem(QIcon::fromTheme(cat.icon), d.name);
                item->setData(d.details, DetailsRole);
                group->appendRow(item);
            }
            root->appendRow(group);
        }
        m_tree->expand(root->index());
        m_props->setEnabled(false);
    }

private:
    void openProperties(const QModelIndex &idx)
    {
        if (!idx.isValid() || m_model->hasChildren(idx)) {
            return;
        }
        showProperties(window(), idx.data().toString(), idx.data(Qt::DecorationRole).value<QIcon>(), idx.data(DetailsRole).toMap());
    }

    QStandardItemModel *m_model;
    Win7Tree *m_tree;
    QAction *m_scan;
    QAction *m_props;
    bool m_showHidden = false;
};
}
