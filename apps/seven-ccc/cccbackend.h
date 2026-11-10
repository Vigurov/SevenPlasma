/*
 * What the Catalyst Control Center pages read and change on Linux:
 *  - the AMD GPU (sysfs of the amdgpu card, Mesa through glxinfo/vulkaninfo)
 *  - displays (kscreen-doctor: mode, refresh rate, rotation, enable/disable)
 *  - desktop colour (gamma ramps of the X11 CRTCs through XRandR, loaded with dlopen; reapplied at login)
 *  - PowerPlay (power-profiles-daemon)
 *  - "Wait for vertical refresh" for OpenGL (Mesa driconf, ~/.drirc)
 * Settings that must survive a restart live in ~/.config/seven-cccrc.
 */
#pragma once
#include "win7i18n.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QSysInfo>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <unistd.h>

namespace Ccc
{

inline QProcessEnvironment childEnv()
{
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    e.remove(QStringLiteral("QT_QPA_PLATFORM")); // kscreen-doctor & co. must see the real screens
    return e;
}

inline QString run(const QString &prog, const QStringList &args, int msec = 8000)
{
    QProcess p;
    p.setProcessEnvironment(childEnv());
    p.start(prog, args);
    if (!p.waitForFinished(msec)) {
        p.kill();
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

inline QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(f.readAll()).trimmed();
}

inline QSettings &store()
{
    static QSettings s(QDir::homePath() + QStringLiteral("/.config/seven-cccrc"), QSettings::IniFormat);
    return s;
}

/* ---------------------------------------------------------------- GPUs present */

enum Vendor { VendorOther, VendorAmd, VendorIntel, VendorNvidia };

struct Gpu {
    QString card;     // /sys/class/drm/cardN
    QString pci;      // 0000:05:00.0
    Vendor vendor = VendorOther;
    QString deviceId, subVendor, subDevice, revision;
    QString name;     // from lspci
    bool bootVga = false;    // the adapter the firmware started the screen on (the default GPU of Mesa)
    bool integrated = false; // APU graphics sharing system memory
    QString memType;         // DDR4, GDDR6, HBM2... from the amdgpu kernel log
    int memWidth = 0;        // memory bus width, bits
    // Mesa's name for the device in DRI_PRIME / driconf device_id
    QString primeTag() const { return QStringLiteral("pci-") + QString(pci).replace(QLatin1Char(':'), QLatin1Char('_')).replace(QLatin1Char('.'), QLatin1Char('_')); }
};

// /sys/class/drm, or a fake tree for tests (WIN7_CCC_SYSFS); devices there may carry an "lspci_name" file
inline QString drmRoot()
{
    const QString fake = qEnvironmentVariable("WIN7_CCC_SYSFS");
    return fake.isEmpty() ? QStringLiteral("/sys/class/drm") : fake;
}

/* "amdgpu 0000:03:00.0: [drm] RAM width 256bits GDDR6" from this boot's kernel log (journal: adm group, else dmesg) */
inline QHash<QString, QPair<int, QString>> amdgpuMemoryLog()
{
    static QHash<QString, QPair<int, QString>> cached;
    static bool done = false;
    if (done) {
        return cached;
    }
    done = true;
    QString log;
    const QString fake = qEnvironmentVariable("WIN7_CCC_KLOG");
    if (!fake.isEmpty()) {
        log = readFile(fake);
    } else {
        log = run(QStringLiteral("journalctl"), {QStringLiteral("-k"), QStringLiteral("-b"), QStringLiteral("-o"), QStringLiteral("cat"), QStringLiteral("--no-pager"),
                                                 QStringLiteral("--grep"), QStringLiteral("RAM width")});
        if (!log.contains(QStringLiteral("RAM width"))) {
            log = run(QStringLiteral("dmesg"), {});
        }
    }
    const QRegularExpression re(QStringLiteral("amdgpu (\\S+): .*RAM width (\\d+)bits (\\S+)"));
    for (const QString &l : log.split(QLatin1Char('\n'))) {
        const auto m = re.match(l);
        if (m.hasMatch()) {
            cached.insert(m.captured(1), {m.captured(2).toInt(), m.captured(3)});
        }
    }
    return cached;
}

inline QList<Gpu> gpus()
{
    static QList<Gpu> cached;
    static bool done = false;
    if (done) {
        return cached;
    }
    done = true;
    QList<Gpu> &out = cached;
    const QDir d(drmRoot());
    for (const QString &c : d.entryList({QStringLiteral("card?"), QStringLiteral("card??")}, QDir::AllEntries | QDir::System)) {
        const QString dev = d.absoluteFilePath(c) + QStringLiteral("/device");
        Gpu g;
        g.card = d.absoluteFilePath(c);
        const QString v = readFile(dev + QStringLiteral("/vendor"));
        g.vendor = v == QLatin1String("0x1002") ? VendorAmd : v == QLatin1String("0x8086") ? VendorIntel : v == QLatin1String("0x10de") ? VendorNvidia : VendorOther;
        g.deviceId = readFile(dev + QStringLiteral("/device")).mid(2).toUpper();
        g.subVendor = readFile(dev + QStringLiteral("/subsystem_vendor")).mid(2).toUpper();
        g.subDevice = readFile(dev + QStringLiteral("/subsystem_device")).mid(2).toUpper();
        g.revision = readFile(dev + QStringLiteral("/revision")).mid(2).toUpper();
        g.pci = QFileInfo(QFile::symLinkTarget(dev)).fileName();
        if (QFile::exists(dev + QStringLiteral("/lspci_name"))) {
            g.name = readFile(dev + QStringLiteral("/lspci_name"));
            g.pci = readFile(dev + QStringLiteral("/pci_address"));
        } else {
            const QString l = run(QStringLiteral("lspci"), {QStringLiteral("-s"), g.pci, QStringLiteral("-mm")});
            // "05:00.0" "VGA compatible controller" "Advanced Micro Devices, Inc. [AMD/ATI]" "Cezanne [Radeon Vega Series / ...]" ...
            const QStringList f = l.split(QLatin1Char('"'));
            g.name = f.size() > 5 ? f.at(5) : QString();
        }
        g.bootVga = readFile(dev + QStringLiteral("/boot_vga")) == QLatin1String("1");
        if (g.vendor == VendorAmd) {
            const auto mem = amdgpuMemoryLog().value(g.pci);
            g.memWidth = mem.first;
            g.memType = mem.second;
            if (!g.memType.isEmpty()) {
                // system memory types mean an APU; graphics memory (GDDR*, HBM*) a graphics card
                g.integrated = !g.memType.startsWith(QLatin1String("GDDR")) && !g.memType.startsWith(QLatin1String("HBM"));
            } else {
                // no log: a card names the maker of its VRAM chips, an APU has no VRAM of its own
                g.integrated = !QFile::exists(dev + QStringLiteral("/mem_info_vram_vendor"));
            }
        } else if (g.vendor == VendorIntel) {
            g.integrated = !g.name.contains(QStringLiteral("Arc"), Qt::CaseInsensitive) && !g.name.contains(QStringLiteral("DG2")) && !g.name.contains(QStringLiteral("Battlemage"));
        }
        out << g;
    }
    return out;
}

/* every AMD adapter, primary first: the one Mesa uses by default (boot VGA), then integrated, then by PCI address */
inline QList<Gpu> amdGpus()
{
    QList<Gpu> l;
    for (const Gpu &g : gpus()) {
        if (g.vendor == VendorAmd) {
            l << g;
        }
    }
    std::stable_sort(l.begin(), l.end(), [](const Gpu &a, const Gpu &b) {
        if (a.bootVga != b.bootVga) {
            return a.bootVga;
        }
        if (a.integrated != b.integrated) {
            return a.integrated;
        }
        return a.pci < b.pci;
    });
    return l;
}

/* Switchable graphics (AMD PowerXpress): an AMD graphics card next to a power-saving GPU (AMD APU or Intel).
   The high-performance GPU is the AMD card that is not the default one. */
struct Switchable {
    Gpu performance, powerSaving;
    bool valid() const { return !performance.card.isEmpty() && !powerSaving.card.isEmpty(); }
};

inline Switchable switchable()
{
    Switchable s;
    const QList<Gpu> all = gpus();
    for (const Gpu &g : all) {
        if (g.vendor == VendorAmd && !g.integrated && !g.bootVga) {
            s.performance = g;
            break;
        }
    }
    if (s.performance.card.isEmpty()) {
        return s;
    }
    for (const Gpu &g : all) {
        if (g.card != s.performance.card && g.bootVga) {
            s.powerSaving = g;
        }
    }
    if (s.powerSaving.card.isEmpty()) {
        for (const Gpu &g : all) {
            if (g.card != s.performance.card && g.integrated) {
                s.powerSaving = g;
                break;
            }
        }
    }
    return s;
}

inline Gpu amdGpu()
{
    return amdGpus().value(0);
}

/* ---------------------------------------------------------------- software / hardware information */

struct Row {
    QString item, value;
};

inline QString mesaVersion()
{
    const QString o = run(QStringLiteral("glxinfo"), {QStringLiteral("-B")});
    const auto m = QRegularExpression(QStringLiteral("OpenGL version string: .*Mesa ([^\\s]+)")).match(o);
    return m.hasMatch() ? m.captured(1) : QString();
}

inline QStringList glInfo()
{
    // renderer, core version, compat version
    const QString o = run(QStringLiteral("glxinfo"), {QStringLiteral("-B")});
    auto cap = [&](const char *re) {
        const auto m = QRegularExpression(QString::fromLatin1(re)).match(o);
        return m.hasMatch() ? m.captured(1).trimmed() : QString();
    };
    return {cap("OpenGL renderer string: ([^\\n]+)"), cap("OpenGL core profile version string: ([^\\n]+)"), cap("OpenGL version string: ([^\\n]+)")};
}

/* marketing name of an adapter: the OpenGL renderer of that very device (DRI_PRIME selects it), else the
   bracketed part of the lspci name */
inline QString cardName(const Gpu &g)
{
    QProcess p;
    QProcessEnvironment e = childEnv();
    e.insert(QStringLiteral("DRI_PRIME"), g.primeTag());
    p.setProcessEnvironment(e);
    p.start(QStringLiteral("glxinfo"), {QStringLiteral("-B")});
    QString o;
    if (p.waitForFinished(8000)) {
        o = QString::fromUtf8(p.readAllStandardOutput());
    }
    // "Device: AMD Radeon Graphics (radeonsi, renoir, ...) (0x1638)" must be this adapter
    const auto dev = QRegularExpression(QStringLiteral("Device: ([^\\n]*)\\(0x([0-9a-fA-F]+)\\)")).match(o);
    if (dev.hasMatch() && dev.captured(2).toUpper().rightJustified(4, QLatin1Char('0')) == g.deviceId) {
        const QString n = dev.captured(1).section(QStringLiteral(" ("), 0, 0).trimmed();
        if (!n.isEmpty()) {
            return n;
        }
    }
    QString b = g.name.section(QLatin1Char('['), 1).section(QLatin1Char(']'), 0, 0);
    if (b.isEmpty()) {
        return g.name;
    }
    return b.startsWith(QLatin1String("AMD")) ? b : QStringLiteral("AMD ") + b;
}

inline QString vulkanVersion()
{
    const QString o = run(QStringLiteral("vulkaninfo"), {QStringLiteral("--summary")});
    // the first RADV device
    const int i = o.indexOf(QStringLiteral("RADV"));
    const QString part = i >= 0 ? o.mid(qMax(0, o.lastIndexOf(QStringLiteral("GPU"), i))) : o;
    const auto m = QRegularExpression(QStringLiteral("apiVersion\\s*=\\s*([0-9.]+)")).match(part);
    return m.hasMatch() ? m.captured(1) : QString();
}

inline QString edidName(const QString &card, const QString &connector)
{
    // monitor name descriptor (tag 0xFC) of the EDID
    const QDir d(drmRoot());
    for (const QString &c : d.entryList({card.isEmpty() ? QStringLiteral("card*-*") : QFileInfo(card).fileName() + QStringLiteral("-*")}, QDir::AllEntries | QDir::System)) {
        if (!c.contains(connector.left(3)) || readFile(d.absoluteFilePath(c) + QStringLiteral("/status")) == QLatin1String("disconnected")) {
            continue;
        }
        QFile f(d.absoluteFilePath(c) + QStringLiteral("/edid"));
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray e = f.readAll();
        QString text;
        for (int k = 54; k + 18 <= qMin<qsizetype>(e.size(), 126); k += 18) {
            if (e.at(k) == 0 && e.at(k + 1) == 0 && uchar(e.at(k + 3)) == 0xFC) {
                return QString::fromLatin1(e.mid(k + 5, 13)).trimmed();
            }
            if (e.at(k) == 0 && e.at(k + 1) == 0 && uchar(e.at(k + 3)) == 0xFE && text.isEmpty()) {
                text = QString::fromLatin1(e.mid(k + 5, 13)).trimmed(); // laptop panels: part number as text
            }
        }
        if (!text.isEmpty()) {
            return text;
        }
    }
    return {};
}

inline int maxClock(const QString &file)
{
    int best = 0;
    for (const QString &l : readFile(file).split(QLatin1Char('\n'))) {
        const auto m = QRegularExpression(QStringLiteral("(\\d+)\\s*Mhz"), QRegularExpression::CaseInsensitiveOption).match(l);
        if (m.hasMatch()) {
            best = qMax(best, m.captured(1).toInt());
        }
    }
    return best;
}

inline int currentClock(const QString &file)
{
    for (const QString &l : readFile(file).split(QLatin1Char('\n'))) {
        if (l.trimmed().endsWith(QLatin1Char('*'))) {
            const auto m = QRegularExpression(QStringLiteral("(\\d+)\\s*Mhz"), QRegularExpression::CaseInsensitiveOption).match(l);
            if (m.hasMatch()) {
                return m.captured(1).toInt();
            }
        }
    }
    return 0;
}

inline QList<Row> softwareRows()
{
    const QStringList gl = glInfo();
    const QString mesa = mesaVersion();
    QString core = gl.value(1);
    core = core.section(QLatin1Char(' '), 0, 0);
    return {
        {tr7("Driver Packaging Version"), mesa.isEmpty() ? QString() : QStringLiteral("Mesa ") + mesa},
        {tr7("Catalyst Version"), tr7("14.9 (look), seven-ccc 1.0")},
        {tr7("Provider"), QStringLiteral("Mesa / Advanced Micro Devices, Inc.")},
        {tr7("Kernel Driver"), QStringLiteral("amdgpu, Linux ") + QSysInfo::kernelVersion()},
        {tr7("OpenGL Driver"), QStringLiteral("radeonsi")},
        {tr7("OpenGL Version"), core.isEmpty() ? gl.value(2) : core},
        {tr7("Vulkan Driver"), QStringLiteral("RADV")},
        {tr7("Vulkan Version"), vulkanVersion()},
        {tr7("Catalyst Control Center Version"), QStringLiteral("2014.0915.1813.32737")},
    };
}

inline QString memSize(qint64 bytes)
{
    return bytes > 0 ? tr7("%1 MB").arg(bytes / (1024 * 1024)) : QString();
}

inline QList<Row> hardwareRows(const Gpu &g)
{
    const QString dev = g.card + QStringLiteral("/device");
    QString bus;
    const QString speed = readFile(dev + QStringLiteral("/current_link_speed"));
    const QString width = readFile(dev + QStringLiteral("/current_link_width"));
    const QString maxSpeed = readFile(dev + QStringLiteral("/max_link_speed"));
    const auto gen = [](const QString &s) {
        return s.startsWith(QLatin1String("2.5")) ? 1 : s.startsWith(QLatin1String("5.0")) ? 2 : s.startsWith(QLatin1String("8.0")) ? 3 : s.startsWith(QLatin1String("16")) ? 4 : s.startsWith(QLatin1String("32")) ? 5 : 0;
    };
    if (!speed.isEmpty()) {
        bus = QStringLiteral("PCI Express %1.0 x%2").arg(gen(maxSpeed.isEmpty() ? speed : maxSpeed)).arg(width);
    }
    const qint64 vram = readFile(dev + QStringLiteral("/mem_info_vram_total")).toLongLong();
    const qint64 gtt = readFile(dev + QStringLiteral("/mem_info_gtt_total")).toLongLong();
    QString memType = g.memType;
    if (g.integrated) {
        memType = memType.isEmpty() ? tr7("Shared System Memory") : tr7("Shared System Memory (%1)").arg(memType);
    }
    const QString vramVendor = readFile(dev + QStringLiteral("/mem_info_vram_vendor"));
    if (!g.integrated && !vramVendor.isEmpty()) {
        QString v = vramVendor;
        v[0] = v.at(0).toUpper();
        memType = memType.isEmpty() ? v : QStringLiteral("%1 (%2)").arg(memType, v);
    }
    QString name = cardName(g);
    return {
        {tr7("Graphics Card Name"), name},
        {tr7("Graphics Chipset"), g.name},
        {tr7("Device ID"), g.deviceId},
        {tr7("Vendor ID"), QStringLiteral("1002")},
        {tr7("Subsystem ID"), g.subDevice},
        {tr7("Subsystem Vendor ID"), g.subVendor},
        {tr7("Revision ID"), g.revision},
        {tr7("Bus Type"), bus},
        {tr7("Current Bus Settings"), speed.isEmpty() ? QString() : QStringLiteral("PCI Express %1 GT/s x%2").arg(speed.section(QLatin1Char(' '), 0, 0), width)},
        {tr7("BIOS Version"), readFile(dev + QStringLiteral("/vbios_version"))},
        {tr7("Memory Type"), memType},
        {tr7("Memory Bit Rate"), g.memWidth && !g.integrated ? tr7("%1-bit").arg(g.memWidth) : QString()},
        {tr7("Memory Size"), memSize(vram)},
        {tr7("Total Available Graphics Memory"), memSize(vram + gtt)},
        {tr7("Core Clock"), maxClock(dev + QStringLiteral("/pp_dpm_sclk")) ? tr7("%1 MHz").arg(maxClock(dev + QStringLiteral("/pp_dpm_sclk"))) : QString()},
        {tr7("Memory Clock"), maxClock(dev + QStringLiteral("/pp_dpm_mclk")) ? tr7("%1 MHz").arg(maxClock(dev + QStringLiteral("/pp_dpm_mclk"))) : QString()},
        {tr7("PCI Address"), g.pci},
    };
}

/* ---------------------------------------------------------------- displays (kscreen-doctor) */

struct Mode {
    QString id;
    int w = 0, h = 0;
    double hz = 0;
};

struct Output {
    QString name;
    bool connected = false, enabled = false;
    int rotation = 1;  // 1 normal, 2 left (90° counter-clockwise), 4 inverted, 8 right
    QString currentMode;
    QList<Mode> modes;
    QString preferredMode;
    int mmW = 0, mmH = 0;
    int type = 0;      // 7 = panel (eDP / LVDS)
    bool builtIn() const { return type == 7 || name.startsWith(QLatin1String("eDP")) || name.startsWith(QLatin1String("LVDS")); }
    Mode mode(const QString &id) const
    {
        for (const Mode &m : modes) {
            if (m.id == id) {
                return m;
            }
        }
        return {};
    }
};

inline qint64 &outputsStamp()
{
    static qint64 t = 0;
    return t;
}

inline QList<Output> outputs()
{
    static QList<Output> cached;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (outputsStamp() && now - outputsStamp() < 3000) {
        return cached;
    }
    QList<Output> out;
    const QJsonObject root = QJsonDocument::fromJson(run(QStringLiteral("kscreen-doctor"), {QStringLiteral("-j")}).toUtf8()).object();
    for (const QJsonValue &v : root.value(QStringLiteral("outputs")).toArray()) {
        const QJsonObject o = v.toObject();
        Output x;
        x.name = o.value(QStringLiteral("name")).toString();
        x.connected = o.value(QStringLiteral("connected")).toBool();
        x.enabled = o.value(QStringLiteral("enabled")).toBool();
        x.rotation = o.value(QStringLiteral("rotation")).toInt(1);
        x.currentMode = o.value(QStringLiteral("currentModeId")).toString();
        x.type = o.value(QStringLiteral("type")).toInt();
        x.mmW = o.value(QStringLiteral("sizeMM")).toObject().value(QStringLiteral("width")).toInt();
        x.mmH = o.value(QStringLiteral("sizeMM")).toObject().value(QStringLiteral("height")).toInt();
        const QJsonArray pm = o.value(QStringLiteral("preferredModes")).toArray();
        x.preferredMode = pm.isEmpty() ? QString() : pm.first().toString();
        for (const QJsonValue &mv : o.value(QStringLiteral("modes")).toArray()) {
            const QJsonObject m = mv.toObject();
            x.modes << Mode{m.value(QStringLiteral("id")).toString(), m.value(QStringLiteral("size")).toObject().value(QStringLiteral("width")).toInt(),
                            m.value(QStringLiteral("size")).toObject().value(QStringLiteral("height")).toInt(), m.value(QStringLiteral("refreshRate")).toDouble()};
        }
        if (x.connected) {
            out << x;
        }
    }
    if (!out.isEmpty()) {
        cached = out;
        outputsStamp() = now;
    }
    return out;
}

inline QString rotationWord(int r)
{
    return r == 2 ? QStringLiteral("left") : r == 4 ? QStringLiteral("inverted") : r == 8 ? QStringLiteral("right") : QStringLiteral("normal");
}

inline bool kscreen(const QStringList &args)
{
    QProcess p;
    p.setProcessEnvironment(childEnv());
    p.start(QStringLiteral("kscreen-doctor"), args);
    p.waitForFinished(15000);
    outputsStamp() = 0;
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

/* ---------------------------------------------------------------- desktop colour: XRandR gamma ramps */

struct Color {
    // per channel 0 = all/red, 1 = green, 2 = blue
    double gamma[3] = {1, 1, 1};
    int brightness[3] = {0, 0, 0};   // -100 .. 100
    int contrast[3] = {100, 100, 100}; // 0 .. 200
    bool operator==(const Color &o) const
    {
        for (int i = 0; i < 3; ++i) {
            if (gamma[i] != o.gamma[i] || brightness[i] != o.brightness[i] || contrast[i] != o.contrast[i]) {
                return false;
            }
        }
        return true;
    }
    bool isDefault() const { return *this == Color(); }
};

/* value 0..1 of input x 0..1 for one channel, the way the Catalyst colour page combines its three sliders */
inline double ramp(const Color &c, int ch, double x)
{
    double v = std::pow(x, 1.0 / qMax(0.1, c.gamma[ch]));
    v = (v - 0.5) * c.contrast[ch] / 100.0 + 0.5 + c.brightness[ch] / 200.0;
    return qBound(0.0, v, 1.0);
}

/* colours are kept per display, not per connector name: the laptop panel is "eDP" on one boot and "eDP-1" on
 * another (it depends on which driver loads first), so every built-in panel shares one key */
inline QString colorKey(const QString &output)
{
    static const QRegularExpression builtin(QStringLiteral("^(eDP|LVDS|DSI)"));
    return builtin.match(output).hasMatch() ? QStringLiteral("builtin") : output;
}

/* the current name of an output saved under an older name ("eDP" saved, "eDP-1" now) */
inline QString currentOutputName(const QString &saved)
{
    QString match;
    for (const Output &o : outputs()) {
        if (o.name == saved) {
            return saved;
        }
        if (match.isEmpty() && colorKey(o.name) != o.name && colorKey(o.name) == colorKey(saved)) {
            match = o.name;
        }
    }
    return match.isEmpty() ? saved : match;
}

inline Color loadColor(const QString &output)
{
    QSettings &s = store();
    Color c;
    QString group = QStringLiteral("Color-") + colorKey(output);
    if (!s.childGroups().contains(group) && colorKey(output) == QLatin1String("builtin")) {
        // settings saved by older versions under the connector name
        for (const QString &g : s.childGroups()) {
            if (g.startsWith(QLatin1String("Color-eDP")) || g.startsWith(QLatin1String("Color-LVDS"))) {
                group = g;
                break;
            }
        }
    }
    s.beginGroup(group);
    for (int i = 0; i < 3; ++i) {
        c.gamma[i] = s.value(QStringLiteral("gamma%1").arg(i), 1.0).toDouble();
        c.brightness[i] = s.value(QStringLiteral("brightness%1").arg(i), 0).toInt();
        c.contrast[i] = s.value(QStringLiteral("contrast%1").arg(i), 100).toInt();
    }
    s.endGroup();
    return c;
}

inline void saveColor(const QString &output, const Color &c)
{
    QSettings &s = store();
    s.beginGroup(QStringLiteral("Color-") + colorKey(output));
    for (int i = 0; i < 3; ++i) {
        s.setValue(QStringLiteral("gamma%1").arg(i), c.gamma[i]);
        s.setValue(QStringLiteral("brightness%1").arg(i), c.brightness[i]);
        s.setValue(QStringLiteral("contrast%1").arg(i), c.contrast[i]);
    }
    s.endGroup();
    s.sync();
}

namespace X
{
using XID = unsigned long;
struct ScreenResources {
    unsigned long timestamp, configTimestamp;
    int ncrtc;
    XID *crtcs;
    int noutput;
    XID *outputs;
    int nmode;
    void *modes;
};
struct OutputInfo {
    unsigned long timestamp;
    XID crtc;
    char *name;
    int nameLen;
    unsigned long mmWidth, mmHeight;
    unsigned short connection, subpixel;
    // rest not needed
};
struct CrtcGamma {
    int size;
    unsigned short *red, *green, *blue;
};
}

/* Set the gamma ramp of the CRTC that drives the X11 output called name (kscreen output names match the
 * XRandR ones on X11). Returns false when not on X11 or XRandR is missing. */
inline bool applyColor(const QString &name, const Color &c)
{
    static void *x11 = dlopen("libX11.so.6", RTLD_LAZY);
    static void *xr = dlopen("libXrandr.so.2", RTLD_LAZY);
    if (!x11 || !xr || qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland")) {
        return false;
    }
    using OpenDisplay = void *(*)(const char *);
    using CloseDisplay = int (*)(void *);
    using DefaultRoot = X::XID (*)(void *);
    using Flush = int (*)(void *);
    using GetRes = X::ScreenResources *(*)(void *, X::XID);
    using FreeRes = void (*)(X::ScreenResources *);
    using GetOut = X::OutputInfo *(*)(void *, X::ScreenResources *, X::XID);
    using FreeOut = void (*)(X::OutputInfo *);
    using GammaSize = int (*)(void *, X::XID);
    using AllocGamma = X::CrtcGamma *(*)(int);
    using SetGamma = void (*)(void *, X::XID, X::CrtcGamma *);
    using FreeGamma = void (*)(X::CrtcGamma *);
    auto openDisplay = (OpenDisplay)dlsym(x11, "XOpenDisplay");
    auto closeDisplay = (CloseDisplay)dlsym(x11, "XCloseDisplay");
    auto defaultRoot = (DefaultRoot)dlsym(x11, "XDefaultRootWindow");
    auto flush = (Flush)dlsym(x11, "XFlush");
    auto getRes = (GetRes)dlsym(xr, "XRRGetScreenResourcesCurrent");
    auto freeRes = (FreeRes)dlsym(xr, "XRRFreeScreenResources");
    auto getOut = (GetOut)dlsym(xr, "XRRGetOutputInfo");
    auto freeOut = (FreeOut)dlsym(xr, "XRRFreeOutputInfo");
    auto gammaSize = (GammaSize)dlsym(xr, "XRRGetCrtcGammaSize");
    auto allocGamma = (AllocGamma)dlsym(xr, "XRRAllocGamma");
    auto setGamma = (SetGamma)dlsym(xr, "XRRSetCrtcGamma");
    auto freeGamma = (FreeGamma)dlsym(xr, "XRRFreeGamma");
    if (!openDisplay || !getRes || !getOut || !setGamma) {
        return false;
    }
    void *dpy = openDisplay(nullptr);
    if (!dpy) {
        return false;
    }
    bool done = false;
    X::ScreenResources *res = getRes(dpy, defaultRoot(dpy));
    for (int i = 0; res && i < res->noutput; ++i) {
        X::OutputInfo *o = getOut(dpy, res, res->outputs[i]);
        if (o && o->crtc && QString::fromLatin1(o->name, o->nameLen) == name) {
            const int n = gammaSize(dpy, o->crtc);
            if (n > 1) {
                X::CrtcGamma *g = allocGamma(n);
                for (int k = 0; k < n; ++k) {
                    const double x = double(k) / (n - 1);
                    g->red[k] = ushort(ramp(c, 0, x) * 65535 + 0.5);
                    g->green[k] = ushort(ramp(c, 1, x) * 65535 + 0.5);
                    g->blue[k] = ushort(ramp(c, 2, x) * 65535 + 0.5);
                }
                setGamma(dpy, o->crtc, g);
                freeGamma(g);
                done = true;
            }
        }
        if (o) {
            freeOut(o);
        }
    }
    if (res) {
        freeRes(res);
    }
    flush(dpy);
    closeDisplay(dpy);
    return done;
}

/* reapply the saved colours (autostart at login, --apply) */
inline void applySavedColors()
{
    for (const Output &o : outputs()) {
        const Color c = loadColor(o.name);
        if (!c.isDefault()) {
            applyColor(o.name, c);
        }
    }
}

inline void setLoginApply(bool on)
{
    const QString path = QDir::homePath() + QStringLiteral("/.config/autostart/seven-ccc-apply.desktop");
    if (!on) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write("[Desktop Entry]\nType=Application\nName=Catalyst Control Center: desktop color\nExec=seven-ccc --apply\nNoDisplay=true\nX-KDE-autostart-phase=2\n");
    }
}

/* ---------------------------------------------------------------- PowerPlay: power-profiles-daemon */

inline QString powerProfile()
{
    return run(QStringLiteral("powerprofilesctl"), {QStringLiteral("get")}).trimmed();
}

inline QStringList powerProfiles()
{
    QStringList out;
    for (const QString &l : run(QStringLiteral("powerprofilesctl"), {QStringLiteral("list")}).split(QLatin1Char('\n'))) {
        const auto m = QRegularExpression(QStringLiteral("^\\*?\\s*([a-z-]+):$")).match(l.trimmed());
        if (m.hasMatch()) {
            out << m.captured(1);
        }
    }
    return out;
}

inline bool setPowerProfile(const QString &p)
{
    QProcess q;
    q.start(QStringLiteral("powerprofilesctl"), {QStringLiteral("set"), p});
    q.waitForFinished(8000);
    return q.exitCode() == 0;
}

inline bool onBattery()
{
    const QDir d(QStringLiteral("/sys/class/power_supply"));
    for (const QString &s : d.entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot)) {
        if (readFile(d.absoluteFilePath(s) + QStringLiteral("/type")) == QLatin1String("Mains")) {
            return readFile(d.absoluteFilePath(s) + QStringLiteral("/online")) == QLatin1String("0");
        }
    }
    return false;
}

/* ---------------------------------------------------------------- OpenGL vsync: Mesa driconf (~/.drirc) */

// 0 always off, 1 off unless the application asks, 2 on unless the application asks, 3 always on
// tag: the panel that owns the block (seven-ccc for radeonsi, seven-igfx for iris / i965)
inline int vblankMode(const QString &tag = QStringLiteral("seven-ccc"))
{
    const QString s = readFile(QDir::homePath() + QStringLiteral("/.drirc"));
    const auto m = QRegularExpression(QStringLiteral("<!-- %1 -->.*?vblank_mode\"\\s+value=\"(\\d)\"").arg(tag), QRegularExpression::DotMatchesEverythingOption).match(s);
    return m.hasMatch() ? m.captured(1).toInt() : 1;
}

inline bool setVblankMode(int mode, const QString &tag = QStringLiteral("seven-ccc"), const QStringList &drivers = {QStringLiteral("radeonsi")})
{
    const QString path = QDir::homePath() + QStringLiteral("/.drirc");
    QString s = readFile(path);
    const QRegularExpression block(QStringLiteral("\\s*<!-- %1 -->.*?<!-- /%1 -->").arg(tag), QRegularExpression::DotMatchesEverythingOption);
    s.remove(block);
    QString devs;
    for (const QString &d : drivers) {
        devs += QStringLiteral("    <device driver=\"%1\">\n        <application name=\"Default\">\n"
                               "            <option name=\"vblank_mode\" value=\"%2\" />\n        </application>\n    </device>\n")
                    .arg(d)
                    .arg(mode);
    }
    const QString mine = QStringLiteral("\n    <!-- %1 -->\n%2    <!-- /%1 -->").arg(tag, devs);
    if (s.isEmpty()) {
        s = QStringLiteral("<driconf>%1\n</driconf>\n").arg(mine);
    } else if (s.contains(QStringLiteral("</driconf>"))) {
        s.replace(s.lastIndexOf(QStringLiteral("</driconf>")), 10, mine + QStringLiteral("\n</driconf>"));
    } else {
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(s.toUtf8());
    return true;
}

/* ---------------------------------------------------------------- switchable graphics (PowerXpress) on Mesa
 * Per program: Mesa driconf "device_id" for the loader (~/.drirc, OpenGL), and for programs started from a
 * menu entry also the freedesktop key PrefersNonDefaultGPU in a local copy of the .desktop file (then Plasma
 * starts them with DRI_PRIME, which covers Vulkan too). Assignments live in ~/.config/seven-cccrc
 * [Switchable]: key = program (executable path or desktop file id), value "perf" / "save".
 * Global mode: 0 optimal power saving (unassigned -> power-saving GPU), 1 optimal performance (unassigned ->
 * high-performance GPU, OpenGL), 2 force power-saving GPU for everything.
 */

struct PxApp {
    QString key;     // /usr/bin/foo or org.kde.foo.desktop
    QString name, exec, icon;
    QString state;   // "perf", "save" or empty (not assigned)
};

inline QString desktopValue(const QString &file, const QString &key)
{
    QSettings d(file, QSettings::IniFormat);
    d.beginGroup(QStringLiteral("Desktop Entry"));
    QString uk = d.value(key + QStringLiteral("[uk]")).toString();
    return uk.isEmpty() ? d.value(key).toString() : uk;
}

inline QString desktopPath(const QString &id)
{
    return QStandardPaths::locate(QStandardPaths::ApplicationsLocation, id);
}

/* the executable a desktop entry starts (first word of Exec without env assignments) */
inline QString desktopExec(const QString &file)
{
    QString e = desktopValue(file, QStringLiteral("Exec"));
    QStringList w = QProcess::splitCommand(e);
    while (!w.isEmpty() && (w.first() == QLatin1String("env") || w.first().contains(QLatin1Char('=')))) {
        w.removeFirst();
    }
    if (w.isEmpty()) {
        return {};
    }
    const QString found = QStandardPaths::findExecutable(w.first());
    return found.isEmpty() ? w.first() : found;
}

inline PxApp pxApp(const QString &key)
{
    PxApp a;
    a.key = key;
    if (key.endsWith(QLatin1String(".desktop"))) {
        const QString f = desktopPath(key);
        a.name = desktopValue(f, QStringLiteral("Name"));
        a.icon = desktopValue(f, QStringLiteral("Icon"));
        a.exec = desktopExec(f);
    } else {
        a.exec = key;
        a.name = QFileInfo(key).fileName();
    }
    if (a.name.isEmpty()) {
        a.name = QFileInfo(a.exec).fileName();
    }
    a.state = store().value(QStringLiteral("Switchable/") + QString(key).replace(QLatin1Char('/'), QLatin1Char('|'))).toString();
    // the binary really seen running (a menu entry may start a wrapper script: google-chrome -> chrome)
    const QString seen = store().value(QStringLiteral("SwitchableExe/") + QString(key).replace(QLatin1Char('/'), QLatin1Char('|'))).toString();
    if (!seen.isEmpty()) {
        a.exec = seen;
    }
    return a;
}

inline QList<PxApp> pxAssigned()
{
    QList<PxApp> l;
    store().beginGroup(QStringLiteral("Switchable"));
    const QStringList keys = store().childKeys();
    store().endGroup();
    for (const QString &k : keys) {
        l << pxApp(QString(k).replace(QLatin1Char('|'), QLatin1Char('/')));
    }
    return l;
}

inline int pxGlobal()
{
    return store().value(QStringLiteral("SwitchableGlobal"), 0).toInt();
}

/* a program's menu entry gets PrefersNonDefaultGPU=true/false in a local copy, or loses our key (state empty) */
inline void setDesktopGpu(const QString &id, const QString &state)
{
    const QString local = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + QLatin1Char('/') + id;
    const QString src = desktopPath(id);
    if (src.isEmpty()) {
        return;
    }
    const QString text = readFile(src);
    const bool ours = text.contains(QStringLiteral("X-Win7-CCC=")); // PrefersNonDefaultGPU there was written by us
    const bool copy = text.contains(QStringLiteral("X-Win7-CCC-Copy=true")); // the whole local file is ours
    QStringList lines;
    for (const QString &l : text.split(QLatin1Char('\n'))) {
        if (l.startsWith(QLatin1String("X-Win7-CCC")) || (l.startsWith(QLatin1String("PrefersNonDefaultGPU=")) && (ours || !state.isEmpty()))) {
            continue;
        }
        lines << l;
    }
    if (state.isEmpty()) {
        if (copy) {
            QFile::remove(local);
            return;
        }
        if (!ours || src != local) {
            return;
        }
    } else {
        const int at = lines.indexOf(QStringLiteral("[Desktop Entry]"));
        if (at < 0) {
            return;
        }
        QStringList ins{QStringLiteral("PrefersNonDefaultGPU=%1").arg(state == QLatin1String("perf") ? QStringLiteral("true") : QStringLiteral("false")),
                        QStringLiteral("X-Win7-CCC=switchable")};
        if (copy || src != local) {
            ins << QStringLiteral("X-Win7-CCC-Copy=true");
        }
        for (int k = ins.size() - 1; k >= 0; --k) {
            lines.insert(at + 1, ins.at(k));
        }
    }
    QDir().mkpath(QFileInfo(local).path());
    QFile f(local);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(lines.join(QLatin1Char('\n')).toUtf8());
    }
}

inline QString xmlEscape(QString s)
{
    return s.replace(QLatin1Char('&'), QStringLiteral("&amp;")).replace(QLatin1Char('"'), QStringLiteral("&quot;")).replace(QLatin1Char('<'), QStringLiteral("&lt;"));
}

/* write ~/.drirc block "seven-ccc-px" and the menu entries for the current assignments */
inline bool applySwitchable()
{
    const Switchable sw = switchable();
    const QString path = QDir::homePath() + QStringLiteral("/.drirc");
    QString s = readFile(path);
    s.remove(QRegularExpression(QStringLiteral("\\s*<!-- seven-ccc-px -->.*?<!-- /seven-ccc-px -->"), QRegularExpression::DotMatchesEverythingOption));
    const int global = pxGlobal();
    QString apps;
    auto entry = [](const QString &name, const QString &exe, const QString &tag) {
        const QString exeAttr = exe.isEmpty() ? QString() : QStringLiteral(" executable=\"%1\"").arg(xmlEscape(QFileInfo(exe).fileName()));
        return QStringLiteral("        <application name=\"%1\"%2>\n            <option name=\"device_id\" value=\"%3\" />\n        </application>\n").arg(xmlEscape(name), exeAttr, tag);
    };
    if (sw.valid()) {
        // the global entry first: later (per-program) entries override it
        if (global == 1) {
            apps += entry(QStringLiteral("Default"), QString(), sw.performance.primeTag());
        } else if (global == 2) {
            apps += entry(QStringLiteral("Default"), QString(), sw.powerSaving.primeTag());
        }
        for (const PxApp &a : pxAssigned()) {
            if (a.exec.isEmpty() || a.state.isEmpty()) {
                continue;
            }
            const bool perf = a.state == QLatin1String("perf") && global != 2;
            apps += entry(a.name, a.exec, perf ? sw.performance.primeTag() : sw.powerSaving.primeTag());
        }
    }
    if (!apps.isEmpty()) {
        const QString mine = QStringLiteral("\n    <!-- seven-ccc-px -->\n    <device driver=\"loader\">\n%1    </device>\n    <!-- /seven-ccc-px -->").arg(apps);
        if (s.isEmpty()) {
            s = QStringLiteral("<driconf>%1\n</driconf>\n").arg(mine);
        } else if (s.contains(QStringLiteral("</driconf>"))) {
            s.replace(s.lastIndexOf(QStringLiteral("</driconf>")), 10, mine + QStringLiteral("\n</driconf>"));
        } else {
            return false;
        }
    }
    if (!s.isEmpty()) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        f.write(s.toUtf8());
    }
    for (const PxApp &a : pxAssigned()) {
        if (a.key.endsWith(QLatin1String(".desktop"))) {
            setDesktopGpu(a.key, global == 2 && a.state == QLatin1String("perf") ? QStringLiteral("save") : a.state);
        }
    }
    return true;
}

