/*
 * "Devices and Printers" of Windows 7: the device icons in groups, the details pane, the context
 * menus, "Properties: <device>" (General / Hardware) and the "Add a device" wizard.
 *
 *   computer                 -> DMI (/sys/class/dmi)
 *   monitors                 -> connected DRM connectors and their EDID
 *   printers                 -> CUPS (lpstat, lpoptions, lpadmin)
 *   USB devices              -> /sys/bus/usb (hubs, Bluetooth adapters and printers are skipped)
 *   Bluetooth devices        -> BlueZ through bluetoothctl (paired ones; the wizard scans and pairs)
 * The Windows disk (internal NVMe) is never listed: only USB disks are.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "win7chrome.h"
#include "win7addprinter.h"

#include <QDialog>
#include <QGroupBox>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>

namespace Win7Dev
{
enum {
    DevicesView = 40,
};

inline QString run(const QString &cmd, const QStringList &args, int timeout = 3000)
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    p.setProcessEnvironment(env);
    p.start(cmd, args);
    if (!p.waitForFinished(timeout)) {
        p.kill();
        p.waitForFinished(500);
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

inline QString sysValue(const QString &path)
{
    return readFile(path).trimmed();
}

struct Function {
    QString name;
    QString type;
};

struct Device {
    enum Kind { Computer, Monitor, Mouse, Keyboard, Audio, Disk, Printer, Phone, Gamepad, Camera, Other };
    Kind kind = Other;
    QString key;
    QString name;
    QString icon;
    QString manufacturer;
    QString model;
    QString modelNumber;
    QString category;
    QString description;
    QString location;
    QString state; // "Стан:" in the details pane
    QString printer; // CUPS queue
    bool isDefault = false;
    QString bt; // Bluetooth address
    bool connected = false;
    QString block; // /dev/sdX of a USB disk
    QList<Function> functions;
};

/* ---------------- what is connected ---------------- */

inline QList<Function> computerFunctions()
{
    QList<Function> f;
    for (const QString &block : readFile(QStringLiteral("/proc/cpuinfo")).split(QStringLiteral("\n\n"), Qt::SkipEmptyParts)) {
        const QString model = keyValue(block, QStringLiteral("model name"), QLatin1Char(':'));
        if (!model.isEmpty()) {
            f.append({model, QStringLiteral("Процесори")});
        }
    }
    static const QRegularExpression quoted(QStringLiteral("\"([^\"]*)\""));
    for (const QString &line : run(QStringLiteral("lspci"), {QStringLiteral("-mm")}).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        QStringList q;
        for (auto it = quoted.globalMatch(line); it.hasNext();) {
            q << it.next().captured(1);
        }
        if (q.size() < 3) {
            continue;
        }
        QString device = q.at(2);
        device.remove(QRegularExpression(QStringLiteral("\\s*\\[[^\\]]*\\]$")));
        QString vendor = q.at(1).section(QLatin1Char(','), 0, 0).remove(QStringLiteral(" Corporation"));
        const QString name = vendor + QLatin1Char(' ') + device;
        if (q.at(0) == QLatin1String("VGA compatible controller") || q.at(0) == QLatin1String("3D controller") || q.at(0) == QLatin1String("Display controller")) {
            f.append({name, QStringLiteral("Відеоадаптери")});
        } else if (q.at(0) == QLatin1String("Network controller") || q.at(0) == QLatin1String("Ethernet controller")) {
            f.append({name, QStringLiteral("Мережні адаптери")});
        } else if (q.at(0) == QLatin1String("Audio device") || q.at(0) == QLatin1String("Multimedia audio controller")) {
            f.append({name, QStringLiteral("Звукові, відео та ігрові пристрої")});
        } else if (q.at(0) == QLatin1String("USB controller")) {
            f.append({name, QStringLiteral("Контролери USB")});
        }
    }
    return f;
}

inline Device computer()
{
    Device d;
    d.kind = Device::Computer;
    d.key = QStringLiteral("computer");
    d.name = QHostInfo::localHostName();
    const QString dmi = QStringLiteral("/sys/class/dmi/id/");
    d.manufacturer = sysValue(dmi + QStringLiteral("sys_vendor"));
    const QString product = sysValue(dmi + QStringLiteral("product_name"));
    const QString version = sysValue(dmi + QStringLiteral("product_version"));
    d.model = version.isEmpty() || version == QLatin1String("None") ? product : version;
    d.modelNumber = d.model == product ? QString() : product;
    const int chassis = sysValue(dmi + QStringLiteral("chassis_type")).toInt();
    const bool laptop = QList<int>{8, 9, 10, 11, 14, 30, 31, 32}.contains(chassis);
    d.icon = laptop ? QStringLiteral("seven-dev-laptop") : QStringLiteral("seven-dev-pc");
    d.category = QStringLiteral("Комп'ютер");
    return d;
}

inline QString pnpVendor(const QString &id)
{
    static const QHash<QString, QString> names{
        {QStringLiteral("ACR"), QStringLiteral("Acer")},         {QStringLiteral("AOC"), QStringLiteral("AOC")},
        {QStringLiteral("AUO"), QStringLiteral("AU Optronics")}, {QStringLiteral("BNQ"), QStringLiteral("BenQ")},
        {QStringLiteral("BOE"), QStringLiteral("BOE")},          {QStringLiteral("CMN"), QStringLiteral("Chimei Innolux")},
        {QStringLiteral("DEL"), QStringLiteral("Dell")},         {QStringLiteral("GSM"), QStringLiteral("LG Electronics")},
        {QStringLiteral("HWP"), QStringLiteral("HP")},           {QStringLiteral("LEN"), QStringLiteral("Lenovo")},
        {QStringLiteral("LGD"), QStringLiteral("LG Display")},   {QStringLiteral("MSI"), QStringLiteral("MSI")},
        {QStringLiteral("PHL"), QStringLiteral("Philips")},      {QStringLiteral("SAM"), QStringLiteral("Samsung")},
        {QStringLiteral("SDC"), QStringLiteral("Samsung Display")}, {QStringLiteral("SHP"), QStringLiteral("Sharp")},
        {QStringLiteral("VSC"), QStringLiteral("ViewSonic")},    {QStringLiteral("AUS"), QStringLiteral("ASUS")},
        {QStringLiteral("GBT"), QStringLiteral("Gigabyte")},     {QStringLiteral("XMI"), QStringLiteral("Xiaomi")},
        {QStringLiteral("IVM"), QStringLiteral("Iiyama")},       {QStringLiteral("HKC"), QStringLiteral("HKC")},
    };
    return names.value(id, id);
}

/** Connected outputs and their EDID: from X (works with the NVIDIA driver too), else from DRM */
inline QList<QPair<QString, QByteArray>> connectedOutputs()
{
    QList<QPair<QString, QByteArray>> outs;
    QString output;
    bool inEdid = false;
    for (const QString &line : run(QStringLiteral("xrandr"), {QStringLiteral("--verbose")}).split(QLatin1Char('\n'))) {
        if (!line.startsWith(QLatin1Char('\t')) && !line.startsWith(QLatin1Char(' '))) {
            inEdid = false;
            output = line.contains(QLatin1String(" connected")) ? line.section(QLatin1Char(' '), 0, 0) : QString();
            if (!output.isEmpty()) {
                outs.append({output, {}});
            }
        } else if (!output.isEmpty() && line.trimmed() == QLatin1String("EDID:")) {
            inEdid = true;
        } else if (inEdid) {
            static const QRegularExpression hex(QStringLiteral("^[0-9a-f]+$"));
            if (hex.match(line.trimmed()).hasMatch()) {
                outs.last().second += QByteArray::fromHex(line.trimmed().toLatin1());
            } else {
                inEdid = false;
            }
        }
    }
    if (!outs.isEmpty()) {
        return outs;
    }
    const QDir drm(QStringLiteral("/sys/class/drm"));
    for (const QString &c : drm.entryList({QStringLiteral("card*-*")}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name)) {
        if (sysValue(drm.filePath(c + QStringLiteral("/status"))) == QLatin1String("connected")) {
            QFile f(drm.filePath(c + QStringLiteral("/edid")));
            outs.append({c.section(QLatin1Char('-'), 1), f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray()});
        }
    }
    return outs;
}

