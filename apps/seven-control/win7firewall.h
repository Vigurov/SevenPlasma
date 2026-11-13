/*
 * "Windows Firewall" pages of the Windows 7 style Control Panel, backed by ufw (Kubuntu, Arch) or firewalld
 * (Fedora: its default zone; the "allowed programs" are its services and ports).
 * ufw has a single set of rules for every network, so there is one network section
 * instead of Windows 7's "Home or work" / "Public" pair. Changes go through pkexec.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include "win7i18n.h"

#include "win7chrome.h"

#include <KAuth/Action>
#include <KAuth/ExecuteJob>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QTreeWidget>
#include <QXmlStreamReader>
#include <QDir>
#include <QRegularExpression>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>

namespace Win7Firewall
{
enum {
    StatusView = 90,
    SettingsView = 91,
    AllowedView = 92,
};

struct Nav {
    std::function<void(int)> open;
    std::function<void()> home;
    std::function<void()> back;
    std::function<void()> networkCenter;
};

inline QString readValue(const QString &file, const QString &key)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    for (const QByteArray &line : f.readAll().split('\n')) {
        const QString l = QString::fromUtf8(line).trimmed();
        if (l.startsWith(key + QLatin1Char('='))) {
            return l.mid(key.size() + 1).remove(QLatin1Char('"'));
        }
    }
    return {};
}

/** firewalld instead of ufw (Fedora) */
inline bool useFirewalld()
{
    static const bool fw = QStandardPaths::findExecutable(QStringLiteral("ufw"), {QStringLiteral("/usr/sbin"), QStringLiteral("/usr/bin")}).isEmpty()
        && !QStandardPaths::findExecutable(QStringLiteral("firewall-cmd")).isEmpty();
    return fw;
}

inline QString firewallCmd(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral("firewall-cmd"), args);
    p.waitForFinished(10000);
    return p.exitCode() == 0 ? QString::fromUtf8(p.readAllStandardOutput()).trimmed() : QString();
}

inline bool enabled()
{
    if (useFirewalld()) {   // systemd answers without a password (firewall-cmd may ask for one: Action Center checks often)
        QProcess p;
        p.start(QStringLiteral("systemctl"), {QStringLiteral("is-active"), QStringLiteral("firewalld")});
        p.waitForFinished(5000);
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed() == QLatin1String("active");
    }
    return readValue(QStringLiteral("/etc/ufw/ufw.conf"), QStringLiteral("ENABLED")) == QLatin1String("yes");
}

inline bool blocksIncoming()
{
    if (useFirewalld()) {
        // the target of the default zone (its file: the user's copy in /etc or the system's)
        const QString zone = firewallCmd({QStringLiteral("--get-default-zone")});
        for (const QString &dir : {QStringLiteral("/etc/firewalld/zones/"), QStringLiteral("/usr/lib/firewalld/zones/")}) {
            QFile f(dir + zone + QStringLiteral(".xml"));
            if (f.open(QIODevice::ReadOnly)) {
                static const QRegularExpression target(QStringLiteral("<zone[^>]*target=\"([^\"]+)\""));
                return target.match(QString::fromUtf8(f.readAll())).captured(1) != QLatin1String("ACCEPT");
            }
        }
        return true;
    }
    return readValue(QStringLiteral("/etc/default/ufw"), QStringLiteral("DEFAULT_INPUT_POLICY")) != QLatin1String("ACCEPT");
}

/** the commands of a change, in the firewall that is there */
inline QString cmdOn()
{
    return useFirewalld() ? QStringLiteral("systemctl enable --now firewalld") : QStringLiteral("ufw --force enable");
}
inline QString cmdOff()
{
    return useFirewalld() ? QStringLiteral("systemctl disable --now firewalld") : QStringLiteral("ufw disable");
}
inline QString cmdRecommended()
{
    return useFirewalld() ? QStringLiteral("systemctl enable --now firewalld")
                          : QStringLiteral("ufw default deny incoming && ufw default allow outgoing && ufw --force enable");
}
inline QString cmdDefaults()
{
    return useFirewalld() ? QStringLiteral("systemctl enable --now firewalld && Z=$(firewall-cmd --get-default-zone) && "
                                           "firewall-cmd --permanent --load-zone-defaults=\"$Z\" && firewall-cmd --reload")
                          : QStringLiteral("ufw --force reset && ufw default deny incoming && ufw default allow outgoing && ufw --force enable");
}