inline void setPxState(const QString &key, const QString &state)
{
    const QString k = QStringLiteral("Switchable/") + QString(key).replace(QLatin1Char('/'), QLatin1Char('|'));
    if (state.isEmpty()) {
        store().setValue(k, QString()); // kept in the list as "not assigned"
    } else {
        store().setValue(k, state);
    }
}

inline void setPxExe(const QString &key, const QString &exe)
{
    store().setValue(QStringLiteral("SwitchableExe/") + QString(key).replace(QLatin1Char('/'), QLatin1Char('|')), exe);
}

inline void removePx(const QString &key)
{
    store().remove(QStringLiteral("SwitchableExe/") + QString(key).replace(QLatin1Char('/'), QLatin1Char('|')));
    if (key.endsWith(QLatin1String(".desktop"))) {
        setDesktopGpu(key, QString());
    }
    store().remove(QStringLiteral("Switchable/") + QString(key).replace(QLatin1Char('/'), QLatin1Char('|')));
}

/* processes of this user that have a GPU open: pid, program, PCI address (DRM fdinfo "drm-pdev") */
struct GpuProcess {
    int pid = 0;
    QString name, exe, pci;
};

inline QList<GpuProcess> gpuProcesses()
{
    QList<GpuProcess> out;
    const QDir proc(QStringLiteral("/proc"));
    const uint me = getuid();
    for (const QString &p : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool num = false;
        const int pid = p.toInt(&num);
        if (!num || QFileInfo(proc.absoluteFilePath(p)).ownerId() != me) {
            continue;
        }
        const QDir fi(proc.absoluteFilePath(p) + QStringLiteral("/fdinfo"));
        QSet<QString> seen;
        for (const QString &fd : fi.entryList(QDir::Files)) {
            const QString t = readFile(fi.absoluteFilePath(fd));
            const int i = t.indexOf(QStringLiteral("drm-pdev:"));
            if (i < 0) {
                continue;
            }
            const QString pci = t.mid(i + 9).section(QLatin1Char('\n'), 0, 0).trimmed();
            if (seen.contains(pci)) {
                continue;
            }
            seen.insert(pci);
            GpuProcess g;
            g.pid = pid;
            g.pci = pci;
            g.name = readFile(proc.absoluteFilePath(p) + QStringLiteral("/comm"));
            g.exe = QFile::symLinkTarget(proc.absoluteFilePath(p) + QStringLiteral("/exe"));
            out << g;
        }
    }
    return out;
}