inline QList<Device> monitors()
{
    QList<Device> list;
    for (const auto &out : connectedOutputs()) {
        const QByteArray &e = out.second;
        Device d;
        d.kind = Device::Monitor;
        d.key = QStringLiteral("monitor:") + out.first;
        d.icon = QStringLiteral("seven-dev-monitor");
        d.category = QStringLiteral("Монітор");
        d.location = out.first;
        QString edidName;
        QString text;
        if (e.size() >= 128) {
            const auto b = [&e](int i) {
                return quint8(e.at(i));
            };
            const int w = (b(8) << 8) | b(9);
            const QString pnp = QString(QChar('A' + ((w >> 10) & 31) - 1)) + QChar('A' + ((w >> 5) & 31) - 1) + QChar('A' + (w & 31) - 1);
            d.manufacturer = pnpVendor(pnp);
            d.modelNumber = pnp + QStringLiteral("%1").arg(b(10) | (b(11) << 8), 4, 16, QLatin1Char('0')).toUpper();
            for (int i : {54, 72, 90, 108}) {
                if (b(i) == 0 && b(i + 1) == 0 && (b(i + 3) == 0xfc || b(i + 3) == 0xfe)) {
                    const QString t = QString::fromLatin1(e.mid(i + 5, 13)).section(QLatin1Char('\n'), 0, 0).trimmed();
                    (b(i + 3) == 0xfc ? edidName : text) = t;
                }
            }
        }
        // like Windows: a monitor without a name in its EDID is a "Generic PnP Monitor"
        d.name = !edidName.isEmpty() ? edidName : e.isEmpty() ? QStringLiteral("Універсальний монітор не PnP") : QStringLiteral("Універсальний монітор PnP");
        d.model = edidName.isEmpty() ? text : edidName;
        d.functions = {{e.isEmpty() ? QStringLiteral("Універсальний монітор не PnP") : QStringLiteral("Універсальний монітор PnP"), QStringLiteral("Монітори")}};
        list.append(d);
    }
    return list;
}

inline QHash<QString, QString> lpoptions(const QString &printer)
{
    QHash<QString, QString> h;
    static const QRegularExpression re(QStringLiteral("([\\w-]+)(?:=(?:'([^']*)'|(\\S*)))?"));
    for (auto it = re.globalMatch(run(QStringLiteral("lpoptions"), {QStringLiteral("-p"), printer})); it.hasNext();) {
        const auto m = it.next();
        h.insert(m.captured(1), m.captured(2).isEmpty() ? m.captured(3) : m.captured(2));
    }
    return h;
}

inline QString defaultPrinter()
{
    const QString d = run(QStringLiteral("lpstat"), {QStringLiteral("-d")}).trimmed();
    return d.contains(QLatin1Char(':')) ? d.section(QLatin1Char(':'), 1).trimmed() : QString();
}

inline QList<Device> printers()
{
    QList<Device> list;
    const QString def = defaultPrinter();
    static const QRegularExpression mfp(QStringLiteral("\\b(L[3-6]\\d{3}|MFP|All-in-One|DCP|MFC|ET-\\d+|XP-\\d+|SCX|DeskJet|Pixma|MG\\d+)"), QRegularExpression::CaseInsensitiveOption);
    for (const QString &name : run(QStringLiteral("lpstat"), {QStringLiteral("-e")}).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const auto o = lpoptions(name);
        Device d;
        d.kind = Device::Printer;
        d.key = QStringLiteral("printer:") + name;
        d.printer = name;
        d.isDefault = name == def;
        d.name = o.value(QStringLiteral("printer-info"), name);
        if (d.name.isEmpty()) {
            d.name = name;
        }
        const QString uri = o.value(QStringLiteral("device-uri"));
        d.model = d.name;
        if (uri.startsWith(QLatin1String("usb://")) || uri.startsWith(QLatin1String("ipp://")) && uri.contains(QLatin1String("/printers/")) == false) {
            d.manufacturer = QUrl::fromPercentEncoding(uri.section(QLatin1Char('/'), 2, 2).toUtf8());
        }
        if (d.manufacturer.isEmpty()) {
            d.manufacturer = d.name.section(QLatin1Char(' '), 0, 0);
        }
        d.category = QStringLiteral("Принтер");
        d.description = o.value(QStringLiteral("printer-location"));
        d.location = uri.startsWith(QLatin1String("usb:")) ? QStringLiteral("USB") : QUrl(uri).host();
        d.icon = mfp.match(d.name).hasMatch() ? QStringLiteral("seven-dev-mfp") : QStringLiteral("seven-dev-printer");
        const int jobs = run(QStringLiteral("lpstat"), {QStringLiteral("-o"), name}).split(QLatin1Char('\n'), Qt::SkipEmptyParts).size();
        d.state = QStringLiteral("Документів у черзі: %1").arg(jobs);
        d.functions = {{d.name, QStringLiteral("Черги друку")}};
        list.append(d);
    }
    return list;
}

inline QList<Device> usbDevices()
{
    QList<Device> list;
    const QDir usb(QStringLiteral("/sys/bus/usb/devices"));
    const QStringList all = usb.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
    for (const QString &dev : all) {
        if (dev.contains(QLatin1Char(':')) || dev.startsWith(QLatin1String("usb"))) {
            continue;
        }
        const QString base = usb.filePath(dev) + QLatin1Char('/');
        if (sysValue(base + QStringLiteral("bDeviceClass")) == QLatin1String("09")) {
            continue; // hub
        }
        QStringList classes;
        bool mouse = false;
        bool keyboard = false;
        for (const QString &intf : all) {
            if (!intf.startsWith(dev + QLatin1Char(':'))) {
                continue;
            }
            const QString ib = usb.filePath(intf) + QLatin1Char('/');
            const QString cls = sysValue(ib + QStringLiteral("bInterfaceClass"));
            classes << cls;
            if (cls == QLatin1String("03")) {
                const QString proto = sysValue(ib + QStringLiteral("bInterfaceProtocol"));
                mouse = mouse || proto == QLatin1String("02");
                keyboard = keyboard || proto == QLatin1String("01");
            }
        }
        if (classes.contains(QStringLiteral("e0")) || sysValue(base + QStringLiteral("bDeviceClass")) == QLatin1String("e0") || classes.contains(QStringLiteral("07"))) {
            continue; // Bluetooth adapter, printers come from CUPS
        }
        Device d;
        d.key = QStringLiteral("usb:") + dev;
        d.name = sysValue(base + QStringLiteral("product"));
        d.manufacturer = sysValue(base + QStringLiteral("manufacturer"));
        if (d.name.isEmpty()) {
            d.name = QStringLiteral("Пристрій USB");
        }
        d.model = d.name;
        d.location = QStringLiteral("Шина USB %1, порт %2").arg(sysValue(base + QStringLiteral("busnum")), sysValue(base + QStringLiteral("devpath")));
        const QString hid = QStringLiteral("Пристрої HID (Human Interface Devices)");
        if (classes.contains(QStringLiteral("08"))) {
            d.kind = Device::Disk;
            d.category = QStringLiteral("Пристрій зберігання даних");
            const QString real = QFileInfo(usb.filePath(dev)).canonicalFilePath();
            bool removable = true;
            for (const QString &blk : QDir(QStringLiteral("/sys/block")).entryList({QStringLiteral("sd*")}, QDir::Dirs | QDir::System)) {
                if (QFileInfo(QStringLiteral("/sys/block/") + blk).canonicalFilePath().startsWith(real + QLatin1Char('/'))) {
                    d.block = QStringLiteral("/dev/") + blk;
                    removable = sysValue(QStringLiteral("/sys/block/%1/removable").arg(blk)) == QLatin1String("1");
                }
            }
            d.icon = removable ? QStringLiteral("seven-dev-usbdisk") : QStringLiteral("seven-dev-disk");
            d.functions = {{d.name + QStringLiteral(" USB Device"), QStringLiteral("Дискові пристрої")}, {QStringLiteral("Запам'ятовуючий пристрій для USB"), QStringLiteral("Контролери USB")}};
        } else if (mouse) {
            d.kind = Device::Mouse;
            d.icon = QStringLiteral("seven-dev-mouse");
            d.category = QStringLiteral("Миша");
            d.functions = {{QStringLiteral("HID-сумісна миша"), QStringLiteral("Миші та інші вказівні пристрої")}, {QStringLiteral("USB-пристрій введення"), hid}};
        } else if (keyboard) {
            d.kind = Device::Keyboard;
            d.icon = QStringLiteral("seven-dev-keyboard");
            d.category = QStringLiteral("Клавіатура");
            d.functions = {{QStringLiteral("Клавіатура HID"), QStringLiteral("Клавіатури")}, {QStringLiteral("USB-пристрій введення"), hid}};
        } else if (classes.contains(QStringLiteral("0e"))) {
            d.kind = Device::Camera;
            d.icon = QStringLiteral("seven-dev-webcam");
            d.category = QStringLiteral("Веб-камера");
            d.functions = {{d.name, QStringLiteral("Пристрої обробки зображень")}};
        } else if (classes.contains(QStringLiteral("06"))) {
            d.kind = Device::Camera;
            d.icon = QStringLiteral("seven-dev-camera");
            d.category = QStringLiteral("Камера");
            d.functions = {{d.name, QStringLiteral("Переносні пристрої")}};
        } else if (classes.contains(QStringLiteral("01"))) {
            d.kind = Device::Audio;
            d.icon = QStringLiteral("seven-dev-headset");
            d.category = QStringLiteral("Аудіопристрій");
            d.functions = {{d.name, QStringLiteral("Звукові, відео та ігрові пристрої")}};
        } else if (classes.contains(QStringLiteral("03"))) {
            d.kind = Device::Gamepad;
            d.icon = QStringLiteral("seven-dev-gamepad");
            d.category = QStringLiteral("Пристрій введення");
            d.functions = {{QStringLiteral("USB-пристрій введення"), hid}};
        } else {
            d.icon = QStringLiteral("seven-dev-generic");
            d.category = QStringLiteral("Пристрій USB");
            d.functions = {{d.name, QStringLiteral("Контролери USB")}};
        }
        list.append(d);
    }
    return list;
}