/** Runs ufw commands as root through pkexec (asks for the password like a UAC shield) */
inline bool runAsRoot(QWidget *parent, const QString &commands)
{
    QProcess p;
    QEventLoop loop;
    QObject::connect(&p, &QProcess::finished, &loop, &QEventLoop::quit);
    // firewalld: the permanent rules changed are made the running ones too
    const QString script = useFirewalld() && (commands.contains(QLatin1String("--permanent --add")) || commands.contains(QLatin1String("--permanent --remove")))
        ? QStringLiteral("%1; r=$?; firewall-cmd --reload >/dev/null 2>&1; exit $r").arg(commands)
        : commands;
    p.start(QStringLiteral("pkexec"), {QStringLiteral("sh"), QStringLiteral("-c"), script});
    if (!p.waitForStarted(3000)) {
        return false;
    }
    loop.exec();
    // 126/127: the password dialog was dismissed
    if (p.exitCode() != 0 && p.exitCode() != 126 && p.exitCode() != 127) {
        QMessageBox::warning(parent, tr7("Windows Firewall"),
                             tr7("The firewall settings could not be changed.\n\n%1").arg(QString::fromUtf8(p.readAllStandardError()).trimmed()));
    }
    return p.exitCode() == 0;
}

/* ---------------- ufw rules (read through KDE's ufw helper: no password needed) ---------------- */

struct Rule {
    int position = 0;   // ufw rule number
    QString action;     // allow / deny / reject / limit
    QString direction;  // in / out
    QString dapp;       // application profile ("CUPS")
    QString dport;
    QString sport;
    QString protocol;   // tcp / udp / any
    QString dst;
    QString src;
    bool v6 = false;
    bool operator==(const Rule &o) const
    {
        return position == o.position && action == o.action && direction == o.direction && dapp == o.dapp && dport == o.dport && sport == o.sport
            && protocol == o.protocol && dst == o.dst && src == o.src && v6 == o.v6;
    }
};

struct Ufw {
    bool ok = false;
    bool enabled = false;
    QString incoming; // deny / allow / reject
    QString outgoing;
    QList<Rule> rules;
};

/** firewalld's default zone as the rules of an ufw: its services (dapp) and ports */
inline Ufw queryFirewalld()
{
    Ufw u;
    u.enabled = enabled();
    u.incoming = blocksIncoming() ? QStringLiteral("deny") : QStringLiteral("allow");
    u.outgoing = QStringLiteral("allow");
    if (!u.enabled) {
        u.ok = true;   // nothing runs: the permanent setup is what will be
    }
    const QStringList pre = u.enabled ? QStringList() : QStringList{QStringLiteral("--permanent")};
    QProcess p;
    p.start(QStringLiteral("firewall-cmd"), pre + QStringList{QStringLiteral("--list-services")});
    p.waitForFinished(10000);
    if (p.exitCode() != 0) {
        return u;
    }
    int n = 0;
    for (const QString &svc : QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        Rule r;
        r.position = ++n;
        r.action = QStringLiteral("allow");
        r.direction = QStringLiteral("in");
        r.dapp = svc.trimmed();
        u.rules.append(r);
    }
    for (const QString &port : firewallCmd(pre + QStringList{QStringLiteral("--list-ports")}).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        Rule r;
        r.position = ++n;
        r.action = QStringLiteral("allow");
        r.direction = QStringLiteral("in");
        r.dport = port.section(QLatin1Char('/'), 0, 0).replace(QLatin1Char('-'), QLatin1Char(':'));
        r.protocol = port.section(QLatin1Char('/'), 1, 1);
        u.rules.append(r);
    }
    u.ok = true;
    return u;
}

inline Ufw queryUfw()
{
    if (useFirewalld()) {
        return queryFirewalld();
    }
    Ufw u;
    KAuth::Action a(QStringLiteral("org.kde.ufw.query"));
    a.setHelperId(QStringLiteral("org.kde.ufw"));
    a.setArguments({{QStringLiteral("defaults"), true}, {QStringLiteral("profiles"), false}});
    KAuth::ExecuteJob *job = a.execute();
    if (!job->exec()) {
        return u;
    }
    const QString xml = job->data().value(QStringLiteral("response")).toString();
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const auto at = r.attributes();
        if (r.name() == QLatin1String("status")) {
            u.enabled = at.value(QLatin1String("enabled")) == QLatin1String("true");
        } else if (r.name() == QLatin1String("defaults")) {
            u.incoming = at.value(QLatin1String("incoming")).toString();
            u.outgoing = at.value(QLatin1String("outgoing")).toString();
        } else if (r.name() == QLatin1String("rule")) {
            Rule x;
            x.position = at.value(QLatin1String("position")).toInt();
            x.action = at.value(QLatin1String("action")).toString();
            x.direction = at.value(QLatin1String("direction")).toString();
            x.dapp = at.value(QLatin1String("dapp")).toString();
            x.dport = at.value(QLatin1String("dport")).toString();
            x.sport = at.value(QLatin1String("sport")).toString();
            x.protocol = at.value(QLatin1String("protocol")).toString();
            x.dst = at.value(QLatin1String("dst")).toString();
            x.src = at.value(QLatin1String("src")).toString();
            x.v6 = at.value(QLatin1String("v6")) == QLatin1String("True");
            u.rules.append(x);
        }
    }
    u.ok = true;
    return u;
}