/* ---------------------------------------------------------------- AMD OverDrive (graphics cards) on amdgpu sysfs
 * Readings: gpu_busy_percent, pp_dpm_sclk / pp_dpm_mclk (current *), hwmon temp1 (edge), fan1_input, pwm1,
 * power1_average / power1_input. Settings (root): pp_od_clk_voltage (needs OverDrive in amdgpu.ppfeaturemask,
 * bit 0x4000): "s <i> <MHz> [mV]", "m <i> <MHz> [mV]", "c" commit, "r" reset; hwmon pwm1_enable (1 manual,
 * 2 auto) + pwm1 (0-255); power1_cap (µW); gpu_od/fan_ctrl/fan_target_temperature (RDNA3) "<°C>" + "c".
 * Written through `pkexec seven-ccc --od-write <file> <line> ...`, which accepts only these files. The requested
 * settings are also kept in /etc/seven-ccc/overdrive.conf and restored at boot by seven-ccc-overdrive.service.
 */

struct OdLevel {
    int index = 0, mhz = 0, mv = 0;
};

struct OdInfo {
    QString dev, hwmon;
    // readings
    int activity = -1, sclk = 0, mclk = 0, temp = -1000, fanRpm = -1, fanPct = -1;
    double powerW = -1;
    // clocks
    bool odClocks = false;
    QList<OdLevel> sclkLevels, mclkLevels;
    int sclkMin = 0, sclkMax = 0, mclkMin = 0, mclkMax = 0;
    // fan, power, temperature
    bool fanControl = false, fanManual = false;
    int fanPwmPct = -1;
    qint64 capUw = 0, capMinUw = 0, capMaxUw = 0, capDefUw = 0;
    bool targetTemp = false;
    int tempTarget = 0, tempMin = 0, tempMax = 0;
};