inline QStringList pairedAddresses()
{
    QString out = run(QStringLiteral("bluetoothctl"), {QStringLiteral("devices"), QStringLiteral("Paired")});
    if (!out.contains(QLatin1String("Device "))) {
        out = run(QStringLiteral("bluetoothctl"), {QStringLiteral("paired-devices")});
    }
    QStringList a;
    for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (line.startsWith(QLatin1String("Device "))) {
            a << line.section(QLatin1Char(' '), 1, 1);
        }
    }
    return a;
}

/** "Icon:" of bluetoothctl info -> our kind, icon and category */
inline void classifyBluetooth(Device &d, const QString &icon)
{
    const QString hid = QStringLiteral("Пристрої HID (Human Interface Devices)");
    const QString audio = QStringLiteral("Звукові, відео та ігрові пристрої");
    if (icon == QLatin1String("input-mouse") || icon == QLatin1String("input-tablet")) {
        d.kind = Device::Mouse;
        d.icon = icon == QLatin1String("input-tablet") ? QStringLiteral("seven-dev-tablet") : QStringLiteral("seven-dev-mouse");
        d.category = QStringLiteral("Миша");
        d.functions = {{QStringLiteral("HID-сумісна миша"), QStringLiteral("Миші та інші вказівні пристрої")}, {QStringLiteral("Пристрій введення Bluetooth (HID)"), hid}};
    } else if (icon == QLatin1String("input-keyboard")) {
        d.kind = Device::Keyboard;
        d.icon = QStringLiteral("seven-dev-keyboard");
        d.category = QStringLiteral("Клавіатура");
        d.functions = {{QStringLiteral("Клавіатура HID"), QStringLiteral("Клавіатури")}, {QStringLiteral("Пристрій введення Bluetooth (HID)"), hid}};
    } else if (icon == QLatin1String("input-gaming")) {
        d.kind = Device::Gamepad;
        d.icon = QStringLiteral("seven-dev-gamepad");
        d.category = QStringLiteral("Ігровий контролер");
        d.functions = {{QStringLiteral("Пристрій введення Bluetooth (HID)"), hid}};
    } else if (icon == QLatin1String("audio-headset")) {
        d.kind = Device::Audio;
        d.icon = QStringLiteral("seven-dev-btheadset");
        d.category = QStringLiteral("Гарнітура");
        d.functions = {{d.name + QStringLiteral(" Stereo"), audio}, {d.name + QStringLiteral(" Hands-Free"), audio}};
    } else if (icon == QLatin1String("audio-headphones")) {
        d.kind = Device::Audio;
        d.icon = QStringLiteral("seven-dev-headphones");
        d.category = QStringLiteral("Навушники");
        d.functions = {{d.name + QStringLiteral(" Stereo"), audio}};
    } else if (icon == QLatin1String("audio-card")) {
        d.kind = Device::Audio;
        d.icon = QStringLiteral("seven-dev-speakers");
        d.category = QStringLiteral("Динаміки");
        d.functions = {{d.name + QStringLiteral(" Stereo"), audio}};
    } else if (icon == QLatin1String("phone")) {
        d.kind = Device::Phone;
        d.icon = QStringLiteral("seven-dev-phone");
        d.category = QStringLiteral("Телефон");
        d.functions = {{d.name, QStringLiteral("Пристрої Bluetooth")}};
    } else if (icon == QLatin1String("computer")) {
        d.kind = Device::Other;
        d.icon = QStringLiteral("seven-dev-laptop");
        d.category = QStringLiteral("Комп'ютер");
        d.functions = {{d.name, QStringLiteral("Пристрої Bluetooth")}};
    } else {
        d.kind = Device::Other;
        d.icon = QStringLiteral("seven-dev-bluetooth");
        d.category = QStringLiteral("Пристрій Bluetooth");
        d.functions = {{d.name, QStringLiteral("Пристрої Bluetooth")}};
    }
}

inline QString bluetoothVendor(const QString &info)
{
    static const QHash<QString, QString> ids{
        {QStringLiteral("046d"), QStringLiteral("Logitech")}, {QStringLiteral("004c"), QStringLiteral("Apple")},
        {QStringLiteral("045e"), QStringLiteral("Microsoft")}, {QStringLiteral("054c"), QStringLiteral("Sony")},
        {QStringLiteral("0075"), QStringLiteral("Samsung")}, {QStringLiteral("05ac"), QStringLiteral("Apple")},
        {QStringLiteral("057e"), QStringLiteral("Nintendo")}, {QStringLiteral("0a12"), QStringLiteral("CSR")},
        {QStringLiteral("00e0"), QStringLiteral("Google")}, {QStringLiteral("0310"), QStringLiteral("SteelSeries")},
    };
    static const QRegularExpression modalias(QStringLiteral("Modalias:\\s*\\w+:v([0-9A-Fa-f]{4})"));
    const auto m = modalias.match(info);
    if (m.hasMatch() && ids.contains(m.captured(1).toLower())) {
        return ids.value(m.captured(1).toLower());
    }
    // a company service UUID names the maker, e.g. "UUID: Logitech International SA (0000fd72-...)"
    static const QRegularExpression company(QStringLiteral("UUID:\\s*([^(\\n]*?(?:Inc\\.?|Ltd\\.?|SA|GmbH|Corporation|Co\\.|AB|LLC|Limited))\\s*\\("));
    const auto c = company.match(info);
    return c.hasMatch() ? c.captured(1).section(QLatin1Char(' '), 0, 0) : QString();
}

