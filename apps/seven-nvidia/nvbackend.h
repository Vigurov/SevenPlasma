#pragma once
#include "win7i18n.h"
/*
 * What the NVIDIA Control Panel can change on Linux, and where it is kept:
 *  - 3D settings: the driver's application profiles, ~/.nv/nvidia-application-profiles-rc. One profile for
 *    every program with its own settings ("Win7CP: <exe>", matched by process name) and a global one that
 *    matches every other process (a list of "not procname" patterns), so the two never overlap.
 *    They are read by OpenGL programs only; Vulkan / DXVK (Proton) games keep their own settings.
 *  - preferred graphics processor: PrefersNonDefaultGPU in the program's .desktop file (a copy in
 *    ~/.local/share/applications); Plasma then starts it with the NVIDIA PRIME offload environment.
 *  - power management mode: the GPU's PowerMizer mode (nvidia-settings), applied again at login.
 * Everything else the Windows panel has is shown greyed out as "not supported".
 */
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace Nv
{
/* ---------------- 3D settings ---------------- */

struct Choice {
    QString label;
    QJsonValue value; // undefined: no key in the profile (the application decides)
};

struct Feature {
    QString id;
    QString label;
    QString key; // application profile key, empty when Linux has no counterpart
    QList<Choice> choices;
    int def = 0;
    QString description;
    QString usage;
    bool globalOnly = false; // per program: "not supported for this application"
    bool supported() const
    {
        return !key.isEmpty() || id == QLatin1String("power") || id == QLatin1String("cuda");
    }
};

inline const QList<Feature> &features()
{
    static const QList<Feature> list = [] {
        const QJsonValue none(QJsonValue::Undefined);
        QList<Feature> l;
        l.append({QStringLiteral("ao"), tr7("Ambient Occlusion"), {},
                  {{tr7("Off"), none}, {tr7("Performance"), none}, {tr7("Quality"), none}}, 0,
                  tr7("Ambient occlusion makes scenes more realistic by shading corners and places where objects touch."),
                  tr7("The NVIDIA driver for Linux does not have this setting.")});
        l.append({QStringLiteral("aniso"), tr7("Anisotropic filtering"), QStringLiteral("GLLogMaxAniso"),
                  {{tr7("Application-controlled"), none}, {tr7("Off"), 0}, {QStringLiteral("2x"), 1}, {QStringLiteral("4x"), 2},
                   {QStringLiteral("8x"), 3}, {QStringLiteral("16x"), 4}},
                  0,
                  tr7("Anisotropic filtering improves the clarity of textures on surfaces going away from the camera, such as a road or a floor."),
                  tr7("Higher values make textures sharper but lower performance slightly. Applies to OpenGL programs.")});
        l.append({QStringLiteral("fxaa"), tr7("Antialiasing - FXAA"), {}, {{tr7("Off"), none}, {tr7("On"), none}}, 0,
                  tr7("FXAA is a fast antialiasing method that works on the finished image."),
                  tr7("On this laptop the screen is connected to the integrated graphics, so the NVIDIA driver cannot turn FXAA on.")});
        l.append({QStringLiteral("aamode"), tr7("Antialiasing - Mode"), QStringLiteral("GLFSAAMode"),
                  {{tr7("Application-controlled"), none}, {tr7("Override any application setting"), none}}, 0,
                  tr7("Specifies how antialiasing is applied to 3D programs."),
                  tr7("Choose \"Override any application setting\" to set the antialiasing level in the Antialiasing - Setting row.")});
        l.append({QStringLiteral("aaset"), tr7("Antialiasing - Setting"), QStringLiteral("GLFSAAMode"),
                  {{tr7("Application-controlled"), none}, {tr7("Off"), 0}, {QStringLiteral("2x"), 1}, {QStringLiteral("4x"), 5},
                   {QStringLiteral("8x"), 10}, {QStringLiteral("16x"), 12}},
                  0, tr7("Antialiasing reduces jaggedness (\"jaggies\") on the edges of objects."),
                  tr7("Higher values give smoother edges but lower performance. Applies to OpenGL programs.")});
        l.append({QStringLiteral("aatr"), tr7("Antialiasing - Transparency"), {}, {{tr7("Off"), none}}, 0,
                  tr7("Smooths the edges of transparent textures, such as leaves or fences."), tr7("The NVIDIA driver for Linux does not have this setting.")});
        l.append({QStringLiteral("cuda"), tr7("CUDA - GPUs"), {}, {{tr7("All"), none}}, 0,
                  tr7("Specifies which graphics processors CUDA programs can use."),
                  tr7("This computer has one NVIDIA graphics processor, and CUDA programs use it.")});
        l.append({QStringLiteral("frames"), tr7("Maximum pre-rendered frames"), {},
                  {{tr7("Use the 3D application setting"), none}}, 0,
                  tr7("Limits the number of frames the processor prepares in advance."), tr7("The NVIDIA driver for Linux does not set this in program profiles.")});
        l.append({QStringLiteral("mixed"), tr7("Multi-display/mixed-GPU acceleration"), {},
                  {{tr7("Multiple display performance mode"), none}}, 0, tr7("Controls how OpenGL works with several displays."),
                  tr7("The NVIDIA driver for Linux does not have this setting.")});
        Feature power{QStringLiteral("power"), tr7("Power management mode"), {},
                      {{tr7("Optimal power"), 2}, {tr7("Adaptive"), 0},
                       {tr7("Prefer maximum performance"), 1}},
                      0,
                      tr7("Specifies how the graphics processor changes clocks depending on the load."),
                      tr7("\"Prefer maximum performance\" keeps high clocks, but increases power consumption and heat.")};
        power.globalOnly = true;
        l.append(power);
        l.append({QStringLiteral("shadercache"), tr7("Shader Cache"), QStringLiteral("GLShaderDiskCache"),
                  {{tr7("On"), none}, {tr7("Off"), false}}, 0,
                  tr7("Stores compiled shaders on the disk so games start faster the next time."),
                  tr7("Recommended to keep on.")});
        l.append({QStringLiteral("tfaso"), tr7("Texture filtering - Anisotropic sample optimization"), {}, {{tr7("Off"), none}}, 0,
                  tr7("Limits the number of anisotropic filtering samples."), tr7("The NVIDIA driver for Linux does not have this setting.")});
        l.append({QStringLiteral("tflod"), tr7("Texture filtering - Negative LOD bias"), {}, {{tr7("Allow"), none}}, 0,
                  tr7("Controls the sharpness of textures."), tr7("The NVIDIA driver for Linux does not have this setting.")});
        l.append({QStringLiteral("tfq"), tr7("Texture filtering - Quality"), {}, {{tr7("Quality"), none}}, 0,
                  tr7("Sets the balance between quality and speed of texture filtering."), tr7("The NVIDIA driver for Linux does not have this setting.")});
        l.append({QStringLiteral("tftri"), tr7("Texture filtering - Trilinear optimization"), {}, {{tr7("Off"), none}}, 0,
                  tr7("Replaces trilinear filtering with a faster one."), tr7("The NVIDIA driver for Linux does not have this setting.")});
        l.append({QStringLiteral("threaded"), tr7("Threaded optimization"), QStringLiteral("GLThreadedOptimizations"),
                  {{tr7("Auto"), none}, {tr7("On"), true}, {tr7("Off"), false}}, 0,
                  tr7("Lets OpenGL programs use several processor cores."),
                  tr7("Can speed up games that load the processor heavily. Applies to OpenGL programs.")});
        l.append({QStringLiteral("triple"), tr7("Triple buffering"), {}, {{tr7("Off"), none}}, 0,
                  tr7("Adds a third frame buffer when vertical sync is on."),
                  tr7("On Linux this setting is only set for the whole X server.")});
        l.append({QStringLiteral("vsync"), tr7("Vertical sync"), QStringLiteral("GLSyncToVblank"),
                  {{tr7("Use the 3D application setting"), none}, {tr7("Force on"), true},
                   {tr7("Force off"), false}},
                  0, tr7("Synchronizes frames with the refresh rate of the screen so there is no tearing."),
                  tr7("Turn it off for the highest frame rate; turn it on to remove tearing. Applies to OpenGL programs.")});
        l.append({QStringLiteral("vr"), tr7("Virtual Reality pre-rendered frames"), {}, {{QStringLiteral("1"), none}}, 0,
                  tr7("The number of frames prepared in advance for virtual reality headsets."),
                  tr7("The NVIDIA driver for Linux does not have this setting.")});
        return l;
    }();
    return list;
}

inline const Feature *feature(const QString &id)
{
    for (const Feature &f : features()) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

/** Choice indices of the features that differ from the default; per program -1 = the global setting */
using Values = QMap<QString, int>;

/* ---------------- graphics processor ---------------- */

enum Gpu { GpuGlobal = -1, GpuAuto = 0, GpuNvidia = 1, GpuIntegrated = 2 };

/* ---------------- programs ---------------- */

struct Program {
    QString id;      // desktop file id ("steam.desktop") or "exe:<name>" for driver profiles
    QString name;
    QString exe;     // process name the driver matches
    QString icon;
    QString desktopPath; // the file Plasma uses (user copy first)
    bool installed = true;
};

inline QString userApps()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications");
}

inline QString exeOf(const QString &exec)
{
    // the program in an Exec line: skips "env A=B", quotes and field codes
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    QStringList parts = exec.split(ws, Qt::SkipEmptyParts);
    while (!parts.isEmpty()) {
        QString p = parts.takeFirst();
        p.remove(QLatin1Char('"'));
        if (p == QLatin1String("env") || p.contains(QLatin1Char('=')) || p.startsWith(QLatin1Char('%'))) {
            continue;
        }
        return QFileInfo(p).fileName();
    }
    return {};
}

inline QList<Program> installedPrograms()
{
    QMap<QString, Program> byId;
    const QStringList dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    // later directories have lower priority: walk them backwards so the user copy wins
    for (int i = dirs.size() - 1; i >= 0; --i) {
        QDirIterator it(dirs.at(i), {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QSettings d(path, QSettings::IniFormat);
            d.beginGroup(QStringLiteral("Desktop Entry"));
            if (d.value(QStringLiteral("Type")).toString() != QLatin1String("Application") || d.value(QStringLiteral("NoDisplay")).toString() == QLatin1String("true")
                || d.value(QStringLiteral("Hidden")).toString() == QLatin1String("true")) {
                byId.remove(QDir(dirs.at(i)).relativeFilePath(path).replace(QLatin1Char('/'), QLatin1Char('-')));
                continue;
            }
            Program p;
            p.id = QDir(dirs.at(i)).relativeFilePath(path).replace(QLatin1Char('/'), QLatin1Char('-'));
            p.name = d.value(QStringLiteral("Name[uk]"), d.value(QStringLiteral("Name"))).toString();
            p.exe = exeOf(d.value(QStringLiteral("Exec")).toString());
            p.icon = d.value(QStringLiteral("Icon")).toString();
            p.desktopPath = path;
            if (!p.name.isEmpty() && !p.exe.isEmpty()) {
                byId.insert(p.id, p);
            }
        }
    }
    QList<Program> list = byId.values();
    std::sort(list.begin(), list.end(), [](const Program &a, const Program &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return list;
}

/** Process names that NVIDIA's own profiles know (the list without "show only programs found") */
inline QStringList driverProfilePrograms()
{
    QStringList out;
    QDirIterator it(QStringLiteral("/usr/share/nvidia"), {QStringLiteral("nvidia-application-profiles-*-rc")}, QDir::Files);
    while (it.hasNext()) {
        QFile f(it.next());
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        static const QRegularExpression re(QStringLiteral("\"feature\"\\s*:\\s*\"procname\"\\s*,\\s*\"matches\"\\s*:\\s*\"([^\"]+)\""));
        auto m = re.globalMatch(QString::fromUtf8(f.readAll()));
        while (m.hasNext()) {
            out << m.next().captured(1);
        }
    }
    out.removeDuplicates();
    out.sort(Qt::CaseInsensitive);
    return out;
}

/** The GPU a program is set to in its .desktop file */
inline Gpu programGpu(const Program &p)
{
    if (p.desktopPath.isEmpty() || !p.desktopPath.startsWith(userApps())) {
        return GpuGlobal;
    }
    QSettings d(p.desktopPath, QSettings::IniFormat);
    d.beginGroup(QStringLiteral("Desktop Entry"));
    if (d.value(QStringLiteral("X-Win7NV-Global")).toString() == QLatin1String("1") || !d.contains(QStringLiteral("PrefersNonDefaultGPU"))) {
        return GpuGlobal;
    }
    return d.value(QStringLiteral("PrefersNonDefaultGPU")).toString() == QLatin1String("true") ? GpuNvidia : GpuIntegrated;
}

/** Reads a .desktop file as lines (QSettings would rewrite it in its own way) */
inline QStringList readLines(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
}

inline void writeLines(const QString &path, QStringList lines)
{
    while (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(lines.join(QLatin1Char('\n')).toUtf8() + '\n');
    }
}

/** Sets or removes keys of [Desktop Entry] */
inline void setDesktopKeys(QStringList &lines, const QMap<QString, QString> &keys)
{
    int start = lines.indexOf(QStringLiteral("[Desktop Entry]"));
    if (start < 0) {
        return;
    }
    int end = start + 1;
    while (end < lines.size() && !lines.at(end).startsWith(QLatin1Char('['))) {
        ++end;
    }
    for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
        int found = -1;
        for (int i = start + 1; i < end; ++i) {
            if (lines.at(i).section(QLatin1Char('='), 0, 0).trimmed() == it.key()) {
                found = i;
            }
        }
        if (it.value().isNull()) {
            if (found >= 0) {
                lines.removeAt(found);
                --end;
            }
        } else if (found >= 0) {
            lines[found] = it.key() + QLatin1Char('=') + it.value();
        } else {
            int at = end;
            while (at > start + 1 && lines.at(at - 1).trimmed().isEmpty()) {
                --at;
            }
            lines.insert(at, it.key() + QLatin1Char('=') + it.value());
            ++end;
        }
    }
}

/** Puts a GPU choice into the program's .desktop file (a user copy, made when needed) */
inline void setProgramGpu(Program &p, Gpu gpu, bool fromGlobal = false)
{
    const QString user = userApps() + QLatin1Char('/') + p.id;
    const bool haveUser = QFile::exists(user);
    QStringList lines = readLines(haveUser ? user : p.desktopPath);
    if (lines.isEmpty()) {
        return;
    }
    const bool ourCopy = haveUser && readLines(user).contains(QStringLiteral("X-Win7NV-Copy=1"));
    if (gpu == GpuGlobal) {
        if (!haveUser) {
            return;
        }
        if (ourCopy) {
            QFile::remove(user); // back to the system file
            p.desktopPath = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, p.id);
            return;
        }
        setDesktopKeys(lines, {{QStringLiteral("PrefersNonDefaultGPU"), QString()}, {QStringLiteral("X-Win7NV-Global"), QString()}});
        writeLines(user, lines);
        return;
    }
    QMap<QString, QString> keys{{QStringLiteral("PrefersNonDefaultGPU"), gpu == GpuNvidia ? QStringLiteral("true") : QStringLiteral("false")},
                                {QStringLiteral("X-Win7NV-Global"), fromGlobal ? QStringLiteral("1") : QString()}};
    if (!haveUser) {
        keys.insert(QStringLiteral("X-Win7NV-Copy"), QStringLiteral("1"));
    }
    setDesktopKeys(lines, keys);
    QDir().mkpath(userApps());
    writeLines(user, lines);
    p.desktopPath = user;
}

/* ---------------- the panel's own settings ---------------- */

inline QSettings &store()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/seven-nvidia.conf"), QSettings::IniFormat);
    return s;
}

/* ---------------- application profiles ---------------- */

inline QString profilesPath()
{
    return QDir::homePath() + QStringLiteral("/.nv/nvidia-application-profiles-rc");
}

inline const QString &prefix()
{
    static const QString p = QStringLiteral("Win7CP: ");
    return p;
}

/** The profile keys (and values) of a set of choices */
inline QJsonArray profileSettings(const Values &values, bool osd)
{
    QJsonArray out;
    QMap<QString, QJsonValue> keys;
    for (const Feature &f : features()) {
        if (f.key.isEmpty() || f.id == QLatin1String("aamode")) {
            continue;
        }
        int idx = values.value(f.id, f.def);
        if (f.id == QLatin1String("aaset") && values.value(QStringLiteral("aamode"), 0) == 0) {
            idx = 0; // the application decides
        }
        const QJsonValue v = f.choices.value(idx).value;
        if (!v.isUndefined()) {
            keys.insert(f.key, v);
        }
    }
    if (osd) {
        keys.insert(QStringLiteral("GLShowGraphicsOSD"), true);
    }
    for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
        out.append(QJsonObject{{QStringLiteral("key"), it.key()}, {QStringLiteral("value"), it.value()}});
    }
    return out;
}