inline QString hwmonOf(const QString &dev)
{
    const QDir d(dev + QStringLiteral("/hwmon"));
    const QStringList l = d.entryList({QStringLiteral("hwmon*")}, QDir::AllEntries | QDir::System);
    return l.isEmpty() ? QString() : d.absoluteFilePath(l.first());
}

inline int odMhz(const QString &s)
{
    const auto m = QRegularExpression(QStringLiteral("(\\d+)\\s*mhz"), QRegularExpression::CaseInsensitiveOption).match(s);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

inline void parseOdClocks(const QString &text, OdInfo &o)
{
    QString sec;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString l = raw.trimmed();
        if (l.startsWith(QLatin1String("OD_"))) {
            sec = l.section(QLatin1Char(':'), 0, 0).trimmed();
            continue;
        }
        if (sec == QLatin1String("OD_SCLK") || sec == QLatin1String("OD_MCLK")) {
            const auto m = QRegularExpression(QStringLiteral("^(\\d+):\\s*(\\d+)\\s*mhz(?:\\s+(\\d+)\\s*mv)?"), QRegularExpression::CaseInsensitiveOption).match(l);
            if (m.hasMatch()) {
                OdLevel v{m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()};
                (sec == QLatin1String("OD_SCLK") ? o.sclkLevels : o.mclkLevels) << v;
            }
        } else if (sec == QLatin1String("OD_RANGE")) {
            const auto m = QRegularExpression(QStringLiteral("^(SCLK|MCLK):\\s*(\\d+)\\s*mhz\\s+(\\d+)\\s*mhz"), QRegularExpression::CaseInsensitiveOption).match(l);
            if (m.hasMatch()) {
                if (m.captured(1) == QLatin1String("SCLK")) {
                    o.sclkMin = m.captured(2).toInt();
                    o.sclkMax = m.captured(3).toInt();
                } else {
                    o.mclkMin = m.captured(2).toInt();
                    o.mclkMax = m.captured(3).toInt();
                }
            }
        }
    }
    o.odClocks = !o.sclkLevels.isEmpty() && o.sclkMax > 0;
}