inline QList<Device> bluetoothDevices()
{
    QList<Device> list;
    for (const QString &addr : pairedAddresses()) {
        const QString info = run(QStringLiteral("bluetoothctl"), {QStringLiteral("info"), addr});
        Device d;
        d.key = QStringLiteral("bt:") + addr;
        d.bt = addr;
        d.name = keyValue(info, QStringLiteral("Alias"), QLatin1Char(':'));
        if (d.name.isEmpty()) {
            d.name = keyValue(info, QStringLiteral("Name"), QLatin1Char(':'));
        }
        if (d.name.isEmpty()) {
            d.name = addr;
        }
        d.connected = keyValue(info, QStringLiteral("Connected"), QLatin1Char(':')) == QLatin1String("yes");
        d.state = d.connected ? QStringLiteral("Підключено") : QStringLiteral("Не підключено");
        d.manufacturer = bluetoothVendor(info);
        d.model = d.name;
        d.location = QStringLiteral("Bluetooth (%1)").arg(addr);
        classifyBluetooth(d, keyValue(info, QStringLiteral("Icon"), QLatin1Char(':')));
        list.append(d);
    }
    return list;
}

/** Cheap fingerprint of what is plugged in, to refresh the window when something changes */
inline QString signature()
{
    QString s = QDir(QStringLiteral("/sys/bus/usb/devices")).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System).join(QLatin1Char(','));
    for (const auto &o : connectedOutputs()) {
        s += o.first;
    }
    s += run(QStringLiteral("lpstat"), {QStringLiteral("-e"), QStringLiteral("-d"), QStringLiteral("-o")});
    s += run(QStringLiteral("bluetoothctl"), {QStringLiteral("devices"), QStringLiteral("Paired")});
    s += run(QStringLiteral("bluetoothctl"), {QStringLiteral("devices"), QStringLiteral("Connected")});
    return s;
}

/* ---------------- painting ---------------- */