/** a program or feature of the "Allowed programs" list: an ufw application profile or a set of ports */
struct Feature {
    QString name;
    QString description;
    QString app;            // ufw application profile, or
    QStringList portSpecs;  // "139,445/tcp", "137,138/udp"
    bool custom = false;
};

inline QStringList appProfiles()
{
    QStringList out;
    for (const QFileInfo &fi : QDir(QStringLiteral("/etc/ufw/applications.d")).entryInfoList(QDir::Files)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        for (const QByteArray &line : f.readAll().split('\n')) {
            const QString l = QString::fromUtf8(line).trimmed();
            if (l.startsWith(QLatin1Char('[')) && l.endsWith(QLatin1Char(']'))) {
                out << l.mid(1, l.size() - 2);
            }
        }
    }
    return out;
}

/** names given to rules added here ("Allow another program..."): port spec -> name */
inline KConfigGroup namesConfig()
{
    return KConfigGroup(KSharedConfig::openConfig(QStringLiteral("seven-firewallrc")), QStringLiteral("Names"));
}

inline QList<Feature> features()
{
    QList<Feature> f{
        {tr7("File and Printer Sharing"), tr7("Lets other computers on the network open your shared folders and printers (Samba)."),
         QString(), {QStringLiteral("137,138/udp"), QStringLiteral("139,445/tcp")}},
        {tr7("Network Discovery"), tr7("Lets this computer find other network devices and be visible to them (mDNS)."),
         QString(), {QStringLiteral("5353/udp")}},
        {tr7("Remote Desktop"), tr7("Lets people connect to this computer with Remote Desktop Connection (RDP)."),
         QString(), {QStringLiteral("3389/tcp")}},
        {tr7("Remote Management (SSH)"), tr7("Lets people log on to this computer through the SSH secure shell."), QString(), {QStringLiteral("22/tcp")}},
        {tr7("Steam (Remote Play and local network transfers)"), tr7("Game streaming and transferring games between the computers on your network."),
         QString(), {QStringLiteral("27036/tcp"), QStringLiteral("27031:27036/udp")}},
    };
    if (useFirewalld()) {
        // firewalld knows these as services
        f = {{tr7("File and Printer Sharing"), tr7("Lets other computers on the network open your shared folders and printers (Samba)."), QStringLiteral("samba"), {}},
             {tr7("Network Discovery"), tr7("Lets this computer find other network devices and be visible to them (mDNS)."), QStringLiteral("mdns"), {}},
             {tr7("Remote Desktop"), tr7("Lets people connect to this computer with Remote Desktop Connection (RDP)."), QStringLiteral("rdp"), {}},
             {tr7("Remote Management (SSH)"), tr7("Lets people log on to this computer through the SSH secure shell."), QStringLiteral("ssh"), {}},
             {tr7("Print Service (CUPS)"), tr7("Lets other computers print on the printers of this computer."), QStringLiteral("ipp"), {}},
             {QStringLiteral("KDE Connect"), tr7("Connection with your phone: notifications, files, clipboard."), QStringLiteral("kdeconnect"), {}},
             f.at(4)};   // Steam: its ports
    }
    const QHash<QString, QPair<QString, QString>> known{
        {QStringLiteral("CUPS"), {tr7("Print Service (CUPS)"), tr7("Lets other computers print on the printers of this computer.")}},
        {QStringLiteral("KDEConnect"), {QStringLiteral("KDE Connect"), tr7("Connection with your phone: notifications, files, clipboard.")}},
    };
    for (const QString &app : useFirewalld() ? QStringList() : appProfiles()) {
        const auto k = known.value(app, {app, tr7("ufw application profile \"%1\".").arg(app)});
        f.append({k.first, k.second, app, {}});
    }
    const KConfigGroup names = namesConfig();
    for (const QString &spec : names.keyList()) {
        f.append({names.readEntry(spec, spec), tr7("A program added manually."), QString(), spec.split(QLatin1Char(' ')), true});
    }
    return f;
}

/** "139,445/tcp" as an ufw rule: dport + protocol */
inline bool ruleMatches(const Rule &r, const QString &spec)
{
    const QString ports = spec.section(QLatin1Char('/'), 0, 0);
    const QString proto = spec.section(QLatin1Char('/'), 1, 1);
    return r.direction == QLatin1String("in") && (r.action == QLatin1String("allow") || r.action == QLatin1String("limit")) && r.dapp.isEmpty()
        && r.dport == ports && (proto.isEmpty() ? (r.protocol.isEmpty() || r.protocol == QLatin1String("any")) : r.protocol == proto);
}

