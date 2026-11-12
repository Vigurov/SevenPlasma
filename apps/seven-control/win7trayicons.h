/*
 * "Notification Area Icons" and "System Icons" pages of the Windows 7 style Control Panel.
 * They edit the AeroThemePlasma system tray through the plasmashell scripting interface:
 *   Show icon and notifications  -> shownItems
 *   Hide icon and notifications  -> hiddenItems
 *   Only show notifications      -> neither (the tray shows the icon only when it needs attention)
 * System icons switch the clock widget and the volume / network / battery tray plasmoids on or off.
 * There is no Action Center on Linux, so that row is left out.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7chrome.h"

#include <QCheckBox>
#include <QCollator>
#include <QComboBox>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QtEndian>

namespace Win7Tray
{
enum {
    IconsView = 95,
    SystemView = 96,
};

struct Nav {
    std::function<void(int)> open;
    std::function<void()> done; // OK / Cancel: back, or close when opened from the tray
};

inline const QString kTray = QStringLiteral("io.gitgud.wackyideas.systemtray");
inline const QString kClock = QStringLiteral("io.gitgud.wackyideas.digitalclocklite");
inline const QString kVolume = QStringLiteral("io.gitgud.wackyideas.volume");
inline const QString kNetwork = QStringLiteral("io.gitgud.wackyideas.networkmanagement");
inline const QString kBattery = QStringLiteral("io.gitgud.wackyideas.battery");

inline QSettings &cache()
{
    static QSettings s(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/seven-trayiconsrc"), QSettings::IniFormat);
    return s;
}

inline QString runScript(const QString &js)
{
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"), QStringLiteral("org.kde.PlasmaShell"),
                                                    QStringLiteral("evaluateScript"));
    m << js;
    const QDBusReply<QString> r = QDBusConnection::sessionBus().call(m, QDBus::Block, 5000);
    return r.isValid() ? r.value() : QString();
}

// finds the panel and the tray widget; scripts below use `p` and `w`
inline QString findTray()
{
    return QStringLiteral("var p = null, w = null; var ps = panels(); for (var i = 0; i < ps.length && !w; ++i) {"
                          " var ws = ps[i].widgets('%1'); if (ws.length) { p = ps[i]; w = ws[0]; } }"
                          " function asList(v) { if (!v) return []; if (typeof v === 'string') return v.split(','); return v; }")
        .arg(kTray);
}

struct TrayState {
    bool valid = false;
    QStringList shown, hidden, extra, disabled;
    bool showAll = false;
    bool clock = false;
};

inline TrayState readTray()
{
    TrayState st;
    const QString out = runScript(findTray()
                                  + QStringLiteral(" if (w) { w.currentConfigGroup = ['General']; print(JSON.stringify({"
                                                   " shown: asList(w.readConfig('shownItems')), hidden: asList(w.readConfig('hiddenItems')),"
                                                   " extra: asList(w.readConfig('extraItems')), disabled: asList(w.readConfig('disabledStatusNotifiers')),"
                                                   " all: String(w.readConfig('showAllItems')), clock: p.widgets('%1').length > 0 })); }")
                                        .arg(kClock));
    const QJsonObject o = QJsonDocument::fromJson(out.toUtf8()).object();
    if (o.isEmpty()) {
        return st;
    }
    auto list = [&](const char *k) {
        QStringList l;
        for (const QJsonValue &v : o.value(QLatin1String(k)).toArray()) {
            if (!v.toString().isEmpty()) {
                l << v.toString();
            }
        }
        return l;
    };
    st.valid = true;
    st.shown = list("shown");
    st.hidden = list("hidden");
    st.extra = list("extra");
    st.disabled = list("disabled");
    st.showAll = o.value(QLatin1String("all")).toString() == QLatin1String("true");
    st.clock = o.value(QLatin1String("clock")).toBool();
    return st;
}

inline QString jsList(const QStringList &l)
{
    QJsonArray a;
    for (const QString &s : l) {
        a.append(s);
    }
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

inline void writeTray(const TrayState &st)
{
    runScript(findTray()
              + QStringLiteral(" if (w) { w.currentConfigGroup = ['General']; w.writeConfig('shownItems', %1); w.writeConfig('hiddenItems', %2);"
                               " w.writeConfig('extraItems', %3); w.writeConfig('showAllItems', %4); w.reloadConfig(); }")
                    .arg(jsList(st.shown), jsList(st.hidden), jsList(st.extra), st.showAll ? QStringLiteral("true") : QStringLiteral("false")));
}

/** Clock off: remember where it was and its settings, then take it off the panel */
inline void removeClock()
{
    const QString out = runScript(findTray()
                                  + QStringLiteral(" if (p) { var cs = p.widgets('%1'); if (cs.length) { var c = cs[0]; var cfg = {};"
                                                   " var groups = ['General', 'Appearance'];"
                                                   " for (var g = 0; g < groups.length; ++g) { c.currentConfigGroup = [groups[g]]; var ks = c.configKeys; var m = {};"
                                                   " for (var k = 0; k < ks.length; ++k) m[ks[k]] = c.readConfig(ks[k]); cfg[groups[g]] = m; }"
                                                   " p.currentConfigGroup = ['General'];"
                                                   " print(JSON.stringify({ x: c.geometry.x, y: c.geometry.y, id: c.id, order: String(p.readConfig('AppletOrder')), cfg: cfg }));"
                                                   " c.remove(); } }")
                                        .arg(kClock));
    if (!out.isEmpty()) {
        cache().setValue(QStringLiteral("Clock/Saved"), out);
        cache().sync();
    }
}