/** Writes the global and per-program profiles, keeping anything in the file that is not ours */
inline bool writeProfiles(const Values &global, const QMap<QString, QPair<QString, Values>> &programs /* id -> exe, values */, bool osd)
{
    QJsonArray rules, profiles;
    QFile in(profilesPath());
    if (in.open(QIODevice::ReadOnly)) {
        const QJsonObject old = QJsonDocument::fromJson(in.readAll()).object();
        for (const QJsonValue &r : old.value(QStringLiteral("rules")).toArray()) {
            if (!r.toObject().value(QStringLiteral("profile")).toString().startsWith(prefix())) {
                rules.append(r);
            }
        }
        for (const QJsonValue &p : old.value(QStringLiteral("profiles")).toArray()) {
            if (!p.toObject().value(QStringLiteral("name")).toString().startsWith(prefix())) {
                profiles.append(p);
            }
        }
        in.close();
    }
    // measured with the driver (595, PRIME offload): the first matching rule wins, and "procname" never matches
    // here while "cmdline" does (NVIDIA's own profiles use it too, also for Proton's .exe). So: each program's
    // own profile first, matched by cmdline, then the global one for every other process.
    QStringList exes;
    for (auto it = programs.cbegin(); it != programs.cend(); ++it) {
        const QString exe = it.value().first;
        if (exe.isEmpty() || exes.contains(exe)) {
            continue;
        }
        exes << exe;
        Values merged = global;
        for (auto v = it.value().second.cbegin(); v != it.value().second.cend(); ++v) {
            if (v.value() >= 0) {
                merged.insert(v.key(), v.value());
            }
        }
        const QString name = prefix() + exe;
        profiles.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("settings"), profileSettings(merged, osd)}});
        rules.prepend(QJsonObject{{QStringLiteral("pattern"), QJsonObject{{QStringLiteral("feature"), QStringLiteral("cmdline")}, {QStringLiteral("matches"), exe}}},
                                  {QStringLiteral("profile"), name}});
    }
    const QJsonArray globalSettings = profileSettings(global, osd);
    if (!globalSettings.isEmpty()) {
        const QString name = prefix() + QStringLiteral("global");
        profiles.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("settings"), globalSettings}});
        rules.append(QJsonObject{{QStringLiteral("pattern"), QJsonObject{{QStringLiteral("feature"), QStringLiteral("true")}, {QStringLiteral("matches"), QString()}}},
                                 {QStringLiteral("profile"), name}});
    }
    QDir().mkpath(QFileInfo(profilesPath()).absolutePath());
    if (rules.isEmpty() && profiles.isEmpty()) {
        QFile::remove(profilesPath());
        return true;
    }
    QFile out(profilesPath());
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    out.write(QJsonDocument(QJsonObject{{QStringLiteral("rules"), rules}, {QStringLiteral("profiles"), profiles}}).toJson());
    return true;
}