inline OdInfo odInfo(const Gpu &g)
{
    OdInfo o;
    o.dev = g.card + QStringLiteral("/device");
    o.hwmon = hwmonOf(o.dev);
    bool ok = false;
    const int busy = readFile(o.dev + QStringLiteral("/gpu_busy_percent")).toInt(&ok);
    o.activity = ok ? busy : -1;
    o.sclk = currentClock(o.dev + QStringLiteral("/pp_dpm_sclk"));
    o.mclk = currentClock(o.dev + QStringLiteral("/pp_dpm_mclk"));
    if (!o.hwmon.isEmpty()) {
        // the "edge" sensor (temp1 on every amdgpu)
        const int t = readFile(o.hwmon + QStringLiteral("/temp1_input")).toInt(&ok);
        if (ok) {
            o.temp = qRound(t / 1000.0);
        }
        const int rpm = readFile(o.hwmon + QStringLiteral("/fan1_input")).toInt(&ok);
        o.fanRpm = ok ? rpm : -1;
        const int pwm = readFile(o.hwmon + QStringLiteral("/pwm1")).toInt(&ok);
        if (ok) {
            o.fanPct = qRound(pwm * 100.0 / 255);
            o.fanPwmPct = o.fanPct;
        }
        o.fanControl = QFile::exists(o.hwmon + QStringLiteral("/pwm1_enable")) && ok;
        o.fanManual = readFile(o.hwmon + QStringLiteral("/pwm1_enable")) == QLatin1String("1");
        QString pw = readFile(o.hwmon + QStringLiteral("/power1_average"));
        if (pw.isEmpty()) {
            pw = readFile(o.hwmon + QStringLiteral("/power1_input"));
        }
        if (!pw.isEmpty()) {
            o.powerW = pw.toLongLong() / 1e6;
        }
        o.capUw = readFile(o.hwmon + QStringLiteral("/power1_cap")).toLongLong();
        o.capMinUw = readFile(o.hwmon + QStringLiteral("/power1_cap_min")).toLongLong();
        o.capMaxUw = readFile(o.hwmon + QStringLiteral("/power1_cap_max")).toLongLong();
        o.capDefUw = readFile(o.hwmon + QStringLiteral("/power1_cap_default")).toLongLong();
        if (!o.capDefUw) {
            o.capDefUw = o.capUw;
        }
    }
    parseOdClocks(readFile(o.dev + QStringLiteral("/pp_od_clk_voltage")), o);
    // RDNA3: "FAN_TARGET_TEMPERATURE:\n95\nOD_RANGE:\nTARGET_TEMPERATURE: 25 110"
    const QString tt = readFile(o.dev + QStringLiteral("/gpu_od/fan_ctrl/fan_target_temperature"));
    const auto tm = QRegularExpression(QStringLiteral("FAN_TARGET_TEMPERATURE:\\s*(\\d+).*TARGET_TEMPERATURE:\\s*(\\d+)\\s+(\\d+)"), QRegularExpression::DotMatchesEverythingOption).match(tt);
    if (tm.hasMatch()) {
        o.targetTemp = true;
        o.tempTarget = tm.captured(1).toInt();
        o.tempMin = tm.captured(2).toInt();
        o.tempMax = tm.captured(3).toInt();
    }
    return o;
}