/** Clock on: put it back next to the same neighbour, with the same settings */
inline void addClock()
{
    const QJsonObject saved = QJsonDocument::fromJson(cache().value(QStringLiteral("Clock/Saved")).toString().toUtf8()).object();
    const QString cfg = QString::fromUtf8(QJsonDocument(saved.value(QLatin1String("cfg")).toObject()).toJson(QJsonDocument::Compact));
    // the panel inserts a new widget before the one under the left half of x, after it for the right half
    runScript(findTray()
              + QStringLiteral(" if (p && p.widgets('%1').length == 0) {"
                               " var order = '%2'.split(';'); var at = order.indexOf('%3'); var x = 100000;"
                               " var next = at >= 0 && at + 1 < order.length ? p.widgetById(parseInt(order[at + 1])) : null;"
                               " var prev = at > 0 ? p.widgetById(parseInt(order[at - 1])) : null;"
                               " if (next) x = next.geometry.x + 2; else if (prev) x = prev.geometry.x + prev.geometry.width - 2;"
                               " var c = p.addWidget('%1', x, 2, 66, 36); var cfg = %4;"
                               " for (var g in cfg) { c.currentConfigGroup = [g]; for (var k in cfg[g]) c.writeConfig(k, cfg[g][k]); }"
                               " c.reloadConfig(); }")
                    .arg(kClock, saved.value(QLatin1String("order")).toString())
                    .arg(saved.value(QLatin1String("id")).toInt(-1))
                    .arg(cfg.isEmpty() ? QStringLiteral("{}") : cfg));
}

inline bool hasBattery()
{
    for (const QString &d : QDir(QStringLiteral("/sys/class/power_supply")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (d.startsWith(QLatin1String("BAT"))) {
            return true;
        }
    }
    return false;
}

inline QString run(const QString &cmd, const QStringList &args)
{
    QProcess p;
    p.start(cmd, args);
    if (!p.waitForFinished(1500)) {
        p.kill();
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

// ---- what the rows show

struct Item {
    QString id;
    QString name;
    QString detail;
    QIcon icon;
};

inline QJsonObject plasmoidMeta(const QString &id)
{
    const QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("plasma/plasmoids/") + id, QStandardPaths::LocateDirectory);
    for (const QString &d : dirs) {
        QFile f(d + QStringLiteral("/metadata.json"));
        if (f.open(QIODevice::ReadOnly)) {
            return QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("KPlugin")).toObject();
        }
    }
    for (const QString &base : QCoreApplication::libraryPaths()) {
        const QString so = base + QStringLiteral("/plasma/applets/") + id + QStringLiteral(".so");
        if (QFile::exists(so)) {
            return QPluginLoader(so).metaData().value(QLatin1String("MetaData")).toObject().value(QLatin1String("KPlugin")).toObject();
        }
    }
    return {};
}

inline QString localized(const QJsonObject &o, const QString &key)
{
    return Win7I18n::localizedValue(o, key);
}

inline QString volumeDetail()
{
    const QString out = run(QStringLiteral("pactl"), {QStringLiteral("get-sink-volume"), QStringLiteral("@DEFAULT_SINK@")});
    const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\d+)%")).match(out);
    const QString mute = run(QStringLiteral("pactl"), {QStringLiteral("get-sink-mute"), QStringLiteral("@DEFAULT_SINK@")});
    if (mute.contains(QLatin1String("yes"))) {
        return tr7("Speakers: Muted");
    }
    return m.hasMatch() ? tr7("Speakers: %1%").arg(m.captured(1)) : tr7("Speakers");
}