/* ---------------- GPU information ---------------- */

inline QString run(const QString &program, const QStringList &args, int timeout = 4000)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(timeout)) {
        p.kill();
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

/** nvidia-smi --query-gpu=<fields>, one value per field ("[N/A]" -> empty) */
inline QStringList smi(const QStringList &fields)
{
    const QString out = run(QStringLiteral("nvidia-smi"), {QStringLiteral("--query-gpu=") + fields.join(QLatin1Char(',')), QStringLiteral("--format=csv,noheader,nounits")});
    QStringList v = out.section(QLatin1Char('\n'), 0, 0).split(QLatin1Char(','));
    for (QString &s : v) {
        s = s.trimmed();
        if (s.startsWith(QLatin1Char('[')) || s == QLatin1String("N/A")) {
            s.clear();
        }
    }
    while (v.size() < fields.size()) {
        v << QString();
    }
    return v;
}

inline QString gpuAttribute(const QString &name)
{
    return run(QStringLiteral("nvidia-settings"), {QStringLiteral("-t"), QStringLiteral("-q"), QStringLiteral("[gpu:0]/") + name});
}

inline QString gpuName()
{
    static const QString n = [] {
        QString s = smi({QStringLiteral("name")}).value(0);
        s.remove(QStringLiteral("NVIDIA "));
        return s.isEmpty() ? QStringLiteral("GeForce") : s;
    }();
    return n;
}

inline QString integratedName()
{
    static const QString n = [] {
        const QString lspci = run(QStringLiteral("lspci"), {});
        for (const QString &l : lspci.split(QLatin1Char('\n'))) {
            if ((l.contains(QLatin1String("VGA")) || l.contains(QLatin1String("Display"))) && !l.contains(QLatin1String("NVIDIA"))) {
                if (l.contains(QLatin1String("AMD")) || l.contains(QLatin1String("ATI"))) {
                    return QStringLiteral("AMD Radeon(TM) Graphics");
                }
                if (l.contains(QLatin1String("Intel"))) {
                    return QStringLiteral("Intel(R) HD Graphics");
                }
            }
        }
        return tr7("Integrated graphics");
    }();
    return n;
}

inline int powerMizerMode()
{
    bool ok = false;
    const int v = gpuAttribute(QStringLiteral("GPUPowerMizerMode")).toInt(&ok);
    return ok ? v : 2;
}

inline bool setPowerMizerMode(int mode)
{
    QProcess p;
    p.start(QStringLiteral("nvidia-settings"), {QStringLiteral("-a"), QStringLiteral("[gpu:0]/GPUPowerMizerMode=%1").arg(mode)});
    return p.waitForFinished(6000) && p.exitCode() == 0;
}
}