/** Green round tick of the default printer */
inline void paintDefaultTick(QPainter &p, const QRectF &r)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QRadialGradient g(r.center() - QPointF(r.width() * 0.2, r.height() * 0.25), r.width() * 0.75);
    g.setColorAt(0, QColor(0x9a, 0xe6, 0x6e));
    g.setColorAt(0.6, QColor(0x3c, 0xb0, 0x2a));
    g.setColorAt(1, QColor(0x1d, 0x7a, 0x12));
    p.setPen(QPen(QColor(255, 255, 255), 1.5));
    p.setBrush(g);
    p.drawEllipse(r.adjusted(1, 1, -1, -1));
    QPainterPath tick;
    tick.moveTo(r.left() + r.width() * 0.26, r.top() + r.height() * 0.52);
    tick.lineTo(r.left() + r.width() * 0.43, r.top() + r.height() * 0.70);
    tick.lineTo(r.left() + r.width() * 0.75, r.top() + r.height() * 0.32);
    p.setPen(QPen(Qt::white, r.width() * 0.14, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(tick);
    p.restore();
}

inline QPixmap deviceIcon(const Device &d, int size)
{
    QPixmap pm = QIcon::fromTheme(d.icon).pixmap(size, size);
    if (d.isDefault) {
        QPainter p(&pm);
        const qreal s = qMax(14.0, size * 0.27);
        paintDefaultTick(p, QRectF(0, pm.height() / pm.devicePixelRatio() - s, s, s));
    }
    return pm;
}

/** Selection / hover box of the Windows 7 Explorer */
inline void paintSelection(QPainter &p, const QRectF &r, bool selected, bool hot, bool focused = true)
{
    if (!selected && !hot) {
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    if (selected && !focused) {
        g.setColorAt(0, QColor(0xf8, 0xf8, 0xf8));
        g.setColorAt(1, QColor(0xe5, 0xe5, 0xe5));
        p.setPen(QColor(0xd9, 0xd9, 0xd9));
    } else if (selected) {
        g.setColorAt(0, hot ? QColor(0xe8, 0xf2, 0xfe) : QColor(0xdc, 0xeb, 0xfc));
        g.setColorAt(1, hot ? QColor(0xc4, 0xde, 0xfc) : QColor(0xc1, 0xdb, 0xfc));
        p.setPen(hot ? QColor(0x7d, 0xa2, 0xce) : QColor(0x84, 0xac, 0xdd));
    } else {
        g.setColorAt(0, QColor(0xfa, 0xfb, 0xfd));
        g.setColorAt(1, QColor(0xeb, 0xf3, 0xfd));
        p.setPen(QColor(0xb8, 0xd6, 0xfb));
    }
    p.setBrush(g);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    p.restore();
}

/* ---------------- "Properties: <device>" ---------------- */

inline QLabel *greyValue(const QString &text)
{
    auto *l = new QLabel(text.isEmpty() ? QStringLiteral("Недоступно") : text);
    if (text.isEmpty()) {
        l->setStyleSheet(QStringLiteral("color: #6d6d6d;"));
    }
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

inline void showProperties(QWidget *parent, const Device &d)
{
    auto *dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QStringLiteral("Властивості: %1").arg(d.name));
    dlg->setWindowIcon(QIcon::fromTheme(d.icon));
    dlg->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dlg->setFixedSize(420, 480);
    auto *v = new QVBoxLayout(dlg);
    v->setContentsMargins(7, 9, 7, 9);
    auto *tabs = new QTabWidget;

    // General
    auto *general = new QWidget;
    auto *gv = new QVBoxLayout(general);
    gv->setContentsMargins(10, 12, 10, 10);
    auto *top = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(deviceIcon(d, 32));
    top->addWidget(icon);
    top->addSpacing(18);
    top->addWidget(new QLabel(d.name), 1);
    gv->addLayout(top);
    gv->addSpacing(8);
    auto *info = new QGroupBox(QStringLiteral("Відомості про пристрій"));
    auto *ig = new QGridLayout(info);
    ig->setHorizontalSpacing(20);
    ig->setVerticalSpacing(9);
    ig->setContentsMargins(10, 14, 10, 14);
    const QList<QPair<QString, QString>> rows{
        {QStringLiteral("Виробник:"), d.manufacturer}, {QStringLiteral("Модель:"), d.model}, {QStringLiteral("Номер моделі:"), d.modelNumber},
        {QStringLiteral("Категорії:"), d.category},    {QStringLiteral("Опис:"), d.description},
    };
    for (int i = 0; i < rows.size(); ++i) {
        ig->addWidget(new QLabel(rows.at(i).first), i, 0);
        ig->addWidget(greyValue(rows.at(i).second), i, 1);
    }
    ig->setColumnStretch(1, 1);
    gv->addWidget(info);
    auto *tasks = new QGroupBox(QStringLiteral("Завдання пристрою"));
    auto *tv = new QVBoxLayout(tasks);
    tv->setContentsMargins(10, 14, 10, 10);
    auto *hint = new QLabel(QStringLiteral("Щоб переглянути завдання для цього пристрою, клацніть правою кнопкою миші значок пристрою у вікні «Пристрої та принтери»."));
    hint->setWordWrap(true);
    tv->addWidget(hint);
    tv->addStretch(1);
    gv->addWidget(tasks, 1);
    tabs->addTab(general, QStringLiteral("Загальні"));

    // Hardware
    auto *hw = new QWidget;
    auto *hv = new QVBoxLayout(hw);
    hv->setContentsMargins(10, 12, 10, 10);
    auto *head = new QHBoxLayout;
    auto *small = new QLabel;
    small->setPixmap(deviceIcon(d, 32));
    head->addWidget(small);
    head->addSpacing(12);
    head->addWidget(new QLabel(d.name), 1);
    hv->addLayout(head);
    hv->addSpacing(6);
    hv->addWidget(new QLabel(QStringLiteral("Функції пристрою:")));
    auto *tree = new QTreeWidget;
    tree->setRootIsDecorated(false);
    tree->setHeaderLabels({QStringLiteral("Ім'я"), QStringLiteral("Тип")});
    tree->setColumnWidth(0, 230);
    const QList<Function> functions = d.kind == Device::Computer ? computerFunctions() : d.functions;
    for (const Function &f : functions) {
        new QTreeWidgetItem(tree, {f.name, f.type});
    }
    hv->addWidget(tree, 1);
    auto *summary = new QGroupBox(QStringLiteral("Зведення функцій пристрою"));
    auto *sg = new QGridLayout(summary);
    sg->setHorizontalSpacing(20);
    sg->setContentsMargins(10, 14, 10, 10);
    auto *mf = greyValue(d.manufacturer);
    auto *loc = greyValue(d.location);
    auto *st = new QLabel(QStringLiteral("Пристрій працює нормально."));
    sg->addWidget(new QLabel(QStringLiteral("Виробник:")), 0, 0);
    sg->addWidget(mf, 0, 1);
    sg->addWidget(new QLabel(QStringLiteral("Розташування:")), 1, 0);
    sg->addWidget(loc, 1, 1);
    sg->addWidget(new QLabel(QStringLiteral("Стан пристрою:")), 2, 0);
    sg->addWidget(st, 2, 1);
    sg->setColumnStretch(1, 1);
    auto *props = new QPushButton(QStringLiteral("Властивості"));
    props->setEnabled(false);
    sg->addWidget(props, 3, 1, Qt::AlignRight);
    hv->addWidget(summary);
    QObject::connect(tree, &QTreeWidget::currentItemChanged, props, [props](QTreeWidgetItem *it) {
        props->setEnabled(it);
    });
    if (tree->topLevelItemCount() > 0) {
        tree->setCurrentItem(tree->topLevelItem(0));
    }
    QObject::connect(props, &QPushButton::clicked, dlg, [tree] {
        if (tree->currentItem()) {
            QProcess::startDetached(QDir::homePath() + QStringLiteral("/.local/bin/seven-devmgr"), {QStringLiteral("--properties"), tree->currentItem()->text(0)});
        }
    });
    tabs->addTab(hw, QStringLiteral("Устаткування"));
    v->addWidget(tabs, 1);

    auto *btns = new QHBoxLayout;
    btns->setContentsMargins(0, 4, 0, 0);
    btns->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
    auto *apply = new QPushButton(QStringLiteral("Застосувати"));
    for (QPushButton *b : {ok, cancel, apply}) {
        b->setMinimumWidth(75);
        btns->addWidget(b);
    }
    ok->setDefault(true);
    apply->setEnabled(false);
    v->addLayout(btns);
    QObject::connect(ok, &QPushButton::clicked, dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, dlg, &QDialog::reject);
    dlg->show();
}

/* ---------------- "Add a device" ---------------- */

struct Found {
    QString addr;
    QString name;
    QString icon;
};

inline void addDeviceWizard(QWidget *parent, const std::function<void()> &changed)
{
    auto *dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QStringLiteral("Додавання пристрою"));
    dlg->setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-devices-printers")));
    dlg->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dlg->resize(650, 520);
    auto *outer = new QVBoxLayout(dlg);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *white = new QWidget;
    white->setObjectName(QStringLiteral("win7wizwhite"));
    white->setAttribute(Qt::WA_StyledBackground, true);
    white->setStyleSheet(QStringLiteral("#win7wizwhite { background: #ffffff; }"));
    auto *wv = new QVBoxLayout(white);
    wv->setContentsMargins(10, 8, 30, 14);
    auto *head = new QHBoxLayout;
    auto *back = new QToolButton;
    back->setIcon(QIcon::fromTheme(QStringLiteral("go-previous")));
    back->setAutoRaise(true);
    back->setEnabled(false);
    head->addWidget(back);
    auto *hicon = new QLabel;
    hicon->setPixmap(QIcon::fromTheme(QStringLiteral("seven-devices-printers")).pixmap(16, 16));
    head->addWidget(hicon);
    head->addWidget(new QLabel(QStringLiteral("Додавання пристрою")), 1);
    wv->addLayout(head);
    auto *body = new QVBoxLayout;
    body->setContentsMargins(30, 14, 0, 0);
    auto *title = new QLabel(QStringLiteral("Виберіть пристрій для додавання до цього комп'ютера"));
    QFont tf = title->font();
    tf.setPixelSize(16);
    title->setFont(tf);
    title->setStyleSheet(QStringLiteral("color: #1e3287;"));
    body->addWidget(title);
    body->addSpacing(14);
    auto *sub = new QLabel(QStringLiteral("Комп'ютер продовжить пошук нових пристроїв і відобразить їх у цьому вікні."));
    sub->setWordWrap(true);
    body->addWidget(sub);
    body->addSpacing(8);
    auto *list = new QListWidget;
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(48, 48));
    list->setGridSize(QSize(120, 96));
    list->setMovement(QListView::Static);
    list->setResizeMode(QListView::Adjust);
    list->setWordWrap(true);
    auto *empty = new QLabel(QStringLiteral("Пошук пристроїв... Переконайтеся, що пристрій доступний для виявлення."), list->viewport());
    empty->setStyleSheet(QStringLiteral("color: #6d6d6d;"));
    empty->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    empty->setWordWrap(true);
    body->addWidget(list, 1);
    auto *result = new QLabel;
    result->setWordWrap(true);
    result->hide();
    body->addWidget(result);
    body->addStretch(0);
    wv->addLayout(body, 1);
    outer->addWidget(white, 1);

    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(10, 10, 10, 10);
    btnRow->addStretch(1);
    auto *next = new QPushButton(QStringLiteral("Далі"));
    auto *cancel = new QPushButton(QStringLiteral("Скасувати"));
    next->setEnabled(false);
    next->setMinimumWidth(75);
    cancel->setMinimumWidth(75);
    btnRow->addWidget(next);
    btnRow->addWidget(cancel);
    outer->addLayout(btnRow);

    // keep discovery running while the window is open (BlueZ stops it when bluetoothctl exits)
    auto *scan = new QProcess(dlg);
    scan->start(QStringLiteral("bluetoothctl"), {QStringLiteral("--timeout"), QStringLiteral("600"), QStringLiteral("scan"), QStringLiteral("on")});
    QObject::connect(dlg, &QObject::destroyed, scan, [scan] {
        scan->kill();
    });
    auto paired = std::make_shared<QStringList>(pairedAddresses());
    auto *poll = new QTimer(dlg);
    auto refresh = [list, empty, paired] {
        static const QRegularExpression addrName(QStringLiteral("^([0-9A-F]{2}[:-]){5}[0-9A-F]{2}$"), QRegularExpression::CaseInsensitiveOption);
        for (const QString &line : run(QStringLiteral("bluetoothctl"), {QStringLiteral("devices")}).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            if (!line.startsWith(QLatin1String("Device "))) {
                continue;
            }
            const QString addr = line.section(QLatin1Char(' '), 1, 1);
            const QString name = line.section(QLatin1Char(' '), 2).trimmed();
            if (paired->contains(addr) || name.isEmpty() || addrName.match(name).hasMatch()) {
                continue; // nameless LE beacons are not shown by Windows either
            }
            bool have = false;
            for (int i = 0; i < list->count(); ++i) {
                have = have || list->item(i)->data(Qt::UserRole).toString() == addr;
            }
            if (have) {
                continue;
            }
            Device d;
            d.name = name;
            classifyBluetooth(d, keyValue(run(QStringLiteral("bluetoothctl"), {QStringLiteral("info"), addr}), QStringLiteral("Icon"), QLatin1Char(':')));
            auto *it = new QListWidgetItem(QIcon::fromTheme(d.icon), name + QStringLiteral("\n") + d.category, list);
            it->setData(Qt::UserRole, addr);
            it->setToolTip(addr);
        }
        empty->setVisible(list->count() == 0);
        empty->setGeometry(10, 22, list->viewport()->width() - 20, 40);
    };
    QObject::connect(poll, &QTimer::timeout, dlg, refresh);
    poll->start(2000);
    QTimer::singleShot(0, dlg, refresh);
    QObject::connect(list, &QListWidget::currentItemChanged, next, [next](QListWidgetItem *it) {
        next->setEnabled(it);
    });

    auto done = std::make_shared<bool>(false);
    auto pair = [=] {
        if (*done) {
            dlg->accept();
            return;
        }
        QListWidgetItem *it = list->currentItem();
        if (!it) {
            return;
        }
        const QString addr = it->data(Qt::UserRole).toString();
        const QString name = it->text().section(QLatin1Char('\n'), 0, 0);
        poll->stop();
        scan->kill();
        list->hide();
        sub->hide();
        title->setText(QStringLiteral("Підключення до пристрою «%1»").arg(name));
        result->setText(QStringLiteral("Зачекайте, триває налаштування пристрою..."));
        result->show();
        next->setEnabled(false);
        auto *proc = new QProcess(dlg);
        proc->start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("bluetoothctl --timeout 25 pair \"$1\" && bluetoothctl trust \"$1\" && bluetoothctl --timeout 15 connect \"$1\""), QStringLiteral("sh"), addr});
        QObject::connect(proc, &QProcess::finished, dlg, [=](int code) {
            const bool ok = code == 0 || pairedAddresses().contains(addr);
            *done = true;
            title->setText(ok ? QStringLiteral("Пристрій успішно додано до цього комп'ютера") : QStringLiteral("Не вдалося додати пристрій"));
            result->setText(ok ? QStringLiteral("Тепер його можна знайти у вікні «Пристрої та принтери».")
                               : QStringLiteral("Переконайтеся, що пристрій увімкнено, перебуває поруч і доступний для виявлення, а потім повторіть спробу."));
            next->setText(QStringLiteral("Закрити"));
            next->setEnabled(true);
            cancel->hide();
            if (ok) {
                changed();
            }
        });
    };
    QObject::connect(next, &QPushButton::clicked, dlg, pair);
    QObject::connect(list, &QListWidget::itemDoubleClicked, dlg, pair);
    QObject::connect(cancel, &QPushButton::clicked, dlg, &QDialog::reject);
    dlg->show();
}