inline QString networkDetail()
{
    QString name;
    for (const QString &line : run(QStringLiteral("nmcli"), {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME,TYPE"), QStringLiteral("connection"), QStringLiteral("show"), QStringLiteral("--active")}).split(QLatin1Char('\n'))) {
        if (!line.isEmpty() && !line.contains(QLatin1String("loopback")) && !line.contains(QLatin1String("bridge"))) {
            name = line.section(QLatin1Char(':'), 0, 0);
            break;
        }
    }
    if (name.isEmpty()) {
        return tr7("Not connected", "tray");
    }
    const bool internet = run(QStringLiteral("nmcli"), {QStringLiteral("networking"), QStringLiteral("connectivity")}) == QLatin1String("full");
    return QStringLiteral("%1: %2").arg(name, internet ? tr7("Internet access") : tr7("No Internet access", "tray"));
}

inline QString batteryDetail()
{
    for (const QString &d : QDir(QStringLiteral("/sys/class/power_supply")).entryList({QStringLiteral("BAT*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        auto read = [&](const char *f) {
            QFile file(QStringLiteral("/sys/class/power_supply/%1/%2").arg(d, QLatin1String(f)));
            return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.readAll()).trimmed() : QString();
        };
        const QString status = read("status");
        const QString state = status == QLatin1String("Charging") ? tr7(" (plugged in, charging)")
            : status == QLatin1String("Discharging")              ? QString()
                                                                  : tr7(" (plugged in)");
        return tr7("%1% available%2").arg(read("capacity"), state);
    }
    return {};
}

inline QIcon pixmapIcon(const QVariant &v)
{
    QIcon icon;
    if (!v.canConvert<QDBusArgument>()) {
        return icon;
    }
    const QDBusArgument arg = v.value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        int w = 0, h = 0;
        QByteArray data;
        arg.beginStructure();
        arg >> w >> h >> data;
        arg.endStructure();
        if (w <= 0 || h <= 0 || data.size() < w * h * 4) {
            continue;
        }
        QImage img(w, h, QImage::Format_ARGB32);
        const quint32 *src = reinterpret_cast<const quint32 *>(data.constData());
        for (int i = 0; i < w * h; ++i) {
            reinterpret_cast<quint32 *>(img.bits())[i] = qFromBigEndian(src[i]);
        }
        icon.addPixmap(QPixmap::fromImage(img));
    }
    arg.endArray();
    return icon;
}

inline QString tooltipText(const QVariant &v)
{
    if (!v.canConvert<QDBusArgument>()) {
        return {};
    }
    const QDBusArgument arg = v.value<QDBusArgument>();
    QString iconName, title, sub;
    arg.beginStructure();
    arg >> iconName;
    arg.beginArray();
    while (!arg.atEnd()) {
        int w, h;
        QByteArray d;
        arg.beginStructure();
        arg >> w >> h >> d;
        arg.endStructure();
    }
    arg.endArray();
    arg >> title >> sub;
    arg.endStructure();
    sub.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    return title.isEmpty() ? sub : title;
}

inline QString iconCacheFile(const QString &id)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/seven-trayicons");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + QString(id).replace(QLatin1Char('/'), QLatin1Char('_')) + QStringLiteral(".png");
}

/** Running StatusNotifier items (programs), remembered so they stay in the list like in Windows 7 */
inline QList<Item> statusNotifiers(const TrayState &st)
{
    QList<Item> items;
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.kde.StatusNotifierWatcher"), QStringLiteral("/StatusNotifierWatcher"),
                                                    QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    m << QStringLiteral("org.kde.StatusNotifierWatcher") << QStringLiteral("RegisteredStatusNotifierItems");
    const QDBusMessage r = QDBusConnection::sessionBus().call(m, QDBus::Block, 2000);
    QStringList registered;
    if (!r.arguments().isEmpty()) {
        registered = r.arguments().first().value<QDBusVariant>().variant().toStringList();
    }
    QSet<QString> seen;
    for (const QString &entry : std::as_const(registered)) {
        const int slash = entry.indexOf(QLatin1Char('/'));
        const QString service = slash < 0 ? entry : entry.left(slash);
        const QString path = slash < 0 ? QStringLiteral("/StatusNotifierItem") : entry.mid(slash);
        QDBusMessage get = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
        get << QStringLiteral("org.kde.StatusNotifierItem");
        const QDBusMessage reply = QDBusConnection::sessionBus().call(get, QDBus::Block, 1500);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            continue;
        }
        QVariantMap props;
        reply.arguments().first().value<QDBusArgument>() >> props;
        const QString id = props.value(QStringLiteral("Id")).toString();
        if (id.isEmpty() || seen.contains(id) || st.disabled.contains(id)) {
            continue;
        }
        seen.insert(id);
        Item it;
        it.id = id;
        it.name = props.value(QStringLiteral("Title")).toString();
        if (it.name.isEmpty() || it.name == id) { // nameless (e.g. Electron): use the program name
            const uint pid = QDBusConnection::sessionBus().interface()->servicePid(service);
            QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
            if (comm.open(QIODevice::ReadOnly)) {
                it.name = QString::fromUtf8(comm.readAll()).trimmed();
                if (!it.name.isEmpty()) {
                    it.name[0] = it.name.at(0).toUpper();
                }
            }
        }
        it.detail = tooltipText(props.value(QStringLiteral("ToolTip")));
        const QString iconName = props.value(QStringLiteral("IconName")).toString();
        it.icon = iconName.isEmpty() ? pixmapIcon(props.value(QStringLiteral("IconPixmap"))) : QIcon::fromTheme(iconName);
        if (it.icon.isNull()) {
            it.icon = QIcon::fromTheme(it.name.toLower(), QIcon::fromTheme(QStringLiteral("application-x-executable")));
        }
        it.icon.pixmap(16, 16).save(iconCacheFile(id));
        cache().setValue(QStringLiteral("Items/%1").arg(id), it.name);
        items << it;
    }
    // programs that are not running now but already have a behaviour
    for (const QString &id : st.shown + st.hidden) {
        if (seen.contains(id) || id.contains(QLatin1Char('.'))) { // dotted ids are plasmoids
            continue;
        }
        QString name = cache().value(QStringLiteral("Items/%1").arg(id)).toString();
        if (name.isEmpty()) {
            static const QHash<QString, QString> known{{QStringLiteral("steam"), QStringLiteral("Steam")},
                                                       {QStringLiteral("TelegramDesktop"), QStringLiteral("Telegram")},
                                                       {QStringLiteral("discord_status_icon_1"), QStringLiteral("Discord")},
                                                       {QStringLiteral("chrome_status_icon_1"), QStringLiteral("Google Chrome")},
                                                       {QStringLiteral("Antigravity_status_icon_1"), QStringLiteral("Antigravity")}};
            name = known.value(id);
        }
        if (name.isEmpty()) {
            continue; // e.g. numeric ids of old windows
        }
        seen.insert(id);
        Item it;
        it.id = id;
        it.name = name;
        it.icon = QFile::exists(iconCacheFile(id)) ? QIcon(iconCacheFile(id))
                                                   : QIcon::fromTheme(name.toLower().remove(QLatin1Char(' ')),
                                                                      QIcon::fromTheme(name.toLower().replace(QLatin1Char(' '), QLatin1Char('-')),
                                                                                       QIcon::fromTheme(QStringLiteral("application-x-executable"))));
        items << it;
    }
    cache().sync();
    return items;
}