inline bool featureAllowed(const Feature &f, const QList<Rule> &rules)
{
    for (const Rule &r : rules) {
        if (!f.app.isEmpty() && r.dapp == f.app && r.direction == QLatin1String("in") && r.action == QLatin1String("allow")) {
            return true;
        }
    }
    if (f.portSpecs.isEmpty()) {
        return false;
    }
    for (const QString &spec : f.portSpecs) {
        bool found = false;
        for (const Rule &r : rules) {
            found = found || ruleMatches(r, spec);
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

inline QString quoted(QString s)
{
    return QLatin1Char('\'') + s.replace(QLatin1Char('\''), QStringLiteral("'\\''")) + QLatin1Char('\'');
}

inline QStringList featureCommands(const Feature &f, bool allow)
{
    QStringList cmds;
    if (useFirewalld()) {
        const QString verb = allow ? QStringLiteral("firewall-cmd --permanent --add-") : QStringLiteral("firewall-cmd --permanent --remove-");
        if (!f.app.isEmpty()) {
            cmds << verb + QStringLiteral("service=") + quoted(f.app);
        }
        for (const QString &spec : f.portSpecs) {   // "139,445/tcp" -> one port (or range) each
            const QString proto = spec.section(QLatin1Char('/'), 1, 1);
            for (const QString &port : spec.section(QLatin1Char('/'), 0, 0).split(QLatin1Char(','))) {
                for (const QString &pr : proto.isEmpty() ? QStringList{QStringLiteral("tcp"), QStringLiteral("udp")} : QStringList{proto}) {
                    cmds << verb + QStringLiteral("port=") + QString(port).replace(QLatin1Char(':'), QLatin1Char('-')) + QLatin1Char('/') + pr;
                }
            }
        }
        return cmds;
    }
    const QString verb = allow ? QStringLiteral("ufw allow ") : QStringLiteral("ufw delete allow ");
    if (!f.app.isEmpty()) {
        cmds << verb + quoted(f.app);
    }
    for (const QString &spec : f.portSpecs) {
        cmds << verb + spec + (allow ? QStringLiteral(" comment ") + quoted(QStringLiteral("Win7: ") + f.name) : QString());
    }
    return cmds;
}

/** "Allow another program": a name and its ports */
inline bool addProgramDialog(QWidget *parent, QString *name, QString *spec)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr7("Add a Program"));
    dlg.setWindowIcon(QIcon::fromTheme(QStringLiteral("seven-cpi-firewall")));
    dlg.setFixedWidth(430);
    auto *v = new QVBoxLayout(&dlg);
    auto *intro = new QLabel(tr7("Enter the name of the program and the ports it accepts connections on. The ports of a program can be found in its documentation."));
    intro->setWordWrap(true);
    v->addWidget(intro);
    auto *g = new QGridLayout;
    auto *nameEdit = new QLineEdit;
    auto *ports = new QLineEdit;
    ports->setPlaceholderText(tr7("for example: 8080 or 6000:6010"));
    auto *proto = new QComboBox;
    proto->addItem(QStringLiteral("TCP"), QStringLiteral("tcp"));
    proto->addItem(QStringLiteral("UDP"), QStringLiteral("udp"));
    proto->addItem(tr7("TCP and UDP"), QString());
    g->addWidget(new QLabel(tr7("Program:")), 0, 0);
    g->addWidget(nameEdit, 0, 1);
    g->addWidget(new QLabel(tr7("Ports:")), 1, 0);
    g->addWidget(ports, 1, 1);
    g->addWidget(new QLabel(tr7("Protocol:")), 2, 0);
    g->addWidget(proto, 2, 1);
    v->addLayout(g);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *ok = new QPushButton(tr7("Add"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    ok->setEnabled(false);
    row->addWidget(ok);
    row->addWidget(cancel);
    v->addLayout(row);
    static const QRegularExpression valid(QStringLiteral("^\\d{1,5}(:\\d{1,5})?(,\\d{1,5}(:\\d{1,5})?)*$"));
    auto check = [=] {
        ok->setEnabled(!nameEdit->text().trimmed().isEmpty() && valid.match(ports->text().remove(QLatin1Char(' '))).hasMatch());
    };
    QObject::connect(nameEdit, &QLineEdit::textChanged, &dlg, check);
    QObject::connect(ports, &QLineEdit::textChanged, &dlg, check);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    *name = nameEdit->text().trimmed();
    const QString p = ports->text().remove(QLatin1Char(' '));
    *spec = proto->currentData().toString().isEmpty() ? p + QStringLiteral("/tcp ") + p + QStringLiteral("/udp") : p + QLatin1Char('/') + proto->currentData().toString();
    return true;
}

inline QStringList activeNetworks()
{
    QProcess p;
    p.start(QStringLiteral("nmcli"), {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME,TYPE"), QStringLiteral("con"), QStringLiteral("show"), QStringLiteral("--active")});
    p.waitForFinished(3000);
    QStringList names;
    for (const QString &line : QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString type = line.section(QLatin1Char(':'), -1);
        if (type == QLatin1String("loopback") || type == QLatin1String("bridge") || type == QLatin1String("tun")) {
            continue;
        }
        names << line.section(QLatin1Char(':'), 0, -2).replace(QStringLiteral("\\:"), QStringLiteral(":"));
    }
    return names;
}

inline QLabel *title(const QString &text, int px = 16)
{
    auto *l = new QLabel(text);
    QFont f = l->font();
    f.setPixelSize(px);
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

/** Network section: coloured bar, shield, title and "Connected" on the header, details below */
inline QWidget *networkSection(bool on)
{
    auto *box = new QFrame;
    box->setObjectName(QStringLiteral("win7fwbox"));
    box->setStyleSheet(QStringLiteral("#win7fwbox { border: 1px solid #c5d3e3; background: #ffffff; }"
                                      "#win7fwhead { border-bottom: 1px solid #c5d3e3; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #f2f6fb); }"));
    auto *v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *head = new QWidget;
    head->setObjectName(QStringLiteral("win7fwhead"));
    head->setAttribute(Qt::WA_StyledBackground, true);
    auto *hh = new QHBoxLayout(head);
    hh->setContentsMargins(0, 0, 10, 0);
    hh->setSpacing(8);
    auto *bar = new QFrame;
    bar->setFixedSize(22, 34);
    bar->setStyleSheet(on ? QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3fa63a, stop:1 #1f7a1d);")
                          : QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #e2453c, stop:1 #a8180f);"));
    hh->addWidget(bar);
    auto *shield = new QLabel;
    shield->setPixmap(QIcon::fromTheme(on ? QStringLiteral("seven-shield-green") : QStringLiteral("seven-shield-red")).pixmap(24, 24));
    hh->addWidget(shield);
    hh->addWidget(title(tr7("All networks"), 15), 1);
    auto *connected = title(activeNetworks().isEmpty() ? tr7("Not connected") : tr7("Connected"), 15);
    hh->addWidget(connected);
    v->addWidget(head);

    auto *desc = plain(tr7("The firewall works the same way for all networks this computer is connected to."));
    desc->setContentsMargins(8, 8, 8, 8);
    v->addWidget(desc);
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: #e3e9f1;"));
    v->addWidget(line);

    auto *grid = new QGridLayout;
    grid->setContentsMargins(8, 10, 8, 12);
    grid->setHorizontalSpacing(30);
    grid->setVerticalSpacing(10);
    int r = 0;
    auto row = [&](const QString &k, QWidget *val) {
        auto *kl = new QLabel(k);
        kl->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        grid->addWidget(kl, r, 0, Qt::AlignTop);
        grid->addWidget(val, r, 1, Qt::AlignTop);
        ++r;
    };
    row(tr7("Windows Firewall state:"), new QLabel(on ? tr7("On", "state") : tr7("Off", "firewall state")));
    if (on) {
        row(tr7("Incoming connections:"),
            plain(blocksIncoming() ? tr7("Block all connections to programs that are not on the list of allowed programs")
                                   : tr7("Allow all incoming connections")));
    }
    auto *nets = new QWidget;
    auto *nv = new QVBoxLayout(nets);
    nv->setContentsMargins(0, 0, 0, 0);
    nv->setSpacing(2);
    const QStringList active = activeNetworks();
    for (const QString &n : active) {
        auto *nr = new QHBoxLayout;
        auto *house = new QLabel;
        house->setPixmap(QIcon::fromTheme(QStringLiteral("network-type-home")).pixmap(16, 16));
        nr->addWidget(house);
        nr->addWidget(new QLabel(n), 1);
        nv->addLayout(nr);
    }
    if (active.isEmpty()) {
        nv->addWidget(new QLabel(tr7("None")));
    }
    row(tr7("Active networks:"), nets);
    grid->setColumnStretch(1, 1);
    v->addLayout(grid);
    return box;
}

inline QWidget *sidePane(const Nav &nav, QWidget *page)
{
    auto *side = new QWidget;
    side->setObjectName(QStringLiteral("win7side"));
    side->setAttribute(Qt::WA_StyledBackground, true);
    side->setFixedWidth(200);
    auto *sv = new QVBoxLayout(side);
    sv->setContentsMargins(4, 16, 10, 14);
    sv->setSpacing(7);
    const QString color = QStringLiteral("#1e3a5f");
    auto addRow = [sv, color](const QString &text, std::function<void()> open, bool shield) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        auto *ic = new QLabel;
        ic->setFixedWidth(18);
        if (shield) {
            ic->setPixmap(QIcon::fromTheme(QStringLiteral("seven-cp-shield")).pixmap(16, 16));
        }
        ic->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        row->addWidget(ic, 0, Qt::AlignTop);
        row->addWidget(new Link(text, std::move(open), color), 1);
        sv->addLayout(row);
    };
    addRow(tr7("Control Panel Home"), nav.home, false);
    sv->addSpacing(9);
    addRow(tr7("Allow a program or feature through Windows Firewall"), [nav] {
        nav.open(AllowedView);
    }, false);
    addRow(tr7("Turn Windows Firewall on or off"), [nav] {
        nav.open(SettingsView);
    }, true);
    addRow(tr7("Restore defaults"), [nav, page] {
        QMessageBox box(QMessageBox::Warning, tr7("Restore default settings"),
                        tr7("Restore the default firewall settings?"), QMessageBox::NoButton, page);
        box.setInformativeText(tr7("All rules you added will be removed; the firewall will be turned on and will block incoming connections."));
        QPushButton *yes = box.addButton(tr7("Yes"), QMessageBox::YesRole);
        box.addButton(tr7("No"), QMessageBox::NoRole);
        box.exec();
        if (box.clickedButton() == yes
            && runAsRoot(page, cmdDefaults())) {
            nav.open(StatusView);
        }
    }, true);
    addRow(tr7("More Options"), [] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--wf")});
    }, true);
    sv->addStretch(1);
    auto *seeRow = new QHBoxLayout;
    seeRow->setContentsMargins(22, 0, 0, 0);
    seeRow->addWidget(grey(tr7("See also")));
    sv->addLayout(seeRow);
    addRow(tr7("Network and Sharing Center"), nav.networkCenter, false);
    return side;
}

inline QWidget *withSide(QWidget *side, QWidget *main)
{
    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("win7page"));
    root->setAttribute(Qt::WA_StyledBackground, true);
    root->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"
                                       "#win7side { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #e3ecf8, stop:0.5 #eef3fb, stop:1 #f3f7fc);"
                                       " border-right: 1px solid #e0e8f3; }"));
    auto *h = new QHBoxLayout(root);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(side);
    h->addWidget(main, 1);
    return root;
}

inline QWidget *buildStatusPage(const Nav &nav)
{
    const bool on = enabled();
    auto *main = new QWidget;
    auto *mv = new QVBoxLayout(main);
    mv->setContentsMargins(24, 18, 24, 20);
    mv->setSpacing(0);
    auto *content = new QWidget;
    content->setMaximumWidth(600);
    auto *cv = new QVBoxLayout(content);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(title(tr7("Help protect your computer with Windows Firewall")));
    cv->addSpacing(8);
    cv->addWidget(plain(tr7("Windows Firewall can help prevent hackers or malicious software from gaining access to your computer through the Internet or a network.")));
    cv->addSpacing(14);

    if (!on) {
        // Windows 7 shows this red box when the firewall is off
        auto *warn = new QFrame;
        warn->setObjectName(QStringLiteral("win7fwwarn"));
        warn->setStyleSheet(QStringLiteral("#win7fwwarn { border: 1px solid #c5d3e3; background: #ffffff; }"));
        auto *wh = new QHBoxLayout(warn);
        wh->setContentsMargins(0, 0, 10, 0);
        auto *bar = new QFrame;
        bar->setFixedWidth(22);
        bar->setStyleSheet(QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #e2453c, stop:1 #a8180f);"));
        wh->addWidget(bar);
        auto *shield = new QLabel;
        shield->setPixmap(QIcon::fromTheme(QStringLiteral("seven-shield-red")).pixmap(32, 32));
        shield->setContentsMargins(6, 10, 0, 0); // breathing room from the bar and the top edge
        wh->addWidget(shield, 0, Qt::AlignTop);
        auto *wv = new QVBoxLayout;
        wv->setContentsMargins(4, 10, 0, 10);
        wv->addWidget(title(tr7("Update your Firewall settings"), 15));
        wv->addWidget(plain(tr7("Windows Firewall is not using the recommended settings to protect your computer.")));
        wh->addLayout(wv, 1);
        auto *use = new QPushButton(QIcon::fromTheme(QStringLiteral("seven-cp-shield")), tr7("Use recommended settings"));
        wh->addWidget(use, 0, Qt::AlignVCenter);
        QObject::connect(use, &QPushButton::clicked, main, [main, nav] {
            if (runAsRoot(main, cmdRecommended())) {
                nav.open(StatusView);
            }
        });
        cv->addWidget(warn);
        cv->addSpacing(10);
    }
    cv->addWidget(networkSection(on));
    mv->addWidget(content);
    mv->addStretch(1);
    return withSide(sidePane(nav, main), main);
}

/** "Customize settings": turn the firewall on or off */
inline QWidget *buildSettingsPage(const Nav &nav)
{
    const bool on = enabled();
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("win7page"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 18, 0, 20);
    outer->setSpacing(0);
    const int width = 580;
    auto *col = new QWidget;
    col->setFixedWidth(width);
    auto *cv = new QVBoxLayout(col);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(title(tr7("Customize settings for each type of network")));
    cv->addSpacing(6);
    cv->addWidget(plain(tr7("You can turn the firewall on or off for the networks this computer is connected to.")));
    cv->addSpacing(16);
    cv->addWidget(new SectionHeader(tr7("Settings for all networks")));
    cv->addSpacing(10);
    auto *group = new QButtonGroup(page);
    auto addChoice = [&](const QString &icon, const QString &text, bool checked, int id) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(16, 0, 0, 0);
        auto *ic = new QLabel;
        ic->setPixmap(QIcon::fromTheme(icon).pixmap(24, 24));
        row->addWidget(ic);
        auto *r = new QRadioButton(text);
        r->setChecked(checked);
        group->addButton(r, id);
        row->addWidget(r, 1);
        cv->addLayout(row);
        cv->addSpacing(10);
    };
    addChoice(QStringLiteral("seven-shield-green"), tr7("Turn on Windows Firewall", "option"), on, 1);
    addChoice(QStringLiteral("seven-shield-red"), tr7("Turn off Windows Firewall (not recommended)"), !on, 0);
    outer->addWidget(col, 0, Qt::AlignHCenter);
    outer->addSpacing(16);
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: #e3e3e3;"));
    outer->addWidget(line);
    outer->addSpacing(10);
    auto *btns = new QWidget;
    btns->setFixedWidth(width);
    auto *bl = new QHBoxLayout(btns);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    ok->setMinimumWidth(80);
    cancel->setMinimumWidth(80);
    bl->addWidget(ok);
    bl->addWidget(cancel);
    outer->addWidget(btns, 0, Qt::AlignHCenter);
    outer->addStretch(1);
    QObject::connect(cancel, &QPushButton::clicked, page, [nav] {
        nav.back();
    });
    QObject::connect(ok, &QPushButton::clicked, page, [=] {
        const bool want = group->checkedId() == 1;
        if (want == on || runAsRoot(page, want ? cmdOn() : cmdOff())) {
            nav.open(StatusView);
        }
    });
    return page;
}

/** "Allow a program or feature through Windows Firewall" */
inline QWidget *buildAllowedPage(const Nav &nav)
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("win7page"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setStyleSheet(QStringLiteral("#win7page { background: #ffffff; }"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 18, 0, 20);
    outer->setSpacing(0);
    const int width = 620;
    auto *col = new QWidget;
    col->setFixedWidth(width);
    auto *cv = new QVBoxLayout(col);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(title(tr7("Allow programs to communicate through Windows Firewall")));
    cv->addSpacing(6);
    cv->addWidget(plain(tr7("To add, change, or remove allowed programs and ports, click Change settings.")));
    cv->addSpacing(8);
    auto *riskRow = new QHBoxLayout;
    auto *risk = new Link(tr7("What are the risks of allowing a program to communicate?"), [page] {
        QMessageBox::information(page, tr7("Windows Firewall"),
                                 tr7("Each allowed program or port opens one more way into your computer from the network. Allow only the programs you trust, and remove the permission when it is no longer needed."));
    });
    risk->setWordWrap(false);
    riskRow->addWidget(risk);
    riskRow->addStretch(1);
    auto *change = new QPushButton(QIcon::fromTheme(QStringLiteral("seven-cp-shield")), tr7("Change settings"));
    riskRow->addWidget(change);
    cv->addLayout(riskRow);
    cv->addSpacing(10);

    const Ufw u = queryUfw();
    cv->addWidget(new QLabel(tr7("Allowed programs and features:")));
    cv->addSpacing(4);
    auto *list = new QTreeWidget;
    list->setRootIsDecorated(false);
    list->setHeaderLabels({tr7("Name", "firewall"), tr7("All networks")});
    list->header()->setStretchLastSection(false);
    list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list->setColumnWidth(1, 110);
    list->setMinimumHeight(260);
    list->setEnabled(false);
    const QList<Feature> feats = features();
    QList<bool> before;
    for (int i = 0; i < feats.size(); ++i) {
        const bool on = featureAllowed(feats.at(i), u.rules);
        before << on;
        auto *it = new QTreeWidgetItem(list, {feats.at(i).name});
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
        it->setCheckState(1, on ? Qt::Checked : Qt::Unchecked);
        it->setData(0, Qt::UserRole, i);
    }
    list->sortItems(0, Qt::AscendingOrder);
    // both boxes of a row move together: ufw has one set of rules for every network
    QObject::connect(list, &QTreeWidget::itemChanged, list, [list](QTreeWidgetItem *it, int column) {
        const Qt::CheckState st = it->checkState(column);
        QSignalBlocker block(list);
        it->setCheckState(column == 0 ? 1 : 0, st);
    });
    cv->addWidget(list);
    if (!u.ok) {
        auto *warn = plain(tr7("The firewall rules could not be read."));
        warn->setStyleSheet(QStringLiteral("color: #8d1414;"));
        cv->addWidget(warn);
    }
    cv->addSpacing(8);
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto *details = new QPushButton(tr7("Details..."));
    auto *remove = new QPushButton(tr7("Delete"));
    for (QPushButton *b : {details, remove}) {
        b->setMinimumWidth(90);
        b->setEnabled(false);
        btnRow->addWidget(b);
    }
    cv->addLayout(btnRow);
    cv->addSpacing(14);
    auto *addRow = new QHBoxLayout;
    addRow->addStretch(1);
    auto *add = new QPushButton(tr7("Allow another program..."));
    add->setEnabled(false);
    addRow->addWidget(add);
    cv->addLayout(addRow);
    // wrapped labels get their height from the known column width
    for (QLabel *l : col->findChildren<QLabel *>()) {
        if (l->wordWrap()) {
            l->setMinimumHeight(l->heightForWidth(width));
        }
    }
    col->setMinimumHeight(cv->sizeHint().height());
    outer->addWidget(col, 0, Qt::AlignHCenter);
    outer->addSpacing(16);
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: #e3e3e3;"));
    outer->addWidget(line);
    outer->addSpacing(10);
    auto *btns = new QWidget;
    btns->setFixedWidth(width);
    auto *bl = new QHBoxLayout(btns);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addStretch(1);
    auto *ok = new QPushButton(QStringLiteral("OK"));
    auto *cancel = new QPushButton(tr7("Cancel"));
    ok->setMinimumWidth(80);
    cancel->setMinimumWidth(80);
    bl->addWidget(ok);
    bl->addWidget(cancel);
    outer->addWidget(btns, 0, Qt::AlignHCenter);
    outer->addStretch(1);


    auto *removed = new QStringList; // custom programs to forget
    QObject::connect(page, &QObject::destroyed, [removed] {
        delete removed;
    });
    auto updateButtons = [list, details, remove, feats] {
        QTreeWidgetItem *it = list->currentItem();
        details->setEnabled(it && list->isEnabled());
        remove->setEnabled(it && list->isEnabled() && feats.value(it->data(0, Qt::UserRole).toInt()).custom);
    };
    QObject::connect(change, &QPushButton::clicked, page, [=] {
        list->setEnabled(true);
        add->setEnabled(true);
        change->setEnabled(false);
        updateButtons();
    });
    QObject::connect(list, &QTreeWidget::currentItemChanged, page, updateButtons);
    QObject::connect(details, &QPushButton::clicked, page, [=] {
        const Feature f = feats.value(list->currentItem()->data(0, Qt::UserRole).toInt());
        QString what = f.app.isEmpty() ? tr7("Ports: ") + f.portSpecs.join(QStringLiteral(", ")).toUpper() : (useFirewalld() ? tr7("firewalld service: ") : tr7("ufw profile: ")) + f.app;
        QMessageBox box(QMessageBox::Information, tr7("Edit a Program"), f.name, QMessageBox::Ok, page);
        box.setInformativeText(f.description + QStringLiteral("\n\n") + what);
        box.exec();
    });
    QObject::connect(remove, &QPushButton::clicked, page, [=] {
        QTreeWidgetItem *it = list->currentItem();
        const Feature f = feats.value(it->data(0, Qt::UserRole).toInt());
        if (QMessageBox::question(page, tr7("Uninstall a program"), tr7("Remove the program \"%1\" from the list?").arg(f.name)) != QMessageBox::Yes) {
            return;
        }
        removed->append(f.portSpecs.join(QLatin1Char(' ')));
        it->setCheckState(0, Qt::Unchecked);
        it->setHidden(true);
    });
    QObject::connect(add, &QPushButton::clicked, page, [=] {
        QString name, spec;
        if (!addProgramDialog(page, &name, &spec)) {
            return;
        }
        // applied together with the other changes on OK
        if (runAsRoot(page, featureCommands({name, QString(), QString(), spec.split(QLatin1Char(' ')), true}, true).join(QStringLiteral(" && ")))) {
            KConfigGroup g = namesConfig();
            g.writeEntry(spec, name);
            g.sync();
            nav.open(AllowedView);
        }
    });
    QObject::connect(cancel, &QPushButton::clicked, page, [nav] {
        nav.back();
    });
    QObject::connect(ok, &QPushButton::clicked, page, [=] {
        QStringList cmds;
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *it = list->topLevelItem(i);
            const int idx = it->data(0, Qt::UserRole).toInt();
            const bool want = it->checkState(0) == Qt::Checked;
            if (want != before.value(idx)) {
                cmds += featureCommands(feats.at(idx), want);
            }
        }
        if (!cmds.isEmpty() && !runAsRoot(page, cmds.join(QStringLiteral("; ")))) {
            return;
        }
        KConfigGroup g = namesConfig();
        for (const QString &spec : *removed) {
            g.deleteEntry(spec);
        }
        g.sync();
        nav.open(StatusView);
    });
    return page;
}
}