/* graphics cards OverDrive works on: AMD, not integrated */
inline QList<Gpu> overdriveGpus()
{
    QList<Gpu> l;
    for (const Gpu &g : amdGpus()) {
        if (!g.integrated) {
            l << g;
        }
    }
    return l;
}

/* the files --od-write may touch (relative to /sys/class/drm, or the test tree) */
inline bool odWritable(const QString &path)
{
    static const QRegularExpression ok(QStringLiteral("^card\\d+/device/(pp_od_clk_voltage|hwmon/hwmon\\d+/(pwm1|pwm1_enable|power1_cap)|gpu_od/fan_ctrl/fan_target_temperature)$"));
    const QString root = drmRoot() + QLatin1Char('/');
    return path.startsWith(root) && !path.contains(QStringLiteral("..")) && ok.match(path.mid(root.size())).hasMatch();
}

/* write each "file line" pair in order (each line its own write, as amdgpu expects); 0 = all fine */
inline int odWriteDirect(const QStringList &pairs)
{
    int bad = 0;
    for (int i = 0; i + 1 < pairs.size(); i += 2) {
        if (!odWritable(pairs.at(i))) {
            ++bad;
            continue;
        }
        QFile f(pairs.at(i));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered) || f.write(pairs.at(i + 1).toUtf8() + '\n') < 0) {
            ++bad;
        }
    }
    return bad;
}