inline QList<Item> trayItems(const TrayState &st)
{
    QList<Item> items;
    for (const QString &id : st.extra) {
        const QJsonObject meta = plasmoidMeta(id);
        if (meta.isEmpty()) {
            continue;
        }
        Item it;
        it.id = id;
        it.name = localized(meta, QStringLiteral("Name"));
        it.detail = localized(meta, QStringLiteral("Description"));
        it.icon = QIcon::fromTheme(meta.value(QLatin1String("Icon")).toString(), QIcon::fromTheme(QStringLiteral("plasma")));
        if (id == kVolume) {
            it.name = tr7("Volume", "tray");
            it.detail = volumeDetail();
            it.icon = QIcon::fromTheme(QStringLiteral("seven-tray-volume"));
        } else if (id == kNetwork) {
            it.name = tr7("Network");
            it.detail = networkDetail();
            it.icon = QIcon::fromTheme(QStringLiteral("seven-tray-network"));
        } else if (id == kBattery) {
            it.name = tr7("Power");
            it.detail = batteryDetail();
            it.icon = QIcon::fromTheme(QStringLiteral("seven-tray-power"));
        }
        items << it;
    }
    items += statusNotifiers(st);
    QCollator c(QLocale{});
    std::sort(items.begin(), items.end(), [&c](const Item &a, const Item &b) {
        return c.compare(a.name, b.name) < 0;
    });
    return items;
}