/* ---------------- the window contents ---------------- */

class DeviceTile : public QWidget
{
public:
    DeviceTile(const Device &dev, QWidget *parent)
        : QWidget(parent)
        , d(dev)
    {
        setAttribute(Qt::WA_Hover);
        setFixedSize(120, 150);
        m_icon = deviceIcon(d, 96);
    }
    Device d;
    bool selected = false;
    std::function<void(DeviceTile *)> onClick, onDoubleClick;
    std::function<void(DeviceTile *, QPoint)> onMenu;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QFontMetrics fm(font());
        QRect text = fm.boundingRect(QRect(4, 104, width() - 8, 200), Qt::AlignHCenter | Qt::TextWordWrap, d.name);
        const int lines = qMin(3, text.height() / fm.lineSpacing());
        const int h = 104 + lines * fm.lineSpacing() + 4;
        paintSelection(p, QRectF(0, 0, width(), qMin(h, height())), selected, underMouse(), window()->isActiveWindow());
        p.drawPixmap((width() - 96) / 2, 4, m_icon);
        p.setPen(Qt::black);
        QString shown = d.name;
        if (text.height() / fm.lineSpacing() > 3) {
            shown = fm.elidedText(d.name, Qt::ElideRight, (width() - 8) * 3 - 20);
        }
        p.drawText(QRect(4, 104, width() - 8, lines * fm.lineSpacing() + 2), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, shown);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (onClick) {
            onClick(this);
        }
        if (e->button() == Qt::RightButton && onMenu) {
            onMenu(this, e->globalPosition().toPoint());
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onDoubleClick) {
            onDoubleClick(this);
        }
    }

private:
    QPixmap m_icon;
};

/** "◢ Printers and Faxes (2) ─────────" */
class GroupHeader : public QWidget
{
public:
    GroupHeader(const QString &text, QWidget *parent)
        : QWidget(parent)
        , m_text(text)
    {
        setAttribute(Qt::WA_Hover);
        setFixedHeight(24);
        setCursor(Qt::PointingHandCursor);
    }
    bool collapsed = false;
    std::function<void()> toggled;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool hot = underMouse();
        p.setPen(Qt::NoPen);
        p.setBrush(hot ? QColor(0x1e, 0x6b, 0xc8) : QColor(0x4d, 0x4d, 0x4d));
        const qreal cy = height() / 2.0;
        if (collapsed) {
            p.drawPolygon(QPolygonF({QPointF(5, cy - 4), QPointF(9, cy), QPointF(5, cy + 4)}));
        } else {
            p.drawPolygon(QPolygonF({QPointF(9, cy - 3), QPointF(9, cy + 3), QPointF(3, cy + 3)}));
        }
        QFont f = font();
        f.setPixelSize(15);
        p.setFont(f);
        p.setPen(hot ? QColor(0x1e, 0x6b, 0xc8) : QColor(0x1e, 0x32, 0x87));
        p.drawText(QRect(16, 0, width(), height()), Qt::AlignLeft | Qt::AlignVCenter, m_text);
        const int x = 16 + QFontMetrics(f).horizontalAdvance(m_text) + 8;
        QLinearGradient g(x, 0, width(), 0);
        g.setColorAt(0, QColor(0xe2, 0xe5, 0xeb));
        g.setColorAt(1, QColor(0xf5, 0xf6, 0xf8));
        p.setPen(QPen(QBrush(g), 1));
        p.drawLine(QPointF(x, cy + 1.5), QPointF(width() - 4, cy + 1.5));
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            collapsed = !collapsed;
            update();
            if (toggled) {
                toggled();
            }
        }
    }

private:
    QString m_text;
};

/** Details pane at the bottom: big icon, name and a few properties */
class DetailsPane : public QWidget
{
public:
    explicit DetailsPane(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(70);
    }

    void showCount(int n)
    {
        m_device.reset();
        m_count = n;
        update();
    }
    void showDevice(const Device &d)
    {
        m_device = std::make_unique<Device>(d);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor(0xf8, 0xfb, 0xfe));
        g.setColorAt(1, QColor(0xe4, 0xec, 0xf6));
        p.fillRect(rect(), g);
        p.setPen(QColor(0xc4, 0xd0, 0xdf));
        p.drawLine(0, 0, width(), 0);
        p.setPen(QColor(255, 255, 255));
        p.drawLine(0, 1, width(), 1);
        if (!m_device) {
            p.drawPixmap(24, 5, QIcon::fromTheme(QStringLiteral("seven-devices-printers")).pixmap(64, 64));
            p.setPen(QColor(0x1e, 0x1e, 0x1e));
            p.drawText(QRect(102, 8, width(), 18), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Елементів: %1").arg(m_count));
            return;
        }
        const Device &d = *m_device;
        p.drawPixmap(24, 5, deviceIcon(d, 64));
        const QFontMetrics fm(font());
        QFont big = font();
        big.setPixelSize(15);
        p.setFont(big);
        p.setPen(Qt::black);
        const int nameW = qMin(QFontMetrics(big).horizontalAdvance(d.name), 240);
        p.drawText(QRect(102, 6, nameW, 22), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(big).elidedText(d.name, Qt::ElideRight, 240));
        p.setFont(font());
        QList<QPair<QString, QString>> rows;
        if (!d.state.isEmpty()) {
            rows.append({QStringLiteral("Стан:"), d.state});
        }
        rows.append({QStringLiteral("Виробник:"), d.manufacturer.isEmpty() ? QStringLiteral("Недоступно") : d.manufacturer});
        rows.append({QStringLiteral("Модель:"), d.model.isEmpty() ? QStringLiteral("Недоступно") : d.model});
        rows.append({QStringLiteral("Категорія:"), d.category});
        int labelW = 0;
        for (const auto &r : rows) {
            labelW = qMax(labelW, fm.horizontalAdvance(r.first));
        }
        const int x = 102 + nameW + 10;
        for (int i = 0; i < qMin(3, int(rows.size())); ++i) {
            const int y = 9 + i * 18;
            p.setPen(QColor(0x5a, 0x67, 0x79));
            p.drawText(QRect(x, y, labelW, 18), Qt::AlignRight | Qt::AlignVCenter, rows.at(i).first);
            p.setPen(Qt::black);
            p.drawText(QRect(x + labelW + 6, y, width() - x - labelW - 10, 18), Qt::AlignLeft | Qt::AlignVCenter, rows.at(i).second);
        }
    }

private:
    std::unique_ptr<Device> m_device;
    int m_count = 0;
};