inline QString odConfPath()
{
    const QString test = qEnvironmentVariable("WIN7_CCC_ETC"); // tests only
    return (test.isEmpty() ? QStringLiteral("/etc/seven-ccc") : test) + QStringLiteral("/overdrive.conf");
}

/* as root: the writes, then (with save) /etc/seven-ccc/overdrive.conf section [pci] + the boot service */
inline bool odWrite(const QStringList &pairs, const QString &pci, bool persist)
{
    if (!qEnvironmentVariableIsEmpty("WIN7_CCC_SYSFS")) {
        return odWriteDirect(pairs) == 0; // test tree: no root needed
    }
    QStringList args{QCoreApplication::applicationFilePath(), QStringLiteral("--od-write"), pci, persist ? QStringLiteral("1") : QStringLiteral("0")};
    args += pairs;
    QProcess p;
    p.start(QStringLiteral("pkexec"), args);
    p.waitForFinished(120000);
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

/* "card2/device/hwmon/hwmon5/pwm1" -> "hwmon/pwm1": stable across boots (card and hwmon numbers are not) */
inline QString odSubPath(const QString &path)
{
    QString sub = path.section(QStringLiteral("/device/"), 1);
    sub.replace(QRegularExpression(QStringLiteral("^hwmon/hwmon\\d+/")), QStringLiteral("hwmon/"));
    return sub;
}

inline QString odResolve(const QString &pci, const QString &sub)
{
    for (const Gpu &g : gpus()) {
        if (g.pci == pci) {
            const QString dev = g.card + QStringLiteral("/device");
            return sub.startsWith(QLatin1String("hwmon/")) ? hwmonOf(dev) + sub.mid(5) : dev + QLatin1Char('/') + sub;
        }
    }
    return {};
}

/* root side of --od-write: pci persist file line ... */
inline int odWriteMain(const QStringList &args)
{
    if (args.size() < 2) {
        return 2;
    }
    const QString pci = args.at(0);
    if (!QRegularExpression(QStringLiteral("^[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-9a-f]$")).match(pci).hasMatch()) {
        return 3;
    }
    const bool persist = args.at(1) == QLatin1String("1");
    const QStringList pairs = args.mid(2);
    const int bad = odWriteDirect(pairs);
    // keep the requested writes for the next boot; a reset forgets the card
    QDir().mkpath(QFileInfo(odConfPath()).path());
    QSettings conf(odConfPath(), QSettings::IniFormat);
    const QString group = QString(pci).replace(QLatin1Char(':'), QLatin1Char('_'));
    conf.remove(group);
    if (persist) {
        QStringList keep;
        for (int i = 0; i + 1 < pairs.size(); i += 2) {
            if (odWritable(pairs.at(i)) && pairs.at(i + 1) != QLatin1String("r")) {
                keep << odSubPath(pairs.at(i)) << pairs.at(i + 1);
            }
        }
        conf.setValue(group + QStringLiteral("/pci"), pci);
        conf.setValue(group + QStringLiteral("/writes"), keep);
    }
    conf.sync();
    const QString unit = QStringLiteral("/etc/systemd/system/seven-ccc-overdrive.service");
    if (persist && !QFile::exists(unit) && qEnvironmentVariableIsEmpty("WIN7_CCC_ETC")) {
        QFile u(unit);
        if (u.open(QIODevice::WriteOnly)) {
            u.write(QStringLiteral("[Unit]\nDescription=AMD Catalyst Control Center: restore Graphics OverDrive settings\nAfter=systemd-modules-load.service\n\n"
                                   "[Service]\nType=oneshot\nExecStart=%1 --od-restore\n\n[Install]\nWantedBy=multi-user.target\n")
                        .arg(QCoreApplication::applicationFilePath())
                        .toUtf8());
            u.close();
            QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("daemon-reload")});
            QProcess::execute(QStringLiteral("systemctl"), {QStringLiteral("enable"), QStringLiteral("seven-ccc-overdrive.service")});
        }
    }
    return bad ? 1 : 0;
}

/* boot: replay the saved writes of every card still present at its PCI address */
inline int odRestoreMain()
{
    QSettings conf(odConfPath(), QSettings::IniFormat);
    int bad = 0;
    for (const QString &group : conf.childGroups()) {
        const QString pci = conf.value(group + QStringLiteral("/pci")).toString();
        const QStringList keep = conf.value(group + QStringLiteral("/writes")).toStringList();
        QStringList pairs;
        for (int i = 0; i + 1 < keep.size(); i += 2) {
            const QString path = odResolve(pci, keep.at(i));
            if (!path.isEmpty()) {
                pairs << path << keep.at(i + 1);
            }
        }
        bad += odWriteDirect(pairs);
    }
    return bad ? 1 : 0;
}

} // namespace Ccc