// ---- page pieces

inline QLabel *title(const QString &text)
{
    auto *l = new QLabel(text);
    QFont f = l->font();
    f.setPixelSize(16);
    l->setFont(f);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: #1e3287;"));
    return l;
}

inline QLabel *plain(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    return l;
}

inline QLabel *bold(const QString &text)
{
    auto *l = new QLabel(text);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}

/** The framed list with the column headings (Icons / Behaviors) */
struct ListBox {
    QFrame *frame;
    QGridLayout *grid;
    int row = 1;
};

inline ListBox makeListBox(const QString &left, const QString &right, int behaviourCol)
{
    ListBox box;
    box.frame = new QFrame;
    box.frame->setObjectName(QStringLiteral("win7traybox"));
    box.frame->setStyleSheet(QStringLiteral("#win7traybox { border: 1px solid #646464; background: #ffffff; }"));
    auto *outer = new QVBoxLayout(box.frame);
    outer->setContentsMargins(1, 1, 1, 1);
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    box.frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *inner = new QWidget;
    inner->setObjectName(QStringLiteral("win7trayinner"));
    inner->setStyleSheet(QStringLiteral("#win7trayinner { background: #ffffff; }"));
    box.grid = new QGridLayout(inner);
    box.grid->setContentsMargins(40, 18, 40, 18);
    box.grid->setHorizontalSpacing(0);
    box.grid->setVerticalSpacing(0);
    box.grid->setColumnMinimumWidth(0, behaviourCol);
    box.grid->setColumnStretch(1, 1);
    auto *headL = bold(left);
    auto *headR = bold(right);
    for (QLabel *h : {headL, headR}) {
        h->setStyleSheet(QStringLiteral("QLabel { border-bottom: 1px solid #dcdcdc; padding-bottom: 3px; }"));
    }
    box.grid->addWidget(headL, 0, 0);
    box.grid->addWidget(headR, 0, 1);
    scroll->setWidget(inner);
    outer->addWidget(scroll);
    return box;
}

inline QWidget *iconCell(const QIcon &icon, const QString &name, const QString &detail)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(6, 0, 12, 0);
    h->setSpacing(8);
    auto *ic = new QLabel;
    ic->setPixmap(icon.pixmap(16, 16));
    ic->setAlignment(Qt::AlignTop);
    ic->setContentsMargins(0, 2, 0, 0);
    h->addWidget(ic, 0, Qt::AlignTop);
    auto *v = new QVBoxLayout;
    v->setSpacing(0);
    v->setSizeConstraint(QLayout::SetFixedSize);
    auto *n = new QLabel(name);
    v->addWidget(n);
    if (!detail.isEmpty()) {
        auto *d = new QLabel;
        QFont f = d->font();
        f.setItalic(true);
        d->setFont(f);
        d->setStyleSheet(QStringLiteral("color: #6d6d6d;"));
        d->setText(QFontMetrics(f).elidedText(detail, Qt::ElideRight, 250));
        d->setToolTip(detail);
        v->addWidget(d);
    }
    h->addLayout(v, 1);
    return w;
}

/** White page with the content column and the grey OK / Cancel band at the bottom */
struct Page {
    QWidget *page;
    QVBoxLayout *col;
    QPushButton *ok;
    QPushButton *cancel;
};