struct Nav {
    std::function<void(int)> open; // another view of the Control Panel
    std::function<void()> close;
};

class DevicesPage : public QWidget
{
public:
    DevicesPage(QToolBar *bar, Nav nav)
        : m_bar(bar)
        , m_nav(std::move(nav))
    {
        setObjectName(QStringLiteral("win7page"));
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        m_scroll = new QScrollArea;
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setWidgetResizable(true);
        m_area = new Area(this);
        m_scroll->setWidget(m_area);
        v->addWidget(m_scroll, 1);
        m_details = new DetailsPane;
        v->addWidget(m_details);
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this] {
            if (signature() != m_signature) {
                reload();
            }
        });
        m_timer->start(4000);
        reload();
    }

    void setFilter(const QString &f)
    {
        m_filter = f;
        relayout();
    }

    void reload()
    {
        m_signature = signature();
        const QString keep = m_selected ? m_selected->d.key : QString();
        qDeleteAll(m_tiles);
        m_tiles.clear();
        m_selected = nullptr;
        QList<Device> printerList = printers();
        QList<Device> devices;
        devices << computer() << monitors() << usbDevices() << bluetoothDevices();
        QCollator collator(QLocale(QLocale::Ukrainian));
        collator.setNumericMode(true);
        std::sort(devices.begin(), devices.end(), [&collator](const Device &a, const Device &b) {
            return collator.compare(a.name, b.name) < 0;
        });
        for (const Device &d : printerList + devices) {
            auto *t = new DeviceTile(d, m_area);
            t->onClick = [this](DeviceTile *w) {
                select(w);
            };
            t->onDoubleClick = [this](DeviceTile *w) {
                activate(w->d);
            };
            t->onMenu = [this](DeviceTile *w, QPoint pos) {
                contextMenu(w->d, pos);
            };
            m_tiles << t;
            if (d.key == keep) {
                m_selected = t;
                t->selected = true;
            }
            t->show();
        }
        relayout();
        updateDetails();
        updateBar();
    }