inline Page makePage()
{
    Page p;
    p.page = new QWidget;
    p.page->setObjectName(QStringLiteral("win7page"));
    p.page->setAttribute(Qt::WA_StyledBackground, true);
    p.page->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *outer = new QVBoxLayout(p.page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *body = new QWidget;
    auto *bh = new QHBoxLayout(body);
    bh->setContentsMargins(112, 22, 40, 14);
    auto *colW = new QWidget;
    colW->setMaximumWidth(732);
    p.col = new QVBoxLayout(colW);
    p.col->setContentsMargins(0, 0, 0, 0);
    p.col->setSpacing(0);
    bh->addWidget(colW, 1);
    bh->addStretch(0);
    outer->addWidget(body, 1);
    auto *band = new QWidget;
    band->setObjectName(QStringLiteral("win7band"));
    band->setAttribute(Qt::WA_StyledBackground, true);
    band->setStyleSheet(QStringLiteral("#win7band { background: #f0f0f0; border-top: 1px solid #dfdfdf; }"));
    auto *bl = new QHBoxLayout(band);
    bl->setContentsMargins(12, 12, 14, 12);
    bl->addStretch(1);
    p.ok = new QPushButton(QStringLiteral("OK"));
    p.cancel = new QPushButton(tr7("Cancel"));
    p.ok->setMinimumWidth(88);
    p.cancel->setMinimumWidth(88);
    p.ok->setDefault(true);
    bl->addWidget(p.ok);
    bl->addSpacing(4);
    bl->addWidget(p.cancel);
    outer->addWidget(band);
    return p;
}

inline Link *link(const QString &text, std::function<void()> action)
{
    auto *l = new Link(text, std::move(action));
    l->setWordWrap(false);
    return l;
}

inline const QStringList &behaviours()
{
    static const QStringList b{tr7("Show icon and notifications"), tr7("Hide icon and notifications"),
                               tr7("Only show notifications")};
    return b;
}

inline QWidget *buildIconsPage(const Nav &nav)
{
    const TrayState st = readTray();
    Page pg = makePage();
    pg.col->addWidget(title(tr7("Select which icons and notifications appear on the taskbar")));
    pg.col->addSpacing(14);
    pg.col->addWidget(plain(tr7("If you choose to hide icons and notifications, you won't be notified about changes or updates. To view hidden icons at any time, click the arrow next to the notification area on the taskbar.")));
    pg.col->addSpacing(14);
    ListBox box = makeListBox(tr7("Icons"), tr7("Behaviors"), 316);
    box.frame->setMinimumHeight(230);
    QList<QPair<QString, QComboBox *>> combos;
    if (!st.valid) {
        box.grid->addWidget(plain(tr7("Could not contact the taskbar.")), box.row++, 0, 1, 2);
    }
    for (const Item &it : trayItems(st)) {
        auto *combo = new QComboBox;
        combo->addItems(behaviours());
        combo->setCurrentIndex(st.shown.contains(it.id) ? 0 : st.hidden.contains(it.id) ? 1 : 2);
        combo->setFixedWidth(304);
        box.grid->addWidget(iconCell(it.icon, it.name, it.detail), box.row, 0, Qt::AlignVCenter);
        box.grid->addWidget(combo, box.row, 1, Qt::AlignLeft | Qt::AlignVCenter);
        box.grid->setRowMinimumHeight(box.row, 58);
        ++box.row;
        combos.append({it.id, combo});
    }
    box.grid->setRowStretch(box.row, 1);
    pg.col->addWidget(box.frame, 1);
    pg.col->addSpacing(16);
    pg.col->addWidget(link(tr7("Turn system icons on or off"), [nav] {
        nav.open(SystemView);
    }), 0, Qt::AlignLeft);
    pg.col->addSpacing(12);
    pg.col->addWidget(link(tr7("Restore default icon behaviors"), [combos] {
        for (const auto &c : combos) { // system icons are shown, programs only notify
            c.second->setCurrentIndex(c.first == kVolume || c.first == kNetwork || c.first == kBattery ? 0 : 2);
        }
    }), 0, Qt::AlignLeft);
    pg.col->addSpacing(12);
    auto *all = new QCheckBox(tr7("Always show all icons and notifications on the taskbar"));
    all->setChecked(st.showAll);
    pg.col->addWidget(all);
    auto syncEnabled = [combos, all] {
        for (const auto &c : combos) {
            c.second->setEnabled(!all->isChecked());
        }
    };
    syncEnabled();
    QObject::connect(all, &QCheckBox::toggled, pg.page, syncEnabled);
    QObject::connect(pg.cancel, &QPushButton::clicked, pg.page, [nav] {
        nav.done();
    });
    QObject::connect(pg.ok, &QPushButton::clicked, pg.page, [nav, combos, all, st] {
        if (st.valid) {
            TrayState out = st;
            for (const auto &c : combos) {
                out.shown.removeAll(c.first);
                out.hidden.removeAll(c.first);
                if (c.second->currentIndex() == 0) {
                    out.shown << c.first;
                } else if (c.second->currentIndex() == 1) {
                    out.hidden << c.first;
                }
            }
            out.showAll = all->isChecked();
            writeTray(out);
        }
        nav.done();
    });
    return pg.page;
}

inline QWidget *buildSystemPage(const Nav &nav)
{
    const TrayState st = readTray();
    const bool battery = hasBattery();
    Page pg = makePage();
    pg.col->addWidget(title(tr7("Turn system icons on or off")));
    pg.col->addSpacing(14);
    pg.col->addWidget(plain(tr7("Turning off a system icon removes it from the notification area and turns off notifications.")));
    pg.col->addSpacing(14);
    ListBox box = makeListBox(tr7("System Icons"), tr7("Behaviors"), 316);
    box.frame->setMinimumHeight(230);
    struct Row {
        QString id;
        QComboBox *combo;
    };
    QList<Row> rows;
    auto add = [&](const QString &icon, const QString &name, const QString &id, bool on, bool enabled) {
        auto *combo = new QComboBox;
        combo->addItems({tr7("On"), tr7("Off")});
        combo->setCurrentIndex(on ? 0 : 1);
        combo->setFixedWidth(70);
        combo->setEnabled(enabled);
        box.grid->addWidget(iconCell(QIcon::fromTheme(icon), name, QString()), box.row, 0, Qt::AlignVCenter);
        box.grid->addWidget(combo, box.row, 1, Qt::AlignLeft | Qt::AlignVCenter);
        box.grid->setRowMinimumHeight(box.row, 58);
        ++box.row;
        rows.append({id, combo});
    };
    add(QStringLiteral("seven-tray-clock"), tr7("Clock"), kClock, st.clock, st.valid);
    add(QStringLiteral("seven-tray-volume"), tr7("Volume", "tray"), kVolume, st.extra.contains(kVolume), st.valid);
    add(QStringLiteral("seven-tray-network"), tr7("Network"), kNetwork, st.extra.contains(kNetwork), st.valid);
    // like Windows 7 on a desktop without a battery: greyed out and off
    add(QStringLiteral("seven-tray-power"), tr7("Power"), kBattery, battery && st.extra.contains(kBattery), st.valid && battery);
    box.grid->setRowStretch(box.row, 1);
    pg.col->addWidget(box.frame, 1);
    pg.col->addSpacing(16);
    pg.col->addWidget(link(tr7("Customize notification icons"), [nav] {
        nav.open(IconsView);
    }), 0, Qt::AlignLeft);
    pg.col->addSpacing(12);
    pg.col->addWidget(link(tr7("Restore default icon behaviors"), [rows] {
        for (const Row &r : rows) {
            if (r.combo->isEnabled()) {
                r.combo->setCurrentIndex(0);
            }
        }
    }), 0, Qt::AlignLeft);
    QObject::connect(pg.cancel, &QPushButton::clicked, pg.page, [nav] {
        nav.done();
    });
    QObject::connect(pg.ok, &QPushButton::clicked, pg.page, [nav, rows, st] {
        if (st.valid) {
            TrayState out = st;
            bool trayChanged = false;
            for (const Row &r : rows) {
                const bool on = r.combo->currentIndex() == 0;
                if (r.id == kClock) {
                    if (on != st.clock) {
                        on ? addClock() : removeClock();
                    }
                    continue;
                }
                if (!r.combo->isEnabled() || on == st.extra.contains(r.id)) {
                    continue;
                }
                trayChanged = true;
                if (on) {
                    out.extra << r.id;
                } else {
                    out.extra.removeAll(r.id);
                }
            }
            if (trayChanged) {
                writeTray(out);
            }
        }
        nav.done();
    });
    return pg.page;
}
}