private:
    /** Scroll area contents: headers and tiles placed by hand */
    class Area : public QWidget
    {
    public:
        explicit Area(DevicesPage *page)
            : m_page(page)
        {
            setObjectName(QStringLiteral("win7devarea"));
            setAttribute(Qt::WA_StyledBackground, true);
            setStyleSheet(QStringLiteral("#win7devarea { background: #ffffff; }"));
            setContextMenuPolicy(Qt::NoContextMenu);
        }

    protected:
        void resizeEvent(QResizeEvent *) override
        {
            m_page->relayout();
        }
        void mousePressEvent(QMouseEvent *e) override
        {
            m_page->select(nullptr);
            if (e->button() == Qt::RightButton) {
                m_page->backgroundMenu(e->globalPosition().toPoint());
            }
        }

    private:
        DevicesPage *m_page;
    };

    void relayout()
    {
        if (!m_area) {
            return;
        }
        QList<DeviceTile *> groups[2];
        for (DeviceTile *t : std::as_const(m_tiles)) {
            const bool match = m_filter.isEmpty() || t->d.name.contains(m_filter, Qt::CaseInsensitive) || t->d.category.contains(m_filter, Qt::CaseInsensitive);
            t->setVisible(false);
            if (match) {
                groups[t->d.kind == Device::Printer ? 0 : 1].append(t);
            }
        }
        const QString titles[2] = {QStringLiteral("Принтери й факси (%1)"), QStringLiteral("Пристрої (%1)")};
        const int width = qMax(200, m_scroll->viewport()->width());
        const int cols = qMax(1, (width - 30) / 120);
        int y = 8;
        for (int g = 0; g < 2; ++g) {
            delete m_headers[g];
            m_headers[g] = new GroupHeader(titles[g].arg(groups[g].size()), m_area);
            m_headers[g]->collapsed = m_collapsed[g];
            m_headers[g]->toggled = [this, g] {
                m_collapsed[g] = !m_collapsed[g];
                QTimer::singleShot(0, this, [this] {
                    relayout();
                });
            };
            if (groups[g].isEmpty()) {
                m_headers[g]->hide();
                continue;
            }
            m_headers[g]->setGeometry(8, y, width - 20, 24);
            m_headers[g]->show();
            y += 32;
            if (m_collapsed[g]) {
                continue;
            }
            int k = 0;
            int rowH = 0;
            for (DeviceTile *t : std::as_const(groups[g])) {
                t->move(16 + (k % cols) * 120, y + (k / cols) * 156);
                t->show();
                rowH = (k / cols + 1) * 156;
                ++k;
            }
            y += rowH + 10;
        }
        m_area->setMinimumHeight(y + 8);
    }

    void select(DeviceTile *w)
    {
        for (DeviceTile *t : std::as_const(m_tiles)) {
            t->selected = (t == w);
            t->update();
        }
        m_selected = w;
        updateDetails();
        updateBar();
    }

    void updateDetails()
    {
        if (m_selected) {
            m_details->showDevice(m_selected->d);
        } else {
            m_details->showCount(m_tiles.size());
        }
    }

    void changed()
    {
        QTimer::singleShot(800, this, [this] {
            reload();
        });
    }

    void addDevice()
    {
        addDeviceWizard(window(), [this] {
            changed();
        });
    }

    void addPrinter()
    {
        Win7Printer::addPrinterWizard(window(), [this] {
            changed();
        });
    }

    static void self(const QStringList &args)
    {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
    }

    /** Double click: the most useful thing for the device, like Device Stage in Windows 7 */
    void activate(const Device &d)
    {
        switch (d.kind) {
        case Device::Printer:
            launch(QStringLiteral("kde-print-queue"), {d.printer});
            break;
        case Device::Mouse:
            self({QStringLiteral("--mouse")});
            break;
        case Device::Disk:
            openDisk(d);
            break;
        default:
            showProperties(window(), d);
        }
    }

    void openDisk(const Device &d)
    {
        if (d.block.isEmpty()) {
            return;
        }
        // the first partition with a file system, mounted on demand
        const QString out = run(QStringLiteral("lsblk"), {QStringLiteral("-nrpo"), QStringLiteral("NAME,FSTYPE,MOUNTPOINT"), d.block});
        for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QStringList f = line.split(QLatin1Char(' '));
            if (f.value(1).isEmpty()) {
                continue;
            }
            QString mp = f.value(2).replace(QStringLiteral("\\x20"), QStringLiteral(" "));
            if (mp.isEmpty()) {
                const QString m = run(QStringLiteral("udisksctl"), {QStringLiteral("mount"), QStringLiteral("-b"), f.at(0)}, 15000);
                static const QRegularExpression at(QStringLiteral(" at (.+?)\\.?\\s*$"));
                mp = at.match(m.trimmed()).captured(1);
            }
            if (!mp.isEmpty()) {
                launch(QStringLiteral("dolphin"), {mp});
                return;
            }
        }
    }

    void ejectDisk(const Device &d)
    {
        if (d.block.isEmpty()) {
            return;
        }
        const QString out = run(QStringLiteral("lsblk"), {QStringLiteral("-nrpo"), QStringLiteral("NAME,MOUNTPOINT"), d.block});
        for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            if (!line.section(QLatin1Char(' '), 1).isEmpty()) {
                QProcess p;
                p.start(QStringLiteral("udisksctl"), {QStringLiteral("unmount"), QStringLiteral("-b"), line.section(QLatin1Char(' '), 0, 0)});
                p.waitForFinished(15000);
                if (p.exitCode() != 0) {
                    QMessageBox::warning(window(), QStringLiteral("Проблема під час вилучення пристрою"),
                                         QStringLiteral("Цей пристрій зараз використовується. Закрийте всі програми або вікна, які можуть використовувати пристрій, і повторіть спробу."));
                    return;
                }
            }
        }
        QProcess::execute(QStringLiteral("udisksctl"), {QStringLiteral("power-off"), QStringLiteral("-b"), d.block});
        changed();
    }

    void removeDevice(const Device &d)
    {
        if (QMessageBox::question(window(), QStringLiteral("Видалення пристрою"), QStringLiteral("Ви дійсно бажаєте видалити цей пристрій?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes) {
            return;
        }
        if (!d.printer.isEmpty()) {
            if (QProcess::execute(QStringLiteral("lpadmin"), {QStringLiteral("-x"), d.printer}) != 0) {
                QProcess::execute(QStringLiteral("pkexec"), {QStringLiteral("lpadmin"), QStringLiteral("-x"), d.printer});
            }
        } else if (!d.bt.isEmpty()) {
            run(QStringLiteral("bluetoothctl"), {QStringLiteral("remove"), d.bt}, 8000);
        }
        changed();
    }

    void setDefaultPrinter(const Device &d)
    {
        if (QProcess::execute(QStringLiteral("lpadmin"), {QStringLiteral("-d"), d.printer}) != 0) {
            QProcess::execute(QStringLiteral("lpoptions"), {QStringLiteral("-d"), d.printer});
        }
        changed();
    }

    /** The device's own commands, used by both the context menu and the command bar */
    struct Command {
        QString text;
        std::function<void()> run;
        bool bold = false;
        bool separatorBefore = false;
    };

    /** BlueZ connect / disconnect in the background; the window refreshes itself from signature() */
    void setBluetoothConnected(const Device &d, bool on)
    {
        auto *proc = new QProcess(this);
        QObject::connect(proc, &QProcess::finished, this, [this, proc] {
            proc->deleteLater();
            changed();
        });
        proc->start(QStringLiteral("bluetoothctl"), {QStringLiteral("--timeout"), QStringLiteral("15"), on ? QStringLiteral("connect") : QStringLiteral("disconnect"), d.bt});
    }

    QList<Command> commands(const Device &d)
    {
        QList<Command> c;
        if (!d.bt.isEmpty()) {
            // every Bluetooth device: it may not reconnect by itself
            if (d.connected) {
                c << Command{QStringLiteral("Відключити"), [this, d] { setBluetoothConnected(d, false); }};
            } else {
                c << Command{QStringLiteral("Підключити"), [this, d] { setBluetoothConnected(d, true); }};
            }
        }
        switch (d.kind) {
        case Device::Printer:
            c << Command{QStringLiteral("Переглянути, що друкується"), [d] { launch(QStringLiteral("kde-print-queue"), {d.printer}); }, true};
            if (!d.isDefault) {
                c << Command{QStringLiteral("Використовувати за замовчуванням"), [this, d] { setDefaultPrinter(d); }, false, true};
            }
            c << Command{QStringLiteral("Настройка друку"), [d] { launch(QStringLiteral("configure-printer"), {d.printer}); }, false, d.isDefault};
            c << Command{QStringLiteral("Властивості принтера"), [d] { launch(QStringLiteral("configure-printer"), {d.printer}); }};
            c << Command{QStringLiteral("Видалити пристрій"), [this, d] { removeDevice(d); }, false, true};
            break;
        case Device::Computer:
            c << Command{QStringLiteral("Властивості системи"), [] { launchWin7Tool(QStringLiteral("seven-system"), QStringLiteral("kinfocenter")); }};
            c << Command{QStringLiteral("Параметри електроживлення"), [this] { m_nav.open(Win7Power::PlansView); }};
            c << Command{QStringLiteral("Параметри миші"), [] { self({QStringLiteral("--mouse")}); }};
            c << Command{QStringLiteral("Параметри клавіатури"), [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--keyboard")}); }};
            c << Command{QStringLiteral("Параметри звуку"), [] { self({QStringLiteral("--sound")}); }};
            c << Command{QStringLiteral("Мова та регіональні стандарти"), [] { self({QStringLiteral("--region")}); }};
            break;
        case Device::Monitor:
            c << Command{QStringLiteral("Параметри екрана"), [this] { m_nav.open(Win7Display::ResolutionView); }};
            break;
        case Device::Mouse:
            c << Command{QStringLiteral("Параметри миші"), [] { self({QStringLiteral("--mouse")}); }};
            break;
        case Device::Keyboard:
            c << Command{QStringLiteral("Параметри клавіатури"), [] { QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--keyboard")}); }};
            break;
        case Device::Audio:
            c << Command{QStringLiteral("Параметри звуку"), [] { self({QStringLiteral("--sound")}); }};
            break;
        case Device::Disk:
            c << Command{QStringLiteral("Відкрити"), [this, d] { openDisk(d); }, true};
            c << Command{QStringLiteral("Вилучити"), [this, d] { ejectDisk(d); }, false, true};
            break;
        default:
            break;
        }
        if (!d.bt.isEmpty()) {
            c << Command{QStringLiteral("Видалити пристрій"), [this, d] { removeDevice(d); }, false, true};
        }
        return c;
    }

    void contextMenu(const Device &d, QPoint pos)
    {
        QMenu menu(this);
        for (const Command &c : commands(d)) {
            if (c.separatorBefore && !menu.isEmpty()) {
                menu.addSeparator();
            }
            QAction *a = menu.addAction(c.text, this, c.run);
            if (c.bold) {
                QFont f = a->font();
                f.setBold(true);
                a->setFont(f);
            }
        }
        if (!menu.isEmpty()) {
            menu.addSeparator();
        }
        QAction *props = menu.addAction(QStringLiteral("Властивості"), this, [this, d] {
            showProperties(window(), d);
        });
        if (d.kind != Device::Printer && d.kind != Device::Disk) {
            QFont f = props->font();
            f.setBold(true);
            props->setFont(f);
        }
        menu.exec(pos);
    }

    void backgroundMenu(QPoint pos)
    {
        QMenu menu(this);
        menu.addAction(QStringLiteral("Додавання пристрою"), this, [this] {
            addDevice();
        });
        menu.addAction(QStringLiteral("Установка принтера"), this, [this] {
            addPrinter();
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Диспетчер пристроїв"), this, [] {
            launchWin7Tool(QStringLiteral("seven-devmgr"), QStringLiteral("kinfocenter"));
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Оновити"), this, [this] {
            reload();
        });
        menu.exec(pos);
    }

    void updateBar()
    {
        if (!m_bar) {
            return;
        }
        m_bar->clear();
        m_bar->addAction(QStringLiteral("Додавання пристрою"), this, [this] {
            addDevice();
        });
        m_bar->addAction(QStringLiteral("Установка принтера"), this, [this] {
            addPrinter();
        });
        if (!m_selected) {
            return;
        }
        const Device d = m_selected->d;
        for (const Command &c : commands(d)) {
            m_bar->addAction(c.text, this, c.run);
        }
    }

    QToolBar *m_bar;
    Nav m_nav;
    QScrollArea *m_scroll = nullptr;
    Area *m_area = nullptr;
    DetailsPane *m_details = nullptr;
    QTimer *m_timer = nullptr;
    QList<DeviceTile *> m_tiles;
    DeviceTile *m_selected = nullptr;
    GroupHeader *m_headers[2] = {nullptr, nullptr};
    bool m_collapsed[2] = {false, false};
    QString m_filter;
    QString m_signature;
};
}
